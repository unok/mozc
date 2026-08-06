// Copyright 2026, MyIME Authors.
// All rights reserved.

#include "win32/tip/tip_passthrough_key.h"

#include <windows.h>

#include <string>
#include <vector>

#include "testing/gunit.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

TEST(ParsePassthroughKeysTest, EmptyConfig) {
  EXPECT_TRUE(ParsePassthroughKeys(L"").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L" , \t, ").empty());
}

TEST(ParsePassthroughKeysTest, ParsesMixedCombinations) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+T Alt+B Ctrl+Shift+M");

  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_FALSE(keys[0].alt);
  EXPECT_FALSE(keys[0].shift);
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('B'));
  EXPECT_FALSE(keys[1].ctrl);
  EXPECT_TRUE(keys[1].alt);
  EXPECT_FALSE(keys[1].shift);
  EXPECT_EQ(keys[2].vk, static_cast<UINT>('M'));
  EXPECT_TRUE(keys[2].ctrl);
  EXPECT_FALSE(keys[2].alt);
  EXPECT_TRUE(keys[2].shift);
}

TEST(ParsePassthroughKeysTest, AcceptsCommaSeparatorsAndAsciiCase) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"cTrL+t,aLt+b,CTRL+sHiFt+m");

  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('B'));
  EXPECT_EQ(keys[2].vk, static_cast<UINT>('M'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_TRUE(keys[1].alt);
  EXPECT_TRUE(keys[2].ctrl);
  EXPECT_TRUE(keys[2].shift);
}

TEST(ParsePassthroughKeysTest, IgnoresInvalidEntries) {
  const std::vector<PassthroughKey> keys = ParsePassthroughKeys(
      L"T Ctrl+T Meta+B Ctrl+Meta+M Alt+tt Shift+! Alt+9");

  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('9'));
  EXPECT_TRUE(keys[1].alt);
}

TEST(ValidatePassthroughKeyEntryTest, AcceptsValidEntryAndNullError) {
  PassthroughKeyEntryError error;
  error.no_modifier = true;
  error.invalid_key = true;

  EXPECT_TRUE(
      ValidatePassthroughKeyEntry(true, false, true, L"t", &error));
  EXPECT_FALSE(error.no_modifier);
  EXPECT_FALSE(error.invalid_key);
  EXPECT_TRUE(
      ValidatePassthroughKeyEntry(false, true, false, L"7", nullptr));
}

TEST(ValidatePassthroughKeyEntryTest, ReportsMissingModifier) {
  PassthroughKeyEntryError error;
  EXPECT_FALSE(
      ValidatePassthroughKeyEntry(false, false, false, L"T", &error));
  EXPECT_TRUE(error.no_modifier);
  EXPECT_FALSE(error.invalid_key);
}

TEST(ValidatePassthroughKeyEntryTest, ReportsInvalidKey) {
  for (const std::wstring& key : {std::wstring(), std::wstring(L"tt"),
                                  std::wstring(L"!")}) {
    PassthroughKeyEntryError error;
    EXPECT_FALSE(
        ValidatePassthroughKeyEntry(true, false, false, key, &error));
    EXPECT_FALSE(error.no_modifier);
    EXPECT_TRUE(error.invalid_key);
  }
}

TEST(ValidatePassthroughKeyEntryTest, ReportsAllApplicableErrors) {
  PassthroughKeyEntryError error;
  EXPECT_FALSE(
      ValidatePassthroughKeyEntry(false, false, false, L"", &error));
  EXPECT_TRUE(error.no_modifier);
  EXPECT_TRUE(error.invalid_key);
}

TEST(FormatPassthroughKeyTest, UsesCanonicalOrderAndCase) {
  EXPECT_EQ(FormatPassthroughKey(true, true, true, L"t"),
            L"Ctrl+Alt+Shift+T");
  EXPECT_EQ(FormatPassthroughKey(true, false, true, L"m"),
            L"Ctrl+Shift+M");
  EXPECT_EQ(FormatPassthroughKey(false, true, false, L"7"), L"Alt+7");
}

TEST(MatchesPassthroughKeyTest, RequiresExactKeyAndModifiers) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+T Alt+B Ctrl+Shift+M");

  EXPECT_TRUE(MatchesPassthroughKey(keys, 'T', true, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', false, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', true, true, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'B', true, false, false));
  EXPECT_TRUE(MatchesPassthroughKey(keys, 'B', false, true, false));
  EXPECT_TRUE(MatchesPassthroughKey(keys, 'M', true, false, true));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'M', true, false, false));
  EXPECT_FALSE(MatchesPassthroughKey({}, 'T', true, false, false));
}

}  // namespace
}  // namespace tsf
}  // namespace win32
}  // namespace mozc
