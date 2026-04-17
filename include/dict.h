#pragma once

#include "common.h"
#include "trie.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sime {

class Dict {
public:
    enum DatType : int {
        LetterPinyin = 0,
        LetterEn = 1,
        NumPinyin = 2,
        NumEn = 3,
        DatCount = 4,
    };

    struct Item {
        TokenID id = 0;
        const char* pieces = nullptr;  // e.g., "ni'hao"
    };

    struct Entry {
        const Item* items = nullptr;
        uint32_t count = 0;
    };

    Dict() = default;
    ~Dict();

    bool Load(const std::filesystem::path& path);
    void Clear();

    const trie::DoubleArray& Dat(DatType type) const { return dats_[type]; }
    Entry GetEntry(DatType type, uint32_t index) const;

    const char32_t* TokenAt(uint32_t id) const;
    uint32_t TokenCount() const { return token_count_; }
    const std::unordered_set<TokenID>& TokenSet() const { return token_set_; }

    // Static pinyin utilities (backed by dict.inc)
    static bool IsKnownPinyin(const std::string& text);
    static char LetterToNum(char c);
    static std::string LettersToNums(std::string_view letters);

private:
    trie::DoubleArray dats_[DatCount];

    // Side table: entries_[type][index] = list of Items
    struct EntryData {
        std::vector<TokenID> ids;
        std::vector<std::string> pieces;  // parallel with ids
    };
    std::vector<EntryData> entries_[DatCount];
    // Flattened Item arrays for GetEntry (built from EntryData)
    std::vector<std::vector<Item>> items_[DatCount];

    // Token text table
    uint32_t token_count_ = 0;
    std::vector<char> blob_;
    std::vector<const char32_t*> token_strs_;
    std::unordered_set<TokenID> token_set_;
};

} // namespace sime
