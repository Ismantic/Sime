#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "trie.h"
#include "dict.h"
#include "unit.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sime {

struct DecodeResult {
    std::string text;        // UTF-8 hanzi
    std::string units;       // segmented pinyin (e.g. "ni'hao")
    float_t score = 0.0;    // larger is better (negative log probability negated)
    std::size_t cnt = 0;     // bytes of input consumed (0 = full match)
};

class Interpreter {
public:
    Interpreter() = default;
    Interpreter(const std::filesystem::path& dict_path,
                const std::filesystem::path& model_path);

    bool Ready() const { return ready_; }
    bool LoadDict(const std::filesystem::path& path);

    // Stream: joint decode with progressive input (digits, possibly incomplete).
    // prefix: confirmed pinyin syllables providing LM context.
    std::vector<DecodeResult> DecodeNumSentence(
        std::string_view digits,
        const std::vector<Unit>& prefix = {},
        std::size_t num = 18) const;

    // Nine: joint decode (digits → expand all pinyin → hanzi model).
    // No pinyin model needed, only dict + cnt.
    // prefix: confirmed pinyin syllables providing LM context.
    // digits: remaining digit sequence to decode.
    std::vector<DecodeResult> DecodeNumStr(
        std::string_view digits,
        const std::vector<Unit>& prefix = {},
        std::size_t num = 18) const;

    // Original: decode full input, return n-best
    std::vector<DecodeResult> DecodeStr(std::string_view input,
                                         std::size_t num = 5) const;

    std::vector<DecodeResult> Decode(const std::vector<Unit>& units,
                                          std::size_t num = 5) const;

    // Sentence: single-lattice decode returning candidates at all prefix lengths.
    // Candidates from different prefix lengths compete by LM score.
    std::vector<DecodeResult> DecodeSentence(
        std::string_view input, std::size_t num = 18) const;


private:
    struct Link {
        std::size_t start = 0;
        std::size_t end = 0;
        TokenID id = 0;
    };

    struct Node {
        std::vector<Link> es;
        NetStates states;
    };

    // Search parameters
    static constexpr std::size_t NodeSize = 40;   // max candidates per span per lattice position
    static constexpr std::size_t BeamSize = 20;   // max states per position in beam search
    static constexpr std::size_t MaxSyllableLen = 6;  // longest pinyin syllable (zhuang)
    static constexpr std::size_t MaxPerPrefix = 15;   // max candidates per partial prefix
    static constexpr float_t DistancePenalty = 3.0;   // penalty divisor for partial matches

    // Prune edges at a position to top-NodeSize by unigram score.
    void PruneNode(std::vector<Link>& edges) const;

    // Original net: SentenceToken only at end
    // tail_expansions: if non-empty, fan out at the end for incomplete syllable
    void InitNet(const std::vector<Unit>& units,
                 std::vector<Node>& net,
                 const std::vector<Unit>& tail_expansions = {}) const;

    void Process(std::vector<Node>& net) const;
    static std::vector<Link> Backtrace(const State& tail_state,
                                       std::size_t end);
    std::u32string ToText(const Link& n,
                          const std::vector<Unit>& units) const;
    static std::string SliceToUnits(
        const std::vector<Unit>& units,
        std::size_t start,
        std::size_t end);

    // Parse input into units, tracking byte boundary for each unit.
    // unit_byte_end[i] = byte position in input after unit i.
    // tail_expansions: filled with possible Units if trailing incomplete syllable.
    static bool ParseWithBoundaries(
        std::string_view input,
        std::vector<Unit>& units,
        std::vector<std::size_t>& unit_byte_end,
        std::vector<Unit>& tail_expansions);

    // Build digit → pinyin Unit mapping from built-in syllable table
    void BuildDigitMap();

    static char LetterToDigit(char c);
    static std::string UnitToDigits(const char* pinyin);

private:
    Trie trie_;
    Scorer scorer_;
    Dict dict_;
    std::unordered_map<std::string, std::vector<Unit>> digit_map_;
    bool ready_ = false;
};

} // namespace sime
