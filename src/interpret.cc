#include "interpret.h"

#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sime {

bool Interpreter::LoadResources(const std::filesystem::path& trie_path,
                                const std::filesystem::path& model_path) {
    if (!trie_.Load(trie_path)) {
        ready_ = false;
        return false;
    }
    if (!scorer_.Load(model_path)) {
        trie_.Clear();
        ready_ = false;
        return false;
    }
    ready_ = true;
    return true;
}

bool Interpreter::LoadNine(const std::filesystem::path& pinyin_model_path) {
    return nine_.Load(pinyin_model_path);
}

Interpreter::NineResult Interpreter::DecodeNine(
    std::string_view digits,
    const std::vector<Unit>& prefix,
    std::size_t num) const {
    NineResult result;
    if (!nine_.Ready()) {
        return result;
    }
    if (digits.empty() && prefix.empty()) {
        return result;
    }

    // Decode digits → pinyin (beam search best + exact-match candidates)
    auto nine = nine_.DecodeSentence(digits, prefix, num);

    // Build best_pinyin string from beam search best parse
    if (!nine.best.units.empty()) {
        for (const auto& u : nine.best.units) {
            const char* syl = UnitData::Decode(u);
            if (syl) {
                if (!result.best_pinyin.empty()) result.best_pinyin += '\'';
                result.best_pinyin += syl;
            }
        }
    }

    // Hanzi: best_pinyin → DecodeSentence
    if (ready_ && !result.best_pinyin.empty()) {
        result.hanzi = DecodeSentence(result.best_pinyin, num);
    }

    // Pinyin candidates: exact matches only
    result.pinyin = std::move(nine.candidates);

    return result;
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
    return DecodeUnits(units, num);
}

std::vector<DecodeResult> Interpreter::DecodeUnits(
    const std::vector<Unit>& units,
    std::size_t num) const {
    std::vector<DecodeResult> results;
    if (!ready_ || units.empty()) {
        return results;
    }

    std::vector<Node> net;
    InitNet(units, net);

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t beam = max_top * 2;
    for (auto& column : net) {
        column.states.SetMaxTop(beam);
    }
    State init_state(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init_state);

    Process(net);

    const auto tail_states = net.back().states.GetStates();
    if (tail_states.empty()) {
        return results;
    }

    const std::size_t total =
        std::min<std::size_t>(beam, tail_states.size());
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

    net[effective_n].es.push_back(
        {effective_n, effective_n + 1, SentenceEnd});
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
    // Wider beam than DecodeText: Layer 2 needs good states at intermediate cols
    const std::size_t beam = std::max<std::size_t>(max_top * 3, 40);
    for (auto& col : net) col.states.SetMaxTop(beam);
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);
    Process(net);

    // === Layer 1: Full sentence N-best (covers all input) ===
    {
        const auto tail = net.back().states.GetStates();
        // libime uses nbest=2 for full sentences by default
        constexpr std::size_t kNBest = 2;
        const std::size_t scan = std::min<std::size_t>(beam, tail.size());
        for (std::size_t rank = 0; rank < scan && results.size() < kNBest; ++rank) {
            auto path = Backtrace(tail[rank], net.size() - 1);
            if (path.empty()) continue;
            std::u32string composed;
            composed.reserve(path.size() * 4);
            for (const auto& w : path) composed += ToText(w, units);
            if (composed.empty()) continue;
            bool dup = false;
            for (const auto& e : results)
                if (e.text == composed) { dup = true; break; }
            if (!dup) {
                SentenceResult r;
                r.text = std::move(composed);
                r.score = -tail[rank].score;
                r.matched_len = total_bytes;  // full match
                results.push_back(std::move(r));
            }
        }
    }

    std::size_t layer1_size = results.size();

    // === Layer 2: Word/phrase candidates from BOS with distance penalty ===
    // Similar to libime: distancePenalty = unknownPenalty / 1.8
    constexpr float_t kDistancePenaltyFactor = 3.0;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / kDistancePenaltyFactor;

    {

        // Collect candidates at each column (including full match for overflow
        // beyond kNBest). Distance penalty = 0 for full matches.
        for (std::size_t col = 1; col <= effective_n; ++col) {
            const auto& col_states = net[col].states.GetStates();
            if (col_states.empty()) continue;

            std::size_t distance = effective_n - col;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;
            std::size_t matched_bytes =
                (col <= unit_byte_end.size()) ? unit_byte_end[col - 1]
                                              : total_bytes;

            // Full-match column: unlimited. Partial: cap per prefix.
            constexpr std::size_t kPerPrefix = 15;
            const std::size_t col_scan =
                (col == effective_n)
                    ? col_states.size()
                    : std::min<std::size_t>(kPerPrefix, col_states.size());
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
                composed.reserve(path.size() * 4);
                for (const auto& w : path) composed += ToText(w, units);
                if (composed.empty()) continue;

                bool dup = false;
                for (const auto& e : results)
                    if (e.text == composed && e.matched_len == matched_bytes) {
                        dup = true; break;
                    }
                if (!dup) {
                    SentenceResult r;
                    r.text = std::move(composed);
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

std::string Interpreter::SegmentPinyin(std::string_view input) {
    std::vector<Unit> units;
    UnitParser parser;
    std::string result;
    std::size_t pos = 0;
    while (pos < input.size()) {
        // Preserve delimiters (apostrophes) as spaces
        while (pos < input.size() && UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        if (pos >= input.size()) break;
        std::size_t start = pos;
        while (pos < input.size() && !UnitParser::IsDelimiter(input[pos])) {
            ++pos;
        }
        std::string chunk(input.substr(start, pos - start));
        std::vector<Unit> chunk_units;
        if (parser.ParseStr(chunk, chunk_units)) {
            for (const auto& u : chunk_units) {
                const char* syl = UnitData::Decode(u);
                if (!syl) continue;
                if (!result.empty()) result.push_back(' ');
                result.append(syl);
            }
        } else {
            // Try partial parse: show complete syllables + incomplete tail
            auto pr = parser.ParseTokenEnhanced(chunk, true);
            if (!pr.complete && pr.matched_len > 0) {
                for (const auto& u : pr.units) {
                    const char* syl = UnitData::Decode(u);
                    if (!syl) continue;
                    if (!result.empty()) result.push_back(' ');
                    result.append(syl);
                }
                // Append the incomplete remainder as-is
                std::string remainder(chunk.substr(pr.matched_len));
                if (!remainder.empty()) {
                    if (!result.empty()) result.push_back(' ');
                    result.append(remainder);
                }
            } else {
                // Fully unparseable: append as-is
                if (!result.empty()) result.push_back(' ');
                result.append(chunk);
            }
        }
    }
    return result;
}

} // namespace sime
