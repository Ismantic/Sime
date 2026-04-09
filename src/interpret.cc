#include "interpret.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace sime {

Interpreter::Interpreter(const std::filesystem::path& trie_path,
                         const std::filesystem::path& model_path) {
    if (!trie_.Load(trie_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        trie_.Clear();
        return;
    }
    ready_ = true;
    BuildNumMap();
}

char Interpreter::LetterToNum(char c) {
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

std::string Interpreter::UnitToNum(const char* unit) {
    std::string result;
    for (const char* p = unit; *p; ++p) {
        char d = LetterToNum(static_cast<char>(
            std::tolower(static_cast<unsigned char>(*p))));
        if (d != '0') {
            result.push_back(d);
        }
    }
    return result;
}

void Interpreter::BuildNumMap() {
    num_map_.clear();
    std::size_t count = 0;
    const UnitEntry* entries = UnitData::GetDict(count);
    for (std::size_t i = 0; i < count; ++i) {
        Unit unit(entries[i].value);
        if (!unit.Full()) continue;
        std::string nums = UnitToNum(entries[i].text);
        if (!nums.empty()) {
            num_map_[nums].push_back(unit);
        }
    }
}


void Interpreter::InitStartState(const std::vector<Unit>& start,
                                   Scorer::Pos& pos, float_t& score) const {
    pos = Scorer::Pos{};
    score = 0.0;
    if (start.empty()) return;
    const Trie::Node* pnode = trie_.Root();
    for (const auto& u : start) {
        pnode = trie_.DoMove(pnode, u);
        if (!pnode) break;
        std::uint32_t count = 0;
        const std::uint32_t* tokens = trie_.GetToken(pnode, count);
        if (count > 0) {
            TokenID tid = static_cast<TokenID>(tokens[0]);
            Scorer::Pos next_pos{};
            score += scorer_.ScoreMove(pos, tid, next_pos);
            scorer_.Back(next_pos);
            pos = next_pos;
            pnode = trie_.Root();
        }
    }
}

void Interpreter::InitNumNet(std::string_view nums, std::vector<Node>& net,
                               bool tail_expansion) const {
    const std::size_t d = nums.size();
    net.clear();
    net.resize(d + 2);

    for (std::size_t start = 0; start < d; ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        std::string key;

        for (std::size_t end = start + 1;
             end <= std::min(start + MaxSyllableCnt, d); ++end) {
            key.push_back(nums[end - 1]);
            auto it = num_map_.find(key);
            if (it == num_map_.end()) continue;

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
                     end2 <= std::min(end + MaxSyllableCnt, d); ++end2) {
                    key2.push_back(nums[end2 - 1]);
                    auto it2 = num_map_.find(key2);
                    if (it2 == num_map_.end()) continue;
                    for (const auto& unit2 : it2->second) {
                        const Trie::Node* node2 = trie_.DoMove(node, unit2);
                        if (!node2) continue;
                        std::uint32_t c2 = 0;
                        const std::uint32_t* t2 = trie_.GetToken(node2, c2);
                        for (std::uint32_t idx = 0; idx < c2; ++idx) {
                            bucket.push_back(
                                {start, end2,
                                 static_cast<TokenID>(t2[idx])});
                            inserted = true;
                        }

                        // Three-syllable words
                        std::string key3;
                        for (std::size_t end3 = end2 + 1;
                             end3 <= std::min(end2 + MaxSyllableCnt, d);
                             ++end3) {
                            key3.push_back(nums[end3 - 1]);
                            auto it3 = num_map_.find(key3);
                            if (it3 == num_map_.end()) continue;
                            for (const auto& unit3 : it3->second) {
                                const Trie::Node* node3 =
                                    trie_.DoMove(node2, unit3);
                                if (!node3) continue;
                                std::uint32_t c3 = 0;
                                const std::uint32_t* t3 =
                                    trie_.GetToken(node3, c3);
                                for (std::uint32_t idx = 0; idx < c3; ++idx) {
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

        // Tail expansion: when remaining digits don't form a complete syllable,
        // try all syllables whose digit sequence starts with the tail.
        if (tail_expansion) {
            std::string tail(nums.substr(start));
            if (tail.size() <= MaxSyllableCnt &&
                num_map_.find(tail) == num_map_.end()) {
                for (const auto& [dkey, units] : num_map_) {
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
        }

        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    for (std::size_t i = 0; i < d; ++i) {
        PruneNode(net[i].es);
    }

    net[d].es.push_back({d, d + 1, SentenceEnd});
}

std::string Interpreter::ExtractNumText(const std::vector<Link>& path) const {
    std::u32string u32;
    for (const auto& link : path) {
        if (link.id == SentenceEnd || link.id == ScoreNotToken ||
            link.id == NotToken) continue;
        const char32_t* chars = trie_.TokenAt(link.id);
        if (!chars) continue;
        for (std::size_t i = 0; chars[i] != 0; ++i) {
            u32.push_back(chars[i]);
        }
    }
    return ustr::FromU32(u32);
}

std::vector<DecodeResult> Interpreter::DecodeNumSentence(
    std::string_view nums,
    const std::vector<Unit>& start,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num_map_.empty()) return results;
    if (nums.empty() && start.empty()) return results;
    for (char c : nums) {
        if (c < '2' || c > '9') return results;
    }

    const std::size_t max_top = num == 0 ? 1 : num;

    Scorer::Pos init_pos{};
    float_t init_score = 0.0;
    InitStartState(start, init_pos, init_score);

    const std::size_t d = nums.size();
    std::vector<Node> net;
    InitNumNet(nums, net, true);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(init_score, 0, init_pos, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto add_result = [&](const std::string& text, float_t score,
                          std::size_t matched) -> bool {
        if (text.empty()) return false;
        for (const auto& existing : results) {
            if (existing.text == text) return false;
        }
        results.push_back({text, {}, score, matched});
        return true;
    };

    // Full match from SentenceEnd column
    {
        auto states = net[d + 1].states.GetStates();
        const std::size_t full_limit = std::max<std::size_t>(max_top / 2, 5);
        for (std::size_t rank = 0;
             rank < states.size() && results.size() < full_limit; ++rank) {
            auto path = Backtrace(states[rank], d + 1);
            add_result(ExtractNumText(path), -states[rank].score, d);
        }
    }

    // Partial matches from intermediate positions
    const std::size_t per_pos = std::max<std::size_t>(max_top / 4, 2);
    for (std::size_t pos = d - 1; pos >= 1 && results.size() < max_top;
         --pos) {
        auto states = net[pos].states.GetStates();
        std::size_t added = 0;
        for (std::size_t rank = 0;
             rank < states.size() && added < per_pos; ++rank) {
            auto path = Backtrace(states[rank], pos);
            if (add_result(ExtractNumText(path), -states[rank].score, pos))
                ++added;
        }
    }

    return results;
}

std::vector<DecodeResult> Interpreter::DecodeNumStr(
    std::string_view nums,
    const std::vector<Unit>& start,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num_map_.empty() || nums.empty()) return results;
    for (char c : nums) {
        if (c < '2' || c > '9') return results;
    }

    const std::size_t max_top = num == 0 ? 1 : num;

    Scorer::Pos init_pos{};
    float_t init_score = 0.0;
    InitStartState(start, init_pos, init_score);

    const std::size_t d = nums.size();
    std::vector<Node> net;
    InitNumNet(nums, net, false);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(init_score, 0, init_pos, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto tail_states = net.back().states.GetStates();
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], d + 1);
        std::string text = ExtractNumText(path);
        if (text.empty()) continue;
        bool dup = false;
        for (const auto& existing : results) {
            if (existing.text == text) { dup = true; break; }
        }
        if (!dup) {
            results.push_back({std::move(text), {}, -tail_states[rank].score, d});
        }
    }

    return results;
}

bool Interpreter::LoadDict(const std::filesystem::path& path) {
    if (!ready_) return false;
    return dict_.Load(path);
}

std::vector<DecodeResult> Interpreter::DecodeStr(
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
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], net.size() - 1);
        if (path.empty()) {
            continue;
        }
        DecodeResult result;
        result.score = -tail_states[rank].score;
        std::u32string composed;
        composed.reserve(path.size() * 4);
        for (const auto& word : path) {
            composed += ToText(word, units);
            // Build pinyin
            std::string seg = SliceToUnits(units, word.start, word.end);
            if (!seg.empty()) {
                if (!result.units.empty()) result.units += '\'';
                result.units += seg;
            }
        }
        if (composed.empty()) {
            continue;
        }
        result.text = ustr::FromU32(composed);
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
    // Debug tokens: 以=436 已=1888 刚=759 港方=136945 防疫=203512
    constexpr bool kDebug = false;
    constexpr TokenID kDebugTokens[] = {436, 1888, 759, 136945, 203512};
    constexpr const char* kDebugNames[] = {"以", "已", "刚", "港方", "防疫"};
    constexpr std::size_t kDebugCount = 5;

    auto debugName = [&](TokenID id) -> const char* {
        if (!kDebug) return nullptr;
        for (std::size_t i = 0; i < kDebugCount; ++i) {
            if (kDebugTokens[i] == id) return kDebugNames[i];
        }
        return nullptr;
    };

    // Helper: check if token 436 (以) is in backtrace chain of a state
    auto hasTokenInChain = [](const State& st, TokenID target) -> bool {
        const State* s = &st;
        while (s && s->backtrace_state) {
            if (s->backtrace_token == target) return true;
            s = s->backtrace_state;
        }
        return false;
    };

    for (std::size_t col = 0; col < net.size(); ++col) {
        auto& column = net[col];

        if (kDebug) {
            // Log edges at this column for debug tokens
            for (const auto& word : column.es) {
                auto name = debugName(word.id);
                if (name) {
                    std::cerr << "[DBG] col=" << col << " edge: "
                              << name << "(id=" << word.id
                              << ") span=[" << word.start << "," << word.end << ")\n";
                }
            }
            // Log states at this column
            auto states = column.states.GetStates();
            if (!states.empty()) {
                std::cerr << "[DBG] col=" << col << " has " << states.size() << " states\n";
                std::size_t show_limit = std::min<std::size_t>(states.size(), 5);
                for (std::size_t i = 0; i < show_limit; ++i) {
                    auto bname = debugName(states[i].backtrace_token);
                    std::cerr << "  [" << i << "] score=" << states[i].score
                              << " pos=(" << states[i].pos.level
                              << "," << states[i].pos.index << ")"
                              << " via=" << (bname ? bname : std::to_string(states[i].backtrace_token).c_str())
                              << "\n";
                }

                // Per-column: count states with 以(436) in backtrace chain
                std::size_t yi_count = 0;
                float_t yi_best = 1e30;
                float_t worst_score = 0;
                for (const auto& st : states) {
                    if (st.score > worst_score) worst_score = st.score;
                    if (hasTokenInChain(st, 436)) {
                        ++yi_count;
                        if (st.score < yi_best) yi_best = st.score;
                    }
                }
                if (yi_count > 0) {
                    std::cerr << "  [以-TRACK] col=" << col << " 以-paths alive: "
                              << yi_count << " best_score=" << yi_best
                              << " worst_in_col=" << worst_score
                              << " total_states=" << states.size() << "\n";
                    // Show all 以-paths at col 8 and 9 (where it's about to die)
                    if (col >= 8) {
                        for (std::size_t si = 0; si < states.size(); ++si) {
                            if (hasTokenInChain(states[si], 436)) {
                                std::cerr << "    [以-detail] rank=" << si
                                          << " score=" << states[si].score
                                          << " pos=(" << states[si].pos.level
                                          << "," << states[si].pos.index << ")"
                                          << " via=" << states[si].backtrace_token
                                          << " tokens:";
                                std::vector<TokenID> toks;
                                const State* ss = &states[si];
                                while (ss && ss->backtrace_state) {
                                    toks.push_back(ss->backtrace_token);
                                    ss = ss->backtrace_state;
                                }
                                for (auto rit = toks.rbegin(); rit != toks.rend(); ++rit) {
                                    auto nm = debugName(*rit);
                                    if (nm) std::cerr << " " << nm;
                                    else std::cerr << " " << *rit;
                                }
                                std::cerr << "\n";
                            }
                        }
                    }
                } else if (col >= 3) {
                    std::cerr << "  [以-TRACK] col=" << col << " *** 以-paths: NONE (pruned!) ***"
                              << " worst_in_col=" << worst_score
                              << " total_states=" << states.size() << "\n";
                }

                // At last column, backtrace all states to show full paths
                if (col == net.size() - 1) {
                    for (std::size_t i = 0; i < states.size(); ++i) {
                        std::cerr << "  path[" << i << "] score=" << states[i].score << " tokens:";
                        // Collect backtrace tokens
                        std::vector<TokenID> tokens;
                        const State* s = &states[i];
                        while (s && s->backtrace_state) {
                            tokens.push_back(s->backtrace_token);
                            s = s->backtrace_state;
                        }
                        for (auto it2 = tokens.rbegin(); it2 != tokens.rend(); ++it2) {
                            auto n = debugName(*it2);
                            if (n) std::cerr << " " << n;
                            else std::cerr << " " << *it2;
                        }
                        std::cerr << "\n";
                    }
                }
            }
        }

        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.es) {
                Scorer::Pos next_pos{};
                float_t step = scorer_.ScoreMove(value.pos, word.id, next_pos);
                scorer_.Back(next_pos);
                float_t next_cost = value.score + step;

                if (kDebug) {
                    auto name = debugName(word.id);
                    auto prev_name = debugName(value.backtrace_token);
                    if (name) {
                        std::cerr << "[DBG] col=" << col << " transition: "
                                  << (prev_name ? prev_name : "?")
                                  << " -> " << name
                                  << " step=" << step
                                  << " total=" << next_cost
                                  << " -> col " << word.end << "\n";
                    }
                }

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
    if (n.id != ScoreNotToken && n.id != NotToken) {
        const char32_t* chars = trie_.TokenAt(n.id);
        if (chars) {
            std::u32string buffer;
            for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
                buffer.push_back(chars[i]);
            }
            if (!buffer.empty()) return buffer;
        }
    }
    return ustr::ToU32("[" + SliceToUnits(units, n.start, n.end) + "]");
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

std::vector<DecodeResult> Interpreter::DecodeSentence(
    std::string_view input,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;

    // 1. Parse input with byte boundary tracking
    std::vector<Unit> units;
    std::vector<std::size_t> unit_byte_end;
    std::vector<Unit> tail_expansions;
    if (!ParseWithBoundaries(input, units, unit_byte_end, tail_expansions)) {
        return results;
    }

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

    std::unordered_set<std::string> dedup;

    // === Layer 1: Full sentence N-best (covers all input) ===
    {
        const auto tail = net.back().states.GetStates();
        // libime: 1 best + num alternatives = num+1 total full sentences
        const std::size_t full_limit = num + 1;
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        for (std::size_t rank = 0; rank < scan && results.size() < full_limit; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::u32string composed;
            std::string py;
            for (const auto& w : path) {
                composed += ToText(w, units);
                std::string seg = SliceToUnits(units, w.start, w.end);
                if (!seg.empty()) {
                    if (!py.empty()) py += '\'';
                    py += seg;
                }
            }
            if (composed.empty()) continue;
            std::string text_utf8 = ustr::FromU32(composed);
            if (!dedup.insert(text_utf8).second) continue;

            DecodeResult r;
            r.text = std::move(text_utf8);
            r.units = std::move(py);
            r.score = -tail[rank].score;
            r.cnt = total_bytes;
            results.push_back(std::move(r));
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: Individual words/chars from lattice edges at position 0 ===
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
    std::size_t word_count = 0;

    for (const auto& edge : net[0].es) {
        if (edge.id == ScoreNotToken || edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge, units);
        if (text_u32.empty()) continue;
        std::string text_utf8 = ustr::FromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        bool is_single_char = (text_u32.size() == 1);
        if (!is_single_char && ++word_count > MaxPerPrefix) continue;

        std::size_t distance = effective_n - edge.end;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        std::size_t matched_bytes =
            (edge.end <= unit_byte_end.size()) ? unit_byte_end[edge.end - 1]
                                               : total_bytes;
        Scorer::Pos dummy{};
        float_t score =
            -(scorer_.ScoreMove(Scorer::Pos{}, edge.id, dummy)) - dist_penalty;

        DecodeResult r;
        r.text = std::move(text_utf8);
        r.units = SliceToUnits(units, edge.start, edge.end);
        r.score = score;
        r.cnt = matched_bytes;
        results.push_back(std::move(r));
    }

    // Sort Layer 2 by score; Layer 1 stays in front.
    std::sort(results.begin() + static_cast<std::ptrdiff_t>(layer1_size),
              results.end(),
              [](const DecodeResult& a, const DecodeResult& b) {
                  return a.score > b.score;
              });

    // === Dict: inject user dict matches at the front ===
    if (!dict_.Empty()) {
        const std::size_t n = units.size();
        for (std::size_t len = n; len >= 1; --len) {
            auto matches = dict_.Lookup(units.data(), len);
            if (matches.empty()) continue;

            std::size_t matched_bytes =
                (len <= unit_byte_end.size()) ? unit_byte_end[len - 1]
                                              : total_bytes;

            for (std::size_t idx : matches) {
                std::string text = ustr::FromU32(dict_.TextAt(idx));
                // Remove existing duplicate
                std::erase_if(results, [&](const DecodeResult& e) {
                    return e.text == text && e.cnt == matched_bytes;
                });
                DecodeResult r;
                r.text = text;
                r.score = 1e9;
                r.cnt = matched_bytes;
                results.insert(results.begin(), std::move(r));
            }
        }
    }

    return results;
}

} // namespace sime
