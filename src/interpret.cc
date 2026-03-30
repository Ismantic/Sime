#include "interpret.h"

#include "ustr.h"

#include <algorithm>
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

void Interpreter::InitSentenceNet(const std::vector<Unit>& units,
                                  std::vector<Node>& net) const {
    net.clear();
    net.resize(units.size() + 2);

    // Word edges: same as InitNet
    for (std::size_t start = 0; start < units.size(); ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        const Trie::Node* trie_node = trie_.Root();
        std::size_t pos = start;
        while (trie_node && pos < units.size()) {
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
        if (!inserted) {
            bucket.push_back({start, start + 1, ScoreNotToken});
        }
    }

    // SentenceToken at EVERY column (not just the last).
    // This enables collecting candidates at all prefix lengths.
    const std::size_t end_col = units.size() + 1;
    for (std::size_t col = 1; col <= units.size(); ++col) {
        net[col].es.push_back({col, end_col, SentenceToken});
    }
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

    // 2. Build sentence net and run forward pass
    std::vector<Node> net;
    InitSentenceNet(units, net);

    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t beam = std::max<std::size_t>(max_top * 4, units.size() * 3);
    for (auto& column : net) {
        column.states.SetMaxTop(beam);
    }
    State init_state(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init_state);

    Process(net);

    // 3. Collect from end node (has candidates from all prefix lengths)
    const std::size_t end_col = units.size() + 1;
    const auto tail_states = net[end_col].states.GetStates();
    if (tail_states.empty()) return results;

    const std::size_t scan = std::min<std::size_t>(beam, tail_states.size());
    for (std::size_t rank = 0; rank < scan && results.size() < max_top; ++rank) {
        auto path = Backtrace(tail_states[rank], end_col);
        if (path.empty()) continue;

        // Determine prefix length: last word link's end = unit column
        std::size_t prefix_units = path.back().end;
        if (prefix_units == 0 || prefix_units > units.size()) continue;
        std::size_t matched_bytes = unit_byte_end[prefix_units - 1];

        // Build text
        std::u32string composed;
        composed.reserve(path.size() * 4);
        for (const auto& word : path) {
            composed += ToText(word, units);
        }
        if (composed.empty()) continue;

        // Deduplicate
        bool duplicate = false;
        for (const auto& existing : results) {
            if (existing.text == composed && existing.matched_len == matched_bytes) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            SentenceResult r;
            r.text = std::move(composed);
            r.score = -tail_states[rank].score;
            r.matched_len = matched_bytes;
            results.push_back(std::move(r));
        }
    }
    return results;
}

} // namespace sime
