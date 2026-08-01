// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_user_dictionary.h"

#include <cstddef>
#include <set>
#include <string>
#include <tuple>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "base/japanese_util.h"
#include "converter/azookey_immutable_converter.h"
#include "dictionary/user_dictionary_storage.h"
#include "dictionary/user_dictionary_util.h"
#include "protocol/user_dictionary_storage.pb.h"

namespace mozc {
namespace {

void AppendJsonString(absl::string_view value, std::string* output) {
  constexpr char kHex[] = "0123456789abcdef";
  output->push_back('"');
  for (const unsigned char c : value) {
    switch (c) {
      case '"':
        output->append("\\\"");
        break;
      case '\\':
        output->append("\\\\");
        break;
      case '\b':
        output->append("\\b");
        break;
      case '\f':
        output->append("\\f");
        break;
      case '\n':
        output->append("\\n");
        break;
      case '\r':
        output->append("\\r");
        break;
      case '\t':
        output->append("\\t");
        break;
      default:
        if (c < 0x20) {
          output->append("\\u00");
          output->push_back(kHex[c >> 4]);
          output->push_back(kHex[c & 0x0f]);
        } else {
          output->push_back(static_cast<char>(c));
        }
    }
  }
  output->push_back('"');
}

}  // namespace

absl::string_view AzooKeyPosCategory(
    user_dictionary::UserDictionary::PosType pos) {
  using UserDictionary = user_dictionary::UserDictionary;
  switch (pos) {
    case UserDictionary::PROPER_NOUN:
      return "proper_noun";
    case UserDictionary::PERSONAL_NAME:
      return "personal_name";
    case UserDictionary::FAMILY_NAME:
      return "family_name";
    case UserDictionary::FIRST_NAME:
      return "first_name";
    case UserDictionary::ORGANIZATION_NAME:
      return "organization";
    case UserDictionary::PLACE_NAME:
      return "place_name";
    case UserDictionary::SA_IRREGULAR_CONJUGATION_NOUN:
      return "sahen_noun";
    case UserDictionary::ADJECTIVE:
      return "adjective";
    case UserDictionary::ADVERB:
      return "adverb";
    case UserDictionary::INTERJECTION:
      return "interjection";
    case UserDictionary::SYMBOL:
    case UserDictionary::EMOTICON:
    case UserDictionary::PUNCTUATION:
      return "symbol";
    case UserDictionary::NOUN:
    default:
      return "noun";
  }
}

std::string BuildAzooKeyUserDictionaryJson(
    const user_dictionary::UserDictionaryStorage& storage) {
  using Entry = user_dictionary::UserDictionary::Entry;
  using PosType = user_dictionary::UserDictionary::PosType;

  size_t entry_count = 0;
  for (const auto& dictionary : storage.dictionaries()) {
    entry_count += dictionary.entries_size();
  }

  std::string json;
  json.reserve(2 + entry_count * 64);
  json.push_back('[');
  bool first = true;
  std::set<std::tuple<std::string, std::string, int>> seen;
  for (const auto& dictionary : storage.dictionaries()) {
    for (const Entry& entry : dictionary.entries()) {
      const PosType pos = entry.pos();
      if (pos == user_dictionary::UserDictionary::NO_POS ||
          pos == user_dictionary::UserDictionary::SUPPRESSION_WORD ||
          !user_dictionary::ValidateEntry(entry).ok()) {
        continue;
      }
      const std::string reading = japanese::NormalizeVoicedSoundMark(
          user_dictionary::NormalizeReading(entry.key()));
      if (!seen.emplace(reading, entry.value(), static_cast<int>(pos))
               .second) {
        continue;
      }

      if (!first) {
        json.push_back(',');
      }
      first = false;
      json.append("{\"reading\":");
      AppendJsonString(reading, &json);
      json.append(",\"word\":");
      AppendJsonString(entry.value(), &json);
      json.append(",\"pos\":");
      AppendJsonString(AzooKeyPosCategory(pos), &json);
      json.push_back('}');
    }
  }
  json.push_back(']');
  return json;
}

bool PushMozcUserDictionaryToAzooKey(
    const dictionary::UserDictionaryInterface& user_dictionary) {
  UserDictionaryStorage storage(user_dictionary.GetFileName());
  const absl::Status status = storage.Load();
  if (!status.ok() && !absl::IsNotFound(status)) {
    LOG(WARNING) << "Failed to load Mozc user dictionary for AzooKey: "
                 << status;
    return false;
  }

  const std::string json =
      status.ok() ? BuildAzooKeyUserDictionaryJson(storage.GetProto()) : "[]";
  const bool pushed = SetAzooKeyUserDictionary(json);
  if (pushed) {
    LOG(INFO) << "Pushed Mozc user dictionary to AzooKey (JSON bytes="
              << json.size() << ")";
  }
  return pushed;
}

}  // namespace mozc
