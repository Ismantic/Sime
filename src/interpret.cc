#include "interpret.h"

#include "ustr.h"

#include <algorithm>

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

    std::vector<Column> net;
    InitNet(units, net);

    const std::size_t max_top = num == 0 ? 1 : num;
    for (auto& column : net) {
        column.states.SetMaxTop(max_top);
    }
    State init_state(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init_state);

    Process(net);

    const auto tail_states = net.back().states.GetStates();
    if (tail_states.empty()) {
        return results;
    }

    const std::size_t total =
        std::min<std::size_t>(max_top, tail_states.size());
    for (std::size_t rank = 0; rank < total; ++rank) {
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
        results.push_back(std::move(result));
    }
    return results;
}

void Interpreter::InitNet(const std::vector<Unit>& units,
                          std::vector<Column>& net) const {
    net.clear();
    net.resize(units.size() + 2);

    for (std::size_t start = 0; start < units.size(); ++start) {
        auto& bucket = net[start].es;
        bool inserted = false;
        const Node* node = trie_.Root();
        std::size_t pos = start;
        while (node && pos < units.size()) {
            node = trie_.DoMove(node, units[pos]);
            ++pos;
            if (!node) {
                break;
            }
            std::uint32_t count = 0;
            const std::uint32_t* tokens = trie_.GetToken(node, count);
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

void Interpreter::Process(std::vector<Column>& net) const {
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
    std::size_t end_frame) {
    std::vector<Link> path;
    const State* state = &tail_state;
    while (state != nullptr && state->backtrace_state != nullptr) {
        const State* prev = state->backtrace_state;
        path.push_back({prev->now, state->now,
                        state->backtrace_token});
        state = prev;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().end == end_frame) {
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
    constexpr std::size_t kMaxWordLength = 64;
    std::u32string buffer;
    buffer.reserve(8);
    for (std::size_t i = 0; i < kMaxWordLength; ++i) {
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
    std::size_t left,
    std::size_t right) {
    std::string result;
    for (std::size_t idx = left; idx < right && idx < units.size(); ++idx) {
        const char* syl = UnitData::Decode(units[idx]);
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

} // namespace sime
