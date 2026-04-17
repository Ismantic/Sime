#include "piece.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace sime {

namespace {

struct DictEntry {
    const char* text;
    std::uint32_t value;  // unused, kept for dict.inc format compatibility
};

const DictEntry PinyinDict[] = {
#include "dict.inc"
};

constexpr std::size_t PinyinDictSize = sizeof(PinyinDict) / sizeof(PinyinDict[0]);

} // namespace

PieceTable::PieceTable() {
    // ID 0 is reserved (invalid).
    pieces_.emplace_back();
}

Unit PieceTable::Register(std::string_view piece, bool en) {
    std::string key(piece);
    auto it = piece_to_id_.find(key);
    if (it != piece_to_id_.end()) {
        if (en) en_ids_.insert(it->second);
        return Unit(it->second);
    }
    auto id = static_cast<std::uint32_t>(pieces_.size());
    pieces_.push_back(key);
    piece_to_id_[key] = id;
    if (IsKnownPinyin(key)) {
        pinyin_ids_.insert(id);
    }
    if (en) en_ids_.insert(id);
    return Unit(id);
}

void PieceTable::BuildMaps() {
    piece_map_.clear();
    piece_map_en_.clear();
    num_map_.clear();
    num_map_en_.clear();
    max_len_ = 0;
    for (std::uint32_t id = 1; id < pieces_.size(); ++id) {
        Unit u(id);
        const auto& text = pieces_[id];
        if (en_ids_.count(id)) {
            piece_map_en_[text].push_back(u);
        }
        if (!en_ids_.count(id) || IsInPinyinDict(text)) {
            piece_map_[text].push_back(u);
        }
        if (text.size() > max_len_) max_len_ = text.size();
        std::string nums = PieceToNum(text);
        if (!nums.empty()) {
            if (en_ids_.count(id)) {
                num_map_en_[nums].push_back(u);
            }
            if (!en_ids_.count(id) || IsInPinyinDict(text)) {
                num_map_[nums].push_back(u);
            }
        }
    }
    BuildDats();
}

void PieceTable::BuildDats() {
    // Build DAT for piece_map_ keys (pinyin).
    piece_dat_ = trie::DoubleArray<std::uint32_t>();
    piece_dat_units_.clear();
    if (!piece_map_.empty()) {
        std::vector<std::string> keys;
        keys.reserve(piece_map_.size());
        for (const auto& kv : piece_map_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());

        std::vector<std::uint32_t> values;
        values.reserve(keys.size());
        piece_dat_units_.reserve(keys.size());
        for (std::uint32_t i = 0; i < keys.size(); ++i) {
            values.push_back(i);
            piece_dat_units_.push_back(piece_map_.at(keys[i]));
        }
        piece_dat_.Build(keys, values);
    }

    // Build DAT for piece_map_en_ keys (english).
    piece_dat_en_ = trie::DoubleArray<std::uint32_t>();
    piece_dat_en_units_.clear();
    if (!piece_map_en_.empty()) {
        std::vector<std::string> keys;
        keys.reserve(piece_map_en_.size());
        for (const auto& kv : piece_map_en_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());

        std::vector<std::uint32_t> values;
        values.reserve(keys.size());
        piece_dat_en_units_.reserve(keys.size());
        for (std::uint32_t i = 0; i < keys.size(); ++i) {
            values.push_back(i);
            piece_dat_en_units_.push_back(piece_map_en_.at(keys[i]));
        }
        piece_dat_en_.Build(keys, values);
    }

    // Build DAT for num_map_ keys (pinyin).
    num_dat_ = trie::DoubleArray<std::uint32_t>();
    num_dat_units_.clear();
    if (!num_map_.empty()) {
        std::vector<std::string> keys;
        keys.reserve(num_map_.size());
        for (const auto& kv : num_map_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());

        std::vector<std::uint32_t> values;
        values.reserve(keys.size());
        num_dat_units_.reserve(keys.size());
        for (std::uint32_t i = 0; i < keys.size(); ++i) {
            values.push_back(i);
            num_dat_units_.push_back(num_map_.at(keys[i]));
        }
        num_dat_.Build(keys, values);
    }

    // Build DAT for num_map_en_ keys (english).
    num_dat_en_ = trie::DoubleArray<std::uint32_t>();
    num_dat_en_units_.clear();
    if (!num_map_en_.empty()) {
        std::vector<std::string> keys;
        keys.reserve(num_map_en_.size());
        for (const auto& kv : num_map_en_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());

        std::vector<std::uint32_t> values;
        values.reserve(keys.size());
        num_dat_en_units_.reserve(keys.size());
        for (std::uint32_t i = 0; i < keys.size(); ++i) {
            values.push_back(i);
            num_dat_en_units_.push_back(num_map_en_.at(keys[i]));
        }
        num_dat_en_.Build(keys, values);
    }
}

const std::vector<Unit>& PieceTable::UnitsByPieceDatIndex(std::uint32_t i) const {
    static const std::vector<Unit> empty;
    if (i >= piece_dat_units_.size()) return empty;
    return piece_dat_units_[i];
}

const std::vector<Unit>& PieceTable::UnitsByPieceDatEnIndex(std::uint32_t i) const {
    static const std::vector<Unit> empty;
    if (i >= piece_dat_en_units_.size()) return empty;
    return piece_dat_en_units_[i];
}

const std::vector<Unit>& PieceTable::UnitsByNumDatIndex(std::uint32_t i) const {
    static const std::vector<Unit> empty;
    if (i >= num_dat_units_.size()) return empty;
    return num_dat_units_[i];
}

const std::vector<Unit>& PieceTable::UnitsByNumDatEnIndex(std::uint32_t i) const {
    static const std::vector<Unit> empty;
    if (i >= num_dat_en_units_.size()) return empty;
    return num_dat_en_units_[i];
}

void PieceTable::Serialize(std::vector<char>& buffer) const {
    // Format: [count:u32] [piece0_len:u16 piece0_data...] ...
    //         [en_count:u32] [en_id0:u32] [en_id1:u32] ...
    auto count = static_cast<std::uint32_t>(pieces_.size());
    std::size_t offset = buffer.size();
    buffer.resize(offset + sizeof(count));
    std::memcpy(buffer.data() + offset, &count, sizeof(count));

    for (const auto& p : pieces_) {
        auto len = static_cast<std::uint16_t>(p.size());
        std::size_t pos = buffer.size();
        buffer.resize(pos + sizeof(len) + len);
        std::memcpy(buffer.data() + pos, &len, sizeof(len));
        std::memcpy(buffer.data() + pos + sizeof(len), p.data(), len);
    }

    // Append en_ids
    auto en_count = static_cast<std::uint32_t>(en_ids_.size());
    std::size_t pos = buffer.size();
    buffer.resize(pos + sizeof(en_count) + en_count * sizeof(std::uint32_t));
    std::memcpy(buffer.data() + pos, &en_count, sizeof(en_count));
    pos += sizeof(en_count);
    for (auto id : en_ids_) {
        std::memcpy(buffer.data() + pos, &id, sizeof(id));
        pos += sizeof(id);
    }
}

bool PieceTable::Deserialize(const char* data, std::size_t size) {
    pieces_.clear();
    piece_to_id_.clear();
    pinyin_ids_.clear();
    en_ids_.clear();

    if (size < sizeof(std::uint32_t)) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, data, sizeof(count));
    std::size_t offset = sizeof(count);

    pieces_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (offset + sizeof(std::uint16_t) > size) return false;
        std::uint16_t len = 0;
        std::memcpy(&len, data + offset, sizeof(len));
        offset += sizeof(len);
        if (offset + len > size) return false;
        std::string piece(data + offset, len);
        offset += len;

        auto id = static_cast<std::uint32_t>(pieces_.size());
        pieces_.push_back(piece);
        if (!piece.empty()) {
            piece_to_id_[piece] = id;
            if (IsKnownPinyin(piece)) {
                pinyin_ids_.insert(id);
            }
        }
    }

    // Read en_ids if present (backward compatible: old files lack this)
    if (offset + sizeof(std::uint32_t) <= size) {
        std::uint32_t en_count = 0;
        std::memcpy(&en_count, data + offset, sizeof(en_count));
        offset += sizeof(en_count);
        for (std::uint32_t i = 0; i < en_count && offset + sizeof(std::uint32_t) <= size; ++i) {
            std::uint32_t id = 0;
            std::memcpy(&id, data + offset, sizeof(id));
            offset += sizeof(id);
            en_ids_.insert(id);
        }
    }

    BuildMaps();
    return true;
}

Unit PieceTable::Encode(std::string_view piece) const {
    std::string key(piece);
    auto it = piece_to_id_.find(key);
    if (it != piece_to_id_.end()) {
        return Unit(it->second);
    }
    return Unit{};
}

const char* PieceTable::Decode(Unit u) const {
    if (u.value == 0 || u.value >= pieces_.size()) {
        return "";
    }
    return pieces_[u.value].c_str();
}

bool PieceTable::IsPinyin(Unit u) const {
    return pinyin_ids_.count(u.value) > 0;
}

bool PieceTable::IsInPinyinDict(const std::string& text) {
    std::size_t left = 0;
    std::size_t right = PinyinDictSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = std::strcmp(text.c_str(), PinyinDict[mid].text);
        if (cmp == 0) return true;
        if (cmp > 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return false;
}

bool PieceTable::IsKnownPinyin(const std::string& text) {
    // A complete pinyin syllable has a non-zero final (low 12 bits of value).
    // Bare initials like "b", "p", "zh" have final = 0 and are not complete.
    constexpr std::uint32_t FinalMask = 0xFFF;
    std::size_t left = 0;
    std::size_t right = PinyinDictSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = std::strcmp(text.c_str(), PinyinDict[mid].text);
        if (cmp == 0) return (PinyinDict[mid].value & FinalMask) != 0;
        if (cmp > 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return false;
}

char PieceTable::LetterToNum(char c) {
    switch (c) {
    case 'a': case 'b': case 'c': return '2';
    case 'd': case 'e': case 'f': return '3';
    case 'g': case 'h': case 'i': return '4';
    case 'j': case 'k': case 'l': return '5';
    case 'm': case 'n': case 'o': return '6';
    case 'p': case 'q': case 'r': case 's': return '7';
    case 't': case 'u': case 'v': return '8';
    case 'w': case 'x': case 'y': case 'z': return '9';
    default: return '0';
    }
}

std::string PieceTable::PieceToNum(const std::string& piece) {
    std::string result;
    for (char ch : piece) {
        char d = LetterToNum(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
        if (d != '0') {
            result.push_back(d);
        }
    }
    return result;
}

} // namespace sime
