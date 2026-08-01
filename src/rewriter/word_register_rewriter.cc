// Copyright 2026, MyIME Authors.
//
// Licensed under the same license as Mozc.

#include "rewriter/word_register_rewriter.h"

#include <cstddef>

#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "request/conversion_request.h"

namespace mozc {
namespace {

constexpr char kValue[] = "辞書登録";
constexpr char kDescription[] = "単語を辞書に登録";
constexpr char kPrefix[] = "【";
constexpr char kSuffix[] = "】";

bool IsWordRegisterCandidate(const converter::Candidate& candidate) {
  return candidate.command ==
             converter::Candidate::LAUNCH_WORD_REGISTER_DIALOG &&
         (candidate.attributes & converter::Attribute::COMMAND_CANDIDATE);
}

}  // namespace

bool WordRegisterRewriter::Rewrite(const ConversionRequest& request,
                                   Segments* segments) const {
  // myime: This command is for the Windows desktop UI only.
  if (segments == nullptr || request.request().mixed_conversion() ||
      request.options().used_in_predictor_realtime_conversion) {
    return false;
  }

  bool modified = false;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    Segment* segment = segments->mutable_conversion_segment(i);

    // Prediction expansion can reuse candidates from the preceding request.
    // Remove an old command candidate so that the refreshed one is unique and
    // remains the absolute last candidate.
    for (size_t candidate_index = segment->candidates_size();
         candidate_index > 0; --candidate_index) {
      const size_t index = candidate_index - 1;
      if (IsWordRegisterCandidate(segment->candidate(index))) {
        segment->erase_candidate(static_cast<int>(index));
        modified = true;
      }
    }

    // myime: Do not create zero-query or otherwise unusable commands.
    if (segment->key().empty() || segment->candidates_size() == 0) {
      continue;
    }

    converter::Candidate* candidate = segment->add_candidate();
    candidate->key = segment->key();
    candidate->content_key = segment->key();
    candidate->value = kValue;
    candidate->content_value = kValue;
    candidate->description = kDescription;
    candidate->prefix = kPrefix;
    candidate->suffix = kSuffix;
    candidate->attributes = converter::Attribute::COMMAND_CANDIDATE |
                            converter::Attribute::NO_LEARNING;
    candidate->command = converter::Candidate::LAUNCH_WORD_REGISTER_DIALOG;
    modified = true;
  }
  return modified;
}

}  // namespace mozc
