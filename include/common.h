#pragma once

#include <cstdint>

namespace sime {

using TokenID = std::uint32_t;
constexpr TokenID NotToken = 0;
constexpr TokenID SentenceStart = 10;
constexpr TokenID SentenceEnd = 11;
constexpr TokenID UnknownToken = 12;
constexpr TokenID StartToken = 70;
constexpr TokenID ScoreNotToken = StartToken - 1;

constexpr std::uint32_t GroupEnd = 1U << 31;
constexpr std::uint32_t GroupTokenMask = ~GroupEnd;

using float_t = double;

} // namespace sime
