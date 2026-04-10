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
    std::size_t cnt = 0;     // bytes of input consumed
};

class Interpreter {
public:
    Interpreter() = default;
    Interpreter(const std::filesystem::path& trie_path,
                const std::filesystem::path& model_path);

    bool Ready() const { return ready_; }
    bool LoadDict(const std::filesystem::path& path);

    // Pinyin decode
    std::vector<DecodeResult> Decode(const std::vector<Unit>& units,
                                     std::size_t num = 5) const;
    std::vector<DecodeResult> DecodeStr(std::string_view input,
                                        std::size_t num = 5) const;
    std::vector<DecodeResult> DecodeSentence(std::string_view input,
                                             std::size_t num = 0) const;

    // Num-key decode (T9/nine-key).
    // `start` is the confirmed pinyin prefix (letters, possibly with `'`
    // separators). It is parsed internally via ParseWithBoundaries, so a
    // trailing incomplete initial (e.g. "q") is supported via tail expansion.
    std::vector<DecodeResult> DecodeNumStr(
        std::string_view nums,
        std::string_view start = {},
        std::size_t num = 18) const;
    std::vector<DecodeResult> DecodeNumSentence(
        std::string_view nums,
        std::string_view start = {},
        std::size_t num = 0) const;

private:
    // Lattice types
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
    static constexpr std::size_t NodeSize = 40;
    static constexpr std::size_t BeamSize = 20;
    static constexpr std::size_t MaxSyllableCnt = 6;
    static constexpr std::size_t MaxPerPrefix = 15;
    static constexpr float_t DistancePenalty = 1.8;

    // Lattice building
    void InitNet(const std::vector<Unit>& units,
                 std::vector<Node>& net,
                 const std::vector<Unit>& tail_expansions = {}) const;
    void PruneNode(std::vector<Link>& edges) const;

    // Beam search
    void Process(std::vector<Node>& net) const;
    static std::vector<Link> Backtrace(const State& tail_state,
                                       std::size_t end);

    // Text extraction
    std::u32string ToText(const Link& n,
                          const std::vector<Unit>& units) const;
    static std::string SliceToUnits(const std::vector<Unit>& units,
                                    std::size_t start, std::size_t end);

    // Pinyin parsing
    static bool ParseWithBoundaries(
        std::string_view input,
        std::vector<Unit>& units,
        std::vector<std::size_t>& unit_byte_end,
        std::vector<Unit>& tail_expansions);

    // Num-key mapping
    void BuildNumMap();
    static char LetterToNum(char c);
    static std::string UnitToNum(const char* unit);

    // Num-key lattice
    using NumUnitMap = std::unordered_map<std::uint64_t, std::string>;
    static std::uint64_t NumEdgeKey(std::size_t start, std::size_t end, TokenID id);
    // Builds a unified lattice that walks the confirmed pinyin prefix
    // (`start` = fixed units, `start_tail` = alternatives for an incomplete
    // trailing initial) followed by the digit columns. Edges may freely cross
    // the prefix/digit boundary so the beam search keeps a real LM context
    // through the prefix. `start_tail` is empty when the prefix is fully
    // parsed; when non-empty it contributes one extra column after `start`
    // whose edges fan out over all listed Units.
    void InitNumNet(const std::vector<Unit>& start,
                     const std::vector<Unit>& start_tail,
                     std::string_view nums,
                     bool tail_expansion,
                     std::vector<Node>& net,
                     NumUnitMap* unit_map = nullptr) const;
    std::string ExtractNumText(const std::vector<Link>& path) const;
    static std::string ExtractNumUnits(const std::vector<Link>& path,
                                         const NumUnitMap& pm);

    // Resources
    Trie trie_;
    Scorer scorer_;
    Dict dict_;
    std::unordered_map<std::string, std::vector<Unit>> num_map_;
    bool ready_ = false;
};

} // namespace sime
