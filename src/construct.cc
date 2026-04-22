#include "construct.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace sime {

namespace {

constexpr TokenID TailMarker = 0x00FFFFFF;

} // namespace

bool Constructor::NodeScore::operator<(const NodeScore& other) const {
    return score < other.score;
}

Constructor::Constructor(ConstructOptions opts) : opts_(std::move(opts)) {
    if (opts_.num < 1 || opts_.num > 3) {
        throw std::invalid_argument("order must be in [1, 3]");
    }
    node_levels_.resize(opts_.num);
    for (auto& level : node_levels_) {
        level.reserve(1024);
    }
    leaves_.reserve(1024);

    node_levels_[0].push_back(Node{0, 0, 0.0, 0.0, 0.0});

    cuts_.assign(opts_.num + 1, 0);
    for (std::size_t i = 0; i < opts_.cutoffs.size() && i < static_cast<std::size_t>(opts_.num); ++i) {
        cuts_[i + 1] = opts_.cutoffs[i];
    }

    discounters_.resize(opts_.num + 1);
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

void Constructor::InsertUnigram(TokenID w, std::uint32_t cnt) {
    if (opts_.num < 1) return;
    node_levels_[0][0].cnt += cnt;
    // down set later in FixDownPointers.
    node_levels_[1].push_back(Node{w, 0, static_cast<float_t>(cnt), 0.0, 0.0});
}

void Constructor::InsertBigram(TokenID u, TokenID v, std::uint32_t cnt) {
    if (opts_.num < 2) return;
    if (cnt <= cuts_[2]) {
        return;
    }
    node_levels_[2].push_back(Node{v, 0, static_cast<float_t>(cnt), 0.0, 0.0});
    bigram_parents_.push_back(u);
}

void Constructor::InsertTrigram(TokenID u, TokenID v, TokenID w, std::uint32_t cnt) {
    if (opts_.num < 3) return;
    if (cnt > cuts_[opts_.num]) {
        leaves_.push_back(Leave{w, static_cast<float_t>(cnt), 0.0});
        leaf_parents_.push_back({u, v});
    }
}

void Constructor::FixDownPointers() {
    // l1[i].down = start index in l2 where bigrams whose parent is l1[i].id begin.
    if (opts_.num >= 2) {
        auto& l1 = node_levels_[1];
        std::size_t bi = 0;
        for (std::size_t i = 0; i < l1.size(); ++i) {
            l1[i].down = static_cast<std::uint32_t>(bi);
            while (bi < bigram_parents_.size() && bigram_parents_[bi] == l1[i].id) {
                ++bi;
            }
        }
    }

    // l2[j].down = start in leaves_ where trigrams whose parent is (u, v) begin.
    if (opts_.num >= 3) {
        auto& l1 = node_levels_[1];
        auto& l2 = node_levels_[2];
        std::size_t li = 0;
        std::size_t tri = 0;
        for (std::size_t j = 0; j < l2.size(); ++j) {
            while (li + 1 < l1.size() && l1[li + 1].down <= j) {
                ++li;
            }
            TokenID u = l1[li].id;
            TokenID v = l2[j].id;
            l2[j].down = static_cast<std::uint32_t>(tri);
            while (tri < leaf_parents_.size() &&
                   leaf_parents_[tri][0] == u &&
                   leaf_parents_[tri][1] == v) {
                ++tri;
            }
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
        node_levels_[lvl].push_back(Node{TailMarker, down_count, 1.0, 0.0, 0.0});
        node_levels_[lvl].back().pro = tail_pro;
    }
    leaves_.push_back(Leave{0, 1.0, tail_pro});
}

template <typename Level>
int Constructor::CutLevel(NodeLevel& up_level, Level& current, int threshold,
                          const std::vector<bool>& protect_mask) {
    if (threshold <= 0) {
        return static_cast<int>(current.size());
    }
    int write = 0;
    auto up_it = up_level.begin();
    auto up_last = up_level.end();
    for (int idx = 0; idx < static_cast<int>(current.size()); ++idx) {
        bool keep = false;
        bool is_protected = (idx < static_cast<int>(protect_mask.size()) &&
                             protect_mask[idx]);
        if constexpr (std::is_same_v<Level, LeaveLevel>) {
            keep = is_protected ||
                   (static_cast<int>(current[idx].cnt) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size()));
        } else {
            keep = is_protected ||
                   (static_cast<int>(current[idx].cnt) > threshold) ||
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
                                int parent_lvl) {
    // Interpolated Absolute Discounting. For each parent h at `parent_lvl`:
    //   gamma(h) = (D * num_children + lost_mass) / denom
    //   P_I(w | h) = max(cnt(h,w) - D, 0) / denom + gamma(h) * P_I(w | h')

    constexpr float_t MinProb = 1e-12;
    constexpr float_t MaxProb = 1.0 - 1e-9;

    for (std::size_t idx = 0; idx + 1 < level.size(); ++idx) {
        Node& node = level[idx];
        Node& next = level[idx + 1];

        std::uint32_t num_children = next.down - node.down;
        float_t sum_children = 0.0;
        for (std::size_t i = node.down; i < next.down; ++i) {
            sum_children += down_level[i].cnt;
        }

        float_t denom = std::max(node.cnt, 1.0);

        float_t lost_mass = std::max(denom - sum_children, 0.0);

        float_t gamma;
        if (num_children == 0) {
            gamma = 1.0;
        } else {
            gamma = (disc.D() * static_cast<float_t>(num_children) + lost_mass)
                  / denom;
        }
        gamma = std::clamp(gamma, MinProb, MaxProb);
        node.bow = static_cast<float>(-std::log(gamma));

        for (std::size_t down_idx = node.down; down_idx < next.down; ++down_idx) {
            float_t discounted = disc.Discount(down_level[down_idx].cnt);
            float_t alpha = discounted / denom;

            TokenID w = down_level[down_idx].id;

            float_t p_lower;
            if (parent_lvl == 0) {
                p_lower = std::exp(-node_levels_[0][0].pro);
            } else if (parent_lvl == 1) {
                p_lower = GetPro(1, &w);
            } else {
                std::array<TokenID, 2> q{node.id, w};
                p_lower = GetPro(2, q.data());
            }

            float_t p_interp = alpha + gamma * p_lower;
            p_interp = std::clamp(p_interp, MinProb, MaxProb);
            down_level[down_idx].pro = static_cast<float>(-std::log(p_interp));
        }
    }
}

float_t Constructor::CalcScore(int level,
                               std::vector<int>& indices,
                               std::vector<TokenID>& words) {
    // Relative entropy contribution of removing this single n-gram (h, w).
    // Mirrors SRILM NgramLM.cc::pruneProbs (Stolcke 1998 Eq. 6):
    //
    //   gamma'(h) = (numerator + P(w|h))    /   (denominator + P(w|h'))
    //   deltaProb = log P(w|h') + log gamma'(h) - log P(w|h)
    //   deltaH    = -P(h) * [ P(w|h) * deltaProb
    //                        + numerator * (log gamma'(h) - log gamma(h)) ]
    //
    // where numerator = 1 - sum_{kept} P(w'|h),
    //       denominator = 1 - sum_{kept} P(w'|h')  (backoff query at shorter
    //       history), which are exactly Sime's prune_cache_pa_ / pb_.
    //
    // Returns the KL contribution -deltaH (>= 0); caller sorts ascending and
    // prunes the smallest contributors.

    const Node& up = node_levels_[level - 1][indices[level - 1]];
    // up.bow stores -log gamma(h), so log gamma(h) = -up.bow.
    float_t log_gamma_old = -up.bow;

    // P(w|h): stored probability at this n-gram entry.
    float_t p_orig;
    if (level == opts_.num) {
        p_orig = std::exp(-leaves_[indices[level]].pro);
    } else {
        p_orig = std::exp(-node_levels_[level][indices[level]].pro);
    }
    if (p_orig <= 0.0) return 0.0;
    float_t log_p_orig = std::log(p_orig);

    // P(w|h_shorter) via the LM's backoff query.
    float_t p_backoff = GetPro(level - 1, words.data() + 2);
    if (p_backoff <= 0.0) return 0.0;
    float_t log_p_backoff = std::log(p_backoff);

    // Cache numerator / denominator for all siblings sharing this history.
    if (prune_cache_level_ != level - 1 ||
        prune_cache_index_ != indices[level - 1]) {
        prune_cache_level_ = level - 1;
        prune_cache_index_ = indices[level - 1];
        prune_cache_pa_ = 1.0;
        prune_cache_pb_ = 1.0;
        std::size_t begin = up.down;
        std::size_t end =
            node_levels_[level - 1][indices[level - 1] + 1].down;
        for (std::size_t j = begin; j < end; ++j) {
            float_t p_child;
            TokenID wid;
            if (level == opts_.num) {
                p_child = std::exp(-leaves_[j].pro);
                wid = leaves_[j].id;
            } else {
                p_child = std::exp(-node_levels_[level][j].pro);
                wid = node_levels_[level][j].id;
            }
            prune_cache_pa_ -= p_child;
            words[level] = wid;
            prune_cache_pb_ -= GetPro(level - 1, words.data() + 2);
        }
    }
    float_t num = prune_cache_pa_;
    float_t den = prune_cache_pb_;
    if (num <= 1e-12 || den <= 1e-12) return 0.0;

    // gamma' after removing this (h, w): the removed ngram joins the "unseen"
    // mass, so both numerator and denominator gain its probability share.
    float_t num_new = num + p_orig;
    float_t den_new = den + p_backoff;
    if (num_new <= 0.0 || den_new <= 0.0) return 0.0;
    float_t log_gamma_new = std::log(num_new) - std::log(den_new);

    // deltaProb = log p_new(w|h) - log p_old(w|h)
    //           = log P(w|h') + log gamma'(h) - log P(w|h)
    float_t log_delta_prob = log_p_backoff + log_gamma_new - log_p_orig;

    // p_hist = P(h) estimated from the current LM, joint of the history
    // tokens: prod_{k=1..level-1} P(words[k] | words[1..k-1]).
    float_t p_hist = 1.0;
    for (int k = 1; k < level; ++k) {
        p_hist *= GetPro(k, words.data() + 1);
    }

    // SRILM pruneProbs stores deltaEntropy with this sign convention: it is
    // the (non-negative) relative-entropy increase caused by pruning, and
    // ngrams with the smallest value are the safest to drop. The inner sum
    // is typically negative (log p' - log p < 0 dominates), and the leading
    // `-p_hist` flips it to the positive KL value.
    float_t delta_entropy = -p_hist * (
        p_orig * log_delta_prob +
        num * (log_gamma_new - log_gamma_old)
    );

    // Negative values come from numerical noise; clamp to 0.
    return std::max(delta_entropy, 0.0);
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
        // Unprunable nodes (those with surviving children) sort to the tail
        // via a +inf sentinel so the natural ascending order places them
        // after all CalcScore-ranked candidates.
        float_t dist = has_down
            ? std::numeric_limits<float_t>::max()
            : CalcScore(level, indices, words);
        candidates.push_back(
            NodeScore{dist, static_cast<std::uint32_t>(idx), has_down});
    }
    std::sort(candidates.begin(), candidates.end());
    int cuts = std::min(prune_cutoffs_[level], static_cast<int>(candidates.size()));
    float_t mark = 0.0;
    for (int i = 0; i < cuts; ++i) {
        if (candidates[i].has_down) continue;
        // No special-token protection: closed IME vocabulary, no <unk>.
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

void Constructor::PruneBigramByPMI() {
    constexpr int level = 2;
    if (opts_.num < 2) return;
    if (prune_cutoffs_.empty() || prune_cutoffs_[level] <= 0) return;
    int n = prune_sizes_[level] - 1;
    if (n <= 0) return;
    int cuts = std::min(prune_cutoffs_[level], n);

    auto& l1 = node_levels_[1];
    auto& l2 = node_levels_[2];

    // count_back[v] = Σ_u count(u, v). Dense vector keyed by token id.
    std::vector<std::uint64_t> count_back(opts_.token_count + 1, 0);
    for (int idx = 0; idx < n; ++idx) {
        TokenID v = l2[idx].id;
        if (v < count_back.size()) {
            count_back[v] += static_cast<std::uint64_t>(l2[idx].cnt);
        }
    }

    std::vector<NodeScore> cands;
    cands.reserve(static_cast<std::size_t>(n));

    // No has_down carve-out: cascade deletion below takes any orphan
    // trigram children along with the bigram so the structural invariant
    // (every trigram has a parent bigram) is preserved.
    std::size_t up_idx = 0;
    for (int idx = 0; idx < n; ++idx) {
        while (up_idx + 1 < l1.size() &&
               l1[up_idx + 1].down <= static_cast<std::uint32_t>(idx)) {
            ++up_idx;
        }
        float_t cnt_uv = l2[idx].cnt;
        float_t cnt_u = l1[up_idx].cnt;
        TokenID v = l2[idx].id;
        float_t cnt_back_v = (v < count_back.size())
            ? static_cast<float_t>(count_back[v]) : 0.0;
        // cnt_u / cnt_uv are always > 0 post-Cut; cnt_back_v is the sum of
        // all surviving (·, v) bigram counts which is also > 0 because this
        // very bigram contributes to it.
        // S(u, v) = count(u, v)^2 / (count(u) * count_back(v))
        float_t score = (cnt_uv * cnt_uv) / (cnt_u * cnt_back_v);
        cands.push_back(NodeScore{score, static_cast<std::uint32_t>(idx),
                                  false});
    }
    std::sort(cands.begin(), cands.end());

    constexpr float_t mark = 0.0;
    for (int i = 0; i < cuts; ++i) {
        std::uint32_t idx = cands[i].index;
        l2[idx].pro = mark;
        // Cascade: mark all trigram children so they're removed too.
        if (opts_.num >= 3) {
            std::uint32_t begin = l2[idx].down;
            std::uint32_t end = l2[idx + 1].down;
            for (std::uint32_t j = begin; j < end; ++j) {
                if (j < leaves_.size()) {
                    leaves_[j].pro = mark;
                }
            }
        }
    }
    auto new_l2_size = CutLevelByMark(l1, l2, mark);
    l2.resize(new_l2_size);
    prune_sizes_[level] = static_cast<int>(new_l2_size);

    // Cascade into leaves: cut marked trigrams, update new l2's .down.
    if (opts_.num >= 3) {
        auto new_leaf_size = CutLevelByMark(l2, leaves_, mark);
        leaves_.resize(new_leaf_size);
        prune_sizes_[opts_.num] = static_cast<int>(new_leaf_size);
    }
}

void Constructor::Discount() {
    constexpr float_t kDefaultD[] = {0.0005, 0.5, 0.5};
    for (int lvl = 1; lvl <= opts_.num; ++lvl) {
        std::size_t idx = static_cast<std::size_t>(lvl - 1);
        float_t d = (idx < opts_.discounts.size())
            ? opts_.discounts[idx]
            : (idx < 3 ? kDefaultD[idx] : 0.5);
        discounters_[lvl].Init(d);
    }

    Node& root = node_levels_[0][0];
    std::uint32_t effective_vocab = std::max<std::uint32_t>(opts_.token_count, 1);
    float_t base = 1.0 / effective_vocab;
    root.pro = static_cast<float>(-std::log(base));

    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        auto& level = node_levels_[lvl];
        if (lvl == opts_.num - 1) {
            DiscountLevel(level, leaves_, discounters_[lvl + 1], lvl);
        } else {
            DiscountLevel(level, node_levels_[lvl + 1], discounters_[lvl + 1], lvl);
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
    // Returns the LM probability P(words[n-1] | words[0..n-2]).
    // Descend the Trie through the history; on a miss, multiply by gamma
    // (stored as -log in bow) and retry with a shorter history.
    if (n <= 0 || node_levels_[0].empty()) {
        return std::exp(-node_levels_[0][0].pro);  // uniform prior
    }
    const Node* cur = &node_levels_[0][0];
    int lvl = 0;
    for (int i = 0; i < n; ++i) {
        const void* down = FindDown(lvl, cur, words[i]);
        if (down == nullptr) {
            float_t bow = std::exp(-cur->bow);
            return bow * GetPro(n - 1, words + 1);
        }
        if (lvl + 1 == opts_.num) {
            const Leave* leaf = static_cast<const Leave*>(down);
            return std::exp(-leaf->pro);
        }
        cur = static_cast<const Node*>(down);
        ++lvl;
    }
    return std::exp(-cur->pro);
}

void Constructor::Finalize() {
    FixDownPointers();
    { auto tmp = decltype(bigram_parents_)(); bigram_parents_.swap(tmp); }
    { auto tmp = decltype(leaf_parents_)(); leaf_parents_.swap(tmp); }
    AppendTails();
    Cut();
    Discount();
    for (int lvl = 1; lvl <= opts_.num; ++lvl) {
        std::cerr << "  D[" << lvl << "] = " << discounters_[lvl].D() << "\n";
    }
}

void Constructor::Prune(const std::vector<int>& reserves) {
    // reserves[0] = bigram reserve, reserves[1] = trigram reserve.
    // Unigrams are never pruned (full vocabulary is kept).
    prune_sizes_.assign(opts_.num + 1, 0);
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        prune_sizes_[lvl] = static_cast<int>(node_levels_[lvl].size());
    }
    prune_sizes_[opts_.num] = static_cast<int>(leaves_.size());

    auto reserve_for = [&](int lvl) -> int {
        // lvl=2 -> reserves[0], lvl=3 -> reserves[1]
        std::size_t slot = static_cast<std::size_t>(lvl - 2);
        if (slot >= reserves.size()) return 0;
        return reserves[slot];
    };

    prune_cutoffs_.assign(opts_.num + 1, 0);
    for (int lvl = 2; lvl <= opts_.num; ++lvl) {
        int remaining = prune_sizes_[lvl] - 1;
        prune_cutoffs_[lvl] = std::max(0, remaining - reserve_for(lvl));
    }

    // Order for a 3-gram IME model:
    //   1. Prune bigrams by PMI, cascade-deleting their trigram children.
    //      This escapes the `has_down` lock that Stolcke-pruned trigrams
    //      impose on bigrams, so universal-suffix bigrams like (X, 的) can
    //      actually be killed.
    //   2. If the cascade left more trigrams than the reserve allows, run
    //      the usual Stolcke pass on the survivors.
    if (opts_.num >= 2) {
        PruneBigramByPMI();
    }
    if (opts_.num >= 3 && prune_cutoffs_[opts_.num] > 0) {
        // Recompute probabilities after bigram PMI pruning so that the
        // Stolcke entropy scores for trigrams use up-to-date P(w|h) / γ(h).
        Discount();
        int remaining = prune_sizes_[opts_.num] - 1;
        int reserve = reserve_for(opts_.num);
        if (remaining > reserve) {
            prune_cutoffs_[opts_.num] = remaining - reserve;
            PruneLevel(opts_.num);
        }
    }
    // Entropy pruning removed entries; rerun Discount so gamma(h) and P_I(w|h)
    // are recomputed over the surviving set.
    Discount();
}


namespace {

struct DiskNode {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t down = 0;
    float bow = 0.0f;
    std::uint32_t bon = 0;
    std::uint32_t boe = 0;
};

struct DiskLeave {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t bon = 0;
    std::uint32_t boe = 0;
};

}  // namespace

void Constructor::GetBack(int length, const TokenID* seq,
                          std::uint32_t& boe, std::uint32_t& bon) const {
    boe = 0;
    bon = 0;
    if (length <= 1 || seq == nullptr) return;
    const TokenID* hw = seq;
    int n = length;
    while (n > 1) {
        --n;
        ++hw;
        // Find this suffix in the model
        const Node* cur = &node_levels_[0][0];
        bool found = true;
        int result_idx = 0;
        for (int i = 0; i < n; ++i) {
            const void* down = FindDown(i, cur, hw[i]);
            if (!down) { found = false; break; }
            if (i + 1 == opts_.num) {
                // leaf level — can't have children
                found = false;
                break;
            }
            cur = static_cast<const Node*>(down);
            result_idx = static_cast<int>(cur - node_levels_[i + 1].data());
        }
        if (found && n < opts_.num) {
            // Check if this node has children
            const Node& node = node_levels_[n][result_idx];
            const Node& next_node = node_levels_[n][result_idx + 1];
            if (next_node.down > node.down) {
                boe = static_cast<std::uint32_t>(n);
                bon = static_cast<std::uint32_t>(result_idx);
                return;
            }
        }
    }
}

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

    // Build parent indices for token sequence recovery
    std::vector<std::vector<int>> node_ups(static_cast<std::size_t>(order));
    for (int lvl = 1; lvl < order; ++lvl) {
        auto& ups = node_ups[static_cast<std::size_t>(lvl)];
        ups.resize(node_levels_[lvl].size());
        std::size_t up = 0;
        for (std::size_t idx = 0; idx + 1 < node_levels_[lvl].size(); ++idx) {
            while (up + 1 < node_levels_[lvl - 1].size() &&
                   node_levels_[lvl - 1][up + 1].down <= idx) {
                ++up;
            }
            ups[idx] = static_cast<int>(up);
        }
    }

    std::vector<int> leave_ups(leaves_.size());
    if (order > 0) {
        std::size_t up = 0;
        for (std::size_t idx = 0; idx + 1 < leaves_.size(); ++idx) {
            while (up + 1 < node_levels_[order - 1].size() &&
                   node_levels_[order - 1][up + 1].down <= idx) {
                ++up;
            }
            leave_ups[idx] = static_cast<int>(up);
        }
    }

    // Helper to get token sequence for a node
    auto get_tokens = [&](int level, int index, std::vector<TokenID>& tokens) {
        tokens.assign(static_cast<std::size_t>(level), TokenID{0});
        if (level == 0) return;
        if (level == order) {
            tokens.back() = leaves_[static_cast<std::size_t>(index)].id;
            int up_idx = leave_ups[static_cast<std::size_t>(index)];
            int cur_level = order - 1;
            for (int pos = level - 1; pos > 0; --pos) {
                tokens[static_cast<std::size_t>(pos - 1)] =
                    node_levels_[cur_level][static_cast<std::size_t>(up_idx)].id;
                if (pos - 1 == 0) break;
                up_idx = node_ups[static_cast<std::size_t>(cur_level)]
                             [static_cast<std::size_t>(up_idx)];
                --cur_level;
            }
            return;
        }
        int cur_idx = index;
        int cur_level = level;
        for (int pos = level; pos > 0; --pos) {
            tokens[static_cast<std::size_t>(pos - 1)] =
                node_levels_[cur_level][static_cast<std::size_t>(cur_idx)].id;
            if (pos - 1 == 0) break;
            cur_idx = node_ups[static_cast<std::size_t>(cur_level)]
                         [static_cast<std::size_t>(cur_idx)];
            --cur_level;
        }
    };

    // Write nodes with bon/boe
    std::vector<TokenID> history;
    for (int lvl = 0; lvl < order; ++lvl) {
        const auto& level = node_levels_[lvl];
        for (std::size_t i = 0; i < level.size(); ++i) {
            const auto& node = level[i];
            DiskNode raw;
            raw.id = node.id;
            raw.pro = static_cast<float>(node.pro);
            raw.down = node.down;
            raw.bow = static_cast<float>(node.bow);
            raw.bon = 0;
            raw.boe = 0;
            if (lvl > 0 && i + 1 < level.size()) {
                get_tokens(lvl, static_cast<int>(i), history);
                GetBack(lvl, history.data(), raw.boe, raw.bon);
            }
            out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
        }
    }

    // Write leaves with bon/boe
    for (std::size_t i = 0; i + 1 < leaves_.size(); ++i) {
        DiskLeave raw;
        raw.id = leaves_[i].id;
        raw.pro = static_cast<float>(leaves_[i].pro);
        get_tokens(order, static_cast<int>(i), history);
        GetBack(order, history.data(), raw.boe, raw.bon);
        out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
    }
    // Write sentinel leaf
    if (!leaves_.empty()) {
        DiskLeave raw;
        raw.id = leaves_.back().id;
        raw.pro = static_cast<float>(leaves_.back().pro);
        raw.bon = 0;
        raw.boe = 0;
        out.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
    }
}


namespace {

std::filesystem::path WithSuffix(const std::filesystem::path& base,
                                 const std::string& suffix) {
    return base.string() + suffix;
}

// Reads a sorted binary n-gram file with records (N x TokenID, Cnt).
// Invokes cb(token_array, cnt) for each record.
template <std::size_t N, typename Cb>
void ForEachNgram(const std::filesystem::path& path, Cb cb) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open: " + path.string());
    }
    std::array<TokenID, N> ids{};
    std::uint32_t cnt = 0;
    while (in.read(reinterpret_cast<char*>(ids.data()),
                   static_cast<std::streamsize>(N * sizeof(TokenID)))) {
        if (!in.read(reinterpret_cast<char*>(&cnt),
                     static_cast<std::streamsize>(sizeof(cnt)))) {
            break;
        }
        cb(ids, cnt);
    }
}

}  // namespace

void RunConstruct(ConstructOptions options) {
    auto input_base = options.input;
    auto output_path = options.output;
    int num = options.num;
    auto prune_reserves = std::move(options.prune_reserves);

    Constructor builder(std::move(options));

    if (num >= 1) {
        auto p = WithSuffix(input_base, ".1gram");
        std::cerr << "loading " << p.string() << "\n";
        ForEachNgram<1>(p,
            [&](const std::array<TokenID, 1>& ids, std::uint32_t cnt) {
                builder.InsertUnigram(ids[0], cnt);
            });
    }
    if (num >= 2) {
        auto p = WithSuffix(input_base, ".2gram");
        std::cerr << "loading " << p.string() << "\n";
        ForEachNgram<2>(p,
            [&](const std::array<TokenID, 2>& ids, std::uint32_t cnt) {
                builder.InsertBigram(ids[0], ids[1], cnt);
            });
    }
    if (num >= 3) {
        auto p = WithSuffix(input_base, ".3gram");
        std::cerr << "loading " << p.string() << "\n";
        ForEachNgram<3>(p,
            [&](const std::array<TokenID, 3>& ids, std::uint32_t cnt) {
                builder.InsertTrigram(ids[0], ids[1], ids[2], cnt);
            });
    }

    builder.Finalize();
    if (!prune_reserves.empty()) {
        builder.Prune(prune_reserves);
    }

    builder.Write(output_path);
}

} // namespace sime
