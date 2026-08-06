// Copyright 2026, MyIME Authors.
// All rights reserved.

#ifndef MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_
#define MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"

namespace mozc {
namespace win32 {
namespace tsf {

struct PassthroughKey {
  UINT vk = 0;
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
};

struct PassthroughKeyEntryError {
  bool no_modifier = false;
  bool invalid_key = false;
};

std::vector<PassthroughKey> ParsePassthroughKeys(std::wstring_view config);

bool ValidatePassthroughKeyEntry(bool ctrl, bool alt, bool shift,
                                 std::wstring_view key,
                                 PassthroughKeyEntryError* error);

std::wstring FormatPassthroughKey(bool ctrl, bool alt, bool shift,
                                  std::wstring_view key);

bool MatchesPassthroughKey(absl::Span<const PassthroughKey> keys, UINT vk,
                           bool ctrl, bool alt, bool shift);

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_
