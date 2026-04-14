#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

namespace sime {

constexpr std::uint32_t TokenBits = 18;
constexpr std::uint32_t ProBits = 16;
constexpr std::uint32_t BowBits = 14;
constexpr std::uint32_t BonBits = 23;
constexpr std::uint32_t BoeBits = 2;
constexpr std::uint32_t DownBits = 23;
constexpr std::uint32_t DownLowBits = 16;
constexpr std::uint32_t DownHighBits = DownBits - DownLowBits;
constexpr std::uint32_t DownLowMask = (1U << DownLowBits) - 1U;
constexpr std::uint32_t DownHighMask = (1U << DownHighBits) - 1U;
constexpr std::uint32_t LeafProLow = 32U - TokenBits;
constexpr std::uint32_t LeafProHigh = ProBits - LeafProLow;

struct RawNode {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t down = 0;
    float bow = 0.0f;
};

struct RawLeave {
    TokenID id = 0;
    float pro = 0.0f;
};

struct CompactNode {
    std::uint32_t w0 = 0;
    std::uint32_t w1 = 0;
    std::uint32_t w2 = 0;
};

struct CompactLeave {
    std::uint32_t w0 = 0;
    std::uint32_t w1 = 0;
};

class RawModel {
public:
    bool Load(const std::filesystem::path& path);
    void LinkUps();

    int Num() const { return num_; }
    const std::vector<int>& LevelSizes() const { return level_sizes_; }
    const std::vector<RawNode>& Level(int idx) const { return levels_.at(idx); }
    const std::vector<RawLeave>& Leaves() const { return leaves_; }

    std::size_t LevelCount(int level) const;
    std::size_t LeaveCount() const;

    void GetTokens(int level, int index, std::vector<TokenID>& out) const;
    int GetIndex(int length, const TokenID* seq) const;
    bool HasDown(int level, int index) const;
    void GetBack(int length,
                 const TokenID* seq,
                 std::uint32_t& boe,
                 std::uint32_t& bon) const;

private:
    std::pair<std::size_t, std::size_t> DownRange(int level, int index) const;

    int num_ = 0;
    std::vector<int> level_sizes_;
    std::vector<std::vector<RawNode>> levels_;
    std::vector<RawLeave> leaves_;
    std::vector<std::vector<int>> node_ups_;
    std::vector<int> leave_ups_;
};

struct Tables {
    std::map<float, int> pro_map;
    std::map<float, int> bow_map;
    std::vector<float> pro_table;
    std::vector<float> bow_table;
};

struct CompactModel {
    std::vector<std::vector<CompactNode>> nodes;
    std::vector<CompactLeave> leaves;
};

void RunCompact(const std::filesystem::path& input,
                const std::filesystem::path& output);

} // namespace sime
