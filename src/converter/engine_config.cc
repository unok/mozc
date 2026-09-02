// Copyright 2024 AzooKey Project.
// All rights reserved.

#include "converter/engine_config.h"

#include <cstdint>
#include <optional>
#include <string>

#include "base/file_util.h"

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace mozc {
namespace {

bool FileExists(const std::string& path) {
  return !path.empty() && FileUtil::FileExists(path).ok();
}

#ifdef _WIN32
std::string GetCsidlDirUtf8(int csidl) {
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

}  // namespace

namespace internal {

bool GetEnvironmentVariableValue(const wchar_t* name, std::wstring* value) {
#ifdef _WIN32
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
#else
  value->clear();
  return false;
#endif
}

std::string WideToUtf8(const std::wstring& wide) {
#ifdef _WIN32
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
#else
  return "";
#endif
}

std::optional<uint32_t> ReadHkcuMozcDword(const wchar_t* value_name) {
#ifdef _WIN32
  if (IsHermeticTestMode()) {
    return std::nullopt;
  }

  HKEY hKey;
  LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Mozc", 0,
                              KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return std::nullopt;
  }

  DWORD value = 0;
  DWORD value_type = 0;
  DWORD data_size = sizeof(value);
  result = RegQueryValueExW(hKey, value_name, nullptr, &value_type,
                            reinterpret_cast<LPBYTE>(&value), &data_size);
  RegCloseKey(hKey);
  if (result != ERROR_SUCCESS || value_type != REG_DWORD ||
      data_size != sizeof(value)) {
    return std::nullopt;
  }
  return value;
#else
  return std::nullopt;
#endif
}

bool ReadHkcuMozcDwordAsBool(const wchar_t* value_name, bool default_value) {
  const std::optional<uint32_t> value = ReadHkcuMozcDword(value_name);
  if (!value.has_value()) {
    return default_value;
  }
  // 歴史的経緯: 既定 true の値は「非0なら有効」、既定 false の値は「1のときだけ
  // 有効」と判定が分かれていた。書き込み側(config_dialog)は 0/1 しか書かないため
  // 実用上の差はないが、リファクタ時に挙動を変えないためそのまま温存している
  return default_value ? *value != 0 : *value == 1;
}

std::wstring ReadHkcuMozcString(const wchar_t* value_name) {
#ifdef _WIN32
  if (IsHermeticTestMode()) {
    return L"";
  }

  HKEY hKey;
  LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Mozc", 0,
                              KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return L"";
  }

  DWORD value_type = 0;
  DWORD data_size = 0;
  result = RegQueryValueExW(hKey, value_name, nullptr, &value_type, nullptr,
                            &data_size);
  if (result != ERROR_SUCCESS || value_type != REG_SZ ||
      data_size < sizeof(wchar_t) || data_size % sizeof(wchar_t) != 0) {
    RegCloseKey(hKey);
    return L"";
  }

  std::wstring value(data_size / sizeof(wchar_t), L'\0');
  result = RegQueryValueExW(hKey, value_name, nullptr, &value_type,
                            reinterpret_cast<LPBYTE>(value.data()), &data_size);
  RegCloseKey(hKey);
  if (result != ERROR_SUCCESS || value_type != REG_SZ ||
      data_size % sizeof(wchar_t) != 0) {
    return L"";
  }
  value.resize(data_size / sizeof(wchar_t));
  while (!value.empty() && value.back() == L'\0') {
    value.pop_back();
  }
  return value;
#else
  return L"";
#endif
}

}  // namespace internal

bool IsHermeticTestMode() {
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

std::string GetZenzaiUserModelDirectory() {
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

std::string GetZenzaiInstallModelDirectory() {
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

std::string GetZenzaiModelDirectory() {
  return GetZenzaiUserModelDirectory();
}

std::string GetZenzaiModelPath() {
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
  if (FileExists(user_path)) {
    return user_path;
  }
  const std::string install_path =
      GetZenzaiInstallModelDirectory() + kZenzaiModelName;
  if (FileExists(install_path)) {
    return install_path;
  }
  return user_path;
}

bool ZenzaiModelExists() { return FileExists(GetZenzaiModelPath()); }

bool IsZenzaiUserEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"ZenzaiEnabled", true);
#else
  return true;
#endif
}

bool IsTypoCorrectionEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"TypoCorrectionEnabled", true);
#else
  return true;
#endif
}

bool IsIdleResuggestEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"IdleResuggest", false);
#else
  return false;
#endif
}

bool IsTypoCorrectionUseAiEnabled() {
#ifdef _WIN32
  return internal::ReadHkcuMozcDwordAsBool(L"TypoCorrectionUseAi", false);
#else
  return false;
#endif
}

bool IsZenzaiGpuEnabled() {
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

std::wstring GetPassthroughImeOffKeys() {
#ifdef _WIN32
  return internal::ReadHkcuMozcString(L"PassthroughImeOffKeys");
#else
  return L"";
#endif
}

bool IsZenzaiEnabled() {
  return IsZenzaiUserEnabled() && ZenzaiModelExists();
}

int GetZenzaiInferenceLimit() { return 10; }

std::string GetAzooKeyDictionaryPath() { return ""; }

std::string GetAzooKeyMemoryPath() {
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
    return internal::WideToUtf8(dir);
  }
#endif
  return "";
}

std::string GetZenzaiWeightPath() {
  if (ZenzaiModelExists()) {
    return GetZenzaiModelPath();
  }
  return "";
}

std::string GetZenzaiModelVersionString() { return kZenzaiModelVersion; }

}  // namespace mozc
