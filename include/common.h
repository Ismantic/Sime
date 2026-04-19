#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

using TokenID = std::uint32_t;

// id space (closed-vocabulary IME):
//   0            NotToken   — "not a real LM token": default/empty slot,
//                             decoder end-of-input marker, skip-score marker
//   1 .. N       dict tokens in sime.token.dict.txt line order
constexpr TokenID NotToken = 0;
constexpr TokenID StartToken = 1;

using float_t = double;

struct TokenMap {
    std::unordered_map<std::string, TokenID> ids;   // token → id
    std::vector<std::string> tokens;                 // id → token
};

inline bool LoadTokenMap(const std::filesystem::path& path, TokenMap& map) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    TokenID next_id = StartToken;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto sep = line.find('\t');
        if (sep == std::string::npos) {
            sep = line.find(' ');
        }
        std::string token = (sep != std::string::npos)
                            ? line.substr(0, sep) : line;
        TokenID id = next_id++;
        if (map.tokens.size() <= id) {
            map.tokens.resize(id + 1);
        }
        map.tokens[id] = token;
        map.ids[token] = id;
    }
    return true;
}

} // namespace sime
