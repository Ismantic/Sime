#pragma once

#include "common.h"
#include "dict.h"
#include "score.h"
#include "trie.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sime {

// Sentinel id emitted for out-of-vocab single-character fragments. Lies
// beyond any real dict id, so Scorer::ScoreMove backs off to the uniform
// root probability (= UnknownPenalty).
constexpr TokenID CutUnkToken = static_cast<TokenID>(-1);

struct CutToken {
    TokenID id = 0;      // dict id; 0 when is_unk
    std::string text;    // UTF-8 slice from the input (always the original chars)
    bool is_unk = false; // true if the slice had no matching dict token
};

// Chinese-text segmenter that reuses the existing Sime LM and dict.
//
// Tokens in `dict` are indexed into a DAT (keyed by their UTF-8 text) at
// construction time. At segmentation time, each input position is looked
// up with one DAT prefix-search call, and a Viterbi beam search over the
// LM picks the best token sequence. Single UTF-8 characters with no
// covering dict token fall through as UNK edges.
class Cutter {
public:
    Cutter(const Dict& dict, const Scorer& scorer);

    // Segment `input` (UTF-8). Returns tokens in order. Empty input → empty.
    std::vector<CutToken> Cut(std::string_view input) const;

private:
    const Dict& dict_;
    const Scorer& scorer_;

    // Byte-level DAT: key = token's UTF-8 text, value = token id.
    trie::DoubleArray dat_;
};

} // namespace sime
