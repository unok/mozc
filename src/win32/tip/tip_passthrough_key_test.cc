// Copyright 2026, MyIME Authors.
// All rights reserved.

#include "win32/tip/tip_passthrough_key.h"

#include <windows.h>

#include <vector>

#include "testing/gunit.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

TEST(ParsePassthroughKeysTest, EmptyConfig) {
  EXPECT_TRUE(ParsePassthroughKeys(L"", L"T").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L"Ctrl", L"").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L"Ctrl", L" , \t, ").empty());
}

TEST(ParsePassthroughKeysTest, AcceptsSeparatorsAndAsciiCase) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"cTrL", L"t, Q m,W 7");

  ASSERT_EQ(keys.size(), 5u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('Q'));
  EXPECT_EQ(keys[2].vk, static_cast<UINT>('M'));
  EXPECT_EQ(keys[3].vk, static_cast<UINT>('W'));
  EXPECT_EQ(keys[4].vk, static_cast<UINT>('7'));
  for (const PassthroughKey& key : keys) {
    EXPECT_TRUE(key.ctrl);
    EXPECT_FALSE(key.alt);
    EXPECT_FALSE(key.shift);
  }
}

TEST(ParsePassthroughKeysTest, AcceptsMultipleModifierSeparators) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+aLt Shift", L"x");

  ASSERT_EQ(keys.size(), 1u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('X'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_TRUE(keys[0].alt);
  EXPECT_TRUE(keys[0].shift);
}

TEST(ParsePassthroughKeysTest, RejectsMissingOrUnknownModifiers) {
  EXPECT_TRUE(ParsePassthroughKeys(L"", L"T").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L"Meta", L"T").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L"Ctrl+Meta", L"T").empty());
}

TEST(ParsePassthroughKeysTest, IgnoresInvalidAndEmptyKeyTokens) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Alt", L", T,, tt, !, 9, ");

  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('9'));
}

TEST(ValidatePassthroughKeyConfigTest, EmptyKeysDisableFeature) {
  PassthroughKeyConfigError error;
  error.no_modifier = true;
  error.invalid_keys.push_back(L"old");

  EXPECT_TRUE(ValidatePassthroughKeyConfig(L"", L"", &error));
  EXPECT_FALSE(error.no_modifier);
  EXPECT_TRUE(error.invalid_keys.empty());
  EXPECT_TRUE(ValidatePassthroughKeyConfig(L"", L" , \t, ", &error));
}

TEST(ValidatePassthroughKeyConfigTest, RequiresModifierForKeyTokens) {
  PassthroughKeyConfigError error;
  EXPECT_FALSE(ValidatePassthroughKeyConfig(L"", L"T, tt", &error));
  EXPECT_TRUE(error.no_modifier);
  ASSERT_EQ(error.invalid_keys.size(), 1u);
  EXPECT_EQ(error.invalid_keys[0], L"tt");
}

TEST(ValidatePassthroughKeyConfigTest, ReportsInvalidKeyTokens) {
  PassthroughKeyConfigError error;
  EXPECT_FALSE(
      ValidatePassthroughKeyConfig(L"Ctrl+Shift", L"T, tt ! 7", &error));
  EXPECT_FALSE(error.no_modifier);
  ASSERT_EQ(error.invalid_keys.size(), 2u);
  EXPECT_EQ(error.invalid_keys[0], L"tt");
  EXPECT_EQ(error.invalid_keys[1], L"!");
}

TEST(ValidatePassthroughKeyConfigTest, AcceptsValidMixedSeparators) {
  PassthroughKeyConfigError error;
  EXPECT_TRUE(
      ValidatePassthroughKeyConfig(L"Ctrl, Alt Shift", L"t, Q 7", &error));
  EXPECT_FALSE(error.no_modifier);
  EXPECT_TRUE(error.invalid_keys.empty());
}

TEST(MatchesPassthroughKeyTest, RequiresExactKeyAndModifiers) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+Alt", L"T X");

  EXPECT_TRUE(MatchesPassthroughKey(keys, 'T', true, true, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', false, true, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', true, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', true, true, true));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'Q', true, true, false));
  EXPECT_TRUE(MatchesPassthroughKey(keys, 'X', true, true, false));
  EXPECT_FALSE(MatchesPassthroughKey({}, 'T', true, true, false));
}

}  // namespace
}  // namespace tsf
}  // namespace win32
}  // namespace mozc
