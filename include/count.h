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
    int num = 0;
    std::size_t count_max = 0;
    std::filesystem::path swap;
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::filesystem::path trie;
};

void RunCount(const CountOptions& option);

} // namespace sime