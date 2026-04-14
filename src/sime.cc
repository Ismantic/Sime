#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace sime {

Sime::Sime(const std::filesystem::path& dict_path,
                         const std::filesystem::path& model_path) {
    if (!dict_.Load(dict_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        dict_.Clear();
        return;
    }
    ready_ = true;
}


std::uint64_t Sime::EdgeKey(std::size_t start, std::size_t end,
                                       TokenID id) {
    return (static_cast<std::uint64_t>(start) << 48) |
           (static_cast<std::uint64_t>(end) << 32) |
           static_cast<std::uint64_t>(id);
}

void Sime::InitNumNet(std::string_view start,
                              std::string_view nums,
                              bool tail_expansion,
                              std::vector<Node>& net,
                              UnitMap* pm) const {
    // Lattice layout:
    //   columns [0, p)     — prefix letter columns (InitNet-style)
    //   columns [p, p+d)   — digit columns (num_map + letter edges)
    //   column  p+d        — SentenceEnd
    //   column  p+d+1      — terminal
    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;

    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    const Dict::Node* node, const std::string& acc) {
        std::uint32_t count = 0;
        const std::uint32_t* tokens = dict_.GetToken(node, count);
        std::uint32_t gi = 0;
        while (gi < count) {
            const std::uint32_t* grp = tokens + gi;
            std::uint16_t glen = 1;
            while (gi < count && (tokens[gi] & GroupEnd) == 0) { ++gi; ++glen; }
            if (gi < count) ++gi;
            auto tid = static_cast<TokenID>(grp[0] & GroupTokenMask);
            net[s].es.push_back({s, new_col, tid, grp, glen});
            if (pm) pm->emplace(EdgeKey(s, new_col, tid), acc);
        }
    };

    auto append_py = [](const std::string& acc, const char* seg) {
        std::string s = seg ? seg : "";
        if (acc.empty()) return s;
        return acc + "'" + s;
    };

    auto walk = [&](auto& self, std::size_t s, std::size_t pos,
                    const Dict::Node* node, const std::string& acc) -> void {
        if (!node || pos >= total) return;

        // === Prefix letter columns ===
        if (pos < p) {
            char ch = start[pos];

            // Boundary — skip without trie move
            if (ch == '\'') {
                self(self, s, pos + 1, node, acc);
                return;
            }

            // Piece matches via piece().GetPieceMap()
            std::string key;
            for (std::size_t end = pos + 1;
                 end <= std::min(pos + piece().MaxLen(), p); ++end) {
                char kc = start[end - 1];
                if (kc == '\'') break;
                key.push_back(kc);
                auto it = piece().GetPieceMap().find(key);
                if (it == piece().GetPieceMap().end()) continue;
                for (const auto& u : it->second) {
                    const Dict::Node* next = dict_.DoMove(node, u);
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
                    if (tail_len > 0 && tail_len <= piece().MaxLen()) {
                        std::string tail_str(start.data() + pos, tail_len);
                        bool is_complete = piece().GetPieceMap().count(tail_str) > 0;
                        if (!is_complete) {
                            for (const auto& [ukey, units] : piece().GetPieceMap()) {
                                if (ukey.size() <= tail_len) continue;
                                if (ukey.compare(0, tail_len, tail_str) != 0)
                                    continue;
                                for (const auto& u : units) {
                                    const Dict::Node* next =
                                        dict_.DoMove(node, u);
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
             dend <= std::min(dpos + piece().MaxLen(), d); ++dend) {
            char ch = nums[dend - 1];
            if (ch == '\'') break;
            key.push_back(ch);
            auto it = piece().GetNumMap().find(key);
            if (it == piece().GetNumMap().end()) continue;
            for (const auto& u : it->second) {
                const Dict::Node* next = dict_.DoMove(node, u);
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
                if (!tail.empty() && tail.size() <= piece().MaxLen()) {
                    for (const auto& [dkey, units] : piece().GetNumMap()) {
                        if (dkey.size() <= tail.size()) continue;
                        if (dkey.compare(0, tail.size(), tail) != 0) continue;
                        for (const auto& u : units) {
                            const Dict::Node* next = dict_.DoMove(node, u);
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
        walk(walk, s, s, dict_.Root(), "");
    }
    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }
    net[total].es.push_back({total, total + 1, SentenceEnd});
}

std::string Sime::ExtractUnits(const std::vector<Link>& path,
                                            const UnitMap& pm) {
    std::string py;
    for (const auto& link : path) {
        if (link.id == SentenceEnd || link.id == ScoreNotToken ||
            link.id == NotToken) continue;
        auto it = pm.find(EdgeKey(link.start, link.end, link.id));
        if (it == pm.end()) continue;
        if (!py.empty()) py += '\'';
        py += it->second;
    }
    return py;
}

std::string Sime::ExtractText(const std::vector<Link>& path) const {
    std::u32string u32;
    for (const auto& link : path) {
        u32 += ToText(link);
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
    UnitMap pm;
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
            std::string text = ExtractText(path);
            if (text.empty() || !dedup.insert(text).second) continue;
            std::string py = ExtractUnits(path, pm);
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
    for (const auto& edge : net[0].es) {
        if (edge.id == ScoreNotToken || edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;

        std::string text_utf8 = ustr::FromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        std::size_t distance = total - edge.end;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos dummy{};
        float_t score =
            -(scorer_.ScoreMove(Scorer::Pos{}, edge.id, dummy)) - dist_penalty;

        auto pit = pm.find(EdgeKey(edge.start, edge.end, edge.id));
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
    UnitMap pm;
    InitNumNet(start, nums, /*tail_expansion=*/false, net, &pm);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto tail_states = net.back().states.GetStates();
    std::unordered_set<std::string> dedup;
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], total + 1);
        std::string text = ExtractText(path);
        if (text.empty() || !dedup.insert(text).second) continue;
        std::string py = ExtractUnits(path, pm);
        results.push_back({std::move(text), std::move(py),
                           -tail_states[rank].score,
                           start.size() + nums.size()});
    }

    return results;
}

namespace {

std::string NormalizeInput(std::string_view input) {
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
    return lower;
}

} // namespace

std::vector<DecodeResult> Sime::DecodeStr(
    std::string_view input,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    std::vector<Node> net;
    UnitMap pm;
    InitNet(lower, net, &pm);

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
        std::string text = ExtractText(path);
        if (text.empty() || !dedup.insert(text).second) continue;
        std::string py = ExtractUnits(path, pm);
        results.push_back({std::move(text), std::move(py),
                           -tail_states[rank].score, input.size()});
    }
    return results;
}

void Sime::InitNet(std::string_view input,
                      std::vector<Node>& net,
                      UnitMap* pm) const {
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    const Dict::Node* node, const std::string& acc) {
        std::uint32_t count = 0;
        const std::uint32_t* tokens = dict_.GetToken(node, count);
        std::uint32_t gi = 0;
        while (gi < count) {
            const std::uint32_t* grp = tokens + gi;
            std::uint16_t glen = 1;
            while (gi < count && (tokens[gi] & GroupEnd) == 0) { ++gi; ++glen; }
            if (gi < count) ++gi;
            auto tid = static_cast<TokenID>(grp[0] & GroupTokenMask);
            net[s].es.push_back({s, new_col, tid, grp, glen});
            if (pm) pm->emplace(EdgeKey(s, new_col, tid), acc);
        }
    };

    auto append_py = [](const std::string& acc, const char* seg) {
        std::string s = seg ? seg : "";
        if (acc.empty()) return s;
        return acc + "'" + s;
    };

    for (std::size_t s = 0; s < total; ++s) {
        // Boundary column: pass-through only.
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }

        auto walk = [&](auto& self, std::size_t pos,
                        const Dict::Node* node,
                        const std::string& acc) -> void {
            if (!node || pos >= total) return;

            // Boundary column — skip forward without consuming trie.
            if (input[pos] == '\'') {
                self(self, pos + 1, node, acc);
                return;
            }

            // 1. Piece matches via piece().GetPieceMap()
            std::string key;
            for (std::size_t end = pos + 1;
                 end <= std::min(pos + piece().MaxLen(), total); ++end) {
                char ch = input[end - 1];
                if (ch == '\'') break;  // Don't extend past boundary.
                key.push_back(ch);
                auto it = piece().GetPieceMap().find(key);
                if (it == piece().GetPieceMap().end()) continue;
                for (const auto& u : it->second) {
                    const Dict::Node* next = dict_.DoMove(node, u);
                    if (!next) continue;
                    std::string new_acc =
                        append_py(acc, piece().Decode(u));
                    emit(s, end, next, new_acc);
                    if (end == total) {
                        // Input exhausted after this piece.
                        // Walk deeper to reach nodes with tokens.
                        constexpr std::size_t MaxDepth = 8;
                        auto deeper = [&](auto& dself,
                                          const Dict::Node* dn,
                                          const std::string& dacc,
                                          std::size_t depth) {
                            if (depth >= MaxDepth) return;
                            std::uint32_t tc = 0;
                            dict_.GetToken(dn, tc);
                            if (tc > 0) return;
                            for (const auto& [dk, dvec] : piece().GetPieceMap()) {
                                for (const auto& du : dvec) {
                                    const Dict::Node* dn2 =
                                        dict_.DoMove(dn, du);
                                    if (!dn2) continue;
                                    std::string da2 =
                                        append_py(dacc, piece().Decode(du));
                                    emit(s, total, dn2, da2);
                                    dself(dself, dn2, da2, depth + 1);
                                }
                            }
                        };
                        deeper(deeper, next, new_acc, 0);
                    } else {
                        self(self, end, next, new_acc);
                    }
                }
            }

            // 2. Tail expansion: remaining input is a prefix of a longer
            //    piece that extends beyond the input end.
            //    Skip when the tail is a complete pinyin syllable.
            //    For non-pinyin pieces, walk deeper until a node with
            //    tokens is found.
            {
                std::size_t tail_len = total - pos;
                if (tail_len > 0 && tail_len <= piece().MaxLen()) {
                    std::string tail_str(input.data() + pos, tail_len);
                    std::string_view tail(tail_str);
                    bool is_pinyin = false;
                    auto pit = piece().GetPieceMap().find(tail_str);
                    if (pit != piece().GetPieceMap().end()) {
                        for (const auto& u : pit->second) {
                            if (piece().IsPinyin(u)) { is_pinyin = true; break; }
                        }
                    }
                    if (!is_pinyin)
                    for (const auto& [ukey, units] : piece().GetPieceMap()) {
                        if (ukey.size() <= tail_len) continue;
                        if (ukey.compare(0, tail_len, tail) != 0) continue;
                        for (const auto& u : units) {
                            const Dict::Node* next =
                                dict_.DoMove(node, u);
                            if (!next) continue;
                            std::string new_acc =
                                append_py(acc, piece().Decode(u));
                            emit(s, total, next, new_acc);
                            // Walk deeper through remaining pieces
                            // until nodes with tokens are reached.
                            constexpr std::size_t MaxTailDepth = 8;
                            auto deep = [&](auto& dself,
                                            const Dict::Node* dn,
                                            const std::string& dacc,
                                            std::size_t depth) {
                                if (depth >= MaxTailDepth) return;
                                std::uint32_t tc = 0;
                                dict_.GetToken(dn, tc);
                                if (tc > 0) return;
                                for (const auto& [dk, dvec] : piece().GetPieceMap()) {
                                    for (const auto& du : dvec) {
                                        const Dict::Node* dn2 =
                                            dict_.DoMove(dn, du);
                                        if (!dn2) continue;
                                        std::string da2 =
                                            append_py(dacc, piece().Decode(du));
                                        emit(s, total, dn2, da2);
                                        dself(dself, dn2, da2, depth + 1);
                                    }
                                }
                            };
                            deep(deep, next, new_acc, 0);
                        }
                    }
                }
            }

        };

        walk(walk, s, dict_.Root(), "");
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
    for (std::size_t col = 0; col < net.size(); ++col) {
        auto& column = net[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.es) {
                Scorer::Pos cur_pos = value.pos;
                float_t step = 0.0;

                if (word.group_len <= 1) {
                    Scorer::Pos next_pos{};
                    step = scorer_.ScoreMove(cur_pos, word.id, next_pos);
                    scorer_.Back(next_pos);
                    cur_pos = next_pos;
                } else {
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
std::u32string Sime::ToText(const Link& n) const {
    if (n.id == ScoreNotToken || n.id == NotToken) {
        return {};
    }
    std::u32string buffer;
    if (n.group_len > 1 && n.group) {
        for (std::uint16_t gi = 0; gi < n.group_len; ++gi) {
            TokenID pid = static_cast<TokenID>(
                n.group[gi] & GroupTokenMask);
            const char32_t* chars = dict_.TokenAt(pid);
            if (chars) {
                for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
                    buffer.push_back(chars[i]);
                }
            }
        }
    } else {
        const char32_t* chars = dict_.TokenAt(n.id);
        if (chars) {
            for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
                buffer.push_back(chars[i]);
            }
        }
    }
    return buffer;
}

std::vector<DecodeResult> Sime::DecodeSentence(
    std::string_view input,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_ || input.empty()) return results;

    std::string lower = NormalizeInput(input);
    if (lower.empty()) return results;

    const std::size_t total = lower.size();

    std::vector<Node> net;
    UnitMap pm;
    InitNet(lower, net, &pm);

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
            std::string text = ExtractText(path);
            if (text.empty() || !dedup.insert(text).second) continue;
            std::string py = ExtractUnits(path, pm);
            results.push_back({std::move(text), std::move(py),
                               -tail[rank].score, input.size()});
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: word/char alternatives at position 0 ===
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
    for (const auto& edge : net[0].es) {
        if (edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;
        std::string text_utf8 = ustr::FromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        std::size_t distance = total - edge.end;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos dummy{};
        float_t score =
            -(scorer_.ScoreMove(Scorer::Pos{}, edge.id, dummy)) -
            dist_penalty;

        auto pit = pm.find(EdgeKey(edge.start, edge.end, edge.id));
        std::string edge_py = (pit != pm.end()) ? pit->second : "";
        results.push_back({std::move(text_utf8), std::move(edge_py),
                           score, edge.end});
    }

    std::sort(
        results.begin() + static_cast<std::ptrdiff_t>(layer1_size),
        results.end(),
        [](const DecodeResult& a, const DecodeResult& b) {
            return a.score > b.score;
        });

    return results;
}

std::vector<DecodeResult> Sime::NextGroups(
    const std::vector<std::string_view>& context,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0) return results;

    // Build scorer context: look up each word's token(s)
    // Collect all token IDs first, then feed with Back() on all but the last.
    std::vector<TokenID> ctx_ids;
    for (auto word : context) {
        std::u32string u32 = ustr::ToU32(word);
        TokenID tid = dict_.TokenFromText(u32);
        if (tid != NotToken) {
            ctx_ids.push_back(tid);
        } else {
            for (auto ch : u32) {
                tid = dict_.TokenFromText(std::u32string(1, ch));
                if (tid != NotToken) ctx_ids.push_back(tid);
            }
        }
    }

    Scorer::Pos pos{};
    for (std::size_t i = 0; i < ctx_ids.size(); ++i) {
        Scorer::Pos next{};
        scorer_.ScoreMove(pos, ctx_ids[i], next);
        // Don't Back() the last token — keep the most specific context
        if (i + 1 < ctx_ids.size()) scorer_.Back(next);
        pos = next;
    }

    // Get successor tokens from LM
    auto next_tokens = scorer_.NextTokens(pos, num * 4);

    const auto& tg = dict_.TokenGroups();

    struct Candidate {
        std::string text;
        float_t score;
    };
    std::vector<Candidate> candidates;

    for (const auto& [tid, pro] : next_tokens) {
        if (tid < StartToken) continue;

        // Only emit words that exist as Groups in the dict
        auto it = tg.find(tid);
        if (it == tg.end()) continue;

        for (const auto& group : it->second) {
            Scorer::Pos g_pos = pos;
            float_t g_score = 0.0;
            std::u32string u32;
            for (auto gid : group) {
                Scorer::Pos g_next{};
                g_score += scorer_.ScoreMove(g_pos, gid, g_next);
                scorer_.Back(g_next);
                g_pos = g_next;
                const char32_t* gc = dict_.TokenAt(gid);
                if (gc) {
                    for (std::size_t i = 0; gc[i] != 0; ++i)
                        u32.push_back(gc[i]);
                }
            }
            if (!u32.empty()) {
                candidates.push_back({ustr::FromU32(u32), g_score});
            }
        }
    }

    // Sort by cost ascending (lower cost = higher probability)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score < b.score; });

    // Dedup and collect
    std::unordered_set<std::string> seen;
    for (auto& c : candidates) {
        if (results.size() >= num) break;
        if (!seen.insert(c.text).second) continue;
        results.push_back({std::move(c.text), {}, -c.score, 0});
    }

    return results;
}

} // namespace sime
