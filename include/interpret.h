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

struct DecodeOptions {
    std::size_t num = 5;
};

class Interpreter {
public:
    Interpreter() = default;

    bool LoadResources(const std::filesystem::path& trie_path,
                       const std::filesystem::path& slm_path);

    bool Ready() const { return ready_; }

    std::vector<DecodeResult> DecodeText(std::string_view input,
                                         const DecodeOptions& options) const;

    std::vector<DecodeResult> DecodeUnits(const std::vector<Unit>& units,
                                          const DecodeOptions& options) const;
private:
    struct Lattice {
        std::size_t left = 0;
        std::size_t right = 0;
        TokenID id = 0;
    };

    struct Column {
        std::vector<Lattice> vecs;
        NetStates states;
    };

    void InitLattice(const std::vector<Unit>& units,
                     std::vector<Column>& lattice) const;
    void Process(std::vector<Column>& lattice) const;
    static std::vector<Lattice> Backtrace(const State& tail_state,
                                          std::size_t end_frame);
    std::u32string ToText(const Lattice& n,
                          const std::vector<Unit>& units) const;
    static std::string SliceToUnits(
        const std::vector<Unit>& units,
        std::size_t left,
        std::size_t right);

private:
    Trie trie_;
    Scorer scorer_;
    bool ready_ = false;
};

} // namespace sime
