#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

class Scorer {
public:
    static constexpr std::uint32_t TokenBits = 18;
    static constexpr std::uint32_t BowBits = 14;
    static constexpr std::uint32_t PrBits = 16;
    static constexpr std::uint32_t BonBits = 23;
    static constexpr std::uint32_t BoeBits = 2;
    static constexpr std::size_t PrTableSize = 1u << PrBits;
    static constexpr std::size_t BowTableSize = 1u << BowBits;

    struct State {
        std::uint32_t level = 0;
        std::uint32_t index = 0;

        bool operator<(const State& other) const {
            if (level != other.level) {
                return level < other.level;
            }
            return index < other.index;
        }

        bool operator==(const State& other) const {
            return level == other.level && index == other.index;
        }
    };

    bool Load(const std::filesystem::path& path);
    void Reset();

    double ScoreMove(State s, TokenID w, State& r) const;
    void Back(State& state) const;

private:
    struct NodeEntry {
        TokenID id = 0;
        std::uint32_t child = 0;
        std::uint32_t bow = 0;
        std::uint32_t pr = 0;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    struct LeaveEntry {
        TokenID id = 0;
        std::uint32_t pr = 0;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    double RawMove(State s, TokenID w, State& r) const;
    std::size_t GetNode(int level, std::size_t b, std::size_t e, TokenID w) const;
    std::size_t GetLeave(std::size_t b, std::size_t e, TokenID w) const;

    int order_ = 0;
    bool use_log_ = false;
    std::vector<int> level_sizes_;
    std::vector<std::vector<NodeEntry>> node_levels_;
    std::vector<LeaveEntry> leave_level_;
    std::vector<float> pr_table_;
    std::vector<float> bow_table_;

};

} // namespace
