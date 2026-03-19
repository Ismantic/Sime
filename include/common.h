#pragma once

#include <cstdint>

namespace sime {

using TokenID = std::uint32_t;
constexpr TokenID kNotToken = 0;
constexpr TokenID kSentenceToken = 10;
constexpr TokenID kRealTokenStart = 70;

} // namespace sime
