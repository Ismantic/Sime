#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace trie {

struct ArrayUnit {
    uint8_t label = 0;
    bool eow = false;
    union {
        uint32_t index;
        int32_t value;
    };
    uint32_t parent = 0;

    ArrayUnit() : index(0) {}

    bool HasValue() const { return label == '\0' && eow; }
    bool IsEmpty() const { return index == 0 && label == 0 && !eow && parent == 0; }
};

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

    // Serialization.
    void Serialize(std::vector<char>& buffer) const;
    bool Deserialize(const char* data, std::size_t size);

    std::size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }

private:
    void CollectWords(std::size_t pos, std::string& word,
                      std::vector<SearchResult>& results,
                      std::size_t max_num) const;

    std::size_t size_ = 0;
    std::unique_ptr<ArrayUnit[]> array_;

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
