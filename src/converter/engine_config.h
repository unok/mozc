// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Engine configuration for AzooKey.

#ifndef MOZC_CONVERTER_ENGINE_CONFIG_H_
#define MOZC_CONVERTER_ENGINE_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>

namespace mozc {

// Hermetic test-mode behavior:
//   MYIME_HERMETIC_TEST enables hermetic isolation when exactly "1".
//   Pass --test_env=MYIME_HERMETIC_TEST=1 from bazel test.
//   TEST_TMPDIR is used only to determine the learning storage location.
//   MYIME_AZOOKEY_DLL_DIR selects the test-only AzooKey DLL directory.
//   MYIME_AZOOKEY_ZENZAI_WEIGHT selects the test-only Zenzai GGUF file.
//   MYIME_AZOOKEY_ZENZAI_GPU enables test-only GPU inference when exactly "1".

constexpr const char* kZenzaiModelName = "ggml-model-Q5_K_M.gguf";
constexpr const char* kZenzaiModelVersion = "zenz-v3.2-small";

// Returns true only when MYIME_HERMETIC_TEST is exactly "1". The first
// evaluation is cached in a function-local static variable.
// Always returns false on non-Windows.
bool IsHermeticTestMode();

// Returns %LOCALAPPDATA%\Mozc\models\, or an empty string in hermetic mode.
std::string GetZenzaiUserModelDirectory();

// Returns %ProgramFiles(x86)%\Mozc\models\, or an empty string in hermetic
// mode.
std::string GetZenzaiInstallModelDirectory();

// Returns the writable user model directory. Empty in hermetic mode.
std::string GetZenzaiModelDirectory();

namespace internal {

// Reads a wide-character environment variable into `value`.
// Always returns false on non-Windows.
bool GetEnvironmentVariableValue(const wchar_t* name, std::wstring* value);

// Converts a UTF-16 string to UTF-8, returning an empty string on failure.
// Always returns an empty string on non-Windows.
std::string WideToUtf8(const std::wstring& wide);

// Reads a REG_DWORD under HKCU\Software\Mozc. Returns nullopt when missing,
// of the wrong type, unreadable, or in hermetic mode.
// Always returns nullopt on non-Windows.
std::optional<uint32_t> ReadHkcuMozcDword(const wchar_t* value_name);

// Reads a REG_DWORD under HKCU\Software\Mozc and converts it to bool, using
// `default_value` when missing, of the wrong type, unreadable, or hermetic.
// Always returns `default_value` on non-Windows.
bool ReadHkcuMozcDwordAsBool(const wchar_t* value_name, bool default_value);

// Reads a REG_SZ under HKCU\Software\Mozc, returning empty on failure or in
// hermetic mode.
// Always returns an empty string on non-Windows.
std::wstring ReadHkcuMozcString(const wchar_t* value_name);

}  // namespace internal

// Checks the user-side model path first, then the install-side path. If neither
// exists, returns the user-side path anyway. In hermetic mode, uses only
// MYIME_AZOOKEY_ZENZAI_WEIGHT.
std::string GetZenzaiModelPath();

// Returns true when the configured Zenzai model file exists on disk.
bool ZenzaiModelExists();

// Missing value or read/type failures default to enabled.
bool IsZenzaiUserEnabled();

// Missing value or read/type failures default to enabled.
bool IsTypoCorrectionEnabled();

// Missing value or read/type failures default to disabled.
bool IsIdleResuggestEnabled();

// Missing value or read/type failures default to disabled.
bool IsTypoCorrectionUseAiEnabled();

// Reads ZenzaiUseGpu (default 0). In hermetic mode, returns true only when
// MYIME_AZOOKEY_ZENZAI_GPU is exactly "1".
bool IsZenzaiGpuEnabled();

// Reads the PassthroughImeOffKeys REG_SZ value under HKCU\Software\Mozc.
std::wstring GetPassthroughImeOffKeys();

// Returns true only when the user setting is enabled and the model exists.
bool IsZenzaiEnabled();

// Returns the fixed Zenzai inference limit.
int GetZenzaiInferenceLimit();

// Returns the AzooKey dictionary path; empty means use the built-in dictionary.
std::string GetAzooKeyDictionaryPath();

// Returns %APPDATA%\Mozc\azookey_memory, creating intermediate directories;
// failure returns empty and disables learning. In hermetic mode, returns
// TEST_TMPDIR\azookey_memory, falling back to
// %TEMP%\myime-hermetic\azookey_memory when TEST_TMPDIR is unset.
std::string GetAzooKeyMemoryPath();

// Returns the model path if it exists on disk, otherwise an empty string.
std::string GetZenzaiWeightPath();

// Returns the Zenzai model version string used for display.
std::string GetZenzaiModelVersionString();

}  // namespace mozc

#endif  // MOZC_CONVERTER_ENGINE_CONFIG_H_
