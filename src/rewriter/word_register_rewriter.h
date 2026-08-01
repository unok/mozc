// Copyright 2026, MyIME Authors.
//
// Licensed under the same license as Mozc.

#ifndef MOZC_REWRITER_WORD_REGISTER_REWRITER_H_
#define MOZC_REWRITER_WORD_REGISTER_REWRITER_H_

#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

namespace mozc {

// Appends the MyIME word-register command candidate to every conversion
// segment.
class WordRegisterRewriter : public RewriterInterface {
 public:
  WordRegisterRewriter() = default;
  ~WordRegisterRewriter() override = default;

  int capability(const ConversionRequest&) const override {
    return SUGGESTION | PREDICTION | CONVERSION;
  }

  bool Rewrite(const ConversionRequest& request,
               Segments* segments) const override;
};

}  // namespace mozc

#endif  // MOZC_REWRITER_WORD_REGISTER_REWRITER_H_
