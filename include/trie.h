#pragma once 

#include "common.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

struct Move {
    std::uint32_t i = 0;
    Unit unit{};
};

struct Entry {
    std::uint32_t i = 0;
    std::uint8_t cost = 0;
    std::uint8_t empty[3]{};
};

struct Node {
    std::uint16_t count = 0;
    std::uint16_t move_count = 0;

    const Move* GetMove() const;
    const Entry* GetEntry() const;
};

class Trie {
public:
    Trie() = default;
    ~Trie();

    bool Load(const std::filesystem::path& path);
    void Clear();

    const Node* Root() const;
    const Node* DoMove(const Node* node, Unit u) const;
    const Entry* GetEntry(const Node* node, uint32_t& count) const;

    const char32_t* StrAt(uint32_t i) const;

    uint32_t StrCount() const;
    uint32_t NodeCount() const;

private:
    const Node* NodeFrom(std::uint32_t i) const;
    std::uint32_t RootIndex() const;
    std::uint32_t StrIndex() const;

    std::vector<char> blob_;
    std::vector<const char32_t*> wstrs_;
};

} /// namespace sime
