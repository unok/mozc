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
  EXPECT_TRUE(ParsePassthroughKeys(L"").empty());
  EXPECT_TRUE(ParsePassthroughKeys(L" \t\r\n").empty());
}

TEST(ParsePassthroughKeysTest, ParsesMultipleKeysCaseInsensitively) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+T cTrL+aLt+x SHIFT+q");

  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('T'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_FALSE(keys[0].alt);
  EXPECT_FALSE(keys[0].shift);
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('X'));
  EXPECT_TRUE(keys[1].ctrl);
  EXPECT_TRUE(keys[1].alt);
  EXPECT_FALSE(keys[1].shift);
  EXPECT_EQ(keys[2].vk, static_cast<UINT>('Q'));
  EXPECT_FALSE(keys[2].ctrl);
  EXPECT_FALSE(keys[2].alt);
  EXPECT_TRUE(keys[2].shift);
}

TEST(ParsePassthroughKeysTest, ParsesDigitKeysWithModifiers) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"a Ctrl+7 Alt+Shift+0");

  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('7'));
  EXPECT_TRUE(keys[0].ctrl);
  EXPECT_EQ(keys[1].vk, static_cast<UINT>('0'));
  EXPECT_TRUE(keys[1].alt);
  EXPECT_TRUE(keys[1].shift);
}

TEST(ParsePassthroughKeysTest, IgnoresKeysWithoutModifiers) {
  EXPECT_TRUE(ParsePassthroughKeys(L"T 7").empty());
}

TEST(ParsePassthroughKeysTest, IgnoresInvalidTokens) {
  EXPECT_TRUE(ParsePassthroughKeys(L"Ctrl+ Meta+T Ctrl++T").empty());

  const std::vector<PassthroughKey> keys = ParsePassthroughKeys(
      L"Ctrl+ Ctrl++T Meta+T Ctrl+TT Ctrl+Ctrl+T T+Ctrl Ctrl+! Alt+Z");

  ASSERT_EQ(keys.size(), 1u);
  EXPECT_EQ(keys[0].vk, static_cast<UINT>('Z'));
  EXPECT_FALSE(keys[0].ctrl);
  EXPECT_TRUE(keys[0].alt);
  EXPECT_FALSE(keys[0].shift);
}

TEST(MatchesPassthroughKeyTest, RequiresExactKeyAndModifiers) {
  const std::vector<PassthroughKey> keys =
      ParsePassthroughKeys(L"Ctrl+T Ctrl+Alt+X Shift+7");

  EXPECT_TRUE(MatchesPassthroughKey(keys, 'T', true, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', false, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'T', true, false, true));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'Q', true, false, false));

  EXPECT_TRUE(MatchesPassthroughKey(keys, 'X', true, true, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'X', true, false, false));
  EXPECT_FALSE(MatchesPassthroughKey(keys, 'X', true, true, true));

  EXPECT_TRUE(MatchesPassthroughKey(keys, '7', false, false, true));
  EXPECT_FALSE(MatchesPassthroughKey({}, 'T', true, false, false));
}

}  // namespace
}  // namespace tsf
}  // namespace win32
}  // namespace mozc
