#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace sime {

Sime::Sime(const std::filesystem::path& dict_path,
                         const std::filesystem::path& model_path,
                         bool separator)
    : separator_(separator) {
    if (!dict_.Load(dict_path)) {
        return;
    }
    if (!scorer_.Load(model_path)) {
        dict_.Clear();
        return;
    }
    ready_ = true;
}


void Sime::InitNumNet(std::string_view start,
                              std::string_view nums,
                              std::vector<Node>& net,
                              bool expansion,
                              bool separator) const {
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
                    const Dict::Node* node) {
        std::uint32_t count = 0;
        const std::uint32_t* tokens = dict_.GetToken(node, count);
        const std::string* np = &dict_.NodePieces(node);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto tid = static_cast<TokenID>(tokens[i]);
            net[s].es.push_back({s, new_col, tid, np});
        }
    };

    auto walk = [&](auto& self, std::size_t s, std::size_t pos,
                    const Dict::Node* node) -> void {
        if (!node || pos >= total) return;

        // === Prefix letter columns ===
        if (pos < p) {
            char ch = start[pos];

            if (separator && ch == '\'') {
                self(self, s, pos + 1, node);
                return;
            }

            auto try_prefix_pieces = [&](const PieceTable::PieceMap& pmap) {
                std::string key;
                for (std::size_t end = pos + 1;
                     end <= std::min(pos + piece().MaxLen(), p); ++end) {
                    char kc = start[end - 1];
                    if (separator && kc == '\'') break;
                    key.push_back(kc);
                    auto it = pmap.find(key);
                    if (it == pmap.end()) continue;
                    for (const auto& u : it->second) {
                        const Dict::Node* next = dict_.DoMove(node, u);
                        if (!next) continue;
                        emit(s, end, next);
                        self(self, s, end, next);
                    }
                }
            };
            try_prefix_pieces(piece().GetPieceMap());
            try_prefix_pieces(piece().GetPieceMapEn());

            // Tail expansion for prefix letters
            {
                std::size_t tail_end = pos;
                while (tail_end < p && start[tail_end] != '\'') ++tail_end;
                if (tail_end == p) {
                    std::size_t tail_len = p - pos;
                    if (tail_len > 0 && tail_len <= piece().MaxLen()) {
                        std::string tail_str(start.data() + pos, tail_len);
                        bool is_pinyin = false;
                        auto pit = piece().GetPieceMap().find(tail_str);
                        if (pit != piece().GetPieceMap().end()) {
                            for (const auto& u : pit->second) {
                                if (piece().IsPinyin(u)) {
                                    is_pinyin = true;
                                    break;
                                }
                            }
                        }
                        if (!is_pinyin) {
                            auto matches = piece().PieceDat().FindWordsWithPrefix(
                                tail_str, 512);
                            for (const auto& r : matches) {
                                if (r.length <= tail_len) continue;
                                const auto& units =
                                    piece().UnitsByPieceDatIndex(r.value);
                                for (const auto& u : units) {
                                    const Dict::Node* next =
                                        dict_.DoMove(node, u);
                                    if (!next) continue;
                                    emit(s, p, next);
                                    self(self, s, p, next);
                                }
                            }
                            auto matches_en = piece().PieceDatEn().FindWordsWithPrefix(
                                tail_str, 1024);
                            for (const auto& r : matches_en) {
                                if (r.length <= tail_len) continue;
                                const auto& units =
                                    piece().UnitsByPieceDatEnIndex(r.value);
                                for (const auto& u : units) {
                                    const Dict::Node* next =
                                        dict_.DoMove(node, u);
                                    if (!next) continue;
                                    emit(s, p, next);
                                    self(self, s, p, next);
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

        if (separator && nums[dpos] == '\'') {
            self(self, s, pos + 1, node);
            return;
        }

        // Match digits against both pinyin and english num maps.
        auto match_num = [&](const PieceTable::PieceMap& nmap) {
            std::string key;
            for (std::size_t dend = dpos + 1;
                 dend <= std::min(dpos + piece().MaxLen(), d); ++dend) {
                char ch = nums[dend - 1];
                if (separator && ch == '\'') break;
                key.push_back(ch);
                auto it = nmap.find(key);
                if (it == nmap.end()) continue;
                for (const auto& u : it->second) {
                    const Dict::Node* next = dict_.DoMove(node, u);
                    if (!next) continue;
                    const std::size_t new_col = p + dend;
                    emit(s, new_col, next);
                    self(self, s, new_col, next);
                }
            }
        };
        match_num(piece().GetNumMap());
        match_num(piece().GetNumMapEn());

        // Tail expansion for digits (both pinyin and english)
        if (expansion) {
            std::size_t tail_len = 0;
            while (dpos + tail_len < d && nums[dpos + tail_len] != '\'') {
                ++tail_len;
            }
            if (dpos + tail_len == d) {
                std::string tail(nums.substr(dpos, tail_len));
                if (!tail.empty() && tail.size() <= piece().MaxLen()) {
                    // Pinyin expansion
                    auto matches = piece().NumDat().FindWordsWithPrefix(
                        tail, 512);
                    for (const auto& r : matches) {
                        if (r.length <= tail.size()) continue;
                        const auto& units =
                            piece().UnitsByNumDatIndex(r.value);
                        for (const auto& u : units) {
                            const Dict::Node* next = dict_.DoMove(node, u);
                            if (!next) continue;
                            emit(s, total, next);
                        }
                    }
                    // English expansion
                    auto matches_en = piece().NumDatEn().FindWordsWithPrefix(
                        tail, 1024);
                    for (const auto& r : matches_en) {
                        if (r.length <= tail.size()) continue;
                        const auto& units =
                            piece().UnitsByNumDatEnIndex(r.value);
                        for (const auto& u : units) {
                            const Dict::Node* next = dict_.DoMove(node, u);
                            if (!next) continue;
                            emit(s, total, next);
                        }
                    }
                }
            }
        }
    };

    for (std::size_t s = 0; s < total; ++s) {
        if (separator && s < p && start[s] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }
        if (separator && s >= p && nums[s - p] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }
        walk(walk, s, s, dict_.Root());
    }
    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }
    net[total].es.push_back({total, total + 1, SentenceEnd});
}

std::string Sime::ExtractUnits(const std::vector<Link>& path) {
    std::string py;
    for (const auto& link : path) {
        if (link.id == SentenceEnd || link.id == ScoreNotToken ||
            link.id == NotToken) continue;
        if (!link.pieces || link.pieces->empty()) continue;
        if (!py.empty()) py += '\'';
        py += *link.pieces;
    }
    return py;
}

std::string Sime::ExtractText(const std::vector<Link>& path) const {
    std::u32string u32;
    for (const auto& link : path) {
        u32 += ToText(link);
    }
    return TextFromU32(u32);
}

std::string Sime::TextFromU32(std::u32string& u32) {
    for (auto& ch : u32) {
        if (ch == 0x2581) ch = U' ';
    }
    return ustr::FromU32(u32);
}

std::vector<TokenID> Sime::ExtractTokens(
    const std::vector<Link>& path) const {
    std::vector<TokenID> ids;
    for (const auto& link : path) {
        if (link.id == ScoreNotToken || link.id == NotToken) continue;
        ids.push_back(link.id);
    }
    return ids;
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
    const bool can_tail_expand = !nums.empty() && nums.back() != '\'';
    InitNumNet(start, nums, net, can_tail_expand, separator_);

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
            std::string py = ExtractUnits(path);
            results.push_back({std::move(text), std::move(py),
                               ExtractTokens(path),
                               -tail[rank].score, full_cnt});
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: unigram alternatives anchored at column 0 ===
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
    for (const auto& edge : net[0].es) {
        if (edge.id == ScoreNotToken || edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;

        std::string text_utf8 = TextFromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        // Distance: difference between piece path length and input length.
        // Covers both "input not fully consumed" and "candidate expanded beyond input".
        std::size_t piece_len = 0;
        if (edge.pieces) {
            for (char c : *edge.pieces) {
                if (c != '\'') ++piece_len;
            }
        }
        std::size_t consumed = edge.end - edge.start;
        std::size_t distance = (piece_len > consumed) ? (piece_len - consumed)
                             : (total > edge.end)     ? (total - edge.end)
                             : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext) - dist_penalty;

        std::string edge_py = edge.pieces ? *edge.pieces : "";
        std::size_t cnt = edge.end;
        results.push_back({std::move(text_utf8), std::move(edge_py),
                           ExtractTokens({edge}),
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
    InitNumNet(start, nums, net, /*expansion=*/false, separator_);

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
        std::string py = ExtractUnits(path);
        results.push_back({std::move(text), std::move(py),
                           ExtractTokens(path),
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
        } else if (c == ' ') {
            // Space → ▁ (U+2581, UTF-8: E2 96 81)
            lower.push_back(static_cast<char>(0xE2));
            lower.push_back(static_cast<char>(0x96));
            lower.push_back(static_cast<char>(0x81));
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            lower.push_back(c);
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
    InitNet(lower, net, /*expansion=*/false, separator_);

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
        std::string py = ExtractUnits(path);
        results.push_back({std::move(text), std::move(py),
                           ExtractTokens(path),
                           -tail_states[rank].score, input.size()});
    }
    return results;
}

void Sime::InitNet(std::string_view input,
                      std::vector<Node>& net,
                      bool expansion,
                      bool separator) const {
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    const Dict::Node* node) {
        std::uint32_t count = 0;
        const std::uint32_t* tokens = dict_.GetToken(node, count);
        const std::string* np = &dict_.NodePieces(node);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto tid = static_cast<TokenID>(tokens[i]);
            net[s].es.push_back({s, new_col, tid, np});
        }
    };

    // Walk deeper through the trie's outgoing moves until a node
    // with tokens is reached. Used by both piece match (when input
    // is exhausted) and tail expansion.
    constexpr std::size_t MaxDeepWalkDepth = 8;
    auto deep_walk = [&](auto& dself, std::size_t s,
                         const Dict::Node* dn,
                         std::size_t depth) {
        if (depth >= MaxDeepWalkDepth) return;
        std::uint32_t tc = 0;
        dict_.GetToken(dn, tc);
        if (tc > 0) return;
        const auto* moves = dn->GetMove();
        for (std::uint32_t mi = 0; mi < dn->move_count; ++mi) {
            Unit du = moves[mi].unit;
            const Dict::Node* dn2 = dict_.DoMove(dn, du);
            if (!dn2) continue;
            emit(s, total, dn2);
            dself(dself, s, dn2, depth + 1);
        }
    };

    for (std::size_t s = 0; s < total; ++s) {
        // Boundary column: pass-through only.
        if (separator && input[s] == '\'') {
            net[s].es.push_back({s, s + 1, ScoreNotToken});
            continue;
        }

        auto walk = [&](auto& self, std::size_t pos,
                        const Dict::Node* node) -> void {
            if (!node || pos >= total) return;

            // Boundary column — skip forward without consuming trie.
            if (separator && input[pos] == '\'') {
                self(self, pos + 1, node);
                return;
            }

            // 1. Piece matches (pinyin + english)
            auto try_pieces = [&](const PieceTable::PieceMap& pmap) {
                std::string key;
                for (std::size_t end = pos + 1;
                     end <= std::min(pos + piece().MaxLen(), total); ++end) {
                    char ch = input[end - 1];
                    if (separator && ch == '\'') break;
                    key.push_back(ch);
                    auto it = pmap.find(key);
                    if (it == pmap.end()) continue;
                    for (const auto& u : it->second) {
                        const Dict::Node* next = dict_.DoMove(node, u);
                        if (!next) continue;
                        emit(s, end, next);
                        if (end == total && expansion) {
                            deep_walk(deep_walk, s, next, 0);
                        } else if (end < total) {
                            self(self, end, next);
                        }
                    }
                }
            };
            try_pieces(piece().GetPieceMap());
            try_pieces(piece().GetPieceMapEn());

            // 2. Expansion: remaining input is a prefix of a longer
            //    piece that extends beyond the input end.
            //    Skip when the tail is a complete pinyin syllable.
            if (expansion) {
                std::size_t tail_len = total - pos;
                if (tail_len > 0 && tail_len <= piece().MaxLen()) {
                    std::string tail_str(input.data() + pos, tail_len);
                    bool is_pinyin = false;
                    auto pit = piece().GetPieceMap().find(tail_str);
                    if (pit != piece().GetPieceMap().end()) {
                        for (const auto& u : pit->second) {
                            if (piece().IsPinyin(u)) { is_pinyin = true; break; }
                        }
                    }
                    if (!is_pinyin) {
                        // Pinyin expansion
                        auto matches = piece().PieceDat().FindWordsWithPrefix(
                            tail_str, 256);
                        for (const auto& r : matches) {
                            if (r.length <= tail_len) continue;
                            const auto& units =
                                piece().UnitsByPieceDatIndex(r.value);
                            for (const auto& u : units) {
                                const Dict::Node* next =
                                    dict_.DoMove(node, u);
                                if (!next) continue;
                                emit(s, total, next);
                                deep_walk(deep_walk, s, next, 0);
                            }
                        }
                        // English expansion
                        auto matches_en = piece().PieceDatEn().FindWordsWithPrefix(
                            tail_str, 1024);
                        for (const auto& r : matches_en) {
                            if (r.length <= tail_len) continue;
                            const auto& units =
                                piece().UnitsByPieceDatEnIndex(r.value);
                            for (const auto& u : units) {
                                const Dict::Node* next =
                                    dict_.DoMove(node, u);
                                if (!next) continue;
                                emit(s, total, next);
                            }
                        }
                    }
                }
            }

        };

        walk(walk, s, dict_.Root());
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

        // Score by unigram cost, keep top NodeSize
        std::vector<std::pair<float_t, std::size_t>> scored;
        scored.reserve(indices.size());
        for (auto idx : indices) {
            const auto& e = edges[idx];
            if (e.id == ScoreNotToken || e.id == SentenceEnd) {
                scored.push_back({0.0, idx});
                continue;
            }
            Scorer::Pos ppos{};
            Scorer::Pos pnext{};
            scored.push_back({scorer_.ScoreMove(ppos, e.id, pnext), idx});
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
                Scorer::Pos next_pos{};
                float_t step = scorer_.ScoreMove(cur_pos, word.id, next_pos);
                scorer_.Back(next_pos);
                cur_pos = next_pos;

                float_t next_cost = value.score + step;
                State next(next_cost, word.end, cur_pos, &value, word.id,
                           word.pieces);
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
                        state->backtrace_pieces});
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
    const char32_t* chars = dict_.TokenAt(n.id);
    if (chars) {
        for (std::size_t i = 0; chars[i] != 0 && i < 64; ++i) {
            buffer.push_back(chars[i]);
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
    InitNet(lower, net, /*expansion=*/true, separator_);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    std::unordered_set<std::string> dedup;

    // === Layer 1: Full sentence N-best ===
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;
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
            std::string py = ExtractUnits(path);
            // Expansion penalty: pieces length vs input length
            std::size_t piece_len = 0;
            for (char c : py) {
                if (c != '\'') ++piece_len;
            }
            std::size_t distance = (piece_len > total) ? (piece_len - total) : 0;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;
            results.push_back({std::move(text), std::move(py),
                               ExtractTokens(path),
                               -tail[rank].score - dist_penalty, input.size()});
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: word/char alternatives at position 0 ===
    for (const auto& edge : net[0].es) {
        if (edge.id == SentenceEnd) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;
        std::string text_utf8 = TextFromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        std::size_t piece_len = 0;
        if (edge.pieces) {
            for (char c : *edge.pieces) {
                if (c != '\'') ++piece_len;
            }
        }
        std::size_t consumed = edge.end - edge.start;
        std::size_t distance = (piece_len > consumed) ? (piece_len - consumed)
                             : (total > edge.end)     ? (total - edge.end)
                             : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;
        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext) - dist_penalty;

        std::string edge_py = edge.pieces ? *edge.pieces : "";
        results.push_back({std::move(text_utf8), std::move(edge_py),
                           ExtractTokens({edge}),
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
    const std::vector<TokenID>& context,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0) return results;

    Scorer::Pos pos{};
    for (auto tid : context) {
        Scorer::Pos next{};
        scorer_.ScoreMove(pos, tid, next);
        pos = next;
    }

    // Get successor tokens from LM
    auto next_tokens = scorer_.NextTokens(pos, num * 4);

    const auto& ts = dict_.TokenSet();

    std::unordered_set<std::string> seen;
    for (const auto& [tid, pro] : next_tokens) {
        if (results.size() >= num) break;
        if (tid < StartToken) continue;
        if (ts.find(tid) == ts.end()) continue;

        const char32_t* chars = dict_.TokenAt(tid);
        if (!chars || chars[0] == 0) continue;

        std::u32string u32;
        for (std::size_t i = 0; chars[i] != 0; ++i)
            u32.push_back(chars[i]);
        std::string text = TextFromU32(u32);
        if (!seen.insert(text).second) continue;

        results.push_back({std::move(text), {}, {tid}, -pro, 0});
    }

    return results;
}

} // namespace sime
