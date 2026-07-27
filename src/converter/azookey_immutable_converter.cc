// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.

#include "converter/azookey_immutable_converter.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "converter/segments.h"
#include "request/options.h"

// Simple JSON parsing for candidates array
// New format: [{"text": "candidate1", "correspondingCount": 6}, ...]
namespace {

// Count UTF-8 characters (not bytes)
size_t CountUtf8Characters(const std::string& utf8_str) {
  size_t count = 0;
  for (size_t i = 0; i < utf8_str.size(); ) {
    unsigned char c = static_cast<unsigned char>(utf8_str[i]);
    if ((c & 0x80) == 0) {
      // ASCII (0xxxxxxx)
      i += 1;
    } else if ((c & 0xE0) == 0xC0) {
      // 2-byte sequence (110xxxxx)
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      // 3-byte sequence (1110xxxx) - includes hiragana/katakana/kanji
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      // 4-byte sequence (11110xxx)
      i += 4;
    } else {
      // Invalid UTF-8, skip one byte
      i += 1;
    }
    ++count;
  }
  return count;
}

// Get the first N UTF-8 characters from a string
std::string GetUtf8Prefix(const std::string& utf8_str, size_t char_count) {
  size_t byte_pos = 0;
  size_t chars_processed = 0;
  while (byte_pos < utf8_str.size() && chars_processed < char_count) {
    unsigned char c = static_cast<unsigned char>(utf8_str[byte_pos]);
    if ((c & 0x80) == 0) {
      byte_pos += 1;
    } else if ((c & 0xE0) == 0xC0) {
      byte_pos += 2;
    } else if ((c & 0xF0) == 0xE0) {
      byte_pos += 3;
    } else if ((c & 0xF8) == 0xF0) {
      byte_pos += 4;
    } else {
      byte_pos += 1;
    }
    ++chars_processed;
  }
  return utf8_str.substr(0, byte_pos);
}

// Get the substring after the first N UTF-8 characters
std::string GetUtf8Suffix(const std::string& utf8_str, size_t skip_char_count) {
  size_t byte_pos = 0;
  size_t chars_processed = 0;
  while (byte_pos < utf8_str.size() && chars_processed < skip_char_count) {
    unsigned char c = static_cast<unsigned char>(utf8_str[byte_pos]);
    if ((c & 0x80) == 0) {
      byte_pos += 1;
    } else if ((c & 0xE0) == 0xC0) {
      byte_pos += 2;
    } else if ((c & 0xF0) == 0xE0) {
      byte_pos += 3;
    } else if ((c & 0xF8) == 0xF0) {
      byte_pos += 4;
    } else {
      byte_pos += 1;
    }
    ++chars_processed;
  }
  // 末尾で切れた不正UTF-8で byte_pos がサイズを超えると substr が
  // std::out_of_range を投げるためクランプする
  return utf8_str.substr(std::min(byte_pos, utf8_str.size()));
}

// JSON の \uXXXX エスケープをデコードして UTF-8 を value に追記する。
// 呼び出し時 json[pos] は 'u'。消費した分 pos を進める（呼び出し側の ++pos が
// 最後の1文字分を消費する規約）。サロゲートペア対応。
// 以前は読み飛ばすだけで文字が無言で欠落していた。
void AppendUnicodeEscape(const std::string& json, size_t& pos,
                         std::string& value) {
  auto read_hex4 = [&json](size_t p, unsigned int* out) -> bool {
    if (p + 4 > json.size()) return false;
    unsigned int v = 0;
    for (size_t i = 0; i < 4; ++i) {
      const char c = json[p + i];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= c - '0';
      } else if (c >= 'a' && c <= 'f') {
        v |= c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        v |= c - 'A' + 10;
      } else {
        return false;
      }
    }
    *out = v;
    return true;
  };

  unsigned int code = 0;
  if (!read_hex4(pos + 1, &code)) {
    return;  // 不正なエスケープは無視（pos は進めず 'u' を通常文字扱いしない）
  }
  pos += 4;  // XXXX を消費（'u' は呼び出し側の ++pos が消費）

  // サロゲートペア（絵文字等の BMP 外文字）
  if (code >= 0xD800 && code <= 0xDBFF) {
    unsigned int low = 0;
    if (pos + 2 < json.size() && json[pos + 1] == '\\' &&
        json[pos + 2] == 'u' && read_hex4(pos + 3, &low) && low >= 0xDC00 &&
        low <= 0xDFFF) {
      code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
      pos += 6;  // \uXXXX を消費
    } else {
      code = 0xFFFD;  // 不完全なサロゲートは置換文字
    }
  } else if (code >= 0xDC00 && code <= 0xDFFF) {
    code = 0xFFFD;
  }

  // UTF-8 エンコード
  if (code < 0x80) {
    value += static_cast<char>(code);
  } else if (code < 0x800) {
    value += static_cast<char>(0xC0 | (code >> 6));
    value += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    value += static_cast<char>(0xE0 | (code >> 12));
    value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    value += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    value += static_cast<char>(0xF0 | (code >> 18));
    value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    value += static_cast<char>(0x80 | (code & 0x3F));
  }
}

struct CandidateInfo {
  std::string text;
  int corresponding_count;  // Number of hiragana characters this candidate covers
};

// Parse JSON object array with text and correspondingCount
std::vector<CandidateInfo> ParseJsonCandidateArray(const std::string& json) {
  std::vector<CandidateInfo> result;

  if (json.empty() || json[0] != '[') {
    return result;
  }

  size_t pos = 1;
  while (pos < json.size()) {
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r')) {
      ++pos;
    }

    if (pos >= json.size() || json[pos] == ']') {
      break;
    }

    // Skip comma
    if (json[pos] == ',') {
      ++pos;
      continue;
    }

    // Expect opening brace for object
    if (json[pos] != '{') {
      break;
    }
    ++pos;

    CandidateInfo info;
    info.corresponding_count = 0;

    // Parse object contents
    while (pos < json.size() && json[pos] != '}') {
      // Skip whitespace
      while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                    json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
      }

      if (pos >= json.size() || json[pos] == '}') {
        break;
      }

      // Skip comma
      if (json[pos] == ',') {
        ++pos;
        continue;
      }

      // Expect key in quotes
      if (json[pos] != '"') {
        break;
      }
      ++pos;

      // Read key
      std::string key;
      while (pos < json.size() && json[pos] != '"') {
        key += json[pos];
        ++pos;
      }
      if (pos < json.size()) ++pos;  // Skip closing quote

      // Skip whitespace and colon
      while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) {
        ++pos;
      }

      if (key == "text") {
        // Read string value
        if (pos < json.size() && json[pos] == '"') {
          ++pos;
          std::string value;
          while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
              ++pos;
              switch (json[pos]) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case 'u': {
                  AppendUnicodeEscape(json, pos, value);
                  break;
                }
                default: value += json[pos]; break;
              }
            } else {
              value += json[pos];
            }
            ++pos;
          }
          if (pos < json.size()) ++pos;  // Skip closing quote
          info.text = std::move(value);
        }
      } else if (key == "correspondingCount") {
        // Read integer value
        int value = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
          value = value * 10 + (json[pos] - '0');
          ++pos;
        }
        info.corresponding_count = value;
      } else {
        // Skip unknown value
        if (pos < json.size() && json[pos] == '"') {
          ++pos;
          while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
            ++pos;
          }
          if (pos < json.size()) ++pos;
        } else {
          while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
            ++pos;
          }
        }
      }
    }

    if (pos < json.size() && json[pos] == '}') {
      ++pos;  // Skip closing brace
    }

    if (!info.text.empty()) {
      result.push_back(std::move(info));
    }
  }

  return result;
}

// Legacy parser for simple string array (kept for compatibility)
std::vector<std::string> ParseJsonStringArray(const std::string& json) {
  std::vector<std::string> result;

  if (json.empty() || json[0] != '[') {
    return result;
  }

  size_t pos = 1;
  while (pos < json.size()) {
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r')) {
      ++pos;
    }

    if (pos >= json.size() || json[pos] == ']') {
      break;
    }

    // Expect opening quote
    if (json[pos] != '"') {
      // Skip comma
      if (json[pos] == ',') {
        ++pos;
        continue;
      }
      break;
    }
    ++pos;

    // Read string content
    std::string value;
    while (pos < json.size() && json[pos] != '"') {
      if (json[pos] == '\\' && pos + 1 < json.size()) {
        // Handle escape sequences
        ++pos;
        switch (json[pos]) {
          case 'n': value += '\n'; break;
          case 't': value += '\t'; break;
          case 'r': value += '\r'; break;
          case '\\': value += '\\'; break;
          case '"': value += '"'; break;
          case 'u': {
            AppendUnicodeEscape(json, pos, value);
            break;
          }
          default: value += json[pos]; break;
        }
      } else {
        value += json[pos];
      }
      ++pos;
    }

    if (pos < json.size() && json[pos] == '"') {
      ++pos;  // Skip closing quote
    }

    if (!value.empty()) {
      result.push_back(std::move(value));
    }
  }

  return result;
}

}  // namespace

namespace mozc {

// Dynamic DLL loader for AzooKey engine
class AzooKeyDllLoader {
 public:
  static AzooKeyDllLoader& GetInstance() {
    static AzooKeyDllLoader instance;
    return instance;
  }

  bool IsLoaded() const { return dll_handle_ != nullptr; }

  // Function pointers
  // Initialize は成功時 1 / 失敗時 0 を返す（参照カウント方式）
  using InitializeFunc = int (*)(const char*, const char*);
  using ShutdownFunc = void (*)();
  // ConvertText(key, allowLearning): 1呼び出しで候補JSONを返す単発API。
  // DLL側グローバル状態への複数呼び出しシーケンスを排除しスレッド競合を防ぐ
  using ConvertTextFunc = const char* (*)(const char*, int);
  using FreeStringFunc = void (*)(const char*);
  using SetZenzaiEnabledFunc = void (*)(bool);
  using SetZenzaiUseGpuFunc = void (*)(bool);
  using SetZenzaiInferenceLimitFunc = void (*)(int);
  using SetZenzaiWeightPathFunc = void (*)(const char*);

  InitializeFunc Initialize = nullptr;
  ShutdownFunc Shutdown = nullptr;
  ConvertTextFunc ConvertText = nullptr;
  FreeStringFunc FreeString = nullptr;
  SetZenzaiEnabledFunc SetZenzaiEnabled = nullptr;
  SetZenzaiUseGpuFunc SetZenzaiUseGpu = nullptr;
  SetZenzaiInferenceLimitFunc SetZenzaiInferenceLimit = nullptr;
  SetZenzaiWeightPathFunc SetZenzaiWeightPath = nullptr;

 private:
  AzooKeyDllLoader() {
    LoadDll();
  }

  ~AzooKeyDllLoader() {
    UnloadDll();
  }

  void LoadDll() {
#ifdef _WIN32
    // Try to load from the same directory as the executable
    wchar_t module_path[MAX_PATH];
    HMODULE hModule = nullptr;

    // Get handle to the current module (mozc_server.exe or mozc_tip64.dll)
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&AzooKeyDllLoader::GetInstance),
                           &hModule)) {
      GetModuleFileNameW(hModule, module_path, MAX_PATH);

      // Remove the filename to get directory
      wchar_t* last_slash = wcsrchr(module_path, L'\\');
      if (last_slash) {
        *last_slash = L'\0';
      }

      // Construct full path to azookey-engine.dll
      std::wstring dll_path = std::wstring(module_path) + L"\\azookey-engine.dll";
      dll_handle_ = LoadLibraryW(dll_path.c_str());
    }

    // NOTE: 相対名での LoadLibraryW フォールバックは行わない。
    // 既定のDLL検索順はカレントディレクトリを含むため、DLLプリロード攻撃面になる。
    // 正規インストールでは DLL は必ずモジュールと同じディレクトリに存在する。
    if (!dll_handle_) {
      DWORD error = GetLastError();
      LOG(ERROR) << "Failed to load azookey-engine.dll, error code: " << error;
      return;
    }

    LOG(INFO) << "Successfully loaded azookey-engine.dll";

    // Load function pointers
    Initialize = reinterpret_cast<InitializeFunc>(
        GetProcAddress(dll_handle_, "Initialize"));
    Shutdown = reinterpret_cast<ShutdownFunc>(
        GetProcAddress(dll_handle_, "Shutdown"));
    ConvertText = reinterpret_cast<ConvertTextFunc>(
        GetProcAddress(dll_handle_, "ConvertText"));
    FreeString = reinterpret_cast<FreeStringFunc>(
        GetProcAddress(dll_handle_, "FreeString"));
    SetZenzaiEnabled = reinterpret_cast<SetZenzaiEnabledFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiEnabled"));
    SetZenzaiUseGpu = reinterpret_cast<SetZenzaiUseGpuFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiUseGpu"));
    SetZenzaiInferenceLimit = reinterpret_cast<SetZenzaiInferenceLimitFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiInferenceLimit"));
    SetZenzaiWeightPath = reinterpret_cast<SetZenzaiWeightPathFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiWeightPath"));

    // Check if essential functions are loaded
    if (!Initialize || !ConvertText || !FreeString) {
      LOG(ERROR) << "Failed to load essential functions from azookey-engine.dll";
      LOG(ERROR) << "Initialize: " << (Initialize ? "OK" : "MISSING");
      LOG(ERROR) << "ConvertText: " << (ConvertText ? "OK" : "MISSING");
      LOG(ERROR) << "FreeString: " << (FreeString ? "OK" : "MISSING");
      UnloadDll();
      return;
    }
#else
    LOG(WARNING) << "AzooKey DLL loading is only supported on Windows";
#endif
  }

  void UnloadDll() {
#ifdef _WIN32
    if (dll_handle_) {
      FreeLibrary(dll_handle_);
      dll_handle_ = nullptr;
    }
#endif
    Initialize = nullptr;
    Shutdown = nullptr;
    ConvertText = nullptr;
    FreeString = nullptr;
    SetZenzaiEnabled = nullptr;
    SetZenzaiUseGpu = nullptr;
    SetZenzaiInferenceLimit = nullptr;
    SetZenzaiWeightPath = nullptr;
  }

#ifdef _WIN32
  HMODULE dll_handle_ = nullptr;
#else
  void* dll_handle_ = nullptr;
#endif
};

namespace {
// Helper function to convert UTF-8 to wide string
std::wstring Utf8ToWideForRegistry(const std::string& utf8) {
  if (utf8.empty()) return L"";
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
  if (size_needed <= 0) return L"";
  std::wstring result(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                      static_cast<int>(utf8.size()), &result[0], size_needed);
  return result;
}

// Write Zenzai status to registry for cross-process communication
void WriteZenzaiStatusToRegistry(bool active, bool use_gpu,
                                 const std::string& weight_path) {
#ifdef _WIN32
  HKEY hKey = nullptr;
  LONG result = RegCreateKeyExW(
      HKEY_CURRENT_USER,
      L"Software\\Mozc",
      0,
      nullptr,
      REG_OPTION_NON_VOLATILE,
      KEY_SET_VALUE,
      nullptr,
      &hKey,
      nullptr);

  if (result != ERROR_SUCCESS) {
    LOG(WARNING) << "RegCreateKeyExW failed: " << result;
    return;
  }

  // Write Active status
  DWORD activeValue = active ? 1 : 0;
  result = RegSetValueExW(hKey, L"ZenzaiActive", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&activeValue), sizeof(DWORD));
  if (result != ERROR_SUCCESS) {
    LOG(WARNING) << "RegSetValueExW(ZenzaiActive) failed: " << result;
  }

  // Write GPU opt-in status used for this engine instance
  DWORD gpuValue = use_gpu ? 1 : 0;
  result = RegSetValueExW(hKey, L"ZenzaiGpuActive", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&gpuValue), sizeof(DWORD));
  if (result != ERROR_SUCCESS) {
    LOG(WARNING) << "RegSetValueExW(ZenzaiGpuActive) failed: " << result;
  }

  // Write weight path
  std::wstring wide_path = Utf8ToWideForRegistry(weight_path);
  result = RegSetValueExW(hKey, L"ZenzaiWeightPath", 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(wide_path.c_str()),
                          static_cast<DWORD>((wide_path.size() + 1) * sizeof(wchar_t)));
  if (result != ERROR_SUCCESS) {
    LOG(WARNING) << "RegSetValueExW(ZenzaiWeightPath) failed: " << result;
  }

  // Write timestamp
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t timestamp[64];
  swprintf_s(timestamp, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  result = RegSetValueExW(hKey, L"ZenzaiTimestamp", 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(timestamp),
                          static_cast<DWORD>((wcslen(timestamp) + 1) * sizeof(wchar_t)));
  if (result != ERROR_SUCCESS) {
    LOG(WARNING) << "RegSetValueExW(ZenzaiTimestamp) failed: " << result;
  }

  RegCloseKey(hKey);
#endif
}
}  // namespace

AzooKeyImmutableConverter::AzooKeyImmutableConverter(const AzooKeyConfig& config)
    : config_(config) {
  auto& loader = AzooKeyDllLoader::GetInstance();

  if (!loader.IsLoaded()) {
    LOG(ERROR) << "AzooKey DLL not loaded, converter will not function";
    initialized_ = false;
    return;
  }

  // Initialize via fine-grained API for better control
  const char* dict_path = config_.dictionary_path.empty() ? nullptr : config_.dictionary_path.c_str();
  const char* mem_path = config_.memory_path.empty() ? nullptr : config_.memory_path.c_str();

  // Initialize は失敗 (辞書パス不正等) を 0 で返す。
  // 以前は戻り値が無く、辞書破損でも「候補が出ないIME」として成功扱いだった。
  if (!loader.Initialize || loader.Initialize(dict_path, mem_path) == 0) {
    LOG(ERROR) << "AzooKey engine Initialize failed (dictionary_path="
               << config_.dictionary_path << ")";
    initialized_ = false;
    return;
  }

  if (loader.SetZenzaiEnabled) {
    loader.SetZenzaiEnabled(config_.zenzai_enabled);
  }

  const bool gpu_setting_applied = loader.SetZenzaiUseGpu != nullptr;
  if (loader.SetZenzaiUseGpu) {
    loader.SetZenzaiUseGpu(config_.zenzai_use_gpu);
  }

  if (loader.SetZenzaiInferenceLimit) {
    loader.SetZenzaiInferenceLimit(config_.zenzai_inference_limit);
  }

  if (loader.SetZenzaiWeightPath && !config_.zenzai_weight_path.empty()) {
    loader.SetZenzaiWeightPath(config_.zenzai_weight_path.c_str());
  }

  initialized_ = true;
  LOG(INFO) << "AzooKeyImmutableConverter initialized with Zenzai="
            << (config_.zenzai_enabled ? "enabled" : "disabled")
            << ", GPU=" << (config_.zenzai_use_gpu ? "enabled" : "disabled");

  // Write Zenzai status to registry for GUI processes to read
  bool zenzai_active = config_.zenzai_enabled && !config_.zenzai_weight_path.empty();
  WriteZenzaiStatusToRegistry(zenzai_active,
                              zenzai_active && config_.zenzai_use_gpu &&
                                  gpu_setting_applied,
                              config_.zenzai_weight_path);
}

AzooKeyImmutableConverter::~AzooKeyImmutableConverter() {
  if (initialized_) {
    auto& loader = AzooKeyDllLoader::GetInstance();
    if (loader.Shutdown) {
      loader.Shutdown();
    }
  }
}

bool AzooKeyImmutableConverter::Convert(const ConversionOptions& options,
                                         Segments* segments) const {
  if (!initialized_ || segments == nullptr) {
    return false;
  }

  auto& loader = AzooKeyDllLoader::GetInstance();
  if (!loader.IsLoaded()) {
    return false;
  }

  if (segments->conversion_segments_size() == 0) {
    return false;
  }

  const size_t num_segments = segments->conversion_segments_size();

  // Collect all keys first to avoid state issues
  std::vector<std::pair<std::string, Segment*>> segment_keys;
  for (size_t i = 0; i < num_segments; ++i) {
    Segment* segment = segments->mutable_conversion_segment(i);
    std::string key = std::string(segment->key());
    segment_keys.push_back({key, segment});
  }

  // シークレットモード等では学習を無効化する
  const int allow_learning = options.enable_user_history_for_conversion ? 1 : 0;

  // Process each segment individually.
  // ConvertText は1呼び出しで完結する単発APIのため、旧来の
  // ClearText→AppendText→GetCandidates のシーケンスと違い
  // 呼び出しの間に他スレッドの操作が割り込まない。
  for (size_t i = 0; i < segment_keys.size(); ++i) {
    const std::string& key = segment_keys[i].first;
    Segment* segment = segment_keys[i].second;

    if (key.empty()) {
      continue;
    }

    const char* candidates_json = loader.ConvertText(key.c_str(), allow_learning);
    if (candidates_json == nullptr) {
      continue;
    }

    std::string json_str(candidates_json);
    loader.FreeString(candidates_json);

    // Parse candidates and populate this segment
    ParseCandidatesForSegment(json_str, key, segment);
  }

  return true;
}

void AzooKeyImmutableConverter::ParseCandidatesForSegment(
    const std::string& json_candidates,
    const std::string& key,
    Segment* segment) const {
  std::vector<CandidateInfo> candidates = ParseJsonCandidateArray(json_candidates);

  // Calculate key character count (not byte count)
  const size_t key_char_count = CountUtf8Characters(key);

  LOG(INFO) << "AzooKey::ParseCandidatesForSegment - key=" << key
            << ", key_char_count=" << key_char_count
            << ", candidates=" << candidates.size();

  // Process candidates: filter those matching key length,
  // or append remaining hiragana for partial matches
  std::vector<CandidateInfo> processed_candidates;
  for (const auto& info : candidates) {
    // 契約: correspondingCount (Swift側 rubyCount) は必ず1以上。
    // 0以下 (キー欠落・パース失敗含む) を「完全一致」とみなすと、部分変換候補が
    // キー全体をカバーする扱いになり consumed_key_size と表示が食い違うため除外。
    if (info.corresponding_count <= 0) {
      continue;
    }
    const size_t candidate_char_count =
        static_cast<size_t>(info.corresponding_count);

    if (candidate_char_count == key_char_count) {
      // Exact match - use as is
      processed_candidates.push_back(info);
    } else if (candidate_char_count < key_char_count) {
      // Partial match - append remaining hiragana
      CandidateInfo processed = info;
      std::string remaining = GetUtf8Suffix(key, candidate_char_count);
      processed.text = info.text + remaining;
      processed.corresponding_count = static_cast<int>(key_char_count);
      processed_candidates.push_back(std::move(processed));
    }
    // Skip candidates with correspondingCount > key_char_count
  }

  // If no candidates, add the key itself as fallback
  if (processed_candidates.empty()) {
    CandidateInfo fallback;
    fallback.text = key;
    fallback.corresponding_count = static_cast<int>(key_char_count);
    processed_candidates.push_back(std::move(fallback));
  }

  // Clear existing candidates and add new ones
  segment->clear_candidates();
  segment->clear_meta_candidates();

  int32_t base_cost = 0;
  for (const auto& info : processed_candidates) {
    converter::Candidate* candidate = segment->add_candidate();

    candidate->key = key;
    candidate->value = info.text;
    candidate->content_key = key;
    candidate->content_value = info.text;
    candidate->cost = base_cost;
    candidate->wcost = base_cost;
    candidate->structure_cost = 0;
    candidate->consumed_key_size = key_char_count;
    // lid/rid = 0 means CompletePosIds() will fill them from dictionary
    candidate->lid = 0;
    candidate->rid = 0;
    // NOTE: candidate->description への印付けは後段の Rewriter に上書きされて
    // 不安定だったため廃止。azookey 由来の識別は予測ラベル側 (result.cc の
    // GetPredictionTypeDebugString で REALTIME を "AZ"/"AZ1" 表示) で行う。

    base_cost += 100;
  }
}


std::unique_ptr<const ImmutableConverterInterface> CreateAzooKeyImmutableConverter(
    const AzooKeyConfig& config) {
  auto converter = std::make_unique<AzooKeyImmutableConverter>(config);
  if (!converter->IsValid()) {
    LOG(ERROR) << "Failed to initialize AzooKeyImmutableConverter";
    return nullptr;
  }
  return converter;
}

}  // namespace mozc
