#pragma once

#include <string>
#include <string_view>

namespace sime::ustr {

std::u32string ToU32(std::string_view input);
std::string FromU32(std::u32string_view input);

} // namespace sime::ustr