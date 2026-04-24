#include "ustr.h"

#include <stdexcept>

namespace sime::ustr {
namespace {

char32_t DecodeUTF8(const std::string_view input, std::size_t& idx) {
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
        result.push_back(DecodeUTF8(input, idx));
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

} // namespace sime::ustr