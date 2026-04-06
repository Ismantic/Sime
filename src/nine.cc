#include "nine.h"

#include "state.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace sime {

char NineDecoder::LetterToNum(char c) {
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

std::string NineDecoder::UnitToNum(const char* unit) {
    std::string result;
    for (const char* p = unit; *p; ++p) {
        char d = LetterToNum(static_cast<char>(
            std::tolower(static_cast<unsigned char>(*p))));
        if (d != '0') {
            result.push_back(d);
        }
    }
    return result;
}

void NineDecoder::BuildNumMap() {
    num_map_.clear();
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
        std::string nums = UnitToNum(entries[i].text);
        if (nums.empty()) {
            continue;
        }

        num_map_[nums].push_back({tid, unit});

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
    BuildNumMap();
    if (!scorer_.Load(pinyin_model_path)) {
        ready_ = false;
        return false;
    }
    ready_ = true;
    return true;
}

std::vector<NineDecoder::Result> NineDecoder::Decode(
    std::string_view nums,
    std::size_t num) const {

    std::vector<Result> results;
    if (!ready_ || nums.empty()) {
        return results;
    }

    // Validate: only digits 2-9
    for (char c : nums) {
        if (c < '2' || c > '9') {
            return results;
        }
    }

    const std::size_t n = nums.size();

    // Build lattice: n+2 columns (0..n for digits, n→n+1 for SentenceEnd)
    struct Link {
        std::size_t end = 0;
        TokenID token_id = 0;
    };

    struct Column {
        std::vector<Link> links;
        NetStates states;
    };

    std::vector<Column> net(n + 2);

    // Build edges from num substrings
    for (std::size_t start = 0; start < n; ++start) {
        std::string key;
        for (std::size_t end = start + 1; end <= std::min(start + 6, n); ++end) {
            key.push_back(nums[end - 1]);
            auto it = num_map_.find(key);
            if (it != num_map_.end()) {
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
    // SentenceEnd at end
    net[n].links.push_back({n + 1, SentenceEnd});

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
            if (tid != SentenceEnd && tid != ScoreNotToken &&
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

} // namespace sime
