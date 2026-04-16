#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace sime {

class Scorer {
public:
    Scorer() = default;
    ~Scorer() { Reset(); }
    Scorer(const Scorer&) = delete;
    Scorer& operator=(const Scorer&) = delete;

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
    // context is backed off in-place to the resolved position.
    std::vector<std::pair<TokenID, float_t>> NextTokens(
        Pos& context, std::size_t num) const;

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
    // Layout matches disk format — enables mmap without copy.
    struct NodeEntry {
        TokenID token = 0;
        float pro = 0.0f;
        std::uint32_t down = 0;
        float bow = 0.0f;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    struct LeaveEntry {
        TokenID token = 0;
        float pro = 0.0f;
        std::uint32_t bon = 0;
        std::uint32_t boe = 0;
    };

    std::size_t GetNode(int level, std::size_t b, std::size_t e, TokenID w) const;
    std::size_t GetLeave(std::size_t b, std::size_t e, TokenID w) const;

    int num_ = 0;
    std::vector<int> sizes_;

    // mmap-backed arrays — point directly into mapped file.
    struct LevelView {
        const NodeEntry* data = nullptr;
        std::size_t size = 0;
    };
    std::vector<LevelView> node_levels_;
    const LeaveEntry* leave_data_ = nullptr;
    std::size_t leave_size_ = 0;

    // mmap state
    void* mmap_addr_ = nullptr;
    std::size_t mmap_len_ = 0;
};

} // namespace sime
