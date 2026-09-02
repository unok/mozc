// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.

#ifndef MOZC_CONVERTER_AZOOKEY_IMMUTABLE_CONVERTER_H_
#define MOZC_CONVERTER_AZOOKEY_IMMUTABLE_CONVERTER_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "converter/immutable_converter_interface.h"
#include "converter/segments.h"
#include "request/options.h"

namespace mozc {

enum class AzooKeyEngineState {
  kNormal = 0,
  kDllLoadFailed = 1,
  kInitializeFailed = 2,
};

// Configuration for AzooKey engine
// Note: AzooKey functions are loaded dynamically from azookey-engine.dll at runtime
struct AzooKeyConfig {
  std::string dictionary_path;
  std::string memory_path;
  bool zenzai_enabled = true;  // Default: AzooKey with Zenzai
  bool zenzai_use_gpu = false;
  int zenzai_inference_limit = 10;
  std::string zenzai_weight_path;
};

// ImmutableConverter implementation using AzooKey Swift engine
class AzooKeyImmutableConverter : public ImmutableConverterInterface {
 public:
  explicit AzooKeyImmutableConverter(const AzooKeyConfig& config);
  AzooKeyImmutableConverter(const AzooKeyImmutableConverter&) = delete;
  AzooKeyImmutableConverter& operator=(const AzooKeyImmutableConverter&) = delete;
  ~AzooKeyImmutableConverter() override;

  // ImmutableConverterInterface implementation
  [[nodiscard]] bool Convert(const ConversionOptions& options,
                             Segments* segments) const override;

  // Check if the engine is properly initialized
  bool IsValid() const { return initialized_; }

 private:
  // Parse JSON candidates and populate a single segment
  void ParseCandidatesForSegment(const std::string& json_candidates,
                                  const std::string& key,
                                  Segment* segment) const;

  AzooKeyConfig config_;
  bool initialized_ = false;
};

// Factory function to create AzooKeyImmutableConverter
std::unique_ptr<const ImmutableConverterInterface> CreateAzooKeyImmutableConverter(
    const AzooKeyConfig& config);

// myime: Replaces AzooKey's process-wide dynamic user dictionary. Returns
// false when the loaded DLL predates the optional SetUserDictionary export.
bool SetAzooKeyUserDictionary(absl::string_view json);

}  // namespace mozc

#endif  // MOZC_CONVERTER_AZOOKEY_IMMUTABLE_CONVERTER_H_
