#include "score.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace sime {
namespace {
constexpr std::uint32_t TokenMask = (1U << TokenBits) - 1U;
constexpr std::uint32_t BowMask = (1U << BowBits) - 1U;
constexpr std::uint32_t ProMask = (1U << ProBits) - 1U;
constexpr std::uint32_t BonMask = (1U << BonBits) - 1U;
constexpr std::uint32_t BoeMask = (1U << BoeBits) - 1U;

} // namespace

bool Scorer::Load(const std::filesystem::path& path) {
    Reset();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    if (!in.read(reinterpret_cast<char*>(&num_), sizeof(num_))) {
        Reset();
        return false;
    }
    if (num_ <= 0) {
        Reset();
        return false;
    }
    std::uint32_t flag = 0;
    if (!in.read(reinterpret_cast<char*>(&flag), sizeof(flag))) {
        Reset();
        return false;
    }
    // flag is reserved (always 1 = log mode)

    sizes_.assign(static_cast<std::size_t>(num_) + 1, 0);
    if (!in.read(reinterpret_cast<char*>(sizes_.data()),
                 static_cast<std::streamsize>(sizes_.size() * sizeof(int)))) {
        Reset();
        return false;
    }

    pro_table_.resize(ProTableSize);
    if (!in.read(reinterpret_cast<char*>(pro_table_.data()),
                 static_cast<std::streamsize>(pro_table_.size() * sizeof(float)))) {
        Reset();
        return false;
    }

    bow_table_.resize(BowTableSize);
    if (!in.read(reinterpret_cast<char*>(bow_table_.data()),
                 static_cast<std::streamsize>(bow_table_.size() * sizeof(float)))) {
        Reset();
        return false;
    }

    node_levels_.resize(static_cast<std::size_t>(num_));
    for (int lvl = 0; lvl < num_; ++lvl) {
        int size = sizes_[static_cast<std::size_t>(lvl)];
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
            entry.token = static_cast<TokenID>(w0 & TokenMask);
            entry.bow = (w0 >> TokenBits) & BowMask;
            entry.pro = w1 & ProMask;
            entry.down = (w1 >> 16U) & 0xFFFFU;
            entry.down |= ((w2 >> (BonBits + BoeBits)) & 0x7FU) << 16U;
            entry.bon = w2 & BonMask;
            entry.boe = (w2 >> BonBits) & BoeMask;
            nodes[static_cast<std::size_t>(idx)] = entry;
        }
    }

    int leave_size = sizes_.back();
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
        entry.token = static_cast<TokenID>(w0 & TokenMask);
        std::uint32_t pro_low = (w0 >> TokenBits) & 0x3FFFU;
        std::uint32_t pro_high = (w1 >> (BonBits + BoeBits)) & 0x3U;
        entry.pro = (pro_high << 14U) | pro_low;
        entry.bon = w1 & BonMask;
        entry.boe = (w1 >> BonBits) & BoeMask;
        leave_level_[static_cast<std::size_t>(idx)] = entry;
    }
    return true;
}

void Scorer::Reset() {
    num_ = 0;
    sizes_.clear();
    node_levels_.clear();
    leave_level_.clear();
    pro_table_.clear();
    bow_table_.clear();
}

void Scorer::Back(Pos& pos) const {
    if (pos.level >= static_cast<std::uint32_t>(num_)) {
        if (pos.index < leave_level_.size()) {
            const auto& e = leave_level_[pos.index];
            pos.level = e.boe;
            pos.index = e.bon;
        }
        return;
    }
    const auto& nodes = node_levels_[pos.level];
    assert(pos.index + 1 < nodes.size());
    const auto& node = nodes[pos.index];
    const auto& next = nodes[pos.index + 1];
    if (node.down == next.down) {
        pos.level = node.boe;
        pos.index = node.bon;
    }
}

float_t Scorer::ScoreMove(Pos s, TokenID w, Pos& r) const {
    std::uint32_t level = s.level;
    std::uint32_t index = s.index;

    float_t cost = 0.0;
    if (w == ScoreNotToken) {
        r = Pos{};
        return cost;
    }

    while (level < static_cast<std::uint32_t>(num_)) {
        const auto& nodes = node_levels_[level];
        std::size_t node_index = (level == 0) ? 0 : index;
        if (node_index + 1 >= nodes.size()) {
            break;
        }
        const auto& node = nodes[node_index];
        const auto& next = nodes[node_index + 1];
        auto begin = node.down;
        auto end = next.down;
        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            auto down_idx = GetLeave(begin, end, w);
            if (down_idx != end) {
                r.level = static_cast<std::uint32_t>(num_);
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + pro_table_[leave_level_[down_idx].pro];
            }
        } else {
            auto down_idx = GetNode(static_cast<int>(level + 1), begin, end, w);
            if (down_idx != end) {
                r.level = level + 1;
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + pro_table_[node_levels_[level + 1][down_idx].pro];
            }
        }

        cost += bow_table_[node.bow];
        if (level == 0) {
            break;
        }
        level = node.boe;
        index = node.bon;
    }

    r = Pos{};
    return cost + pro_table_[node_levels_[0][0].pro];
}

std::size_t Scorer::GetNode(int level,
                            std::size_t begin,
                            std::size_t end,
                            TokenID w) const {
    const auto& nodes = node_levels_[static_cast<std::size_t>(level)];
    auto first = nodes.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = nodes.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w, [](const NodeEntry& node, TokenID token) {
        return node.token < token;
    });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(nodes.begin(), it));
}

std::size_t Scorer::GetLeave(std::size_t begin,
                             std::size_t end, 
                             TokenID w) const {
    auto first = leave_level_.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = leave_level_.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w, [](const LeaveEntry& leaf, TokenID token) {
        return leaf.token < token;
    });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(leave_level_.begin(), it));
}

} // namespace sime
