#pragma once 

#include "common.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sime {

// Trie structures are stored in binary blob and frequently accessed
// Align to 8 bytes for optimal cache performance
struct alignas(8) Move {
    std::uint32_t i = 0;    // 4 bytes - child node index
    Unit unit{};            // 4 bytes - unit value
    // Total: 8 bytes (naturally aligned)
};

struct alignas(8) Entry {
    std::uint32_t i = 0;      // 4 bytes - token ID
    std::uint8_t cost = 0;    // 1 byte - cost
    std::uint8_t empty[3]{};  // 3 bytes - padding
    // Total: 8 bytes (naturally aligned)
};

struct alignas(4) Node {
    std::uint16_t count = 0;       // 2 bytes - number of entries
    std::uint16_t move_count = 0;  // 2 bytes - number of moves
    // Total: 4 bytes

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
