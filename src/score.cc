#include "score.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

// SIMD intrinsics
#ifdef SIME_HAS_AVX2
#include <immintrin.h>
#endif

namespace sime {
namespace {
constexpr std::uint32_t TokenMask = (1U << Scorer::TokenBits) - 1U;
constexpr std::uint32_t BowMask = (1U << Scorer::BowBits) - 1U;
constexpr std::uint32_t PrMask = (1U << Scorer::PrBits) - 1U;
constexpr std::uint32_t BonMask = (1U << Scorer::BonBits) - 1U;
constexpr std::uint32_t BoeMask = (1U << Scorer::BoeBits) - 1U;

constexpr TokenID ScoreNotToken = 69;

} // namespace

bool Scorer::Load(const std::filesystem::path& path) {
    Reset();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    if (!in.read(reinterpret_cast<char*>(&order_), sizeof(order_))) {
        Reset();
        return false;
    }
    if (order_ <= 0) {
        Reset();
        return false;
    }
    std::uint32_t flag = 0;
    if (!in.read(reinterpret_cast<char*>(&flag), sizeof(flag))) {
        Reset();
        return false;
    }
    use_log_ = (flag != 0);

    level_sizes_.assign(static_cast<std::size_t>(order_) + 1, 0);
    if (!in.read(reinterpret_cast<char*>(level_sizes_.data()),
                 static_cast<std::streamsize>(level_sizes_.size() * sizeof(int)))) {
        Reset();
        return false;
    }

    pr_table_.resize(PrTableSize);
    if (!in.read(reinterpret_cast<char*>(pr_table_.data()),
                 static_cast<std::streamsize>(pr_table_.size() * sizeof(float)))) {
        Reset();
        return false;
    }

    bow_table_.resize(BowTableSize);
    if (!in.read(reinterpret_cast<char*>(bow_table_.data()),
                 static_cast<std::streamsize>(bow_table_.size() * sizeof(float)))) {
        Reset();
        return false;
    }

    node_levels_.resize(static_cast<std::size_t>(order_));
    for (int lvl = 0; lvl < order_; ++lvl) {
        int size = level_sizes_[static_cast<std::size_t>(lvl)];
        if (size <= 0) {
            Reset();
            return false;
        }
        auto& nodes = node_levels_[static_cast<std::size_t>(lvl)];
        nodes.resize(static_cast<std::size_t>(size));
        for (int idx = 0; idx < size; ++idx) {
            std::uint32_t w0 = 0;
            std::uint32_t w1 = 0;
            std::uint32_t w2 = 0;
            if (!in.read(reinterpret_cast<char*>(&w0), sizeof(w0)) ||
                !in.read(reinterpret_cast<char*>(&w1), sizeof(w1)) ||
                !in.read(reinterpret_cast<char*>(&w2), sizeof(w2))) {
                Reset();
                return false;
            }
            NodeEntry entry;
            entry.id = static_cast<TokenID>(w0 & TokenMask);
            entry.bow = (w0 >> TokenBits) & BowMask;
            entry.pr = w1 & PrMask;
            entry.child = (w1 >> 16U) & 0xFFFFU;
            entry.child |= ((w2 >> (BonBits + BoeBits)) & 0x7FU) << 16U;
            entry.bon = w2 & BonMask;
            entry.boe = (w2 >> BonBits) & BoeMask;
            nodes[static_cast<std::size_t>(idx)] = entry;
        }
    }

    int leave_size = level_sizes_.back();
    if (leave_size <= 0) {
        Reset();
        return false;
    }
    leave_level_.resize(static_cast<std::size_t>(leave_size));
    for (int idx = 0; idx < leave_size; ++idx) {
        std::uint32_t w0 = 0;
        std::uint32_t w1 = 0;
        if (!in.read(reinterpret_cast<char*>(&w0), sizeof(w0)) ||
            !in.read(reinterpret_cast<char*>(&w1), sizeof(w1))) {
            Reset();
            return false;
        }
        LeaveEntry entry;
        entry.id = static_cast<TokenID>(w0 & TokenMask);
        std::uint32_t pr_low = (w0 >> TokenBits) & 0x3FFFU;
        std::uint32_t pr_high = (w1 >> (BonBits + BoeBits)) & 0x3U;
        entry.pr = (pr_high << 14U) | pr_low;
        entry.bon = w1 & BonMask;
        entry.boe = (w1 >> BonBits) & BoeMask;
        leave_level_[static_cast<std::size_t>(idx)] = entry;
    }
    return true;
}

void Scorer::Reset() {
    order_ = 0;
    use_log_ = false;
    level_sizes_.clear();
    node_levels_.clear();
    leave_level_.clear();
    pr_table_.clear();
    bow_table_.clear();
    back_cache_.clear();
}

void Scorer::Back(State& state) const {
    if (state.level >= static_cast<std::uint32_t>(order_)) {
        if (state.index < leave_level_.size()) {
            const auto& leaf = leave_level_[state.index];
            state.level = leaf.boe;
            state.index = leaf.bon;
        }
        return;
    }
    const auto& nodes = node_levels_[state.level];
    if (state.index + 1 >= nodes.size()) {
        return;
    }
    const auto& node = nodes[state.index];
    const auto& next = nodes[state.index + 1];
    if (node.child == next.child) {
        state.level = node.boe;
        state.index = node.bon;
    }
}

void Scorer::BackCached(State& state) const {
    std::uint64_t key = StateToKey(state);
    auto it = back_cache_.find(key);
    if (it != back_cache_.end()) {
        state = it->second;
        return;
    }

    // Cache miss: compute and store
    Back(state);
    back_cache_[key] = state;
}

double Scorer::ScoreMove(State s, TokenID w, State& r) const {
    double value = RawMove(s, w, r);
    if (use_log_) {
        return value;
    }
    if (value <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return -std::log(value);
}

double Scorer::RawMove(State s, TokenID w, State& r) const {
    std::uint32_t level = s.level;
    std::uint32_t index = s.index;

    double cost = use_log_ ? 0.0 : 1.0;
    if (w == ScoreNotToken) {
        r = State{};
        return cost;
    }

    while (level < static_cast<std::uint32_t>(order_)) {
        const auto& nodes = node_levels_[level];
        std::size_t node_index = (level == 0) ? 0 : index;
        if (node_index + 1 >= nodes.size()) {
            break;
        }
        const auto& node = nodes[node_index];
        const auto& next = nodes[node_index + 1];
        auto begin = node.child;
        auto end = next.child;
        if (level == static_cast<std::uint32_t>(order_ - 1)) {
            auto child_idx = GetLeave(begin, end, w);
            if (child_idx != end) {
                r.level = static_cast<std::uint32_t>(order_);
                r.index = static_cast<std::uint32_t>(child_idx);
                double pr = pr_table_[leave_level_[child_idx].pr];
                return use_log_ ? cost + pr : cost * pr;
            }
        } else {
            auto child_idx = GetNode(static_cast<int>(level + 1), begin, end, w);
            if (child_idx != end) {
                r.level = level + 1;
                r.index = static_cast<std::uint32_t>(child_idx);
                double pr = pr_table_[node_levels_[level + 1][child_idx].pr];
                return use_log_ ? cost + pr : cost * pr;
            }
        }

        double bow = bow_table_[node.bow];
        cost = use_log_ ? (cost + bow) : (cost * bow);
        if (level == 0) {
            break;
        }
        level = node.boe;
        index = node.bon;
    }

    r = State{};
    double root_pr = pr_table_[node_levels_[0][0].pr];
    return use_log_ ? (cost + root_pr) : (cost * root_pr);
}

std::size_t Scorer::GetNode(int level,
                            std::size_t begin,
                            std::size_t end,
                            TokenID w) const {
    const auto& nodes = node_levels_[static_cast<std::size_t>(level)];
    const std::size_t count = end - begin;

#ifdef SIME_HAS_AVX2
    // For small ranges (8-32 elements), use SIMD linear search
    // This can be faster than binary search due to:
    // 1. Avoiding branch mispredictions
    // 2. Better use of SIMD parallelism
    // 3. Prefetching benefits
    if (count >= 8 && count <= 32) {
        const __m256i target_vec = _mm256_set1_epi32(static_cast<int>(w));
        const std::size_t simd_end = begin + (count & ~7u);  // Round down to multiple of 8

        for (std::size_t i = begin; i < simd_end; i += 8) {
            // Load 8 token IDs (assuming NodeEntry has id as first member)
            // We need to gather the IDs since NodeEntry is larger than 4 bytes
            alignas(32) std::uint32_t ids[8];
            for (int j = 0; j < 8; ++j) {
                ids[j] = nodes[i + static_cast<std::size_t>(j)].id;
            }
            __m256i data = _mm256_load_si256(reinterpret_cast<const __m256i*>(ids));
            __m256i cmp = _mm256_cmpeq_epi32(data, target_vec);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0) {
                // Found a match, determine which element
                int byte_pos = __builtin_ctz(static_cast<unsigned>(mask));
                return i + static_cast<std::size_t>(byte_pos / 4);
            }
        }

        // Check remaining elements (< 8)
        for (std::size_t i = simd_end; i < end; ++i) {
            if (nodes[i].id == w) {
                return i;
            }
            if (nodes[i].id > w) {
                return end;  // Not found, array is sorted
            }
        }
        return end;
    }
#endif

    // Fallback to binary search for larger ranges or when SIMD not available
    auto first = nodes.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = nodes.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w, [](const NodeEntry& node, TokenID id) {
        return node.id < id;
    });
    if (it == last || it->id != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(nodes.begin(), it));
}

std::size_t Scorer::GetLeave(std::size_t begin,
                             std::size_t end,
                             TokenID w) const {
    const std::size_t count = end - begin;

#ifdef SIME_HAS_AVX2
    // SIMD linear search for small ranges
    if (count >= 8 && count <= 32) {
        const __m256i target_vec = _mm256_set1_epi32(static_cast<int>(w));
        const std::size_t simd_end = begin + (count & ~7u);

        for (std::size_t i = begin; i < simd_end; i += 8) {
            // Gather token IDs from LeaveEntry structs
            alignas(32) std::uint32_t ids[8];
            for (int j = 0; j < 8; ++j) {
                ids[j] = leave_level_[i + static_cast<std::size_t>(j)].id;
            }
            __m256i data = _mm256_load_si256(reinterpret_cast<const __m256i*>(ids));
            __m256i cmp = _mm256_cmpeq_epi32(data, target_vec);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0) {
                int byte_pos = __builtin_ctz(static_cast<unsigned>(mask));
                return i + static_cast<std::size_t>(byte_pos / 4);
            }
        }

        // Check remaining elements
        for (std::size_t i = simd_end; i < end; ++i) {
            if (leave_level_[i].id == w) {
                return i;
            }
            if (leave_level_[i].id > w) {
                return end;
            }
        }
        return end;
    }
#endif

    // Fallback to binary search
    auto first = leave_level_.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = leave_level_.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w, [](const LeaveEntry& leaf, TokenID id) {
        return leaf.id < id;
    });
    if (it == last || it->id != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(leave_level_.begin(), it));
}

} // namespace sime
