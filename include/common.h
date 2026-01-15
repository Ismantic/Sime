#pragma once 

#include <cstdint> 
#include <array>

namespace sime {

using TokenID = std::uint32_t;
constexpr TokenID kNotToken = 0;
constexpr TokenID kSentenceToken = 10;
constexpr TokenID kRealTokenStart = 70;
constexpr TokenID NotToken = kNotToken; // TODO
constexpr TokenID SentenceToken = kSentenceToken; // TODO

//constexpr TokenID ScoreNotToken = 69;


} // namespace sime
