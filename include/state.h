#pragma once

#include "common.h"
#include "score.h"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace sime {

struct State {
    float_t score = 0.0;
    std::size_t now = 0;
    Scorer::Pos pos{};
    const State* backtrace_state = nullptr;
    TokenID backtrace_token = 0;
    const std::uint32_t* backtrace_group = nullptr;
    std::uint16_t backtrace_group_len = 1;
    const std::string* backtrace_pieces = nullptr;

    State() = default;

    State(float_t score,
          std::size_t now,
          Scorer::Pos pos,
          const State* backtrace_state,
          TokenID backtrace_token,
          const std::uint32_t* backtrace_group = nullptr,
          std::uint16_t backtrace_group_len = 1,
          const std::string* backtrace_pieces = nullptr);

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
    std::size_t size_;
};

class NetStates {
public:
    NetStates();

    void SetMaxTop(std::size_t max_top) { max_top_ = max_top; }
    void Clear();
    void Insert(const State& state);

    std::vector<State> GetStates() const;

private:
    using PosMap = std::map<Scorer::Pos, TopStates>;

public:
    class iterator {
    public:
        iterator() = default;
        iterator(PosMap::iterator outer, PosMap::iterator outer_end);

        iterator& operator++();
        bool operator!=(const iterator& rhs) const;
        State& operator*() const;
        State* operator->() const;

    private:
        PosMap::iterator outer_{};
        PosMap::iterator outer_end_{};
        TopStates::iterator inner_{};

        void SkipEmpty();
    };

    iterator begin();
    iterator end();

private:
    void PushScoreHeap(float_t score, const Scorer::Pos& pos);
    void PopScoreHeap();
    void RefreshTopIndex(std::size_t index);
    void AdjustUp(std::size_t node);
    void AdjustDown(std::size_t node);

private:
    static constexpr std::size_t BeamSize = 48;

    PosMap pos_map_;
    std::size_t state_size_ = 0;
    std::size_t max_top_ = 2;

    std::map<Scorer::Pos, std::size_t> top_index_;
    std::vector<std::pair<float_t, Scorer::Pos>> top_score_;
};

} // namespace sime
