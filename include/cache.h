#pragma once

#include "trie.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace trie {

class T9CacheSession {
public:
    using CharExpander = DoubleArray::CharExpander;

    T9CacheSession() = default;

    void Bind(const DoubleArray* dat, CharExpander expand);
    void Reset();
    void Reset(std::string_view input);
    void Append(std::string_view input);
    void Append(uint8_t ch);

    std::size_t InputLen() const { return input_len_; }
    const std::vector<DoubleArray::PinyinState>& States() const {
        return states_;
    }

    std::vector<SearchResult> CollectPrefixMatchesCurrent(
        std::size_t max_per_len = 80) const;
    std::vector<SearchResult> CollectCompletions(
        std::size_t max_num = 96,
        bool stop_at_sep = true) const;

private:
    const DoubleArray* dat_ = nullptr;
    CharExpander expand_ = nullptr;
    std::vector<DoubleArray::PinyinState> states_;
    std::size_t input_len_ = 0;
};

} // namespace trie
