#include "convert.h"
#include "ustr.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace sime {

namespace {

bool IsWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool IsUnitChar(char ch) {
    auto uc = static_cast<unsigned char>(ch);
    return (ch >= 'a' && ch <= 'z') || ch == '/' || ch == '\'' ||
           (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           uc >= 0x80;
}

bool ParseLine(const std::string& line,
               std::string& token_col,
               std::vector<std::string>& units) {
    token_col.clear();
    units.clear();
    if (line.empty()) return false;

    const char* ptr = line.c_str();

    // Column 1: Token
    while (*ptr && IsWhitespace(*ptr)) ++ptr;
    if (*ptr == '\0') return false;
    const char* start = ptr;
    while (*ptr && !IsWhitespace(*ptr)) ++ptr;
    token_col.assign(start, ptr - start);

    // Column 2+: Units (piece decompositions)
    std::set<std::string> unique;
    while (*ptr) {
        while (*ptr && IsWhitespace(*ptr)) ++ptr;
        if (*ptr == '\0') break;
        const char* part_start = ptr;
        while (*ptr && !IsWhitespace(*ptr)) ++ptr;
        std::string part(part_start, ptr - part_start);
        std::size_t pos = 0;
        while (pos < part.size() && IsUnitChar(part[pos])) ++pos;
        if (pos == 0 || pos < part.size()) continue;
        unique.insert(std::move(part));
    }

    units.assign(unique.begin(), unique.end());
    return !units.empty();
}

} // namespace

bool DictConverter::LoadTokens(const std::filesystem::path& path) {
    maps_[0].clear();
    maps_[1].clear();
    tokens_.clear();
    token_ids_.clear();

    TokenMap map;
    if (!LoadTokenMap(path, map)) return false;
    tokens_ = std::move(map.tokens);
    token_ids_ = std::move(map.ids);
    return true;
}

bool DictConverter::Load(const std::filesystem::path& path, bool en) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line, token_col;
    std::vector<std::string> unit_strs;
    std::size_t line_num = 0;
    std::size_t loaded = 0;

    while (std::getline(in, line)) {
        ++line_num;
        if (!ParseLine(line, token_col, unit_strs)) {
            if (!line.empty()) {
                std::cerr << "warning: skipping invalid line " << line_num
                          << ": " << line.substr(0, 40) << "\n";
            }
            continue;
        }
        auto cit = token_ids_.find(token_col);
        if (cit == token_ids_.end()) continue;
        TokenID id = cit->second;

        for (const auto& u : unit_strs) {
            // Split on '/' to get individual pieces
            std::vector<std::string> pieces;
            std::size_t pos = 0;
            while (pos <= u.size()) {
                std::size_t next = u.find('/', pos);
                std::string seg(u, pos, next == std::string::npos
                                        ? std::string::npos : next - pos);
                if (!seg.empty()) pieces.push_back(seg);
                if (next == std::string::npos) break;
                pos = next + 1;
            }
            if (pieces.empty()) continue;

            std::string pieces_str = PiecesToString(pieces);
            Item item{id, pieces_str};

            // Only store letter keys (with separator).
            // T9 digits are expanded to letters at query time.
            if (en) {
                std::string letter_key = PiecesToLetterKey(pieces);
                maps_[Dict::LetterEn][letter_key].push_back(item);
            } else {
                maps_[Dict::LetterPinyin][pieces_str].push_back(item);
            }
        }
        ++loaded;
    }
    std::cerr << "loaded " << loaded << " entries from " << path << "\n";
    return true;
}

std::string DictConverter::PiecesToLetterKey(
    const std::vector<std::string>& pieces) {
    std::string key;
    for (const auto& p : pieces) key += p;
    return key;
}

std::string DictConverter::PiecesToString(
    const std::vector<std::string>& pieces) {
    std::string s;
    for (const auto& p : pieces) {
        if (!s.empty()) s += '\'';
        s += p;
    }
    return s;
}

// Binary format:
//   Header: 5 x uint32_t
//     [0] token_count
//     [1] token_table_offset
//     [2..3] section_offsets[2] (each = DAT + side table)
//     [4] total_size
//   Token text table (char32_t, null-terminated per token)
//   4 x { DAT serialized | side table }
//
// Side table format:
//   entry_count: uint32_t
//   per entry:
//     item_count: uint16_t
//     per item:
//       token_id: uint32_t
//       pieces_len: uint16_t
//       pieces_data: char[pieces_len]

bool DictConverter::Write(const std::filesystem::path& output) {
    constexpr std::size_t HeaderSize = 5 * sizeof(uint32_t);
    std::vector<char> buffer;
    buffer.resize(HeaderSize);

    // Token text table
    auto token_offset = static_cast<uint32_t>(buffer.size());
    WriteTokenTable(buffer);

    // 4 DAT sections
    uint32_t section_offsets[Dict::DatCount];
    for (int t = 0; t < Dict::DatCount; ++t) {
        section_offsets[t] = static_cast<uint32_t>(buffer.size());

        const auto& map = maps_[t];
        if (map.empty()) {
            // Empty DAT: write size=0 DAT + entry_count=0
            trie::DoubleArray empty_dat;
            empty_dat.Serialize(buffer);
            uint32_t zero = 0;
            std::size_t pos = buffer.size();
            buffer.resize(pos + sizeof(zero));
            std::memcpy(buffer.data() + pos, &zero, sizeof(zero));
            continue;
        }

        // Build sorted keys and values
        std::vector<std::string> keys;
        keys.reserve(map.size());
        for (const auto& [k, _] : map) keys.push_back(k);
        // keys already sorted (std::map)

        std::vector<uint32_t> values;
        values.reserve(keys.size());
        for (uint32_t i = 0; i < keys.size(); ++i)
            values.push_back(i);

        // Build and serialize DAT
        trie::DoubleArray dat;
        dat.Build(keys, values);
        dat.Serialize(buffer);

        // Serialize side table
        WriteSideTable(map, values, keys, buffer);
    }

    // Fill header
    auto token_count = static_cast<uint32_t>(tokens_.size());
    auto total_size = static_cast<uint32_t>(buffer.size());
    auto* header = reinterpret_cast<uint32_t*>(buffer.data());
    header[0] = token_count;
    header[1] = token_offset;
    for (int t = 0; t < Dict::DatCount; ++t)
        header[2 + t] = section_offsets[t];
    header[4] = total_size;

    // Write file
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return out.good();
}

void DictConverter::WriteSideTable(
    const EntryMap& map,
    const std::vector<uint32_t>& /*dat_values*/,
    const std::vector<std::string>& dat_keys,
    std::vector<char>& buffer) {
    auto entry_count = static_cast<uint32_t>(dat_keys.size());
    std::size_t pos = buffer.size();
    buffer.resize(pos + sizeof(entry_count));
    std::memcpy(buffer.data() + pos, &entry_count, sizeof(entry_count));

    for (const auto& key : dat_keys) {
        const auto& items = map.at(key);
        auto item_count = static_cast<uint16_t>(items.size());
        pos = buffer.size();
        buffer.resize(pos + sizeof(item_count));
        std::memcpy(buffer.data() + pos, &item_count, sizeof(item_count));

        for (const auto& item : items) {
            // token_id
            pos = buffer.size();
            buffer.resize(pos + sizeof(item.id));
            std::memcpy(buffer.data() + pos, &item.id, sizeof(item.id));
            // pieces
            auto pieces_len = static_cast<uint16_t>(item.pieces.size());
            pos = buffer.size();
            buffer.resize(pos + sizeof(pieces_len) + pieces_len);
            std::memcpy(buffer.data() + pos, &pieces_len, sizeof(pieces_len));
            std::memcpy(buffer.data() + pos + sizeof(pieces_len),
                        item.pieces.data(), pieces_len);
        }
    }
}

std::size_t DictConverter::WriteTokenTable(std::vector<char>& buffer) {
    std::vector<char32_t> table;
    for (const auto& entry : tokens_) {
        if (entry.empty()) {
            table.push_back(0);
            continue;
        }
        std::u32string word = ustr::ToU32(entry);
        table.insert(table.end(), word.begin(), word.end());
        table.push_back(0);
    }
    const auto bytes = table.size() * sizeof(char32_t);
    std::size_t offset = buffer.size();
    buffer.resize(buffer.size() + bytes);
    std::memcpy(buffer.data() + offset, table.data(), bytes);
    return bytes;
}

} // namespace sime
