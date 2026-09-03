// Copyright 2026, MyIME Authors.
//
// Licensed under the same license as Mozc.

#include "prediction/dictionary_predictor.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/strings/assign.h"
#include "composer/composer.h"
#include "composer/table.h"
#include "config/config_handler.h"
#include "converter/attribute.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_token.h"
#include "engine/modules.h"
#include "prediction/dictionary_prediction_aggregator.h"
#include "prediction/realtime_decoder.h"
#include "prediction/result.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"
#include "testing/test_peer.h"

namespace mozc::prediction {

using ::mozc::converter::Attribute;
using ::mozc::dictionary::Token;

class DictionaryPredictorTestPeer
    : public testing::TestPeer<DictionaryPredictor> {
 public:
  explicit DictionaryPredictorTestPeer(DictionaryPredictor& predictor)
      : testing::TestPeer<DictionaryPredictor>(predictor) {}

  PEER_STATIC_METHOD(RemoveMissSpelledCandidates);
  PEER_METHOD(RerankAndFilterResults);
};

class StubRealtimeDecoder : public RealtimeDecoder {
 public:
  ~StubRealtimeDecoder() override = default;

  std::vector<Result> Decode(const ConversionRequest&) const override {
    return {};
  }
};

class StubAggregator : public DictionaryPredictionAggregatorInterface {
 public:
  ~StubAggregator() override = default;

  std::vector<Result> AggregateResultsForDesktop(
      const ConversionRequest&) const override {
    return {};
  }

  std::vector<Result> AggregateResultsForMixedConversion(
      const ConversionRequest&) const override {
    return {};
  }

  std::vector<Result> AggregateTypingCorrectedResultsForMixedConversion(
      const ConversionRequest&) const override {
    return {};
  }
};

// Owns the minimum module graph needed to invoke DictionaryPredictor's private
// reranking path through its existing test-peer hook.
class MockDataAndPredictor {
 public:
  MockDataAndPredictor() {
    auto aggregator = std::make_unique<StubAggregator>();
    decoder_ = std::make_unique<StubRealtimeDecoder>();
    modules_ = engine::ModulesPresetBuilder()
                   .Build(std::make_unique<testing::MockDataManager>())
                   .value();
    predictor_ = absl::WrapUnique(new DictionaryPredictor(
        *modules_, std::move(aggregator), *decoder_));
  }

  DictionaryPredictorTestPeer predictor_peer() {
    return DictionaryPredictorTestPeer(*predictor_);
  }

 private:
  std::unique_ptr<StubRealtimeDecoder> decoder_;
  std::unique_ptr<engine::Modules> modules_;
  std::unique_ptr<DictionaryPredictor> predictor_;
};

namespace {

Result CreateResult(absl::string_view key, absl::string_view value, int cost,
                    Token::AttributesBitfield token_attributes = Token::NONE) {
  Result result;
  strings::Assign(result.key, key);
  strings::Assign(result.value, value);
  result.cost = cost;
  result.wcost = cost;
  result.SetTypesAndTokenAttributes(prediction::UNIGRAM, token_attributes);
  return result;
}

class DictionaryPredictorMyImeTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    request_ = std::make_unique<commands::Request>();
    config_ = std::make_unique<config::Config>();
    config::ConfigHandler::GetDefaultConfig(config_.get());
    composer_ = std::make_unique<composer::Composer>(
        composer::Table::GetSharedDefaultTable(), *request_, *config_);
    data_and_predictor_ = std::make_unique<MockDataAndPredictor>();
  }

  void InitHistory(absl::string_view key, absl::string_view value) {
    strings::Assign(history_result_.key, key);
    strings::Assign(history_result_.value, value);
  }

  ConversionRequest CreateRequest(size_t max_candidates_size,
                                  absl::string_view key = "test") const {
    ConversionRequest::Options options;
    options.request_type = ConversionRequest::SUGGESTION;
    options.max_dictionary_prediction_candidates_size = max_candidates_size;
    return ConversionRequestBuilder()
        .SetComposer(*composer_)
        .SetRequestView(*request_)
        .SetConfigView(*config_)
        .SetOptions(std::move(options))
        .SetHistoryResultView(history_result_)
        .SetKey(key)
        .Build();
  }

  DictionaryPredictorTestPeer predictor_peer() {
    return data_and_predictor_->predictor_peer();
  }

 private:
  std::unique_ptr<commands::Request> request_;
  std::unique_ptr<config::Config> config_;
  std::unique_ptr<composer::Composer> composer_;
  std::unique_ptr<MockDataAndPredictor> data_and_predictor_;
  Result history_result_;
};

// Pinning/regression test: this intentionally breaks if upstream's trimming
// implementation changes.
TEST_F(DictionaryPredictorMyImeTest, ReappendsTrimmedSpellingCorrectionAtTail) {
  std::vector<Result> results = {
      CreateResult("test", "ordinary-1", 100),
      CreateResult("test", "ordinary-2", 200),
      CreateResult("test", "typo", 900, Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(2),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0].value, "ordinary-1");
  EXPECT_EQ(results[1].value, "ordinary-2");
  EXPECT_EQ(results[2].value, "typo");
  EXPECT_NE(results[2].attributes & Attribute::SPELLING_CORRECTION, 0);
}

// Pinning/regression test: this intentionally breaks if upstream's trimming
// implementation changes.
TEST_F(DictionaryPredictorMyImeTest,
       ReappendsAtMostThreeSpellingCorrectionsInCostOrder) {
  std::vector<Result> results = {
      CreateResult("test", "ordinary-1", 100),
      CreateResult("test", "ordinary-2", 200),
      CreateResult("test", "typo-900", 900, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-600", 600, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-800", 800, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-700", 700, Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(2),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 5);
  EXPECT_EQ(results[0].value, "ordinary-1");
  EXPECT_EQ(results[1].value, "ordinary-2");
  EXPECT_EQ(results[2].value, "typo-600");
  EXPECT_EQ(results[3].value, "typo-700");
  EXPECT_EQ(results[4].value, "typo-800");
}

// Pinning/regression test: invalid candidates must not be collected for
// reappending after trimming.
TEST_F(DictionaryPredictorMyImeTest,
       DoesNotReappendInvalidCostSpellingCorrection) {
  std::vector<Result> results = {
      CreateResult("test", "ordinary", 100),
      CreateResult("test", "invalid-typo", Result::kInvalidCost,
                   Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(1),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].value, "ordinary");
}

// Pinning/regression test: removed candidates must not be collected for
// reappending after trimming.
TEST_F(DictionaryPredictorMyImeTest,
       DoesNotReappendRemovedSpellingCorrection) {
  std::vector<Result> results = {
      CreateResult("test", "ordinary", 100),
      CreateResult("test", "removed-typo", 200,
                   Token::SPELLING_CORRECTION),
  };
  results[1].removed = true;

  results = predictor_peer().RerankAndFilterResults(CreateRequest(1),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].value, "ordinary");
}

// A spelling correction already present in the trimmed results is skipped as
// a duplicate without consuming the three-candidate append limit.
TEST_F(DictionaryPredictorMyImeTest,
       SurvivingSpellingCorrectionDoesNotCountTowardAppendLimit) {
  std::vector<Result> results = {
      CreateResult("test", "survivor", 50, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-600", 600, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-700", 700, Token::SPELLING_CORRECTION),
      CreateResult("test", "typo-800", 800, Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(1),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 4);
  EXPECT_EQ(results[0].value, "survivor");
  EXPECT_EQ(results[1].value, "typo-600");
  EXPECT_EQ(results[2].value, "typo-700");
  EXPECT_EQ(results[3].value, "typo-800");
}

// Pinning/regression test: this intentionally breaks if upstream's trimming
// implementation changes.
TEST_F(DictionaryPredictorMyImeTest,
       DoesNotDuplicateSpellingCorrectionThatSurvivesTrim) {
  std::vector<Result> results = {
      CreateResult("test", "typo", 50, Token::SPELLING_CORRECTION),
      CreateResult("test", "ordinary-1", 100),
      CreateResult("test", "ordinary-2", 200),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(2),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(results[0].value, "typo");
  EXPECT_EQ(results[1].value, "ordinary-1");
}

TEST_F(DictionaryPredictorMyImeTest,
       DoesNotDuplicateSpellingCorrectionWithSameValueAndDifferentKey) {
  std::vector<Result> results = {
      CreateResult("test", "typo", 100),
      CreateResult("test-2", "typo", 900, Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(2),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].value, "typo");
  EXPECT_EQ(results[0].key, "test");
}

TEST_F(DictionaryPredictorMyImeTest,
       DoesNotReappendSuggestionFilteredSpellingCorrection) {
  std::vector<Result> results = {
      CreateResult("ふぃるたーたいし", "ordinary", 100),
      CreateResult("ふぃるたーたいしょう", "フィルター対象", 900,
                   Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(
      CreateRequest(1, "ふぃるたーたいし"), std::move(results));

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].value, "ordinary");
}

TEST_F(DictionaryPredictorMyImeTest,
       DoesNotReappendHistoryAndValueFilteredSpellingCorrection) {
  InitHistory("ふぃるたー", "フィルター");
  // "対象" is allowed by itself, while "フィルター対象" is filtered.
  std::vector<Result> results = {
      CreateResult("test", "ordinary", 100),
      CreateResult("", "対象", 900, Token::SPELLING_CORRECTION),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(1),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0].value, "ordinary");
}

// Pinning/regression test: this intentionally breaks if upstream's trimming
// implementation changes.
TEST_F(DictionaryPredictorMyImeTest,
       RemoveMissSpelledCandidatesKeepsOtherSpellingCorrections) {
  // Confirms the upstream inner-loop exclusion of other
  // SPELLING_CORRECTION candidates.
  std::vector<Result> results = {
      CreateResult("misspelled", "correction-1", 100,
                   Token::SPELLING_CORRECTION),
      CreateResult("misspelled", "correction-2", 200,
                   Token::SPELLING_CORRECTION),
  };
  const ConversionRequest request =
      ConversionRequestBuilder().SetKey("m").Build();

  DictionaryPredictorTestPeer::RemoveMissSpelledCandidates(
      request, absl::MakeSpan(results));

  EXPECT_FALSE(results[0].removed);
  EXPECT_FALSE(results[1].removed);
}

TEST_F(DictionaryPredictorMyImeTest,
       KeepsSpellingCorrectionWhoseKeyEqualsRequestKey) {
  std::vector<Result> results = {
      CreateResult("test", "typo", 100, Token::SPELLING_CORRECTION),
      CreateResult("test", "other", 200),
      CreateResult("other-key", "typo", 300),
  };
  const ConversionRequest request =
      ConversionRequestBuilder().SetKey("test").Build();

  DictionaryPredictorTestPeer::RemoveMissSpelledCandidates(
      request, absl::MakeSpan(results));

  EXPECT_FALSE(results[0].removed);
  EXPECT_FALSE(results[1].removed);
}

// Pinning/regression test: this intentionally breaks if upstream's trimming
// implementation changes.
TEST_F(DictionaryPredictorMyImeTest,
       LeavesOrderingAndCountUnchangedWithoutSpellingCorrections) {
  std::vector<Result> results = {
      CreateResult("test", "ordinary-4", 400),
      CreateResult("test", "ordinary-1", 100),
      CreateResult("test", "ordinary-3", 300),
      CreateResult("test", "ordinary-2", 200),
  };

  results = predictor_peer().RerankAndFilterResults(CreateRequest(2),
                                                    std::move(results));

  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(results[0].value, "ordinary-1");
  EXPECT_EQ(results[1].value, "ordinary-2");
}

}  // namespace
}  // namespace mozc::prediction
