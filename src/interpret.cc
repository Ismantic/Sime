#include "interpret.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sime {

Interpreter::Interpreter(const std::filesystem::path& dict_path,
                         const std::filesystem::path& model_path) {
    if (!trie_.Load(dict_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        trie_.Clear();
        return;
    }
    ready_ = true;
    BuildDigitMap();
}

char Interpreter::LetterToDigit(char c) {
    switch (c) {
    case 'a': case 'b': case 'c': return '2';
    case 'd': case 'e': case 'f': return '3';
    case 'g': case 'h': case 'i': return '4';
    case 'j': case 'k': case 'l': return '5';
    case 'm': case 'n': case 'o': return '6';
    case 'p': case 'q': case 'r': case 's': return '7';
    case 't': case 'u': case 'v': return '8';
    case 'w': case 'x': case 'y': case 'z': return '9';
    default: return '0';
    }
}

std::string Interpreter::PinyinToDigits(const char* pinyin) {
    std::string result;
    for (const char* p = pinyin; *p; ++p) {
        char d = LetterToDigit(static_cast<char>(
            std::tolower(static_cast<unsigned char>(*p))));
        if (d != '0') {
            result.push_back(d);
        }
    }
    return result;
}

void Interpreter::BuildDigitMap() {
    digit_map_.clear();
    std::size_t count = 0;
    const UnitEntry* entries = UnitData::GetDict(count);
    for (std::size_t i = 0; i < count; ++i) {
        Unit unit(entries[i].value);
        if (!unit.Full()) continue;
        std::string digits = PinyinToDigits(entries[i].text);
        if (!digits.empty()) {
            digit_map_[digits].push_back(unit);
        }
    }
}


Interpreter::NineResult Interpreter::DecodeStream(
    std::string_view digits,
    const std::vector<Unit>& prefix,
    std::size_t num) const {
    NineResult result;
    if (!ready_ || digit_map_.empty()) {
        return result;
    }
    if (digits.empty() && prefix.empty()) {
        return result;
    }

    // Validate digits
    for (char c : digits) {
        if (c < '2' || c > '9') return result;
    }

    // Build initial scorer state from prefix
    Scorer::Pos init_pos{};
    float_t init_score = 0.0;
    if (!prefix.empty()) {
        const Trie::Node* pnode = trie_.Root();
        for (const auto& u : prefix) {
            pnode = trie_.DoMove(pnode, u);
            if (!pnode) break;
            std::uint32_t count = 0;
            const std::uint32_t* tokens = trie_.GetToken(pnode, count);
            if (count > 0) {
                TokenID tid = static_cast<TokenID>(tokens[0]);
                Scorer::Pos next_pos{};
                init_score += scorer_.ScoreMove(init_pos, tid, next_pos);
                scorer_.Back(next_pos);
                init_pos = next_pos;
                pnode = trie_.Root();
            }
        }
    }

    const std::size_t d = digits.size();
    const std::size_t full_col = d + 1; // for full-match SentenceEnd
    std::vector<Node> net(full_col + 1);

    // --- Build lattice with joint search ---
    for (std::size_t start = 0; start < d; ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        std::string key;

        for (std::size_t end = start + 1; end <= std::min(start + MaxSyllableLen, d);
             ++end) {
            key.push_back(digits[end - 1]);
            auto it = digit_map_.find(key);
            if (it == digit_map_.end()) continue;

            for (const auto& unit : it->second) {
                const Trie::Node* node = trie_.DoMove(trie_.Root(), unit);
                if (!node) continue;

                // Single-syllable hanzi
                std::uint32_t count = 0;
                const std::uint32_t* tokens = trie_.GetToken(node, count);
                for (std::uint32_t idx = 0; idx < count; ++idx) {
                    bucket.push_back(
                        {start, end, static_cast<TokenID>(tokens[idx])});
                    inserted = true;
                }

                // Two-syllable words
                std::string key2;
                for (std::size_t end2 = end + 1;
                     end2 <= std::min(end + MaxSyllableLen, d); ++end2) {
                    key2.push_back(digits[end2 - 1]);
                    auto it2 = digit_map_.find(key2);
                    if (it2 == digit_map_.end()) continue;
                    for (const auto& unit2 : it2->second) {
                        const Trie::Node* node2 =
                            trie_.DoMove(node, unit2);
                        if (!node2) continue;
                        std::uint32_t c2 = 0;
                        const std::uint32_t* t2 =
                            trie_.GetToken(node2, c2);
                        for (std::uint32_t idx = 0; idx < c2; ++idx) {
                            bucket.push_back(
                                {start, end2,
                                 static_cast<TokenID>(t2[idx])});
                            inserted = true;
                        }

                        // Three-syllable words
                        std::string key3;
                        for (std::size_t end3 = end2 + 1;
                             end3 <= std::min(end2 + MaxSyllableLen, d); ++end3) {
                            key3.push_back(digits[end3 - 1]);
                            auto it3 = digit_map_.find(key3);
                            if (it3 == digit_map_.end()) continue;
                            for (const auto& unit3 : it3->second) {
                                const Trie::Node* node3 =
                                    trie_.DoMove(node2, unit3);
                                if (!node3) continue;
                                std::uint32_t c3 = 0;
                                const std::uint32_t* t3 =
                                    trie_.GetToken(node3, c3);
                                for (std::uint32_t idx = 0; idx < c3;
                                     ++idx) {
                                    bucket.push_back(
                                        {start, end3,
                                         static_cast<TokenID>(t3[idx])});
                                    inserted = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Tail expansion ---
        // When remaining digits don't form a complete syllable,
        // try all syllables whose digit sequence starts with the tail.
        std::string tail(digits.substr(start));
        if (tail.size() <= MaxSyllableLen &&
            digit_map_.find(tail) == digit_map_.end()) {
            for (const auto& [dkey, units] : digit_map_) {
                if (dkey.size() > tail.size() &&
                    dkey.compare(0, tail.size(), tail) == 0) {
                    for (const auto& unit : units) {
                        const Trie::Node* node =
                            trie_.DoMove(trie_.Root(), unit);
                        if (!node) continue;
                        std::uint32_t count = 0;
                        const std::uint32_t* tokens =
                            trie_.GetToken(node, count);
                        for (std::uint32_t idx = 0; idx < count; ++idx) {
                            bucket.push_back(
                                {start, d,
                                 static_cast<TokenID>(tokens[idx])});
                            inserted = true;
                        }
                    }
                }
            }
        }

        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    // SentenceEnd at full-match position only
    net[d].es.push_back({d, full_col, SentenceEnd});
    for (auto& col : net) {
        col.states.SetMaxTop(BeamSize);
    }
    State init(init_score, 0, init_pos, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // --- Collect hanzi results ---
    // Helper to extract text from a path
    auto extract = [&](const std::vector<Link>& path) -> std::u32string {
        std::u32string text;
        for (const auto& link : path) {
            if (link.id == SentenceEnd || link.id == ScoreNotToken ||
                link.id == NotToken) continue;
            const char32_t* chars = trie_.TokenAt(link.id);
            if (!chars) continue;
            for (std::size_t i = 0; chars[i] != 0; ++i) {
                text.push_back(chars[i]);
            }
        }
        return text;
    };

    auto add_result = [&](const std::u32string& text, float_t score,
                          std::size_t matched) -> bool {
        if (text.empty()) return false;
        for (const auto& existing : result.hanzi) {
            if (existing.text == text) return false;
        }
        SentenceResult sr;
        sr.text = text;
        sr.score = score;
        sr.matched_len = matched;
        result.hanzi.push_back(std::move(sr));
        return true;
    };

    // Full match: from SentenceEnd column (highest priority)
    {
        auto states = net[full_col].states.GetStates();
        const std::size_t full_limit = std::max<std::size_t>(num / 2, 5);
        for (std::size_t rank = 0;
             rank < states.size() && result.hanzi.size() < full_limit;
             ++rank) {
            auto path = Backtrace(states[rank], full_col);
            auto text = extract(path);
            add_result(text, -states[rank].score, d);
        }
    }

    // Partial matches: from each intermediate position
    const std::size_t per_pos = std::max<std::size_t>(num / 4, 2);
    for (std::size_t pos = d - 1; pos >= 1 && result.hanzi.size() < num;
         --pos) {
        auto states = net[pos].states.GetStates();
        std::size_t added = 0;
        for (std::size_t rank = 0;
             rank < states.size() && added < per_pos; ++rank) {
            auto path = Backtrace(states[rank], pos);
            auto text = extract(path);
            if (add_result(text, -states[rank].score, pos)) {
                ++added;
            }
        }
    }

    // --- Build best_pinyin from top hanzi result ---
    // (For preedit display — take the best result's matched digits
    //  and try to reconstruct the pinyin string)
    // This is approximate; the exact pinyin comes from user selection.

    // --- Pinyin candidates: single-syllable matches, long→short ---
    for (std::size_t len = d; len >= 1 && result.pinyin.size() < num;
         --len) {
        std::string dkey(digits.substr(0, len));
        auto it = digit_map_.find(dkey);
        if (it == digit_map_.end()) continue;
        for (const auto& unit : it->second) {
            if (result.pinyin.size() >= num) break;
            // Deduplicate
            bool dup = false;
            for (const auto& existing : result.pinyin) {
                if (existing.units.size() == 1 &&
                    existing.units[0] == unit) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            PinyinCandidate pr;
            pr.units.push_back(unit);
            pr.cnt = len;
            result.pinyin.push_back(std::move(pr));
        }
    }

    // Tail expansion pinyin candidates (incomplete last syllable)
    if (d > 0) {
        std::string tail(digits);
        for (const auto& [dkey, units] : digit_map_) {
            if (result.pinyin.size() >= num) break;
            if (dkey.size() > tail.size() &&
                dkey.compare(0, tail.size(), tail) == 0) {
                for (const auto& unit : units) {
                    if (result.pinyin.size() >= num) break;
                    bool dup = false;
                    for (const auto& existing : result.pinyin) {
                        if (existing.units.size() == 1 &&
                            existing.units[0] == unit) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) continue;
                    PinyinCandidate pr;
                    pr.units.push_back(unit);
                    pr.cnt = d;
                    result.pinyin.push_back(std::move(pr));
                }
            }
        }
    }

    return result;
}

std::vector<SentenceResult> Interpreter::DecodeNine(
    std::string_view digits,
    const std::vector<Unit>& prefix,
    std::size_t num) const {
    std::vector<SentenceResult> results;
    if (!ready_ || digit_map_.empty() || digits.empty()) {
        return results;
    }

    // Validate: only digits 2-9
    for (char c : digits) {
        if (c < '2' || c > '9') return results;
    }

    // Build initial scorer state from prefix (confirmed pinyin context)
    Scorer::Pos init_pos{};
    float_t init_score = 0.0;
    if (!prefix.empty()) {
        // Walk prefix through Trie to get token IDs, feed to scorer
        const Trie::Node* pnode = trie_.Root();
        for (const auto& u : prefix) {
            pnode = trie_.DoMove(pnode, u);
            if (!pnode) break;
            std::uint32_t count = 0;
            const std::uint32_t* tokens = trie_.GetToken(pnode, count);
            if (count > 0) {
                // Use first (best) token for context
                TokenID tid = static_cast<TokenID>(tokens[0]);
                Scorer::Pos next_pos{};
                init_score += scorer_.ScoreMove(init_pos, tid, next_pos);
                scorer_.Back(next_pos);
                init_pos = next_pos;
                pnode = trie_.Root(); // reset Trie for next syllable
            }
        }
    }

    const std::size_t d = digits.size();
    const std::size_t net_size = d + 2;
    std::vector<Node> net(net_size);

    // Build lattice: for each digit span, enumerate pinyin syllables,
    // then for each syllable (or syllable sequence), query Trie for hanzi tokens.
    // We need to try all possible pinyin segmentations implicitly through the lattice.

    // digit_map_ maps digit_string → syllable entries (from NineDecoder)
    // For each starting position, try digit substrings of length 1..6,
    // map to pinyin syllables, then walk the Trie to find hanzi tokens.

    for (std::size_t start = 0; start < d; ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        std::string key;

        for (std::size_t end = start + 1; end <= std::min(start + MaxSyllableLen, d); ++end) {
            key.push_back(digits[end - 1]);
            // Find all pinyin syllables matching this digit substring
            auto it = digit_map_.find(key);
            if (it == digit_map_.end()) continue;

            for (const auto& unit : it->second) {
                // Walk Trie with this single syllable from root
                const Trie::Node* node = trie_.DoMove(trie_.Root(), unit);
                if (!node) continue;

                // Single-syllable hanzi matches
                std::uint32_t count = 0;
                const std::uint32_t* tokens = trie_.GetToken(node, count);
                for (std::uint32_t idx = 0; idx < count; ++idx) {
                    TokenID wid = static_cast<TokenID>(tokens[idx]);
                    bucket.push_back({start, end, wid});
                    inserted = true;
                }

                // Try extending with next digit spans for multi-syllable words
                // (e.g., "zhong" + "guo" → "中国")
                std::string key2;
                for (std::size_t end2 = end + 1;
                     end2 <= std::min(end + MaxSyllableLen, d); ++end2) {
                    key2.push_back(digits[end2 - 1]);
                    auto it2 = digit_map_.find(key2);
                    if (it2 == digit_map_.end()) continue;

                    for (const auto& unit2 : it2->second) {
                        const Trie::Node* node2 =
                            trie_.DoMove(node, unit2);
                        if (!node2) continue;

                        std::uint32_t count2 = 0;
                        const std::uint32_t* tokens2 =
                            trie_.GetToken(node2, count2);
                        for (std::uint32_t idx = 0; idx < count2; ++idx) {
                            TokenID wid = static_cast<TokenID>(tokens2[idx]);
                            bucket.push_back({start, end2, wid});
                            inserted = true;
                        }

                        // Try third syllable for 3-char words
                        std::string key3;
                        for (std::size_t end3 = end2 + 1;
                             end3 <= std::min(end2 + MaxSyllableLen, d); ++end3) {
                            key3.push_back(digits[end3 - 1]);
                            auto it3 = digit_map_.find(key3);
                            if (it3 == digit_map_.end()) continue;

                            for (const auto& unit3 : it3->second) {
                                const Trie::Node* node3 =
                                    trie_.DoMove(node2, unit3);
                                if (!node3) continue;

                                std::uint32_t count3 = 0;
                                const std::uint32_t* tokens3 =
                                    trie_.GetToken(node3, count3);
                                for (std::uint32_t idx = 0; idx < count3;
                                     ++idx) {
                                    TokenID wid =
                                        static_cast<TokenID>(tokens3[idx]);
                                    bucket.push_back({start, end3, wid});
                                    inserted = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    // SentenceEnd
    net[d].es.push_back({d, d + 1, SentenceEnd});

    // Beam search
    for (auto& col : net) {
        col.states.SetMaxTop(BeamSize);
    }
    State init(init_score, 0, init_pos, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // Backtrace from final column
    auto tail_states = net.back().states.GetStates();
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < num; ++rank) {
        auto path = Backtrace(tail_states[rank], d + 1);
        if (path.empty()) continue;

        std::u32string text;
        for (const auto& link : path) {
            if (link.id == ScoreNotToken || link.id == NotToken) continue;
            const char32_t* chars = trie_.TokenAt(link.id);
            if (!chars) continue;
            for (std::size_t i = 0; chars[i] != 0; ++i) {
                text.push_back(chars[i]);
            }
        }
        if (text.empty()) continue;

        bool dup = false;
        for (const auto& existing : results) {
            if (existing.text == text) { dup = true; break; }
        }
        if (dup) continue;

        SentenceResult sr;
        sr.text = std::move(text);
        sr.score = -tail_states[rank].score;
        sr.matched_len = d;
        results.push_back(std::move(sr));
    }

    return results;
}

bool Interpreter::LoadDict(const std::filesystem::path& path) {
    if (!ready_) return false;
    return dict_.Load(path);
}

std::vector<DecodeResult> Interpreter::DecodeText(
    std::string_view input,
    std::size_t num) const {
    std::vector<Unit> units;
    UnitParser parser;
    std::size_t pos = 0;
    while (pos < input.size()) {
        while (pos < input.size() && UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        if (pos >= input.size()) {
            break;
        }
        std::size_t start = pos;
        while (pos < input.size() && !UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        std::string str(input.substr(start, pos - start));
        std::vector<Unit> chunk;
        if (parser.ParseStr(str, chunk)) {
            units.insert(units.end(), chunk.begin(), chunk.end());
        }
    }
    return Decode(units, num);
}

std::vector<DecodeResult> Interpreter::Decode(
    const std::vector<Unit>& units,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || units.empty()) {
        return results;
    }

    std::vector<Node> net;
    InitNet(units, net);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& column : net) {
        column.states.SetMaxTop(BeamSize);
    }
    State init_state(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init_state);

    Process(net);

    const auto tail_states = net.back().states.GetStates();
    if (tail_states.empty()) {
        return results;
    }

    const std::size_t total =
        std::min<std::size_t>(BeamSize, tail_states.size());
    for (std::size_t rank = 0; rank < total && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], net.size() - 1);
        if (path.empty()) {
            continue;
        }
        DecodeResult result;
        result.score = -tail_states[rank].score;
        std::u32string composed;
        composed.reserve(path.size() * 4);
        for (const auto& word : path) {
            result.tokens.push_back(word.id);
            composed += ToText(word, units);
            // Build pinyin
            std::string seg = SliceToUnits(units, word.start, word.end);
            if (!seg.empty()) {
                if (!result.pinyin.empty()) result.pinyin += '\'';
                result.pinyin += seg;
            }
        }
        if (composed.empty()) {
            continue;
        }
        result.text = std::move(composed);
        bool duplicate = false;
        for (const auto& existing : results) {
            if (existing.text == result.text) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            results.push_back(std::move(result));
        }
    }
    return results;
}

void Interpreter::InitNet(const std::vector<Unit>& units,
                          std::vector<Node>& net,
                          const std::vector<Unit>& tail_expansions) const {
    const std::size_t n = units.size();
    const bool has_exp = !tail_expansions.empty();
    // With expansion: extra column for the incomplete syllable
    const std::size_t effective_n = has_exp ? n + 1 : n;
    net.clear();
    net.resize(effective_n + 2);

    for (std::size_t start = 0; start < effective_n; ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        const Trie::Node* trie_node = trie_.Root();
        std::size_t pos = start;

        // Traverse through complete units
        while (trie_node && pos < n) {
            trie_node = trie_.DoMove(trie_node, units[pos]);
            ++pos;
            if (!trie_node) break;
            std::uint32_t count = 0;
            const std::uint32_t* tokens = trie_.GetToken(trie_node, count);
            for (std::uint32_t idx = 0; idx < count; ++idx) {
                TokenID wid = static_cast<TokenID>(tokens[idx]);
                bucket.push_back({start, pos, wid});
            }
            if (count > 0) inserted = true;
        }

        // Fan out: try each expansion at the incomplete tail position
        if (has_exp && trie_node && pos == n) {
            for (const auto& exp : tail_expansions) {
                const Trie::Node* exp_node =
                    trie_.DoMove(trie_node, exp);
                if (!exp_node) continue;
                std::uint32_t count = 0;
                const std::uint32_t* tokens =
                    trie_.GetToken(exp_node, count);
                for (std::uint32_t idx = 0; idx < count; ++idx) {
                    TokenID wid = static_cast<TokenID>(tokens[idx]);
                    bucket.push_back({start, n + 1, wid});
                }
                if (count > 0) inserted = true;
            }
        }

        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    // Prune each position
    for (std::size_t i = 0; i < effective_n; ++i) {
        PruneNode(net[i].es);
    }

    net[effective_n].es.push_back(
        {effective_n, effective_n + 1, SentenceEnd});
}

void Interpreter::PruneNode(std::vector<Link>& edges) const {
    if (edges.size() <= NodeSize) return;

    // Group edges by span length, prune each group independently.
    std::unordered_map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        groups[edges[i].end - edges[i].start].push_back(i);
    }

    std::vector<Link> pruned;
    pruned.reserve(edges.size());

    for (auto& [span, indices] : groups) {
        if (indices.size() <= NodeSize) {
            for (auto idx : indices) {
                pruned.push_back(edges[idx]);
            }
            continue;
        }

        // Score by unigram, keep top NodeSize
        std::vector<std::pair<float_t, std::size_t>> scored;
        scored.reserve(indices.size());
        for (auto idx : indices) {
            if (edges[idx].id == ScoreNotToken ||
                edges[idx].id == SentenceEnd) {
                scored.push_back({0.0, idx});
                continue;
            }
            Scorer::Pos dummy{};
            float_t s = scorer_.ScoreMove(Scorer::Pos{}, edges[idx].id, dummy);
            scored.push_back({s, idx});
        }

        std::partial_sort(
            scored.begin(),
            scored.begin() + static_cast<std::ptrdiff_t>(NodeSize),
            scored.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first; // smaller cost = higher probability
            });

        for (std::size_t i = 0; i < NodeSize; ++i) {
            pruned.push_back(edges[scored[i].second]);
        }
    }

    edges = std::move(pruned);
}

void Interpreter::Process(std::vector<Node>& net) const {
    for (std::size_t col = 0; col < net.size(); ++col) {
        auto& column = net[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.es) {
                Scorer::Pos next_pos{};
                float_t step = scorer_.ScoreMove(value.pos, word.id, next_pos);
                scorer_.Back(next_pos);
                float_t next_cost = value.score + step;
                State next(next_cost, word.end, next_pos, &value, word.id);
                net[word.end].states.Insert(next);
            }
        }
    }
}

std::vector<Interpreter::Link> Interpreter::Backtrace(
    const State& tail_state,
    std::size_t end) {
    std::vector<Link> path;
    const State* state = &tail_state;
    while (state != nullptr && state->backtrace_state != nullptr) {
        const State* prev = state->backtrace_state;
        path.push_back({prev->now, state->now,
                        state->backtrace_token});
        state = prev;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().end == end) {
        path.pop_back();
    }
    return path;
}
std::u32string Interpreter::ToText(const Link& n,
                                   const std::vector<Unit>& units) const {
    if (n.id == ScoreNotToken || n.id == NotToken) {
        std::string fallback = SliceToUnits(units, n.start, n.end);
        return ustr::ToU32("[" + fallback + "]");
    }
    const char32_t* chars = trie_.TokenAt(n.id);
    if (chars == nullptr || chars[0] == 0) {
        std::string fallback = SliceToUnits(units, n.start, n.end);
        return ustr::ToU32("[" + fallback + "]");
    }
    constexpr std::size_t MaxTokenSize = 64;
    std::u32string buffer;
    buffer.reserve(8);
    for (std::size_t i = 0; i < MaxTokenSize; ++i) {
        char32_t ch = chars[i];
        if (ch == 0) {
            break;
        }
        buffer.push_back(ch);
    }
    if (buffer.empty()) {
        std::string fallback = SliceToUnits(units, n.start, n.end);
        return ustr::ToU32("[" + fallback + "]");
    }
    return buffer;
}

std::string Interpreter::SliceToUnits(
    const std::vector<Unit>& units,
    std::size_t start,
    std::size_t end) {
    std::string result;
    for (std::size_t i = start; i < end && i < units.size(); ++i) {
        const char* syl = UnitData::Decode(units[i]);
        if (!syl) {
            continue;
        }
        if (!result.empty()) {
            result.push_back('\'');
        }
        result.append(syl);
    }
    return result;
}

// ===== Sentence: multi-endpoint decode =====

bool Interpreter::ParseWithBoundaries(
    std::string_view input,
    std::vector<Unit>& units,
    std::vector<std::size_t>& unit_byte_end,
    std::vector<Unit>& tail_expansions) {
    units.clear();
    unit_byte_end.clear();
    tail_expansions.clear();
    UnitParser parser;
    std::size_t pos = 0;
    while (pos < input.size()) {
        while (pos < input.size() && UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        if (pos >= input.size()) break;
        std::size_t chunk_start = pos;
        while (pos < input.size() && !UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        std::string chunk_str(input.substr(chunk_start, pos - chunk_start));
        std::vector<Unit> chunk;
        if (!parser.ParseStr(chunk_str, chunk)) {
            // Full parse failed — try partial parse + incomplete tail
            auto pr = parser.ParseTokenEnhanced(chunk_str, true);
            if (pr.matched_len > 0) {
                chunk = std::move(pr.units);
            } else {
                continue;
            }
        }

        // Check if last unit is a bare initial (incomplete syllable)
        if (!chunk.empty() && !chunk.back().Full()) {
            // Last unit is incomplete — pop it and generate expansions
            Unit bare = chunk.back();
            chunk.pop_back();
            // Add the complete units
            std::size_t byte_offset = chunk_start;
            for (const auto& u : chunk) {
                const char* syl = UnitData::Decode(u);
                if (syl) byte_offset += std::strlen(syl);
                unit_byte_end.push_back(byte_offset);
            }
            units.insert(units.end(), chunk.begin(), chunk.end());
            // Generate expansions from the bare initial
            const char* initial = UnitData::Decode(bare);
            if (initial) {
                auto exps = UnitData::ExpandIncomplete(initial);
                if (!exps.empty()) {
                    tail_expansions = std::move(exps);
                    unit_byte_end.push_back(chunk_start + chunk_str.size());
                }
            }
        } else {
            // All units are complete
            std::size_t byte_offset = chunk_start;
            for (const auto& u : chunk) {
                const char* syl = UnitData::Decode(u);
                if (syl) byte_offset += std::strlen(syl);
                unit_byte_end.push_back(byte_offset);
            }
            units.insert(units.end(), chunk.begin(), chunk.end());
        }
    }
    return !units.empty();
}

std::vector<SentenceResult> Interpreter::DecodeSentence(
    std::string_view input,
    std::size_t num) const {
    std::vector<SentenceResult> results;
    if (!ready_) return results;

    // 1. Parse input with byte boundary tracking
    std::vector<Unit> units;
    std::vector<std::size_t> unit_byte_end;
    std::vector<Unit> tail_expansions;
    if (!ParseWithBoundaries(input, units, unit_byte_end, tail_expansions)) {
        return results;
    }

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t total_bytes = input.size();

    // Build lattice once, shared by Layer 1 and Layer 2
    std::vector<Node> net;
    const bool has_exp = !tail_expansions.empty();
    const std::size_t effective_n = has_exp ? units.size() + 1 : units.size();
    InitNet(units, net, tail_expansions);
    // BeamSize applies to all decode functions uniformly
    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // === Layer 1: Full sentence N-best (covers all input) ===
    {
        const auto tail = net.back().states.GetStates();
        const std::size_t full_limit = std::max<std::size_t>(max_top / 2, 3);
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        for (std::size_t rank = 0; rank < scan && results.size() < full_limit; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::u32string composed;
            std::string py;
            composed.reserve(path.size() * 4);
            for (const auto& w : path) {
                composed += ToText(w, units);
                std::string seg = SliceToUnits(units, w.start, w.end);
                if (!seg.empty()) {
                    if (!py.empty()) py += '\'';
                    py += seg;
                }
            }
            if (composed.empty()) continue;
            bool dup = false;
            for (const auto& e : results)
                if (e.text == composed) { dup = true; break; }
            if (!dup) {
                SentenceResult r;
                r.text = std::move(composed);
                r.pinyin = std::move(py);
                r.score = -tail[rank].score;
                r.matched_len = total_bytes;
                results.push_back(std::move(r));
            }
        }
    }

    std::size_t layer1_size = results.size();

    // === Layer 2: Word/phrase candidates from BOS with distance penalty ===
    // Similar to libime: distancePenalty = unknownPenalty / 1.8
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    {

        // Collect partial candidates from intermediate columns only.
        // Full match already handled by Layer 1.
        const std::size_t kPerPrefix = MaxPerPrefix;
        for (std::size_t col = effective_n - 1; col >= 1; --col) {
            const auto& col_states = net[col].states.GetStates();
            if (col_states.empty()) continue;

            std::size_t distance = effective_n - col;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;
            std::size_t matched_bytes =
                (col <= unit_byte_end.size()) ? unit_byte_end[col - 1]
                                              : total_bytes;

            const std::size_t col_scan =
                std::min<std::size_t>(kPerPrefix, col_states.size());
            for (std::size_t rank = 0; rank < col_scan; ++rank) {
                const auto& st = col_states[rank];

                // Score the SentenceEnd transition from this state
                Scorer::Pos sent_pos{};
                float_t sent_step =
                    scorer_.ScoreMove(st.pos, SentenceEnd, sent_pos);
                float_t raw_score = -(st.score + sent_step);
                float_t adjusted = raw_score - dist_penalty;

                // Backtrace to get the text.
                // Pass SIZE_MAX as end so Backtrace won't strip the last link
                // (that stripping is for removing the SentenceEnd link,
                //  which doesn't apply here since we're at an intermediate column).
                auto path = Backtrace(st, SIZE_MAX);
                if (path.empty()) continue;

                std::u32string composed;
                std::string py;
                composed.reserve(path.size() * 4);
                for (const auto& w : path) {
                    composed += ToText(w, units);
                    std::string seg = SliceToUnits(units, w.start, w.end);
                    if (!seg.empty()) {
                        if (!py.empty()) py += '\'';
                        py += seg;
                    }
                }
                if (composed.empty()) continue;

                bool dup = false;
                for (const auto& e : results)
                    if (e.text == composed && e.matched_len == matched_bytes) {
                        dup = true; break;
                    }
                if (!dup) {
                    SentenceResult r;
                    r.text = std::move(composed);
                    r.pinyin = std::move(py);
                    r.score = adjusted;
                    r.matched_len = matched_bytes;
                    results.push_back(std::move(r));
                }
            }
        }
    }

    // Sort Layer 2 (partial matches) by adjusted score.
    // Layer 1 (full matches) already sorted by LM score and stays in front.
    std::sort(results.begin() + static_cast<std::ptrdiff_t>(layer1_size),
              results.end(),
              [](const SentenceResult& a, const SentenceResult& b) {
                  return a.score > b.score;
              });

    // === Dict: inject matches at the front, always top priority ===
    if (!dict_.Empty()) {
        const std::size_t n = units.size();
        for (std::size_t len = n; len >= 1; --len) {
            auto matches = dict_.Lookup(units.data(), len);
            if (matches.empty()) continue;

            std::size_t matched_bytes =
                (len <= unit_byte_end.size()) ? unit_byte_end[len - 1]
                                              : total_bytes;

            for (std::size_t idx : matches) {
                const auto& text = dict_.TextAt(idx);

                // Remove duplicate if already in results
                for (auto it2 = results.begin(); it2 != results.end(); ++it2) {
                    if (it2->text == text && it2->matched_len == matched_bytes) {
                        results.erase(it2);
                        if (it2 - results.begin() < static_cast<std::ptrdiff_t>(layer1_size))
                            --layer1_size;
                        break;
                    }
                }

                SentenceResult r;
                r.text = text;
                r.score = 1e9;  // always top
                r.matched_len = matched_bytes;
                results.insert(results.begin(), std::move(r));
                ++layer1_size;
            }
        }
    }

    if (results.size() > max_top) {
        results.resize(max_top);
    }
    return results;
}

} // namespace sime
