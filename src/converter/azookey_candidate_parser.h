// Copyright 2026 MyIME Project.
// All rights reserved.

#ifndef MOZC_CONVERTER_AZOOKEY_CANDIDATE_PARSER_H_
#define MOZC_CONVERTER_AZOOKEY_CANDIDATE_PARSER_H_

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace mozc {
namespace converter {
class Segment;
}  // namespace converter

struct AzooKeyCandidateInfo {
  std::string text;
  int corresponding_count = 0;
  bool typo_corrected = false;
  std::string corrected_reading;
};

std::vector<AzooKeyCandidateInfo> ParseAzooKeyCandidateJson(
    absl::string_view json);

void FillSegmentWithAzooKeyCandidates(
    absl::Span<const AzooKeyCandidateInfo> candidates, absl::string_view key,
    mozc::converter::Segment* segment);

size_t CountUtf8Characters(absl::string_view utf8_str);

}  // namespace mozc

#endif  // MOZC_CONVERTER_AZOOKEY_CANDIDATE_PARSER_H_
