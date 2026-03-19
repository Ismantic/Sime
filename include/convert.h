#pragma once

#include "unit.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

class TrieConverter {
public:
    bool Load(const std::filesystem::path& path);
    bool Write(const std::filesystem::path& output);
    std::size_t Count() const;
    std::vector<std::string> Dump() const;

private:
    struct Phone {
        std::string str;
        std::uint8_t cost = 0;
    };

    struct Node {
        std::map<std::uint32_t, Node*> moves;
        std::map<std::uint32_t, std::uint8_t> costs;
    };

    struct NodeSize {
        std::uint32_t i = 0;
        std::size_t size = 0;
    };

    bool ParseLine(const std::string& line,
                   std::string& text,
                   std::vector<Phone>& phones) const;

    void InsertUnits(std::uint32_t id, std::uint8_t cost, const std::vector<Unit>& units);

    Node* CreateNode();
    Node* InsertMove(Node* node, Unit unit);
    void InsertText(Node* node, std::uint32_t id, std::uint8_t cost);

    std::size_t SerializeTree(std::vector<char>& buffer);
    std::size_t WriteStrTable(std::vector<char>& buffer);
    void SerializeNode(const Node* node,
                       const NodeSize& metrics,
                       std::vector<char>& buffer);

    Node* root_ = nullptr;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<Node*> order_;
    std::vector<std::string> lexicon_;
    std::unordered_map<const Node*, NodeSize> metrics_;
};

struct ConvertOptions {
    std::filesystem::path input;
    std::filesystem::path output;
};

void ConvertRun(const ConvertOptions& options);

} // namespace sime
