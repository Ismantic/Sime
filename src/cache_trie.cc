#include "cache_trie.h"

#include <unordered_set>

namespace trie {

void T9CacheSession::Bind(const DoubleArray* dat, CharExpander expand) {
    dat_ = dat;
    expand_ = expand;
    Reset();
}

void T9CacheSession::Reset() {
    input_len_ = 0;
    if (!dat_ || dat_->Empty()) {
        states_.clear();
        return;
    }
    states_ = {{0, 0}};
}

void T9CacheSession::Reset(std::string_view input) {
    Reset();
    Append(input);
}

void T9CacheSession::Append(std::string_view input) {
    for (char ch : input) {
        Append(static_cast<uint8_t>(ch));
    }
}

void T9CacheSession::Append(uint8_t ch) {
    ++input_len_;
    if (!dat_ || dat_->Empty() || states_.empty()) {
        states_.clear();
        return;
    }
    dat_->AdvanceT9States(states_, ch, expand_);
}

std::vector<SearchResult> T9CacheSession::CollectPrefixMatchesCurrent(
    std::size_t max_per_len) const {
    std::vector<SearchResult> results;
    if (!dat_ || dat_->Empty() || max_per_len == 0 || states_.empty()) {
        return results;
    }

    constexpr std::size_t MaxT9Digits = 24;
    if (input_len_ > MaxT9Digits) return results;

    std::unordered_set<uint64_t> seen;
    auto add_result = [&](uint32_t val) -> bool {
        if (results.size() >= max_per_len) return false;
        uint64_t key = (static_cast<uint64_t>(val) << 32)
                     | static_cast<uint64_t>(input_len_);
        if (!seen.insert(key).second) return false;
        results.push_back({val, input_len_});
        return true;
    };

    for (const auto& s : states_) {
        if (results.size() >= max_per_len) break;
        std::size_t pos = s.pos;
        if (pos >= dat_->size_ || !dat_->array_[pos].eow) continue;
        std::size_t vp = pos ^ dat_->array_[pos].index;
        if (vp >= dat_->size_ || !dat_->array_[vp].HasValue()) continue;
        add_result(static_cast<uint32_t>(dat_->array_[vp].value));
    }

    for (const auto& s : states_) {
        if (results.size() >= max_per_len) break;
        if (s.depth != 1) continue;
        struct Frame { std::size_t pos; int rem; };
        std::vector<Frame> stack = {{s.pos, 6}};
        while (!stack.empty() && results.size() < max_per_len) {
            auto [pos, rem] = stack.back();
            stack.pop_back();
            if (rem <= 0 || pos >= dat_->size_) continue;
            if (dat_->array_[pos].eow) {
                std::size_t vp = pos ^ dat_->array_[pos].index;
                if (vp < dat_->size_ && dat_->array_[vp].HasValue()) {
                    add_result(static_cast<uint32_t>(dat_->array_[vp].value));
                }
            }
            uint32_t base = dat_->array_[pos].index;
            auto try_child = [&](int ch) {
                std::size_t child = pos ^ base ^ static_cast<unsigned>(ch);
                if (child < dat_->size_ && child != pos &&
                    dat_->array_[child].label == ch &&
                    dat_->array_[child].parent == pos) {
                    stack.push_back({child, rem - 1});
                }
            };
            for (int ch = 'a'; ch <= 'z'; ++ch) try_child(ch);
            for (int ch = 'A'; ch <= 'Z'; ++ch) try_child(ch);
        }
    }

    return results;
}

std::vector<SearchResult> T9CacheSession::CollectCompletions(
    std::size_t max_num,
    bool stop_at_sep) const {
    std::vector<SearchResult> results;
    if (!dat_ || dat_->Empty() || max_num == 0) return results;

    for (const auto& s : states_) {
        if (results.size() >= max_num) break;
        std::string word;
        dat_->CollectWords(s.pos, word, results, max_num, stop_at_sep);
    }
    return results;
}

} // namespace trie
