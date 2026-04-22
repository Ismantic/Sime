#pragma once

#include "common.h"

#include <array>
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

    Pos StartPos() const;

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
    // SoA level view for quantized compact format.
    struct LevelView {
        const TokenID* tokens = nullptr;
        const std::uint16_t* pro_q = nullptr;   // 16-bit quantized
        const std::uint8_t* bow_q = nullptr;     // 8-bit quantized
        const std::uint32_t* down = nullptr;
        std::size_t size = 0;
    };

    // Dequantize helpers.
    float NodePro(int level, std::size_t index) const {
        return qt_pro_[static_cast<std::size_t>(level)]
                      [node_levels_[static_cast<std::size_t>(level)].pro_q[index]];
    }
    float NodeBow(int level, std::size_t index) const {
        return qt_bow_[static_cast<std::size_t>(level)]
                      [node_levels_[static_cast<std::size_t>(level)].bow_q[index]];
    }
    float LeafPro(std::size_t index) const {
        return qt_leaf_pro_[leaf_pro_q_[index]];
    }

    std::size_t GetNode(int level, std::size_t b, std::size_t e, TokenID w) const;
    std::size_t GetLeave(std::size_t b, std::size_t e, TokenID w) const;
    std::size_t FindNode(int level, TokenID token) const;

    int num_ = 0;
    std::vector<int> sizes_;

    // mmap-backed SoA arrays.
    std::vector<LevelView> node_levels_;

    // Leaf SoA.
    const TokenID* leaf_tokens_ = nullptr;
    const std::uint16_t* leaf_pro_q_ = nullptr;
    std::size_t leave_size_ = 0;

    // Quantization tables: pro is 65536-entry (16-bit), bow is 256-entry (8-bit).
    std::vector<std::vector<float>> qt_pro_;    // [level][0..65535]
    std::vector<std::vector<float>> qt_bow_;    // [level][0..255]
    std::vector<float> qt_leaf_pro_;            // [0..65535]

    // mmap state
    void* mmap_addr_ = nullptr;
    std::size_t mmap_len_ = 0;
};

} // namespace sime
