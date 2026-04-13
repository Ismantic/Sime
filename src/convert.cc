#include "convert.h"
#include "common.h"
#include "unit.h"
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

bool TrieConverter::Load(const std::filesystem::path& path) {
    nodes_.clear();
    order_.clear();
    metrics_.clear();
    tokens_.clear();
    root_ = CreateNode();
    UnitParser parser;

    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    std::string token;
    std::uint32_t next_id = StartToken;
    std::vector<std::string> unit_strs;
    while (std::getline(in, line)) {
        bool s = ParseLine(line, token, unit_strs);
        if (token.empty()) {
            continue;
        }
        std::uint32_t i = next_id++;
        if (tokens_.size() <= i) {
            tokens_.resize(i + 1);
        }
        auto& entry = tokens_[i];
        if (entry.empty()) {
            entry = token;
        }
        if (!s) {
            continue;
        }
        std::vector<Unit> units;
        std::vector<std::uint32_t> id_group = {i};
        for (const auto& u : unit_strs) {
            if (!parser.ParseUnits(u, units)) {
                continue;
            }
            InsertUnits(id_group, units);
        }
    }
    return true;
}

bool TrieConverter::LoadTokens(const std::filesystem::path& path) {
    if (root_ == nullptr) {
        return false;
    }

    // Build reverse lookup: token string → token ID
    std::unordered_map<std::string, std::uint32_t> token_ids;
    for (std::uint32_t i = 0; i < tokens_.size(); ++i) {
        if (!tokens_[i].empty()) {
            token_ids[tokens_[i]] = i;
        }
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        // Split line into pieces by whitespace
        std::vector<std::string> pieces;
        const char* ptr = line.c_str();
        while (*ptr) {
            while (*ptr && IsWhitespace(*ptr)) {
                ++ptr;
            }
            if (*ptr == '\0') {
                break;
            }
            const char* start = ptr;
            while (*ptr && !IsWhitespace(*ptr)) {
                ++ptr;
            }
            pieces.emplace_back(start, ptr - start);
        }
        if (pieces.empty()) {
            continue;
        }

        // Resolve each piece to its token ID
        std::vector<std::uint32_t> id_group;
        bool all_found = true;
        for (const auto& piece : pieces) {
            auto it = token_ids.find(piece);
            if (it == token_ids.end()) {
                all_found = false;
                break;
            }
            id_group.push_back(it->second);
        }
        if (!all_found || id_group.empty()) {
            continue;
        }

        // Build letter-level Unit path from concatenated lowercase word
        std::string full_word;
        for (const auto& piece : pieces) {
            for (char c : piece) {
                full_word += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
        }

        std::vector<Unit> units;
        bool valid = true;
        for (char c : full_word) {
            if (c < 'a' || c > 'z') {
                valid = false;
                break;
            }
            units.push_back(Unit::Letter(c));
        }
        if (!valid || units.empty()) {
            continue;
        }

        InsertUnits(id_group, units);
    }
    return true;
}

bool TrieConverter::ParseLine(const std::string& line,
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

void TrieConverter::InsertUnits(const std::vector<std::uint32_t>& ids,
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

TrieConverter::Node* TrieConverter::CreateNode() {
    auto node = std::make_unique<Node>();
    Node* raw = node.get();
    nodes_.push_back(std::move(node));
    order_.push_back(raw);
    return raw;
}

TrieConverter::Node* TrieConverter::InsertMove(Node* node, Unit u) {
    auto it = node->moves.find(u.value);
    if (it != node->moves.end()) {
        return it->second;
    }
    Node* c = CreateNode();
    node->moves[u.value] = c;
    return c;
}

void TrieConverter::InsertText(Node* node,
                               const std::vector<std::uint32_t>& ids) {
    node->ids.insert(ids);
}

std::size_t TrieConverter::Count() const {
    return tokens_.size();
}

std::vector<std::string> TrieConverter::Dump() const {
    std::vector<std::string> result;
    result.reserve(tokens_.size());
    for (const auto& entry : tokens_) {
        result.push_back(entry);
    }
    return result;
}

std::size_t TrieConverter::SerializeTree(std::vector<char>& buffer) {
    metrics_.clear();
    const std::uint32_t header = 3 * sizeof(std::uint32_t);
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

void TrieConverter::SerializeNode(const Node* node,
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

std::size_t TrieConverter::WriteTokenTable(std::vector<char>& buffer) {
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

bool TrieConverter::Write(const std::filesystem::path& output) {
    std::vector<char> buffer;
    auto tree_size = SerializeTree(buffer);
    WriteTokenTable(buffer);
    std::uint32_t token_count =
        static_cast<std::uint32_t>(tokens_.size());
    std::uint32_t node_count =
        static_cast<std::uint32_t>(order_.size());
    std::uint32_t token_offset =
        static_cast<std::uint32_t>(tree_size + 3 * sizeof(std::uint32_t));
    auto* header = reinterpret_cast<std::uint32_t*>(buffer.data());
    header[0] = node_count;
    header[1] = token_count;
    header[2] = token_offset;

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(buffer.data(),
              static_cast<std::streamsize>(buffer.size()));
    return out.good();
}

} // namespace sime
