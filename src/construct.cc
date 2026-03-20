#include "construct.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace sime {

namespace {

constexpr int MaxR = 4096;
constexpr TokenID TailMarker = 0x00FFFFFF;

template <typename T>
void EnsureSortedUnique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

AbsoluteDiscounter::AbsoluteDiscounter(std::optional<double> c) : user_c_(c) {}

void AbsoluteDiscounter::Init(int, const std::vector<std::uint64_t>& nr) {
    if (user_c_) {
        c_ = *user_c_;
    } else {
        if (nr[1] == 0) {
            c_ = 0.5;
        } else {
            c_ = static_cast<double>(nr[1]) /
                 (static_cast<double>(nr[1]) + 2.0 * static_cast<double>(nr[2]));
        }
    }
}

double AbsoluteDiscounter::Discount(double freq) const {
    return freq > 0.0 ? freq - c_ : 0.0;
}

LinearDiscounter::LinearDiscounter(std::optional<double> d) : user_d_(d) {}

void LinearDiscounter::Init(int, const std::vector<std::uint64_t>& nr) {
    if (user_d_) {
        d_ = *user_d_;
    } else {
        if (nr[0] == 0) {
            d_ = 0.5;
        } else {
            d_ = 1.0 - static_cast<double>(nr[1]) / static_cast<double>(nr[0]);
        }
    }
}

double LinearDiscounter::Discount(double freq) const { return freq * d_; }

bool Constructor::NodeScore::operator<(const NodeScore& other) const {
    if (has_child == other.has_child) {
        return score < other.score;
    }
    return !has_child && other.has_child;
}

Constructor::Constructor(ConstructOptions opts) : opts_(std::move(opts)) {
    if (opts_.num <= 0) {
        throw std::invalid_argument("order must be positive");
    }
    node_levels_.resize(opts_.num);
    for (auto& level : node_levels_) {
        level.reserve(1024);
    }
    leaves_.reserve(1024);

    node_levels_[0].push_back(Node{0, 0, 0.0, 0.0, 0.0});

    nr_.assign(opts_.num + 1, std::vector<std::uint64_t>(MaxR, 0));

    cutoffs_.assign(opts_.num + 1, 0);
    for (std::size_t i = 0; i < opts_.cutoffs.size() && i < static_cast<std::size_t>(opts_.num); ++i) {
        cutoffs_[i + 1] = opts_.cutoffs[i];
    }

    discounters_.assign(opts_.num + 1, nullptr);
    for (std::size_t i = 0; i < opts_.discounters.size() && i < static_cast<std::size_t>(opts_.num); ++i) {
        discounters_[i + 1] = opts_.discounters[i].get();
    }
}

bool Constructor::IsBreaker(TokenID i) const {
    return i == kSentenceToken;
}

template <typename Level>
std::size_t Constructor::CutLevelByMark(std::vector<Node>& parents, Level& current, double mark_value) {
    if (current.empty()) {
        return 0;
    }
    const std::size_t total = current.size();
    std::vector<std::size_t> removed_prefix(total + 1, 0);
    std::size_t new_size = 0;
    for (std::size_t idx = 0; idx < total; ++idx) {
        bool keep = (current[idx].pr != mark_value) || (idx + 1 == total);
        if (keep) {
            if (new_size != idx) {
                current[new_size] = current[idx];
            }
            removed_prefix[idx + 1] = removed_prefix[idx];
            ++new_size;
        } else {
            removed_prefix[idx + 1] = removed_prefix[idx] + 1;
        }
    }

    for (auto& parent : parents) {
        std::size_t child = parent.child;
        if (child > total) {
            child = total;
        }
        std::size_t removed = removed_prefix[child];
        parent.child = static_cast<std::uint32_t>(child - removed);
    }
    return new_size;
}

void Constructor::InsertItem(const std::vector<TokenID>& ids, std::uint32_t freq) {
    if (static_cast<int>(ids.size()) != opts_.num) {
        throw std::invalid_argument("ngram length mismatch");
    }
    bool breaker = IsBreaker(ids[0]);
    if (!breaker) {
        node_levels_[0][0].freq += freq;
    }
    bool branch = false;
    for (int lvl = 1; (!breaker && lvl < opts_.num); ++lvl) {
        auto& parent_level = node_levels_[lvl - 1];
        auto& current = node_levels_[lvl];
        bool need_new = branch || 
                        current.empty() || 
                        parent_level.back().child >= static_cast<std::uint32_t>(current.size());
        if (!need_new && current.back().id != ids[lvl-1]) {
            need_new = true;
        }

        if (need_new) {
            std::uint32_t child =
                (lvl == opts_.num - 1)
                    ? static_cast<std::uint32_t>(leaves_.size())
                    : static_cast<std::uint32_t>(node_levels_[lvl + 1].size());
            current.push_back(Node{ids[lvl - 1], child, static_cast<double>(freq), 0.0, 0.0});
        } else {
            current.back().freq += freq;
        }

        branch = need_new;
        breaker = (lvl > 1 && IsBreaker(ids[lvl - 1])) || IsBreaker(ids[lvl]);
    }

    if (!breaker) {
        if (freq > cutoffs_[opts_.num]) {
            leaves_.push_back(Leave{ids.back(), static_cast<double>(freq), 0.0});
        } else {
            nr_[opts_.num][0] += freq;
            if (freq < static_cast<std::uint32_t>(MaxR)) {
                nr_[opts_.num][freq] += freq;
            }
        }
    }
}

void Constructor::CountCnt() {
    for (int lvl = 1; lvl < opts_.num; ++lvl) {
        for (const auto& node : node_levels_[lvl]) {
            auto freq = static_cast<std::uint32_t>(node.freq);
            nr_[lvl][0] += freq;
            if (freq < static_cast<std::uint32_t>(MaxR)) {
                nr_[lvl][freq] += freq;
            }
        }
    }    
    for (const auto& leaf : leaves_) {
        auto freq = static_cast<std::uint32_t>(leaf.freq);
        nr_[opts_.num][0] += freq;
        if (freq < static_cast<std::uint32_t>(MaxR)) {
            nr_[opts_.num][freq] += freq;
        }
    }
}

void Constructor::AppendTails() {
    const double tail_pr = static_cast<double>(std::numeric_limits<float>::denorm_min());
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        std::uint32_t child_count = 0;
        if (lvl == opts_.num - 1) {
            child_count = static_cast<std::uint32_t>(leaves_.size());
        } else {
            child_count = static_cast<std::uint32_t>(node_levels_[lvl + 1].size());
        }
        node_levels_[lvl].push_back(Node{TailMarker, child_count, 1.0, 0.0, 0.0});
        node_levels_[lvl].back().pr = tail_pr;
    }
    leaves_.push_back(Leave{0, 1.0, tail_pr});
}

template <typename Level>
int Constructor::CutLevel(NodeLevel& parent, Level& current, int threshold) {
    if (threshold <= 0) {
        return static_cast<int>(current.size());
    }
    int write = 0;
    auto pfirst = parent.begin();
    auto plast = parent.end();
    for (int idx = 0; idx < static_cast<int>(current.size()); ++idx) {
        bool keep = false;
        if constexpr (std::is_same_v<Level, LeaveLevel>) {
            keep = (static_cast<int>(current[idx].freq) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size()));
        } else {
            keep = (static_cast<int>(current[idx].freq) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size())) ||
                   (current[idx + 1].child != current[idx].child);
        }
        if (keep) {
            if (write != idx) {
                current[write] = current[idx];
            }
            while (pfirst != plast &&
                   pfirst->child <= static_cast<std::uint32_t>(idx)) {
                pfirst->child = static_cast<std::uint32_t>(write);
                ++pfirst;
            }
            ++write;
        }
    }
    return write;
}

void Constructor::Cut() {
    for (int lvl = opts_.num; lvl > 0; --lvl) {
        if (cutoffs_[lvl] <= 0) {
            continue;
        }
        auto& parent = node_levels_[lvl - 1];
        if (lvl == opts_.num) {
            int new_size = CutLevel(parent, leaves_, cutoffs_[lvl]);
            leaves_.resize(static_cast<std::size_t>(new_size));
        } else {
            auto& level = node_levels_[lvl];
            int new_size = CutLevel(parent, level, cutoffs_[lvl]);
            level.resize(static_cast<std::size_t>(new_size));
        }
    }
}

template <typename ChildLevel>
void Constructor::DiscountLevel(NodeLevel& level, 
                            ChildLevel& child, 
                            Discounter& disc) {
    for (std::size_t idx = 0; idx + 1 < level.size(); ++idx) {
        Node& node = level[idx];
        Node& next = level[idx + 1];
        for (std::size_t child_idx = node.child; child_idx < next.child; ++child_idx) {
            double discounted = disc.Discount(child[child_idx].freq);
            double pr = discounted / node.freq;
            pr = std::clamp(pr, 1e-12, 1.0 - 1e-9);
            double encoded = opts_.use_log_pr ? -std::log(pr) : pr;
            child[child_idx].pr = static_cast<float>(encoded);
        }
    }
}

double Constructor::CalcScore(int level, std::vector<int>& indices, std::vector<TokenID>& words) {
    double ph = 1.0;
    for (int i = 1; i < level; ++i) {
        ph *= GetPr(i, words.data() + level - i + 1);
    }
    const Node& parent = node_levels_[level - 1][indices[level - 1]];
    double bow = opts_.use_log_pr ? std::exp(-parent.bow) : parent.bow;
    double phw = 0.0;
    if (level == opts_.num) {
        const Leave& leaf = leaves_[indices[level]];
        phw = opts_.use_log_pr ? std::exp(-leaf.pr) : leaf.pr;
    } else {
        const Node& node = node_levels_[level][indices[level]];
        phw = opts_.use_log_pr ? std::exp(-node.pr) : node.pr;
    }
    double ph_w = GetPr(level - 1, words.data() + 2);
    if (prune_cache_level_ != level - 1 || prune_cache_index_ != indices[level - 1]) {
        prune_cache_level_ = level - 1;
        prune_cache_index_ = indices[level - 1];
        prune_cache_pa_ = 1.0;
        prune_cache_pb_ = 1.0;
        std::size_t begin = parent.child;
        std::size_t end = node_levels_[level - 1][indices[level - 1] + 1].child;
        for (std::size_t child_idx = begin; child_idx < end; ++child_idx) {
            double pr = 0.0;
            TokenID wid = 0;
            if (level == opts_.num) {
                const Leave& leaf = leaves_[child_idx];
                pr = opts_.use_log_pr ? std::exp(-leaf.pr) : leaf.pr;
                wid = leaf.id;
            } else {
                const Node& node = node_levels_[level][child_idx];
                pr = opts_.use_log_pr ? std::exp(-node.pr) : node.pr;
                wid = node.id;
            }
            prune_cache_pa_ -= pr;
            words[level] = wid;
            double pr_back = GetPr(level - 1, words.data() + 2);
            prune_cache_pb_ -= pr_back;
        }
    }
    double pa = prune_cache_pa_;
    double pb = prune_cache_pb_;
    if (pa <= 0.0 || pb <= 0.0) {
        return 0.0;
    }
    double phw_backoff = bow * ph_w;
    double score = phw * (std::log(phw) - std::log(phw_backoff));
    return std::max(score, 0.0);
}

void Constructor::PruneLevel(int level) {
    if (prune_cutoffs_.empty() || prune_cutoffs_[level] <= 0) {
        return;
    }
    int n = prune_sizes_[level] - 1;
    if (n <= 0) {
        return;
    }
    std::vector<NodeScore> candidates;
    candidates.reserve(static_cast<std::size_t>(n));
    std::vector<int> indices(opts_.num + 1, 0);
    std::vector<TokenID> words(opts_.num + 2, 0);

    for (int idx = 0; idx < n; ++idx) {
        indices[level] = idx;
        if (level == opts_.num) {
            words[level] = leaves_[idx].id;
        } else {
            words[level] = node_levels_[level][idx].id;
        }
        for (int j = level - 1; j >= 0; --j) {
            int parent_idx = indices[j];
            const Node* parent = &node_levels_[j][parent_idx];
            while ((parent + 1)->child <= static_cast<std::uint32_t>(indices[j + 1])) {
                ++parent;
                ++indices[j];
            }
            words[j] = parent->id;
        }
        bool has_child = false;
        if (level != opts_.num) {
            const Node& node = node_levels_[level][idx];
            has_child = ((node_levels_[level][idx + 1].child - node.child) > 0);
        }
        double dist = has_child ? 0.0 : CalcScore(level, indices, words);
        candidates.push_back(NodeScore{dist, static_cast<std::uint32_t>(idx), has_child});
    }
    std::sort(candidates.begin(), candidates.end());
    int cuts = std::min(prune_cutoffs_[level], static_cast<int>(candidates.size()));
    double mark = opts_.use_log_pr ? 0.0 : 1.0;
    for (int i = 0; i < cuts; ++i) {
        if (candidates[i].has_child) {
            continue;
        }
        if (level == opts_.num) {
            leaves_[candidates[i].index].pr = mark;
        } else {
            node_levels_[level][candidates[i].index].pr = mark;
        }
    }
    if (level == opts_.num) {
        auto new_size = CutLevelByMark(node_levels_[level - 1], leaves_, mark);
        leaves_.resize(new_size);
        prune_sizes_[level] = static_cast<int>(new_size);
    } else {
        auto new_size = CutLevelByMark(node_levels_[level - 1], node_levels_[level], mark);
        node_levels_[level].resize(new_size);
        prune_sizes_[level] = static_cast<int>(new_size);
    }
}

void Constructor::Discount() {
    for (int lvl = opts_.num; lvl >= 1; --lvl) {
        auto* disc = discounters_[lvl];
        if (!disc) {
            throw std::runtime_error("missing discounter for level");
        }
        disc->Init(MaxR, nr_[lvl]);
    }
    for (int lvl = opts_.num - 1; lvl >= 0; --lvl) {
        auto& level = node_levels_[lvl];
        if (lvl == opts_.num - 1) {
            DiscountLevel(level, leaves_, *discounters_[lvl+1]);
        } else {
            DiscountLevel(level, node_levels_[lvl+1], *discounters_[lvl+1]);
        }
    }
    Node& root = node_levels_[0][0];
    double base = 1.0 / std::max<std::uint32_t>(opts_.token_count, 1);
    root.pr = opts_.use_log_pr ? -std::log(base) : base;
    root.pr = static_cast<float>(root.pr);
}

template <typename ChildLevel>
double Constructor::CalcNodeBow(int level, 
                            TokenID* words,
                            const ChildLevel& child, 
                            std::size_t begin, 
                            std::size_t end) const {
    if (begin >= end) {
        return 1.0;
    }
    double sum_child = 0.0;
    double sum_backoff = 0.0;
    for (std::size_t idx = begin; idx < end; ++idx) {
        double pr = opts_.use_log_pr ? std::exp(-child[idx].pr) : child[idx].pr;
        sum_child += pr;
        words[level + 1] = child[idx].id;
        sum_backoff += GetPr(level, words + 2);
    }
    if (sum_child >= 1.0 || sum_backoff >= 1.0) {
        double bow = std::max(sum_child, sum_backoff) + 0.0001;
        return (bow - sum_child) / (bow - sum_backoff);
    }
    return (1.0 - sum_child) / (1.0 - sum_backoff);
}


void Constructor::CalcBow() {
    std::vector<TokenID> words(opts_.num + 2, 0);
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        auto& level = node_levels_[lvl];
        if (level.size() <= 1) {
            continue;
        }
        std::vector<Node*> bases(static_cast<std::size_t>(lvl + 1));
        std::vector<int> idx(static_cast<std::size_t>(lvl + 1), 0);
        for (int i = 0; i <= lvl; ++i) {
            bases[static_cast<std::size_t>(i)] = node_levels_[i].data();
        }
        int sz = static_cast<int>(level.size()) - 1;
        for (; idx[static_cast<std::size_t>(lvl)] < sz; ++idx[static_cast<std::size_t>(lvl)]) {
            words[lvl] = bases[static_cast<std::size_t>(lvl)][idx[static_cast<std::size_t>(lvl)]].id;
            for (int k = lvl - 1; k >= 0; --k) {
                Node* base = bases[static_cast<std::size_t>(k)];
                while (base[idx[static_cast<std::size_t>(k)] + 1].child <=
                       static_cast<std::uint32_t>(idx[static_cast<std::size_t>(k + 1)])) {
                    ++idx[static_cast<std::size_t>(k)];
                }
                words[k] = base[idx[static_cast<std::size_t>(k)]].id;
            }
            Node& node = bases[static_cast<std::size_t>(lvl)][idx[static_cast<std::size_t>(lvl)]];
            Node& next = bases[static_cast<std::size_t>(lvl)][idx[static_cast<std::size_t>(lvl)] + 1];
            double bow = 1.0;
            if (lvl == opts_.num - 1) {
                bow = CalcNodeBow(lvl,
                                    words.data(),
                                    leaves_,
                                    node.child,
                                    next.child);
            } else {
                bow = CalcNodeBow(lvl,
                                    words.data(),
                                    node_levels_[lvl + 1],
                                    node.child,
                                    next.child);
            }
            node.bow = opts_.use_log_pr ? -std::log(bow) : bow;
            node.bow = static_cast<float>(node.bow);
        }
    }
}


const void* Constructor::FindChild(int level, const Node* node, TokenID id) const {
    std::uint32_t begin = node->child;
    std::uint32_t end = (node + 1)->child;
    if (level == opts_.num - 1) {
        const Leave* base = leaves_.data();
        auto it = std::lower_bound(base + begin, base + end, id, [](const Leave& leaf, TokenID value) {
            return leaf.id < value;
        });
        if (it != base + end && it->id == id) {
            return it;
        }
        return nullptr;
    }
    const Node* base = node_levels_[level + 1].data();
    auto it = std::lower_bound(base + begin, base + end, id, [](const Node& child, TokenID value) {
        return child.id < value;
    });
    if (it != base + end && it->id == id) {
        return it;
    }
    return nullptr;
}

double Constructor::GetPr(int n, const TokenID* words) const {
    const Node* root = node_levels_[0].data();
    if (n <= 0 || root == nullptr) {
        return opts_.use_log_pr ? std::exp(-root->pr) : root->pr;
    }
    const void* pnode = root;
    int lvl = 0;
    double bow = 1.0;
    while (pnode != nullptr && lvl < n) {
        const Node* current = static_cast<const Node*>(pnode);
        bow = opts_.use_log_pr ? std::exp(-current->bow) : current->bow;
        pnode = FindChild(lvl, current, words[lvl]);
        ++lvl;
    }
    if (pnode != nullptr) {
        if (lvl == opts_.num) {
            const Leave* leaf = static_cast<const Leave*>(pnode);
            return opts_.use_log_pr ? std::exp(-leaf->pr) : leaf->pr;
        }
        const Node* node = static_cast<const Node*>(pnode);
        return opts_.use_log_pr ? std::exp(-node->pr) : node->pr;
    }
    if (n > 0 && lvl == n - 1) {
        return bow * GetPr(n - 1, words + 1);
    }
    return GetPr(n - 1, words + 1);
}

void Constructor::Finalize() {
    CountCnt();
    AppendTails();
    Cut();
    Discount();
    CalcBow();
}

void Constructor::Prune(const std::vector<int>& reserves) {
    prune_sizes_.assign(opts_.num + 1, 0);
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        prune_sizes_[lvl] = static_cast<int>(node_levels_[lvl].size());
    }
    prune_sizes_[opts_.num] = static_cast<int>(leaves_.size());

    prune_cutoffs_.assign(opts_.num + 1, 0);
    for (int lvl = 1; lvl <= opts_.num; ++lvl) {
        int remaining = prune_sizes_[lvl] - 1;
        int reserve = 0;
        if (lvl <= static_cast<int>(reserves.size())) {
            reserve = reserves[static_cast<std::size_t>(lvl - 1)];
        }
        prune_cutoffs_[lvl] = std::max(0, remaining - reserve);
    }

    for (int level = opts_.num; level > 0; --level) {
        PruneLevel(level);
    }
    CalcBow();
}


namespace {

struct DiskNode {
    TokenID id = 0;
    float pr = 0.0f;
    std::uint32_t child = 0;
    float bow = 0.0f;
};

struct DiskLeave {
    TokenID id = 0;
    float pr = 0.0f;
};

}  // namespace

void Constructor::Write(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open output file");
    }
    int order = opts_.num;
    out.write(reinterpret_cast<const char*>(&order), sizeof(order));
    std::uint32_t flag = opts_.use_log_pr ? 1u : 0u;
    out.write(reinterpret_cast<const char*>(&flag), sizeof(flag));
    for (int lvl = 0; lvl <= order; ++lvl) {
        std::uint32_t size = 0;
        if (lvl == order) {
            size = static_cast<std::uint32_t>(leaves_.size());
        } else {
            size = static_cast<std::uint32_t>(node_levels_[lvl].size());
        }
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    }
    for (int lvl = 0; lvl < order; ++lvl) {
        const auto& level = node_levels_[lvl];
        for (const auto& node : level) {
            DiskNode raw;
            raw.id = node.id;
            raw.pr = static_cast<float>(node.pr);
            raw.child = node.child;
            raw.bow = static_cast<float>(node.bow);
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        }
    }
    for (const auto& leaf : leaves_) {
        DiskLeave raw;
        raw.id = leaf.id;
        raw.pr = static_cast<float>(leaf.pr);
        out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
    }
}


void RunConstruct(ConstructOptions options) {
    auto input_path = options.input;
    auto output_path = options.output;
    int num = options.num;
    auto prune_reserves = std::move(options.prune_reserves);

    Constructor builder(std::move(options));

    std::ifstream input(input_path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open idngram file");
    }
    std::vector<TokenID> ids(num);
    std::uint32_t freq = 0;
    while (input.read(reinterpret_cast<char*>(ids.data()),
                      static_cast<std::streamsize>(ids.size() * sizeof(TokenID)))) {
        if (!input.read(reinterpret_cast<char*>(&freq),
                        static_cast<std::streamsize>(sizeof(freq)))) {
            break;
        }
        builder.InsertItem(ids, freq);
    }
    builder.Finalize();
    if (!prune_reserves.empty()) {
        builder.Prune(prune_reserves);
    }

    builder.Write(output_path);
}

} // namespace sime
