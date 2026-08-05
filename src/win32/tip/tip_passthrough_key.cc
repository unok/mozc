// Copyright 2026, MyIME Authors.
// All rights reserved.

#include "win32/tip/tip_passthrough_key.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

bool IsAsciiWhitespace(wchar_t c) {
  return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
         c == L'\f' || c == L'\v';
}

bool IsModifierDelimiter(wchar_t c) {
  return c == L'+' || c == L',' || IsAsciiWhitespace(c);
}

bool IsKeyDelimiter(wchar_t c) {
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

bool ParseModifiers(std::wstring_view modifiers, PassthroughKey* key) {
  bool valid = true;
  bool found = false;
  ForEachToken(modifiers, IsModifierDelimiter, [&](std::wstring_view token) {
    found = true;
    if (EqualsIgnoreAsciiCase(token, L"Ctrl")) {
      key->ctrl = true;
    } else if (EqualsIgnoreAsciiCase(token, L"Alt")) {
      key->alt = true;
    } else if (EqualsIgnoreAsciiCase(token, L"Shift")) {
      key->shift = true;
    } else {
      valid = false;
    }
  });
  return valid && found && (key->ctrl || key->alt || key->shift);
}

}  // namespace

std::vector<PassthroughKey> ParsePassthroughKeys(
    std::wstring_view modifiers, std::wstring_view key_config) {
  PassthroughKey modifier_state;
  if (!ParseModifiers(modifiers, &modifier_state)) {
    return {};
  }

  std::vector<PassthroughKey> keys;
  ForEachToken(key_config, IsKeyDelimiter, [&](std::wstring_view token) {
    if (IsAsciiAlphanumeric(token)) {
      PassthroughKey key = modifier_state;
      key.vk = static_cast<UINT>(ToAsciiUpper(token[0]));
      keys.push_back(key);
    }
  });
  return keys;
}

bool ValidatePassthroughKeyConfig(std::wstring_view modifiers,
                                  std::wstring_view keys,
                                  PassthroughKeyConfigError* error) {
  PassthroughKeyConfigError validation_error;
  bool has_key_token = false;
  ForEachToken(keys, IsKeyDelimiter, [&](std::wstring_view token) {
    has_key_token = true;
    if (!IsAsciiAlphanumeric(token)) {
      validation_error.invalid_keys.emplace_back(token);
    }
  });

  if (!has_key_token) {
    if (error != nullptr) {
      *error = {};
    }
    return true;
  }

  PassthroughKey modifier_state;
  validation_error.no_modifier =
      !ParseModifiers(modifiers, &modifier_state) ||
      !(modifier_state.ctrl || modifier_state.alt || modifier_state.shift);
  const bool valid = !validation_error.no_modifier &&
                     validation_error.invalid_keys.empty();
  if (error != nullptr) {
    *error = std::move(validation_error);
  }
  return valid;
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
