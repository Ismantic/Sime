#include "construct.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace sime {

namespace {

constexpr TokenID TailMarker = 0x00FFFFFF;

} // namespace

void NeyDiscounter::Init(std::uint64_t n1, std::uint64_t n2,
                         std::uint64_t n3, std::uint64_t n4) {
    float_t d1 = static_cast<float_t>(n1);
    float_t d2 = static_cast<float_t>(n2);
    float_t d3 = static_cast<float_t>(n3);
    float_t d4 = static_cast<float_t>(n4);
    float_t y = (d1 == 0.0) ? 0.0 : d1 / (d1 + 2.0 * d2);
    v1_ = 1.0 - 2.0 * y * (d2 / std::max(d1, 1.0));
    v2_ = 2.0 - 3.0 * y * (d3 / std::max(d2, 1.0));
    v3_ = 3.0 - 4.0 * y * (d4 / std::max(d3, 1.0));
    v1_ = std::max(v1_, 0.0);
    v2_ = std::max(v2_, 0.0);
    v3_ = std::max(v3_, 0.0);
}

float_t NeyDiscounter::Discount(float_t cnt) const {
    if (cnt <= 0.0) return 0.0;
    if (cnt <= 1.0) return cnt - v1_;
    if (cnt <= 2.0) return cnt - v2_;
    return cnt - v3_;
}

bool Constructor::NodeScore::operator<(const NodeScore& other) const {
    if (has_down == other.has_down) {
        return score < other.score;
    }
    return !has_down && other.has_down;
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

    node_levels_[0].push_back(Node{0, 0, 0.0, 0.0, 0.0, 0});

    nt_.assign(opts_.num + 1, {});

    cuts_.assign(opts_.num + 1, 0);
    for (std::size_t i = 0; i < opts_.cutoffs.size() && i < static_cast<std::size_t>(opts_.num); ++i) {
        cuts_[i + 1] = opts_.cutoffs[i];
    }

    discounters_.resize(opts_.num + 1);
}

bool Constructor::IsBreaker(TokenID i) const {
    return i == SentenceStart || i == SentenceEnd;
}

template <typename Level>
std::size_t Constructor::CutLevelByMark(std::vector<Node>& ups, Level& current, float_t mark_value) {
    if (current.empty()) {
        return 0;
    }
    const std::size_t total = current.size();
    std::vector<std::size_t> removed_prefix(total + 1, 0);
    std::size_t new_size = 0;
    for (std::size_t idx = 0; idx < total; ++idx) {
        bool keep = (current[idx].pro != mark_value) || (idx + 1 == total);
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

    for (auto& up : ups) {
        std::size_t down = up.down;
        if (down > total) {
            down = total;
        }
        std::size_t removed = removed_prefix[down];
        up.down = static_cast<std::uint32_t>(down - removed);
    }
    return new_size;
}

void Constructor::InsertItem(const std::vector<TokenID>& ids, std::uint32_t cnt) {
    if (static_cast<int>(ids.size()) != opts_.num) {
        throw std::invalid_argument("ngram length mismatch");
    }
    bool breaker = IsBreaker(ids[0]);
    if (!breaker) {
        node_levels_[0][0].cnt += cnt;
    }
    bool branch = false;
    for (int lvl = 1; (!breaker && lvl < opts_.num); ++lvl) {
        auto& up_level = node_levels_[lvl - 1];
        auto& current = node_levels_[lvl];
        bool need_new = branch ||
                        current.empty() ||
                        up_level.back().down >= static_cast<std::uint32_t>(current.size());
        if (!need_new && current.back().id != ids[lvl-1]) {
            need_new = true;
        }

        if (need_new) {
            std::uint32_t down =
                (lvl == opts_.num - 1)
                    ? static_cast<std::uint32_t>(leaves_.size())
                    : static_cast<std::uint32_t>(node_levels_[lvl + 1].size());
            current.push_back(Node{ids[lvl - 1], down, static_cast<float_t>(cnt), 0.0, 0.0, 0});
        } else {
            current.back().cnt += cnt;
        }

        branch = need_new;
        breaker = (lvl > 1 && IsBreaker(ids[lvl - 1])) || IsBreaker(ids[lvl]);
    }

    if (!breaker) {
        if (cnt > cuts_[opts_.num]) {
            leaves_.push_back(Leave{ids.back(), static_cast<float_t>(cnt), 0.0, 0});
        } else {
            if (cnt >= 1 && cnt <= 4) {
                nt_[opts_.num][cnt] += 1;
            }
        }
    }
}

void Constructor::CountCnt() {
    for (int lvl = 1; lvl < opts_.num; ++lvl) {
        for (const auto& node : node_levels_[lvl]) {
            auto c = static_cast<std::uint32_t>(node.cnt);
            if (c >= 1 && c <= 4) {
                nt_[lvl][c] += 1;
            }
        }
    }
    for (const auto& leaf : leaves_) {
        auto c = static_cast<std::uint32_t>(leaf.cnt);
        if (c >= 1 && c <= 4) {
            nt_[opts_.num][c] += 1;
        }
    }
}

void Constructor::AppendTails() {
    const float_t tail_pro = std::numeric_limits<float_t>::denorm_min();
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        std::uint32_t down_count = 0;
        if (lvl == opts_.num - 1) {
            down_count = static_cast<std::uint32_t>(leaves_.size());
        } else {
            down_count = static_cast<std::uint32_t>(node_levels_[lvl + 1].size());
        }
        node_levels_[lvl].push_back(Node{TailMarker, down_count, 1.0, 0.0, 0.0, 0});
        node_levels_[lvl].back().pro = tail_pro;
    }
    leaves_.push_back(Leave{0, 1.0, tail_pro, 0});
}

template <typename Level>
int Constructor::CutLevel(NodeLevel& up_level, Level& current, int threshold) {
    if (threshold <= 0) {
        return static_cast<int>(current.size());
    }
    int write = 0;
    auto up_it = up_level.begin();
    auto up_last = up_level.end();
    for (int idx = 0; idx < static_cast<int>(current.size()); ++idx) {
        bool keep = false;
        if constexpr (std::is_same_v<Level, LeaveLevel>) {
            keep = (static_cast<int>(current[idx].cnt) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size()));
        } else {
            keep = (static_cast<int>(current[idx].cnt) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size())) ||
                   (current[idx + 1].down != current[idx].down);
        }
        if (keep) {
            if (write != idx) {
                current[write] = current[idx];
            }
            while (up_it != up_last &&
                   up_it->down <= static_cast<std::uint32_t>(idx)) {
                up_it->down = static_cast<std::uint32_t>(write);
                ++up_it;
            }
            ++write;
        }
    }
    return write;
}

void Constructor::Cut() {
    for (int lvl = opts_.num; lvl > 0; --lvl) {
        if (cuts_[lvl] <= 0) {
            continue;
        }
        auto& up_level = node_levels_[lvl - 1];
        if (lvl == opts_.num) {
            int new_size = CutLevel(up_level, leaves_, cuts_[lvl]);
            leaves_.resize(static_cast<std::size_t>(new_size));
        } else {
            auto& level = node_levels_[lvl];
            int new_size = CutLevel(up_level, level, cuts_[lvl]);
            level.resize(static_cast<std::size_t>(new_size));
        }
    }
}

template <typename DownLevel>
void Constructor::DiscountLevel(NodeLevel& level,
                                DownLevel& down_level,
                                NeyDiscounter& disc,
                                bool use_context) {
    for (std::size_t idx = 0; idx + 1 < level.size(); ++idx) {
        Node& node = level[idx];
        Node& next = level[idx + 1];
        float_t denom;
        if (use_context) {
            denom = 0.0;
            for (std::size_t i = node.down; i < next.down; ++i) {
                denom += down_level[i].ctx;
            }
        } else {
            denom = node.cnt;
        }
        for (std::size_t down_idx = node.down; down_idx < next.down; ++down_idx) {
            float_t raw = use_context
                ? static_cast<float_t>(down_level[down_idx].ctx)
                : down_level[down_idx].cnt;
            float_t discounted = disc.Discount(raw);
            float_t pro = discounted / std::max(denom, 1.0);
            pro = std::clamp(pro, 1e-12, 1.0 - 1e-9);
            float_t encoded = -std::log(pro);
            down_level[down_idx].pro = static_cast<float>(encoded);
        }
    }
}

float_t Constructor::CalcScore(int level, std::vector<int>& indices, std::vector<TokenID>& words) {
    float_t ph = 1.0;
    for (int i = 1; i < level; ++i) {
        ph *= GetPro(i, words.data() + level - i + 1);
    }
    const Node& up = node_levels_[level - 1][indices[level - 1]];
    float_t bow = std::exp(-up.bow);
    float_t phw = 0.0;
    if (level == opts_.num) {
        const Leave& leaf = leaves_[indices[level]];
        phw = std::exp(-leaf.pro);
    } else {
        const Node& node = node_levels_[level][indices[level]];
        phw = std::exp(-node.pro);
    }
    float_t ph_w = GetPro(level - 1, words.data() + 2);
    if (prune_cache_level_ != level - 1 || prune_cache_index_ != indices[level - 1]) {
        prune_cache_level_ = level - 1;
        prune_cache_index_ = indices[level - 1];
        prune_cache_pa_ = 1.0;
        prune_cache_pb_ = 1.0;
        std::size_t begin = up.down;
        std::size_t end = node_levels_[level - 1][indices[level - 1] + 1].down;
        for (std::size_t down_idx = begin; down_idx < end; ++down_idx) {
            float_t pro = 0.0;
            TokenID wid = 0;
            if (level == opts_.num) {
                const Leave& leaf = leaves_[down_idx];
                pro = std::exp(-leaf.pro);
                wid = leaf.id;
            } else {
                const Node& node = node_levels_[level][down_idx];
                pro = std::exp(-node.pro);
                wid = node.id;
            }
            prune_cache_pa_ -= pro;
            words[level] = wid;
            float_t pro_back = GetPro(level - 1, words.data() + 2);
            prune_cache_pb_ -= pro_back;
        }
    }
    float_t pa = prune_cache_pa_;
    float_t pb = prune_cache_pb_;
    if (pa <= 0.0 || pb <= 0.0) {
        return 0.0;
    }
    float_t phw_backoff = bow * ph_w;
    float_t score = phw * (std::log(phw) - std::log(phw_backoff));
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
            int up_idx = indices[j];
            const Node* up = &node_levels_[j][up_idx];
            while ((up + 1)->down <= static_cast<std::uint32_t>(indices[j + 1])) {
                ++up;
                ++indices[j];
            }
            words[j] = up->id;
        }
        bool has_down = false;
        if (level != opts_.num) {
            const Node& node = node_levels_[level][idx];
            has_down = ((node_levels_[level][idx + 1].down - node.down) > 0);
        }
        float_t dist = has_down ? 0.0 : CalcScore(level, indices, words);
        candidates.push_back(NodeScore{dist, static_cast<std::uint32_t>(idx), has_down});
    }
    std::sort(candidates.begin(), candidates.end());
    int cuts = std::min(prune_cutoffs_[level], static_cast<int>(candidates.size()));
    float_t mark = 0.0;
    for (int i = 0; i < cuts; ++i) {
        if (candidates[i].has_down) {
            continue;
        }
        if (level == opts_.num) {
            leaves_[candidates[i].index].pro = mark;
        } else {
            node_levels_[level][candidates[i].index].pro = mark;
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

void Constructor::ComputeContinuationCounts() {
    // For bigram models: compute unigram continuation counts from leaves.
    // N1+(.,w) = number of distinct contexts v where bigram (v,w) exists.
    if (opts_.num == 2) {
        auto& unigram_level = node_levels_[1];

        std::unordered_map<std::uint32_t, std::uint32_t> ctx_map;
        for (std::size_t v = 0; v + 1 < unigram_level.size(); ++v) {
            for (std::size_t w = unigram_level[v].down;
                 w < unigram_level[v + 1].down; ++w) {
                ctx_map[leaves_[w].id]++;
            }
        }
        for (auto& node : unigram_level) {
            auto it = ctx_map.find(node.id);
            if (it != ctx_map.end()) {
                node.ctx = it->second;
            }
        }
        return;
    }

    if (opts_.num < 3) return;

    auto& l1 = node_levels_[1];
    auto& l2 = node_levels_[2];

    // For each trigram path root->u(l1)->v(l2)->w(leaf),
    // compute N1+(.,v,w) for the bigram node root->v(l1)->w(l2).
    std::unordered_map<std::uint64_t, std::uint32_t> bigram_ctx;
    for (std::size_t u_idx = 0; u_idx + 1 < l1.size(); ++u_idx) {
        for (std::size_t v_idx = l1[u_idx].down; v_idx + 1 < l2.size() &&
             v_idx < l1[u_idx + 1].down; ++v_idx) {
            TokenID v_id = l2[v_idx].id;
            for (std::size_t w_idx = l2[v_idx].down; w_idx < l2[v_idx + 1].down; ++w_idx) {
                TokenID w_id = leaves_[w_idx].id;
                auto key = (static_cast<std::uint64_t>(v_id) << 32) | w_id;
                bigram_ctx[key]++;
            }
        }
    }

    // Set ctx on level 2 nodes (bigram level).
    for (std::size_t v_idx = 0; v_idx + 1 < l1.size(); ++v_idx) {
        TokenID v_id = l1[v_idx].id;
        for (std::size_t w_idx = l1[v_idx].down;
             w_idx + 1 < l2.size() && w_idx < l1[v_idx + 1].down; ++w_idx) {
            TokenID w_id = l2[w_idx].id;
            auto key = (static_cast<std::uint64_t>(v_id) << 32) | w_id;
            auto it = bigram_ctx.find(key);
            if (it != bigram_ctx.end()) {
                l2[w_idx].ctx = it->second;
            }
        }
    }

    // Set ctx on level 1 nodes (unigram level).
    // N1+(.,v) = number of distinct u's that have level 2 child v.
    std::unordered_map<std::uint32_t, std::uint32_t> unigram_ctx;
    for (std::size_t u_idx = 0; u_idx + 1 < l1.size(); ++u_idx) {
        for (std::size_t v_idx = l1[u_idx].down;
             v_idx + 1 < l2.size() && v_idx < l1[u_idx + 1].down; ++v_idx) {
            unigram_ctx[l2[v_idx].id]++;
        }
    }
    for (std::size_t v_idx = 0; v_idx + 1 < l1.size(); ++v_idx) {
        auto it = unigram_ctx.find(l1[v_idx].id);
        if (it != unigram_ctx.end()) {
            l1[v_idx].ctx = it->second;
        }
    }
}

void Constructor::Discount() {
    for (int lvl = opts_.num; lvl >= 1; --lvl) {
        discounters_[lvl].Init(nt_[lvl][1], nt_[lvl][2], nt_[lvl][3], nt_[lvl][4]);
    }

    ComputeContinuationCounts();

    for (int lvl = opts_.num - 1; lvl >= 0; --lvl) {
        auto& level = node_levels_[lvl];
        bool use_context = lvl < opts_.num - 1;
        if (lvl == opts_.num - 1) {
            DiscountLevel(level, leaves_, discounters_[lvl+1], false);
        } else {
            DiscountLevel(level, node_levels_[lvl+1], discounters_[lvl+1], use_context);
        }
    }
    Node& root = node_levels_[0][0];
    float_t base = 1.0 / std::max<std::uint32_t>(opts_.token_count, 1);
    root.pro = -std::log(base);
    root.pro = static_cast<float>(root.pro);
}

template <typename DownLevel>
float_t Constructor::CalcNodeBow(int level,
                                 TokenID* words,
                                 const DownLevel& down_level,
                                 std::size_t begin,
                                 std::size_t end) const {
    if (begin >= end) {
        return 1.0;
    }
    float_t sum_down = 0.0;
    float_t sum_backoff = 0.0;
    for (std::size_t idx = begin; idx < end; ++idx) {
        float_t pro = std::exp(-down_level[idx].pro);
        sum_down += pro;
        words[level + 1] = down_level[idx].id;
        sum_backoff += GetPro(level, words + 2);
    }
    if (sum_down >= 1.0 || sum_backoff >= 1.0) {
        float_t bow = std::max(sum_down, sum_backoff) + 0.0001;
        return (bow - sum_down) / (bow - sum_backoff);
    }
    return (1.0 - sum_down) / (1.0 - sum_backoff);
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
                while (base[idx[static_cast<std::size_t>(k)] + 1].down <=
                       static_cast<std::uint32_t>(idx[static_cast<std::size_t>(k + 1)])) {
                    ++idx[static_cast<std::size_t>(k)];
                }
                words[k] = base[idx[static_cast<std::size_t>(k)]].id;
            }
            Node& node = bases[static_cast<std::size_t>(lvl)][idx[static_cast<std::size_t>(lvl)]];
            Node& next = bases[static_cast<std::size_t>(lvl)][idx[static_cast<std::size_t>(lvl)] + 1];
            float_t bow = 1.0;
            if (lvl == opts_.num - 1) {
                bow = CalcNodeBow(lvl,
                                    words.data(),
                                    leaves_,
                                    node.down,
                                    next.down);
            } else {
                bow = CalcNodeBow(lvl,
                                    words.data(),
                                    node_levels_[lvl + 1],
                                    node.down,
                                    next.down);
            }
            node.bow = -std::log(bow);
            node.bow = static_cast<float>(node.bow);
        }
    }
}


const void* Constructor::FindDown(int level, const Node* node, TokenID id) const {
    std::uint32_t begin = node->down;
    std::uint32_t end = (node + 1)->down;
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
    auto it = std::lower_bound(base + begin, base + end, id, [](const Node& down, TokenID value) {
        return down.id < value;
    });
    if (it != base + end && it->id == id) {
        return it;
    }
    return nullptr;
}

float_t Constructor::GetPro(int n, const TokenID* words) const {
    const Node* root = node_levels_[0].data();
    if (n <= 0 || root == nullptr) {
        return std::exp(-root->pro);
    }
    const void* pnode = root;
    int lvl = 0;
    float_t bow = 1.0;
    while (pnode != nullptr && lvl < n) {
        const Node* current = static_cast<const Node*>(pnode);
        bow = std::exp(-current->bow);
        pnode = FindDown(lvl, current, words[lvl]);
        ++lvl;
    }
    if (pnode != nullptr) {
        if (lvl == opts_.num) {
            const Leave* leaf = static_cast<const Leave*>(pnode);
            return std::exp(-leaf->pro);
        }
        const Node* node = static_cast<const Node*>(pnode);
        return std::exp(-node->pro);
    }
    if (n > 0 && lvl == n - 1) {
        return bow * GetPro(n - 1, words + 1);
    }
    return GetPro(n - 1, words + 1);
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
    float pro = 0.0f;
    std::uint32_t down = 0;
    float bow = 0.0f;
};

struct DiskLeave {
    TokenID id = 0;
    float pro = 0.0f;
};

}  // namespace

void Constructor::Write(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open output file");
    }
    int order = opts_.num;
    out.write(reinterpret_cast<const char*>(&order), sizeof(order));
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
            raw.pro = static_cast<float>(node.pro);
            raw.down = node.down;
            raw.bow = static_cast<float>(node.bow);
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        }
    }
    for (const auto& leaf : leaves_) {
        DiskLeave raw;
        raw.id = leaf.id;
        raw.pro = static_cast<float>(leaf.pro);
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
    std::uint32_t cnt = 0;
    while (input.read(reinterpret_cast<char*>(ids.data()),
                      static_cast<std::streamsize>(ids.size() * sizeof(TokenID)))) {
        if (!input.read(reinterpret_cast<char*>(&cnt),
                        static_cast<std::streamsize>(sizeof(cnt)))) {
            break;
        }
        builder.InsertItem(ids, cnt);
    }
    builder.Finalize();
    if (!prune_reserves.empty()) {
        builder.Prune(prune_reserves);
    }

    builder.Write(output_path);
}

} // namespace sime
