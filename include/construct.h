#pragma once

#include "common.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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


class GoodTuringDiscounter final : public Discounter {
public:
    GoodTuringDiscounter(int r_max, double linear_factor);
    const char* Name() const override { return "Good-Turing"; }
    void Init(int max_r, const std::vector<std::uint64_t>& nr) override; 
    double Discount(double freq) const override;
    std::unique_ptr<Discounter> Clone() const override {
        return std::make_unique<GoodTuringDiscounter>(*this);
    }

private:
    int rmax_;
    double linear_factor_;
    std::vector<double> factors_;
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

struct ConstructorOptions {
    int num = 0;
    bool use_log_pr = false;
    std::string output_path;
    std::vector<std::uint32_t> cutoffs; // size = order
    std::vector<std::unique_ptr<Discounter>> discounters; // size = order 
    std::vector<TokenID> breakers;
    std::vector<TokenID> excludes;
    std::uint32_t token_count = 0;
};

class Constructor {
public:
    explicit Constructor(ConstructorOptions opts);
    void InsertItem(const std::vector<TokenID>& ids, std::uint32_t cnt);
    void Finalize();
    void Prune(const std::vector<int>& cutoffs, const std::vector<int>& reserves);
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

    struct NodeInfo {
        double distance = 0.0;
        std::uint32_t index = 0;
        bool has_child = false;
        bool operator<(const NodeInfo& other) const;
    };

    using NodeLevel = std::vector<Node>;
    using LeaveLevel = std::vector<Leave>;

    void EnsureCapacity();
    bool IsBreaker(TokenID i) const;
    bool IsExcluded(TokenID i) const;

    template <typename Level>
    std::size_t CutLevelByMark(std::vector<Node>& parents, Level& current, double mark_value);

    template <typename Level>
    int CutLevel(NodeLevel& parent, Level& current, int threshold);

    void CountCnt();
    void Cut();
    void PruneLevel(int level);
    void AppendTails();
    void Discount();
    void CalcBow();
    const void* FindChild(int level, const Node* node, TokenID i) const;
    double GetPr(int level, const TokenID* tokens) const;
    double CalcDistance(int level, std::vector<int>& indices, std::vector<TokenID>& words);

    template <typename ChildLevel>
    double CalcNodeBow(int level, 
                       TokenID* tokens,
                       const ChildLevel& child,
                       std::size_t begin,
                       std::size_t end) const;
    
    template <typename ChildLevel>
    void DiscountLevel(NodeLevel& level, ChildLevel& child, Discounter& disc);

    ConstructorOptions opts_;
    std::vector<std::unique_ptr<Discounter>> discounter_storage_;
    std::vector<NodeLevel> node_levels_;
    LeaveLevel leaves_;
    std::vector<std::vector<std::uint64_t>> nr_;
    std::vector<std::uint32_t> cutoffs_;
    std::vector<int> prune_sizes_;
    std::vector<int> prune_cutoffs_;
    std::vector<Discounter*> discounters_;
    std::vector<TokenID> breakers_;
    std::vector<TokenID> excludes_;

    mutable int prune_cache_level_ = -1;
    mutable int prune_cache_index_ = -1;
    mutable double prune_cache_pa_ = 0.0;
    mutable double prune_cache_pb_ = 0.0;
};

struct ConstructOptions {
    int num = 0;
    bool use_log = false;
    std::filesystem::path output;
    std::filesystem::path input;
    std::uint32_t token_count = 0;
    std::vector<std::uint32_t> cutoffs;
    std::vector<std::unique_ptr<Discounter>> discounters;
    std::vector<TokenID> break_ids;
    std::vector<TokenID> exclude_ids;
    std::vector<int> prune_cutoffs;
    std::vector<int> prune_reserves;
};

void RunConstruct(const ConstructOptions& options);

} // namespace sime
