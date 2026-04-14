#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "trie.h"
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
    std::size_t cnt = 0;     // bytes of input consumed
};

class Sime {
public:
    Sime() = default;
    Sime(const std::filesystem::path& trie_path,
                const std::filesystem::path& model_path);

    bool Ready() const { return ready_; }
    // Decode
    std::vector<DecodeResult> DecodeStr(std::string_view input,
                                        std::size_t num = 5) const;
    std::vector<DecodeResult> DecodeSentence(std::string_view input,
                                             std::size_t extra = 0) const;

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
    // column. Always returned in full (subject to MaxPerPrefix for
    // multi-character entries). Both layers are scored against the
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
        const std::uint32_t* group = nullptr;
        std::uint16_t group_len = 1;
    };

    struct Node {
        std::vector<Link> es;
        NetStates states;
    };

    // Search parameters
    static constexpr std::size_t NodeSize = 40;
    static constexpr std::size_t BeamSize = 60;
    static constexpr std::size_t MaxSyllableCnt = 6;
    static constexpr std::size_t MaxPerPrefix = 15;
    static constexpr float_t DistancePenalty = 1.8;

    // Shared types
    using UnitMap = std::unordered_map<std::uint64_t, std::string>;
    static std::uint64_t EdgeKey(std::size_t start, std::size_t end, TokenID id);

    // Lattice building
    void InitNet(std::string_view input,
                    std::vector<Node>& net,
                    UnitMap* unit_map = nullptr) const;
    void PruneNode(std::vector<Link>& edges) const;

    // Beam search
    void Process(std::vector<Node>& net) const;
    static std::vector<Link> Backtrace(const State& tail_state,
                                       std::size_t end);

    // Text extraction
    std::u32string ToText(const Link& n,
                          const std::vector<Unit>& units) const;
    std::string SliceToUnits(const std::vector<Unit>& units,
                             std::size_t start, std::size_t end) const;

    // Num-key lattice
    void InitNumNet(std::string_view start,
                     std::string_view nums,
                     bool tail_expansion,
                     std::vector<Node>& net,
                     UnitMap* unit_map = nullptr) const;
    std::string ExtractText(const std::vector<Link>& path) const;
    static std::string ExtractUnits(const std::vector<Link>& path,
                                         const UnitMap& pm);

    // Resources
    const PieceTable& piece() const { return trie_.GetPieceTable(); }
    Trie trie_;
    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
