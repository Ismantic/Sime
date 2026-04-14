#include "convert.h"
#include "common.h"
#include "ustr.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace sime {

namespace {

struct TrieNodeHeader {
    std::uint16_t move_count = 0;
    std::uint16_t count = 0;
};

struct TrieMove {
    std::uint32_t unit = 0;
    std::uint32_t next = 0;
};


bool IsWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool IsUnitChar(char ch) {
    return (ch >= 'a' && ch <= 'z') || ch == '\'' || ch == '"' ||
           (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

} // namespace

bool DictConverter::LoadTokens(const std::filesystem::path& path) {
    nodes_.clear();
    order_.clear();
    metrics_.clear();
    tokens_.clear();
    token_ids_.clear();
    root_ = CreateNode();

    TokenMap map;
    if (!LoadTokenMap(path, map)) {
        return false;
    }
    tokens_ = std::move(map.tokens);
    token_ids_ = std::move(map.ids);
    return true;
}

bool DictConverter::Load(const std::filesystem::path& path) {
    if (root_ == nullptr) {
        return false;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    std::string token;
    std::vector<std::string> unit_strs;
    while (std::getline(in, line)) {
        if (!ParseLine(line, token, unit_strs)) {
            continue;
        }
        std::vector<std::uint32_t> id_group;
        auto it = token_ids_.find(token);
        if (it != token_ids_.end()) {
            id_group.push_back(it->second);
        } else {
            // Token not in vocab — decompose by pieces into token IDs.
            // e.g. "iPhone" with pieces "i'Ph'one" → {id_i, id_Ph, id_one}
            // Use the first unit_str to determine the decomposition.
            if (unit_strs.empty()) continue;
            const auto& u = unit_strs[0];
            std::size_t pos = 0;
            bool valid = true;
            while (pos <= u.size()) {
                std::size_t next = u.find('\'', pos);
                std::string seg(u, pos, next == std::string::npos
                                        ? std::string::npos : next - pos);
                if (!seg.empty()) {
                    auto cit = token_ids_.find(seg);
                    if (cit == token_ids_.end()) { valid = false; break; }
                    id_group.push_back(cit->second);
                }
                if (next == std::string::npos) break;
                pos = next + 1;
            }
            if (!valid || id_group.empty()) continue;
        }
        for (const auto& u : unit_strs) {
            // Split on apostrophe, register each piece
            std::vector<Unit> units;
            std::size_t pos = 0;
            while (pos <= u.size()) {
                std::size_t next = u.find('\'', pos);
                std::string_view seg = std::string_view(u).substr(
                    pos, next == std::string::npos
                             ? std::string::npos
                             : next - pos);
                if (!seg.empty()) {
                    units.push_back(piece_.Register(seg));
                }
                if (next == std::string::npos) break;
                pos = next + 1;
            }
            if (units.empty()) continue;
            InsertUnits(id_group, units);
        }
    }
    return true;
}

bool DictConverter::ParseLine(const std::string& line,
                              std::string& token,
                              std::vector<std::string>& units) const {
    units.clear();
    token.clear();
    if (line.empty() || line[0] == '\n') {
        return false;
    }

    const char* ptr = line.c_str();
    while (*ptr && IsWhitespace(*ptr)) {
        ++ptr;
    }
    if (*ptr == '\0') {
        return false;
    }
    const char* start = ptr;
    while (*ptr && !IsWhitespace(*ptr)) {
        ++ptr;
    }
    token.assign(start, ptr - start);

    std::set<std::string> unique;
    while (*ptr) {
        while (*ptr && IsWhitespace(*ptr)) {
            ++ptr;
        }
        if (*ptr == '\0') {
            break;
        }
        const char* part_start = ptr;
        while (*ptr && !IsWhitespace(*ptr)) {
            ++ptr;
        }
        std::string part(part_start, ptr - part_start);
        std::size_t pos = 0;
        while (pos < part.size() && IsUnitChar(part[pos])) {
            ++pos;
        }
        if (pos == 0 || pos < part.size()) {
            continue;
        }
        unique.insert(std::move(part));
    }

    units.assign(unique.begin(), unique.end());
    return !units.empty();
}

void DictConverter::InsertUnits(const std::vector<std::uint32_t>& ids,
                                const std::vector<Unit>& units) {
    if (units.empty() || ids.empty() || root_ == nullptr) {
        return;
    }
    Node* node = root_;
    for (const auto& u : units) {
        node = InsertMove(node, u);
    }
    InsertText(node, ids);
}

DictConverter::Node* DictConverter::CreateNode() {
    auto node = std::make_unique<Node>();
    Node* raw = node.get();
    nodes_.push_back(std::move(node));
    order_.push_back(raw);
    return raw;
}

DictConverter::Node* DictConverter::InsertMove(Node* node, Unit u) {
    auto it = node->moves.find(u.value);
    if (it != node->moves.end()) {
        return it->second;
    }
    Node* c = CreateNode();
    node->moves[u.value] = c;
    return c;
}

void DictConverter::InsertText(Node* node,
                               const std::vector<std::uint32_t>& ids) {
    node->ids.insert(ids);
}

std::size_t DictConverter::Count() const {
    return tokens_.size();
}

std::vector<std::string> DictConverter::Dump() const {
    std::vector<std::string> result;
    result.reserve(tokens_.size());
    for (const auto& entry : tokens_) {
        result.push_back(entry);
    }
    return result;
}

std::size_t DictConverter::SerializeTree(std::vector<char>& buffer) {
    metrics_.clear();
    const std::uint32_t header = 4 * sizeof(std::uint32_t);
    std::uint32_t offset = header;
    for (Node* node : order_) {
        std::size_t total_tokens = 0;
        for (const auto& group : node->ids) {
            total_tokens += group.size();
        }
        NodeSize metrics;
        metrics.size =
            sizeof(TrieNodeHeader) +
            node->moves.size() * sizeof(TrieMove) +
            total_tokens * sizeof(std::uint32_t);
        metrics.i = offset;
        metrics_[node] = metrics;
        offset += static_cast<std::uint32_t>(metrics.size);
    }
    buffer.assign(offset, 0);
    for (Node* node : order_) {
        SerializeNode(node, metrics_.at(node), buffer);
    }
    return offset - header;
}

void DictConverter::SerializeNode(const Node* node,
                                  const NodeSize& metrics,
                                  std::vector<char>& buffer) {
    auto* base =
        reinterpret_cast<TrieNodeHeader*>(buffer.data() + metrics.i);
    TrieNodeHeader header{};
    std::size_t total_tokens = 0;
    for (const auto& group : node->ids) {
        total_tokens += group.size();
    }
    auto entry_count =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            total_tokens, static_cast<std::size_t>(0xFFFF)));
    auto move_count =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            node->moves.size(), static_cast<std::size_t>(0xFFFF)));
    header.count = static_cast<std::uint16_t>(entry_count);
    header.move_count = static_cast<std::uint16_t>(move_count);
    *base = header;

    auto* moves = reinterpret_cast<TrieMove*>(base + 1);
    std::size_t idx = 0;
    for (const auto& [unit, down] : node->moves) {
        moves[idx].unit = unit;
        moves[idx].next = metrics_.at(down).i;
        ++idx;
    }

    auto* entries = reinterpret_cast<std::uint32_t*>(moves + node->moves.size());
    idx = 0;
    for (const auto& group : node->ids) {
        for (std::size_t j = 0; j < group.size(); ++j) {
            std::uint32_t val = group[j];
            if (j + 1 == group.size()) {
                val |= GroupEnd;
            }
            entries[idx++] = val;
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

bool DictConverter::Write(const std::filesystem::path& output) {
    piece_.BuildMaps();

    constexpr std::size_t HeaderSize = 4 * sizeof(std::uint32_t);
    std::vector<char> buffer;
    auto tree_size = SerializeTree(buffer);
    WriteTokenTable(buffer);
    std::uint32_t piece_offset =
        static_cast<std::uint32_t>(buffer.size());
    piece_.Serialize(buffer);

    std::uint32_t token_count =
        static_cast<std::uint32_t>(tokens_.size());
    std::uint32_t node_count =
        static_cast<std::uint32_t>(order_.size());
    std::uint32_t token_offset =
        static_cast<std::uint32_t>(tree_size + HeaderSize);
    auto* header = reinterpret_cast<std::uint32_t*>(buffer.data());
    header[0] = node_count;
    header[1] = token_count;
    header[2] = token_offset;
    header[3] = piece_offset;

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(buffer.data(),
              static_cast<std::streamsize>(buffer.size()));
    return out.good();
}

} // namespace sime
