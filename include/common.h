#pragma once

#include <cstdint>

namespace sime {

using TokenID = std::uint32_t;
constexpr TokenID NotToken = 0;
constexpr TokenID SentenceToken = 10;
constexpr TokenID StartToken = 70;
constexpr TokenID ScoreNotToken = StartToken - 1;

using float_t = double;

} // namespace sime
