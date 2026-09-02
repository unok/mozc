// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_candidate_parser.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "converter/attribute.h"
#include "converter/segments.h"

namespace mozc {
namespace {

// Get the substring after the first N UTF-8 characters.
std::string GetUtf8Suffix(absl::string_view utf8_str,
                          size_t skip_char_count) {
  size_t byte_pos = 0;
  size_t chars_processed = 0;
  while (byte_pos < utf8_str.size() && chars_processed < skip_char_count) {
    const unsigned char c = static_cast<unsigned char>(utf8_str[byte_pos]);
    if ((c & 0x80) == 0) {
      byte_pos += 1;
    } else if ((c & 0xE0) == 0xC0) {
      byte_pos += 2;
    } else if ((c & 0xF0) == 0xE0) {
      byte_pos += 3;
    } else if ((c & 0xF8) == 0xF0) {
      byte_pos += 4;
    } else {
      byte_pos += 1;
    }
    ++chars_processed;
  }
  // Clamp malformed trailing UTF-8 so substr does not throw out_of_range.
  return std::string(utf8_str.substr(std::min(byte_pos, utf8_str.size())));
}

// Decodes a JSON \uXXXX escape at json[pos], including surrogate pairs.
void AppendUnicodeEscape(absl::string_view json, size_t& pos,
                         std::string& value) {
  auto read_hex4 = [json](size_t p, unsigned int* out) -> bool {
    if (p + 4 > json.size()) return false;
    unsigned int v = 0;
    for (size_t i = 0; i < 4; ++i) {
      const char c = json[p + i];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= c - '0';
      } else if (c >= 'a' && c <= 'f') {
        v |= c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        v |= c - 'A' + 10;
      } else {
        return false;
      }
    }
    *out = v;
    return true;
  };

  unsigned int code = 0;
  if (!read_hex4(pos + 1, &code)) {
    return;
  }
  pos += 4;

  if (code >= 0xD800 && code <= 0xDBFF) {
    unsigned int low = 0;
    if (pos + 2 < json.size() && json[pos + 1] == '\\' &&
        json[pos + 2] == 'u' && read_hex4(pos + 3, &low) && low >= 0xDC00 &&
        low <= 0xDFFF) {
      code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
      pos += 6;
    } else {
      code = 0xFFFD;
    }
  } else if (code >= 0xDC00 && code <= 0xDFFF) {
    code = 0xFFFD;
  }

  if (code < 0x80) {
    value += static_cast<char>(code);
  } else if (code < 0x800) {
    value += static_cast<char>(0xC0 | (code >> 6));
    value += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    value += static_cast<char>(0xE0 | (code >> 12));
    value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    value += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    value += static_cast<char>(0xF0 | (code >> 18));
    value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    value += static_cast<char>(0x80 | (code & 0x3F));
  }
}

std::string ParseJsonStringValue(absl::string_view json, size_t& pos) {
  std::string value;
  if (pos >= json.size() || json[pos] != '"') {
    return value;
  }
  ++pos;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      ++pos;
      switch (json[pos]) {
        case 'n':
          value += '\n';
          break;
        case 't':
          value += '\t';
          break;
        case 'r':
          value += '\r';
          break;
        case '\\':
          value += '\\';
          break;
        case '"':
          value += '"';
          break;
        case 'u':
          AppendUnicodeEscape(json, pos, value);
          break;
        default:
          value += json[pos];
          break;
      }
    } else {
      value += json[pos];
    }
    ++pos;
  }
  if (pos < json.size()) {
    ++pos;
  }
  return value;
}

bool ParseJsonStringToken(absl::string_view json, size_t* pos,
                          std::string* value) {
  if (*pos >= json.size() || json[*pos] != '"') {
    return false;
  }
  for (size_t i = *pos + 1; i < json.size(); ++i) {
    if (json[i] == '\\') {
      ++i;
    } else if (json[i] == '"') {
      *value = ParseJsonStringValue(json, *pos);
      return true;
    }
  }
  return false;
}

void SkipJsonWhitespace(absl::string_view json, size_t* pos) {
  while (*pos < json.size() &&
         (json[*pos] == ' ' || json[*pos] == '\t' ||
          json[*pos] == '\n' || json[*pos] == '\r')) {
    ++*pos;
  }
}

bool SkipJsonValue(absl::string_view json, size_t* pos) {
  if (*pos >= json.size()) {
    return false;
  }
  if (json[*pos] == '"') {
    std::string ignored;
    return ParseJsonStringToken(json, pos, &ignored);
  }
  if (json[*pos] == '{' || json[*pos] == '[') {
    std::vector<char> closing_delimiters;
    closing_delimiters.push_back(json[*pos] == '{' ? '}' : ']');
    ++*pos;
    while (*pos < json.size() && !closing_delimiters.empty()) {
      if (json[*pos] == '"') {
        std::string ignored;
        if (!ParseJsonStringToken(json, pos, &ignored)) {
          return false;
        }
      } else if (json[*pos] == '{' || json[*pos] == '[') {
        closing_delimiters.push_back(json[*pos] == '{' ? '}' : ']');
        ++*pos;
      } else if (json[*pos] == closing_delimiters.back()) {
        closing_delimiters.pop_back();
        ++*pos;
      } else {
        ++*pos;
      }
    }
    return closing_delimiters.empty();
  }

  const size_t start = *pos;
  while (*pos < json.size() && json[*pos] != ',' && json[*pos] != '}') {
    ++*pos;
  }
  return *pos > start;
}

std::optional<size_t> FindTopLevelJsonFieldValue(
    absl::string_view json, absl::string_view key) {
  size_t pos = 0;
  std::optional<size_t> result;
  SkipJsonWhitespace(json, &pos);
  if (pos >= json.size() || json[pos] != '{') {
    return std::nullopt;
  }
  ++pos;

  while (pos < json.size()) {
    SkipJsonWhitespace(json, &pos);
    if (pos < json.size() && json[pos] == '}') {
      ++pos;
      SkipJsonWhitespace(json, &pos);
      return pos == json.size() ? result : std::nullopt;
    }

    std::string field_name;
    if (!ParseJsonStringToken(json, &pos, &field_name)) {
      return std::nullopt;
    }
    SkipJsonWhitespace(json, &pos);
    if (pos >= json.size() || json[pos] != ':') {
      return std::nullopt;
    }
    ++pos;
    SkipJsonWhitespace(json, &pos);
    if (!result.has_value() && absl::string_view(field_name) == key) {
      result = pos;
    }
    if (!SkipJsonValue(json, &pos)) {
      return std::nullopt;
    }
    SkipJsonWhitespace(json, &pos);
    if (pos < json.size() && json[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < json.size() && json[pos] == '}') {
      ++pos;
      SkipJsonWhitespace(json, &pos);
      return pos == json.size() ? result : std::nullopt;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

size_t CountUtf8Characters(absl::string_view utf8_str) {
  size_t count = 0;
  for (size_t i = 0; i < utf8_str.size();) {
    const unsigned char c = static_cast<unsigned char>(utf8_str[i]);
    if ((c & 0x80) == 0) {
      i += 1;
    } else if ((c & 0xE0) == 0xC0) {
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      i += 4;
    } else {
      i += 1;
    }
    ++count;
  }
  return count;
}

std::optional<std::string> FindAzooKeyJsonStringField(
    absl::string_view json, absl::string_view key) {
  const std::optional<size_t> value_pos =
      FindTopLevelJsonFieldValue(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size() ||
      json[*value_pos] != '"') {
    return std::nullopt;
  }
  size_t pos = *value_pos;
  std::string value;
  if (!ParseJsonStringToken(json, &pos, &value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<bool> FindAzooKeyJsonBoolField(absl::string_view json,
                                             absl::string_view key) {
  const std::optional<size_t> value_pos =
      FindTopLevelJsonFieldValue(json, key);
  if (!value_pos.has_value()) {
    return std::nullopt;
  }
  const absl::string_view value = json.substr(*value_pos);
  if (value.substr(0, 4) == "true" &&
      (value.size() == 4 || value[4] == ',' || value[4] == '}' ||
       value[4] == ' ' || value[4] == '\t' || value[4] == '\n' ||
       value[4] == '\r')) {
    return true;
  }
  if (value.substr(0, 5) == "false" &&
      (value.size() == 5 || value[5] == ',' || value[5] == '}' ||
       value[5] == ' ' || value[5] == '\t' || value[5] == '\n' ||
       value[5] == '\r')) {
    return false;
  }
  return std::nullopt;
}

std::vector<AzooKeyCandidateInfo> ParseAzooKeyCandidateJson(
    absl::string_view json) {
  std::vector<AzooKeyCandidateInfo> result;
  if (json.empty() || json[0] != '[') {
    return result;
  }

  size_t pos = 1;
  while (pos < json.size()) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r')) {
      ++pos;
    }
    if (pos >= json.size() || json[pos] == ']') {
      break;
    }
    if (json[pos] == ',') {
      ++pos;
      continue;
    }
    if (json[pos] != '{') {
      break;
    }
    ++pos;

    AzooKeyCandidateInfo info;
    while (pos < json.size() && json[pos] != '}') {
      while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                    json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
      }
      if (pos >= json.size() || json[pos] == '}') {
        break;
      }
      if (json[pos] == ',') {
        ++pos;
        continue;
      }
      if (json[pos] != '"') {
        break;
      }
      ++pos;

      std::string field_name;
      while (pos < json.size() && json[pos] != '"') {
        field_name += json[pos];
        ++pos;
      }
      if (pos < json.size()) {
        ++pos;
      }
      while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) {
        ++pos;
      }

      if (field_name == "text" && pos < json.size() && json[pos] == '"') {
        info.text = ParseJsonStringValue(json, pos);
      } else if (field_name == "correctedReading" && pos < json.size() &&
                 json[pos] == '"') {
        info.corrected_reading = ParseJsonStringValue(json, pos);
      } else if (field_name == "correspondingCount") {
        int value = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
          value = value * 10 + (json[pos] - '0');
          ++pos;
        }
        info.corresponding_count = value;
      } else if (field_name == "typoCorrected") {
        if (json.substr(pos, 4) == "true") {
          info.typo_corrected = true;
          pos += 4;
        } else if (json.substr(pos, 5) == "false") {
          info.typo_corrected = false;
          pos += 5;
        }
      } else if (pos < json.size() && json[pos] == '"') {
        ++pos;
        while (pos < json.size() && json[pos] != '"') {
          if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
          }
          ++pos;
        }
        if (pos < json.size()) {
          ++pos;
        }
      } else {
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
          ++pos;
        }
      }
    }

    if (pos < json.size() && json[pos] == '}') {
      ++pos;
    }
    if (!info.text.empty()) {
      result.push_back(std::move(info));
    }
  }
  return result;
}

void FillSegmentWithAzooKeyCandidates(
    absl::Span<const AzooKeyCandidateInfo> candidates, absl::string_view key,
    converter::Segment* segment) {
  const size_t key_char_count = CountUtf8Characters(key);

  LOG(INFO) << "AzooKey::ParseCandidatesForSegment - key=" << key
            << ", key_char_count=" << key_char_count
            << ", candidates=" << candidates.size();

  std::vector<AzooKeyCandidateInfo> processed_candidates;
  for (const AzooKeyCandidateInfo& info : candidates) {
    // correspondingCount (Swift rubyCount) must be positive. Treating a
    // missing or invalid value as an exact match would disagree with display
    // text and consumed_key_size.
    if (info.corresponding_count <= 0) {
      continue;
    }
    const size_t candidate_char_count =
        static_cast<size_t>(info.corresponding_count);
    if (candidate_char_count == key_char_count) {
      processed_candidates.push_back(info);
    } else if (candidate_char_count < key_char_count) {
      AzooKeyCandidateInfo processed = info;
      processed.text = info.text + GetUtf8Suffix(key, candidate_char_count);
      processed.corresponding_count = static_cast<int>(key_char_count);
      processed_candidates.push_back(std::move(processed));
    }
  }

  if (processed_candidates.empty()) {
    AzooKeyCandidateInfo fallback;
    fallback.text = std::string(key);
    fallback.corresponding_count = static_cast<int>(key_char_count);
    processed_candidates.push_back(std::move(fallback));
  }

  segment->clear_candidates();
  segment->clear_meta_candidates();

  int32_t base_cost = 0;
  bool typo_cost_penalty_applied = false;
  for (const AzooKeyCandidateInfo& info : processed_candidates) {
    if (info.typo_corrected && !typo_cost_penalty_applied) {
      base_cost += 10000;
      typo_cost_penalty_applied = true;
    }

    converter::Candidate* candidate = segment->add_candidate();
    candidate->key.assign(key.data(), key.size());
    candidate->value = info.text;
    candidate->content_key.assign(key.data(), key.size());
    candidate->content_value = info.text;
    candidate->cost = base_cost;
    candidate->wcost = base_cost;
    candidate->structure_cost = 0;
    candidate->consumed_key_size = key_char_count;
    // lid/rid = 0 means Converter::Finish will set general_noun_id.
    candidate->lid = 0;
    candidate->rid = 0;
    if (info.typo_corrected) {
      candidate->attributes |= converter::Attribute::SPELLING_CORRECTION;
      candidate->attributes |= converter::Attribute::NO_HISTORY_LEARNING;
    }
    // NOTE: candidate->description markers were unstable because a later
    // Rewriter overwrote them. AzooKey origin is represented by the prediction
    // label (REALTIME is shown as "AZ"/"AZ1" in result.cc).

    base_cost += 100;
  }
}

}  // namespace mozc
