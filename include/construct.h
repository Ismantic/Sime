#pragma once

#include "common.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

class NeyDiscounter {
public:
    void Init(std::uint64_t n1, std::uint64_t n2, std::uint64_t n3, std::uint64_t n4);
    float_t Discount(float_t cnt) const;

    // Discount amounts D1 / D2 / D3+ (used to compute MKN gamma).
    float_t D1() const { return v1_; }
    float_t D2() const { return v2_; }
    float_t D3() const { return v3_; }

private:
    float_t v1_ = 0.0;
    float_t v2_ = 0.0;
    float_t v3_ = 0.0;
};

struct ConstructOptions {
    // N-gram order of the model. Supported range: 1..3.
    // Several hot paths (InsertTrigram, ComputeContinuationCounts,
    // RunConstruct's input loop) are specialized to this range; extending it
    // requires generalizing InsertNgram, parent-chain tracking, per-level
    // continuation count computation, and the merge pipeline's kMaxN.
    int num = 0;
    std::filesystem::path output;
    // Base path for per-order count files. Reads <input>.1gram, .2gram,
    // .3gram up to <num>.
    std::filesystem::path input;
    std::uint32_t token_count = 0;
    // Per-order count cutoffs; cutoffs[k-1] applies to k-grams.
    std::vector<std::uint32_t> cutoffs;
    // Post-cut reserves for bigram/trigram entropy pruning.
    // prune_reserves[0] = bigram reserve, prune_reserves[1] = trigram reserve.
    // Unigrams are never pruned.
    std::vector<int> prune_reserves;
};

class Constructor {
public:
    explicit Constructor(ConstructOptions opts);

    // Feed raw counts at each order. Must be called in sorted token order
    // per order, and in order 1 -> 2 -> 3 overall.
    void InsertUnigram(TokenID w, std::uint32_t cnt);
    void InsertBigram(TokenID u, TokenID v, std::uint32_t cnt);
    void InsertTrigram(TokenID u, TokenID v, TokenID w, std::uint32_t cnt);

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
        // Pre-cut sum of children's ctx at the level below. Populated by
        // ComputeContinuationCounts before Cut/Prune, used by DiscountLevel
        // as the MKN "denominator" for lower orders so gamma's normalizer
        // term reflects lost continuation mass. Not persisted to disk.
        std::uint32_t ctx_sum = 0;
    };

    struct Leave {
        TokenID id = 0;
        float_t cnt = 0.0;
        float_t pro = 0.0;
        std::uint32_t ctx = 0;
    };

    using NodeLevel = std::vector<Node>;
    using LeaveLevel = std::vector<Leave>;

    // Walk sorted parent arrays and set .down pointers on l1 / l2 nodes.
    void FixDownPointers();

    template <typename Level>
    int CutLevel(NodeLevel& up_level, Level& current, int threshold,
                 const std::vector<bool>& protect_mask = {});

    void CountCnt();
    void Cut();
    void AppendTails();
    void ComputeContinuationCounts();
    void Discount();
    void CalcBow();
    const void* FindDown(int level, const Node* node, TokenID i) const;
    float_t GetPro(int level, const TokenID* tokens) const;
    void GetBack(int length, const TokenID* seq,
                 std::uint32_t& boe, std::uint32_t& bon) const;

    template <typename DownLevel>
    float_t CalcNodeBow(int level,
                        TokenID* tokens,
                        const DownLevel& down_level,
                        std::size_t begin,
                        std::size_t end) const;

    template <typename DownLevel>
    void DiscountLevel(NodeLevel& level, DownLevel& down_level, NeyDiscounter& disc,
                       bool use_context, int parent_lvl);

    ConstructOptions opts_;
    std::vector<NodeLevel> node_levels_;
    LeaveLevel leaves_;
    std::vector<std::array<std::uint64_t, 5>> nt_;
    std::vector<std::uint32_t> cuts_;
    std::vector<NeyDiscounter> discounters_;

    // Parent token arrays parallel to node_levels_[2] and leaves_.
    // Filled during InsertBigram / InsertTrigram, consumed by FixDownPointers.
    std::vector<TokenID> bigram_parents_;          // parent l1 id per l2 entry
    std::vector<std::array<TokenID, 2>> leaf_parents_;  // parent (u, v) per leaf

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
    // Alternative level-2 pruning that ranks bigrams by count-weighted PMI
    // (collocation strength). Trigram and higher levels still use Stolcke KL.
    void PruneBigramByPMI();
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
