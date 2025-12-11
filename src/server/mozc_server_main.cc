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
// Returns true if server should continue, false if it should exit.
bool CheckZenzaiModelAndPrompt() {
  if (mozc::ZenzaiModelExists()) {
    return true;  // Model exists, continue normally
  }

  // Model doesn't exist, ask user
  int result = MessageBoxW(
      nullptr,
      L"Zenzai AI\u30e2\u30c7\u30eb\u304c\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u3055\u308c\u3066\u3044\u307e\u305b\u3093\u3002\n\n"
      L"\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u3057\u307e\u3059\u304b\uff1f\n\n"
      L"\u300c\u306f\u3044\u300d\u3092\u9078\u629e\u3059\u308b\u3068\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u753b\u9762\u3092\u958b\u304d\u307e\u3059\u3002\n"
      L"\u300c\u3044\u3044\u3048\u300d\u3092\u9078\u629e\u3059\u308b\u3068IME\u3092\u8d77\u52d5\u3057\u307e\u305b\u3093\u3002",
      L"Mozc - Zenzai Model Required",
      MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);

  if (result == IDYES) {
    // Launch mozc_tool with zenzai_download mode
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
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = tool_path.c_str();
        sei.lpParameters = args.c_str();
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&sei)) {
          // Wait for download tool to finish
          if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
          }
          // Check again if model was downloaded
          return mozc::ZenzaiModelExists();
        }
      }
    }
    // Failed to launch tool
    MessageBoxW(
        nullptr,
        L"\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\u30c4\u30fc\u30eb\u306e\u8d77\u52d5\u306b\u5931\u6557\u3057\u307e\u3057\u305f\u3002",
        L"Mozc - Error",
        MB_OK | MB_ICONERROR);
    return false;
  }

  // User chose "No"
  return false;
}

}  // namespace
#endif  // _WIN32

int main(int argc, char* argv[]) {
#ifdef _WIN32
  // Check for Zenzai model before starting server
  if (!CheckZenzaiModelAndPrompt()) {
    return 1;  // Exit if user declined or download failed
  }
#endif  // _WIN32

  mozc::server::InitMozcAndMozcServer(argv[0], &argc, &argv, false);

  const int return_value = mozc::server::MozcServer::Run();
  mozc::server::MozcServer::Finalize();
  return return_value;
}
