#include "dict.h"

#include "ustr.h"

#include <fstream>
#include <string_view>

namespace sime {
namespace {

constexpr bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool IsSpace(char c) {
    return c == ' ' || c == '\t';
}

std::string_view Trim(std::string_view input) {
    auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }
    auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1); 
}

} // namespace

bool Dict::Load(const std::filesystem::path& path) {
    Clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::string line;
    std::u32string t;
    TokenID i = 0;
    while (std::getline(in, line)) {
        if (!ParseLine(line, t, i)) {
            continue;
        }
        Node* node = &root_;
        for (char32_t c : t) {
            node = &node->children[c];
        }
        node->token_id = i;
    }
    return true;
}

void Dict::Clear() {
    root_ = Node{};
}

bool Dict::ParseLine(std::string_view line, std::u32string& t, TokenID& i) {
    line = Trim(line);
    if (line.empty() || line.front() == '#') {
        return false;
    }
    auto pos = line.find_first_of(" \t");
    if (pos == std::string_view::npos) {
        return false;
    }
    std::string_view token = line.substr(0, pos);
    auto rest = line.substr(pos);
    rest = Trim(rest);
    if (rest.empty() || !IsDigit(rest.front())) {
        return false;
    }
    TokenID value = 0;
    for (char c : rest) {
        if (!IsDigit(c)) {
            break;
        }
        value = value * 10 + static_cast<TokenID>(c - '0');
    }
    t = ustr::ToU32(std::string(token));
    i = value;
    return true;
}

Match Dict::DoMatch(std::u32string_view text, std::size_t offset) const {
    const Node* node = &root_;
    Match best{};
    for (std::size_t i = 0; (offset+i) < text.size(); ++i) {
        auto it = node->children.find(text[offset + i]);
        if (it == node->children.end()) {
            break;
        }
        node = &it->second;
        if (node->token_id.has_value()) {
            best.length = i+1;
            best.token_id = node->token_id.value();
        }
    }
    return best;
}

void Dict::ForEachMatch(std::u32string_view text,
                                std::size_t offset,
                                std::size_t max_len,
                                const std::function<void(std::size_t, TokenID)>& cb) const {
    const Node* node = &root_;
    for (std::size_t i = 0; (offset + i) < text.size() && i < max_len; ++i) {
        auto it = node->children.find(text[offset + i]);
        if (it == node->children.end()) {
            break;
        }
        node = &it->second;
        if (node->token_id.has_value()) {
            cb(i + 1, node->token_id.value());
        }
    }
}


} // namespace sime
