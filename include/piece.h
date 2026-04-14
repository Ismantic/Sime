#pragma once

#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sime {

class PieceTable {
public:
    PieceTable();

    // --- Build mode (converter) ---
    // Register a piece, assign a new ID if unseen. Returns its Unit.
    Unit Register(std::string_view piece);

    // Build num_map/piece_map after all pieces are registered.
    void BuildMaps();

    // Serialize piece table to buffer (for writing into trie file).
    void Serialize(std::vector<char>& buffer) const;

    // --- Load mode (runtime) ---
    // Deserialize piece table from buffer (loaded from trie file).
    bool Deserialize(const char* data, std::size_t size);

    // --- Common ---
    Unit Encode(std::string_view piece) const;
    const char* Decode(Unit u) const;
    bool IsPinyin(Unit u) const;

    // T9 digit mapping
    using PieceMap = std::unordered_map<std::string, std::vector<Unit>>;
    const PieceMap& GetPieceMap() const { return piece_map_; }
    const PieceMap& GetNumMap() const { return num_map_; }

    std::size_t Size() const { return pieces_.size(); }
    std::size_t MaxLen() const { return max_len_; }

private:
    static char LetterToNum(char c);
    static std::string PieceToNum(const std::string& piece);
    static bool IsKnownPinyin(const std::string& text);

    // ID 0 is reserved (invalid Unit). Pieces start at index 1.
    std::vector<std::string> pieces_;  // ID → string
    std::unordered_map<std::string, std::uint32_t> piece_to_id_;
    std::unordered_set<std::uint32_t> pinyin_ids_;

    PieceMap piece_map_;  // piece text (+ lowercase aliases) → Units
    PieceMap num_map_;    // digit string → Units
    std::size_t max_len_ = 0;  // longest piece string length
};

} // namespace sime
