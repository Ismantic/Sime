#include "score.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <utility>

namespace sime {

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

} // namespace

bool Scorer::Load(const std::filesystem::path& path) {
    Reset();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    if (!in.read(reinterpret_cast<char*>(&num_), sizeof(num_))) {
        Reset();
        return false;
    }
    if (num_ <= 0) {
        Reset();
        return false;
    }

    sizes_.assign(static_cast<std::size_t>(num_) + 1, 0);
    if (!in.read(reinterpret_cast<char*>(sizes_.data()),
                 static_cast<std::streamsize>(sizes_.size() * sizeof(int)))) {
        Reset();
        return false;
    }

    node_levels_.resize(static_cast<std::size_t>(num_));
    for (int lvl = 0; lvl < num_; ++lvl) {
        int size = sizes_[static_cast<std::size_t>(lvl)];
        if (size <= 0) { Reset(); return false; }
        auto& nodes = node_levels_[static_cast<std::size_t>(lvl)];
        nodes.resize(static_cast<std::size_t>(size));
        for (int idx = 0; idx < size; ++idx) {
            DiskNode dn;
            if (!in.read(reinterpret_cast<char*>(&dn), sizeof(dn))) {
                Reset();
                return false;
            }
            nodes[static_cast<std::size_t>(idx)] = {
                dn.id, dn.down, dn.pro, dn.bow, dn.bon, dn.boe};
        }
    }

    int leave_size = sizes_.back();
    if (leave_size <= 0) { Reset(); return false; }
    leave_level_.resize(static_cast<std::size_t>(leave_size));
    for (int idx = 0; idx < leave_size; ++idx) {
        DiskLeave dl;
        if (!in.read(reinterpret_cast<char*>(&dl), sizeof(dl))) {
            Reset();
            return false;
        }
        leave_level_[static_cast<std::size_t>(idx)] = {
            dl.id, dl.pro, dl.bon, dl.boe};
    }

    return true;
}

void Scorer::Reset() {
    num_ = 0;
    sizes_.clear();
    node_levels_.clear();
    leave_level_.clear();
}

void Scorer::Back(Pos& pos) const {
    if (pos.level >= static_cast<std::uint32_t>(num_)) {
        if (pos.index < leave_level_.size()) {
            const auto& e = leave_level_[pos.index];
            pos.level = e.boe;
            pos.index = e.bon;
        }
        return;
    }
    const auto& nodes = node_levels_[pos.level];
    if (pos.index + 1 >= nodes.size()) {
        return;
    }
    const auto& node = nodes[pos.index];
    const auto& next = nodes[pos.index + 1];
    if (node.down == next.down) {
        pos.level = node.boe;
        pos.index = node.bon;
    }
}

float_t Scorer::ScoreMove(Pos s, TokenID w, Pos& r) const {
    std::uint32_t level = s.level;
    std::uint32_t index = s.index;

    float_t cost = 0.0;
    if (w == ScoreNotToken) {
        r = Pos{};
        return cost;
    }

    while (level < static_cast<std::uint32_t>(num_)) {
        const auto& nodes = node_levels_[level];
        std::size_t node_index = (level == 0) ? 0 : index;
        if (node_index + 1 >= nodes.size()) {
            break;
        }
        const auto& node = nodes[node_index];
        const auto& next = nodes[node_index + 1];
        auto begin = node.down;
        auto end = next.down;
        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            auto down_idx = GetLeave(begin, end, w);
            if (down_idx != end) {
                r.level = static_cast<std::uint32_t>(num_);
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + leave_level_[down_idx].pro;
            }
        } else {
            auto down_idx = GetNode(static_cast<int>(level + 1), begin, end, w);
            if (down_idx != end) {
                r.level = level + 1;
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + node_levels_[level + 1][down_idx].pro;
            }
        }

        cost += node.bow;
        if (level == 0) {
            break;
        }
        level = node.boe;
        index = node.bon;
    }

    r = Pos{};
    return cost + node_levels_[0][0].pro;
}

float_t Scorer::UnknownPenalty() const {
    if (node_levels_.empty() || node_levels_[0].empty()) {
        constexpr float_t DefaultUnknownPenalty = -20.0;
        return DefaultUnknownPenalty;
    }
    return node_levels_[0][0].pro;
}

std::vector<std::pair<TokenID, float_t>> Scorer::NextTokens(
    Pos& context, std::size_t num) const {
    std::vector<std::pair<TokenID, float_t>> result;
    if (num_ < 2) return result;

    Pos ctx = context;
    while (true) {
        if (ctx.level == 0) return result;

        if (ctx.level >= static_cast<std::uint32_t>(num_)) {
            Back(ctx);
            if (ctx.level >= static_cast<std::uint32_t>(num_)) return result;
            continue;
        }

        const auto& nodes = node_levels_[ctx.level];
        std::size_t node_index = ctx.index;
        if (node_index + 1 >= nodes.size()) return result;
        auto begin = nodes[node_index].down;
        auto end = nodes[node_index + 1].down;
        if (begin < end) break;

        Pos backed = ctx;
        Back(backed);
        if (backed.level == ctx.level && backed.index == ctx.index) {
            return result;
        }
        ctx = backed;
    }

    auto collect = [&](std::uint32_t level, std::uint32_t index,
                       std::vector<std::pair<TokenID, float_t>>& out) {
        if (level >= static_cast<std::uint32_t>(num_)) return;
        const auto& nodes = node_levels_[level];
        std::size_t ni = index;
        if (ni + 1 >= nodes.size()) return;
        auto begin = nodes[ni].down;
        auto end = nodes[ni + 1].down;

        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            for (auto i = begin; i < end && i < leave_level_.size(); ++i) {
                out.emplace_back(leave_level_[i].token, leave_level_[i].pro);
            }
        } else {
            const auto& children = node_levels_[level + 1];
            for (auto i = begin; i < end && i < children.size(); ++i) {
                out.emplace_back(children[i].token, children[i].pro);
            }
        }
    };

    collect(ctx.level, ctx.index, result);
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if (result.size() < num && ctx.level > 1) {
        const auto& node = node_levels_[ctx.level][ctx.index];
        Pos backed{node.boe, node.bon};
        if (backed.level >= 1) {
            std::unordered_set<TokenID> seen;
            for (const auto& [tid, _] : result) seen.insert(tid);

            std::vector<std::pair<TokenID, float_t>> backoff;
            collect(backed.level, backed.index, backoff);
            std::sort(backoff.begin(), backoff.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });
            for (const auto& entry : backoff) {
                if (result.size() >= num) break;
                if (seen.insert(entry.first).second) {
                    result.push_back(entry);
                }
            }
        }
    }

    context = ctx;
    if (result.size() > num) result.resize(num);
    return result;
}

std::vector<Scorer::NGram> Scorer::DumpLevel(int level) const {
    std::vector<NGram> results;
    if (level < 1 || level > num_) return results;

    if (level == 1) {
        if (num_ < 2 || node_levels_.size() < 2) return results;
        const auto& root = node_levels_[0];
        const auto& unigrams = node_levels_[1];
        auto begin = root[0].down;
        auto end = (root.size() > 1) ? root[1].down : unigrams.size();
        for (auto i = begin; i < end && i < unigrams.size(); ++i) {
            NGram ng;
            ng.tokens.push_back(unigrams[i].token);
            ng.pro = unigrams[i].pro;
            results.push_back(std::move(ng));
        }
    } else if (level == 2) {
        if (num_ < 2 || node_levels_.size() < 2) return results;
        const auto& unigrams = node_levels_[1];
        if (num_ == 2) {
            for (std::size_t i = 0; i + 1 < unigrams.size(); ++i) {
                auto begin = unigrams[i].down;
                auto end = unigrams[i + 1].down;
                for (auto j = begin; j < end && j < leave_level_.size(); ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams[i].token);
                    ng.tokens.push_back(leave_level_[j].token);
                    ng.pro = leave_level_[j].pro;
                    results.push_back(std::move(ng));
                }
            }
        } else {
            const auto& bigrams = node_levels_[2];
            for (std::size_t i = 0; i + 1 < unigrams.size(); ++i) {
                auto begin = unigrams[i].down;
                auto end = unigrams[i + 1].down;
                for (auto j = begin; j < end && j + 1 < bigrams.size(); ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams[i].token);
                    ng.tokens.push_back(bigrams[j].token);
                    ng.pro = bigrams[j].pro;
                    results.push_back(std::move(ng));
                }
            }
        }
    } else if (level == 3 && num_ >= 3) {
        const auto& unigrams = node_levels_[1];
        const auto& bigrams = node_levels_[2];
        for (std::size_t i = 0; i + 1 < unigrams.size(); ++i) {
            auto bi_begin = unigrams[i].down;
            auto bi_end = unigrams[i + 1].down;
            for (auto j = bi_begin; j < bi_end && j + 1 < bigrams.size(); ++j) {
                auto leaf_begin = bigrams[j].down;
                auto leaf_end = bigrams[j + 1].down;
                for (auto k = leaf_begin; k < leaf_end && k < leave_level_.size(); ++k) {
                    NGram ng;
                    ng.tokens.push_back(unigrams[i].token);
                    ng.tokens.push_back(bigrams[j].token);
                    ng.tokens.push_back(leave_level_[k].token);
                    ng.pro = leave_level_[k].pro;
                    results.push_back(std::move(ng));
                }
            }
        }
    }

    return results;
}

std::size_t Scorer::GetNode(int level,
                            std::size_t begin,
                            std::size_t end,
                            TokenID w) const {
    const auto& nodes = node_levels_[static_cast<std::size_t>(level)];
    auto first = nodes.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = nodes.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w,
        [](const NodeEntry& node, TokenID token) {
            return node.token < token;
        });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(nodes.begin(), it));
}

std::size_t Scorer::GetLeave(std::size_t begin,
                             std::size_t end,
                             TokenID w) const {
    auto first = leave_level_.begin() + static_cast<std::ptrdiff_t>(begin);
    auto last = leave_level_.begin() + static_cast<std::ptrdiff_t>(end);
    auto it = std::lower_bound(first, last, w,
        [](const LeaveEntry& leaf, TokenID token) {
            return leaf.token < token;
        });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(std::distance(leave_level_.begin(), it));
}

} // namespace sime
