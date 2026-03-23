#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sime {

class Discounter {
public:
    virtual ~Discounter() = default;
    virtual const char* Name() const = 0;
    virtual void Init(int max_r, const std::vector<std::uint64_t>& nr) = 0;
    virtual float_t Discount(float_t freq) const = 0;
    virtual std::unique_ptr<Discounter> Clone() const = 0;
};

class AbsoluteDiscounter final : public Discounter {
public:
    explicit AbsoluteDiscounter(std::optional<float_t> c);
    const char* Name() const override { return "Absolute"; }
    void Init(int max_r, const std::vector<std::uint64_t>& nr) override;
    float_t Discount(float_t freq) const override;
    std::unique_ptr<Discounter> Clone() const override {
        return std::make_unique<AbsoluteDiscounter>(*this);
    }

private:
    std::optional<float_t> user_c_;
    float_t c_ = 0.0;
};

class LinearDiscounter final : public Discounter {
public:
    explicit LinearDiscounter(std::optional<float_t> d);
    const char* Name() const override { return "Linear"; }
    void Init(int max_r, const std::vector<std::uint64_t>& nr) override;
    float_t Discount(float_t freq) const override;
    std::unique_ptr<Discounter> Clone() const override {
        return std::make_unique<LinearDiscounter>(*this);
    }

private:
    std::optional<float_t> user_d_;
    float_t d_ = 0.0;
};

struct ConstructOptions {
    int num = 0;
    std::filesystem::path output;
    std::filesystem::path input;
    std::uint32_t token_count = 0;
    std::vector<std::uint32_t> cutoffs;
    std::vector<std::unique_ptr<Discounter>> discounters;
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
        float_t freq = 0.0;
        float_t pro = 0.0;
        float_t bow = 0.0;
    };

    struct Leave {
        TokenID id = 0;
        float_t freq = 0.0;
        float_t pro = 0.0;
    };

    using NodeLevel = std::vector<Node>;
    using LeaveLevel = std::vector<Leave>;

    bool IsBreaker(TokenID i) const;

    template <typename Level>
    int CutLevel(NodeLevel& up_level, Level& current, int threshold);

    void CountCnt();
    void Cut();
    void AppendTails();
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
    void DiscountLevel(NodeLevel& level, DownLevel& down_level, Discounter& disc);

    ConstructOptions opts_;
    std::vector<NodeLevel> node_levels_;
    LeaveLevel leaves_;
    std::vector<std::vector<std::uint64_t>> nr_;
    std::vector<std::uint32_t> cuts_;
    std::vector<Discounter*> discounters_;

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
