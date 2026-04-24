#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "dict.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sime {

struct DecodeResult {
    std::string text;        // UTF-8 display text (▁ prefix stripped)
    std::string units;       // segmented pinyin (e.g. "ni'hao")
    std::vector<TokenID> tokens;  // token IDs for LM context
    float_t score = 0.0;    // larger is better (negative log probability negated)
    std::size_t cnt = 0;     // bytes of input consumed
};

class Sime {
public:
    Sime() = default;
    Sime(const std::filesystem::path& dict_path,
         const std::filesystem::path& model_path);

    bool Ready() const { return ready_; }
    int ContextSize() const { return scorer_.Num() - 1; }
    // Decode
    std::vector<DecodeResult> DecodeStr(std::string_view input,
                                        std::size_t num = 5) const;
    std::vector<DecodeResult> DecodeSentence(std::string_view input,
                                             std::size_t extra = 0) const;
    // Prediction: given confirmed token IDs as context, suggest next words.
    // When `en` is true, only English tokens are returned (for the English
    // IME's prediction slot); Chinese tokens are filtered out.
    std::vector<DecodeResult> NextTokens(
        const std::vector<TokenID>& context,
        std::size_t num = 10,
        bool en = false) const;

    // Prefix completion: return tokens starting with `prefix`, sorted by
    // unigram score. Default searches both English and pinyin DATs (mixed
    // mode). When `en` is true, only the English DAT is searched.
    std::vector<DecodeResult> GetTokens(
        std::string_view prefix,
        std::size_t num = 10,
        bool en = false) const;

    // Num-key decode (T9/nine-key).
    // `start` is the confirmed prefix (letters, possibly with `'`
    // separators). Supports both pinyin and English prefixes.
    std::vector<DecodeResult> DecodeNumStr(
        std::string_view nums,
        std::string_view start = {},
        std::size_t num = 18) const;
    // Layer 1: full sentence N-best covering start + nums. Returns
    // 1 + `extra` sentences (top sentence is always included; `extra`
    // additional alternatives are appended).
    // Layer 2: word/char alternatives anchored at the first digit
    // column. Both layers are scored against the
    // LM context produced by the prefix `start`.
    std::vector<DecodeResult> DecodeNumSentence(
        std::string_view nums,
        std::string_view start = {},
        std::size_t extra = 0) const;
private:
    // Lattice types
    struct Link {
        std::size_t start = 0;
        std::size_t end = 0;
        TokenID id = 0;
        const char* pieces = nullptr;  // piece path, e.g. "ni'hao"
        float_t penalty = 0;           // syllable mismatch penalty
        bool expansion = false;        // tail-expansion edge (lower priority in PruneNode)
    };

    struct Node {
        std::vector<Link> es;
        NetStates states;
    };

    // Search parameters
    static constexpr std::size_t NodeSize = 40;
    static constexpr std::size_t BeamSize = 20;
    static constexpr float_t DistancePenalty = 1.8;
    // Per-syllable penalty for incomplete pinyin matches. Applied on edges
    // before beam search so that fully-typed syllables outrank abbreviated
    // ones during Process. Layer 2 also applies it in post-processing.
    static constexpr float_t PinyinMatchPenalty = 3.2;

    // Lattice building
    void InitNet(std::string_view input,
                    std::vector<Node>& net,
                    bool expansion = true) const;
    static void ComputeEdgePenalties(std::vector<Node>& net,
                                     std::string_view input,
                                     float_t penalty_per_mismatch,
                                     std::size_t t9_boundary);
    void PruneNode(std::vector<Link>& edges,
                   std::string_view input,
                   std::size_t t9_boundary,
                   std::unordered_map<TokenID, float_t>* score_cache = nullptr) const;

    // Beam search
    void Process(std::vector<Node>& net) const;
    static std::vector<Link> Backtrace(const State& tail_state,
                                       std::size_t end);

    std::u32string ToText(const Link& n) const;
    std::string ExtractText(const std::vector<Link>& path) const;
    static std::string ExtractUnits(const std::vector<Link>& path,
                                    std::string_view input);
    static std::string AbbreviatePieces(const char* full_pieces,
                                        std::string_view input);
    std::vector<TokenID> ExtractTokens(const std::vector<Link>& path) const;
    static std::string TextFromU32(std::u32string& u32);
    // Sum CountSyllableMismatch over every scoring link in `path`.
    // `t9_boundary` splits letter columns [0, boundary) from T9-digit
    // columns [boundary, ...); 0 means the whole path is letter input.
    static std::size_t CountPathMismatch(const std::vector<Link>& path,
                                         std::string_view input,
                                         std::size_t t9_boundary);

    // Num-key lattice
    void InitNumNet(std::string_view start,
                     std::string_view nums,
                     std::vector<Node>& net,
                     bool expansion = true) const;

    // Resources
    Dict dict_;
    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
