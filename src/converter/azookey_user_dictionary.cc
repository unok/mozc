// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_user_dictionary.h"

#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/japanese_util.h"
#include "converter/azookey_cforms.inc"
#include "converter/azookey_immutable_converter.h"
#include "data_manager/data_manager.h"
#include "dictionary/user_dictionary_storage.h"
#include "dictionary/user_dictionary_util.h"
#include "dictionary/user_pos.h"
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
    case UserDictionary::NO_POS:
      // Excluded before serialization; keep the total mapping well-defined.
      return "名詞,一般,*,*,*,*";
    case UserDictionary::NOUN:
      return "名詞,一般,*,*,*,*";
    case UserDictionary::ABBREVIATION:
      // IPADIC has no user-dictionary abbreviation POS.
      return "名詞,一般,*,*,*,*";
    case UserDictionary::SUGGESTION_ONLY:
      // This is a Mozc visibility attribute, not an IPADIC POS.
      return "名詞,一般,*,*,*,*";
    case UserDictionary::PROPER_NOUN:
      return "名詞,固有名詞,一般,*,*,*";
    case UserDictionary::PERSONAL_NAME:
      return "名詞,固有名詞,人名,一般,*,*";
    case UserDictionary::FAMILY_NAME:
      return "名詞,固有名詞,人名,姓,*,*";
    case UserDictionary::FIRST_NAME:
      return "名詞,固有名詞,人名,名,*,*";
    case UserDictionary::ORGANIZATION_NAME:
      return "名詞,固有名詞,組織,*,*,*";
    case UserDictionary::PLACE_NAME:
      return "名詞,固有名詞,地域,一般,*,*";
    case UserDictionary::SA_IRREGULAR_CONJUGATION_NOUN:
      return "名詞,サ変接続,*,*,*,*";
    case UserDictionary::ADJECTIVE_VERBAL_NOUN:
      return "名詞,形容動詞語幹,*,*,*,*";
    case UserDictionary::NUMBER:
      // Mozc's extra アラビア数字 field does not exist in IPADIC.
      return "名詞,数,*,*,*,*";
    case UserDictionary::ALPHABET:
      return "記号,アルファベット,*,*,*,*";
    case UserDictionary::SYMBOL:
    case UserDictionary::EMOTICON:
      return "記号,一般,*,*,*,*";
    case UserDictionary::ADVERB:
      return "副詞,一般,*,*,*,*";
    case UserDictionary::PRENOUN_ADJECTIVAL:
      return "連体詞,*,*,*,*,*";
    case UserDictionary::CONJUNCTION:
      return "接続詞,*,*,*,*,*";
    case UserDictionary::INTERJECTION:
      return "感動詞,*,*,*,*,*";
    case UserDictionary::PREFIX:
      return "接頭詞,名詞接続,*,*,*,*";
    case UserDictionary::COUNTER_SUFFIX:
      return "名詞,接尾,助数詞,*,*,*";
    case UserDictionary::GENERIC_SUFFIX:
      return "名詞,接尾,一般,*,*,*";
    case UserDictionary::PERSON_NAME_SUFFIX:
      return "名詞,接尾,人名,*,*,*";
    case UserDictionary::PLACE_NAME_SUFFIX:
      return "名詞,接尾,地域,*,*,*";
    case UserDictionary::WA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・ワ行促音便,基本形";
    case UserDictionary::KA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・カ行イ音便,基本形";
    case UserDictionary::SA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・サ行,基本形";
    case UserDictionary::TA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・タ行,基本形";
    case UserDictionary::NA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・ナ行,基本形";
    case UserDictionary::MA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・マ行,基本形";
    case UserDictionary::RA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・ラ行,基本形";
    case UserDictionary::GA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・ガ行,基本形";
    case UserDictionary::BA_GROUP1_VERB:
      return "動詞,自立,*,*,五段・バ行,基本形";
    case UserDictionary::HA_GROUP1_VERB:
      return "動詞,自立,*,*,四段・ハ行,基本形";
    case UserDictionary::GROUP2_VERB:
      return "動詞,自立,*,*,一段,基本形";
    case UserDictionary::KURU_GROUP3_VERB:
      return "動詞,自立,*,*,カ変・来ル,基本形";
    case UserDictionary::SURU_GROUP3_VERB:
      return "動詞,自立,*,*,サ変・－スル,基本形";
    case UserDictionary::ZURU_GROUP3_VERB:
      return "動詞,自立,*,*,サ変・－ズル,基本形";
    case UserDictionary::RU_GROUP3_VERB:
      return "動詞,自立,*,*,ラ変,基本形";
    case UserDictionary::ADJECTIVE:
      return "形容詞,自立,*,*,形容詞・アウオ段,基本形";
    case UserDictionary::SENTENCE_ENDING_PARTICLE:
      return "助詞,終助詞,*,*,*,*";
    case UserDictionary::PUNCTUATION:
      return "記号,読点,*,*,*,*";
    case UserDictionary::FREE_STANDING_WORD:
      // Mozc maps 独立語 to 記号,一般, but it behaves like a noun in user
      // dictionaries, so expose it to AzooKey as 名詞,一般.
      return "名詞,一般,*,*,*,*";
    case UserDictionary::SUPPRESSION_WORD:
      // Excluded before serialization; keep the total mapping well-defined.
      return "名詞,一般,*,*,*,*";
  }
  return "名詞,一般,*,*,*,*";
}

std::string BuildAzooKeyUserDictionaryJson(
    const user_dictionary::UserDictionaryStorage& storage,
    const dictionary::UserPos& user_pos) {
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
  std::set<std::tuple<std::string, std::string, std::string>> seen;
  auto append_entry = [&json, &first, &seen](absl::string_view reading,
                                             absl::string_view word,
                                             absl::string_view pos) {
    if (!seen.emplace(reading, word, pos).second) {
      return;
    }
    if (!first) {
      json.push_back(',');
    }
    first = false;
    json.append("{\"reading\":");
    AppendJsonString(reading, &json);
    json.append(",\"word\":");
    AppendJsonString(word, &json);
    json.append(",\"pos\":");
    AppendJsonString(pos, &json);
    json.push_back('}');
  };
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

      const azookey_internal::ConjugationData* conjugation =
          azookey_internal::FindConjugationData(pos);
      if (conjugation == nullptr) {
        append_entry(reading, entry.value(), AzooKeyPosCategory(pos));
        continue;
      }

      // UserPos::GetTokens returns the base form first (user_pos.cc), followed
      // by the cforms.def order retained by gen_user_pos_data.py.  The generated
      // metadata deliberately mirrors that filtering and order.  Tests pin the
      // base-form-first rule and representative suffix/form pairings.
      const std::vector<dictionary::UserPos::Token> tokens =
          user_pos.GetTokens(reading, entry.value(), pos);
      if (tokens.size() != conjugation->form_count) {
        LOG(ERROR) << "AzooKey conjugation metadata mismatch for PosType "
                   << static_cast<int>(pos) << ": tokens=" << tokens.size()
                   << ", forms=" << conjugation->form_count;
        append_entry(reading, entry.value(), AzooKeyPosCategory(pos));
        continue;
      }
      const absl::string_view kind =
          pos == user_dictionary::UserDictionary::ADJECTIVE ? "形容詞" : "動詞";
      for (size_t i = 0; i < tokens.size(); ++i) {
        const azookey_internal::ConjugationForm& form = conjugation->forms[i];
        if (!form.exists_in_ipadic) {
          continue;
        }
        const std::string feature =
            absl::StrCat(kind, ",自立,*,*,", conjugation->conjugation_type,
                         ",", form.name);
        const std::string token_reading = japanese::NormalizeVoicedSoundMark(
            user_dictionary::NormalizeReading(tokens[i].key));
        append_entry(token_reading, tokens[i].value, feature);
      }
    }
  }
  json.push_back(']');
  return json;
}

bool PushMozcUserDictionaryToAzooKey(
    const dictionary::UserDictionaryInterface& user_dictionary,
    const DataManager& data_manager) {
  UserDictionaryStorage storage(user_dictionary.GetFileName());
  const absl::Status status = storage.Load();
  if (!status.ok() && !absl::IsNotFound(status)) {
    LOG(WARNING) << "Failed to load Mozc user dictionary for AzooKey: "
                 << status;
    return false;
  }

  const dictionary::UserPos user_pos =
      std::make_from_tuple<dictionary::UserPos>(data_manager.GetUserPosData());
  const std::string json = status.ok()
                               ? BuildAzooKeyUserDictionaryJson(
                                     storage.GetProto(), user_pos)
                               : "[]";
  const bool pushed = SetAzooKeyUserDictionary(json);
  if (pushed) {
    LOG(INFO) << "Pushed Mozc user dictionary to AzooKey (JSON bytes="
              << json.size() << ")";
  }
  return pushed;
}

}  // namespace mozc
