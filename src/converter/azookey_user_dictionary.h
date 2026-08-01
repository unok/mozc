// Copyright 2026 MyIME Project.
// All rights reserved.

#ifndef MOZC_CONVERTER_AZOOKEY_USER_DICTIONARY_H_
#define MOZC_CONVERTER_AZOOKEY_USER_DICTIONARY_H_

#include <string>

#include "absl/strings/string_view.h"
#include "dictionary/dictionary_interface.h"
#include "protocol/user_dictionary_storage.pb.h"

namespace mozc {

// Stable JSON category used at the C++/Swift boundary.
absl::string_view AzooKeyPosCategory(
    user_dictionary::UserDictionary::PosType pos);

// Serializes all active dictionaries in storage. Current Mozc storage no
// longer has a per-dictionary enabled flag, so every dictionary is active.
std::string BuildAzooKeyUserDictionaryJson(
    const user_dictionary::UserDictionaryStorage& storage);

// Loads Mozc's source-of-truth file and replaces AzooKey's in-memory dynamic
// dictionary. A missing file is treated as an empty dictionary.
bool PushMozcUserDictionaryToAzooKey(
    const dictionary::UserDictionaryInterface& user_dictionary);

}  // namespace mozc

#endif  // MOZC_CONVERTER_AZOOKEY_USER_DICTIONARY_H_
