#pragma once

#include "common.h"
#include "score.h"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace sime {

using SentenceScore = double;

// Cache-aligned State structure to avoid false sharing
// Aligned to 64 bytes (typical cache line size) for optimal performance
struct alignas(64) State {
    SentenceScore score = 0.0;           // 8 bytes
    std::size_t frame_index = 0;         // 8 bytes
    const State* backtrace = nullptr;    // 8 bytes
    Scorer::State scorer_state{};        // 8 bytes (2 × uint32_t)
    TokenID backtrace_token = 0;         // 4 bytes
    // Total: 36 bytes, padded to 64 bytes

    State() = default;

    State(SentenceScore s,
          std::size_t frame,
          const State* back,
          Scorer::State sc,
          TokenID t);

    bool operator<(const State& r) const {
        return score < r.score;
    }
};

class TopStates {
public:
    explicit TopStates(std::size_t t);

    bool Push(const State& state);
    void Pop();

    const State& Top() const { return heap_.front(); }
    std::size_t Size() const { return heap_.size(); }

    using iterator = std::vector<State>::iterator;
    using const_iterator = std::vector<State>::const_iterator;

    iterator begin() { return heap_.begin(); }
    iterator end() { return heap_.end(); }
    const_iterator begin() const { return heap_.begin(); }
    const_iterator end() const { return heap_.end(); }

private:
    std::vector<State> heap_;
    std::size_t threshold_;
};

class NetStates {
public:
    NetStates();

    void SetMaxBest(std::size_t max_best) { max_best_ = max_best; }
    void Clear();
    void Add(const State& state);

    std::vector<State> GetSortedResult() const;
    std::vector<State> GetFilteredResult() const;

    using StateMap = std::map<Scorer::State, TopStates>;

    class iterator {
    public:
        iterator() = default;
        iterator(StateMap::iterator outer, StateMap::iterator outer_end);

        iterator& operator++();
        bool operator!=(const iterator& rhs) const;
        State& operator*() const;
        State* operator->() const;

    private:
        StateMap::iterator outer_{};
        StateMap::iterator outer_end_{};
        TopStates::iterator inner_{};

        void SkipEmpty();
    };

    iterator begin();
    iterator end();

private:
    void PushScoreHeap(SentenceScore score, const Scorer::State& state);
    void PopScoreHeap();
    void RefreshHeapIndex(std::size_t heap_index);
    void AdjustUp(std::size_t node);
    void AdjustDown(std::size_t node);

private:
    static constexpr std::size_t BeamWidth = 48;
    static constexpr double FilterRatioL1 = 0.12;
    static constexpr double FilterRatioL2 = 0.02;
    static constexpr double FilterThreshold = -40.0;

    StateMap state_map_;
    std::size_t size_ = 0;
    std::size_t max_best_ = 2;

    std::map<Scorer::State, std::size_t> heap_index_;
    std::vector<std::pair<SentenceScore, Scorer::State>> score_heap_;
};

} // namespace sime
