#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace sime {

class NeyDiscounter {
public:
    void Init(int max_r, const std::vector<std::uint64_t>& nr);
    float_t Discount(float_t cnt) const;

private:
    float_t v1_ = 0.0;
    float_t v2_ = 0.0;
    float_t v3_ = 0.0;
};

struct ConstructOptions {
    int num = 0;
    std::filesystem::path output;
    std::filesystem::path input;
    std::uint32_t token_count = 0;
    std::vector<std::uint32_t> cutoffs;
    std::vector<int> prune_reserves;
};

class Constructor {
public:
    explicit Constructor(ConstructOptions opts);
    void InsertItem(const std::vector<TokenID>& ids, std::uint32_t cnt);
    void Finalize();
    void Prune(const std::vector<int>& reserves);
    void Write(const std::filesystem::path& path) const;

private:
    struct Node {
        TokenID id = 0;
        std::uint32_t down = 0;
        float_t cnt = 0.0;
        float_t pro = 0.0;
        float_t bow = 0.0;
        std::uint32_t ctx = 0;
    };

    struct Leave {
        TokenID id = 0;
        float_t cnt = 0.0;
        float_t pro = 0.0;
        std::uint32_t ctx = 0;
    };

    using NodeLevel = std::vector<Node>;
    using LeaveLevel = std::vector<Leave>;

    bool IsBreaker(TokenID i) const;

    template <typename Level>
    int CutLevel(NodeLevel& up_level, Level& current, int threshold);

    void CountCnt();
    void Cut();
    void AppendTails();
    void ComputeContinuationCounts();
    void Discount();
    void CalcBow();
    const void* FindDown(int level, const Node* node, TokenID i) const;
    float_t GetPro(int level, const TokenID* tokens) const;

    template <typename DownLevel>
    float_t CalcNodeBow(int level,
                        TokenID* tokens,
                        const DownLevel& down_level,
                        std::size_t begin,
                        std::size_t end) const;

    template <typename DownLevel>
    void DiscountLevel(NodeLevel& level, DownLevel& down_level, NeyDiscounter& disc,
                       bool use_context);

    ConstructOptions opts_;
    std::vector<NodeLevel> node_levels_;
    LeaveLevel leaves_;
    std::vector<std::vector<std::uint64_t>> nr_;
    std::vector<std::uint32_t> cuts_;
    std::vector<NeyDiscounter> discounters_;

    // Prune
    struct NodeScore {
        float_t score = 0.0;
        std::uint32_t index = 0;
        bool has_down = false;
        bool operator<(const NodeScore& other) const;
    };

    template <typename Level>
    std::size_t CutLevelByMark(std::vector<Node>& ups, Level& current, float_t mark_value);

    void PruneLevel(int level);
    float_t CalcScore(int level, std::vector<int>& indices, std::vector<TokenID>& words);

    std::vector<int> prune_sizes_;
    std::vector<int> prune_cutoffs_;
    mutable int prune_cache_level_ = -1;
    mutable int prune_cache_index_ = -1;
    mutable float_t prune_cache_pa_ = 0.0;
    mutable float_t prune_cache_pb_ = 0.0;
};

void RunConstruct(ConstructOptions options);

} // namespace sime
