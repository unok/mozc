// Copyright 2026, MyIME Authors.
//
// Licensed under the same license as Mozc.

#include "rewriter/word_register_rewriter.h"

#include <cstddef>
#include <string>

#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/commands.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

ConversionRequest MakeRequest(ConversionRequest::RequestType request_type) {
  return ConversionRequestBuilder().SetRequestType(request_type).Build();
}

void AddOrdinaryCandidate(const std::string& key, const std::string& value,
                          Segment* segment) {
  segment->set_key(key);
  converter::Candidate* candidate = segment->add_candidate();
  candidate->key = key;
  candidate->content_key = key;
  candidate->value = value;
  candidate->content_value = value;
}

void ExpectWordRegisterCandidate(const std::string& reading,
                                 const converter::Candidate& candidate) {
  EXPECT_EQ(candidate.key, reading);
  EXPECT_EQ(candidate.content_key, reading);
  EXPECT_EQ(candidate.value, "辞書登録");
  EXPECT_EQ(candidate.content_value, "辞書登録");
  EXPECT_EQ(candidate.prefix, "【");
  EXPECT_EQ(candidate.suffix, "】");
  EXPECT_NE(candidate.attributes & converter::Attribute::COMMAND_CANDIDATE, 0);
  EXPECT_EQ(candidate.attributes & converter::Attribute::NO_LEARNING,
            converter::Attribute::NO_LEARNING);
  EXPECT_EQ(candidate.command,
            converter::Candidate::LAUNCH_WORD_REGISTER_DIALOG);
}

class WordRegisterRewriterTest : public testing::TestWithTempUserProfile {};

TEST_F(WordRegisterRewriterTest, AppendsToPredictionWithoutChangingCandidates) {
  WordRegisterRewriter rewriter;
  const ConversionRequest request =
      MakeRequest(ConversionRequest::PREDICTION);
  EXPECT_NE(rewriter.capability(request) & RewriterInterface::PREDICTION, 0);

  Segments segments;
  Segment* segment = segments.push_back_segment();
  AddOrdinaryCandidate("よみ", "読み", segment);
  AddOrdinaryCandidate("よみ", "黄泉", segment);

  ASSERT_TRUE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 3);
  EXPECT_EQ(segment->candidate(0).value, "読み");
  EXPECT_EQ(segment->candidate(1).value, "黄泉");
  ExpectWordRegisterCandidate("よみ", segment->candidate(2));

  // Rewriting reused prediction segments refreshes instead of duplicating.
  ASSERT_TRUE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 3);
  ExpectWordRegisterCandidate("よみ", segment->candidate(2));
}

TEST_F(WordRegisterRewriterTest, AppendsToEveryConversionSegment) {
  WordRegisterRewriter rewriter;
  const ConversionRequest request =
      MakeRequest(ConversionRequest::CONVERSION);
  EXPECT_NE(rewriter.capability(request) & RewriterInterface::CONVERSION, 0);

  Segments segments;
  AddOrdinaryCandidate("とうきょう", "東京", segments.push_back_segment());
  AddOrdinaryCandidate("えき", "駅", segments.push_back_segment());

  ASSERT_TRUE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segments.conversion_segments_size(), 2);
  for (size_t i = 0; i < segments.conversion_segments_size(); ++i) {
    const Segment& segment = segments.conversion_segment(i);
    ASSERT_EQ(segment.candidates_size(), 2);
    ExpectWordRegisterCandidate(std::string(segment.key()),
                                segment.candidate(1));
  }
}

TEST_F(WordRegisterRewriterTest, DoesNotRewritePredictorRealtimeConversion) {
  WordRegisterRewriter rewriter;
  ConversionRequest::Options options;
  options.request_type = ConversionRequest::CONVERSION;
  options.used_in_predictor_realtime_conversion = true;
  const ConversionRequest request =
      ConversionRequestBuilder().SetOptions(options).Build();

  Segments segments;
  Segment* segment = segments.push_back_segment();
  AddOrdinaryCandidate("よみ", "読み", segment);

  EXPECT_FALSE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 1);
  EXPECT_EQ(segment->candidate(0).value, "読み");
}

TEST_F(WordRegisterRewriterTest, SupportsSuggestion) {
  WordRegisterRewriter rewriter;
  const ConversionRequest request =
      MakeRequest(ConversionRequest::SUGGESTION);
  EXPECT_NE(rewriter.capability(request) & RewriterInterface::SUGGESTION, 0);

  Segments segments;
  Segment* segment = segments.push_back_segment();
  AddOrdinaryCandidate("にゅうりょく", "入力", segment);
  ASSERT_TRUE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 2);
  ExpectWordRegisterCandidate("にゅうりょく", segment->candidate(1));
}

TEST_F(WordRegisterRewriterTest, DoesNotRewriteMobileRequest) {
  WordRegisterRewriter rewriter;
  commands::Request mobile_request;
  mobile_request.set_mixed_conversion(true);
  const ConversionRequest request =
      ConversionRequestBuilder()
          .SetRequest(mobile_request)
          .SetRequestType(ConversionRequest::PREDICTION)
          .Build();

  Segments segments;
  Segment* segment = segments.push_back_segment();
  AddOrdinaryCandidate("はじまる", "始まる", segment);

  EXPECT_FALSE(rewriter.Rewrite(request, &segments));
  ASSERT_EQ(segment->candidates_size(), 1);
  EXPECT_EQ(segment->candidate(0).value, "始まる");
}

TEST_F(WordRegisterRewriterTest, SkipsEmptyKeyAndCandidateList) {
  WordRegisterRewriter rewriter;
  const ConversionRequest request =
      MakeRequest(ConversionRequest::PREDICTION);

  Segments segments;
  Segment* empty_key_segment = segments.push_back_segment();
  AddOrdinaryCandidate("", "zero query", empty_key_segment);
  Segment* empty_candidates_segment = segments.push_back_segment();
  empty_candidates_segment->set_key("よみ");

  EXPECT_FALSE(rewriter.Rewrite(request, &segments));
  EXPECT_EQ(empty_key_segment->candidates_size(), 1);
  EXPECT_EQ(empty_candidates_segment->candidates_size(), 0);
}

}  // namespace
}  // namespace mozc
