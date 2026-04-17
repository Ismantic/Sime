#pragma once

#include "piece.h"
#include "unit.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

class DictConverter {
public:
    bool LoadTokens(const std::filesystem::path& path);
    bool Load(const std::filesystem::path& path, bool en = false);
    bool Write(const std::filesystem::path& output);
    std::size_t Count() const;
    std::vector<std::string> Dump() const;

private:
    struct Node {
        std::map<std::uint32_t, Node*> moves;
        std::set<std::uint32_t> ids;
    };

    struct NodeSize {
        std::uint32_t i = 0;
        std::size_t size = 0;
    };

    bool ParseLine(const std::string& line,
                   std::string& token_col,
                   std::vector<std::string>& units) const;

    void InsertUnits(std::uint32_t id, const std::vector<Unit>& units);

    Node* CreateNode();
    Node* InsertMove(Node* node, Unit unit);
    void InsertText(Node* node, std::uint32_t id);

    std::size_t SerializeTree(std::vector<char>& buffer);
    std::size_t WriteTokenTable(std::vector<char>& buffer);
    void SerializeNode(const Node* node,
                       const NodeSize& metrics,
                       std::vector<char>& buffer);

    Node* root_ = nullptr;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<Node*> order_;
    std::vector<std::string> tokens_;
    std::unordered_map<std::string, std::uint32_t> token_ids_;
    std::unordered_map<const Node*, NodeSize> metrics_;
    PieceTable piece_;
};

} // namespace sime
