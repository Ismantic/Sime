#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sime {

using TokenID = std::uint32_t;
constexpr TokenID NotToken = 0;
constexpr TokenID SentenceStart = 10;
constexpr TokenID SentenceEnd = 11;
constexpr TokenID UnknownToken = 12;
constexpr TokenID StartToken = 70;
constexpr TokenID ScoreNotToken = StartToken - 1;

constexpr std::uint32_t GroupEnd = 1U << 31;
constexpr std::uint32_t GroupTokenMask = ~GroupEnd;

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
