// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "base/win32/winmain.h"
#endif  // _WIN32
#include "converter/engine_config.h"
#include "server/mozc_server.h"

#ifdef _WIN32
namespace {

// Check if Zenzai model exists and prompt user to download if not.
// NOTE: \u30e2\u30c7\u30eb\u4e0d\u5728\u3067\u3082\u30b5\u30fc\u30d0\u306f\u5fc5\u305a\u8d77\u52d5\u3059\u308b\uff08Zenzai\u7121\u52b9\u306e\u307e\u307eAzooKey\u5909\u63db\u306f\u52d5\u4f5c\u3059\u308b
// \u8a2d\u8a08\u3002engine_config.h \u306e IsZenzaiEnabled / engine.cc \u306e NoOp \u30d5\u30a9\u30fc\u30eb\u30d0\u30c3\u30af\u3068\u6574\u5408\uff09\u3002
// \u4ee5\u524d\u306f\u3053\u3053\u3067\u8d77\u52d5\u62d2\u5426(return 1)\u3057\u3066\u3044\u305f\u304c\u3001TSF\u30af\u30e9\u30a4\u30a2\u30f3\u30c8\u304c\u30b5\u30fc\u30d0\u8d77\u52d5\u3092
// \u30ea\u30c8\u30e9\u30a4\u3059\u308b\u305f\u3073\u306b\u30c0\u30a4\u30a2\u30ed\u30b0\u304c\u518d\u8868\u793a\u3055\u308c\u3001\u304b\u306a\u5165\u529b\u3059\u3089\u4e0d\u80fd\u306b\u306a\u308b\u305f\u3081\u5ec3\u6b62\u3002
void CheckZenzaiModelAndPrompt() {
  if (mozc::ZenzaiModelExists()) {
    return;  // Model exists, continue normally
  }

  // Model doesn't exist, ask user
  int result = MessageBoxW(
      nullptr,
      L"Zenzai AI\u30e2\u30c7\u30eb\u304c\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3055\u308c\u3066\u3044\u307e\u305b\u3093\u3002\n\n"
      L"\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u307e\u3059\u304b\uff1f\n\n"
      L"\u300c\u306f\u3044\u300d\u3092\u9078\u629e\u3059\u308b\u3068\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u753b\u9762\u3092\u958b\u304d\u307e\u3059\u3002\n"
      L"\u300c\u3044\u3044\u3048\u300d\u3092\u9078\u629e\u3057\u3066\u3082IME\u306f\u8d77\u52d5\u3057\u307e\u3059\uff08Zenzai\u306f\u7121\u52b9\uff09\u3002",
      L"Mozc - Zenzai Model",
      MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);

  if (result == IDYES) {
    // Launch mozc_tool with zenzai_download mode (do not block server startup)
    wchar_t module_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) > 0) {
      // Get directory of current executable
      std::wstring path(module_path);
      size_t pos = path.find_last_of(L"\\/");
      if (pos != std::wstring::npos) {
        std::wstring tool_path = path.substr(0, pos + 1) + L"mozc_tool.exe";
        std::wstring args = L"--mode=zenzai_download";

        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpFile = tool_path.c_str();
        sei.lpParameters = args.c_str();
        sei.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) {
          MessageBoxW(
              nullptr,
              L"\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u30c4\u30fc\u30eb\u306e\u8d77\u52d5\u306b\u5931\u6557\u3057\u307e\u3057\u305f\u3002",
              L"Mozc - Error",
              MB_OK | MB_ICONERROR);
        }
      }
    }
  }
  // \u30e2\u30c7\u30eb\u304c\u7528\u610f\u3055\u308c\u308b\u307e\u3067\u306f Zenzai \u7121\u52b9\u3067\u7d9a\u884c\uff08\u6b21\u56de\u30b5\u30fc\u30d0\u8d77\u52d5\u6642\u306b\u518d\u6848\u5185\uff09
}

}  // namespace
#endif  // _WIN32

int main(int argc, char* argv[]) {
#ifdef _WIN32
  // Inform the user about the Zenzai model, but never block server startup.
  CheckZenzaiModelAndPrompt();
#endif  // _WIN32

  mozc::server::InitMozcAndMozcServer(argv[0], &argc, &argv, false);

  const int return_value = mozc::server::MozcServer::Run();
  mozc::server::MozcServer::Finalize();
  return return_value;
}
