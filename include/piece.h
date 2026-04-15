#pragma once

#include "trie.h"
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

    // Prefix-query accelerators for tail expansion.
    // piece_dat_ / num_dat_ are Double-Array tries built from
    // piece_map_ / num_map_ keys. Their value is an index into
    // piece_dat_units_ / num_dat_units_ which holds the same
    // vector<Unit> as piece_map_[key] / num_map_[key].
    const trie::DoubleArray<std::uint32_t>& PieceDat() const { return piece_dat_; }
    const trie::DoubleArray<std::uint32_t>& NumDat() const { return num_dat_; }
    const std::vector<Unit>& UnitsByPieceDatIndex(std::uint32_t i) const;
    const std::vector<Unit>& UnitsByNumDatIndex(std::uint32_t i) const;

    std::size_t Size() const { return pieces_.size(); }
    std::size_t MaxLen() const { return max_len_; }

private:
    static char LetterToNum(char c);
    static std::string PieceToNum(const std::string& piece);
    static bool IsKnownPinyin(const std::string& text);

    void BuildDats();

    // ID 0 is reserved (invalid Unit). Pieces start at index 1.
    std::vector<std::string> pieces_;  // ID → string
    std::unordered_map<std::string, std::uint32_t> piece_to_id_;
    std::unordered_set<std::uint32_t> pinyin_ids_;

    PieceMap piece_map_;  // piece text (+ lowercase aliases) → Units
    PieceMap num_map_;    // digit string → Units
    std::size_t max_len_ = 0;  // longest piece string length

    trie::DoubleArray<std::uint32_t> piece_dat_;
    trie::DoubleArray<std::uint32_t> num_dat_;
    std::vector<std::vector<Unit>> piece_dat_units_;
    std::vector<std::vector<Unit>> num_dat_units_;
};

} // namespace sime
