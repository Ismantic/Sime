#include "construct.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
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
    if (opts_.num < 1 || opts_.num > 3) {
        // See include/construct.h: several hot paths (InsertTrigram,
        // ComputeContinuationCounts, RunConstruct) are specialized for 1..3.
        throw std::invalid_argument("order must be in [1, 3]");
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
    // Only <s> is excluded from root's aggregate count. </s> is counted
    // normally; it appears as an n-gram suffix in the corpus.
    return i == SentenceStart;
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
    if (!IsBreaker(w)) {
        node_levels_[0][0].cnt += cnt;
    }
    // down set later in FixDownPointers.
    node_levels_[1].push_back(Node{w, 0, static_cast<float_t>(cnt), 0.0, 0.0, 0});
}

void Constructor::InsertBigram(TokenID u, TokenID v, std::uint32_t cnt) {
    if (opts_.num < 2) return;
    node_levels_[2].push_back(Node{v, 0, static_cast<float_t>(cnt), 0.0, 0.0, 0});
    bigram_parents_.push_back(u);
}

void Constructor::InsertTrigram(TokenID u, TokenID v, TokenID w, std::uint32_t cnt) {
    if (opts_.num < 3) return;
    if (cnt > cuts_[opts_.num]) {
        leaves_.push_back(Leave{w, static_cast<float_t>(cnt), 0.0, 0});
        leaf_parents_.push_back({u, v});
    } else if (cnt >= 1 && cnt <= 4) {
        // Filtered trigrams still count toward n-of-n for discount estimation.
        nt_[opts_.num][cnt] += 1;
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

void Constructor::CountCnt() {
    // Highest order: use raw trigram count (matches KenLM's AdjustCounts
    // highest-order path). Filtered trigrams contributed to nt_[opts_.num]
    // at Insert time; add kept leaves here, skipping the tail sentinel.
    const std::size_t leaves_end = leaves_.empty() ? 0 : leaves_.size() - 1;
    for (std::size_t i = 0; i < leaves_end; ++i) {
        auto c = static_cast<std::uint32_t>(leaves_[i].cnt);
        if (c >= 1 && c <= 4) {
            nt_[opts_.num][c] += 1;
        }
    }
    // Lower orders: MKN estimates D1/D2/D3+ from the distribution of
    // continuation counts N1+, not raw counts (KenLM AdjustCounts lines 76-88
    // push the adjusted count into stat.n[count]).
    for (int lvl = 1; lvl < opts_.num; ++lvl) {
        const auto& level = node_levels_[lvl];
        const std::size_t end = level.empty() ? 0 : level.size() - 1;
        for (std::size_t i = 0; i < end; ++i) {
            auto c = level[i].ctx;
            if (c >= 1 && c <= 4) {
                nt_[lvl][c] += 1;
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
        node_levels_[lvl].push_back(Node{TailMarker, down_count, 1.0, 0.0, 0.0, 0});
        node_levels_[lvl].back().pro = tail_pro;
    }
    leaves_.push_back(Leave{0, 1.0, tail_pro, 0});
}

template <typename Level>
int Constructor::CutLevel(NodeLevel& up_level, Level& current, int threshold,
                          bool protect_specials) {
    if (threshold <= 0) {
        return static_cast<int>(current.size());
    }
    int write = 0;
    auto up_it = up_level.begin();
    auto up_last = up_level.end();
    for (int idx = 0; idx < static_cast<int>(current.size()); ++idx) {
        bool keep = false;
        bool is_special = protect_specials &&
            (current[idx].id == SentenceStart ||
             current[idx].id == SentenceEnd ||
             current[idx].id == UnknownToken);
        if constexpr (std::is_same_v<Level, LeaveLevel>) {
            keep = is_special ||
                   (static_cast<int>(current[idx].cnt) > threshold) ||
                   (idx + 1 == static_cast<int>(current.size()));
        } else {
            keep = is_special ||
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
        // KenLM convention: never prune <s> / </s> / <unk> at unigram level.
        bool protect = (lvl == 1);
        if (lvl == opts_.num) {
            int new_size = CutLevel(up_level, leaves_, cuts_[lvl], protect);
            leaves_.resize(static_cast<std::size_t>(new_size));
        } else {
            auto& level = node_levels_[lvl];
            int new_size = CutLevel(up_level, level, cuts_[lvl], protect);
            level.resize(static_cast<std::size_t>(new_size));
        }
    }
}

template <typename DownLevel>
void Constructor::DiscountLevel(NodeLevel& level,
                                DownLevel& down_level,
                                NeyDiscounter& disc,
                                bool use_context,
                                int parent_lvl) {
    // Interpolated MKN. For each parent h at `parent_lvl`:
    //   gamma(h) = (D1*N1 + D2*N2 + D3+*N3+) / denom
    //   P_I(w | h) = max(count - D, 0) / denom + gamma(h) * P_I(w | h')
    // Stores gamma(h) into parent.bow (as -log) and P_I(w|h) into child.pro
    // (as -log). Expects lvl < parent_lvl to have been processed already so
    // GetPro can return P_I at shorter histories.

    constexpr float_t MinProb = 1e-12;
    constexpr float_t MaxProb = 1.0 - 1e-9;

    // Cursor into node_levels_[parent_lvl - 1] for recovering the ancestor's
    // id when parent_lvl >= 2 (needed to form the shorter-context query).
    std::size_t up_cursor = 0;

    for (std::size_t idx = 0; idx + 1 < level.size(); ++idx) {
        Node& node = level[idx];
        Node& next = level[idx + 1];

        // <s> as a history has no continuation-count predecessors, so its
        // children would all clamp to ~0 if we used ctx. Fall back to raw
        // counts for <s>'s subtree (standard KenLM <s>-bigram fix).
        bool lower_order = use_context;
        if (use_context && node.id == SentenceStart) {
            lower_order = false;
        }

        // Compute gamma's numerator and denominator from the kept children.
        std::uint64_t n1 = 0, n2 = 0, n3p = 0;
        float_t sum_children = 0.0;
        for (std::size_t i = node.down; i < next.down; ++i) {
            std::uint32_t c = lower_order
                ? down_level[i].ctx
                : static_cast<std::uint32_t>(down_level[i].cnt);
            sum_children += static_cast<float_t>(c);
            if (c == 1) ++n1;
            else if (c == 2) ++n2;
            else if (c >= 3) ++n3p;
        }

        // Pre-cut denominator: bigram.cnt for the highest order (equal to sum
        // of all trigram children before Cut/Prune), or parent.ctx_sum for
        // lower orders (snapshot taken in ComputeContinuationCounts before
        // Cut). Using a post-cut denominator here would make gamma shrink as
        // pruning removes children and break normalization.
        float_t pre_cut_denom = lower_order
            ? static_cast<float_t>(node.ctx_sum)
            : node.cnt;
        if (pre_cut_denom <= 0.0) {
            // Fallback: if we have no pre-cut record (e.g., ctx_sum=0 due to
            // no bigram children), fall back to sum of kept children.
            pre_cut_denom = sum_children;
        }
        pre_cut_denom = std::max(pre_cut_denom, 1.0);

        // Lost mass = pre-cut total - kept total. This is the probability
        // mass that Cut + entropy-pruning removed; KenLM rolls it into the
        // gamma numerator so Sigma P_I(w|h) + gamma(h) still sums to 1.
        float_t lost_mass = pre_cut_denom - sum_children;
        if (lost_mass < 0.0) lost_mass = 0.0;

        float_t gamma;
        if (node.down == next.down) {
            // No children at all; all probability routes to backoff.
            gamma = 1.0;
        } else {
            float_t gamma_num = disc.D1() * static_cast<float_t>(n1) +
                                disc.D2() * static_cast<float_t>(n2) +
                                disc.D3() * static_cast<float_t>(n3p) +
                                lost_mass;  // <-- normalizer term
            gamma = gamma_num / pre_cut_denom;
        }
        gamma = std::clamp(gamma, MinProb, MaxProb);
        node.bow = static_cast<float>(-std::log(gamma));

        // Recover the ancestor id (first token of parent's context) so we
        // can build the shorter-history query. For parent_lvl <= 1 there's
        // nothing to recover.
        TokenID ancestor_id = 0;
        if (parent_lvl >= 2) {
            auto& up = node_levels_[parent_lvl - 1];
            while (up_cursor + 1 < up.size() &&
                   up[up_cursor + 1].down <= idx) {
                ++up_cursor;
            }
            ancestor_id = up[up_cursor].id;
        }

        for (std::size_t down_idx = node.down; down_idx < next.down; ++down_idx) {
            std::uint32_t c = lower_order
                ? down_level[down_idx].ctx
                : static_cast<std::uint32_t>(down_level[down_idx].cnt);
            float_t discounted = disc.Discount(static_cast<float_t>(c));
            if (discounted < 0.0) discounted = 0.0;
            float_t alpha = discounted / pre_cut_denom;

            TokenID w = down_level[down_idx].id;

            // P_I(w | shorter(h)). For parent_lvl = 0 the shorter history is
            // empty and we use the uniform prior stored in root.pro.
            float_t p_lower;
            if (parent_lvl == 0) {
                p_lower = std::exp(-node_levels_[0][0].pro);
            } else if (parent_lvl == 1) {
                // Shorter history is empty: want P_I(w).
                p_lower = GetPro(1, &w);
            } else {
                // parent_lvl == 2: shorter history is [node.id], query
                // P_I(w | node.id).
                std::array<TokenID, 2> q{node.id, w};
                p_lower = GetPro(2, q.data());
            }

            float_t p_interp = alpha + gamma * p_lower;
            p_interp = std::clamp(p_interp, MinProb, MaxProb);
            down_level[down_idx].pro = static_cast<float>(-std::log(p_interp));
        }

        (void)ancestor_id;  // reserved for higher orders once supported
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
        // KenLM convention (adjust_counts.cc:227): n-grams whose leftmost
        // token is a special (<s>/<unk>/</s>) are exempt from pruning.
        // For higher orders this is crucial for (<s>, *) and (<s>, <s>, *):
        // their raw-count denominator is the whole sentence count, so alpha
        // is tiny and CalcScore undervalues them, otherwise nearly all
        // sentence-initial trigrams get pruned.
        bool protect = false;
        if (level >= 1) {
            TokenID first = words[1];
            protect = (first == SentenceStart ||
                       first == SentenceEnd ||
                       first == UnknownToken);
        }
        float_t dist = has_down ? 0.0 : CalcScore(level, indices, words);
        candidates.push_back(
            NodeScore{dist, static_cast<std::uint32_t>(idx), has_down, protect});
    }
    std::sort(candidates.begin(), candidates.end());
    int cuts = std::min(prune_cutoffs_[level], static_cast<int>(candidates.size()));
    float_t mark = 0.0;
    for (int i = 0; i < cuts; ++i) {
        if (candidates[i].has_down) continue;
        if (candidates[i].protect) continue;
        // Also protect specials at unigram level regardless of first-token
        // rule above (e.g., an <unk> unigram's words[1] is <unk> already, so
        // the above catches it; this is kept as belt-and-suspenders).
        if (level == 1) {
            TokenID id = node_levels_[1][candidates[i].index].id;
            if (id == SentenceStart || id == SentenceEnd || id == UnknownToken) {
                continue;
            }
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
    // With independent counting, l2 contains every bigram in the corpus
    // (not just trigram prefixes), so N1+(., w) is now correct for all
    // tokens including </s>.
    auto finalize_ctx_sum = [&]() {
        // Pre-cut ctx_sum for each parent at levels 0..num-2. Runs BEFORE Cut
        // so the sum reflects every child including those about to be pruned.
        // Used by DiscountLevel's gamma normalizer to recover mass lost to
        // pruning.
        for (int lvl = 0; lvl < opts_.num - 1; ++lvl) {
            auto& parents = node_levels_[lvl];
            auto& children = node_levels_[lvl + 1];
            for (std::size_t idx = 0; idx + 1 < parents.size(); ++idx) {
                std::uint64_t sum = 0;
                std::size_t begin = parents[idx].down;
                std::size_t end = parents[idx + 1].down;
                for (std::size_t i = begin; i < end; ++i) {
                    sum += children[i].ctx;
                }
                parents[idx].ctx_sum = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(sum, std::numeric_limits<std::uint32_t>::max()));
            }
        }
    };

    if (opts_.num == 2) {
        auto& unigram_level = node_levels_[1];
        std::unordered_map<std::uint32_t, std::uint32_t> ctx_map;
        for (std::size_t j = 0; j + 1 < node_levels_[2].size(); ++j) {
            ctx_map[node_levels_[2][j].id]++;
        }
        for (auto& node : unigram_level) {
            auto it = ctx_map.find(node.id);
            if (it != ctx_map.end()) {
                node.ctx = it->second;
            }
        }
        finalize_ctx_sum();
        return;
    }

    if (opts_.num < 3) { finalize_ctx_sum(); return; }

    auto& l1 = node_levels_[1];
    auto& l2 = node_levels_[2];

    // Bigram continuation count N1+(., v, w) = distinct u such that trigram
    // (u, v, w) exists.
    std::unordered_map<std::uint64_t, std::uint32_t> bigram_ctx;
    for (std::size_t u_idx = 0; u_idx + 1 < l1.size(); ++u_idx) {
        for (std::size_t v_idx = l1[u_idx].down;
             v_idx + 1 < l2.size() && v_idx < l1[u_idx + 1].down; ++v_idx) {
            TokenID v_id = l2[v_idx].id;
            for (std::size_t w_idx = l2[v_idx].down; w_idx < l2[v_idx + 1].down; ++w_idx) {
                TokenID w_id = leaves_[w_idx].id;
                auto key = (static_cast<std::uint64_t>(v_id) << 32) | w_id;
                bigram_ctx[key]++;
            }
        }
    }
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

    // Unigram continuation count N1+(., w) = distinct v such that bigram
    // (v, w) exists. l2 has every bigram, so just count l2 entries by .id.
    std::unordered_map<std::uint32_t, std::uint32_t> unigram_ctx;
    for (std::size_t j = 0; j + 1 < l2.size(); ++j) {
        unigram_ctx[l2[j].id]++;
    }
    for (auto& node : l1) {
        auto it = unigram_ctx.find(node.id);
        if (it != unigram_ctx.end()) {
            node.ctx = it->second;
        }
    }

    finalize_ctx_sum();
}

void Constructor::Discount() {
    for (int lvl = opts_.num; lvl >= 1; --lvl) {
        discounters_[lvl].Init(nt_[lvl][1], nt_[lvl][2], nt_[lvl][3], nt_[lvl][4]);
    }

    // Set root uniform *before* running DiscountLevel at lvl=0: GetPro falls
    // back to exp(-root.pro) for n=0, and lvl=0 interpolation reads it.
    Node& root = node_levels_[0][0];
    std::uint32_t effective_vocab = (opts_.token_count > 1)
        ? opts_.token_count - 1 : 1;
    float_t base = 1.0 / effective_vocab;
    root.pro = static_cast<float>(-std::log(base));

    // Forward iteration. At each parent level k, compute gamma(h) into the
    // parent's bow and P_I(w|h) into each child's pro; this requires lower
    // orders (0..k-1) to be already written so GetPro returns the full
    // interpolated probability at the shorter context.
    for (int lvl = 0; lvl < opts_.num; ++lvl) {
        auto& level = node_levels_[lvl];
        bool use_context = lvl < opts_.num - 1;
        if (lvl == opts_.num - 1) {
            DiscountLevel(level, leaves_, discounters_[lvl + 1], false, lvl);
        } else {
            DiscountLevel(level, node_levels_[lvl + 1], discounters_[lvl + 1],
                          use_context, lvl);
        }
    }

    // <unk>'s natural P_I = gamma(root)/V is what KenLM effectively produces
    // (prob=0, gamma=sums.gamma, interpolated to gamma/V). We already
    // zeroed its count and ctx in Finalize so alpha(<unk>) = 0, meaning the
    // computed P_I at lvl=0 is exactly gamma(root) * 1/V. No override here.
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
        TokenID tid = down_level[idx].id;
        float_t pro = std::exp(-down_level[idx].pro);
        sum_down += pro;
        words[level + 1] = tid;
        sum_backoff += GetPro(level, words + 2);
    }
    if (sum_down >= 1.0 || sum_backoff >= 1.0) {
        constexpr float_t BowEpsilon = 0.0001;
        float_t bow = std::max(sum_down, sum_backoff) + BowEpsilon;
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
    // Returns the LM probability P(words[n-1] | words[0..n-2]) using the
    // currently stored `pro` (as -log P) and `bow` (as -log gamma or -log
    // Katz BOW, depending on caller's semantics). The recursion is the same
    // for both interpolated and backoff MKN (KenLM trick): descend the Trie
    // through the history; on a miss, multiply by the deepest-reached node's
    // bow and retry with a shorter history.
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
    AppendTails();
    // ctx first so CountCnt can estimate low-order discount params from
    // continuation counts (see KenLM AdjustCounts).
    ComputeContinuationCounts();
    // Zero <unk> early -- its count shouldn't skew discount param estimation,
    // and we want alpha(<unk>) = 0 so the natural P_I(<unk>) = gamma(root)/V
    // comes out right at lvl=0.
    for (auto& node : node_levels_[1]) {
        if (node.id == UnknownToken) {
            node.cnt = 0.0;
            node.ctx = 0;
            break;
        }
    }
    CountCnt();
    Cut();
    // Discount now produces interpolated MKN: stores P_I in pro and MKN gamma
    // in bow. CalcBow/CalcNodeBow are no longer part of the pipeline (they
    // computed Katz BOWs for the old backoff variant).
    Discount();
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
    // Entropy pruning removed entries; rerun Discount so gamma(h) and P_I(w|h)
    // are recomputed over the surviving set (nt_ stays as pre-prune stats,
    // which is fine -- D1/D2/D3+ don't need to be re-estimated).
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
