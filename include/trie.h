#pragma once 

#include "common.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

class Trie {
public:
    struct Move {
        Unit unit{};
        std::uint32_t next = 0;
    };

    struct Node {
        std::uint16_t move_count = 0;
        std::uint16_t count = 0;

        const Move* GetMove() const;
        const std::uint32_t* GetToken() const;
    };

    Trie() = default;
    ~Trie();

    bool Load(const std::filesystem::path& path);
    void Clear();

    const Node* Root() const;
    const Node* DoMove(const Node* node, Unit u) const;
    const std::uint32_t* GetToken(const Node* node, uint32_t& count) const;

    uint32_t NodeCount() const;
    uint32_t TokenCount() const;

    const char32_t* TokenAt(uint32_t i) const;

private:
    const Node* NodeFrom(std::uint32_t i) const;
    std::uint32_t RootIndex() const;
    std::uint32_t TokenIndex() const;

    std::vector<char> blob_;
    std::vector<const char32_t*> token_strs_;
};

} // namespace sime
