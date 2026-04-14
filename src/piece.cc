#include "piece.h"

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

Unit PieceTable::Register(std::string_view piece) {
    std::string key(piece);
    auto it = piece_to_id_.find(key);
    if (it != piece_to_id_.end()) {
        return Unit(it->second);
    }
    auto id = static_cast<std::uint32_t>(pieces_.size());
    pieces_.push_back(key);
    piece_to_id_[key] = id;
    if (IsKnownPinyin(key)) {
        pinyin_ids_.insert(id);
    }
    return Unit(id);
}

void PieceTable::BuildMaps() {
    piece_map_.clear();
    num_map_.clear();
    max_len_ = 0;
    for (std::uint32_t id = 1; id < pieces_.size(); ++id) {
        Unit u(id);
        const auto& text = pieces_[id];
        piece_map_[text].push_back(u);
        if (text.size() > max_len_) max_len_ = text.size();
        // TODO: 不确定小写key映射是否合适，先这样处理大小写匹配。
        std::string lower;
        bool has_upper = false;
        for (char ch : text) {
            char lc = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
            lower.push_back(lc);
            if (lc != ch) has_upper = true;
        }
        if (has_upper) {
            piece_map_[lower].push_back(u);
        }
        std::string nums = PieceToNum(text);
        if (!nums.empty()) {
            num_map_[nums].push_back(u);
        }
    }
}

void PieceTable::Serialize(std::vector<char>& buffer) const {
    // Format: [count:u32] [piece0_len:u16 piece0_data...] ...
    // piece 0 is empty (reserved), still serialized for simplicity.
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
}

bool PieceTable::Deserialize(const char* data, std::size_t size) {
    pieces_.clear();
    piece_to_id_.clear();
    pinyin_ids_.clear();

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

bool PieceTable::IsKnownPinyin(const std::string& text) {
    // Binary search in the sorted PinyinDict.
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
