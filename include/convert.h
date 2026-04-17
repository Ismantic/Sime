#pragma once

#include "common.h"
#include "dict.h"
#include "trie.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

class DictConverter {
public:
    bool LoadTokens(const std::filesystem::path& path);
    bool Load(const std::filesystem::path& path, bool en = false);
    bool Write(const std::filesystem::path& output);

    std::size_t Count() const { return tokens_.size(); }

private:
    // Per-key entry: multiple (token_id, pieces) pairs
    struct Item {
        TokenID id;
        std::string pieces;  // e.g., "ni'hao"
    };

    // key → items map, one per DAT type
    using EntryMap = std::map<std::string, std::vector<Item>>;
    EntryMap maps_[Dict::DatCount];

    // Token table
    std::vector<std::string> tokens_;
    std::unordered_map<std::string, TokenID> token_ids_;

    // Utilities
    static std::string PiecesToLetterKey(const std::vector<std::string>& pieces);
    static std::string PiecesToString(const std::vector<std::string>& pieces);

    void WriteSideTable(const EntryMap& map,
                        const std::vector<uint32_t>& dat_values,
                        const std::vector<std::string>& dat_keys,
                        std::vector<char>& buffer);
    std::size_t WriteTokenTable(std::vector<char>& buffer);
};

} // namespace sime
