#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

class Scorer {
public:
    static constexpr std::uint32_t TokenBits = 18;
    static constexpr std::uint32_t ProBits = 16;
    static constexpr std::uint32_t BowBits = 14;
    static constexpr std::uint32_t BonBits = 23;
    static constexpr std::uint32_t BoeBits = 2;
    static constexpr std::size_t ProTableSize = 1u << ProBits;
    static constexpr std::size_t BowTableSize = 1u << BowBits;

    struct Pos {
        std::uint32_t level = 0;
        std::uint32_t index = 0;

        bool operator<(const Pos& other) const {
            if (level != other.level) {
                return level < other.level;
            }
            return index < other.index;
        }

        bool operator==(const Pos& other) const {
            return level == other.level && index == other.index;
        }
    };

    bool Load(const std::filesystem::path& path);
    void Reset();

    float_t ScoreMove(Pos s, TokenID w, Pos& r) const;
    void Back(Pos& pos) const;

private:
    struct NodeEntry {
        TokenID token = 0;
        std::uint32_t down = 0;
        std::uint32_t bow = 0;
        std::uint32_t pro = 0;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    struct LeaveEntry {
        TokenID token = 0;
        std::uint32_t pro = 0;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    float_t RawMove(Pos s, TokenID w, Pos& r) const;
    std::size_t GetNode(int level, std::size_t b, std::size_t e, TokenID w) const;
    std::size_t GetLeave(std::size_t b, std::size_t e, TokenID w) const;

    int num_ = 0;
    std::vector<int> sizes_;
    std::vector<std::vector<NodeEntry>> node_levels_;
    std::vector<LeaveEntry> leave_level_;
    std::vector<float> pro_table_;
    std::vector<float> bow_table_;

};

} // namespace
