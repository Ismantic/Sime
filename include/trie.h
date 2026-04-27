#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trie {

struct ArrayUnit {
    uint8_t label = 0;
    bool eow = false;
    uint16_t _pad = 0;  // explicit padding so on-disk layout matches in-memory
    union {
        uint32_t index;
        int32_t value;
    };
    uint32_t parent = 0;

    ArrayUnit() : index(0) {}

    bool HasValue() const { return label == '\0' && eow; }
    bool IsEmpty() const { return index == 0 && label == 0 && !eow && parent == 0; }
};
static_assert(sizeof(ArrayUnit) == 12, "ArrayUnit must be 12 bytes for mmap-compatible layout");
static_assert(alignof(ArrayUnit) == 4, "ArrayUnit alignment must be 4");

struct SearchResult {
    uint32_t value = 0;
    std::size_t length = 0;
};

class DoubleArray {
public:
    DoubleArray() = default;
    DoubleArray(DoubleArray&&) noexcept = default;
    DoubleArray& operator=(DoubleArray&&) noexcept = default;
    DoubleArray(const DoubleArray&) = delete;
    DoubleArray& operator=(const DoubleArray&) = delete;

    void Build(const std::vector<std::string>& keys,
               const std::vector<uint32_t>& values);

    // Exact match.
    bool Get(std::string_view key, uint32_t& out) const;

    // All keys that are prefixes of `str`.
    std::vector<SearchResult> PrefixSearch(std::string_view str,
                                           std::size_t max_num = 96) const;

    // All keys that start with `prefix`.
    std::vector<SearchResult> FindWordsWithPrefix(std::string_view prefix,
                                                  std::size_t max_num = 96) const;

    // Pinyin-aware variants: `'` in DAT keys is treated as a syllable
    // boundary.  Input chars that don't match the DAT can skip ahead to
    // the next `'` (syllable abbreviation), and `'` in keys can be
    // silently skipped when the input has no separator.
    std::vector<SearchResult> PrefixSearchPinyin(
        std::string_view str, std::size_t max_num = 96) const;
    std::vector<SearchResult> FindWordsWithPrefixPinyin(
        std::string_view prefix, std::size_t max_num = 96) const;

    // T9 digit-expansion variants: each digit (2-9) is expanded to
    // its possible letters and matched against letter-based DAT keys.
    // The expander function returns possible chars for an input byte.
    using CharExpander = const char* (*)(uint8_t);
    std::vector<SearchResult> PrefixSearchT9(
        std::string_view digits, CharExpander expand,
        std::size_t max_num = 96) const;

    // Incremental frontier APIs used by decoder cache layers.
    struct PinyinState {
        std::size_t pos = 0;
        uint8_t depth = 0;  // 0=boundary, 1=initial, 2+=deep in syllable
        bool fuzzy = false; // true if this path used abbreviation/skip
    };

    struct ExactState {
        std::size_t pos = 0;
        bool valid = true;
    };

    std::vector<PinyinState> StartPinyinStates() const;
    ExactState StartExactState() const;

    void AdvancePinyinStates(std::vector<PinyinState>& states,
                             uint8_t ch) const;
    void AdvanceT9States(std::vector<PinyinState>& states,
                         uint8_t ch,
                         CharExpander expand) const;
    void AdvanceExactState(ExactState& state, uint8_t ch) const;

    std::vector<SearchResult> CollectPrefixMatchesPinyin(
        const std::vector<PinyinState>& states,
        std::size_t input_len,
        std::size_t max_num = 96,
        bool include_last_syllable = true) const;
    std::vector<SearchResult> CollectPrefixMatchesExact(
        const ExactState& state,
        std::size_t input_len,
        std::size_t max_num = 96) const;
    std::vector<SearchResult> CollectCompletionsPinyin(
        const std::vector<PinyinState>& states,
        std::size_t prefix_len,
        std::size_t max_num = 96,
        bool stop_at_sep = true) const;
    std::vector<SearchResult> CollectCompletionsExact(
        const ExactState& state,
        std::size_t prefix_len,
        std::size_t max_num = 96,
        bool stop_at_sep = false) const;

    // Serialization.
    void Serialize(std::vector<char>& buffer) const;
    bool Deserialize(const char* data, std::size_t size);
    // Zero-copy attach to mmap'd memory. `data` must be 4-byte aligned and
    // remain valid for the lifetime of this DoubleArray. On success returns
    // true and writes the number of bytes consumed (header + array) to
    // *consumed if non-null.
    bool MmapAttach(const char* data, std::size_t size,
                    std::size_t* consumed = nullptr);

    std::size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }

private:
    void CollectWords(std::size_t pos, std::string& word,
                      std::vector<SearchResult>& results,
                      std::size_t max_num,
                      bool stop_at_sep = false) const;

    // Try following a character from pos. Returns child pos or SIZE_MAX.
    std::size_t TryChild(std::size_t pos, uint8_t ch) const;

    // Find all '\'' descendant positions reachable from pos within
    // max_depth non-'\'' steps.
    void FindSepDescendants(std::size_t pos,
                            std::vector<std::size_t>& out,
                            int max_depth) const;

    // Core state-machine advance: given a set of DAT states and an input
    // character, compute the next state set.
    void AdvancePinyin(std::vector<PinyinState>& states,
                       uint8_t ch) const;
    // Fused T9 advance: expand digit to letters and advance all states
    // in one pass, avoiding per-letter vector copies.
    void AdvanceT9(std::vector<PinyinState>& states,
                   const char* letters) const;

    const std::vector<std::size_t>& GetSepDescendants(std::size_t pos) const;

    std::size_t size_ = 0;
    // `array_` is the read-only view used by all lookup paths.
    // Either backed by `owned_` (Build/Deserialize) or by external mmap memory
    // (MmapAttach). `owned_` is null in the mmap case.
    const ArrayUnit* array_ = nullptr;
    std::unique_ptr<ArrayUnit[]> owned_;
    std::vector<uint8_t> alphabet_;  // distinct labels in the trie
    // Lazy cache of FindSepDescendants(pos, …, 6). Hot path: ~21M hits per
    // num-decode sentence batch. Direct vector index beats std::unordered_map
    // hash+bucket walk.
    mutable std::vector<std::vector<std::size_t>> sep_cache_;
    mutable std::vector<bool> sep_cache_computed_;

    // --- Builder (used only during Build) ---
    struct TrieNode {
        std::map<uint8_t, std::unique_ptr<TrieNode>> children;
        bool eow = false;
        uint32_t value = 0;
    };

    class Builder {
    public:
        void Run(const std::vector<std::string>& keys,
                 const std::vector<uint32_t>& values);
        std::unique_ptr<ArrayUnit[]> GetResult(std::size_t& size);

    private:
        void ConvertNode(TrieNode* node, std::size_t pos);
        uint32_t SetupChildren(const std::vector<uint8_t>& labels,
                               std::size_t pos, TrieNode* node);
        uint32_t FindFreeBase(const std::vector<uint8_t>& labels);
        void EnsureSize(std::size_t n);

        std::vector<ArrayUnit> units_;
        std::vector<bool> used_;
        uint32_t prev_base_ = 0;
    };
};

} // namespace trie
