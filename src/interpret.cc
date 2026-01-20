#include "interpret.h"

#include "ustr.h"

#include <algorithm>

namespace {
constexpr sime::TokenID kScoreNotToken = 69;
} // namespace

namespace sime {

bool Interpreter::LoadResources(const std::filesystem::path& trie_path,
                                const std::filesystem::path& slm_path) {
    if (!trie_.Load(trie_path)) {
        ready_ = false;
        return false;
    }
    if (!scorer_.Load(slm_path)) {
        trie_.Clear();
        ready_ = false;
        return false;
    }
    ready_ = true;
    return true;
}

std::vector<DecodeResult> Interpreter::DecodeText(
    std::string_view input,
    const DecodeOptions& options) const {
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
        std::string token(input.substr(start, pos - start));
        std::vector<Unit> chunk;
        if (parser.ParseToken(token, chunk)) {
            units.insert(units.end(), chunk.begin(), chunk.end());
        }
    }
    return DecodeUnits(units, options);
}

std::vector<DecodeResult> Interpreter::DecodeUnits(
    const std::vector<Unit>& units,
    const DecodeOptions& options) const {
    std::vector<DecodeResult> results;
    if (!ready_ || units.empty()) {
        return results;
    }

    std::vector<Column> lattice;
    InitLattice(units, lattice);

    const std::size_t max_best = options.num == 0 ? 1 : options.num;
    for (auto& column : lattice) {
        column.states.SetMaxBest(max_best);
    }
    State init_state(0.0, 0, nullptr, Scorer::State{}, 0);
    lattice[0].states.Add(init_state);

    Process(lattice);

    

        const auto tail_states = lattice.back().states.GetFilteredResult();

        if (tail_states.empty()) {

            return results;

        }

    

        const std::size_t total =

            std::min<std::size_t>(max_best, tail_states.size());

        for (std::size_t rank = 0; rank < total; ++rank) {

            auto path = Backtrace(tail_states[rank], lattice.size() - 1);

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

void Interpreter::InitLattice(const std::vector<Unit>& units,
                              std::vector<Column>& lattice) const {
    lattice.clear();
    lattice.resize(units.size() + 2);

    for (std::size_t start = 0; start < units.size(); ++start) {
        auto& bucket = lattice[start].vecs;
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
            const Entry* entries = trie_.GetEntry(node, count);
            for (std::uint32_t idx = 0; idx < count; ++idx) {
                TokenID wid = static_cast<TokenID>(entries[idx].i);
                bucket.push_back({start, pos, wid});
            }
            if (count > 0) {
                inserted = true;
            }
        }
        if (!inserted) {
            bucket.push_back({start, start + 1, kScoreNotToken});
        }
    }

    lattice[units.size()].vecs.push_back(
        {units.size(), units.size() + 1, kSentenceToken});
}

void Interpreter::Process(std::vector<Column>& lattice) const {
#if SIME_USE_FAST_STATES
    // Optimized version with staged pruning
    for (std::size_t col = 0; col < lattice.size(); ++col) {
        auto& column = lattice[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.vecs) {
                Scorer::State next_state{};
                double step = scorer_.ScoreMove(value.scorer_state, word.id, next_state);
                scorer_.Back(next_state);
                double next_cost = value.score + step;
                State next(next_cost, word.right, &value, next_state, word.id);
                lattice[word.right].states.Add(next);
            }
        }
    }

    // Staged pruning: prune all columns after processing
    for (auto& column : lattice) {
        column.states.Prune();
    }
#else
    // Original version with immediate pruning on every Add()
    for (std::size_t col = 0; col < lattice.size(); ++col) {
        auto& column = lattice[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& value = *it;
            for (const auto& word : column.vecs) {
                Scorer::State next_state{};
                double step = scorer_.ScoreMove(value.scorer_state, word.id, next_state);
                scorer_.Back(next_state);
                double next_cost = value.score + step;
                State next(next_cost, word.right, &value, next_state, word.id);
                lattice[word.right].states.Add(next);
            }
        }
    }
#endif
}

std::vector<Interpreter::Lattice> Interpreter::Backtrace(
    const State& tail_state,
    std::size_t end_frame) {
    std::vector<Lattice> path;
    const State* state = &tail_state;
    while (state != nullptr && state->backtrace != nullptr) {
        const State* prev = state->backtrace;
        path.push_back({prev->frame_index, state->frame_index,
                        state->backtrace_token});
        state = prev;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty() && path.back().right == end_frame) {
        path.pop_back();
    }
    return path;
}

std::u32string Interpreter::ToText(const Lattice& n,
                                   const std::vector<Unit>& units) const {
    if (n.id == kScoreNotToken || n.id == kNotToken) {
        std::string fallback = SliceToUnits(units, n.left, n.right);
        return ustr::ToU32("[" + fallback + "]");
    }
    const char32_t* chars = trie_.StrAt(n.id);
    if (chars == nullptr || chars[0] == 0) {
        std::string fallback = SliceToUnits(units, n.left, n.right);
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
        std::string fallback = SliceToUnits(units, n.left, n.right);
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
