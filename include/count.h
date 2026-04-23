#pragma once 

#include "common.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

using Cnt = std::uint32_t;

template <std::size_t N>
using Item = std::array<TokenID, N>;

template <std::size_t N>
struct Group {
    Item<N> item{};
    Cnt cnt = 0;
};

struct CountOptions {
    // Maximum n-gram order. Supported range: 1..3. A single pass over the
    // corpus emits every order from 1 up to `num` into <output>.Ngram.
    int num = 0;
    std::size_t count_max = 0;
    // Base path; per-order swap files go to <swap>.1 .. <swap>.<num>.
    std::filesystem::path swap;
    std::vector<std::filesystem::path> inputs;
    // Base path; per-order outputs go to <output>.1gram .. <output>.<num>gram.
    std::filesystem::path output;
    std::filesystem::path dict;
    // Optional punctuation list. Tokens in this file break the sliding
    // window after being counted, so they only appear at n-gram ends.
    std::filesystem::path punct;
};

void RunCount(const CountOptions& option);

} // namespace sime