#pragma once 

#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sime::ustr {

std::u32string ToU32(std::string_view input);
std::string FromU32(std::u32string_view input);

class StreamDecoder {
public:
    explicit StreamDecoder(std::istream& in) : in_(in) {}

    std::optional<char32_t> Next();

private:
    std::istream& in_;
};

} // namespace sime::ustr