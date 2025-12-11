// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Engine configuration for switching between Mozc and AzooKey.

#ifndef MOZC_CONVERTER_ENGINE_CONFIG_H_
#define MOZC_CONVERTER_ENGINE_CONFIG_H_

#include <string>
#include <fstream>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace mozc {

// Engine type enumeration
enum class ConversionEngineType {
  MOZC = 0,     // Default Mozc engine
  AZOOKEY = 1,  // AzooKey engine with Zenzai AI
};

// Zenzai model configuration
constexpr const char* kZenzaiModelName = "ggml-model-Q5_K_M.gguf";
constexpr const char* kZenzaiModelVersion = "zenz-v3.1-small";

// Get the Zenzai model directory path
// Returns: %ProgramFiles%\Mozc\models\ on Windows (read from install directory)
inline std::string GetZenzaiModelDirectory() {
#ifdef _WIN32
  wchar_t path[MAX_PATH];
  // Use Program Files for machine-wide installation
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, path))) {
    char narrow_path[MAX_PATH];
    wcstombs(narrow_path, path, MAX_PATH);
    return std::string(narrow_path) + "\\Mozc\\models\\";
  }
#endif
  return "";
}

// Get the full path to Zenzai model file
inline std::string GetZenzaiModelPath() {
  std::string dir = GetZenzaiModelDirectory();
  if (dir.empty()) {
    return "";
  }
  return dir + kZenzaiModelName;
}

// Check if Zenzai model file exists
inline bool ZenzaiModelExists() {
  std::string path = GetZenzaiModelPath();
  if (path.empty()) {
    return false;
  }
  std::ifstream file(path);
  return file.good();
}

// Get the configured conversion engine type.
// Always uses AzooKey engine.
inline ConversionEngineType GetConversionEngineType() {
  return ConversionEngineType::AZOOKEY;
}

// Check if Zenzai AI is enabled for AzooKey engine.
// Zenzai is enabled only when model file exists.
inline bool IsZenzaiEnabled() {
  return ZenzaiModelExists();
}

// Get Zenzai inference limit.
// Fixed at 10 for optimal performance/quality balance.
inline int GetZenzaiInferenceLimit() {
  return 10;
}

// Get AzooKey dictionary path.
// Empty means use built-in dictionary.
inline std::string GetAzooKeyDictionaryPath() {
  return "";
}

// Get Zenzai weight file path.
// Returns the model path if exists, empty otherwise.
inline std::string GetZenzaiWeightPath() {
  if (ZenzaiModelExists()) {
    return GetZenzaiModelPath();
  }
  return "";
}

// Get Zenzai model version string for display
inline std::string GetZenzaiModelVersionString() {
  return kZenzaiModelVersion;
}

}  // namespace mozc

#endif  // MOZC_CONVERTER_ENGINE_CONFIG_H_
