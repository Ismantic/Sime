#pragma once

#include "common.h"
#include "score.h"
#include "trie.h"
#include "unit.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

class UserDict {
public:
    struct Match {
        TokenID id;
    };

    // Load user dict entries and compute LM scores using trie + scorer.
    bool Load(const std::filesystem::path& path,
              const Trie& trie,
              const Scorer& scorer);

    // Given units[0..len), return matching entries with global token IDs.
    std::vector<Match> Lookup(const Unit* units, std::size_t len) const;

    const std::u32string& TextAt(std::size_t local_id) const;
    float_t ScoreAt(std::size_t local_id) const;

    std::size_t EntryCount() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }

    void SetBaseTokenID(TokenID base) { base_id_ = base; }
    TokenID BaseTokenID() const { return base_id_; }

private:
    static std::string MakeKey(const Unit* units, std::size_t len);

    // Greedy tokenize u32 text using trie's token table, return token IDs.
    static std::vector<TokenID> Tokenize(
        const std::u32string& text,
        const std::unordered_map<std::u32string, TokenID>& text_to_id);

    // Score a token sequence with LM from BOS.
    static float_t ScoreTokens(const std::vector<TokenID>& tokens,
                                const Scorer& scorer);

    struct Entry {
        std::u32string text;
        float_t score;
    };

    // key = packed unit sequence, value = indices into entries_
    std::unordered_map<std::string, std::vector<std::size_t>> dict_;
    std::vector<Entry> entries_;
    TokenID base_id_ = 0;
};

} // namespace sime
