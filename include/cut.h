#pragma once

#include "common.h"
#include "dict.h"
#include "score.h"
#include "sentence.h"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <vector>

namespace sime {

struct CutOutputOptions {
    bool text_output = false;
    bool show_id = false;
    TokenID sentence_token = kSentenceToken;
};

class Cutter {
public:
    Cutter(Dict dict, Scorer scorer);

    void SegmentStream(std::istream& in,
                       std::ostream& out,
                       CutOutputOptions options) const;

private:
    struct LatticeWord {
        std::size_t left = 0;
        std::size_t right = 0;
        TokenID id = 0;
    };

    struct StateValue {
        double cost = 0.0;
        const LatticeWord* back_word = nullptr;
        Scorer::State back_state{};
    };

    struct Column {
        std::vector<LatticeWord> words;
        std::map<Scorer::State, StateValue> states;
    };

    void BuildLattice(const std::u32string& sentence,
                      CutOutputOptions options,
                      std::vector<Column>& lattice) const;
    void FullSegmentBlock(const std::u32string& sentence,
                          std::size_t start,
                          std::size_t length,
                          std::vector<Column>& lattice) const;
    void SearchBest(std::vector<Column>& lattice) const;
    std::vector<LatticeWord> ExtractBest(const std::vector<Column>& lattice) const;
    void EmitSentence(const std::u32string& sentence,
                      const std::vector<LatticeWord>& words,
                      std::ostream& out,
                      CutOutputOptions options,
                      std::size_t& word_count) const;

    static std::size_t CalcAmbiguity(const Dict& dict,
                                     std::u32string_view sentence,
                                     std::size_t start,
                                     std::size_t base_len);
    Dict dict_;
    Scorer scorer_;
};

} // namespace sime
