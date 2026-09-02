// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_candidate_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "converter/attribute.h"
#include "converter/segments.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

TEST(AzooKeyCandidateParserTest, ParsesNormalCandidateArray) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"text":"候補","correspondingCount":3},)"
          R"({"text":"第二候補","correspondingCount":4}])");

  ASSERT_EQ(candidates.size(), 2);
  EXPECT_EQ(candidates[0].text, "候補");
  EXPECT_EQ(candidates[0].corresponding_count, 3);
  EXPECT_FALSE(candidates[0].typo_corrected);
  EXPECT_EQ(candidates[1].text, "第二候補");
  EXPECT_EQ(candidates[1].corresponding_count, 4);
}

TEST(AzooKeyCandidateParserTest, ParsesTypoCorrectionFields) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"text":"マンション","correspondingCount":5,)"
          R"("typoCorrected":true,"correctedReading":"まんしょん"}])");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_TRUE(candidates[0].typo_corrected);
  EXPECT_EQ(candidates[0].corrected_reading, "まんしょん");
}

TEST(AzooKeyCandidateParserTest, DecodesUnicodeEscapesAndSurrogatePair) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"text":"\u5019\u88dc\uD83D\uDE00",)"
          R"("correspondingCount":3}])");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].text, "候補😀");
}

TEST(AzooKeyCandidateParserTest, ParsesEmptyArray) {
  EXPECT_TRUE(ParseAzooKeyCandidateJson("[]").empty());
}

TEST(AzooKeyCandidateParserTest, RejectsInputWithoutArray) {
  EXPECT_TRUE(ParseAzooKeyCandidateJson(R"({"text":"候補"})").empty());
}

TEST(AzooKeyCandidateParserTest, HandlesTruncatedInput) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"text":"候補","correspondingCount":2)");
  EXPECT_EQ(candidates.size(), 1);
  ASSERT_FALSE(candidates.empty());
  EXPECT_EQ(candidates[0].text, "候補");
  EXPECT_EQ(candidates[0].corresponding_count, 2);
}

TEST(AzooKeyCandidateParserTest,
     SkipsNonStringCorrectedReadingAndParsesFollowingCandidate) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"text":"AB","correspondingCount":2,"correctedReading":null},)"
          R"({"text":"CD","correspondingCount":2}])");

  ASSERT_EQ(candidates.size(), 2);
  EXPECT_EQ(candidates[0].text, "AB");
  EXPECT_EQ(candidates[0].corresponding_count, 2);
  EXPECT_TRUE(candidates[0].corrected_reading.empty());
  EXPECT_EQ(candidates[1].text, "CD");
  EXPECT_EQ(candidates[1].corresponding_count, 2);
}

TEST(AzooKeyCandidateParserTest,
     SkipsNonStringCorrectedReadingBeforeTextField) {
  const std::vector<AzooKeyCandidateInfo> candidates =
      ParseAzooKeyCandidateJson(
          R"([{"correctedReading":null,"text":"AB","correspondingCount":2}])");

  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates[0].text, "AB");
  EXPECT_EQ(candidates[0].corresponding_count, 2);
  EXPECT_TRUE(candidates[0].corrected_reading.empty());
}

TEST(AzooKeyCandidateParserTest, BuildsExactMatchCandidate) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {{"変換", 4}};
  FillSegmentWithAzooKeyCandidates(candidates, "へんかん", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  const converter::Candidate& candidate = segment.candidate(0);
  EXPECT_EQ(candidate.key, "へんかん");
  EXPECT_EQ(candidate.value, "変換");
  EXPECT_EQ(candidate.content_key, "へんかん");
  EXPECT_EQ(candidate.content_value, "変換");
  EXPECT_EQ(candidate.cost, 0);
  EXPECT_EQ(candidate.wcost, 0);
  EXPECT_EQ(candidate.structure_cost, 0);
  EXPECT_EQ(candidate.consumed_key_size, 4);
  EXPECT_EQ(candidate.lid, 0);
  EXPECT_EQ(candidate.rid, 0);
}

TEST(AzooKeyCandidateParserTest, AppendsRemainingKeyForPartialMatch) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {{"今日", 2}};
  FillSegmentWithAzooKeyCandidates(candidates, "きょうです", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "今日うです");
  EXPECT_EQ(segment.candidate(0).consumed_key_size, 5);
}

TEST(AzooKeyCandidateParserTest, SkipsCandidateLongerThanKey) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {
      {"長すぎる", 4}, {"有効", 2}};
  FillSegmentWithAzooKeyCandidates(candidates, "かな", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "有効");
}

TEST(AzooKeyCandidateParserTest, ExcludesNonPositiveCorrespondingCount) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {
      {"ゼロ", 0}, {"負", -1}, {"有効", 2}};
  FillSegmentWithAzooKeyCandidates(candidates, "かな", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "有効");
}

TEST(AzooKeyCandidateParserTest, FallsBackToKeyAfterFiltering) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {
      {"ゼロ", 0}, {"長すぎる", 3}};
  FillSegmentWithAzooKeyCandidates(candidates, "かな", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "かな");
  EXPECT_EQ(segment.candidate(0).consumed_key_size, 2);
}

TEST(AzooKeyCandidateParserTest, AppliesTypoCostsAndAttributes) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {
      {"補正一", 2, true, "ほせいいち"},
      {"補正二", 2, true, "ほせいに"},
      {"通常", 2, false, ""}};
  FillSegmentWithAzooKeyCandidates(candidates, "かな", &segment);

  ASSERT_EQ(segment.candidates_size(), 3);
  EXPECT_EQ(segment.candidate(0).cost, 10000);
  EXPECT_EQ(segment.candidate(0).wcost, 10000);
  EXPECT_EQ(segment.candidate(1).cost, 10100);
  EXPECT_EQ(segment.candidate(1).wcost, 10100);
  EXPECT_EQ(segment.candidate(2).cost, 10200);
  EXPECT_EQ(segment.candidate(2).wcost, 10200);

  const uint32_t typo_attributes =
      converter::Attribute::SPELLING_CORRECTION |
      converter::Attribute::NO_HISTORY_LEARNING;
  EXPECT_EQ(segment.candidate(0).attributes, typo_attributes);
  EXPECT_EQ(segment.candidate(1).attributes, typo_attributes);
  EXPECT_EQ(segment.candidate(2).attributes, 0);
}

TEST(AzooKeyCandidateParserTest, UsesCharacterCountForConsumedKeySize) {
  converter::Segment segment;
  const std::vector<AzooKeyCandidateInfo> candidates = {{"日本語", 4}};
  FillSegmentWithAzooKeyCandidates(candidates, "にほんご", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(std::string("にほんご").size(), 12);
  EXPECT_EQ(segment.candidate(0).consumed_key_size, 4);
  EXPECT_EQ(CountUtf8Characters("にほんご"), 4);
}

TEST(AzooKeyCandidateParserTest, ClearsExistingCandidatesAndMetaCandidates) {
  converter::Segment segment;
  segment.add_candidate()->value = "old candidate";
  segment.add_meta_candidate()->value = "old meta candidate";

  const std::vector<AzooKeyCandidateInfo> candidates = {{"新規", 2}};
  FillSegmentWithAzooKeyCandidates(candidates, "しん", &segment);

  ASSERT_EQ(segment.candidates_size(), 1);
  EXPECT_EQ(segment.candidate(0).value, "新規");
  EXPECT_EQ(segment.meta_candidates_size(), 0);
}

}  // namespace
}  // namespace mozc
