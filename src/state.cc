#include "state.h"

#include <algorithm>
#include <limits>

namespace sime {

State::State(SentenceScore s,
             std::size_t frame,
             const State* back,
             Scorer::State sc,
             TokenID t)
    : score(s),
      frame_index(frame),
      backtrace(back),
      scorer_state(sc),
      backtrace_token(t) {}

TopStates::TopStates(std::size_t threshold)
    : threshold_(threshold) {}

bool TopStates::Push(const State& state) {
    if (threshold_ == 0) {
        return false;
    }
    if (heap_.size() >= threshold_) {
        if (heap_.front() < state) {
            return false;
        }
        std::pop_heap(heap_.begin(), heap_.end());
        heap_.pop_back();
    }
    heap_.push_back(state);
    std::push_heap(heap_.begin(), heap_.end());
    return true;
}

void TopStates::Pop() {
    if (heap_.empty()) {
        return;
    }
    std::pop_heap(heap_.begin(), heap_.end());
    heap_.pop_back();
}

NetStates::NetStates() = default;

void NetStates::Clear() {
    state_map_.clear();
    heap_index_.clear();
    score_heap_.clear();
    size_ = 0;
}

std::vector<State> NetStates::GetSortedResult() const {
    std::vector<State> result;
    for (const auto& pair : state_map_) {
        result.insert(result.end(), pair.second.begin(), pair.second.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<State> NetStates::GetFilteredResult() const {
    std::vector<State> sorted = GetSortedResult();
    std::vector<State> filtered;
    if (sorted.empty()) {
        return filtered;
    }
    filtered.push_back(sorted[0]);
    SentenceScore max_score = sorted[0].score;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        SentenceScore current = sorted[i].score;
        if (current < FilterThreshold) {
            break;
        }
        if (max_score != 0.0) {
            double ratio = current / max_score;
            if (ratio < FilterRatioL2) {
                break;
            }
            if (ratio < FilterRatioL1 && current < max_score) {
                break;
            }
        }
        filtered.push_back(sorted[i]);
    }
    return filtered;
}

void NetStates::Add(const State& state) {
    auto it = state_map_.find(state.scorer_state);
    bool inserted = false;

    if (it == state_map_.end()) {
        TopStates bucket(max_best_);
        inserted = bucket.Push(state);
        state_map_.emplace(state.scorer_state, bucket);
        PushScoreHeap(state.score, state.scorer_state);
    } else {
        inserted = it->second.Push(state);
        auto heap_it = heap_index_.find(state.scorer_state);
        if (heap_it != heap_index_.end() && heap_it->second < score_heap_.size()) {
            AdjustDown(heap_it->second);
        }
    }

    if (inserted) {
        ++size_;
    }

    while (size_ > BeamWidth && !score_heap_.empty()) {
        const auto& best = score_heap_.front().second;
        auto bucket_it = state_map_.find(best);
        if (bucket_it == state_map_.end()) {
            PopScoreHeap();
            continue;
        }
        bucket_it->second.Pop();
        if (bucket_it->second.Size() == 0) {
            state_map_.erase(bucket_it);
            PopScoreHeap();
        } else {
            score_heap_.front().first = bucket_it->second.Top().score;
            AdjustDown(0);
        }
        --size_;
    }
}

void NetStates::PushScoreHeap(SentenceScore score,
                                  const Scorer::State& state) {
    score_heap_.emplace_back(score, state);
    AdjustUp(score_heap_.size() - 1);
}

void NetStates::PopScoreHeap() {
    if (score_heap_.empty()) {
        return;
    }
    heap_index_.erase(score_heap_.front().second);
    score_heap_.front() = score_heap_.back();
    score_heap_.pop_back();
    if (!score_heap_.empty()) {
        RefreshHeapIndex(0);
        AdjustDown(0);
    }
}

void NetStates::RefreshHeapIndex(std::size_t heap_index) {
    if (heap_index >= score_heap_.size()) {
        return;
    }
    heap_index_[score_heap_[heap_index].second] = heap_index;
}

void NetStates::AdjustUp(std::size_t node) {
    while (node > 0) {
        std::size_t parent = (node - 1) / 2;
        if (score_heap_[parent].first < score_heap_[node].first) {
            std::swap(score_heap_[parent], score_heap_[node]);
            RefreshHeapIndex(parent);
            node = parent;
        } else {
            break;
        }
    }
    RefreshHeapIndex(node);
}

void NetStates::AdjustDown(std::size_t node) {
    std::size_t left = node * 2 + 1;
    while (left < score_heap_.size()) {
        std::size_t child = node;
        if (score_heap_[child].first < score_heap_[left].first) {
            child = left;
        }
        std::size_t right = left + 1;
        if (right < score_heap_.size() &&
            score_heap_[child].first < score_heap_[right].first) {
            child = right;
        }
        if (child == node) {
            break;
        }
        std::swap(score_heap_[node], score_heap_[child]);
        RefreshHeapIndex(child);
        node = child;
        left = node * 2 + 1;
    }
    RefreshHeapIndex(node);
}

NetStates::iterator NetStates::begin() {
    return iterator(state_map_.begin(), state_map_.end());
}

NetStates::iterator NetStates::end() {
    return iterator(state_map_.end(), state_map_.end());
}

NetStates::iterator::iterator(StateMap::iterator outer,
                              StateMap::iterator outer_end)
    : outer_(outer), outer_end_(outer_end) {
    if (outer_ != outer_end_) {
        inner_ = outer_->second.begin();
        SkipEmpty();
    }
}

void NetStates::iterator::SkipEmpty() {
    while (outer_ != outer_end_ && outer_->second.begin() == outer_->second.end()) {
        ++outer_;
        if (outer_ != outer_end_) {
            inner_ = outer_->second.begin();
        }
    }
}

NetStates::iterator& NetStates::iterator::operator++() {
    if (outer_ == outer_end_) {
        return *this;
    }
    ++inner_;
    if (inner_ == outer_->second.end()) {
        ++outer_;
        if (outer_ != outer_end_) {
            inner_ = outer_->second.begin();
            SkipEmpty();
        }
    }
    return *this;
}

bool NetStates::iterator::operator!=(const iterator& rhs) const {
    if (outer_ == outer_end_ && rhs.outer_ == rhs.outer_end_) {
        return false;
    }
    return outer_ != rhs.outer_ || (outer_ != outer_end_ && inner_ != rhs.inner_);
}

State& NetStates::iterator::operator*() const {
    return *inner_;
}

State* NetStates::iterator::operator->() const {
    return inner_.operator->();
}

} // namespace sime
