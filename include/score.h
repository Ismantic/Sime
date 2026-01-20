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

    // Scorer state is frequently used as map key and compared
    // Align to 8 bytes for better cache performance
    struct alignas(8) State {
        std::uint32_t level = 0;  // 4 bytes
        std::uint32_t index = 0;  // 4 bytes
        // Total: 8 bytes (naturally aligned)

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
    // Language model node entry - align to 32 bytes for cache efficiency
    struct alignas(32) NodeEntry {
        TokenID id = 0;            // 4 bytes - token ID
        std::uint32_t child = 0;   // 4 bytes - child index
        std::uint32_t bow = 0;     // 4 bytes - backoff weight index
        std::uint32_t pr = 0;      // 4 bytes - probability index
        std::uint32_t bon = 0;     // 4 bytes - backoff node index
        std::uint32_t boe = 0;     // 4 bytes - backoff end level
        // Total: 24 bytes, padded to 32 bytes
    };

    // Language model leaf entry - align to 16 bytes
    struct alignas(16) LeaveEntry {
        TokenID id = 0;          // 4 bytes - token ID
        std::uint32_t pr = 0;    // 4 bytes - probability index
        std::uint32_t bon = 0;   // 4 bytes - backoff node index
        std::uint32_t boe = 0;   // 4 bytes - backoff end level
        // Total: 16 bytes (naturally aligned)
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
