#pragma once

#include "common.h"
#include "score.h"
#include "state.h"
#include "trie.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Enable optimized state management with flat hash table and staged pruning
// Set to 1 to use FastNetStates (faster), 0 to use NetStates (original)
// FastNetStates is now fully debugged and enabled by default
#ifndef SIME_USE_FAST_STATES
#define SIME_USE_FAST_STATES 1
#endif

// Staged pruning: prune every N frames instead of every state addition
// Only used when SIME_USE_FAST_STATES is enabled
#ifndef SIME_PRUNE_INTERVAL
#define SIME_PRUNE_INTERVAL 4
#endif

// Enable batch processing optimization for better cache locality
// Collects state transitions per column before processing
#ifndef SIME_USE_BATCH_PROCESSING
#define SIME_USE_BATCH_PROCESSING 1
#endif

namespace sime {

struct DecodeResult {
    std::u32string text;
    double score = 0.0;   // larger is better (negative log probability negated)
    std::vector<TokenID> tokens;
    bool partial_match = false;     // Whether this result is from partial pinyin matching
    std::size_t matched_length = 0; // Number of input characters that were matched
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
#if SIME_USE_FAST_STATES
        FastNetStates states;
#else
        NetStates states;
#endif
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
