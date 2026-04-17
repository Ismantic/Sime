#include "dict.h"
#include "ustr.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>

namespace sime {

const Dict::Move* Dict::Node::GetMove() const {
    const char* p = reinterpret_cast<const char*>(this + 1);
    return reinterpret_cast<const Dict::Move*>(p);
}

const std::uint32_t* Dict::Node::GetToken() const {
    const char* p = 
        reinterpret_cast<const char*>(this + 1) + 
        sizeof(Dict::Move) * move_count;
    return reinterpret_cast<const std::uint32_t*>(p);
}

Dict::~Dict() { Clear(); }

void Dict::Clear() {
    blob_.clear();
    token_strs_.clear();
    token_groups_.clear();
    token_ids_.clear();
    node_pieces_.clear();
}

uint32_t Dict::NodeCount() const {
    if (blob_.size() < sizeof(std::uint32_t)) {
        return 0;
    }
    return *reinterpret_cast<const std::uint32_t*>(blob_.data());
}

uint32_t Dict::TokenCount() const {
    if (blob_.size() < 2 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[1];
}

std::uint32_t Dict::TokenIndex() const {
    if (blob_.size() < 3 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[2];
}

std::uint32_t Dict::PieceIndex() const {
    if (blob_.size() < 4 * sizeof(std::uint32_t)) {
        return 0;
    }
    return reinterpret_cast<const std::uint32_t*>(blob_.data())[3];
}

std::uint32_t Dict::RootIndex() const {
    return 4U * static_cast<std::uint32_t>(sizeof(std::uint32_t));
}

const Dict::Node* Dict::NodeFrom(std::uint32_t i) const {
    if (blob_.empty() || i < RootIndex() || i >= blob_.size()) {
        return nullptr;
    }
    const char* p = blob_.data() + i;
    return reinterpret_cast<const Dict::Node*>(p);
}

const Dict::Node* Dict::Root() const {
    return NodeFrom(RootIndex());
}

bool Dict::Load(const std::filesystem::path& path) {
    Clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return false;
    }
    blob_.resize(static_cast<std::size_t>(size));
    if (!in.read(blob_.data(), size)) {
        Clear();
        return false;
    }

    uint32_t count = TokenCount();
    token_strs_.reserve(count);
    const char* base = blob_.data() + TokenIndex();
    auto p = reinterpret_cast<const char32_t*>(base);
    for (uint32_t i = 0; i < count; ++i) {
        token_strs_.push_back(p);
        const char32_t* start = p;
        while (*p++) {}
        std::u32string key(start, static_cast<std::size_t>(p - 1 - start));
        if (!key.empty()) {
            token_ids_[key] = i;
        }
    }

    // Load piece table
    std::uint32_t pi = PieceIndex();
    if (pi > 0 && pi < blob_.size()) {
        if (!piece_.Deserialize(blob_.data() + pi,
                                blob_.size() - pi)) {
            Clear();
            return false;
        }
    }

    BuildTokenGroups();
    return true;
}

const Dict::Node* Dict::DoMove(const Dict::Node* node, Unit u) const {
    if (node == nullptr || node->move_count == 0) {
        return nullptr;
    }
    const auto* begin = node->GetMove();
    const auto* end = begin + node->move_count;
    const auto it = std::lower_bound(
        begin,
        end,
        u.value,
        [](const Dict::Move& move, std::uint32_t value) {
            return move.unit.value < value;
        });
    if (it == end || it->unit.value != u.value) {
        return nullptr;
    }
    return NodeFrom(it->next);
}

const std::uint32_t* Dict::GetToken(const Dict::Node* node, uint32_t& count) const {
    if (node == nullptr) {
        count = 0;
        return nullptr;
    } 
    count = node->count;
    return node->GetToken();
}

const char32_t* Dict::TokenAt(uint32_t i) const {
    if (i >= token_strs_.size()) {
        return nullptr;
    }
    return token_strs_[i];
}

TokenID Dict::TokenFromText(const std::u32string& text) const {
    auto it = token_ids_.find(text);
    if (it != token_ids_.end()) return it->second;
    return NotToken;
}

std::vector<std::vector<std::uint32_t>> Dict::GetGroups(
    std::string_view pieces, std::size_t num) const {
    std::vector<std::vector<std::uint32_t>> result;
    if (num == 0) return result;

    // Walk piece path to anchor node
    const Node* node = Root();
    if (!node) return result;

    std::size_t pos = 0;
    while (pos < pieces.size() && node) {
        // Skip apostrophe separators
        if (pieces[pos] == '\'') {
            ++pos;
            continue;
        }
        std::size_t next = pieces.find('\'', pos);
        std::string_view seg = pieces.substr(
            pos, next == std::string_view::npos ? std::string_view::npos
                                                : next - pos);
        if (seg.empty()) break;
        Unit u = piece_.Encode(seg);
        if (u.value == 0) return result;  // unknown piece
        node = DoMove(node, u);
        pos = (next == std::string_view::npos) ? pieces.size() : next + 1;
    }
    if (!node) return result;

    // BFS subtree, collect Groups
    std::vector<const Node*> queue;
    queue.push_back(node);
    std::size_t head = 0;
    while (head < queue.size() && result.size() < num) {
        const Node* cur = queue[head++];

        // Collect groups at this node (skip anchor itself)
        if (cur != node && cur->count > 0) {
            const std::uint32_t* tokens = cur->GetToken();
            std::uint32_t gi = 0;
            while (gi < cur->count && result.size() < num) {
                std::vector<std::uint32_t> group;
                do {
                    group.push_back(tokens[gi] & GroupTokenMask);
                    bool is_end = (tokens[gi] & GroupEnd) != 0;
                    ++gi;
                    if (is_end) break;
                } while (gi < cur->count);
                result.push_back(std::move(group));
            }
        }

        // Enqueue children
        const auto* moves = cur->GetMove();
        for (std::uint32_t i = 0; i < cur->move_count; ++i) {
            const Node* child = DoMove(cur, Unit(moves[i].unit.value));
            if (child) queue.push_back(child);
        }
    }
    return result;
}

void Dict::BuildTokenGroups() {
    token_groups_.clear();
    node_pieces_.clear();
    const Node* root = Root();
    if (!root) return;

    // DFS the entire trie, collect all Groups and piece paths.
    struct Frame {
        const Node* node;
        std::string pieces;
    };
    std::vector<Frame> stack;
    stack.push_back({root, ""});
    while (!stack.empty()) {
        auto [cur, pieces] = stack.back();
        stack.pop_back();
        if (!cur) continue;

        if (cur->count > 0) {
            node_pieces_[cur] = pieces;
            const std::uint32_t* tokens = cur->GetToken();
            std::uint32_t gi = 0;
            while (gi < cur->count) {
                std::vector<TokenID> group;
                do {
                    group.push_back(
                        static_cast<TokenID>(tokens[gi] & GroupTokenMask));
                    bool is_end = (tokens[gi] & GroupEnd) != 0;
                    ++gi;
                    if (is_end) break;
                } while (gi < cur->count);
                token_groups_[group[0]].push_back(group);
            }
        }

        const auto* moves = cur->GetMove();
        for (std::uint32_t i = 0; i < cur->move_count; ++i) {
            const Node* child = DoMove(cur, Unit(moves[i].unit.value));
            if (!child) continue;
            const char* seg = piece_.Decode(Unit(moves[i].unit.value));
            std::string child_pieces = pieces;
            if (!child_pieces.empty()) child_pieces += "'";
            child_pieces += (seg ? seg : "");
            stack.push_back({child, std::move(child_pieces)});
        }
    }
}

} // namespace sime
