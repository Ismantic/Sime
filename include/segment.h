#pragma once

#include "dict.h"
#include "common.h"

#include <iosfwd>
#include <string>

namespace sime {


struct SegmentOptions {
    bool text_output = false;
    TokenID sentence_token = SentenceToken;
};

class Segmenter {
public:
    explicit Segmenter(Dict dict) : dict_(std::move(dict)) {}

    void SegmentStream(std::istream& in, std::ostream& out, SegmentOptions options) const;

private:
    void EmitSentence(const std::u32string& sentence, 
                      std::ostream& out, 
                      SegmentOptions options, 
                      std::size_t& token_count) const;
    
    Dict dict_;
};

} // namespace sime

