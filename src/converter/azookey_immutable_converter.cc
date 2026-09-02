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
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "converter/azookey_candidate_parser.h"
#include "converter/engine_config.h"
#include "converter/segments.h"
#include "request/options.h"

namespace mozc {

#ifdef _WIN32
namespace {

// 環境変数の読み取りと UTF-16 → UTF-8 変換は engine_config.h の internal 実装を共有する
using internal::GetEnvironmentVariableValue;

std::string WideToUtf8ForLog(const std::wstring& wide) {
  const std::string utf8 = internal::WideToUtf8(wide);
  return (utf8.empty() && !wide.empty()) ? "<UTF-16 conversion failed>" : utf8;
}

}  // namespace
#endif  // _WIN32

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
  using SetTypoCorrectionEnabledFunc = void (*)(bool);
  using SetTypoCorrectionUseAiFunc = void (*)(bool);
  using SetTypoCorrectionBudgetFunc = void (*)(int);
  // myime: Optional in older azookey-engine.dll versions.
  using SetUserDictionaryFunc = int (*)(const char*);

  InitializeFunc Initialize = nullptr;
  ShutdownFunc Shutdown = nullptr;
  ConvertTextFunc ConvertText = nullptr;
  FreeStringFunc FreeString = nullptr;
  SetZenzaiEnabledFunc SetZenzaiEnabled = nullptr;
  SetZenzaiUseGpuFunc SetZenzaiUseGpu = nullptr;
  SetZenzaiInferenceLimitFunc SetZenzaiInferenceLimit = nullptr;
  SetZenzaiWeightPathFunc SetZenzaiWeightPath = nullptr;
  SetTypoCorrectionEnabledFunc SetTypoCorrectionEnabled = nullptr;
  SetTypoCorrectionUseAiFunc SetTypoCorrectionUseAi = nullptr;
  SetTypoCorrectionBudgetFunc SetTypoCorrectionBudget = nullptr;
  SetUserDictionaryFunc SetUserDictionary = nullptr;

 private:
  AzooKeyDllLoader() {
    LoadDll();
  }

  ~AzooKeyDllLoader() {
    // NOTE: プロセス終了時の静的デストラクタで FreeLibrary すると、Swift ランタイムと
    // ggml/llama のワーカースレッドが残ったまま DLL_PROCESS_DETACH に入り、ローダロックで
    // プロセスが終了しなくなる（session_handler_main と bazel test で実測: テストは全件
    // PASSED のままプロセスが残り 300 秒でタイムアウト。Zenzai 無効でも発生）。
    // DLL はプロセス終了時に OS が回収するため、ここでは解放しない。
    // 明示的なアンロードが必要な経路（必須関数の欠落時）は UnloadDll() を直接呼ぶ。
  }

  void LoadDll() {
#ifdef _WIN32
    std::wstring override_directory;
    const bool has_nonempty_override_directory =
        IsHermeticTestMode() && GetEnvironmentVariableValue(
                                    L"MYIME_AZOOKEY_DLL_DIR",
                                    &override_directory);
    // GetEnvironmentVariableValue deliberately returns false for an empty
    // value. ERROR_SUCCESS distinguishes that case from an undefined variable.
    const bool has_override_directory =
        IsHermeticTestMode() &&
        (has_nonempty_override_directory || GetLastError() == ERROR_SUCCESS);
    const bool is_drive_absolute =
        override_directory.size() >= 3 &&
        ((override_directory[0] >= L'A' && override_directory[0] <= L'Z') ||
         (override_directory[0] >= L'a' && override_directory[0] <= L'z')) &&
        override_directory[1] == L':' &&
        (override_directory[2] == L'\\' || override_directory[2] == L'/');
    const bool is_unc_absolute =
        override_directory.size() >= 2 && override_directory[0] == L'\\' &&
        override_directory[1] == L'\\';
    const bool use_hermetic_test_override =
        has_nonempty_override_directory && !override_directory.empty() &&
        (is_drive_absolute || is_unc_absolute);

    if (has_override_directory && !use_hermetic_test_override) {
      LOG(ERROR) << "MYIME_AZOOKEY_DLL_DIR は絶対パスが必要: "
                 << WideToUtf8ForLog(override_directory);
    }

    if (use_hermetic_test_override) {
      std::wstring dll_path = override_directory;
      if (dll_path.empty() ||
          (dll_path.back() != L'\\' && dll_path.back() != L'/')) {
        dll_path += L'\\';
      }
      dll_path += L"azookey-engine.dll";
      LOG(INFO) << "AzooKey DLL load path: hermetic test override, directory="
                << WideToUtf8ForLog(override_directory)
                << ", path=" << WideToUtf8ForLog(dll_path);
      dll_handle_ = LoadLibraryExW(
          dll_path.c_str(), nullptr,
          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      // A valid override is authoritative. If loading it fails, do not fall
      // back to the module directory because that would hide test setup errors.
    } else {
      // Try to load from the same directory as the executable.
      wchar_t module_path[MAX_PATH];
      HMODULE hModule = nullptr;

      // Get handle to the current module (mozc_server.exe or mozc_tip64.dll)
      if (GetModuleHandleExW(
              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
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
        const std::wstring dll_directory(module_path);
        const std::wstring dll_path =
            dll_directory + L"\\azookey-engine.dll";
        LOG(INFO) << "AzooKey DLL load path: module directory, directory="
                  << WideToUtf8ForLog(dll_directory)
                  << ", path=" << WideToUtf8ForLog(dll_path);
        dll_handle_ = LoadLibraryW(dll_path.c_str());
      }
    }

    // NOTE: 相対名での LoadLibraryW フォールバックは行わない。
    // 既定のDLL検索順はカレントディレクトリを含むため、DLLプリロード攻撃面になる。
    // 正規インストールでは DLL は必ずモジュールと同じディレクトリに存在する。
    // MYIME_HERMETIC_TEST=1 のときだけ上記のテスト専用オーバーライドを使う。
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
    SetTypoCorrectionEnabled =
        reinterpret_cast<SetTypoCorrectionEnabledFunc>(
            GetProcAddress(dll_handle_, "SetTypoCorrectionEnabled"));
    SetTypoCorrectionUseAi =
        reinterpret_cast<SetTypoCorrectionUseAiFunc>(
            GetProcAddress(dll_handle_, "SetTypoCorrectionUseAi"));
    SetTypoCorrectionBudget =
        reinterpret_cast<SetTypoCorrectionBudgetFunc>(
            GetProcAddress(dll_handle_, "SetTypoCorrectionBudget"));
    // myime: Keep this export optional for compatibility with older DLLs.
    SetUserDictionary = reinterpret_cast<SetUserDictionaryFunc>(
        GetProcAddress(dll_handle_, "SetUserDictionary"));

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
    SetTypoCorrectionEnabled = nullptr;
    SetTypoCorrectionUseAi = nullptr;
    SetTypoCorrectionBudget = nullptr;
    SetUserDictionary = nullptr;
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
  if (IsHermeticTestMode()) {
    return;
  }
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
  // タイポ補正は「変換(スペース)」と「アイドル再サジェスト(入力が 0.5 秒
  // 止まってから走る経路)」でのみ実行する。打鍵毎のサジェストに乗せると
  // 1 打鍵あたり 20〜50ms が加算され、実際に入力の引っかかりとして体感された
  // used_in_predictor_realtime_conversion: 予測器が打鍵毎に内部で行う
  // トップ候補用の変換(realtime_decoder の PushBackTopConversionResult)。
  // request_type が CONVERSION に差し替えられて届くため、除外しないと
  // タイポ補正(AI 有効時は Zenzai まで)が打鍵毎に走り、結果は
  // candidate(0) しか使われないのにコストだけ払うことになる
  const bool typo_correction_enabled =
      ((options.request_type == RequestType::CONVERSION &&
        !options.used_in_predictor_realtime_conversion) ||
       (options.idle_resuggest && IsIdleResuggestEnabled())) &&
      IsTypoCorrectionEnabled();
  // AI(Zenzai)による候補評価は 1 変換あたり 150ms 超のコストがかかるため
  // (実測: 26ms -> 154ms)、ユーザーが待つ前提の変換時のみ有効にする。
  // 打鍵毎のサジェストに乗せると入力が詰まる
  const bool typo_correction_use_ai =
      IsTypoCorrectionUseAiEnabled() &&
      options.request_type == RequestType::CONVERSION &&
      !options.used_in_predictor_realtime_conversion;
  const int typo_correction_budget =
      options.idle_resuggest
          ? 60
          : (options.request_type == RequestType::CONVERSION ? 12 : 7);

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

    if (loader.SetTypoCorrectionEnabled) {
      loader.SetTypoCorrectionEnabled(typo_correction_enabled);
    }
    if (loader.SetTypoCorrectionUseAi) {
      loader.SetTypoCorrectionUseAi(typo_correction_use_ai);
    }
    if (loader.SetTypoCorrectionBudget) {
      loader.SetTypoCorrectionBudget(typo_correction_budget);
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
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(json_candidates);
  FillSegmentWithAzooKeyCandidates(candidates, key, segment);
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

bool SetAzooKeyUserDictionary(absl::string_view json) {
  auto& loader = AzooKeyDllLoader::GetInstance();
  if (!loader.IsLoaded() || !loader.SetUserDictionary) {
    return false;
  }
  // The Swift C ABI consumes a NUL-terminated UTF-8 string.
  const std::string null_terminated_json(json);
  return loader.SetUserDictionary(null_terminated_json.c_str()) != 0;
}

}  // namespace mozc
