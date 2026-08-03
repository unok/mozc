// Copyright 2026, MyIME Authors.
// All rights reserved.

#include "win32/tip/tip_passthrough_key.h"

#include <string_view>
#include <vector>

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

bool IsAsciiWhitespace(wchar_t c) {
  return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
         c == L'\f' || c == L'\v';
}

wchar_t ToAsciiUpper(wchar_t c) {
  return (L'a' <= c && c <= L'z') ? c - L'a' + L'A' : c;
}

bool EqualsIgnoreAsciiCase(std::wstring_view lhs, std::wstring_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (ToAsciiUpper(lhs[i]) != ToAsciiUpper(rhs[i])) {
      return false;
    }
  }
  return true;
}

bool ParseToken(std::wstring_view token, PassthroughKey* key) {
  PassthroughKey parsed;
  size_t begin = 0;
  while (begin < token.size()) {
    const size_t plus = token.find(L'+', begin);
    const bool is_last = plus == std::wstring_view::npos;
    const std::wstring_view part = token.substr(
        begin, is_last ? std::wstring_view::npos : plus - begin);
    if (part.empty()) {
      return false;
    }

    if (is_last) {
      if (part.size() != 1) {
        return false;
      }
      if (!parsed.ctrl && !parsed.alt && !parsed.shift) {
        return false;
      }
      const wchar_t c = ToAsciiUpper(part[0]);
      if (!((L'A' <= c && c <= L'Z') || (L'0' <= c && c <= L'9'))) {
        return false;
      }
      parsed.vk = static_cast<UINT>(c);
      *key = parsed;
      return true;
    }

    if (EqualsIgnoreAsciiCase(part, L"Ctrl")) {
      if (parsed.ctrl) {
        return false;
      }
      parsed.ctrl = true;
    } else if (EqualsIgnoreAsciiCase(part, L"Alt")) {
      if (parsed.alt) {
        return false;
      }
      parsed.alt = true;
    } else if (EqualsIgnoreAsciiCase(part, L"Shift")) {
      if (parsed.shift) {
        return false;
      }
      parsed.shift = true;
    } else {
      return false;
    }
    begin = plus + 1;
  }
  return false;
}

}  // namespace

std::vector<PassthroughKey> ParsePassthroughKeys(std::wstring_view config) {
  std::vector<PassthroughKey> keys;
  size_t begin = 0;
  while (begin < config.size()) {
    while (begin < config.size() && IsAsciiWhitespace(config[begin])) {
      ++begin;
    }
    if (begin == config.size()) {
      break;
    }

    size_t end = begin;
    while (end < config.size() && !IsAsciiWhitespace(config[end])) {
      ++end;
    }
    PassthroughKey key;
    if (ParseToken(config.substr(begin, end - begin), &key)) {
      keys.push_back(key);
    }
    begin = end;
  }
  return keys;
}

bool MatchesPassthroughKey(absl::Span<const PassthroughKey> keys, UINT vk,
                           bool ctrl, bool alt, bool shift) {
  for (const PassthroughKey& key : keys) {
    if (key.vk == vk && key.ctrl == ctrl && key.alt == alt &&
        key.shift == shift) {
      return true;
    }
  }
  return false;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
