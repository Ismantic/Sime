#include "score.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

namespace sime {

bool Scorer::Load(const std::filesystem::path& path) {
    Reset();

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    mmap_len_ = static_cast<std::size_t>(st.st_size);
    if (mmap_len_ == 0) { close(fd); return false; }

    mmap_addr_ = mmap(nullptr, mmap_len_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mmap_addr_ == MAP_FAILED) {
        mmap_addr_ = nullptr;
        mmap_len_ = 0;
        return false;
    }

    const char* p = static_cast<const char*>(mmap_addr_);
    const char* end = p + mmap_len_;

    // Read header: num
    if (p + sizeof(int) > end) { Reset(); return false; }
    std::memcpy(&num_, p, sizeof(int));
    p += sizeof(int);
    if (num_ <= 0) { Reset(); return false; }

    // Read sizes
    std::size_t sizes_bytes = static_cast<std::size_t>(num_ + 1) * sizeof(int);
    if (p + sizes_bytes > end) { Reset(); return false; }
    sizes_.assign(num_ + 1, 0);
    std::memcpy(sizes_.data(), p, sizes_bytes);
    p += sizes_bytes;

    // Map node levels
    node_levels_.resize(static_cast<std::size_t>(num_));
    for (int lvl = 0; lvl < num_; ++lvl) {
        int size = sizes_[static_cast<std::size_t>(lvl)];
        if (size <= 0) { Reset(); return false; }
        std::size_t bytes = static_cast<std::size_t>(size) * sizeof(NodeEntry);
        if (p + bytes > end) { Reset(); return false; }
        node_levels_[static_cast<std::size_t>(lvl)] = {
            reinterpret_cast<const NodeEntry*>(p),
            static_cast<std::size_t>(size)};
        p += bytes;
    }

    // Map leaves
    int leave_size = sizes_.back();
    if (leave_size <= 0) { Reset(); return false; }
    std::size_t leave_bytes = static_cast<std::size_t>(leave_size) * sizeof(LeaveEntry);
    if (p + leave_bytes > end) { Reset(); return false; }
    leave_data_ = reinterpret_cast<const LeaveEntry*>(p);
    leave_size_ = static_cast<std::size_t>(leave_size);

    return true;
}

void Scorer::Reset() {
    if (mmap_addr_ && mmap_addr_ != MAP_FAILED) {
        munmap(mmap_addr_, mmap_len_);
    }
    mmap_addr_ = nullptr;
    mmap_len_ = 0;
    num_ = 0;
    sizes_.clear();
    node_levels_.clear();
    leave_data_ = nullptr;
    leave_size_ = 0;
}

void Scorer::Back(Pos& pos) const {
    if (pos.level >= static_cast<std::uint32_t>(num_)) {
        if (pos.index < leave_size_) {
            const auto& e = leave_data_[pos.index];
            pos.level = e.boe;
            pos.index = e.bon;
        }
        return;
    }
    const auto& lv = node_levels_[pos.level];
    if (pos.index + 1 >= lv.size) {
        return;
    }
    const auto& node = lv.data[pos.index];
    const auto& next = lv.data[pos.index + 1];
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
        const auto& lv = node_levels_[level];
        std::size_t node_index = (level == 0) ? 0 : index;
        if (node_index + 1 >= lv.size) {
            break;
        }
        const auto& node = lv.data[node_index];
        const auto& next = lv.data[node_index + 1];
        auto begin = node.down;
        auto end = next.down;
        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            auto down_idx = GetLeave(begin, end, w);
            if (down_idx != end) {
                r.level = static_cast<std::uint32_t>(num_);
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + leave_data_[down_idx].pro;
            }
        } else {
            auto down_idx = GetNode(static_cast<int>(level + 1), begin, end, w);
            if (down_idx != end) {
                r.level = level + 1;
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + node_levels_[level + 1].data[down_idx].pro;
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
    // Unknown token: use a fixed high penalty instead of <unk>'s
    // inflated unigram score (which accumulates all OOV occurrences).
    constexpr float_t UnknownCost = 20.0;
    return cost + UnknownCost;
}

float_t Scorer::UnknownPenalty() const {
    if (node_levels_.empty() || node_levels_[0].size == 0) {
        constexpr float_t DefaultUnknownPenalty = -20.0;
        return DefaultUnknownPenalty;
    }
    return node_levels_[0].data[0].pro;
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

        const auto& lv = node_levels_[ctx.level];
        std::size_t node_index = ctx.index;
        if (node_index + 1 >= lv.size) return result;
        auto begin = lv.data[node_index].down;
        auto end = lv.data[node_index + 1].down;
        if (begin < end) break;

        Pos backed = ctx;
        Back(backed);
        if (backed.level == ctx.level && backed.index == ctx.index) {
            return result;
        }
        ctx = backed;
    }

    // Zero-probability threshold: root.pro is the unknown penalty,
    // entries with this score have no real probability.
    const float zero_pro = node_levels_[0].data[0].pro;

    auto collect = [&](std::uint32_t level, std::uint32_t index,
                       std::vector<std::pair<TokenID, float_t>>& out) {
        if (level >= static_cast<std::uint32_t>(num_)) return;
        const auto& lv = node_levels_[level];
        std::size_t ni = index;
        if (ni + 1 >= lv.size) return;
        auto begin = lv.data[ni].down;
        auto end = lv.data[ni + 1].down;

        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            for (auto i = begin; i < end && i < leave_size_; ++i) {
                if (leave_data_[i].pro >= zero_pro) continue;
                out.emplace_back(leave_data_[i].token, leave_data_[i].pro);
            }
        } else {
            const auto& children = node_levels_[level + 1];
            for (auto i = begin; i < end && i < children.size; ++i) {
                if (children.data[i].pro >= zero_pro) continue;
                out.emplace_back(children.data[i].token, children.data[i].pro);
            }
        }
    };

    collect(ctx.level, ctx.index, result);
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if (result.size() < num && ctx.level > 1) {
        const auto& node = node_levels_[ctx.level].data[ctx.index];
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
        auto begin = root.data[0].down;
        auto end = (root.size > 1) ? root.data[1].down
                                   : static_cast<std::uint32_t>(unigrams.size);
        for (auto i = begin; i < end && i < unigrams.size; ++i) {
            NGram ng;
            ng.tokens.push_back(unigrams.data[i].token);
            ng.pro = unigrams.data[i].pro;
            results.push_back(std::move(ng));
        }
    } else if (level == 2) {
        if (num_ < 2 || node_levels_.size() < 2) return results;
        const auto& unigrams = node_levels_[1];
        if (num_ == 2) {
            for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
                auto begin = unigrams.data[i].down;
                auto end = unigrams.data[i + 1].down;
                for (auto j = begin; j < end && j < leave_size_; ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.data[i].token);
                    ng.tokens.push_back(leave_data_[j].token);
                    ng.pro = leave_data_[j].pro;
                    results.push_back(std::move(ng));
                }
            }
        } else {
            const auto& bigrams = node_levels_[2];
            for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
                auto begin = unigrams.data[i].down;
                auto end = unigrams.data[i + 1].down;
                for (auto j = begin; j < end && j + 1 < bigrams.size; ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.data[i].token);
                    ng.tokens.push_back(bigrams.data[j].token);
                    ng.pro = bigrams.data[j].pro;
                    results.push_back(std::move(ng));
                }
            }
        }
    } else if (level == 3 && num_ >= 3) {
        const auto& unigrams = node_levels_[1];
        const auto& bigrams = node_levels_[2];
        for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
            auto bi_begin = unigrams.data[i].down;
            auto bi_end = unigrams.data[i + 1].down;
            for (auto j = bi_begin; j < bi_end && j + 1 < bigrams.size; ++j) {
                auto leaf_begin = bigrams.data[j].down;
                auto leaf_end = bigrams.data[j + 1].down;
                for (auto k = leaf_begin; k < leaf_end && k < leave_size_; ++k) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.data[i].token);
                    ng.tokens.push_back(bigrams.data[j].token);
                    ng.tokens.push_back(leave_data_[k].token);
                    ng.pro = leave_data_[k].pro;
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
    const auto& lv = node_levels_[static_cast<std::size_t>(level)];
    auto first = lv.data + begin;
    auto last = lv.data + end;
    auto it = std::lower_bound(first, last, w,
        [](const NodeEntry& node, TokenID token) {
            return node.token < token;
        });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(it - lv.data);
}

std::size_t Scorer::GetLeave(std::size_t begin,
                             std::size_t end,
                             TokenID w) const {
    auto first = leave_data_ + begin;
    auto last = leave_data_ + end;
    auto it = std::lower_bound(first, last, w,
        [](const LeaveEntry& leaf, TokenID token) {
            return leaf.token < token;
        });
    if (it == last || it->token != w) {
        return end;
    }
    return static_cast<std::size_t>(it - leave_data_);
}

} // namespace sime
