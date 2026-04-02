#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "nine.h"
#include "trie.h"
#include "dict.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sime {

struct DecodeResult {
    std::u32string text;
    float_t score = 0.0;   // larger is better (negative log probability negated)
    std::vector<TokenID> tokens;
};

struct SentenceResult {
    std::u32string text;
    float_t score = 0.0;        // larger is better
    std::size_t matched_len = 0; // bytes of input consumed
};

class Interpreter {
public:
    Interpreter() = default;

    bool LoadResources(const std::filesystem::path& trie_path,
                       const std::filesystem::path& model_path);

    bool LoadDict(const std::filesystem::path& path);

    bool LoadNine(const std::filesystem::path& pinyin_model_path);
    bool NineReady() const { return nine_.Ready(); }

    struct NineResult {
        std::string best_pinyin;                   // beam search best (for preedit/hanzi)
        std::vector<NineDecoder::Result> pinyin;   // exact-match syllable candidates
        std::vector<SentenceResult> hanzi;         // hanzi candidates
    };

    // Nine: decode digit sequence → pinyin + hanzi candidates.
    // prefix: locked confirmed syllables (empty for initial decode).
    NineResult DecodeNine(
        std::string_view digits,
        const std::vector<Unit>& prefix = {},
        std::size_t num = 18) const;

    bool Ready() const { return ready_; }

    // Original: decode full input, return n-best
    std::vector<DecodeResult> DecodeText(std::string_view input,
                                         std::size_t num = 5) const;

    std::vector<DecodeResult> DecodeUnits(const std::vector<Unit>& units,
                                          std::size_t num = 5) const;

    // Sentence: single-lattice decode returning candidates at all prefix lengths.
    // Candidates from different prefix lengths compete by LM score.
    std::vector<SentenceResult> DecodeSentence(
        std::string_view input, std::size_t num = 18) const;

    // Segment raw pinyin into syllables separated by spaces.
    // e.g. "zenmele" → "zen me le"
    static std::string SegmentPinyin(std::string_view input);

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

private:
    Trie trie_;
    Scorer scorer_;
    Dict dict_;
    NineDecoder nine_;
    bool ready_ = false;
};

} // namespace sime
