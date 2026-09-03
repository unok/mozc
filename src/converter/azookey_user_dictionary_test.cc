// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_user_dictionary.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>

#include "absl/strings/str_cat.h"
#include "converter/azookey_cforms.inc"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/user_pos.h"
#include "protocol/user_dictionary_storage.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

using UserDictionary = user_dictionary::UserDictionary;

class AzooKeyUserDictionaryTest : public ::testing::Test {
 protected:
  AzooKeyUserDictionaryTest()
      : user_pos_(std::make_from_tuple<dictionary::UserPos>(
            data_manager_.GetUserPosData())) {}

  static void AddEntry(user_dictionary::UserDictionaryStorage* storage,
                       const std::string& reading, const std::string& word,
                       UserDictionary::PosType pos) {
    if (storage->dictionaries().empty()) {
      storage->add_dictionaries();
    }
    UserDictionary::Entry* entry =
        storage->mutable_dictionaries(0)->add_entries();
    entry->set_key(reading);
    entry->set_value(word);
    entry->set_pos(pos);
  }

  static void ExpectJsonEntry(const std::string& json,
                              absl::string_view reading,
                              absl::string_view word, absl::string_view pos) {
    EXPECT_NE(json.find(absl::StrCat("{\"reading\":\"", reading,
                                    "\",\"word\":\"", word,
                                    "\",\"pos\":\"", pos, "\"}")),
              std::string::npos)
        << json;
  }

  const testing::MockDataManager data_manager_;
  const dictionary::UserPos user_pos_;
};

TEST_F(AzooKeyUserDictionaryTest, MapsRepresentativeNonConjugatingPos) {
  const std::pair<UserDictionary::PosType, absl::string_view> cases[] = {
      {UserDictionary::FAMILY_NAME, "名詞,固有名詞,人名,姓,*,*"},
      {UserDictionary::FIRST_NAME, "名詞,固有名詞,人名,名,*,*"},
      {UserDictionary::ORGANIZATION_NAME, "名詞,固有名詞,組織,*,*,*"},
      {UserDictionary::PLACE_NAME, "名詞,固有名詞,地域,一般,*,*"},
      {UserDictionary::NUMBER, "名詞,数,*,*,*,*"},
      {UserDictionary::GENERIC_SUFFIX, "名詞,接尾,一般,*,*,*"},
      {UserDictionary::COUNTER_SUFFIX, "名詞,接尾,助数詞,*,*,*"},
      {UserDictionary::PREFIX, "接頭詞,名詞接続,*,*,*,*"},
      {UserDictionary::PRENOUN_ADJECTIVAL, "連体詞,*,*,*,*,*"},
      {UserDictionary::CONJUNCTION, "接続詞,*,*,*,*,*"},
      {UserDictionary::SENTENCE_ENDING_PARTICLE, "助詞,終助詞,*,*,*,*"},
      {UserDictionary::SYMBOL, "記号,一般,*,*,*,*"},
  };
  for (const auto& [pos, feature] : cases) {
    EXPECT_EQ(AzooKeyPosCategory(pos), feature);
  }
}

TEST_F(AzooKeyUserDictionaryTest, SerializesAllDictionariesAndFiltersEntries) {
  user_dictionary::UserDictionaryStorage storage;
  UserDictionary* first_dictionary = storage.add_dictionaries();
  UserDictionary::Entry* name = first_dictionary->add_entries();
  name->set_key("うのけ");
  name->set_value("宇野\"家");
  name->set_pos(UserDictionary::PERSONAL_NAME);

  UserDictionary::Entry* no_pos = first_dictionary->add_entries();
  no_pos->set_key("むこう");
  no_pos->set_value("無効");
  no_pos->set_pos(UserDictionary::NO_POS);

  UserDictionary::Entry* suppression = first_dictionary->add_entries();
  suppression->set_key("よくせい");
  suppression->set_value("抑制");
  suppression->set_pos(UserDictionary::SUPPRESSION_WORD);

  UserDictionary* second_dictionary = storage.add_dictionaries();
  *second_dictionary->add_entries() = *name;
  UserDictionary::Entry* place = second_dictionary->add_entries();
  place->set_key("トウキョウ");
  place->set_value("東京");
  place->set_pos(UserDictionary::PLACE_NAME);

  EXPECT_EQ(BuildAzooKeyUserDictionaryJson(storage, user_pos_),
            "[{\"reading\":\"うのけ\",\"word\":\"宇野\\\"家\","
            "\"pos\":\"名詞,固有名詞,人名,一般,*,*\"},"
            "{\"reading\":\"とうきょう\",\"word\":\"東京\","
            "\"pos\":\"名詞,固有名詞,地域,一般,*,*\"}]");
}

TEST_F(AzooKeyUserDictionaryTest, ExpandsWaGroup1VerbInCformsOrder) {
  user_dictionary::UserDictionaryStorage storage;
  AddEntry(&storage, "かう", "買う", UserDictionary::WA_GROUP1_VERB);
  AddEntry(&storage, "かう", "買う", UserDictionary::WA_GROUP1_VERB);
  const std::string json = BuildAzooKeyUserDictionaryJson(storage, user_pos_);

  // The first generated row and first UserPos token are both the base form.
  EXPECT_EQ(azookey_internal::kWA_GROUP1_VERBForms[0].name, "基本形");
  ExpectJsonEntry(json, "かう", "買う",
                  "動詞,自立,*,*,五段・ワ行促音便,基本形");
  ExpectJsonEntry(json, "かわ", "買わ",
                  "動詞,自立,*,*,五段・ワ行促音便,未然形");
  ExpectJsonEntry(json, "かい", "買い",
                  "動詞,自立,*,*,五段・ワ行促音便,連用形");
  ExpectJsonEntry(json, "かえ", "買え",
                  "動詞,自立,*,*,五段・ワ行促音便,仮定形");
  ExpectJsonEntry(json, "かえ", "買え",
                  "動詞,自立,*,*,五段・ワ行促音便,命令ｅ");
  ExpectJsonEntry(json, "かお", "買お",
                  "動詞,自立,*,*,五段・ワ行促音便,未然ウ接続");
  ExpectJsonEntry(json, "かっ", "買っ",
                  "動詞,自立,*,*,五段・ワ行促音便,連用タ接続");
  EXPECT_EQ(std::count(json.begin(), json.end(), '{'), 7);
}

TEST_F(AzooKeyUserDictionaryTest, ExpandsAdjective) {
  user_dictionary::UserDictionaryStorage storage;
  AddEntry(&storage, "たかい", "高い", UserDictionary::ADJECTIVE);
  const std::string json = BuildAzooKeyUserDictionaryJson(storage, user_pos_);

  ExpectJsonEntry(json, "たかく", "高く",
                  "形容詞,自立,*,*,形容詞・アウオ段,連用テ接続");
  ExpectJsonEntry(json, "たかかっ", "高かっ",
                  "形容詞,自立,*,*,形容詞・アウオ段,連用タ接続");
  ExpectJsonEntry(json, "たかけれ", "高けれ",
                  "形容詞,自立,*,*,形容詞・アウオ段,仮定形");
}

TEST_F(AzooKeyUserDictionaryTest, EveryPosProducesOnlyIpadicFeatures) {
  for (int value = UserDictionary::NO_POS;
       value <= UserDictionary::SUPPRESSION_WORD; ++value) {
    const auto pos = static_cast<UserDictionary::PosType>(value);
    EXPECT_TRUE(azookey_internal::IsIpadicFeature(AzooKeyPosCategory(pos)))
        << UserDictionary::PosType_Name(pos) << ": "
        << AzooKeyPosCategory(pos);
    const azookey_internal::ConjugationData* conjugation =
        azookey_internal::FindConjugationData(pos);
    if (conjugation == nullptr) {
      continue;
    }

    ASSERT_GT(conjugation->form_count, 0) << UserDictionary::PosType_Name(pos);
    EXPECT_EQ(conjugation->forms[0].name, "基本形");
    for (size_t i = 0; i < conjugation->form_count; ++i) {
      if (!conjugation->forms[i].exists_in_ipadic) {
        continue;  // The serializer skips this (type, form) pair.
      }
      const absl::string_view kind =
          pos == UserDictionary::ADJECTIVE ? "形容詞" : "動詞";
      const std::string feature =
          absl::StrCat(kind, ",自立,*,*,", conjugation->conjugation_type,
                       ",", conjugation->forms[i].name);
      EXPECT_TRUE(azookey_internal::IsIpadicFeature(feature))
          << UserDictionary::PosType_Name(pos) << ": " << feature;
    }
  }
}

TEST_F(AzooKeyUserDictionaryTest, EmptyStorageProducesEmptyArray) {
  EXPECT_EQ(BuildAzooKeyUserDictionaryJson(
                user_dictionary::UserDictionaryStorage(), user_pos_),
            "[]");
}

}  // namespace
}  // namespace mozc
