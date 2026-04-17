#include "state.h"

#include <algorithm>
#include <limits>

namespace sime {

State::State(float_t score,
             std::size_t now,
             Scorer::Pos pos,
             const State* backtrace_state,
             TokenID backtrace_token,
             const std::string* backtrace_pieces)
    : score(score),
      now(now),
      pos(pos),
      backtrace_state(backtrace_state),
      backtrace_token(backtrace_token),
      backtrace_pieces(backtrace_pieces) {}

TopStates::TopStates(std::size_t size)
    : size_(size) {}

bool TopStates::Push(const State& state) {
    if (size_ == 0) {
        return false;
    }
    if (heap_.size() >= size_) {
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
    pos_map_.clear();
    top_index_.clear();
    top_score_.clear();
    state_size_ = 0;
}

std::vector<State> NetStates::GetStates() const {
    std::vector<State> result;
    for (const auto& pair : pos_map_) {
        result.insert(result.end(), pair.second.begin(), pair.second.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}

void NetStates::Insert(const State& state) {
    auto it = pos_map_.find(state.pos);
    bool inserted = false;

    if (it == pos_map_.end()) {
        TopStates bucket(max_top_);
        inserted = bucket.Push(state);
        pos_map_.emplace(state.pos, bucket);
        PushScoreHeap(state.score, state.pos);
    } else {
        inserted = it->second.Push(state);
        auto heap_it = top_index_.find(state.pos);
        if (heap_it != top_index_.end() && heap_it->second < top_score_.size()) {
            AdjustDown(heap_it->second);
        }
    }

    if (inserted) {
        ++state_size_;
    }

    while (state_size_ > BeamSize && !top_score_.empty()) {
        const auto& top = top_score_.front().second;
        auto bucket_it = pos_map_.find(top);
        if (bucket_it == pos_map_.end()) {
            PopScoreHeap();
            continue;
        }
        bucket_it->second.Pop();
        if (bucket_it->second.Size() == 0) {
            pos_map_.erase(bucket_it);
            PopScoreHeap();
        } else {
            top_score_.front().first = bucket_it->second.Top().score;
            AdjustDown(0);
        }
        --state_size_;
    }
}

void NetStates::PushScoreHeap(float_t score,
                              const Scorer::Pos& pos) {
    top_score_.emplace_back(score, pos);
    AdjustUp(top_score_.size() - 1);
}

void NetStates::PopScoreHeap() {
    if (top_score_.empty()) {
        return;
    }
    top_index_.erase(top_score_.front().second);
    top_score_.front() = top_score_.back();
    top_score_.pop_back();
    if (!top_score_.empty()) {
        RefreshTopIndex(0);
        AdjustDown(0);
    }
}

void NetStates::RefreshTopIndex(std::size_t index) {
    if (index >= top_score_.size()) {
        return;
    }
    top_index_[top_score_[index].second] = index;
}

void NetStates::AdjustUp(std::size_t node) {
    while (node > 0) {
        std::size_t parent = (node - 1) / 2;
        if (top_score_[parent].first < top_score_[node].first) {
            std::swap(top_score_[parent], top_score_[node]);
            RefreshTopIndex(parent);
            node = parent;
        } else {
            break;
        }
    }
    RefreshTopIndex(node);
}

void NetStates::AdjustDown(std::size_t node) {
    std::size_t left = node * 2 + 1;
    while (left < top_score_.size()) {
        std::size_t child = node;
        if (top_score_[child].first < top_score_[left].first) {
            child = left;
        }
        std::size_t right = left + 1;
        if (right < top_score_.size() &&
            top_score_[child].first < top_score_[right].first) {
            child = right;
        }
        if (child == node) {
            break;
        }
        std::swap(top_score_[node], top_score_[child]);
        RefreshTopIndex(child);
        node = child;
        left = node * 2 + 1;
    }
    RefreshTopIndex(node);
}

NetStates::iterator NetStates::begin() {
    return iterator(pos_map_.begin(), pos_map_.end());
}

NetStates::iterator NetStates::end() {
    return iterator(pos_map_.end(), pos_map_.end());
}

NetStates::iterator::iterator(PosMap::iterator outer,
                              PosMap::iterator outer_end)
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
