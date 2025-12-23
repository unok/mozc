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
#include "request/conversion_request.h"

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
  return utf8_str.substr(byte_pos);
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
                  if (pos + 4 < json.size()) {
                    pos += 4;
                  }
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
            // Unicode escape \uXXXX
            if (pos + 4 < json.size()) {
              // Simple handling - just skip for now
              pos += 4;
            }
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
  using InitializeFunc = void (*)(const char*, const char*);
  using ShutdownFunc = void (*)();
  using AppendTextFunc = void (*)(const char*);
  using ClearTextFunc = void (*)();
  using GetCandidatesFunc = const char* (*)();
  using FreeStringFunc = void (*)(const char*);
  using SetZenzaiEnabledFunc = void (*)(bool);
  using SetZenzaiInferenceLimitFunc = void (*)(int);
  using SetZenzaiWeightPathFunc = void (*)(const char*);

  InitializeFunc Initialize = nullptr;
  ShutdownFunc Shutdown = nullptr;
  AppendTextFunc AppendText = nullptr;
  ClearTextFunc ClearText = nullptr;
  GetCandidatesFunc GetCandidates = nullptr;
  FreeStringFunc FreeString = nullptr;
  SetZenzaiEnabledFunc SetZenzaiEnabled = nullptr;
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

    // Fallback: try current directory or system PATH
    if (!dll_handle_) {
      dll_handle_ = LoadLibraryW(L"azookey-engine.dll");
    }

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
    AppendText = reinterpret_cast<AppendTextFunc>(
        GetProcAddress(dll_handle_, "AppendText"));
    ClearText = reinterpret_cast<ClearTextFunc>(
        GetProcAddress(dll_handle_, "ClearText"));
    GetCandidates = reinterpret_cast<GetCandidatesFunc>(
        GetProcAddress(dll_handle_, "GetCandidates"));
    FreeString = reinterpret_cast<FreeStringFunc>(
        GetProcAddress(dll_handle_, "FreeString"));
    SetZenzaiEnabled = reinterpret_cast<SetZenzaiEnabledFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiEnabled"));
    SetZenzaiInferenceLimit = reinterpret_cast<SetZenzaiInferenceLimitFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiInferenceLimit"));
    SetZenzaiWeightPath = reinterpret_cast<SetZenzaiWeightPathFunc>(
        GetProcAddress(dll_handle_, "SetZenzaiWeightPath"));

    // Check if essential functions are loaded
    if (!Initialize || !AppendText || !GetCandidates) {
      LOG(ERROR) << "Failed to load essential functions from azookey-engine.dll";
      LOG(ERROR) << "Initialize: " << (Initialize ? "OK" : "MISSING");
      LOG(ERROR) << "AppendText: " << (AppendText ? "OK" : "MISSING");
      LOG(ERROR) << "GetCandidates: " << (GetCandidates ? "OK" : "MISSING");
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
    AppendText = nullptr;
    ClearText = nullptr;
    GetCandidates = nullptr;
    FreeString = nullptr;
    SetZenzaiEnabled = nullptr;
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
void WriteZenzaiStatusToRegistry(bool active, const std::string& weight_path) {
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

  if (loader.Initialize) {
    loader.Initialize(dict_path, mem_path);
  }

  if (loader.SetZenzaiEnabled) {
    loader.SetZenzaiEnabled(config_.zenzai_enabled);
  }

  if (loader.SetZenzaiInferenceLimit) {
    loader.SetZenzaiInferenceLimit(config_.zenzai_inference_limit);
  }

  if (loader.SetZenzaiWeightPath && !config_.zenzai_weight_path.empty()) {
    loader.SetZenzaiWeightPath(config_.zenzai_weight_path.c_str());
  }

  initialized_ = true;
  LOG(INFO) << "AzooKeyImmutableConverter initialized with Zenzai="
            << (config_.zenzai_enabled ? "enabled" : "disabled");

  // Write Zenzai status to registry for GUI processes to read
  bool zenzai_active = config_.zenzai_enabled && !config_.zenzai_weight_path.empty();
  WriteZenzaiStatusToRegistry(zenzai_active, config_.zenzai_weight_path);
}

AzooKeyImmutableConverter::~AzooKeyImmutableConverter() {
  if (initialized_) {
    auto& loader = AzooKeyDllLoader::GetInstance();
    if (loader.Shutdown) {
      loader.Shutdown();
    }
  }
}

bool AzooKeyImmutableConverter::Convert(const ConversionRequest& request,
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

  // Process each segment individually
  for (size_t i = 0; i < segment_keys.size(); ++i) {
    const std::string& key = segment_keys[i].first;
    Segment* segment = segment_keys[i].second;

    if (key.empty()) {
      continue;
    }

    // Clear existing text and set new input for this segment
    if (loader.ClearText) {
      loader.ClearText();
    }

    if (loader.AppendText) {
      loader.AppendText(key.c_str());
    }

    // Get candidates from AzooKey
    if (!loader.GetCandidates) {
      continue;
    }

    const char* candidates_json = loader.GetCandidates();
    if (candidates_json == nullptr) {
      continue;
    }

    std::string json_str(candidates_json);

    if (loader.FreeString) {
      loader.FreeString(candidates_json);
    }

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
    size_t candidate_char_count = (info.corresponding_count > 0)
        ? static_cast<size_t>(info.corresponding_count)
        : key_char_count;

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

    base_cost += 100;
  }
}

bool AzooKeyImmutableConverter::ParseCandidates(const std::string& json_candidates,
                                                 const std::string& key,
                                                 Segments* segments) const {
  std::vector<CandidateInfo> candidates = ParseJsonCandidateArray(json_candidates);

  // Calculate key character count (not byte count)
  const size_t key_char_count = CountUtf8Characters(key);

  LOG(WARNING) << "AzooKey::ParseCandidates - key=" << key
               << ", key_char_count=" << key_char_count
               << ", raw_candidates=" << candidates.size();

  // Process candidates: if correspondingCount is shorter than key length,
  // append the remaining hiragana to the candidate text
  std::vector<CandidateInfo> processed_candidates;
  for (const auto& info : candidates) {
    size_t candidate_char_count = (info.corresponding_count > 0)
        ? static_cast<size_t>(info.corresponding_count)
        : key_char_count;

    CandidateInfo processed = info;
    if (candidate_char_count < key_char_count) {
      // Append remaining hiragana to the candidate text
      std::string remaining = GetUtf8Suffix(key, candidate_char_count);
      processed.text = info.text + remaining;
      processed.corresponding_count = static_cast<int>(key_char_count);
    }
    processed_candidates.push_back(std::move(processed));
  }

  LOG(WARNING) << "AzooKey::ParseCandidates - processed_candidates=" << processed_candidates.size();

  // If no candidates, add the key itself as fallback
  if (processed_candidates.empty()) {
    CandidateInfo fallback;
    fallback.text = key;
    fallback.corresponding_count = static_cast<int>(key_char_count);
    processed_candidates.push_back(std::move(fallback));
  }

  // Clear existing conversion segments
  segments->clear_conversion_segments();

  // Create single segment for the entire key
  Segment* segment = segments->add_segment();
  segment->set_segment_type(Segment::FREE);
  segment->set_key(key);

  // Add candidates to the segment
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
    candidate->lid = 0;
    candidate->rid = 0;

    // Increment cost for subsequent candidates
    base_cost += 100;
  }

  return !processed_candidates.empty();
}

bool AzooKeyImmutableConverter::ParseCandidatesForResizedSegment(
    const std::string& json_candidates,
    const std::string& key,
    Segments* segments) const {
  std::vector<CandidateInfo> candidates = ParseJsonCandidateArray(json_candidates);

  // Calculate key character count
  const size_t key_char_count = CountUtf8Characters(key);

  LOG(WARNING) << "AzooKey::ParseCandidatesForResizedSegment - key=" << key
               << ", key_char_count=" << key_char_count
               << ", raw_candidates=" << candidates.size();

  // Filter candidates: only accept those matching the key length
  std::vector<CandidateInfo> matching_candidates;
  for (const auto& info : candidates) {
    size_t candidate_char_count = (info.corresponding_count > 0)
        ? static_cast<size_t>(info.corresponding_count)
        : key_char_count;

    if (candidate_char_count == key_char_count) {
      matching_candidates.push_back(info);
    }
  }

  LOG(WARNING) << "AzooKey::ParseCandidatesForResizedSegment - matching_candidates="
               << matching_candidates.size();

  // If no matching candidates, add the key itself as fallback
  if (matching_candidates.empty()) {
    CandidateInfo fallback;
    fallback.text = key;
    fallback.corresponding_count = static_cast<int>(key_char_count);
    matching_candidates.push_back(std::move(fallback));
  }

  // For resized segments, we only update the first segment's candidates
  // without clearing other segments
  if (segments->conversion_segments_size() == 0) {
    return false;
  }

  // Get the first segment and clear its candidates
  Segment* first_segment = segments->mutable_conversion_segment(0);
  first_segment->clear_candidates();
  first_segment->clear_meta_candidates();

  // Add matching candidates to the first segment
  int32_t base_cost = 0;
  for (const auto& info : matching_candidates) {
    converter::Candidate* candidate = first_segment->add_candidate();

    candidate->key = key;
    candidate->value = info.text;
    candidate->content_key = key;
    candidate->content_value = info.text;
    candidate->cost = base_cost;
    candidate->wcost = base_cost;
    candidate->structure_cost = 0;
    candidate->consumed_key_size = key_char_count;
    candidate->lid = 0;
    candidate->rid = 0;

    // Increment cost for subsequent candidates
    base_cost += 100;
  }

  return !matching_candidates.empty();
}

std::string AzooKeyImmutableConverter::HiraganaToRomaji(const std::string& hiragana) const {
  // This is a simplified conversion - in production, use a proper table
  // For now, we'll just return hiragana as-is since AzooKey can handle it
  return hiragana;
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
