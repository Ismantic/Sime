#include "nine.h"

#include "state.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace sime {

char NineDecoder::LetterToDigit(char c) {
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

std::string NineDecoder::PinyinToDigits(const char* pinyin) {
    std::string result;
    for (const char* p = pinyin; *p; ++p) {
        char d = LetterToDigit(static_cast<char>(
            std::tolower(static_cast<unsigned char>(*p))));
        if (d != '0') {
            result.push_back(d);
        }
    }
    return result;
}

void NineDecoder::BuildDigitMap() {
    digit_map_.clear();
    token_to_unit_.clear();
    unit_to_token_.clear();

    std::size_t count = 0;
    const UnitEntry* entries = UnitData::GetDict(count);

    TokenID next_id = StartToken;
    for (std::size_t i = 0; i < count; ++i) {
        Unit unit(entries[i].value);
        // Skip bare initials (incomplete syllables)
        if (!unit.Full()) {
            continue;
        }

        TokenID tid = next_id++;
        std::string digits = PinyinToDigits(entries[i].text);
        if (digits.empty()) {
            continue;
        }

        digit_map_[digits].push_back({tid, unit});

        // Grow token_to_unit_ to accommodate this token
        std::size_t idx = tid - StartToken;
        if (idx >= token_to_unit_.size()) {
            token_to_unit_.resize(idx + 1);
        }
        token_to_unit_[idx] = unit;

        // Reverse map: first token wins (for prefix lookup)
        unit_to_token_.emplace(unit.value, tid);
    }
}

bool NineDecoder::Load(const std::filesystem::path& pinyin_model_path) {
    BuildDigitMap();
    if (!scorer_.Load(pinyin_model_path)) {
        ready_ = false;
        return false;
    }
    ready_ = true;
    return true;
}

std::vector<NineDecoder::Result> NineDecoder::Decode(
    std::string_view digits,
    std::size_t num) const {

    std::vector<Result> results;
    if (!ready_ || digits.empty()) {
        return results;
    }

    // Validate: only digits 2-9
    for (char c : digits) {
        if (c < '2' || c > '9') {
            return results;
        }
    }

    const std::size_t n = digits.size();

    // Build lattice: n+2 columns (0..n for digits, n→n+1 for SentenceToken)
    struct Link {
        std::size_t end = 0;
        TokenID token_id = 0;
    };

    struct Column {
        std::vector<Link> links;
        NetStates states;
    };

    std::vector<Column> net(n + 2);

    // Build edges from digit substrings
    for (std::size_t start = 0; start < n; ++start) {
        std::string key;
        for (std::size_t end = start + 1; end <= std::min(start + 6, n); ++end) {
            key.push_back(digits[end - 1]);
            auto it = digit_map_.find(key);
            if (it != digit_map_.end()) {
                for (const auto& entry : it->second) {
                    net[start].links.push_back({end, entry.token_id});
                }
            }
        }
        // Fallback: skip one digit if no valid syllable starts here
        if (net[start].links.empty()) {
            net[start].links.push_back({start + 1, ScoreNotToken});
        }
    }
    // SentenceToken at end
    net[n].links.push_back({n + 1, SentenceToken});

    // Set beam width
    const std::size_t beam = std::max<std::size_t>(num * 2, 16);
    for (auto& col : net) {
        col.states.SetMaxTop(beam);
    }

    // Initialize
    State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
    net[0].states.Insert(init);

    // Forward pass (same pattern as Interpreter::Process)
    for (std::size_t col = 0; col < net.size(); ++col) {
        auto& column = net[col];
        for (auto it = column.states.begin(); it != column.states.end(); ++it) {
            const auto& state = *it;
            for (const auto& link : column.links) {
                Scorer::Pos next_pos{};
                float_t step =
                    scorer_.ScoreMove(state.pos, link.token_id, next_pos);
                scorer_.Back(next_pos);
                float_t next_cost = state.score + step;
                State next(next_cost, link.end, next_pos, &state,
                           link.token_id);
                net[link.end].states.Insert(next);
            }
        }
    }

    // Backtrace from final column
    auto tail_states = net.back().states.GetStates();
    const std::size_t max_top = num == 0 ? 1 : num;
    const std::size_t scan =
        std::min<std::size_t>(beam, tail_states.size());

    for (std::size_t rank = 0;
         rank < scan && results.size() < max_top; ++rank) {
        // Trace back to collect token sequence
        std::vector<TokenID> tokens;
        const State* state = &tail_states[rank];
        while (state->backtrace_state != nullptr) {
            TokenID tid = state->backtrace_token;
            if (tid != SentenceToken && tid != ScoreNotToken &&
                tid != NotToken) {
                tokens.push_back(tid);
            }
            state = state->backtrace_state;
        }
        std::reverse(tokens.begin(), tokens.end());
        if (tokens.empty()) {
            continue;
        }

        // Convert token IDs to Units
        Result result;
        result.score = -tail_states[rank].score;
        for (TokenID tid : tokens) {
            std::size_t idx = tid - StartToken;
            if (idx < token_to_unit_.size()) {
                result.units.push_back(token_to_unit_[idx]);
            }
        }
        if (result.units.empty()) {
            continue;
        }

        // Deduplicate
        bool dup = false;
        for (const auto& existing : results) {
            if (existing.units == result.units) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            results.push_back(std::move(result));
        }
    }

    return results;
}

std::vector<NineDecoder::Result> NineDecoder::DecodeSentence(
    std::string_view digits,
    const std::vector<Unit>& prefix,
    std::size_t num) const {

    std::vector<Result> results;
    if (!ready_ || num == 0) {
        return results;
    }
    if (digits.empty() && prefix.empty()) {
        return results;
    }

    // Validate digits
    for (char c : digits) {
        if (c < '2' || c > '9') {
            return results;
        }
    }

    // Validate prefix: every unit must have a known TokenID
    std::vector<TokenID> prefix_tokens;
    for (const auto& u : prefix) {
        auto it = unit_to_token_.find(u.value);
        if (it == unit_to_token_.end()) return results;
        prefix_tokens.push_back(it->second);
    }

    const std::size_t p = prefix.size();
    const std::size_t d = digits.size();

    // --- First result: best full-match via beam search ---
    // Lattice: p prefix columns + d digit columns + 1 SentenceToken column
    // Total columns: p + d + 2
    if (p + d > 0) {
        struct Link {
            std::size_t end = 0;
            TokenID token_id = 0;
        };
        struct Column {
            std::vector<Link> links;
            NetStates states;
        };

        const std::size_t n = p + d;
        std::vector<Column> net(n + 2);

        // Prefix columns: fixed single edge each
        for (std::size_t i = 0; i < p; ++i) {
            net[i].links.push_back({i + 1, prefix_tokens[i]});
        }

        // Digit columns: normal digit_map_ expansion
        for (std::size_t start = 0; start < d; ++start) {
            std::string key;
            std::size_t col = p + start;
            for (std::size_t end = start + 1;
                 end <= std::min(start + 6, d); ++end) {
                key.push_back(digits[end - 1]);
                auto it = digit_map_.find(key);
                if (it != digit_map_.end()) {
                    for (const auto& entry : it->second) {
                        net[col].links.push_back(
                            {p + end, entry.token_id});
                    }
                }
            }
            if (net[col].links.empty()) {
                net[col].links.push_back({col + 1, ScoreNotToken});
            }
        }

        // SentenceToken at end
        net[n].links.push_back({n + 1, SentenceToken});

        // Beam search
        const std::size_t beam = std::max<std::size_t>(16, 16);
        for (auto& col : net) {
            col.states.SetMaxTop(beam);
        }
        State init(0.0, 0, Scorer::Pos{}, nullptr, 0);
        net[0].states.Insert(init);

        for (std::size_t col = 0; col < net.size(); ++col) {
            auto& column = net[col];
            for (auto it = column.states.begin();
                 it != column.states.end(); ++it) {
                const auto& state = *it;
                for (const auto& link : column.links) {
                    Scorer::Pos next_pos{};
                    float_t step = scorer_.ScoreMove(
                        state.pos, link.token_id, next_pos);
                    scorer_.Back(next_pos);
                    float_t next_cost = state.score + step;
                    State next(next_cost, link.end, next_pos,
                               &state, link.token_id);
                    net[link.end].states.Insert(next);
                }
            }
        }

        // Backtrace from final column — take ONE best
        auto tail_states = net.back().states.GetStates();
        for (std::size_t rank = 0;
             rank < tail_states.size() && results.empty(); ++rank) {
            std::vector<TokenID> tokens;
            const State* state = &tail_states[rank];
            while (state->backtrace_state != nullptr) {
                TokenID tid = state->backtrace_token;
                if (tid != SentenceToken && tid != ScoreNotToken &&
                    tid != NotToken) {
                    tokens.push_back(tid);
                }
                state = state->backtrace_state;
            }
            std::reverse(tokens.begin(), tokens.end());
            if (tokens.empty()) continue;

            Result result;
            result.score = -tail_states[rank].score;
            result.cnt = d;  // all digits consumed
            for (TokenID tid : tokens) {
                std::size_t idx = tid - StartToken;
                if (idx < token_to_unit_.size()) {
                    result.units.push_back(token_to_unit_[idx]);
                }
            }
            if (!result.units.empty()) {
                results.push_back(std::move(result));
            }
        }
    }

    // --- Remaining: single-syllable lookups from digit prefixes, long→short ---
    for (std::size_t len = d; len >= 1 && results.size() < num; --len) {
        std::string key(digits.substr(0, len));
        auto it = digit_map_.find(key);
        if (it == digit_map_.end()) continue;

        for (const auto& entry : it->second) {
            if (results.size() >= num) break;

            // Deduplicate against existing results (single-unit match)
            bool dup = false;
            for (const auto& existing : results) {
                if (existing.units.size() == 1 &&
                    existing.units[0] == entry.unit) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            Result result;
            result.units.push_back(entry.unit);
            result.cnt = len;
            results.push_back(std::move(result));
        }
    }

    return results;
}

} // namespace sime
