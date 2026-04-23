#include "score.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

namespace sime {

namespace {

constexpr std::uint32_t CompactMagic = 0x51434D53;  // "SMCQ"

// Align pointer forward to 4-byte boundary.
const char* Align4(const char* p) {
    auto addr = reinterpret_cast<std::uintptr_t>(p);
    auto rem = addr % 4;
    return (rem == 0) ? p : p + (4 - rem);
}

} // namespace

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

    // Check magic.
    if (p + sizeof(std::uint32_t) > end) { Reset(); return false; }
    std::uint32_t magic = 0;
    std::memcpy(&magic, p, sizeof(magic));
    if (magic != CompactMagic) { Reset(); return false; }
    p += sizeof(std::uint32_t);

    // Read header: num.
    if (p + sizeof(int) > end) { Reset(); return false; }
    std::memcpy(&num_, p, sizeof(int));
    p += sizeof(int);
    if (num_ <= 0) { Reset(); return false; }

    // Read sizes.
    std::size_t sizes_bytes = static_cast<std::size_t>(num_ + 1) * sizeof(int);
    if (p + sizes_bytes > end) { Reset(); return false; }
    sizes_.assign(num_ + 1, 0);
    std::memcpy(sizes_.data(), p, sizes_bytes);
    p += sizes_bytes;

    // Read quantization tables: per-level (pro16 + bow8), then leaf pro16.
    constexpr std::size_t ProTableBytes = 65536 * sizeof(float);
    constexpr std::size_t BowTableBytes = 256 * sizeof(float);
    qt_pro_.resize(static_cast<std::size_t>(num_));
    qt_bow_.resize(static_cast<std::size_t>(num_));
    for (int lvl = 0; lvl < num_; ++lvl) {
        if (p + ProTableBytes + BowTableBytes > end) { Reset(); return false; }
        qt_pro_[static_cast<std::size_t>(lvl)].resize(65536);
        std::memcpy(qt_pro_[static_cast<std::size_t>(lvl)].data(), p, ProTableBytes);
        p += ProTableBytes;
        qt_bow_[static_cast<std::size_t>(lvl)].resize(256);
        std::memcpy(qt_bow_[static_cast<std::size_t>(lvl)].data(), p, BowTableBytes);
        p += BowTableBytes;
    }
    if (p + ProTableBytes > end) { Reset(); return false; }
    qt_leaf_pro_.resize(65536);
    std::memcpy(qt_leaf_pro_.data(), p, ProTableBytes);
    p += ProTableBytes;

    // Map SoA node levels: tokens | pro_q (pad4) | bow_q (pad4) | down.
    node_levels_.resize(static_cast<std::size_t>(num_));
    for (int lvl = 0; lvl < num_; ++lvl) {
        auto size = static_cast<std::size_t>(sizes_[static_cast<std::size_t>(lvl)]);
        if (size == 0) { Reset(); return false; }

        std::size_t tok_bytes = size * sizeof(TokenID);
        std::size_t pro_bytes = size * sizeof(std::uint16_t);
        std::size_t bow_bytes = size;  // uint8_t per entry
        std::size_t down_bytes = size * sizeof(std::uint32_t);

        auto& lv = node_levels_[static_cast<std::size_t>(lvl)];
        lv.tokens = reinterpret_cast<const TokenID*>(p);
        p += tok_bytes;
        lv.pro_q = reinterpret_cast<const std::uint16_t*>(p);
        p += pro_bytes;
        p = Align4(p);
        lv.bow_q = reinterpret_cast<const std::uint8_t*>(p);
        p += bow_bytes;
        p = Align4(p);
        lv.down = reinterpret_cast<const std::uint32_t*>(p);
        p += down_bytes;
        lv.size = size;
    }

    // Map SoA leaves: tokens | pro_q16.
    auto leave_sz = static_cast<std::size_t>(sizes_.back());
    if (leave_sz == 0) { Reset(); return false; }
    std::size_t leaf_tok_bytes = leave_sz * sizeof(TokenID);
    std::size_t leaf_pro_bytes = leave_sz * sizeof(std::uint16_t);
    if (p + leaf_tok_bytes + leaf_pro_bytes > end) { Reset(); return false; }
    leaf_tokens_ = reinterpret_cast<const TokenID*>(p);
    p += leaf_tok_bytes;
    leaf_pro_q_ = reinterpret_cast<const std::uint16_t*>(p);
    leave_size_ = leave_sz;

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
    leaf_tokens_ = nullptr;
    leaf_pro_q_ = nullptr;
    leave_size_ = 0;
    qt_pro_.clear();
    qt_bow_.clear();
    qt_leaf_pro_.clear();
}

Scorer::Pos Scorer::StartPos() const {
    return Pos{};
}

std::size_t Scorer::FindNode(int level, TokenID token) const {
    if (level < 1 || level >= num_) return SIZE_MAX;
    const auto& lv = node_levels_[static_cast<std::size_t>(level)];
    std::size_t lo = 0;
    std::size_t hi = (lv.size > 0) ? lv.size - 1 : 0;
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        if (lv.tokens[mid] < token) lo = mid + 1;
        else hi = mid;
    }
    if (lo < lv.size - 1 && lv.tokens[lo] == token) return lo;
    return SIZE_MAX;
}

void Scorer::Back(Pos& pos) const {
    if (pos.level <= 0) return;

    if (pos.level >= static_cast<std::uint32_t>(num_)) {
        if (pos.level != static_cast<std::uint32_t>(num_)) {
            pos = Pos{};
            return;
        }
        TokenID w = leaf_tokens_[pos.index];

        if (num_ >= 3) {
            // Find parent at level (num_-1) to get token v.
            const auto& parent_lv = node_levels_[static_cast<std::size_t>(num_ - 1)];
            std::size_t lo = 0;
            std::size_t hi = (parent_lv.size > 1) ? parent_lv.size - 1 : 0;
            while (lo + 1 < hi) {
                std::size_t mid = lo + (hi - lo) / 2;
                if (parent_lv.down[mid] <= pos.index) lo = mid;
                else hi = mid;
            }
            TokenID v = parent_lv.tokens[lo];
            auto v_idx = FindNode(1, v);
            if (v_idx != SIZE_MAX && v_idx + 1 < node_levels_[1].size) {
                auto begin = node_levels_[1].down[v_idx];
                auto end_d = node_levels_[1].down[v_idx + 1];
                auto vw_idx = GetNode(2, begin, end_d, w);
                if (vw_idx != end_d) {
                    const auto& l2 = node_levels_[2];
                    if (vw_idx + 1 < l2.size &&
                        l2.down[vw_idx + 1] > l2.down[vw_idx]) {
                        pos.level = 2;
                        pos.index = static_cast<std::uint32_t>(vw_idx);
                        return;
                    }
                }
            }
        }

        auto w_idx = FindNode(1, w);
        if (w_idx != SIZE_MAX && w_idx + 1 < node_levels_[1].size &&
            node_levels_[1].down[w_idx + 1] > node_levels_[1].down[w_idx]) {
            pos.level = 1;
            pos.index = static_cast<std::uint32_t>(w_idx);
            return;
        }

        pos = Pos{};
        return;
    }

    // Node level: stay put if has children.
    const auto& lv = node_levels_[pos.level];
    if (pos.index + 1 >= lv.size) {
        pos = Pos{};
        return;
    }
    if (lv.down[pos.index + 1] > lv.down[pos.index]) {
        return;
    }

    if (pos.level == 1) {
        pos = Pos{};
        return;
    }
    TokenID v = lv.tokens[pos.index];
    auto idx = FindNode(static_cast<int>(pos.level - 1), v);
    if (idx != SIZE_MAX) {
        pos.level = pos.level - 1;
        pos.index = static_cast<std::uint32_t>(idx);
        Back(pos);
    } else {
        pos = Pos{};
    }
}

float_t Scorer::ScoreMove(Pos s, TokenID w, Pos& r) const {
    std::uint32_t level = s.level;
    std::uint32_t index = s.index;

    float_t cost = 0.0;
    if (w == NotToken) {
        r = Pos{};
        return cost;
    }

    while (level < static_cast<std::uint32_t>(num_)) {
        const auto& lv = node_levels_[level];
        std::size_t node_index = (level == 0) ? 0 : index;
        if (node_index + 1 >= lv.size) {
            break;
        }
        auto begin = lv.down[node_index];
        auto end_d = lv.down[node_index + 1];
        if (level == static_cast<std::uint32_t>(num_ - 1)) {
            auto down_idx = GetLeave(begin, end_d, w);
            if (down_idx != end_d) {
                r.level = static_cast<std::uint32_t>(num_);
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + LeafPro(down_idx);
            }
        } else {
            auto down_idx = GetNode(static_cast<int>(level + 1), begin, end_d, w);
            if (down_idx != end_d) {
                r.level = level + 1;
                r.index = static_cast<std::uint32_t>(down_idx);
                return cost + NodePro(static_cast<int>(level + 1), down_idx);
            }
        }

        cost += NodeBow(static_cast<int>(level), node_index);
        if (level == 0) {
            break;
        }
        if (level == 1) {
            level = 0;
            index = 0;
        } else {
            TokenID v = lv.tokens[node_index];
            auto idx = FindNode(static_cast<int>(level - 1), v);
            if (idx != SIZE_MAX) {
                level = level - 1;
                index = static_cast<std::uint32_t>(idx);
            } else {
                level = 0;
                index = 0;
            }
        }
    }

    r = Pos{};
    return cost + NodePro(0, 0);
}

float_t Scorer::UnknownPenalty() const {
    if (node_levels_.empty() || node_levels_[0].size == 0) {
        constexpr float_t DefaultUnknownPenalty = -20.0;
        return DefaultUnknownPenalty;
    }
    return NodePro(0, 0);
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
        auto begin = lv.down[node_index];
        auto end_d = lv.down[node_index + 1];
        if (begin < end_d) break;

        Pos backed = ctx;
        Back(backed);
        if (backed.level == ctx.level && backed.index == ctx.index) {
            return result;
        }
        ctx = backed;
    }

    const float zero_pro = NodePro(0, 0);

    auto collect = [&](std::uint32_t clevel, std::uint32_t cindex,
                       std::vector<std::pair<TokenID, float_t>>& out) {
        if (clevel >= static_cast<std::uint32_t>(num_)) return;
        const auto& lv = node_levels_[clevel];
        std::size_t ni = cindex;
        if (ni + 1 >= lv.size) return;
        auto begin = lv.down[ni];
        auto end_d = lv.down[ni + 1];

        if (clevel == static_cast<std::uint32_t>(num_ - 1)) {
            for (auto i = begin; i < end_d && i < leave_size_; ++i) {
                float pro = LeafPro(i);
                if (pro >= zero_pro) continue;
                out.emplace_back(leaf_tokens_[i], pro);
            }
        } else {
            const auto& children = node_levels_[clevel + 1];
            for (auto i = begin; i < end_d && i < children.size; ++i) {
                float pro = NodePro(static_cast<int>(clevel + 1), i);
                if (pro >= zero_pro) continue;
                out.emplace_back(children.tokens[i], pro);
            }
        }
    };

    collect(ctx.level, ctx.index, result);
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if (result.size() < num && ctx.level > 1) {
        std::unordered_set<TokenID> seen;
        for (const auto& [tid, _] : result) seen.insert(tid);

        // Walk down to lower-order contexts to fill remaining slots.
        Pos backed = ctx;
        while (result.size() < num && backed.level > 1) {
            // Force descent: find the current node's token at level-1.
            const auto& lv = node_levels_[backed.level];
            TokenID v = lv.tokens[backed.index];
            if (backed.level == 2) {
                // bigram → unigram: find v at level 1
                auto idx = FindNode(1, v);
                if (idx == SIZE_MAX) break;
                backed.level = 1;
                backed.index = static_cast<std::uint32_t>(idx);
            } else {
                backed.level = backed.level - 1;
                auto idx = FindNode(static_cast<int>(backed.level), v);
                if (idx == SIZE_MAX) break;
                backed.index = static_cast<std::uint32_t>(idx);
            }

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
        auto begin = root.down[0];
        auto end_d = (root.size > 1) ? root.down[1]
                                     : static_cast<std::uint32_t>(unigrams.size);
        for (auto i = begin; i < end_d && i < unigrams.size; ++i) {
            NGram ng;
            ng.tokens.push_back(unigrams.tokens[i]);
            ng.pro = NodePro(1, i);
            results.push_back(std::move(ng));
        }
    } else if (level == 2) {
        if (num_ < 2 || node_levels_.size() < 2) return results;
        const auto& unigrams = node_levels_[1];
        if (num_ == 2) {
            for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
                auto begin = unigrams.down[i];
                auto end_d = unigrams.down[i + 1];
                for (auto j = begin; j < end_d && j < leave_size_; ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.tokens[i]);
                    ng.tokens.push_back(leaf_tokens_[j]);
                    ng.pro = LeafPro(j);
                    results.push_back(std::move(ng));
                }
            }
        } else {
            const auto& bigrams = node_levels_[2];
            for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
                auto begin = unigrams.down[i];
                auto end_d = unigrams.down[i + 1];
                for (auto j = begin; j < end_d && j + 1 < bigrams.size; ++j) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.tokens[i]);
                    ng.tokens.push_back(bigrams.tokens[j]);
                    ng.pro = NodePro(2, j);
                    results.push_back(std::move(ng));
                }
            }
        }
    } else if (level == 3 && num_ >= 3) {
        const auto& unigrams = node_levels_[1];
        const auto& bigrams = node_levels_[2];
        for (std::size_t i = 0; i + 1 < unigrams.size; ++i) {
            auto bi_begin = unigrams.down[i];
            auto bi_end = unigrams.down[i + 1];
            for (auto j = bi_begin; j < bi_end && j + 1 < bigrams.size; ++j) {
                auto leaf_begin = bigrams.down[j];
                auto leaf_end = bigrams.down[j + 1];
                for (auto k = leaf_begin; k < leaf_end && k < leave_size_; ++k) {
                    NGram ng;
                    ng.tokens.push_back(unigrams.tokens[i]);
                    ng.tokens.push_back(bigrams.tokens[j]);
                    ng.tokens.push_back(leaf_tokens_[k]);
                    ng.pro = LeafPro(k);
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
    const auto* tokens = node_levels_[static_cast<std::size_t>(level)].tokens;
    const auto* first = tokens + begin;
    const auto* last = tokens + end;
    auto it = std::lower_bound(first, last, w);
    if (it == last || *it != w) {
        return end;
    }
    return static_cast<std::size_t>(it - tokens);
}

std::size_t Scorer::GetLeave(std::size_t begin,
                             std::size_t end,
                             TokenID w) const {
    const auto* first = leaf_tokens_ + begin;
    const auto* last = leaf_tokens_ + end;
    auto it = std::lower_bound(first, last, w);
    if (it == last || *it != w) {
        return end;
    }
    return static_cast<std::size_t>(it - leaf_tokens_);
}

} // namespace sime
