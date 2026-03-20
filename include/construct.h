#pragma once

#include "common.h"

#include <cstdint>
#include <cstdio>
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
    virtual double Discount(double freq) const = 0;
    virtual std::unique_ptr<Discounter> Clone() const = 0;
};

class AbsoluteDiscounter final : public Discounter {
public:
    explicit AbsoluteDiscounter(std::optional<double> c);
    const char* Name() const override { return "Absolute"; }
    void Init(int max_r, const std::vector<std::uint64_t>& nr) override;
    double Discount(double freq) const override;
    std::unique_ptr<Discounter> Clone() const override {
        return std::make_unique<AbsoluteDiscounter>(*this);
    }

private:
    std::optional<double> user_c_;
    double c_ = 0.0;
};

class LinearDiscounter final : public Discounter {
public:
    explicit LinearDiscounter(std::optional<double> d);
    const char* Name() const override { return "Linear"; }
    void Init(int max_r, const std::vector<std::uint64_t>& nr) override;
    double Discount(double freq) const override;
    std::unique_ptr<Discounter> Clone() const override {
        return std::make_unique<LinearDiscounter>(*this);
    }

private:
    std::optional<double> user_d_;
    double d_ = 0.0;
};

struct ConstructOptions {
    int num = 0;
    bool use_log_pr = false;
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
    void Write(FILE* out) const;

private:
    struct Node {
        TokenID id = 0;
        std::uint32_t child = 0;
        double freq = 0.0;
        double pr = 0.0;
        double bow = 0.0;
    };

    struct Leave {
        TokenID id = 0;
        double freq = 0.0;
        double pr = 0.0;
    };

    using NodeLevel = std::vector<Node>;
    using LeaveLevel = std::vector<Leave>;

    bool IsBreaker(TokenID i) const;

    template <typename Level>
    int CutLevel(NodeLevel& parent, Level& current, int threshold);

    void CountCnt();
    void Cut();
    void AppendTails();
    void Discount();
    void CalcBow();
    const void* FindChild(int level, const Node* node, TokenID i) const;
    double GetPr(int level, const TokenID* tokens) const;

    template <typename ChildLevel>
    double CalcNodeBow(int level,
                       TokenID* tokens,
                       const ChildLevel& child,
                       std::size_t begin,
                       std::size_t end) const;

    template <typename ChildLevel>
    void DiscountLevel(NodeLevel& level, ChildLevel& child, Discounter& disc);

    ConstructOptions opts_;
    std::vector<NodeLevel> node_levels_;
    LeaveLevel leaves_;
    std::vector<std::vector<std::uint64_t>> nr_;
    std::vector<std::uint32_t> cutoffs_;
    std::vector<Discounter*> discounters_;

    // Prune
    struct NodeScore {
        double score = 0.0;
        std::uint32_t index = 0;
        bool has_child = false;
        bool operator<(const NodeScore& other) const;
    };

    template <typename Level>
    std::size_t CutLevelByMark(std::vector<Node>& parents, Level& current, double mark_value);

    void PruneLevel(int level);
    double CalcScore(int level, std::vector<int>& indices, std::vector<TokenID>& words);

    std::vector<int> prune_sizes_;
    std::vector<int> prune_cutoffs_;
    mutable int prune_cache_level_ = -1;
    mutable int prune_cache_index_ = -1;
    mutable double prune_cache_pa_ = 0.0;
    mutable double prune_cache_pb_ = 0.0;
};

void RunConstruct(ConstructOptions options);

} // namespace sime
