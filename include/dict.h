#pragma once 

#include "common.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

struct Match {
    TokenID token_id = NotToken;
    std::size_t length = 0;
};

class Dict {
public:
    bool Load(const std::filesystem::path& path);
    void Clear();

    Match DoMatch(std::u32string_view text, std::size_t offset) const;
    void ForEachMatch(std::u32string_view text, 
                      std::size_t offset,
                      std::size_t max_len,
                      const std::function<void(std::size_t, TokenID)>& cb) const;

private:
    struct Node {
        std::optional<TokenID> token_id{};
        std::unordered_map<char32_t, Node> children{};
    };

    static bool ParseLine(std::string_view line, std::u32string& token);

    Node root_;
};

} // namespace sime