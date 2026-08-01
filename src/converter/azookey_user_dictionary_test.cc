// Copyright 2026 MyIME Project.
// All rights reserved.

#include "converter/azookey_user_dictionary.h"

#include "protocol/user_dictionary_storage.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

using UserDictionary = user_dictionary::UserDictionary;

TEST(AzooKeyUserDictionaryTest, MapsSupportedPosCategories) {
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::NOUN), "noun");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::PROPER_NOUN), "proper_noun");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::PERSONAL_NAME),
            "personal_name");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::FAMILY_NAME), "family_name");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::FIRST_NAME), "first_name");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::PLACE_NAME), "place_name");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::ORGANIZATION_NAME),
            "organization");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::SA_IRREGULAR_CONJUGATION_NOUN),
            "sahen_noun");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::ADJECTIVE), "adjective");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::ADVERB), "adverb");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::INTERJECTION), "interjection");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::SYMBOL), "symbol");
  EXPECT_EQ(AzooKeyPosCategory(UserDictionary::PREFIX), "noun");
}

TEST(AzooKeyUserDictionaryTest, SerializesAllDictionariesAndFiltersEntries) {
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

  EXPECT_EQ(BuildAzooKeyUserDictionaryJson(storage),
            "[{\"reading\":\"うのけ\",\"word\":\"宇野\\\"家\","
            "\"pos\":\"personal_name\"},{\"reading\":\"とうきょう\","
            "\"word\":\"東京\",\"pos\":\"place_name\"}]");
}

TEST(AzooKeyUserDictionaryTest, EmptyStorageProducesEmptyArray) {
  EXPECT_EQ(BuildAzooKeyUserDictionaryJson(
                user_dictionary::UserDictionaryStorage()),
            "[]");
}

}  // namespace
}  // namespace mozc
