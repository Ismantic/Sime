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

// ============================================================================
// FastNetStates implementation - Optimized flat hash-based state storage
// ============================================================================

FastNetStates::FastNetStates() = default;

void FastNetStates::Clear() {
    for (auto& bucket : buckets_) {
        bucket.Reset();
    }
    size_ = 0;
}

std::size_t FastNetStates::Hash(const Scorer::State& s) const {
    // FNV-1a hash algorithm
    std::size_t hash = 14695981039346656037ULL;
    hash ^= s.level;
    hash *= 1099511628211ULL;
    hash ^= s.index;
    hash *= 1099511628211ULL;
    return hash & (TABLE_SIZE - 1);  // Fast modulo for power-of-2
}

FastNetStates::Bucket* FastNetStates::FindBucket(const Scorer::State& key) {
    std::size_t idx = Hash(key);

    // Linear probing (cache-friendly)
    for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
        std::size_t pos = (idx + probe) & (TABLE_SIZE - 1);
        auto& bucket = buckets_[pos];

        if (!bucket.occupied) {
            return &bucket;  // Empty slot found
        }

        if (bucket.key == key) {
            return &bucket;  // Matching bucket found
        }
    }

    return nullptr;  // Table full (should not happen with proper sizing)
}

const FastNetStates::Bucket* FastNetStates::FindBucket(const Scorer::State& key) const {
    std::size_t idx = Hash(key);

    for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
        std::size_t pos = (idx + probe) & (TABLE_SIZE - 1);
        const auto& bucket = buckets_[pos];

        if (!bucket.occupied) {
            return nullptr;  // Not found
        }

        if (bucket.key == key) {
            return &bucket;
        }
    }

    return nullptr;
}

bool FastNetStates::Bucket::TryAdd(const State& state) {
    if (count < MAX_BEST) {
        // Have space, just add
        states[count++] = state;
        UpdateMinScore();
        return true;
    }

    // Bucket full, check if new state is better than worst
    // Since smaller score = better, min_score is the best in bucket
    // We only keep states with score <= all existing states
    // So if new state has score >= any existing, we reject it

    // Find worst (largest score) state in bucket
    std::size_t worst_idx = 0;
    SentenceScore worst_score = states[0].score;

    for (std::size_t i = 1; i < count; ++i) {
        if (states[i].score > worst_score) {
            worst_score = states[i].score;
            worst_idx = i;
        }
    }

    // Only replace if new state is better than worst
    if (state.score >= worst_score) {
        return false;  // New state is worse than or equal to worst existing
    }

    states[worst_idx] = state;
    UpdateMinScore();
    return true;
}

void FastNetStates::Bucket::UpdateMinScore() {
    if (count == 0) {
        min_score = std::numeric_limits<SentenceScore>::lowest();
        return;
    }

    min_score = states[0].score;
    for (std::size_t i = 1; i < count; ++i) {
        if (states[i].score < min_score) {
            min_score = states[i].score;
        }
    }
}

void FastNetStates::Add(const State& state) {
    Bucket* bucket = FindBucket(state.scorer_state);

    if (!bucket) {
        // Hash table full - should not happen with proper sizing
        return;
    }

    if (!bucket->occupied) {
        // New bucket
        bucket->key = state.scorer_state;
        bucket->occupied = true;
        bucket->states[0] = state;
        bucket->count = 1;
        bucket->UpdateMinScore();
        ++size_;
    } else {
        // Existing bucket
        std::size_t old_count = bucket->count;
        if (bucket->TryAdd(state)) {
            if (bucket->count > old_count) {
                ++size_;  // Actually added a new state
            }
        }
    }
}

void FastNetStates::Prune() {
    if (size_ <= BeamWidth) {
        return;  // No pruning needed
    }

    // Collect all states into a vector
    std::vector<std::pair<SentenceScore, std::size_t>> scores;
    scores.reserve(size_);

    for (std::size_t i = 0; i < TABLE_SIZE; ++i) {
        if (buckets_[i].occupied) {
            for (std::size_t j = 0; j < buckets_[i].count; ++j) {
                scores.emplace_back(buckets_[i].states[j].score, (i << 16) | j);
            }
        }
    }

    // Partial sort to find beam width threshold
    // Since smaller score = better, we want to keep the smallest BeamWidth scores
    // Use default comparator (ascending order): smallest scores come first
    std::size_t nth = std::min(BeamWidth, scores.size());
    std::nth_element(scores.begin(),
                     scores.begin() + static_cast<std::ptrdiff_t>(nth),
                     scores.end());

    SentenceScore threshold = scores[nth - 1].first;

    // Keep states with score <= threshold (better or equal)
    std::size_t new_size = 0;

    for (auto& bucket : buckets_) {
        if (!bucket.occupied) {
            continue;
        }

        std::size_t write_idx = 0;
        for (std::size_t read_idx = 0; read_idx < bucket.count; ++read_idx) {
            if (bucket.states[read_idx].score <= threshold) {
                if (write_idx != read_idx) {
                    bucket.states[write_idx] = bucket.states[read_idx];
                }
                ++write_idx;
            }
        }

        bucket.count = static_cast<std::uint8_t>(write_idx);
        new_size += write_idx;

        if (bucket.count == 0) {
            bucket.Reset();
        } else {
            bucket.UpdateMinScore();
        }
    }

    size_ = new_size;
}

std::vector<State> FastNetStates::GetSortedResult() const {
    std::vector<State> result;
    result.reserve(size_);

    for (const auto& bucket : buckets_) {
        if (bucket.occupied) {
            for (std::size_t i = 0; i < bucket.count; ++i) {
                result.push_back(bucket.states[i]);
            }
        }
    }

    // Sort in ascending order by score (best first, since smaller score = better)
    // Uses State::operator<, same as NetStates
    std::sort(result.begin(), result.end());

    return result;
}

std::vector<State> FastNetStates::GetFilteredResult() const {
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

FastNetStates::iterator FastNetStates::begin() {
    return iterator(this, 0, 0);
}

FastNetStates::iterator FastNetStates::end() {
    return iterator(this, TABLE_SIZE, 0);
}

FastNetStates::iterator::iterator(FastNetStates* parent,
                                   std::size_t bucket_idx,
                                   std::size_t state_idx)
    : parent_(parent), bucket_idx_(bucket_idx), state_idx_(state_idx) {
    AdvanceToValid();
}

void FastNetStates::iterator::AdvanceToValid() {
    if (!parent_) {
        return;
    }

    while (bucket_idx_ < TABLE_SIZE) {
        auto& bucket = parent_->buckets_[bucket_idx_];

        if (bucket.occupied && state_idx_ < bucket.count) {
            return;  // Found valid state
        }

        // Move to next bucket
        ++bucket_idx_;
        state_idx_ = 0;
    }
}

FastNetStates::iterator& FastNetStates::iterator::operator++() {
    if (bucket_idx_ >= TABLE_SIZE) {
        return *this;
    }

    ++state_idx_;
    AdvanceToValid();

    return *this;
}

bool FastNetStates::iterator::operator!=(const iterator& rhs) const {
    return bucket_idx_ != rhs.bucket_idx_ || state_idx_ != rhs.state_idx_;
}

State& FastNetStates::iterator::operator*() const {
    return parent_->buckets_[bucket_idx_].states[state_idx_];
}

State* FastNetStates::iterator::operator->() const {
    return &parent_->buckets_[bucket_idx_].states[state_idx_];
}

} // namespace sime
