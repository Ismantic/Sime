#include "trie.h"

#include <cassert>
#include <cstring>

namespace trie {

// --- DoubleArray ---

void DoubleArray::Build(const std::vector<std::string>& keys,
                        const std::vector<uint32_t>& values) {
    assert(keys.size() == values.size());
    Builder b;
    b.Run(keys, values);
    array_ = b.GetResult(size_);
}

bool DoubleArray::Get(std::string_view key, uint32_t& out) const {
    if (Empty()) return false;

    std::size_t pos = 0;
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (pos >= size_) return false;
        auto ch = static_cast<uint8_t>(key[i]);
        std::size_t prev = pos;
        std::size_t next = pos ^ array_[pos].index ^ ch;
        if (next >= size_) return false;
        pos = next;
        if (array_[pos].label != ch || array_[pos].parent != prev)
            return false;
    }
    if (!array_[pos].eow) return false;
    std::size_t vp = pos ^ array_[pos].index;
    if (vp >= size_ || !array_[vp].HasValue()) return false;
    out = static_cast<uint32_t>(array_[vp].value);
    return true;
}

std::vector<SearchResult> DoubleArray::PrefixSearch(
    std::string_view str, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    std::size_t pos = 0;

    // Check empty-string key at root.
    if (array_[0].eow) {
        std::size_t vp = 0 ^ array_[0].index;
        if (vp < size_ && array_[vp].HasValue()) {
            results.push_back({static_cast<uint32_t>(array_[vp].value), 0});
            if (results.size() >= max_num) return results;
        }
    }

    for (std::size_t i = 0; i < str.size(); ++i) {
        if (pos >= size_) break;
        auto ch = static_cast<uint8_t>(str[i]);
        std::size_t prev = pos;
        std::size_t next = pos ^ array_[pos].index ^ ch;
        if (next >= size_) break;
        pos = next;
        if (array_[pos].label != ch || array_[pos].parent != prev) break;

        if (array_[pos].eow) {
            std::size_t vp = pos ^ array_[pos].index;
            if (vp < size_ && array_[vp].HasValue()) {
                results.push_back(
                    {static_cast<uint32_t>(array_[vp].value), i + 1});
                if (results.size() >= max_num) break;
            }
        }
    }
    return results;
}

std::vector<SearchResult> DoubleArray::FindWordsWithPrefix(
    std::string_view prefix, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    std::size_t pos = 0;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        auto ch = static_cast<uint8_t>(prefix[i]);
        std::size_t prev = pos;
        pos ^= array_[pos].index ^ ch;
        if (pos >= size_) return results;
        if (array_[pos].label != ch || array_[pos].parent != prev)
            return results;
    }

    std::string word(prefix);
    CollectWords(pos, word, results, max_num);
    return results;
}

void DoubleArray::CollectWords(std::size_t pos, std::string& word,
                               std::vector<SearchResult>& results,
                               std::size_t max_num,
                               bool stop_at_sep) const {
    if (results.size() >= max_num || pos >= size_) return;

    if (array_[pos].eow) {
        std::size_t vp = pos ^ array_[pos].index;
        if (vp < size_ && array_[vp].HasValue()) {
            results.push_back(
                {static_cast<uint32_t>(array_[vp].value), word.size()});
            if (results.size() >= max_num) return;
        }
    }

    uint32_t base = array_[pos].index;
    for (int ch = 1; ch <= 255; ++ch) {
        if (stop_at_sep && ch == '\'') continue;
        std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
        if (child >= size_ || child == pos) continue;
        if (array_[child].label != ch || array_[child].parent != pos) continue;
        word.push_back(static_cast<char>(ch));
        CollectWords(child, word, results, max_num, stop_at_sep);
        word.pop_back();
        if (results.size() >= max_num) return;
    }
}

// -----------------------------------------------------------------------
// Pinyin / T9 state machine — "syllable-boundary skipping"
// -----------------------------------------------------------------------
//
// DAT keys are stored as literal Pinyin with apostrophe separators
// ("zhong'guo" for 中国). These functions let abbreviated, non-separated,
// or mid-syllable input match the same keys without duplicating entries.
// See `private/SyllableBoundarySkipping.md` for the technique writeup.
//
// Pieces and how they compose (the functions appear below in this order,
// except for the T9 drivers that come first and reuse everything):
//
//   PinyinState { pos, depth }
//     DAT cursor + chars consumed in current syllable. depth=0 means just
//     past '\'', 1 means only the initial, >=2 means deep. A set of states
//     runs in parallel because the same input may parse several ways
//     (e.g. "xian" = "xian" or "xi'an").
//
//   FindSepDescendants(pos, out, max_depth)
//     Bounded DFS from `pos` collecting every '\'' node within one syllable
//     (depth bound = max Pinyin syllable length). Used by AdvancePinyin to
//     find the start of the next syllable when skipping forward.
//
//   RecordMatches(states, input_len, results, max_num)
//     Record any state whose `pos` is an end-of-word — the input matched a
//     stored key exactly.
//
//   AdvancePinyin(states, ch)
//     One state-machine step. Per state: (a) direct child match, (b) skip a
//     stored '\'' then match, (c) bounded DFS to find '\'' descendants and
//     match beyond them — (c) only when depth==1, enforcing the "only the
//     initial can abbreviate" convention. Input '\'' is handled symmetrically.
//
//   PrefixSearchPinyin(input, max_num)
//     Lattice-lookup driver. Calls AdvancePinyin one input char at a time;
//     after each step, RecordMatches + a bounded "last syllable eow" DFS
//     (so "zg" → 中国 hits even though `zg` is not itself an eow node).
//     Emits edges at every consumed length — used for decoder lattice.
//
//   FindWordsWithPrefixPinyin(prefix, max_num)
//     Tail-expansion driver. Advances through the whole prefix first, then
//     CollectWords from surviving states staying within the current syllable
//     (stop_at_sep=true) — used when the user is typing mid-syllable and
//     wants candidates that complete it.
//
//   PrefixSearchT9 / FindWordsWithPrefixT9
//     Same shape, but each input digit is expanded to its candidate letters
//     (2→abc, 3→def, …) and every expansion is advanced in parallel. The
//     letter-keyed DAT serves nine-key input without a separate table.
//
// -----------------------------------------------------------------------

std::vector<SearchResult> DoubleArray::PrefixSearchT9(
    std::string_view digits, CharExpander expand,
    std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    // Reuse the pinyin state machine but expand each input char
    std::vector<PinyinState> states = {{0, 0}};
    RecordMatches(states, 0, results, max_num);

    // Complete the last syllable via bounded DFS (same as PrefixSearchPinyin).
    // Only fires for depth==1 states (just the initial typed).
    auto recordLastSyllableMatches = [&](std::size_t input_len) {
        for (const auto& s : states) {
            if (results.size() >= max_num) break;
            if (s.depth != 1) continue;
            struct Frame { std::size_t pos; int rem; };
            std::vector<Frame> stack = {{s.pos, 6}};
            while (!stack.empty() && results.size() < max_num) {
                auto [pos, rem] = stack.back();
                stack.pop_back();
                if (rem <= 0 || pos >= size_) continue;
                if (array_[pos].eow) {
                    std::size_t vp = pos ^ array_[pos].index;
                    if (vp < size_ && array_[vp].HasValue()) {
                        uint32_t val = static_cast<uint32_t>(array_[vp].value);
                        bool dup = false;
                        for (const auto& r : results) {
                            if (r.value == val && r.length == input_len) {
                                dup = true; break;
                            }
                        }
                        if (!dup) results.push_back({val, input_len});
                    }
                }
                uint32_t base = array_[pos].index;
                for (int ch = 1; ch <= 255; ++ch) {
                    if (ch == '\'') continue;
                    std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
                    if (child >= size_ || child == pos) continue;
                    if (array_[child].label != ch ||
                        array_[child].parent != pos) continue;
                    stack.push_back({child, rem - 1});
                }
            }
        }
    };

    // Do NOT break early on results.size() >= max_num: T9 digit expansion
    // floods short matches quickly, which would prevent discovering longer
    // (and often better) matches.  Process every digit so that full-length
    // matches like 还是 (424744) or 什么 (743663) are always reachable.
    // Use a generous internal limit; trim at the end.
    const std::size_t internal_limit = std::max(max_num, std::size_t{4096});

    for (std::size_t i = 0; i < digits.size() && !states.empty(); ++i) {
        auto ch = static_cast<uint8_t>(digits[i]);

        if (ch == '\'') {
            AdvancePinyin(states, ch);
        } else {
            const char* letters = expand(ch);
            if (!letters || !letters[0]) {
                states.clear();
                break;
            }
            AdvanceT9(states, letters);
        }
        RecordMatches(states, i + 1, results, internal_limit);
        recordLastSyllableMatches(i + 1);
    }
    return results;
}

std::vector<SearchResult> DoubleArray::FindWordsWithPrefixT9(
    std::string_view digits, CharExpander expand,
    std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    // Advance through digit prefix
    std::vector<PinyinState> states = {{0, 0}};
    for (std::size_t i = 0; i < digits.size() && !states.empty(); ++i) {
        auto ch = static_cast<uint8_t>(digits[i]);
        if (ch == '\'') {
            AdvancePinyin(states, ch);
        } else {
            const char* letters = expand(ch);
            if (!letters || !letters[0]) { states.clear(); break; }
            AdvanceT9(states, letters);
        }
    }

    // Collect words, staying within current syllable
    for (const auto& s : states) {
        if (results.size() >= max_num) break;
        std::string word;
        CollectWords(s.pos, word, results, max_num, /*stop_at_sep=*/true);
    }
    return results;
}

// --- Serialize / Deserialize ---

void DoubleArray::Serialize(std::vector<char>& buffer) const {
    auto sz = static_cast<uint32_t>(size_);
    std::size_t offset = buffer.size();
    // Each ArrayUnit: label(1) + eow(1) + index/value(4) + parent(4) = 10 bytes
    constexpr std::size_t kUnitBytes = 10;
    buffer.resize(offset + sizeof(sz) + sz * kUnitBytes);
    std::memcpy(buffer.data() + offset, &sz, sizeof(sz));
    offset += sizeof(sz);
    for (uint32_t i = 0; i < sz; ++i) {
        const auto& u = array_[i];
        buffer[offset++] = static_cast<char>(u.label);
        buffer[offset++] = static_cast<char>(u.eow ? 1 : 0);
        std::memcpy(buffer.data() + offset, &u.index, sizeof(u.index));
        offset += sizeof(u.index);
        std::memcpy(buffer.data() + offset, &u.parent, sizeof(u.parent));
        offset += sizeof(u.parent);
    }
}

bool DoubleArray::Deserialize(const char* data, std::size_t size) {
    if (size < sizeof(uint32_t)) return false;
    uint32_t sz = 0;
    std::memcpy(&sz, data, sizeof(sz));
    constexpr std::size_t kUnitBytes = 10;
    if (size < sizeof(sz) + sz * kUnitBytes) return false;

    size_ = sz;
    array_ = std::make_unique<ArrayUnit[]>(sz);
    std::size_t offset = sizeof(sz);
    for (uint32_t i = 0; i < sz; ++i) {
        auto& u = array_[i];
        u.label = static_cast<uint8_t>(data[offset++]);
        u.eow = data[offset++] != 0;
        std::memcpy(&u.index, data + offset, sizeof(u.index));
        offset += sizeof(u.index);
        std::memcpy(&u.parent, data + offset, sizeof(u.parent));
        offset += sizeof(u.parent);
    }
    return true;
}

// --- Pinyin-aware matching ---

std::size_t DoubleArray::TryChild(std::size_t pos, uint8_t ch) const {
    if (pos >= size_) return SIZE_MAX;
    std::size_t child = pos ^ array_[pos].index ^ ch;
    if (child >= size_ || child == pos) return SIZE_MAX;
    if (array_[child].label != ch || array_[child].parent != pos)
        return SIZE_MAX;
    return child;
}

void DoubleArray::FindSepDescendants(std::size_t pos,
                                     std::vector<std::size_t>& out,
                                     int max_depth) const {
    constexpr std::size_t MaxSeps = 256;
    if (max_depth <= 0 || pos >= size_ || out.size() >= MaxSeps) return;
    uint32_t base = array_[pos].index;
    for (int ch = 1; ch <= 255; ++ch) {
        if (out.size() >= MaxSeps) return;
        std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
        if (child >= size_ || child == pos) continue;
        if (array_[child].label != ch || array_[child].parent != pos)
            continue;
        if (ch == '\'') {
            out.push_back(child);
        } else {
            FindSepDescendants(child, out, max_depth - 1);
        }
    }
}

const std::vector<std::size_t>& DoubleArray::GetSepDescendants(
    std::size_t pos) const {
    auto it = sep_cache_.find(pos);
    if (it != sep_cache_.end()) return it->second;
    auto& entry = sep_cache_[pos];
    FindSepDescendants(pos, entry, 6);
    return entry;
}

void DoubleArray::RecordMatches(const std::vector<PinyinState>& states,
                                std::size_t input_len,
                                std::vector<SearchResult>& results,
                                std::size_t max_num) const {
    for (const auto& s : states) {
        if (results.size() >= max_num) return;
        if (!array_[s.pos].eow) continue;
        std::size_t vp = s.pos ^ array_[s.pos].index;
        if (vp >= size_ || !array_[vp].HasValue()) continue;
        uint32_t val = static_cast<uint32_t>(array_[vp].value);
        // Deduplicate by value
        bool dup = false;
        for (const auto& r : results) {
            if (r.value == val && r.length == input_len) { dup = true; break; }
        }
        if (!dup) {
            results.push_back({val, input_len});
        }
    }
}

void DoubleArray::AdvancePinyin(std::vector<PinyinState>& states,
                                uint8_t ch) const {
    auto canSkip = [&](const PinyinState& s) -> bool {
        return s.depth == 1;
    };

    std::vector<PinyinState> next;

    for (const auto& s : states) {
        uint8_t new_depth = (s.depth < 255) ? static_cast<uint8_t>(s.depth + 1) : 255;

        if (ch == static_cast<uint8_t>('\'')) {
            // Separator in input
            // 1. Direct '\'' match
            std::size_t sep = TryChild(s.pos, ch);
            if (sep != SIZE_MAX) {
                next.push_back({sep, 0});
            }
            // 2. Syllable skip to '\''
            if (canSkip(s)) {
                for (auto sp : GetSepDescendants(s.pos)) {
                    next.push_back({sp, 0});
                }
            }
        } else {
            // Normal character
            // 1. Direct match
            std::size_t child = TryChild(s.pos, ch);
            if (child != SIZE_MAX) {
                next.push_back({child, new_depth});
            }
            // 2. Skip '\'' then match (for non-separator input crossing boundary)
            std::size_t sep = TryChild(s.pos, static_cast<uint8_t>('\''));
            if (sep != SIZE_MAX) {
                std::size_t after_sep = TryChild(sep, ch);
                if (after_sep != SIZE_MAX) {
                    next.push_back({after_sep, 1});  // new syllable, first char
                }
            }
            // 3. Syllable skip
            if (canSkip(s)) {
                for (auto sp : GetSepDescendants(s.pos)) {
                    std::size_t after = TryChild(sp, ch);
                    if (after != SIZE_MAX) {
                        next.push_back({after, 1});  // new syllable
                    }
                }
            }
        }
    }

    // Deduplicate by pos (keep smallest depth)
    std::sort(next.begin(), next.end(),
              [](const PinyinState& a, const PinyinState& b) {
                  return a.pos < b.pos || (a.pos == b.pos && a.depth < b.depth);
              });
    next.erase(std::unique(next.begin(), next.end(),
                           [](const PinyinState& a, const PinyinState& b) {
                               return a.pos == b.pos;
                           }), next.end());

    // Cap state count to prevent combinatorial explosion (T9)
    constexpr std::size_t MaxStates = 2048;
    if (next.size() > MaxStates) next.resize(MaxStates);

    states = std::move(next);
}

void DoubleArray::AdvanceT9(std::vector<PinyinState>& states,
                            const char* letters) const {
    auto canSkip = [&](const PinyinState& s) -> bool {
        return s.depth == 1;
    };

    std::vector<PinyinState> next;

    for (const auto& s : states) {
        // Pre-compute shared state: separator child and sep descendants
        std::size_t sep_child = TryChild(s.pos, static_cast<uint8_t>('\''));
        bool need_skip = canSkip(s);
        const std::vector<std::size_t>* seps = nullptr;
        if (need_skip) {
            seps = &GetSepDescendants(s.pos);
        }

        for (const char* lp = letters; *lp; ++lp) {
            uint8_t ch = static_cast<uint8_t>(*lp);
            uint8_t new_depth = (s.depth < 255)
                ? static_cast<uint8_t>(s.depth + 1) : 255;

            // 1. Direct match
            std::size_t child = TryChild(s.pos, ch);
            if (child != SIZE_MAX) {
                next.push_back({child, new_depth});
            }
            // 2. Skip separator then match
            if (sep_child != SIZE_MAX) {
                std::size_t after_sep = TryChild(sep_child, ch);
                if (after_sep != SIZE_MAX) {
                    next.push_back({after_sep, 1});
                }
            }
            // 3. Syllable skip
            if (need_skip) {
                for (auto sp : *seps) {
                    std::size_t after = TryChild(sp, ch);
                    if (after != SIZE_MAX) {
                        next.push_back({after, 1});
                    }
                }
            }
        }
    }

    // Deduplicate by pos (keep smallest depth)
    std::sort(next.begin(), next.end(),
              [](const PinyinState& a, const PinyinState& b) {
                  return a.pos < b.pos || (a.pos == b.pos && a.depth < b.depth);
              });
    next.erase(std::unique(next.begin(), next.end(),
                           [](const PinyinState& a, const PinyinState& b) {
                               return a.pos == b.pos;
                           }), next.end());

    constexpr std::size_t MaxT9States = 4096;
    if (next.size() > MaxT9States) next.resize(MaxT9States);

    states = std::move(next);
}

std::vector<SearchResult> DoubleArray::PrefixSearchPinyin(
    std::string_view str, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    std::vector<PinyinState> states = {{0, 0}};

    RecordMatches(states, 0, results, max_num);

    auto recordLastSyllableMatches = [&](std::size_t input_len) {
        for (const auto& s : states) {
            if (results.size() >= max_num) break;
            if (s.depth != 1) continue;
            // Bounded DFS looking for eow within the current syllable
            struct Frame { std::size_t pos; int rem; };
            std::vector<Frame> stack = {{s.pos, 6}};
            while (!stack.empty() && results.size() < max_num) {
                auto [pos, rem] = stack.back();
                stack.pop_back();
                if (rem <= 0 || pos >= size_) continue;
                if (array_[pos].eow) {
                    std::size_t vp = pos ^ array_[pos].index;
                    if (vp < size_ && array_[vp].HasValue()) {
                        uint32_t val = static_cast<uint32_t>(array_[vp].value);
                        bool dup = false;
                        for (const auto& r : results) {
                            if (r.value == val && r.length == input_len) {
                                dup = true; break;
                            }
                        }
                        if (!dup) results.push_back({val, input_len});
                    }
                }
                uint32_t base = array_[pos].index;
                for (int ch = 1; ch <= 255; ++ch) {
                    if (ch == '\'') continue;
                    std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
                    if (child >= size_ || child == pos) continue;
                    if (array_[child].label != ch ||
                        array_[child].parent != pos) continue;
                    stack.push_back({child, rem - 1});
                }
            }
        }
    };

    for (std::size_t i = 0; i < str.size() && !states.empty(); ++i) {
        if (results.size() >= max_num) break;
        AdvancePinyin(states, static_cast<uint8_t>(str[i]));
        RecordMatches(states, i + 1, results, max_num);
        recordLastSyllableMatches(i + 1);
    }

    return results;
}

std::vector<SearchResult> DoubleArray::FindWordsWithPrefixPinyin(
    std::string_view prefix, std::size_t max_num) const {
    std::vector<SearchResult> results;
    if (Empty()) return results;

    // Advance through prefix using pinyin state machine
    std::vector<PinyinState> states = {{0, 0}};
    for (std::size_t i = 0; i < prefix.size() && !states.empty(); ++i) {
        AdvancePinyin(states, static_cast<uint8_t>(prefix[i]));
    }

    // Collect words, staying within current syllable (stop at '\'')
    for (const auto& s : states) {
        if (results.size() >= max_num) break;
        std::string word(prefix);
        CollectWords(s.pos, word, results, max_num, /*stop_at_sep=*/true);
    }

    return results;
}

// --- Builder ---

void DoubleArray::Builder::Run(const std::vector<std::string>& keys,
                               const std::vector<uint32_t>& values) {
    // Build intermediate trie.
    auto root = std::make_unique<TrieNode>();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        TrieNode* cur = root.get();
        for (char c : keys[i]) {
            auto ch = static_cast<uint8_t>(c);
            auto& child = cur->children[ch];
            if (!child) child = std::make_unique<TrieNode>();
            cur = child.get();
        }
        cur->eow = true;
        cur->value = values[i];
    }

    // Convert to double array.
    units_.resize(1024);
    used_.resize(1024, false);
    used_[0] = true;
    units_[0].label = 0;
    if (!root->children.empty() || root->eow)
        ConvertNode(root.get(), 0);

    // Trim trailing unused.
    while (!units_.empty() && !used_.back()) {
        units_.pop_back();
        used_.pop_back();
    }
}

std::unique_ptr<ArrayUnit[]> DoubleArray::Builder::GetResult(std::size_t& size) {
    size = units_.size();
    auto result = std::make_unique<ArrayUnit[]>(size);
    for (std::size_t i = 0; i < size; ++i) result[i] = units_[i];
    return result;
}

void DoubleArray::Builder::ConvertNode(TrieNode* node, std::size_t pos) {
    std::vector<uint8_t> labels;
    std::vector<TrieNode*> children;
    for (const auto& [ch, child] : node->children) {
        labels.push_back(ch);
        children.push_back(child.get());
    }
    if (node->eow) {
        labels.push_back(0);
        children.push_back(nullptr);
    }
    if (labels.empty()) return;

    uint32_t base = SetupChildren(labels, pos, node);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] != 0) {
            ConvertNode(children[i], base ^ labels[i]);
        }
    }
}

uint32_t DoubleArray::Builder::SetupChildren(
    const std::vector<uint8_t>& labels, std::size_t pos, TrieNode* node) {
    uint32_t base = FindFreeBase(labels);
    units_[pos].index = static_cast<uint32_t>(pos ^ base);

    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::size_t p = base ^ labels[i];
        EnsureSize(p + 1);
        used_[p] = true;
        units_[p].label = labels[i];
        units_[p].parent = static_cast<uint32_t>(pos);
        if (labels[i] == 0) {
            units_[p].value = static_cast<int32_t>(node->value);
            units_[p].eow = true;
            units_[pos].eow = true;
        }
    }
    return base;
}

uint32_t DoubleArray::Builder::FindFreeBase(const std::vector<uint8_t>& labels) {
    uint32_t start = (prev_base_ > 256) ? prev_base_ - 256 : 1;
    for (uint32_t base = start; ; ++base) {
        bool ok = true;
        for (auto l : labels) {
            std::size_t p = base ^ l;
            EnsureSize(p + 1);
            if (used_[p]) { ok = false; break; }
        }
        if (ok) {
            prev_base_ = base;
            return base;
        }
    }
}

void DoubleArray::Builder::EnsureSize(std::size_t n) {
    if (n > units_.size()) {
        std::size_t ns = std::max(n, units_.size() * 2);
        units_.resize(ns);
        used_.resize(ns, false);
    }
}

} // namespace trie
