#pragma once 

#include "common.h"
#include "piece.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

class Dict {
public:
    struct Move {
        Unit unit{};
        std::uint32_t next = 0;
    };

    struct Node {
        std::uint32_t move_count = 0;
        std::uint32_t count = 0;

        const Move* GetMove() const;
        const std::uint32_t* GetToken() const;
    };

    Dict() = default;
    ~Dict();

    bool Load(const std::filesystem::path& path);
    void Clear();

    const Node* Root() const;
    const Node* DoMove(const Node* node, Unit u) const;
    const std::uint32_t* GetToken(const Node* node, std::uint32_t& count) const;

    std::uint32_t NodeCount() const;
    std::uint32_t TokenCount() const;

    const char32_t* TokenAt(std::uint32_t i) const;
    TokenID TokenFromText(const std::u32string& text) const;

    const PieceTable& GetPieceTable() const { return piece_; }

    // Walk piece path, BFS subtree, collect Groups (token ID sequences).
    std::vector<std::vector<std::uint32_t>> GetGroups(
        std::string_view pieces, std::size_t num) const;

    // Reverse index: first TokenID → complete Group sequences containing it.
    const std::unordered_map<TokenID, std::vector<std::vector<TokenID>>>&
    TokenGroups() const { return token_groups_; }

    // Piece path for a trie node (e.g. "ni'hao" for 你好).
    const std::string& NodePieces(const Node* node) const {
        static const std::string empty;
        auto it = node_pieces_.find(node);
        return it != node_pieces_.end() ? it->second : empty;
    }

private:
    void BuildTokenGroups();
    const Node* NodeFrom(std::uint32_t i) const;
    std::uint32_t RootIndex() const;
    std::uint32_t TokenIndex() const;
    std::uint32_t PieceIndex() const;

    std::vector<char> blob_;
    std::vector<const char32_t*> token_strs_;
    PieceTable piece_;
    std::unordered_map<TokenID, std::vector<std::vector<TokenID>>> token_groups_;
    std::unordered_map<std::u32string, TokenID> token_ids_;
    std::unordered_map<const Node*, std::string> node_pieces_;
};

} // namespace sime
