// Copyright 2026, MyIME Authors.
// All rights reserved.

#ifndef MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_
#define MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_

#include <windows.h>

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

std::vector<PassthroughKey> ParsePassthroughKeys(std::wstring_view config);

bool MatchesPassthroughKey(absl::Span<const PassthroughKey> keys, UINT vk,
                           bool ctrl, bool alt, bool shift);

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_PASSTHROUGH_KEY_H_
