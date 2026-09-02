// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Engine configuration for switching between Mozc and AzooKey.

#ifndef MOZC_CONVERTER_ENGINE_CONFIG_H_
#define MOZC_CONVERTER_ENGINE_CONFIG_H_

#include <fstream>
#include <string>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace mozc {

// Hermetic test-mode behavior:
//   MYIME_HERMETIC_TEST enables hermetic isolation when exactly "1".
//   Pass --test_env=MYIME_HERMETIC_TEST=1 from bazel test.
//   TEST_TMPDIR is used only to determine the learning storage location.
//   MYIME_AZOOKEY_DLL_DIR selects the test-only AzooKey DLL directory.
//   MYIME_AZOOKEY_ZENZAI_WEIGHT selects the test-only Zenzai GGUF file.
//   MYIME_AZOOKEY_ZENZAI_GPU enables test-only GPU inference when exactly "1".

// Engine type enumeration
enum class ConversionEngineType {
  MOZC = 0,     // Default Mozc engine
  AZOOKEY = 1,  // AzooKey engine with Zenzai AI
};

// Zenzai model configuration
constexpr const char* kZenzaiModelName = "ggml-model-Q5_K_M.gguf";
constexpr const char* kZenzaiModelVersion = "zenz-v3.2-small";

inline bool IsHermeticTestMode();

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
  if (IsHermeticTestMode()) {
    return "";
  }
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
  if (IsHermeticTestMode()) {
    return "";
  }
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

#ifdef _WIN32
inline bool GetEnvironmentVariableValue(const wchar_t* name,
                                        std::wstring* value) {
  SetLastError(ERROR_SUCCESS);
  const DWORD required_size = GetEnvironmentVariableW(name, nullptr, 0);
  if (required_size == 0) {
    value->clear();
    return false;
  }

  std::wstring buffer(required_size, L'\0');
  const DWORD copied = GetEnvironmentVariableW(
      name, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (copied == 0 || copied >= buffer.size()) {
    value->clear();
    return false;
  }
  buffer.resize(copied);
  *value = buffer;
  return true;
}

inline std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return "";
  }
  const int len = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (len <= 0) {
    return "";
  }
  std::string narrow(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                      static_cast<int>(wide.size()), narrow.data(), len,
                      nullptr, nullptr);
  return narrow;
}

// 設定ダイアログ(config_dialog.cc)が書き込む値を、config.protoを変えずにレジストリ直読みする。
inline bool ReadHkcuMozcDwordAsBool(const wchar_t* value_name,
                                    bool default_value) {
  if (IsHermeticTestMode()) {
    return default_value;
  }

  HKEY hKey;
  LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Mozc", 0,
                              KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return default_value;
  }

  DWORD enabledValue = default_value ? 1 : 0;
  DWORD valueType = 0;
  DWORD dataSize = sizeof(DWORD);
  result = RegQueryValueExW(hKey, value_name, nullptr, &valueType,
                            reinterpret_cast<LPBYTE>(&enabledValue),
                            &dataSize);
  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS || valueType != REG_DWORD ||
      dataSize != sizeof(DWORD)) {
    return default_value;
  }

  // 歴史的経緯: 既定 true の値は「非0なら有効」、既定 false の値は「1のときだけ
  // 有効」と判定が分かれていた。書き込み側(config_dialog)は 0/1 しか書かないため
  // 実用上の差はないが、リファクタ時に挙動を変えないためそのまま温存している
  return default_value ? enabledValue != 0 : enabledValue == 1;
}

inline std::wstring ReadHkcuMozcString(const wchar_t* value_name) {
  if (IsHermeticTestMode()) {
    return L"";
  }

  HKEY hKey;
  LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Mozc", 0,
                              KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return L"";
  }

  DWORD valueType = 0;
  DWORD dataSize = 0;
  result = RegQueryValueExW(hKey, value_name, nullptr, &valueType, nullptr,
                            &dataSize);
  if (result != ERROR_SUCCESS || valueType != REG_SZ ||
      dataSize < sizeof(wchar_t) || dataSize % sizeof(wchar_t) != 0) {
    RegCloseKey(hKey);
    return L"";
  }

  std::wstring value(dataSize / sizeof(wchar_t), L'\0');
  result = RegQueryValueExW(hKey, value_name, nullptr, &valueType,
                            reinterpret_cast<LPBYTE>(value.data()), &dataSize);
  RegCloseKey(hKey);
  if (result != ERROR_SUCCESS || valueType != REG_SZ ||
      dataSize % sizeof(wchar_t) != 0) {
    return L"";
  }
  value.resize(dataSize / sizeof(wchar_t));
  while (!value.empty() && value.back() == L'\0') {
    value.pop_back();
  }
  return value;
}
#endif  // _WIN32
}  // namespace internal

inline bool IsHermeticTestMode() {
#ifdef _WIN32
  static const bool is_hermetic_test = [] {
    std::wstring value;
    return internal::GetEnvironmentVariableValue(L"MYIME_HERMETIC_TEST",
                                                  &value) &&
           value == L"1";
  }();
  return is_hermetic_test;
#else
  static const bool is_hermetic_test = false;
  return is_hermetic_test;
#endif
}

// モデルファイルのフルパス。
// 既存ファイルを優先的に探す: ユーザー領域 → インストール先 の順。
// どちらにも無ければ、ダウンロード先 (ユーザー領域) のパスを返す。
inline std::string GetZenzaiModelPath() {
#ifdef _WIN32
  if (IsHermeticTestMode()) {
    std::wstring weight_path;
    if (internal::GetEnvironmentVariableValue(
            L"MYIME_AZOOKEY_ZENZAI_WEIGHT", &weight_path)) {
      return internal::WideToUtf8(weight_path);
    }
    return "";
  }
#endif
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
// Missing value or read/type failures default to enabled.
inline bool IsZenzaiUserEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"ZenzaiEnabled", true);
#else
  return true;
#endif
}

// Check if typo-correction candidates are enabled for AzooKey engine.
// Missing value or read/type failures default to enabled.
inline bool IsTypoCorrectionEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"TypoCorrectionEnabled", true);
#else
  return true;
#endif
}

// Check if idle resuggest spike is enabled.
// Missing value or read/type failures default to disabled.
inline bool IsIdleResuggestEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"IdleResuggest", false);
#else
  return false;
#endif
}

// Check if Zenzai is used for typo correction candidate ranking.
// Missing value or read/type failures default to disabled.
inline bool IsTypoCorrectionUseAiEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"TypoCorrectionUseAi", false);
#else
  return false;
#endif
}

// Check if Zenzai GPU inference is enabled by user opt-in.
// Missing value or read/type failures default to disabled.
inline bool IsZenzaiGpuEnabled() {
#ifdef _WIN32
  if (IsHermeticTestMode()) {
    std::wstring use_gpu;
    return internal::GetEnvironmentVariableValue(
               L"MYIME_AZOOKEY_ZENZAI_GPU", &use_gpu) &&
           use_gpu == L"1";
  }
  return internal::ReadHkcuMozcDwordAsBool(L"ZenzaiUseGpu", false);
#else
  return false;
#endif
}

inline std::wstring GetPassthroughImeOffKeys() {
#ifdef _WIN32
  return internal::ReadHkcuMozcString(L"PassthroughImeOffKeys");
#else
  return L"";
#endif
}

// Zenzai is enabled only when user setting allows it and model file exists.
inline bool IsZenzaiEnabled() {
  return IsZenzaiUserEnabled() && ZenzaiModelExists();
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
  if (IsHermeticTestMode()) {
    std::wstring test_tmpdir;
    if (internal::GetEnvironmentVariableValue(L"TEST_TMPDIR", &test_tmpdir)) {
      return internal::WideToUtf8(test_tmpdir + L"\\azookey_memory");
    }
    std::wstring temp_directory;
    if (!internal::GetEnvironmentVariableValue(L"TEMP", &temp_directory)) {
      return "";
    }
    return internal::WideToUtf8(temp_directory +
                                L"\\myime-hermetic\\azookey_memory");
  }

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

}  // namespace mozc

#endif  // MOZC_CONVERTER_ENGINE_CONFIG_H_
