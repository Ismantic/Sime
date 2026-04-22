#include "dict.h"

#include <cstddef>
#include <cstring>
#include <fstream>

namespace sime {

namespace {

struct DictEntry {
    const char* text;
    std::uint32_t value;
};

const DictEntry PinyinDict[] = {
#include "dict.inc"
};

constexpr std::size_t PinyinDictSize = sizeof(PinyinDict) / sizeof(PinyinDict[0]);

} // namespace

Dict::~Dict() { Clear(); }

void Dict::Clear() {
    for (int t = 0; t < DatCount; ++t) {
        dats_[t] = trie::DoubleArray();
        entries_[t].clear();
        items_[t].clear();
    }
    token_count_ = 0;
    blob_.clear();
    token_strs_.clear();
    token_set_.clear();
}

bool Dict::Load(const std::filesystem::path& path) {
    Clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (size < 5 * sizeof(uint32_t)) return false;

    blob_.resize(size);
    if (!in.read(blob_.data(), static_cast<std::streamsize>(size))) {
        Clear();
        return false;
    }

    // Parse header
    const auto* header = reinterpret_cast<const uint32_t*>(blob_.data());
    token_count_ = header[0];
    uint32_t token_offset = header[1];
    uint32_t section_offsets[DatCount];
    for (int t = 0; t < DatCount; ++t)
        section_offsets[t] = header[2 + t];
    uint32_t total_size = header[4];
    if (total_size != size) { Clear(); return false; }

    // Parse token text table
    token_strs_.reserve(token_count_);
    auto p = reinterpret_cast<const char32_t*>(blob_.data() + token_offset);
    for (uint32_t i = 0; i < token_count_; ++i) {
        token_strs_.push_back(p);
        while (*p++) {}
    }

    // Parse 4 DAT sections
    for (int t = 0; t < DatCount; ++t) {
        std::size_t offset = section_offsets[t];
        if (offset >= size) continue;

        // Deserialize DAT
        if (!dats_[t].Deserialize(blob_.data() + offset, size - offset)) {
            continue;
        }
        // Skip past DAT data to side table
        // DAT serialized size: 4 bytes (size) + size * 10 bytes (units)
        uint32_t dat_size = 0;
        std::memcpy(&dat_size, blob_.data() + offset, sizeof(dat_size));
        offset += sizeof(dat_size) + dat_size * 10;

        if (offset + sizeof(uint32_t) > size) continue;

        // Parse side table
        uint32_t entry_count = 0;
        std::memcpy(&entry_count, blob_.data() + offset, sizeof(entry_count));
        offset += sizeof(entry_count);

        entries_[t].resize(entry_count);
        items_[t].resize(entry_count);

        for (uint32_t e = 0; e < entry_count; ++e) {
            if (offset + sizeof(uint16_t) > size) break;
            uint16_t item_count = 0;
            std::memcpy(&item_count, blob_.data() + offset, sizeof(item_count));
            offset += sizeof(item_count);

            auto& entry = entries_[t][e];
            entry.ids.reserve(item_count);
            entry.pieces.reserve(item_count);

            auto& item_vec = items_[t][e];
            item_vec.reserve(item_count);

            for (uint16_t j = 0; j < item_count; ++j) {
                if (offset + sizeof(TokenID) + sizeof(uint16_t) > size) break;

                TokenID id = 0;
                std::memcpy(&id, blob_.data() + offset, sizeof(id));
                offset += sizeof(id);

                uint16_t pieces_len = 0;
                std::memcpy(&pieces_len, blob_.data() + offset, sizeof(pieces_len));
                offset += sizeof(pieces_len);

                if (offset + pieces_len > size) break;

                entry.ids.push_back(id);
                entry.pieces.emplace_back(blob_.data() + offset, pieces_len);
                offset += pieces_len;

                token_set_.insert(id);
            }

            // Build Item array for GetEntry
            for (std::size_t j = 0; j < entry.ids.size(); ++j) {
                item_vec.push_back({entry.ids[j], entry.pieces[j].c_str()});
            }
        }
    }

    return true;
}

Dict::Entry Dict::GetEntry(DatType type, uint32_t index) const {
    if (index >= items_[type].size()) return {nullptr, 0};
    const auto& vec = items_[type][index];
    return {vec.data(), static_cast<uint32_t>(vec.size())};
}

const char32_t* Dict::TokenAt(uint32_t i) const {
    if (i >= token_strs_.size()) return nullptr;
    return token_strs_[i];
}

bool Dict::IsKnownPinyin(const std::string& text) {
    constexpr uint32_t FinalMask = 0xFFF;
    std::size_t left = 0;
    std::size_t right = PinyinDictSize;
    while (left < right) {
        std::size_t mid = left + (right - left) / 2;
        int cmp = std::strcmp(text.c_str(), PinyinDict[mid].text);
        if (cmp == 0) return (PinyinDict[mid].value & FinalMask) != 0;
        if (cmp > 0) left = mid + 1;
        else right = mid;
    }
    return false;
}

char Dict::LetterToNum(char c) {
    switch (c) {
    case 'a': case 'b': case 'c':
    case 'A': case 'B': case 'C': return '2';
    case 'd': case 'e': case 'f':
    case 'D': case 'E': case 'F': return '3';
    case 'g': case 'h': case 'i':
    case 'G': case 'H': case 'I': return '4';
    case 'j': case 'k': case 'l':
    case 'J': case 'K': case 'L': return '5';
    case 'm': case 'n': case 'o':
    case 'M': case 'N': case 'O': return '6';
    case 'p': case 'q': case 'r': case 's':
    case 'P': case 'Q': case 'R': case 'S': return '7';
    case 't': case 'u': case 'v':
    case 'T': case 'U': case 'V': return '8';
    case 'w': case 'x': case 'y': case 'z':
    case 'W': case 'X': case 'Y': case 'Z': return '9';
    default: return 0;
    }
}

std::string Dict::LettersToNums(std::string_view letters) {
    std::string result;
    for (char ch : letters) {
        char d = LetterToNum(ch);
        if (d != 0) result.push_back(d);
    }
    return result;
}

const char* Dict::NumToLetters(uint8_t digit) {
    switch (digit) {
    case '2': return "abcABC";
    case '3': return "defDEF";
    case '4': return "ghiGHI";
    case '5': return "jklJKL";
    case '6': return "mnoMNO";
    case '7': return "pqrsPQRS";
    case '8': return "tuvTUV";
    case '9': return "wxyzWXYZ";
    default: return nullptr;
    }
}

} // namespace sime
