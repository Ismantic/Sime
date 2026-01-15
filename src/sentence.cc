#include "sentence.h"

#include "ustr.h"

#include <array>
#include <optional>

namespace sime {
namespace {

constexpr char32_t Null = U'\0';
constexpr char32_t Return = U'\n';
constexpr char32_t Tab = U'\t';
constexpr char32_t Space = U' ';
constexpr char32_t DoubleSpace = 0x3000;
constexpr char32_t JuHao = 0x3002;
constexpr char32_t WenHao = 0xFF1F;
constexpr char32_t TanHao = 0xFF01;
constexpr char32_t FenHao = 0xFF1B;
constexpr char32_t MaoHao = 0xFF1A;
constexpr char32_t DouHao = 0xFF0C;
constexpr char32_t ShengLue = 0x2026;

bool IsSentencePunct(char32_t c) {
    return c == JuHao || c == WenHao || c == TanHao || c == FenHao || 
           c == MaoHao || c == ShengLue;
}

} // namespace

SentenceReader::SentenceReader(std::istream& in) : in_(in), decoder_(in) {}

bool SentenceReader::NextSentence(std::u32string& sentence) {
    sentence.clear();
    int brk = 0;
    while (true) {
        char32_t ch = Peek(0);
        if (ch == Null) {
            break;
        }
        char32_t chnext = Peek(1);
        char32_t ch2 = Peek(2);
        if (ch == JuHao || ch == WenHao || ch == TanHao || ch == ShengLue) {
            brk = 1;
        } else if (ch == DouHao && chnext == DouHao) {
            brk = 1;
        } else if (ch == Return || ch == Tab) {
            brk = 2;
        } else if (ch == Space || ch == DoubleSpace) {
            if (chnext == Return) {
                if (ch2 == Space || ch2 == Tab || ch2 == Return) {
                    brk = 2;
                }
            } else if (chnext == Space || chnext == Tab) {
                brk = 2;
            }
        }
        if (brk != 0) {
            break;
        }
        sentence.push_back(ch);
        Consume();
    }

    if (brk == 2 && sentence.empty()) {
        char32_t ch = Peek(0);
        while (ch == Space || ch == Tab || ch == Return) {
            sentence.push_back(ch);
            Consume();
            ch = Peek(0);
        }
    } else if (brk == 1) {
        char32_t ch = Peek(0);
        while (IsSentencePunct(ch) || ch == DouHao) {
            sentence.push_back(ch);
            Consume();
            ch = Peek(0);
        }
    } else if (Peek(0) == Null) {
        Consume();
    }

    return !sentence.empty();
}

char32_t SentenceReader::Peek(std::size_t idx) {
    Ensure(idx);
    if (idx >= buffer_.size()) {
        return Null;
    }
    return buffer_[idx];
}

void SentenceReader::Consume(std::size_t count) {
    while (count-- > 0 && !buffer_.empty()) {
        buffer_.pop_front();
    }
}

bool SentenceReader::Ensure(std::size_t idx) {
    if (reached_eof_) {
        return idx < buffer_.size();
    }
    while (!reached_eof_ && buffer_.size() <= idx) {
        auto next = decoder_.Next();
        if (!next.has_value()) {
            buffer_.push_back(Null);
            reached_eof_ = true;
            break;
        }
        buffer_.push_back(*next);
    }
    return idx < buffer_.size();
}

} // namespace sime