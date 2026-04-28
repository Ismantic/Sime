#pragma once

#include "common.h"
#include "trie.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sime {

class Dict {
public:
    enum DatType : int {
        LetterPinyin = 0,
        LetterEn = 1,
        DatCount = 2,
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
    Dict(const Dict&) = delete;
    Dict& operator=(const Dict&) = delete;

    bool Load(const std::filesystem::path& path);
    void Clear();

    const trie::DoubleArray& Dat(DatType type) const { return dats_[type]; }
    Entry GetEntry(DatType type, uint32_t index) const;

    // Cache trim. See trie::DoubleArray::ClearSepCache / ResetSepCache.
    void ClearSepCaches() const {
        for (int t = 0; t < DatCount; ++t) dats_[t].ClearSepCache();
    }
    void ResetSepCaches() const {
        for (int t = 0; t < DatCount; ++t) dats_[t].ResetSepCache();
    }

    const char32_t* TokenAt(uint32_t id) const;
    uint32_t TokenCount() const { return token_count_; }

    // Static pinyin utilities (backed by dict.inc)
    static bool IsKnownPinyin(const std::string& text);
    // True iff some longer pinyin final has `text` as a strict prefix
    // (e.g. "xu" → true since "xue"/"xun"/"xuan" exist; "xue" → false).
    static bool IsExtendablePinyin(const std::string& text);
    // T9 analog: digits is the T9 form of *some* known pinyin syllable.
    static bool IsKnownT9Syllable(std::string_view digits);
    // T9 analog of IsExtendablePinyin: true iff some longer pinyin
    // final's T9 form has `digits` as a strict prefix.
    static bool IsExtendableT9Syllable(std::string_view digits);
    static std::vector<std::string> T9PinyinSyllables(
        std::string_view digits, std::size_t limit);
    static char LetterToNum(char c);
    static std::string LettersToNums(std::string_view letters);
    static const char* NumToLetters(uint8_t digit);
    static const char* NumToLettersLower(uint8_t digit);

private:
    trie::DoubleArray dats_[DatCount];

    // Side table: offset into the mmap'd file for each entry (zero-copy).
    std::vector<uint32_t> side_offsets_[DatCount];
    mutable std::vector<Item> scratch_;  // reused by GetEntry

    // Token text table — pointers into mmap'd memory.
    uint32_t token_count_ = 0;
    std::vector<const char32_t*> token_strs_;

    // mmap state — replaces the heap-loaded blob_ vector.
    void* mmap_addr_ = nullptr;
    std::size_t mmap_len_ = 0;
};

} // namespace sime
