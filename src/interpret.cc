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
                          std::vector<Node>& net) const {
    net.clear();
    net.resize(units.size() + 2);

    for (std::size_t start = 0; start < units.size(); ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        const Trie::Node* trie_node = trie_.Root();
        std::size_t pos = start;
        while (trie_node && pos < units.size()) {
            trie_node = trie_.DoMove(trie_node, units[pos]);
            ++pos;
            if (!trie_node) {
                break;
            }
            std::uint32_t count = 0;
            const std::uint32_t* tokens = trie_.GetToken(trie_node, count);
            for (std::uint32_t idx = 0; idx < count; ++idx) {
                TokenID wid = static_cast<TokenID>(tokens[idx]);
                bucket.push_back({start, pos, wid});
            }
            if (count > 0) {
                inserted = true;
            }
        }
        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    net[units.size()].es.push_back(
        {units.size(), units.size() + 1, SentenceToken});
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
    std::vector<std::size_t>& unit_byte_end) {
    units.clear();
    unit_byte_end.clear();
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
        if (!parser.ParseStr(chunk_str, chunk)) continue;

        // Reconstruct per-unit byte offsets within this chunk
        std::size_t byte_offset = chunk_start;
        for (const auto& u : chunk) {
            const char* syl = UnitData::Decode(u);
            if (syl) byte_offset += std::strlen(syl);
            unit_byte_end.push_back(byte_offset);
        }
        units.insert(units.end(), chunk.begin(), chunk.end());
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
    if (!ParseWithBoundaries(input, units, unit_byte_end)) {
        return results;
    }

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t total_bytes = input.size();

    // Build lattice once, shared by Layer 1 and Layer 2
    std::vector<Node> net;
    InitNet(units, net);
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

    const std::size_t layer1_size = results.size();

    // === Layer 2: Word/phrase candidates from BOS with distance penalty ===
    // Similar to libime: distancePenalty = unknownPenalty / 1.8
    constexpr float_t kDistancePenaltyFactor = 3.0;
    const float_t penalty_per_unit =
        std::abs(scorer_.UnknownPenalty()) / kDistancePenaltyFactor;

    {

        // Collect candidates at each column (including full match for overflow
        // beyond kNBest). Distance penalty = 0 for full matches.
        for (std::size_t col = 1; col <= units.size(); ++col) {
            const auto& col_states = net[col].states.GetStates();
            if (col_states.empty()) continue;

            std::size_t distance = units.size() - col;
            float_t dist_penalty =
                static_cast<float_t>(distance) * penalty_per_unit;
            std::size_t matched_bytes =
                (col <= unit_byte_end.size()) ? unit_byte_end[col - 1]
                                              : total_bytes;

            // Top few per prefix: enough variety without flooding
            constexpr std::size_t kPerPrefix = 3;
            const std::size_t col_scan =
                std::min<std::size_t>(kPerPrefix, col_states.size());
            for (std::size_t rank = 0; rank < col_scan; ++rank) {
                const auto& st = col_states[rank];

                // Score the SentenceToken transition from this state
                Scorer::Pos sent_pos{};
                float_t sent_step =
                    scorer_.ScoreMove(st.pos, SentenceToken, sent_pos);
                float_t raw_score = -(st.score + sent_step);
                float_t adjusted = raw_score - dist_penalty;

                // Backtrace to get the text.
                // Pass SIZE_MAX as end so Backtrace won't strip the last link
                // (that stripping is for removing the SentenceToken link,
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
            // Unparseable chunk: append as-is
            if (!result.empty()) result.push_back(' ');
            result.append(chunk);
        }
    }
    return result;
}

} // namespace sime
