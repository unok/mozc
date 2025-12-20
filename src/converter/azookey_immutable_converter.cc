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

// Debug logging using OutputDebugStringW (Unicode)
namespace {

#ifdef _WIN32
// Convert UTF-8 string to wide string for OutputDebugStringW
std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return L"";
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
  if (size_needed <= 0) return L"";
  std::wstring result(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                      static_cast<int>(utf8.size()), &result[0], size_needed);
  return result;
}
#endif

void DebugLog(const char* message) {
#ifdef _WIN32
  std::wstring wide_msg = Utf8ToWide(std::string(message));
  OutputDebugStringW(L"[AzooKey] ");
  OutputDebugStringW(wide_msg.c_str());
  OutputDebugStringW(L"\n");
#endif
}

void DebugLog(const std::string& message) {
  DebugLog(message.c_str());
}

// Write Zenzai status to registry for cross-process communication
}  // namespace

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "converter/segments.h"
#include "request/conversion_request.h"

// Simple JSON parsing for candidates array
// New format: [{"text": "candidate1", "correspondingCount": 6}, ...]
namespace {

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
    DebugLog("=== AzooKey DLL Loading Started ===");

    // Try to load from the same directory as the executable
    // First, get the module path
    wchar_t module_path[MAX_PATH];
    HMODULE hModule = nullptr;

    // Get handle to the current module (mozc_server.exe or mozc_tip64.dll)
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&AzooKeyDllLoader::GetInstance),
                           &hModule)) {
      GetModuleFileNameW(hModule, module_path, MAX_PATH);

      // Log module path (convert wchar to char for logging)
      char module_path_narrow[MAX_PATH];
      wcstombs(module_path_narrow, module_path, MAX_PATH);
      DebugLog(std::string("Current module path: ") + module_path_narrow);

      // Remove the filename to get directory
      wchar_t* last_slash = wcsrchr(module_path, L'\\');
      if (last_slash) {
        *last_slash = L'\0';
      }

      // Construct full path to azookey-engine.dll
      std::wstring dll_path = std::wstring(module_path) + L"\\azookey-engine.dll";

      char dll_path_narrow[MAX_PATH];
      wcstombs(dll_path_narrow, dll_path.c_str(), MAX_PATH);
      DebugLog(std::string("Attempting to load DLL from: ") + dll_path_narrow);

      LOG(INFO) << "Attempting to load AzooKey DLL from module directory";
      dll_handle_ = LoadLibraryW(dll_path.c_str());

      if (dll_handle_) {
        DebugLog("DLL loaded successfully from module directory");
      } else {
        DWORD error = GetLastError();
        DebugLog(std::string("Failed to load from module dir, error: ") + std::to_string(error));
      }
    } else {
      DWORD error = GetLastError();
      DebugLog(std::string("GetModuleHandleExW failed, error: ") + std::to_string(error));
    }

    // Fallback: try current directory or system PATH
    if (!dll_handle_) {
      DebugLog("Attempting to load AzooKey DLL from PATH");
      LOG(INFO) << "Attempting to load AzooKey DLL from PATH";
      dll_handle_ = LoadLibraryW(L"azookey-engine.dll");

      if (dll_handle_) {
        DebugLog("DLL loaded successfully from PATH");
      } else {
        DWORD error = GetLastError();
        DebugLog(std::string("Failed to load from PATH, error: ") + std::to_string(error));
      }
    }

    if (!dll_handle_) {
      DWORD error = GetLastError();
      DebugLog(std::string("FINAL: Failed to load azookey-engine.dll, error: ") + std::to_string(error));
      LOG(ERROR) << "Failed to load azookey-engine.dll, error code: " << error;
      return;
    }

    DebugLog("Successfully loaded azookey-engine.dll");
    LOG(INFO) << "Successfully loaded azookey-engine.dll";

    // Load function pointers
    DebugLog("Loading function pointers...");
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

    DebugLog(std::string("Initialize: ") + (Initialize ? "OK" : "MISSING"));
    DebugLog(std::string("Shutdown: ") + (Shutdown ? "OK" : "MISSING"));
    DebugLog(std::string("AppendText: ") + (AppendText ? "OK" : "MISSING"));
    DebugLog(std::string("ClearText: ") + (ClearText ? "OK" : "MISSING"));
    DebugLog(std::string("GetCandidates: ") + (GetCandidates ? "OK" : "MISSING"));
    DebugLog(std::string("FreeString: ") + (FreeString ? "OK" : "MISSING"));
    DebugLog(std::string("SetZenzaiEnabled: ") + (SetZenzaiEnabled ? "OK" : "MISSING"));
    DebugLog(std::string("SetZenzaiInferenceLimit: ") + (SetZenzaiInferenceLimit ? "OK" : "MISSING"));
    DebugLog(std::string("SetZenzaiWeightPath: ") + (SetZenzaiWeightPath ? "OK" : "MISSING"));

    // Check if essential functions are loaded
    if (!Initialize || !AppendText || !GetCandidates) {
      DebugLog("FAILED: Essential functions missing!");
      LOG(ERROR) << "Failed to load essential functions from azookey-engine.dll";
      LOG(ERROR) << "Initialize: " << (Initialize ? "OK" : "MISSING");
      LOG(ERROR) << "AppendText: " << (AppendText ? "OK" : "MISSING");
      LOG(ERROR) << "GetCandidates: " << (GetCandidates ? "OK" : "MISSING");
      UnloadDll();
      return;
    }

    DebugLog("All AzooKey DLL functions loaded successfully");
    LOG(INFO) << "All AzooKey DLL functions loaded successfully";
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
  OutputDebugStringW(L"[AzooKey] WriteZenzaiStatusToRegistry called");
  HKEY hKey;
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
    wchar_t errMsg[128];
    swprintf_s(errMsg, 128, L"[AzooKey] RegCreateKeyExW FAILED: %ld", result);
    OutputDebugStringW(errMsg);
    return;
  }
  OutputDebugStringW(L"[AzooKey] RegCreateKeyExW SUCCESS");

  // Write Active status
  DWORD activeValue = active ? 1 : 0;
  RegSetValueExW(hKey, L"ZenzaiActive", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&activeValue), sizeof(DWORD));

  // Write weight path
  std::wstring wide_path = Utf8ToWideForRegistry(weight_path);
  RegSetValueExW(hKey, L"ZenzaiWeightPath", 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(wide_path.c_str()),
                 static_cast<DWORD>((wide_path.size() + 1) * sizeof(wchar_t)));

  // Write timestamp
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t timestamp[64];
  swprintf_s(timestamp, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  RegSetValueExW(hKey, L"ZenzaiTimestamp", 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(timestamp),
                 static_cast<DWORD>((wcslen(timestamp) + 1) * sizeof(wchar_t)));

  RegCloseKey(hKey);
  OutputDebugStringW(L"[AzooKey] Registry write completed");
#endif
}
}  // namespace

AzooKeyImmutableConverter::AzooKeyImmutableConverter(const AzooKeyConfig& config)
    : config_(config) {
  DebugLog("=== AzooKeyImmutableConverter Constructor ===");
  auto& loader = AzooKeyDllLoader::GetInstance();

  if (!loader.IsLoaded()) {
    DebugLog("ERROR: AzooKey DLL not loaded");
    LOG(ERROR) << "AzooKey DLL not loaded, converter will not function";
    initialized_ = false;
    return;
  }
  DebugLog("DLL is loaded, proceeding with initialization");

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
    DebugLog(std::string("Zenzai weight path set to: ") + config_.zenzai_weight_path);
  }

  initialized_ = true;
  DebugLog(std::string("AzooKeyImmutableConverter initialized, Zenzai=") +
           (config_.zenzai_enabled ? "enabled" : "disabled") +
           ", WeightPath=" + (config_.zenzai_weight_path.empty() ? "(empty)" : config_.zenzai_weight_path));
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
  DebugLog("=== Convert called ===");
  if (!initialized_ || segments == nullptr) {
    DebugLog("Convert: not initialized or segments null");
    return false;
  }

  auto& loader = AzooKeyDllLoader::GetInstance();
  if (!loader.IsLoaded()) {
    DebugLog("Convert: loader not loaded");
    return false;
  }

  // Get the key (reading) from the first conversion segment
  if (segments->conversion_segments_size() == 0) {
    DebugLog("Convert: no conversion segments");
    return false;
  }

  std::string key;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    key += std::string(segments->conversion_segment(i).key());
  }

  if (key.empty()) {
    DebugLog("Convert: key is empty");
    return false;
  }
  DebugLog(std::string("Convert: key = ") + key);

  // Clear existing text and set new input
  if (loader.ClearText) {
    loader.ClearText();
  }

  // AzooKey expects romaji input, but the key is in hiragana
  // For now, we'll pass hiragana directly since AzooKey can handle it
  // with proper configuration
  if (loader.AppendText) {
    loader.AppendText(key.c_str());
  }

  // Get candidates from AzooKey
  if (!loader.GetCandidates) {
    return false;
  }

  const char* candidates_json = loader.GetCandidates();
  if (candidates_json == nullptr) {
    DebugLog("Convert: GetCandidates returned null");
    return false;
  }

  std::string json_str(candidates_json);
  DebugLog(std::string("Convert: GetCandidates returned: ") + json_str);

  if (loader.FreeString) {
    loader.FreeString(candidates_json);
  }

  // Parse and populate segments
  return ParseCandidates(json_str, key, segments);
}

bool AzooKeyImmutableConverter::ParseCandidates(const std::string& json_candidates,
                                                 const std::string& key,
                                                 Segments* segments) const {
  std::vector<CandidateInfo> candidates = ParseJsonCandidateArray(json_candidates);

  if (candidates.empty()) {
    // No candidates, add the key itself as fallback
    CandidateInfo fallback;
    fallback.text = key;
    fallback.corresponding_count = 0;  // Will use key.size() as fallback
    candidates.push_back(std::move(fallback));
  }

  // Clear existing conversion segments and create a single segment
  segments->clear_conversion_segments();

  Segment* segment = segments->add_segment();
  segment->set_segment_type(Segment::FREE);
  segment->set_key(key);

  // Add candidates
  int32_t base_cost = 0;
  for (const auto& info : candidates) {
    converter::Candidate* candidate = segment->add_candidate();
    candidate->key = key;
    candidate->value = info.text;
    candidate->content_key = key;
    candidate->content_value = info.text;
    candidate->cost = base_cost;
    candidate->wcost = base_cost;
    candidate->structure_cost = 0;

    // Use full key size for consumed_key_size
    candidate->consumed_key_size = key.size();

    // Increment cost for subsequent candidates
    base_cost += 100;
  }

  LOG(INFO) << "AzooKeyImmutableConverter: Converted '" << key
            << "' with " << candidates.size() << " candidates";

  return !candidates.empty();
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
