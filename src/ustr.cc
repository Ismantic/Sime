#include "ustr.h"

#include <istream>
#include <stdexcept>
#include <vector>

namespace sime::ustr {
namespace {

char32_t DecodeSequence(const std::string_view input, std::size_t& idx) {
    unsigned char lead = static_cast<unsigned char>(input[idx]);
    if (lead < 0x80) {
        ++idx;
        return lead;
    }
    auto remaining = [lead]() -> int {
        if ((lead >> 5) == 0x6) return 1;
        if ((lead >> 4) == 0xE) return 2;
        if ((lead >> 3) == 0x1E) return 3;
        return -1;
    }();
    if (remaining < 0 || idx + remaining >= input.size()) {
        throw std::runtime_error("Invalid UTF-8 sequence");
    }
    char32_t codepoint = lead & ((1u << (7 - remaining - 1)) - 1);
    ++idx;
    for (int i = 0; i < remaining; ++i, ++idx) {
        unsigned char byte = static_cast<unsigned char>(input[idx]);
        if ((byte >> 6) != 0x2) {
            throw std::runtime_error("Invalid UTF-8 continuation byte");
        }
        codepoint = (codepoint << 6) | (byte & 0x3F);
    }
    return codepoint;
}

void EncodeUTF8(char32_t codepoint, std::string& out) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}
} // namespace

std::u32string ToU32(std::string_view input) {
    std::u32string result;
    result.reserve(input.size());
    for (std::size_t idx = 0; idx < input.size();) {
        result.push_back(DecodeSequence(input, idx));
    }
    return result;
}

std::string FromU32(std::u32string_view input) {
    std::string result;
    for (char32_t c : input) {
        EncodeUTF8(c, result);
    }
    return result;
}

std::optional<char32_t> StreamDecoder::Next() {
    std::string buffer;
    buffer.reserve(4);
    char ch;
    while (buffer.empty()) {
        if (!in_.get(ch)) {
            return std::nullopt;
        }
        unsigned char byte = static_cast<unsigned char>(ch);
        buffer.push_back(ch);
        int needed = 0;
        if (byte < 0x80) {
            break;
        } else if ((byte >> 5) == 0x6) {
            needed = 1;
        } else if ((byte >> 4) == 0xE) {
            needed = 2;
        } else if ((byte >> 3) == 0x1E) {
            needed = 3;
        } else {
            throw std::runtime_error("Invalid UTF-8 leading byte");
        }
        for (int i = 0; i < needed; ++i) {
            if (!in_.get(ch)) {
                throw std::runtime_error("Unexpected EOF in UTF-8 sequence");
            }
            unsigned char cont = static_cast<unsigned char>(ch);
            if ((cont >> 6) != 0x2) {
                throw std::runtime_error("Invalid UTF-8 continuation byte");
            }
            buffer.push_back(ch);
        }
        break;
    }
    if (buffer.empty()) {
        return std::nullopt;
    }
    std::size_t idx = 0;
    return DecodeSequence(buffer, idx);
}

} // namespace sime::ustr