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
constexpr const char* kZenzaiModelVersion = "zenz-v3.2-small";

#ifdef _WIN32
// CSIDL から UTF-8 のパスを得るヘルパ (Swift FFI は UTF-8 前提)。
inline std::string GetCsidlDirUtf8(int csidl) {
  wchar_t path[MAX_PATH];
  if (!SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, path))) {
    return "";
  }
  const int len =
      WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return "";
  }
  std::string narrow(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow.data(), len, nullptr,
                      nullptr);
  return narrow;
}
#endif  // _WIN32

// ユーザー領域のモデルディレクトリ (%LOCALAPPDATA%\Mozc\models\)。
// 書き込みに管理者権限が不要なので、ランタイム自動ダウンロードの保存先に使う。
inline std::string GetZenzaiUserModelDirectory() {
#ifdef _WIN32
  const std::string base = GetCsidlDirUtf8(CSIDL_LOCAL_APPDATA);
  if (!base.empty()) {
    return base + "\\Mozc\\models\\";
  }
#endif
  return "";
}

// インストール先のモデルディレクトリ (%ProgramFiles(x86)%\Mozc\models\)。
// MSI が配置する従来の場所 (書き込みは管理者権限が必要)。
inline std::string GetZenzaiInstallModelDirectory() {
#ifdef _WIN32
  const std::string base = GetCsidlDirUtf8(CSIDL_PROGRAM_FILESX86);
  if (!base.empty()) {
    return base + "\\Mozc\\models\\";
  }
#endif
  return "";
}

// ダウンロード保存先 (書き込み可能なユーザー領域)。
inline std::string GetZenzaiModelDirectory() {
  return GetZenzaiUserModelDirectory();
}

namespace internal {
inline bool FileExists(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::ifstream file(path);
  return file.good();
}
}  // namespace internal

// モデルファイルのフルパス。
// 既存ファイルを優先的に探す: ユーザー領域 → インストール先 の順。
// どちらにも無ければ、ダウンロード先 (ユーザー領域) のパスを返す。
inline std::string GetZenzaiModelPath() {
  const std::string user_path = GetZenzaiUserModelDirectory() + kZenzaiModelName;
  if (internal::FileExists(user_path)) {
    return user_path;
  }
  const std::string install_path =
      GetZenzaiInstallModelDirectory() + kZenzaiModelName;
  if (internal::FileExists(install_path)) {
    return install_path;
  }
  return user_path;  // 未存在: ダウンロード先 (ユーザー領域)
}

// Check if Zenzai model file exists (ユーザー領域 or インストール先)。
inline bool ZenzaiModelExists() {
  return internal::FileExists(GetZenzaiModelPath());
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

// Get AzooKey user-learning memory directory.
// %APPDATA%\Mozc\azookey_memory (per-user, roaming). Empty disables learning.
inline std::string GetAzooKeyMemoryPath() {
#ifdef _WIN32
  wchar_t path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
    std::wstring dir = std::wstring(path) + L"\\Mozc\\azookey_memory";
    // 中間ディレクトリも含めて作成（既存なら成功扱い）
    const int result = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS) {
      return "";
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr,
                                        0, nullptr, nullptr);
    if (len > 0) {
      std::string narrow(len - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, narrow.data(), len,
                          nullptr, nullptr);
      return narrow;
    }
  }
#endif
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

// Get Zenzai runtime status from Swift Engine
// Returns JSON string with actual engine status, or empty string if unavailable
inline std::string GetZenzaiRuntimeStatus() {
#ifdef _WIN32
  // Try to get status from loaded DLL
  HMODULE hDll = GetModuleHandleW(L"azookey-engine.dll");
  if (!hDll) {
    return "{\"active\": false, \"reason\": \"DLL not loaded\"}";
  }

  using GetZenzaiStatusFunc = const char* (*)();
  using FreeStringFunc = void (*)(const char*);

  auto getStatus = reinterpret_cast<GetZenzaiStatusFunc>(
      GetProcAddress(hDll, "GetZenzaiStatus"));
  auto freeStr = reinterpret_cast<FreeStringFunc>(
      GetProcAddress(hDll, "FreeString"));

  if (!getStatus) {
    return "{\"active\": false, \"reason\": \"GetZenzaiStatus not found\"}";
  }

  const char* status = getStatus();
  if (!status) {
    return "{\"active\": false, \"reason\": \"Status returned null\"}";
  }

  std::string result(status);
  if (freeStr) {
    freeStr(status);
  }
  return result;
#else
  return "{\"active\": false, \"reason\": \"Not Windows\"}";
#endif
}

// Check if Zenzai is actually active in the Swift Engine
// Reads from registry written by the IME process
inline bool IsZenzaiActiveInEngine() {
#ifdef _WIN32
  HKEY hKey;
  LONG result = RegOpenKeyExW(
      HKEY_CURRENT_USER,
      L"Software\\Mozc",
      0,
      KEY_READ,
      &hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  DWORD activeValue = 0;
  DWORD dataSize = sizeof(DWORD);
  result = RegQueryValueExW(hKey, L"ZenzaiActive", nullptr, nullptr,
                            reinterpret_cast<LPBYTE>(&activeValue), &dataSize);
  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return activeValue != 0;
#else
  return false;
#endif
}

}  // namespace mozc

#endif  // MOZC_CONVERTER_ENGINE_CONFIG_H_
