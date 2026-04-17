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
    Unit Register(std::string_view piece, bool en = false);
    void BuildMaps();
    void Serialize(std::vector<char>& buffer) const;

    // --- Load mode (runtime) ---
    bool Deserialize(const char* data, std::size_t size);

    // --- Common ---
    Unit Encode(std::string_view piece) const;
    const char* Decode(Unit u) const;
    bool IsPinyin(Unit u) const;

    // Piece map (for InitNet walk)
    using PieceMap = std::unordered_map<std::string, std::vector<Unit>>;
    const PieceMap& GetPieceMap() const { return piece_map_; }
    const PieceMap& GetPieceMapEn() const { return piece_map_en_; }
    const PieceMap& GetNumMap() const { return num_map_; }
    const PieceMap& GetNumMapEn() const { return num_map_en_; }

    // DAT accelerators
    const trie::DoubleArray<std::uint32_t>& PieceDat() const { return piece_dat_; }
    const trie::DoubleArray<std::uint32_t>& PieceDatEn() const { return piece_dat_en_; }
    const trie::DoubleArray<std::uint32_t>& NumDat() const { return num_dat_; }
    const trie::DoubleArray<std::uint32_t>& NumDatEn() const { return num_dat_en_; }
    const std::vector<Unit>& UnitsByPieceDatIndex(std::uint32_t i) const;
    const std::vector<Unit>& UnitsByPieceDatEnIndex(std::uint32_t i) const;
    const std::vector<Unit>& UnitsByNumDatIndex(std::uint32_t i) const;
    const std::vector<Unit>& UnitsByNumDatEnIndex(std::uint32_t i) const;

    std::size_t Size() const { return pieces_.size(); }
    std::size_t MaxLen() const { return max_len_; }

private:
    static char LetterToNum(char c);
    static std::string PieceToNum(const std::string& piece);
    static bool IsInPinyinDict(const std::string& text);
    static bool IsKnownPinyin(const std::string& text);

    void BuildDats();

    std::vector<std::string> pieces_;
    std::unordered_map<std::string, std::uint32_t> piece_to_id_;
    std::unordered_set<std::uint32_t> pinyin_ids_;
    std::unordered_set<std::uint32_t> en_ids_;

    PieceMap piece_map_;      // pinyin text → Units
    PieceMap piece_map_en_;   // english text → Units
    PieceMap num_map_;        // pinyin digit → Units
    PieceMap num_map_en_;     // english digit → Units
    std::size_t max_len_ = 0;

    trie::DoubleArray<std::uint32_t> piece_dat_;
    trie::DoubleArray<std::uint32_t> piece_dat_en_;
    trie::DoubleArray<std::uint32_t> num_dat_;
    trie::DoubleArray<std::uint32_t> num_dat_en_;
    std::vector<std::vector<Unit>> piece_dat_units_;
    std::vector<std::vector<Unit>> piece_dat_en_units_;
    std::vector<std::vector<Unit>> num_dat_units_;
    std::vector<std::vector<Unit>> num_dat_en_units_;
};

} // namespace sime
