// Copyright 2026, MyIME Authors.
// All rights reserved.

#include "win32/tip/tip_passthrough_key.h"

#include <string>
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

bool IsEntryDelimiter(wchar_t c) {
  return c == L',' || IsAsciiWhitespace(c);
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

bool IsAsciiAlphanumeric(std::wstring_view token) {
  if (token.size() != 1) {
    return false;
  }
  const wchar_t c = ToAsciiUpper(token[0]);
  return (L'A' <= c && c <= L'Z') || (L'0' <= c && c <= L'9');
}

template <typename IsDelimiter, typename Callback>
void ForEachToken(std::wstring_view value, IsDelimiter is_delimiter,
                  Callback callback) {
  size_t begin = 0;
  while (begin < value.size()) {
    while (begin < value.size() && is_delimiter(value[begin])) {
      ++begin;
    }
    if (begin == value.size()) {
      break;
    }
    size_t end = begin;
    while (end < value.size() && !is_delimiter(value[end])) {
      ++end;
    }
    callback(value.substr(begin, end - begin));
    begin = end;
  }
}

bool ParseEntry(std::wstring_view entry, PassthroughKey* key) {
  const size_t last_plus = entry.rfind(L'+');
  if (last_plus == std::wstring_view::npos) {
    return false;
  }

  const std::wstring_view key_token = entry.substr(last_plus + 1);
  if (!IsAsciiAlphanumeric(key_token)) {
    return false;
  }

  size_t begin = 0;
  while (begin <= last_plus) {
    const size_t plus = entry.find(L'+', begin);
    const size_t end = plus == std::wstring_view::npos ? entry.size() : plus;
    const std::wstring_view modifier = entry.substr(begin, end - begin);
    if (EqualsIgnoreAsciiCase(modifier, L"Ctrl")) {
      key->ctrl = true;
    } else if (EqualsIgnoreAsciiCase(modifier, L"Alt")) {
      key->alt = true;
    } else if (EqualsIgnoreAsciiCase(modifier, L"Shift")) {
      key->shift = true;
    } else {
      return false;
    }
    if (plus == last_plus) {
      break;
    }
    begin = plus + 1;
  }

  key->vk = static_cast<UINT>(ToAsciiUpper(key_token[0]));
  return key->ctrl || key->alt || key->shift;
}

}  // namespace

std::vector<PassthroughKey> ParsePassthroughKeys(std::wstring_view config) {
  std::vector<PassthroughKey> keys;
  ForEachToken(config, IsEntryDelimiter, [&](std::wstring_view token) {
    PassthroughKey key;
    if (ParseEntry(token, &key)) {
      keys.push_back(key);
    }
  });
  return keys;
}

bool ValidatePassthroughKeyEntry(bool ctrl, bool alt, bool shift,
                                 std::wstring_view key,
                                 PassthroughKeyEntryError* error) {
  PassthroughKeyEntryError validation_error;
  validation_error.no_modifier = !ctrl && !alt && !shift;
  validation_error.invalid_key = !IsAsciiAlphanumeric(key);
  const bool valid = !validation_error.no_modifier &&
                     !validation_error.invalid_key;
  if (error != nullptr) {
    *error = validation_error;
  }
  return valid;
}

std::wstring FormatPassthroughKey(bool ctrl, bool alt, bool shift,
                                  std::wstring_view key) {
  std::wstring result;
  const auto append_modifier = [&result](std::wstring_view modifier) {
    if (!result.empty()) {
      result.push_back(L'+');
    }
    result.append(modifier);
  };
  if (ctrl) {
    append_modifier(L"Ctrl");
  }
  if (alt) {
    append_modifier(L"Alt");
  }
  if (shift) {
    append_modifier(L"Shift");
  }
  if (!result.empty() && !key.empty()) {
    result.push_back(L'+');
  }
  for (const wchar_t c : key) {
    result.push_back(ToAsciiUpper(c));
  }
  return result;
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
