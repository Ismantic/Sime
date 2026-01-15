#pragma once 

#include "ustr.h"

#include <deque>
#include <istream>
#include <string>

namespace sime {

class SentenceReader {
public:
    explicit SentenceReader(std::istream& in);

    bool NextSentence(std::u32string& sentence);

private:
    char32_t Peek(std::size_t idx);
    void Consume(std::size_t count = 1);
    bool Ensure(std::size_t idx);

    std::istream& in_;
    ustr::StreamDecoder decoder_;
    std::deque<char32_t> buffer_;
    bool reached_eof_ = false;

};

} // namespace sime