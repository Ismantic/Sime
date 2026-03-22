#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "trie.h"

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

class Interpreter {
public:
    Interpreter() = default;

    bool LoadResources(const std::filesystem::path& trie_path,
                       const std::filesystem::path& model_path);

    bool Ready() const { return ready_; }

    std::vector<DecodeResult> DecodeText(std::string_view input,
                                         std::size_t num = 5) const;

    std::vector<DecodeResult> DecodeUnits(const std::vector<Unit>& units,
                                          std::size_t num = 5) const;
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

    void InitNet(const std::vector<Unit>& units,
                 std::vector<Node>& net) const;
    void Process(std::vector<Node>& net) const;
    static std::vector<Link> Backtrace(const State& tail_state,
                                       std::size_t end);
    std::u32string ToText(const Link& n,
                          const std::vector<Unit>& units) const;
    static std::string SliceToUnits(
        const std::vector<Unit>& units,
        std::size_t start,
        std::size_t end);

private:
    Trie trie_;
    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
