#pragma once

#include "common.h"
#include "compact.h"

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace sime {

constexpr std::size_t ProTableSize = 1u << ProBits;
constexpr std::size_t BowTableSize = 1u << BowBits;

class Scorer {
public:

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
    float_t UnknownPenalty() const;

    // Enumerate successor tokens from context, sorted by cost (best first).
    std::vector<std::pair<TokenID, float_t>> NextTokens(
        Pos context, std::size_t num) const;

    // Accessors for dump tool
    int Num() const { return num_; }
    int LevelSize(int level) const { return sizes_[static_cast<std::size_t>(level)]; }
    int LeaveSize() const { return sizes_.back(); }

    struct NGram {
        std::vector<TokenID> tokens;
        float pro = 0;
    };

    std::vector<NGram> DumpLevel(int level) const;

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
