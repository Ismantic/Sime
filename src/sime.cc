#include "sime.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace sime {

namespace {

// Count syllables in `pieces` whose letters are not fully present in
// `input_slice`. When `input_is_digits` is true, compare pieces letters
// against their T9 digit form instead. Used to penalize abbreviated or
// tail-expanded matches relative to fully-typed ones.
std::size_t CountSyllableMismatch(const char* pieces,
                                  std::string_view input_slice,
                                  bool input_is_digits) {
    if (!pieces) return 0;
    std::size_t mismatch = 0;
    std::size_t ipos = 0;
    const char* p = pieces;
    while (*p) {
        const char* syl_end = p;
        while (*syl_end && *syl_end != '\'') ++syl_end;
        std::size_t syl_len = static_cast<std::size_t>(syl_end - p);

        while (ipos < input_slice.size() && input_slice[ipos] == '\'') ++ipos;

        std::size_t matched = 0;
        while (matched < syl_len && ipos < input_slice.size() &&
               input_slice[ipos] != '\'') {
            char expected = input_is_digits
                ? Dict::LetterToNum(p[matched])
                : p[matched];
            if (input_slice[ipos] != expected) break;
            ++matched;
            ++ipos;
        }
        if (matched < syl_len) ++mismatch;

        p = (*syl_end == '\'') ? syl_end + 1 : syl_end;
    }
    return mismatch;
}

} // namespace

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


void Sime::InitNumNet(std::string_view start,
                              std::string_view nums,
                              std::vector<Node>& net,
                              bool expansion) const {
    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;

    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    TokenID tid, const char* pieces) {
        net[s].es.push_back({s, new_col, tid, pieces});
    };

    auto emit_dat = [&](std::size_t s, Dict::DatType type,
                        std::string_view suffix, std::size_t max_end,
                        bool pinyin) {
        auto results = pinyin
            ? dict_.Dat(type).PrefixSearchPinyin(suffix, 512)
            : dict_.Dat(type).PrefixSearch(suffix, 512);
        for (const auto& r : results) {
            std::size_t new_col = s + r.length;
            if (new_col > max_end) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                emit(s, new_col, entry.items[i].id, entry.items[i].pieces);
            }
        }
    };

    auto expand_dat = [&](std::size_t s, Dict::DatType type,
                          std::string_view tail, std::size_t target_col,
                          std::size_t max_num, bool pinyin) {
        auto results = pinyin
            ? dict_.Dat(type).FindWordsWithPrefixPinyin(tail, max_num)
            : dict_.Dat(type).FindWordsWithPrefix(tail, max_num);
        for (const auto& r : results) {
            if (r.length <= tail.size()) continue;
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                emit(s, target_col, entry.items[i].id, entry.items[i].pieces);
            }
        }
    };

    for (std::size_t s = 0; s < total; ++s) {
        // === Prefix letter columns ===
        if (s < p) {
            if (start[s] == '\'') {
                net[s].es.push_back({s, s + 1, NotToken});
                continue;
            }

            std::string_view suffix = start.substr(s);
            emit_dat(s, Dict::LetterPinyin, suffix, p, true);
            emit_dat(s, Dict::LetterEn, suffix, p, false);

            // Tail expansion for prefix letters
            if (expansion) {
                std::size_t tail_end = s;
                while (tail_end < p && start[tail_end] != '\'') ++tail_end;
                if (tail_end == p) {
                    std::string tail_clean;
                    for (std::size_t i = s; i < p; ++i) {
                        if (start[i] == '\'') continue;
                        tail_clean.push_back(start[i]);
                    }
                    if (!tail_clean.empty() && !Dict::IsKnownPinyin(tail_clean)) {
                        expand_dat(s, Dict::LetterPinyin, suffix, p, 512, true);
                        expand_dat(s, Dict::LetterEn, tail_clean, p, 1024, false);
                    }
                }
            }
            continue;
        }

        // === Digit columns ===
        const std::size_t dpos = s - p;

        if (nums[dpos] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        // T9: expand digits to letters and search letter DATs
        std::string_view num_suffix = nums.substr(dpos);
        auto t9_emit = [&](Dict::DatType type, std::size_t max_num) {
            auto results = dict_.Dat(type).PrefixSearchT9(
                num_suffix, Dict::NumToLetters, max_num);
            for (const auto& r : results) {
                std::size_t new_col = s + r.length;
                if (new_col > total) continue;
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, new_col, entry.items[i].id, entry.items[i].pieces);
                }
            }
        };
        t9_emit(Dict::LetterPinyin, 512);
        t9_emit(Dict::LetterEn, 512);

        // Tail expansion for digits
        if (expansion) {
            std::size_t tail_len = 0;
            while (dpos + tail_len < d && nums[dpos + tail_len] != '\'')
                ++tail_len;
            if (dpos + tail_len == d && tail_len > 0) {
                std::string tail(nums.substr(dpos, tail_len));
                auto t9_expand = [&](Dict::DatType type, std::size_t max_num) {
                    auto results = dict_.Dat(type).FindWordsWithPrefixT9(
                        tail, Dict::NumToLetters, max_num);
                    for (const auto& r : results) {
                        if (r.length <= tail.size()) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            emit(s, total, entry.items[i].id,
                                 entry.items[i].pieces);
                        }
                    }
                };
                t9_expand(Dict::LetterPinyin, 512);
                t9_expand(Dict::LetterEn, 1024);
            }
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }
    net[total].es.push_back({total, total + 1, NotToken});
}

std::string Sime::AbbreviatePieces(const char* full_pieces,
                                   std::string_view input) {
    std::string result;
    std::size_t ipos = 0;
    const char* p = full_pieces;
    bool first = true;
    while (*p) {
        if (!first) result += '\'';
        first = false;
        // Current syllable: p up to next '\'' or end
        const char* syl_end = p;
        while (*syl_end && *syl_end != '\'') ++syl_end;
        // Match input chars against syllable chars
        const char* s = p;
        std::size_t syl_start = result.size();
        while (s < syl_end && ipos < input.size() && input[ipos] != '\'') {
            if (input[ipos] == *s) {
                result.push_back(input[ipos]);
                ++ipos;
                ++s;
            } else {
                break;
            }
        }
        if (result.size() == syl_start) {
            // No chars matched for this syllable — stop
            if (!result.empty()) result.pop_back();  // remove trailing '\''
            break;
        }
        // Skip '\'' in input if present
        if (ipos < input.size() && input[ipos] == '\'') ++ipos;
        // Advance pieces past this syllable
        p = (*syl_end == '\'') ? syl_end + 1 : syl_end;
    }
    // If nothing matched, return full pieces as-is
    if (result.empty()) return full_pieces;
    return result;
}

std::string Sime::ExtractUnits(const std::vector<Link>& path,
                               std::string_view input) {
    std::string py;
    for (const auto& link : path) {
        if (link.id == NotToken) continue;
        if (!link.pieces || link.pieces[0] == '\0') continue;
        if (!py.empty()) py += '\'';
        py += AbbreviatePieces(link.pieces,
                               input.substr(link.start, link.end - link.start));
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
        if (link.id == NotToken) continue;
        ids.push_back(link.id);
    }
    return ids;
}

std::size_t Sime::CountPathMismatch(const std::vector<Link>& path,
                                    std::string_view input,
                                    std::size_t t9_boundary) {
    std::size_t mismatch = 0;
    for (const auto& link : path) {
        if (link.id == NotToken || !link.pieces) continue;
        bool is_t9 = link.start >= t9_boundary;
        auto slice = input.substr(link.start, link.end - link.start);
        mismatch += CountSyllableMismatch(link.pieces, slice, is_t9);
    }
    return mismatch;
}

std::vector<DecodeResult> Sime::DecodeNumSentence(
    std::string_view nums,
    std::string_view start,
    std::size_t extra) const {
    std::vector<DecodeResult> results;
    if (!ready_) return results;
    for (char c : nums) {
        if (c == '\'') continue;
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;

    const std::size_t p = start.size();
    const std::size_t d = nums.size();
    const std::size_t total = p + d;
    std::string combined_input(start);
    combined_input += nums;

    std::vector<Node> net;
    const bool can_tail_expand = !nums.empty() && nums.back() != '\'';
    InitNumNet(start, nums, net, can_tail_expand);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // See DecodeSentence: Layer 1 dedups with a local set so beam members
    // that don't make the 1+extra cut remain eligible for Layer 2.
    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    // === Layer 1: Full sentence N-best ===
    // Scan the whole beam, re-score with fragment penalty (letter links
    // compared directly, T9 digit links compared via LetterToNum), sort
    // by combined score, then take top (1 + extra).
    {
        const auto tail = net[total + 1].states.GetStates();
        const std::size_t scan = std::min<std::size_t>(BeamSize, tail.size());
        const std::size_t full_cnt = start.size() + d;
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], total + 1);
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, combined_input);

            std::size_t mismatch = CountPathMismatch(path, combined_input, p);
            float_t frag_penalty =
                static_cast<float_t>(mismatch) * PinyinMatchPenalty;

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score - frag_penalty, full_cnt});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: unigram alternatives at column 0 ===
    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;

        std::string text_utf8 = TextFromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        bool is_t9 = (edge.start >= p);
        auto slice = std::string_view(combined_input).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, is_t9);
        float_t frag_penalty =
            static_cast<float_t>(mismatch) * PinyinMatchPenalty;

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty - frag_penalty;

        std::string edge_py = edge.pieces ? edge.pieces : "";
        std::size_t cnt = edge.end;
        results.push_back({std::move(text_utf8), std::move(edge_py),
                           ExtractTokens({edge}),
                           score, cnt});
    }

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
        if (c == '\'') continue;
        if (c < '2' || c > '9') return results;
    }
    if (nums.empty() && start.empty()) return results;

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t total = start.size() + nums.size();
    std::string combined_input(start);
    combined_input += nums;

    std::vector<Node> net;
    InitNumNet(start, nums, net, /*expansion=*/false);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    auto tail_states = net.back().states.GetStates();
    std::unordered_set<std::string> dedup;
    for (std::size_t rank = 0;
         rank < tail_states.size() && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], total + 1);
        std::string text = ExtractText(path);
        if (text.empty() || !dedup.insert(text).second) continue;
        std::string py = ExtractUnits(path, combined_input);
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
    InitNet(lower, net, /*expansion=*/true);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
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
        std::string py = ExtractUnits(path, lower);
        results.push_back({std::move(text), std::move(py),
                           ExtractTokens(path),
                           -tail_states[rank].score, input.size()});
    }
    return results;
}

void Sime::InitNet(std::string_view input,
                      std::vector<Node>& net,
                      bool expansion) const {
    const std::size_t total = input.size();
    net.clear();
    net.resize(total + 2);

    auto emit = [&](std::size_t s, std::size_t new_col,
                    TokenID tid, const char* pieces) {
        net[s].es.push_back({s, new_col, tid, pieces});
    };

    for (std::size_t s = 0; s < total; ++s) {
        if (input[s] == '\'') {
            net[s].es.push_back({s, s + 1, NotToken});
            continue;
        }

        // Pinyin: PrefixSearchPinyin handles separator skipping
        // and syllable abbreviation at query time.
        // English: plain PrefixSearch (no abbreviation).
        std::string_view suffix = input.substr(s);

        auto emit_results = [&](const std::vector<trie::SearchResult>& results,
                                Dict::DatType type) {
            for (const auto& r : results) {
                auto entry = dict_.GetEntry(type, r.value);
                for (uint32_t i = 0; i < entry.count; ++i) {
                    emit(s, s + r.length, entry.items[i].id,
                         entry.items[i].pieces);
                }
            }
        };

        emit_results(
            dict_.Dat(Dict::LetterPinyin).PrefixSearchPinyin(suffix, 512),
            Dict::LetterPinyin);
        emit_results(
            dict_.Dat(Dict::LetterEn).PrefixSearch(suffix, 512),
            Dict::LetterEn);

        // Tail expansion
        if (expansion) {
            std::string tail_clean;
            for (char c : suffix) {
                if (c == '\'') continue;
                tail_clean.push_back(c);
            }
            if (!tail_clean.empty() && !Dict::IsKnownPinyin(tail_clean)) {
                auto expand_results = [&](
                    const std::vector<trie::SearchResult>& results,
                    Dict::DatType type, std::size_t tail_len) {
                    for (const auto& r : results) {
                        if (r.length <= tail_len) continue;
                        auto entry = dict_.GetEntry(type, r.value);
                        for (uint32_t i = 0; i < entry.count; ++i) {
                            emit(s, total, entry.items[i].id,
                                 entry.items[i].pieces);
                        }
                    }
                };
                expand_results(
                    dict_.Dat(Dict::LetterPinyin).FindWordsWithPrefixPinyin(
                        suffix, 512),
                    Dict::LetterPinyin, suffix.size());
                expand_results(
                    dict_.Dat(Dict::LetterEn).FindWordsWithPrefix(
                        tail_clean, 1024),
                    Dict::LetterEn, tail_clean.size());
            }
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        PruneNode(net[i].es);
    }

    net[total].es.push_back({total, total + 1, NotToken});
}

void Sime::PruneNode(std::vector<Link>& edges) const {
    if (edges.size() <= NodeSize) return;

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

        std::vector<std::pair<float_t, std::size_t>> scored;
        scored.reserve(indices.size());
        for (auto idx : indices) {
            const auto& e = edges[idx];
            if (e.id == NotToken) {
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
                return a.first < b.first;
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
    if (n.id == NotToken) {
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
    InitNet(lower, net, /*expansion=*/true);

    for (auto& col : net) col.states.SetMaxTop(BeamSize);
    State init(0.0, 0, scorer_.StartPos(), nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // `dedup` tracks texts already emitted in `results`; Layer 2 checks
    // against it to avoid duplicating a candidate that made it into Layer 1.
    // Layer 1 uses its own local dedup during beam scan so that beam members
    // that don't make the final 1+extra cut remain eligible for Layer 2.
    std::unordered_set<std::string> dedup;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / DistancePenalty;

    // === Layer 1: Full sentence N-best ===
    // Scan the whole beam, re-score with distance + fragment penalties,
    // sort by the combined score, then take top (1 + extra).
    {
        const auto tail = net.back().states.GetStates();
        const std::size_t scan =
            std::min<std::size_t>(BeamSize, tail.size());
        std::vector<DecodeResult> l1;
        l1.reserve(scan);
        std::unordered_set<std::string> l1_seen;
        for (std::size_t rank = 0; rank < scan; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::string text = ExtractText(path);
            if (text.empty() || !l1_seen.insert(text).second) continue;
            std::string py = ExtractUnits(path, lower);

            std::size_t piece_len = 0;
            for (char c : py) {
                if (c != '\'') ++piece_len;
            }
            std::size_t distance = (piece_len > total) ? (piece_len - total) : 0;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;

            std::size_t mismatch = CountPathMismatch(path, lower, total + 1);
            float_t frag_penalty =
                static_cast<float_t>(mismatch) * PinyinMatchPenalty;

            l1.push_back({std::move(text), std::move(py),
                          ExtractTokens(path),
                          -tail[rank].score - dist_penalty - frag_penalty,
                          input.size()});
        }
        std::sort(l1.begin(), l1.end(),
                  [](const DecodeResult& a, const DecodeResult& b) {
                      return a.score > b.score;
                  });
        const std::size_t full_limit = 1 + extra;
        for (std::size_t i = 0; i < l1.size() && results.size() < full_limit;
             ++i) {
            dedup.insert(l1[i].text);
            results.push_back(std::move(l1[i]));
        }
    }

    const std::size_t layer1_size = results.size();

    // === Layer 2: word/char alternatives at position 0 ===
    for (const auto& edge : net[0].es) {
        if (edge.id == NotToken) continue;

        std::u32string text_u32 = ToText(edge);
        if (text_u32.empty()) continue;
        std::string text_utf8 = TextFromU32(text_u32);
        if (!dedup.insert(text_utf8).second) continue;

        std::size_t distance = (total > edge.end) ? (total - edge.end) : 0;
        float_t dist_penalty =
            static_cast<float_t>(distance) * penalty_per_unit;

        auto slice = std::string_view(lower).substr(
            edge.start, edge.end - edge.start);
        std::size_t mismatch = CountSyllableMismatch(edge.pieces, slice, false);
        float_t frag_penalty =
            static_cast<float_t>(mismatch) * PinyinMatchPenalty;

        Scorer::Pos epos{};
        Scorer::Pos enext{};
        float_t score = -scorer_.ScoreMove(epos, edge.id, enext)
                        - dist_penalty - frag_penalty;

        std::string edge_py = edge.pieces
            ? AbbreviatePieces(edge.pieces, slice)
            : "";
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

std::vector<DecodeResult> Sime::NextTokens(
    const std::vector<TokenID>& context,
    std::size_t num,
    bool en) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0) return results;

    Scorer::Pos pos{};
    for (auto tid : context) {
        Scorer::Pos next{};
        scorer_.ScoreMove(pos, tid, next);
        pos = next;
    }

    // When filtering to English only, widen the scorer pool — many top
    // predictions will be Chinese and get dropped.
    const std::size_t pool = en ? num * 16 : num * 4;
    auto next_tokens = scorer_.NextTokens(pos, pool);
    const auto& ts = dict_.TokenSet();

    // English tokens in Sime's corpora are ASCII-letter words (possibly
    // containing an apostrophe, e.g. don't, 's), or SentencePiece-style
    // pieces prefixed with U+2581 (word boundary). Contraction pieces like
    // "'s" / "'re" start with the apostrophe, so accept it too.
    auto is_english = [](const char32_t* chars) {
        char32_t c = chars[0];
        if (c == 0x2581) return true;
        if (c == U'\'') return true;
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
    };

    std::unordered_set<std::string> seen;
    for (const auto& [tid, pro] : next_tokens) {
        if (results.size() >= num) break;
        if (tid < StartToken) continue;
        if (ts.find(tid) == ts.end()) continue;

        const char32_t* chars = dict_.TokenAt(tid);
        if (!chars || chars[0] == 0) continue;
        if (en && !is_english(chars)) continue;

        std::u32string u32;
        for (std::size_t i = 0; chars[i] != 0; ++i)
            u32.push_back(chars[i]);
        std::string text = TextFromU32(u32);
        if (!seen.insert(text).second) continue;

        results.push_back({std::move(text), {}, {tid}, -pro, 0});
    }

    return results;
}

std::vector<DecodeResult> Sime::GetTokens(
    std::string_view prefix,
    std::size_t num,
    bool en) const {
    std::vector<DecodeResult> results;
    if (!ready_ || num == 0 || prefix.empty()) return results;

    struct Candidate {
        std::string text;
        TokenID id;
        float_t score;
    };
    std::vector<Candidate> candidates;

    auto collect = [&](Dict::DatType type,
                       const std::vector<trie::SearchResult>& matches) {
        for (const auto& r : matches) {
            auto entry = dict_.GetEntry(type, r.value);
            for (uint32_t i = 0; i < entry.count; ++i) {
                TokenID tid = entry.items[i].id;
                const char32_t* chars = dict_.TokenAt(tid);
                if (!chars || chars[0] == 0) continue;

                std::u32string u32;
                for (std::size_t j = 0; chars[j] != 0; ++j)
                    u32.push_back(chars[j]);
                std::string text = TextFromU32(u32);

                Scorer::Pos pos{};
                Scorer::Pos next{};
                float_t cost = scorer_.ScoreMove(pos, tid, next);

                candidates.push_back({std::move(text), tid, cost});
            }
        }
    };

    collect(Dict::LetterEn,
            dict_.Dat(Dict::LetterEn).FindWordsWithPrefix(prefix, num * 4));
    if (!en) {
        // Mixed mode: also search the pinyin DAT. Plain prefix match is
        // exactly what we want — user typed a literal pinyin prefix (possibly
        // with '\'' syllable separators) and wants every token whose stored
        // pinyin starts with it, including deeper multi-syllable completions.
        collect(Dict::LetterPinyin,
                dict_.Dat(Dict::LetterPinyin)
                    .FindWordsWithPrefix(prefix, num * 4));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.score < b.score; });

    std::unordered_set<std::string> seen;
    for (auto& c : candidates) {
        if (results.size() >= num) break;
        if (!seen.insert(c.text).second) continue;
        results.push_back({std::move(c.text), {}, {c.id}, -c.score,
                           prefix.size()});
    }

    return results;
}

} // namespace sime
