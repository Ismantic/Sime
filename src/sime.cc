#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

namespace sime {

Sime::Sime(const std::filesystem::path& trie_path,
                         const std::filesystem::path& model_path) {
    if (!trie_.Load(trie_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        trie_.Clear();
        return;
    }
    ready_ = true;
}


std::uint64_t Sime::NumEdgeKey(std::size_t start, std::size_t end,
                                       TokenID id) {
    return (static_cast<std::uint64_t>(start) << 48) |
           (static_cast<std::uint64_t>(end) << 32) |
           static_cast<std::uint64_t>(id);
}

void Sime::InitNumNet(std::string_view start,
                              std::string_view nums,
                              bool tail_expansion,
                              std::vector<Node>& net,
                              NumUnitMap* pm) const {
    // Lattice layout:
    //   columns [0, p)     — prefix letter columns (InitMixNet-style)
    //   columns [p, p+d)   — digit columns (num_map + letter edges)
    //   column  p+d        — SentenceEnd
    //   column  p+d+1      — terminal
    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;

    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    const Trie::Node* node, const std::string& acc) {
        std::uint32_t count = 0;
        const std::uint32_t* tokens = trie_.GetToken(node, count);
        std::uint32_t gi = 0;
        while (gi < count) {
            const std::uint32_t* grp = tokens + gi;
            std::uint16_t glen = 1;
            while (gi < count && (tokens[gi] & GroupEnd) == 0) { ++gi; ++glen; }
            if (gi < count) ++gi;
            auto tid = static_cast<TokenID>(grp[0] & GroupTokenMask);
            net[s].es.push_back({s, new_col, tid, grp, glen});
            if (pm) pm->emplace(NumEdgeKey(s, new_col, tid), acc);
        }
    };

    auto append_py = [](const std::string& acc, const char* seg) {
        std::string s = seg ? seg : "";
        if (acc.empty()) return s;
        return acc + "'" + s;
    };

    auto walk = [&](auto& self, std::size_t s, std::size_t pos,
                    const Trie::Node* node, const std::string& acc) -> void {
        if (!node || pos >= total) return;

        // === Prefix letter columns ===
        if (pos < p) {
            char ch = start[pos];

            // Boundary — skip without trie move
            if (ch == '\'') {
                self(self, s, pos + 1, node, acc);
                return;
            }

            // Pinyin syllable matches via piece().GetPieceMap()
            std::string key;
            for (std::size_t end = pos + 1;
                 end <= std::min(pos + MaxSyllableCnt, p); ++end) {
                char kc = start[end - 1];
                if (kc == '\'') break;
                key.push_back(kc);
                auto it = piece().GetPieceMap().find(key);
                if (it == piece().GetPieceMap().end()) continue;
                for (const auto& u : it->second) {
                    const Trie::Node* next = trie_.DoMove(node, u);
                    if (!next) continue;
                    std::string new_acc =
                        append_py(acc, piece().Decode(u));
                    emit(s, end, next, new_acc);
                    self(self, s, end, next, new_acc);
                }
            }

            // Tail expansion: remaining prefix is an incomplete syllable
            {
                std::size_t tail_end = pos;
                while (tail_end < p && start[tail_end] != '\'') ++tail_end;
                if (tail_end == p) {
                    std::size_t tail_len = p - pos;
                    if (tail_len > 0 && tail_len <= MaxSyllableCnt) {
                        std::string tail_str(start.data() + pos, tail_len);
                        bool is_complete = piece().GetPieceMap().count(tail_str) > 0;
                        if (!is_complete) {
                            for (const auto& [ukey, units] : piece().GetPieceMap()) {
                                if (ukey.size() <= tail_len) continue;
                                if (ukey.compare(0, tail_len, tail_str) != 0)
                                    continue;
                                for (const auto& u : units) {
                                    const Trie::Node* next =
                                        trie_.DoMove(node, u);
                                    if (!next) continue;
                                    std::string new_acc = append_py(
                                        acc, piece().Decode(u));
                                    emit(s, p, next, new_acc);
                                    self(self, s, p, next, new_acc);
                                }
                            }
                        }
                    }
                }
            }

            return;
        }

        // === Digit columns ===
        const std::size_t dpos = pos - p;

        // Boundary — skip
        if (nums[dpos] == '\'') {
            self(self, s, pos + 1, node, acc);
            return;
        }

        // Pinyin syllable matches via piece().GetNumMap()
        std::string key;
        for (std::size_t dend = dpos + 1;
             dend <= std::min(dpos + MaxSyllableCnt, d); ++dend) {
            char ch = nums[dend - 1];
            if (ch == '\'') break;
            key.push_back(ch);
            auto it = piece().GetNumMap().find(key);
            if (it == piece().GetNumMap().end()) continue;
            for (const auto& u : it->second) {
                const Trie::Node* next = trie_.DoMove(node, u);
                if (!next) continue;
                std::string new_acc = append_py(acc, piece().Decode(u));
                const std::size_t new_col = p + dend;
                emit(s, new_col, next, new_acc);
                self(self, s, new_col, next, new_acc);
            }
        }

        // Tail expansion for digits
        if (tail_expansion) {
            std::size_t tail_len = 0;
            while (dpos + tail_len < d && nums[dpos + tail_len] != '\'') {
                ++tail_len;
            }
            if (dpos + tail_len == d) {
                std::string tail(nums.substr(dpos, tail_len));
                if (!tail.empty() && tail.size() <= MaxSyllableCnt) {
                    for (const auto& [dkey, units] : piece().GetNumMap()) {
                        if (dkey.size() <= tail.size()) continue;
                        if (dkey.compare(0, tail.size(), tail) != 0) continue;
                        for (const auto& u : units) {
                            const Trie::Node* next = trie_.DoMove(node, u);
                            if (!next) continue;
                            std::string new_acc =
                                append_py(acc, piece().Decode(u));
                            emit(s, total, next, new_acc);
                        }
                    }
                }
            }
        }
    };

    for (std::size_t s = 0; s < total; ++s) {
        if (s < p && start[s] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }
        if (s >= p && nums[s - p] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }
        walk(walk, s, s, trie_.Root(), "");
    }
    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }
    net[total].es.push_back({total, total + 1, SentenceEnd});
}

std::string Sime::ExtractNumUnits(const std::vector<Link>& path,
                                            const NumUnitMap& pm) {
    std::string py;
    for (const auto& link : path) {
        if (link.id == SentenceEnd || link.id == ScoreNotToken ||
            link.id == NotToken) continue;
        auto it = pm.find(NumEdgeKey(link.start, link.end, link.id));
        if (it == pm.end()) continue;
        if (!py.empty()) py += '\'';
        py += it->second;
    }
    return py;
}

std::string Sime::ExtractNumText(const std::vector<Link>& path) const {
    std::u32string u32;
    for (const auto& link : path) {
        if (link.id == SentenceEnd || link.id == ScoreNotToken ||
            link.id == NotToken) continue;
        if (link.group_len > 1 && link.group) {
            for (std::uint16_t gi = 0; gi < link.group_len; ++gi) {
                TokenID pid = static_cast<TokenID>(
                    link.group[gi] & GroupTokenMask);
                const char32_t* chars = trie_.TokenAt(pid);
                if (!chars) continue;
                for (std::size_t i = 0; chars[i] != 0; ++i) {
                    u32.push_back(chars[i]);
                }
            }
        } else {
            const char32_t* chars = trie_.TokenAt(link.id);
            if (!chars) continue;
            for (std::size_t i = 0; chars[i] != 0; ++i) {
                u32.push_back(chars[i]);
            }
        }
    }
    return ustr::FromU32(u32);
}

std::vector<DecodeResult> Sime::DecodeNumSentence(
    std::string_view nums,
    std::string_view start,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;  // boundary hint, handled by the net builder
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;
    if (!nums.empty() && piece().GetNumMap().empty()) return results;

    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;

    std::vector<Node> net;
    NumUnitMap pm;
    const bool can_tail_expand = !nums.empty() && nums.back() != '\'';
    InitNumNet(start, nums, can_tail_expand, net, &pm);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    std::unordered_set<std::string> dedup;

    // === Layer 1: Full sentence N-best (covers prefix + all digits) ===
    // Returns 1 + extra entries (the top sentence is always included;
    // `extra` additional alternatives are appended).
    {
        const auto tail = net[total + 1].states.GetStates();
        const std::size_t full_limit = 1 + extra;
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        // cnt = total bytes consumed = start (already confirmed) + all digits.
        // Aligned with DecodeSentence which returns total input bytes.
        const std::size_t full_cnt = start.size() + d;
        for (std::size_t rank = 0;
             rank < scan && results.size() < full_limit; ++rank) {
            auto path = Backtrace(tail[rank], total + 1);
            std::string text = ExtractNumText(path);
            if (text.empty() || !dedup.insert(text).second) continue;
            std::string py = ExtractNumUnits(path, pm);
            results.push_back({std::move(text), std::move(py),
                               -tail[rank].score, full_cnt});
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: unigram alternatives anchored at column 0 ===
    // Each edge in net[0].es is one trie token starting from column 0.
    // The first pc columns of the lattice are fixed to start's syllables,
    // so any emitted token's pinyin is some prefix of (start + nums).
    // Multi-syllable trie tokens (e.g. 你好) are legitimate unigrams and
    // are included. Scoring is unigram (Scorer::Pos{}, no LM context).
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
    std::size_t word_count = 0;

    for (const auto& edge : net[0].es) {
        if (edge.id == ScoreNotToken || edge.id == SentenceEnd) continue;

        std::u32string text_u32;
        if (edge.group_len > 1 && edge.group) {
            for (std::uint16_t gi = 0; gi < edge.group_len; ++gi) {
                TokenID pid = static_cast<TokenID>(
                    edge.group[gi] & GroupTokenMask);
                const char32_t* chars = trie_.TokenAt(pid);
                if (chars) {
                    for (std::size_t i = 0; chars[i] != 0; ++i)
                        text_u32.push_back(chars[i]);
                }
            }
        } else {
            const char32_t* chars = trie_.TokenAt(edge.id);
            if (chars) {
                for (std::size_t i = 0; chars[i] != 0; ++i)
                    text_u32.push_back(chars[i]);
            }
        }
        if (text_u32.empty()) continue;

        std::string text_utf8 = ustr::FromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        bool is_single_char = (text_u32.size() == 1);
        if (!is_single_char && ++word_count > MaxPerPrefix) continue;

        std::size_t distance = total - edge.end;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos dummy{};
        float_t score =
            -(scorer_.ScoreMove(Scorer::Pos{}, edge.id, dummy)) - dist_penalty;

        auto pit = pm.find(NumEdgeKey(edge.start, edge.end, edge.id));
        std::string edge_py = (pit != pm.end()) ? pit->second : "";
        std::size_t cnt = edge.end;
        results.push_back({std::move(text_utf8), std::move(edge_py),
                           score, cnt});
    }

    // Sort Layer 2 by score; Layer 1 stays in front.
    std::sort(results.begin() + static_cast<std::ptrdiff_t>(layer1_size),
              results.end(),
              [](const DecodeResult& a, const DecodeResult& b) {
                  return a.score > b.score;
              });

    return results;
}

std::vector<DecodeResult> Sime::DecodeNumStr(
    std::string_view nums,
    std::string_view start,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;  // boundary hint, honored by InitNumNet
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;
    if (!nums.empty() && piece().GetNumMap().empty()) return results;

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t total = start.size() + nums.size();

    std::vector<Node> net;
    NumUnitMap pm;
    InitNumNet(start, nums, /*tail_expansion=*/false, net, &pm);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto tail_states = net.back().states.GetStates();
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], total + 1);
        std::string text = ExtractNumText(path);
        if (text.empty()) continue;
        bool dup = false;
        for (const auto& existing : results) {
            if (existing.text == text) { dup = true; break; }
        }
        if (!dup) {
            std::string py = ExtractNumUnits(path, pm);
            results.push_back({std::move(text), std::move(py),
                               -tail_states[rank].score,
                               start.size() + nums.size()});
        }
    }

    return results;
}

std::vector<DecodeResult> Sime::DecodeStr(
    std::string_view input,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    // Lowercase input, keep only letters and apostrophe boundaries
    std::string lower;
    lower.reserve(input.size());
    for (char c : input) {
        if (c == '\'') {
            lower.push_back(c);
        } else {
            char lc = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (lc >= 'a' && lc <= 'z') {
                lower.push_back(lc);
            }
        }
    }
    if (lower.empty()) return results;

    std::vector<Node> net;
    InitMixNet(lower, net);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    const auto tail_states = net.back().states.GetStates();
    std::unordered_set<std::string> dedup;
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], net.size() - 1);
        if (path.empty()) continue;

        std::u32string composed;
        for (const auto& w : path) {
            composed += ToText(w, {});
        }
        if (composed.empty()) continue;

        std::string text_utf8 = ustr::FromU32(composed);
        if (!dedup.insert(text_utf8).second) continue;

        DecodeResult r;
        r.text = std::move(text_utf8);
        r.score = -tail_states[rank].score;
        r.cnt = input.size();
        results.push_back(std::move(r));
    }
    return results;
}

void Sime::InitMixNet(std::string_view input,
                      std::vector<Node>& net) const {
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);

    for (std::size_t s = 0; s < total; ++s) {
        // Boundary column: pass-through only.
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }

        auto& bucket = net[s].es;

        auto walk = [&](auto& self, std::size_t pos,
                        const Trie::Node* node) -> void {
            if (!node || pos >= total) return;

            // Boundary column — skip forward without consuming trie.
            if (input[pos] == '\'') {
                self(self, pos + 1, node);
                return;
            }

            // 1. Pinyin syllable matches via piece().GetPieceMap()
            std::string key;
            for (std::size_t end = pos + 1;
                 end <= std::min(pos + MaxSyllableCnt, total); ++end) {
                char ch = input[end - 1];
                if (ch == '\'') break;  // Don't extend past boundary.
                key.push_back(ch);
                auto it = piece().GetPieceMap().find(key);
                if (it == piece().GetPieceMap().end()) continue;
                for (const auto& u : it->second) {
                    const Trie::Node* next = trie_.DoMove(node, u);
                    if (!next) continue;
                    std::uint32_t count = 0;
                    const std::uint32_t* tokens =
                        trie_.GetToken(next, count);
                    std::uint32_t gi = 0;
                    while (gi < count) {
                        const std::uint32_t* grp = tokens + gi;
                        std::uint16_t glen = 1;
                        while (gi < count &&
                               (tokens[gi] & GroupEnd) == 0) {
                            ++gi; ++glen;
                        }
                        if (gi < count) ++gi;
                        TokenID wid = static_cast<TokenID>(
                            grp[0] & GroupTokenMask);
                        bucket.push_back({s, end, wid, grp, glen});
                    }
                    self(self, end, next);
                }
            }

            // 2. Tail expansion: remaining input is a prefix of a longer
            //    pinyin syllable that extends beyond the input end.
            //    Only fires when the tail is NOT itself a complete syllable.
            {
                std::size_t tail_len = total - pos;
                if (tail_len > 0 && tail_len <= MaxSyllableCnt) {
                    std::string tail_str(input.data() + pos, tail_len);
                    std::string_view tail(tail_str);
                    bool is_complete = piece().GetPieceMap().count(tail_str) > 0;
                    if (!is_complete)
                    for (const auto& [ukey, units] : piece().GetPieceMap()) {
                        if (ukey.size() <= tail_len) continue;
                        if (ukey.compare(0, tail_len, tail) != 0) continue;
                        for (const auto& u : units) {
                            const Trie::Node* next =
                                trie_.DoMove(node, u);
                            if (!next) continue;
                            std::uint32_t count = 0;
                            const std::uint32_t* tokens =
                                trie_.GetToken(next, count);
                            std::uint32_t gi = 0;
                            while (gi < count) {
                                const std::uint32_t* grp = tokens + gi;
                                std::uint16_t glen = 1;
                                while (gi < count &&
                                       (tokens[gi] & GroupEnd) == 0) {
                                    ++gi; ++glen;
                                }
                                if (gi < count) ++gi;
                                TokenID wid = static_cast<TokenID>(
                                    grp[0] & GroupTokenMask);
                                bucket.push_back(
                                    {s, total, wid, grp, glen});
                            }
                        }
                    }
                }
            }

        };

        walk(walk, s, trie_.Root());
    }

    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }

    net[total].es.push_back({total, total + 1, SentenceEnd});
}

void Sime::PruneNode(std::vector<Link>& edges) const {
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

void Sime::Process(std::vector<Node>& net) const {
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
                Scorer::Pos cur_pos = value.pos;
                float_t step = 0.0;

                if (word.group_len <= 1) {
                    // Single token (Chinese or special)
                    Scorer::Pos next_pos{};
                    step = scorer_.ScoreMove(cur_pos, word.id, next_pos);
                    scorer_.Back(next_pos);
                    cur_pos = next_pos;
                } else {
                    // Compound token — chain multiply
                    for (std::uint16_t gi = 0; gi < word.group_len; ++gi) {
                        TokenID pid = static_cast<TokenID>(
                            word.group[gi] & GroupTokenMask);
                        Scorer::Pos next_pos{};
                        step += scorer_.ScoreMove(cur_pos, pid, next_pos);
                        scorer_.Back(next_pos);
                        cur_pos = next_pos;
                    }
                }

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

                State next(next_cost, word.end, cur_pos, &value, word.id,
                           word.group, word.group_len);
                net[word.end].states.Insert(next);
            }
        }
    }
}

std::vector<Sime::Link> Sime::Backtrace(
    const State& tail_state,
    std::size_t end) {
    std::vector<Link> path;
    const State* state = &tail_state;
    while (state != nullptr && state->backtrace_state != nullptr) {
        const State* prev = state->backtrace_state;
        path.push_back({prev->now, state->now,
                        state->backtrace_token,
                        state->backtrace_group,
                        state->backtrace_group_len});
        state = prev;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().end == end) {
        path.pop_back();
    }
    return path;
}
std::u32string Sime::ToText(const Link& n,
                                   const std::vector<Unit>& units) const {
    if (n.id != ScoreNotToken && n.id != NotToken) {
        if (n.group_len > 1 && n.group) {
            // Compound: concatenate display text of each piece
            std::u32string buffer;
            for (std::uint16_t gi = 0; gi < n.group_len; ++gi) {
                TokenID pid = static_cast<TokenID>(
                    n.group[gi] & GroupTokenMask);
                const char32_t* chars = trie_.TokenAt(pid);
                if (chars) {
                    for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
                        buffer.push_back(chars[i]);
                    }
                }
            }
            if (!buffer.empty()) return buffer;
        } else {
            const char32_t* chars = trie_.TokenAt(n.id);
            if (chars) {
                std::u32string buffer;
                for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
                    buffer.push_back(chars[i]);
                }
                if (!buffer.empty()) return buffer;
            }
        }
    }
    return ustr::ToU32("[" + SliceToUnits(units, n.start, n.end) + "]");
}

std::string Sime::SliceToUnits(
    const std::vector<Unit>& units,
    std::size_t start,
    std::size_t end) const {
    std::string result;
    for (std::size_t i = start; i < end && i < units.size(); ++i) {
        const char* syl = piece().Decode(units[i]);
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

std::vector<DecodeResult> Sime::DecodeSentence(
    std::string_view input,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    // Lowercase the input, keep only letters and apostrophe boundaries
    std::string lower;
    lower.reserve(input.size());
    for (char c : input) {
        if (c == '\'') {
            lower.push_back(c);
        } else {
            char lc = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (lc >= 'a' && lc <= 'z') {
                lower.push_back(lc);
            }
        }
    }
    if (lower.empty()) return results;

    const std::size_t total = lower.size();

    std::vector<Node> net;
    InitMixNet(lower, net);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    std::unordered_set<std::string> dedup;

    // === Layer 1: Full sentence N-best ===
    {
        const auto tail = net.back().states.GetStates();
        const std::size_t full_limit = 1 + extra;
        const std::size_t scan =
            std::min<std::size_t>(BeamSize, tail.size());
        for (std::size_t rank = 0;
             rank < scan && results.size() < full_limit; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::u32string composed;
            for (const auto& w : path) {
                composed += ToText(w, {});
            }
            if (composed.empty()) continue;
            std::string text_utf8 = ustr::FromU32(composed);
            if (!dedup.insert(text_utf8).second) continue;

            DecodeResult r;
            r.text = std::move(text_utf8);
            r.score = -tail[rank].score;
            r.cnt = input.size();
            results.push_back(std::move(r));
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: word/char alternatives at position 0 ===
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
    std::size_t word_count = 0;

    for (const auto& edge : net[0].es) {
        if (edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge, {});
        if (text_u32.empty()) continue;
        std::string text_utf8 = ustr::FromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        bool is_single_char = (text_u32.size() == 1);
        if (!is_single_char && ++word_count > MaxPerPrefix) continue;

        std::size_t distance = total - edge.end;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos dummy{};
        float_t score =
            -(scorer_.ScoreMove(Scorer::Pos{}, edge.id, dummy)) -
            dist_penalty;

        DecodeResult r;
        r.text = std::move(text_utf8);
        r.score = score;
        r.cnt = edge.end;
        results.push_back(std::move(r));
    }

    std::sort(
        results.begin() + static_cast<std::ptrdiff_t>(layer1_size),
        results.end(),
        [](const DecodeResult& a, const DecodeResult& b) {
            return a.score > b.score;
        });

    return results;
}

} // namespace sime
