#include "userdict.h"
#include "ustr.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace sime {

bool UserDict::Load(const std::filesystem::path& path,
                    const Trie& trie,
                    const Scorer& scorer) {
    dict_.clear();
    entries_.clear();

    std::ifstream in(path);
    if (!in) {
        return false;
    }

    // Build reverse map: text → token ID from trie's token table.
    std::unordered_map<std::u32string, TokenID> text_to_id;
    for (std::uint32_t i = 0; i < trie.TokenCount(); ++i) {
        const char32_t* p = trie.TokenAt(i);
        if (!p || !p[0]) continue;
        std::u32string s;
        while (*p) s.push_back(*p++);
        // Keep the first (lowest ID) mapping for each text.
        text_to_id.emplace(std::move(s), i);
    }

    UnitParser parser;
    std::string line;
    while (std::getline(in, line)) {
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        // Parse: 汉字 拼音
        std::istringstream ss(line.substr(start));
        std::string hanzi, pinyin;
        if (!(ss >> hanzi >> pinyin)) continue;

        std::vector<Unit> units;
        if (!parser.ParseStr(pinyin, units)) continue;
        if (units.empty()) continue;

        std::u32string text = ustr::ToU32(hanzi);
        auto tokens = Tokenize(text, text_to_id);
        float_t score = ScoreTokens(tokens, scorer);

        std::size_t idx = entries_.size();
        entries_.push_back({std::move(text), score});

        std::string key = MakeKey(units.data(), units.size());
        dict_[key].push_back(idx);
    }

    return !entries_.empty();
}

std::vector<UserDict::Match> UserDict::Lookup(
    const Unit* units, std::size_t len) const {
    std::vector<Match> result;
    std::string key = MakeKey(units, len);
    auto it = dict_.find(key);
    if (it == dict_.end()) return result;

    for (std::size_t idx : it->second) {
        result.push_back({base_id_ + static_cast<TokenID>(idx)});
    }
    return result;
}

const std::u32string& UserDict::TextAt(std::size_t local_id) const {
    return entries_[local_id].text;
}

float_t UserDict::ScoreAt(std::size_t local_id) const {
    return entries_[local_id].score;
}

std::string UserDict::MakeKey(const Unit* units, std::size_t len) {
    std::string key;
    key.resize(len * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < len; ++i) {
        std::uint32_t v = units[i].value;
        std::memcpy(key.data() + i * sizeof(v), &v, sizeof(v));
    }
    return key;
}

std::vector<TokenID> UserDict::Tokenize(
    const std::u32string& text,
    const std::unordered_map<std::u32string, TokenID>& text_to_id) {
    std::vector<TokenID> result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        // Greedy longest match
        TokenID best_id = NotToken;
        std::size_t best_len = 0;
        for (std::size_t len = text.size() - pos; len >= 1; --len) {
            auto it = text_to_id.find(text.substr(pos, len));
            if (it != text_to_id.end()) {
                best_id = it->second;
                best_len = len;
                break;
            }
        }
        if (best_len > 0) {
            result.push_back(best_id);
            pos += best_len;
        } else {
            // Character not in token table, skip it (will get unknown penalty)
            result.push_back(NotToken);
            pos += 1;
        }
    }
    return result;
}

float_t UserDict::ScoreTokens(const std::vector<TokenID>& tokens,
                               const Scorer& scorer) {
    float_t total = 0.0;
    Scorer::Pos pos{};  // BOS
    for (TokenID id : tokens) {
        Scorer::Pos next{};
        total += scorer.ScoreMove(pos, id, next);
        scorer.Back(next);
        pos = next;
    }
    // Add SentenceToken transition to match system lattice scoring.
    Scorer::Pos end{};
    total += scorer.ScoreMove(pos, SentenceToken, end);
    return total;
}

} // namespace sime
