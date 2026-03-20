#include "compact.h"
#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace sime {

namespace {

struct ArrayItem {
    float value = 0.0F;
    unsigned heap_index = 0;
};

struct HeapItem {
    unsigned first = 0;
    unsigned last = 0;
    unsigned count = 0;
    double approx = 0.0;
    double sum = 0.0;
    double distance = 0.0;

    bool operator<(const HeapItem& other) const { return approx < other.approx; }
};

using ArrayBuffer = std::vector<ArrayItem>;
using HeapBuffer = std::vector<HeapItem>;

template <typename Cmp>
void BubbleUp(HeapBuffer& heap, ArrayBuffer& arr, int index, Cmp cmp) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (!cmp(heap[index], heap[parent])) {
            break;
        }
        for (unsigned h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(parent);
        }
        for (unsigned h = heap[parent].first; h < heap[parent].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(index);
        }
        std::swap(heap[parent], heap[index]);
        index = parent;
    }
}

template <typename Cmp>
void SiftDown(HeapBuffer& heap, ArrayBuffer& arr, int index, int bottom, Cmp cmp) {
    int left = 0;
    while ((left = 2 * index + 1) < bottom) {
        int best = index;
        if (cmp(heap[left], heap[best])) {
            best = left;
        }
        if (left + 1 < bottom && cmp(heap[left + 1], heap[best])) {
            best = left + 1;
        }
        if (best == index) {
            break;
        }
        for (unsigned h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(best);
        }
        for (unsigned h = heap[best].first; h < heap[best].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(index);
        }
        std::swap(heap[best], heap[index]);
        index = best;
    }
}

auto CmpByValue = [](const HeapItem& a, const HeapItem& b) {
    return !(a < b);  // max-heap by approx
};

auto CmpByDistance = [](const HeapItem& a, const HeapItem& b) {
    return a.distance < b.distance;  // min-heap by distance
};

void CompressValues(std::map<float, int>& values,
                    std::map<float, int>& mapping,
                    std::vector<float>& table,
                    unsigned limit) {
        ArrayBuffer arr;
        HeapBuffer heap;
        arr.reserve(values.size());
        heap.reserve(values.size());

        for (const auto& [val, freq] : values) {
            unsigned idx = static_cast<unsigned>(arr.size());
            arr.push_back(ArrayItem{val, idx});
            double sum = static_cast<double>(val);
            if (freq > 0) {
                sum *= static_cast<double>(freq);
            }
            heap.push_back(
                HeapItem{idx, idx + 1, static_cast<unsigned>(freq), val, sum, 0.0});
        }

        const int heap_size = static_cast<int>(heap.size());
        for (int i = 0; i < heap_size - 1; ++i) {
            if (heap[i].count == 0 || heap[i + 1].count == 0) {
                heap[i].distance = std::numeric_limits<double>::max();
            } else {
                heap[i].distance = heap[i + 1].approx - heap[i].approx;
            }
            BubbleUp(heap, arr, i, CmpByDistance);
        }
        if (!heap.empty()) {
            heap.back().distance = std::numeric_limits<double>::max();
            BubbleUp(heap, arr, static_cast<int>(heap.size()) - 1, CmpByDistance);
        }

        while (static_cast<int>(heap.size()) > static_cast<int>(limit)) {
            const auto current_first = heap.front().first;
            const auto current_last = heap.front().last;
            int prev_index = (current_first == 0)
                                 ? -1
                                 : static_cast<int>(arr[current_first - 1].heap_index);
            int next_index = static_cast<int>(arr[current_last].heap_index);

            for (unsigned h = current_first; h < current_last; ++h) {
                arr[h].heap_index = static_cast<unsigned>(next_index);
            }

            const double merged_val = (heap.front().sum + heap[next_index].sum) /
                                      (heap.front().count + heap[next_index].count);
            if (prev_index >= 0) {
                heap[prev_index].distance += (merged_val - heap.front().approx);
            }
            heap[next_index].first = heap.front().first;
            heap[next_index].count += heap.front().count;
            heap[next_index].sum += heap.front().sum;
            heap[next_index].distance += (heap[next_index].approx - merged_val);
            heap[next_index].approx = merged_val;

            if (prev_index > next_index) {
                std::swap(prev_index, next_index);
            }

            SiftDown(heap, arr, next_index, static_cast<int>(heap.size()), CmpByDistance);
            if (prev_index > 0) {
                SiftDown(heap, arr, prev_index, static_cast<int>(heap.size()), CmpByDistance);
            }

            heap.front() = heap.back();
            for (unsigned h = heap.front().first; h < heap.front().last; ++h) {
                arr[h].heap_index = 0U;
            }
            heap.pop_back();
            SiftDown(heap, arr, 0, static_cast<int>(heap.size()), CmpByDistance);
        }

        for (int i = 1; i < static_cast<int>(heap.size()); ++i) {
            BubbleUp(heap, arr, i, CmpByValue);
        }
        for (int i = static_cast<int>(heap.size()) - 1; i > 0; --i) {
            for (unsigned h = heap[0].first; h < heap[0].last; ++h) {
                arr[h].heap_index = static_cast<unsigned>(i);
            }
            for (unsigned h = heap[i].first; h < heap[i].last; ++h) {
                arr[h].heap_index = 0U;
            }
            std::swap(heap[0], heap[i]);
            SiftDown(heap, arr, 0, i, CmpByValue);
        }

        mapping.clear();
        for (const auto& item : arr) {
            mapping[item.value] = static_cast<int>(item.heap_index);
        }

        table.clear();
        table.reserve(heap.size());
        for (const auto& bucket : heap) {
            table.push_back(static_cast<float>(bucket.approx));
        }
}

void CompressWithReverse(std::map<float, float>& effective_to_real,
                         std::map<float, int>& values,
                         std::map<float, int>& mapping,
                         std::vector<float>& table,
                         unsigned limit) {
    std::map<float, int> temp;
    CompressValues(values, temp, table, limit);

    mapping.clear();
    for (const auto& [eff_value, index] : temp) {
        mapping[effective_to_real[eff_value]] = index;
    }
}

constexpr std::uint32_t kBitsBow = 14;
constexpr std::uint32_t kBitsPr = 16;
constexpr std::uint32_t kWordBits = 18;
constexpr std::uint32_t kBonBits = 23;
constexpr std::uint32_t kBolBits = 2;
constexpr std::uint32_t kChildBits = 23;

struct RawNode {
    TokenID id = 0;
    float pr = 0.0f;
    std::uint32_t child = 0;
    float bow = 0.0f;
};

struct RawLeaf {
    TokenID id = 0;
    float pr = 0.0f;
};

class SimpleSlm {
public:
    bool Load(const std::filesystem::path& path);
    void BuildLinks();

    int Order() const { return order_; }
    bool UseLog() const { return use_log_pr_; }
    const std::vector<int>& LevelSizes() const { return level_sizes_; }
    const std::vector<RawNode>& Level(int idx) const { return levels_.at(idx); }
    const std::vector<RawLeaf>& Leaves() const { return leaves_; }

    std::size_t ActualSize(int level) const;
    std::size_t LeafCount() const;

    void FillHistory(int level, int index, std::vector<TokenID>& out) const;
    int FindState(int length, const TokenID* seq) const;
    bool HasChildren(int level, int index) const;
    void FindBackoffState(int length, const TokenID* seq, unsigned& bol, unsigned& bon) const;

private:
    std::pair<std::size_t, std::size_t> ChildRange(int level, int index) const;

    int order_ = 0;
    bool use_log_pr_ = false;
    std::vector<int> level_sizes_;
    std::vector<std::vector<RawNode>> levels_;
    std::vector<RawLeaf> leaves_;
    std::vector<std::vector<int>> parent_links_;
    std::vector<int> leaf_parents_;
};

struct PackedNode {
    std::uint32_t word0 = 0;
    std::uint32_t word1 = 0;
    std::uint32_t word2 = 0;
};

struct PackedLeaf {
    std::uint32_t word0 = 0;
    std::uint32_t word1 = 0;
};

float EffectivePr(bool use_log, float value) {
    return use_log ? (value / std::log(2.0F)) : -std::log2(value);
}

float OriginalPr(bool use_log, float effective) {
    return use_log ? (effective * std::log(2.0F)) : std::exp2(-effective);
}

float EffectiveBow(bool use_log, float value) {
    return use_log ? std::exp(-value) : value;
}

float OriginalBow(bool use_log, float effective) {
    return use_log ? -std::log(effective) : effective;
}

template <typename EffFn, typename OrigFn>
int LookupIndex(const std::map<float, int>& mapping,
                float real_value,
                bool use_log,
                EffFn eff_fn,
                OrigFn orig_fn) {
    auto it = mapping.find(real_value);
    if (it != mapping.end()) {
        return it->second;
    }

    float effective = eff_fn(use_log, real_value);
    float canonical = orig_fn(use_log, effective);
    it = mapping.find(canonical);
    if (it != mapping.end()) {
        return it->second;
    }
    throw std::runtime_error("quantized value not found");
}

struct Tables {
    std::map<float, int> pr_map;
    std::map<float, int> bow_map;
    std::vector<float> pr_table;
    std::vector<float> bow_table;
};

struct ThreadedModel {
    std::vector<std::vector<PackedNode>> nodes;
    std::vector<PackedLeaf> leaves;
};

bool SimpleSlm::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    if (!in.read(reinterpret_cast<char*>(&order_), sizeof(order_))) {
        return false;
    }
    std::uint32_t flag = 0;
    if (!in.read(reinterpret_cast<char*>(&flag), sizeof(flag))) {
        return false;
    }
    use_log_pr_ = (flag != 0);

    level_sizes_.assign(order_ + 1, 0);
    if (!in.read(reinterpret_cast<char*>(level_sizes_.data()),
                 static_cast<std::streamsize>(level_sizes_.size() * sizeof(int)))) {
        return false;
    }

    levels_.resize(order_);
    for (int lvl = 0; lvl < order_; ++lvl) {
        auto& nodes = levels_[lvl];
        nodes.resize(static_cast<std::size_t>(level_sizes_[lvl]));
        if (!in.read(reinterpret_cast<char*>(nodes.data()),
                     static_cast<std::streamsize>(nodes.size() * sizeof(RawNode)))) {
            return false;
        }
    }

    leaves_.resize(static_cast<std::size_t>(level_sizes_.back()));
    if (!in.read(reinterpret_cast<char*>(leaves_.data()),
                 static_cast<std::streamsize>(leaves_.size() * sizeof(RawLeaf)))) {
        return false;
    }
    return true;
}

void SimpleSlm::BuildLinks() {
    parent_links_.clear();
    parent_links_.resize(static_cast<std::size_t>(order_));
    for (int level = 1; level < order_; ++level) {
        auto actual = ActualSize(level);
        parent_links_[static_cast<std::size_t>(level)].resize(actual);
        const auto& parents = levels_[static_cast<std::size_t>(level - 1)];
        std::size_t parent = 0;
        for (std::size_t idx = 0; idx < actual; ++idx) {
            while (parent + 1 < parents.size() &&
                   parents[parent + 1].child <= idx) {
                ++parent;
            }
            parent_links_[static_cast<std::size_t>(level)][idx] = static_cast<int>(parent);
        }
    }

    leaf_parents_.clear();
    if (order_ == 0) {
        return;
    }
    auto total_leaves = LeafCount();
    leaf_parents_.resize(total_leaves);
    const auto& parents = levels_[static_cast<std::size_t>(order_ - 1)];
    std::size_t parent = 0;
    for (std::size_t idx = 0; idx < total_leaves; ++idx) {
        while (parent + 1 < parents.size() &&
               parents[parent + 1].child <= idx) {
            ++parent;
        }
        leaf_parents_[idx] = static_cast<int>(parent);
    }
}

std::size_t SimpleSlm::ActualSize(int level) const {
    if (level_sizes_.empty()) {
        return 0;
    }
    const auto size = level_sizes_.at(level);
    return (size == 0) ? 0 : static_cast<std::size_t>(size - 1);
}

std::size_t SimpleSlm::LeafCount() const {
    if (level_sizes_.empty()) {
        return 0;
    }
    const auto size = level_sizes_.back();
    return (size == 0) ? 0 : static_cast<std::size_t>(size - 1);
}

std::pair<std::size_t, std::size_t> SimpleSlm::ChildRange(int level, int index) const {
    const auto& nodes = levels_.at(static_cast<std::size_t>(level));
    std::size_t begin = nodes.at(static_cast<std::size_t>(index)).child;
    std::size_t end = nodes.at(static_cast<std::size_t>(index + 1)).child;
    return {begin, end};
}

void SimpleSlm::FillHistory(int level, int index, std::vector<TokenID>& out) const {
    out.assign(static_cast<std::size_t>(level), TokenID{0});
    if (level == 0) {
        return;
    }
    if (level == order_) {
        int current = index;
        out.back() = leaves_.at(static_cast<std::size_t>(current)).id;
        int parent_idx = leaf_parents_.at(static_cast<std::size_t>(current));
        int current_level = order_ - 1;
        for (int pos = level - 1; pos > 0; --pos) {
            const auto& nodes = levels_.at(static_cast<std::size_t>(current_level));
            out[static_cast<std::size_t>(pos - 1)] =
                nodes.at(static_cast<std::size_t>(parent_idx)).id;
            if (pos - 1 == 0) {
                break;
            }
            parent_idx = parent_links_.at(static_cast<std::size_t>(current_level))
                             .at(static_cast<std::size_t>(parent_idx));
            --current_level;
        }
        return;
    }

    int current_idx = index;
    int current_level = level;
    for (int pos = level; pos > 0; --pos) {
        const auto& nodes = levels_.at(static_cast<std::size_t>(current_level));
        out[static_cast<std::size_t>(pos - 1)] =
            nodes.at(static_cast<std::size_t>(current_idx)).id;
        if (pos - 1 == 0) {
            break;
        }
        current_idx = parent_links_.at(static_cast<std::size_t>(current_level))
                          .at(static_cast<std::size_t>(current_idx));
        --current_level;
    }
}

int SimpleSlm::FindState(int length, const TokenID* seq) const {
    if (length == 0) {
        return 0;
    }
    const TokenID* hw = seq;
    int n = length;
    while (n > order_) {
        --n;
        ++hw;
    }

    int node_index = 0;
    for (int lvl = 0; lvl < n; ++lvl) {
        auto [begin, end] = ChildRange(lvl, node_index);
        if (begin >= end) {
            return -1;
        }
        if (lvl + 1 == order_) {
            const auto it = std::lower_bound(
                leaves_.begin() + static_cast<std::ptrdiff_t>(begin),
                leaves_.begin() + static_cast<std::ptrdiff_t>(end),
                hw[lvl],
                [](const RawLeaf& leaf, TokenID id) { return leaf.id < id; });
            if (it == leaves_.begin() + static_cast<std::ptrdiff_t>(end) ||
                it->id != hw[lvl]) {
                return -1;
            }
            node_index = static_cast<int>(std::distance(leaves_.begin(), it));
        } else {
            const auto& next_level = levels_.at(static_cast<std::size_t>(lvl + 1));
            const auto it = std::lower_bound(
                next_level.begin() + static_cast<std::ptrdiff_t>(begin),
                next_level.begin() + static_cast<std::ptrdiff_t>(end),
                hw[lvl],
                [](const RawNode& node, TokenID id) { return node.id < id; });
            if (it == next_level.begin() + static_cast<std::ptrdiff_t>(end) ||
                it->id != hw[lvl]) {
                return -1;
            }
            node_index = static_cast<int>(std::distance(next_level.begin(), it));
        }
    }
    return node_index;
}

bool SimpleSlm::HasChildren(int level, int index) const {
    if (level >= order_) {
        return false;
    }
    const auto& nodes = levels_.at(static_cast<std::size_t>(level));
    const auto begin = nodes.at(static_cast<std::size_t>(index)).child;
    const auto end = nodes.at(static_cast<std::size_t>(index + 1)).child;
    return end > begin;
}

void SimpleSlm::FindBackoffState(int length,
                                 const TokenID* seq,
                                 unsigned& bol,
                                 unsigned& bon) const {
    bol = 0;
    bon = 0;
    if (length <= 1 || seq == nullptr) {
        return;
    }
    const TokenID* hw = seq;
    int n = length;
    while (n > 1) {
        --n;
        ++hw;
        int idx = FindState(n, hw);
        if (idx >= 0 && HasChildren(n, idx)) {
            bol = static_cast<unsigned>(n);
            bon = static_cast<unsigned>(idx);
            return;
        }
    }
}

using EffFn = float(*)(bool, float);

void CollectValue(float value, bool use_log, EffFn eff_fn, EffFn orig_fn,
                  std::map<float, float>& eff_map, std::map<float, int>& counts) {
    float eff = eff_fn(use_log, value);
    if (eff_map.find(eff) == eff_map.end()) {
        eff_map[eff] = value;
    } else {
        eff_map[eff] = orig_fn(use_log, eff);
    }
    ++counts[eff];
}

template <std::size_t N>
void AddMilestones(const float (&milestones)[N], bool use_log, EffFn eff_fn, EffFn orig_fn,
                   std::map<float, float>& eff_map, std::map<float, int>& counts) {
    for (float milestone : milestones) {
        float real = use_log ? -std::log(milestone) : milestone;
        float eff = eff_fn(use_log, real);
        if (eff_map.find(eff) == eff_map.end()) {
            eff_map[eff] = real;
        } else {
            eff_map[eff] = orig_fn(use_log, eff);
        }
        counts[eff] = 0;
    }
}

Tables BuildTables(const SimpleSlm& model) {
    Tables tables;
    bool use_log = model.UseLog();

    std::map<float, float> pr_eff;
    std::map<float, int> pr_counts;
    std::map<float, float> bow_eff;
    std::map<float, int> bow_counts;

    for (int lvl = 0; lvl < model.Order(); ++lvl) {
        const auto& nodes = model.Level(lvl);
        auto actual = model.ActualSize(lvl);
        for (std::size_t idx = 0; idx < actual; ++idx) {
            CollectValue(nodes[idx].pr, use_log,
                         EffectivePr, OriginalPr, pr_eff, pr_counts);
            CollectValue(nodes[idx].bow, use_log,
                         EffectiveBow, OriginalBow, bow_eff, bow_counts);
        }
    }
    const auto& leaves = model.Leaves();
    for (std::size_t idx = 0; idx < model.LeafCount(); ++idx) {
        CollectValue(leaves[idx].pr, use_log,
                     EffectivePr, OriginalPr, pr_eff, pr_counts);
    }

    static constexpr float kMilestonesPr[] = {
        0.9F, 0.8F, 0.7F, 0.6F, 1.0F / 2.0F, 1.0F / 4.0F, 1.0F / 8.0F,
        1.0F / 16.0F, 1.0F / 32.0F, 1.0F / 64.0F, 1.0F / 128.0F,
        1.0F / 256.0F, 1.0F / 512.0F, 1.0F / 1024.0F, 1.0F / 2048.0F,
        1.0F / 4096.0F, 1.0F / 8192.0F, 1.0F / 16384.0F, 1.0F / 32768.0F,
        1.0F / 65536.0F};
    AddMilestones(kMilestonesPr, use_log, EffectivePr, OriginalPr, pr_eff, pr_counts);

    static constexpr float kMilestonesBow[] = {
        1.0F,  0.9F,  0.8F,   0.7F,    0.6F,     0.5F,     0.4F,     0.3F,
        0.2F,  0.1F,  0.05F,  0.01F,   0.005F,   0.001F,   0.0005F,  0.0001F,
        0.00005F, 0.00001F, 0.000005F, 0.000001F, 0.0000005F, 0.0000001F};
    AddMilestones(kMilestonesBow, use_log, EffectiveBow, OriginalBow, bow_eff, bow_counts);

    CompressWithReverse(pr_eff, pr_counts, tables.pr_map, tables.pr_table, 1U << kBitsPr);
    for (auto& value : tables.pr_table) {
        value = OriginalPr(use_log, value);
    }

    CompressWithReverse(bow_eff, bow_counts, tables.bow_map, tables.bow_table, 1U << kBitsBow);
    for (auto& value : tables.bow_table) {
        value = OriginalBow(use_log, value);
    }
    return tables;
}

PackedNode DoPackNode(TokenID wid,
                      std::uint32_t bow_index,
                      std::uint32_t pr_index,
                      std::uint32_t child_index,
                      std::uint32_t bon,
                      std::uint32_t bol) {
    if (wid >= (1U << kWordBits) || bow_index >= (1U << kBitsBow) ||
        pr_index >= (1U << kBitsPr) || child_index >= (1U << kChildBits) ||
        bon >= (1U << kBonBits) || bol >= (1U << kBolBits)) {
        throw std::runtime_error("slmthread: value exceeds bit-field limit");
    }

    PackedNode node;
    node.word0 = (wid & ((1U << kWordBits) - 1U)) |
                 ((bow_index & ((1U << kBitsBow) - 1U)) << kWordBits);
    node.word1 = (pr_index & 0xFFFFU) | ((child_index & 0xFFFFU) << 16U);
    node.word2 = (bon & ((1U << kBonBits) - 1U)) |
                 ((bol & ((1U << kBolBits) - 1U)) << kBonBits) |
                 (((child_index >> 16U) & 0x7FU) << (kBonBits + kBolBits));
    return node;
}

PackedLeaf DoPackLeaf(TokenID wid,
                      std::uint32_t pr_index,
                      std::uint32_t bon,
                      std::uint32_t bol) {
    if (wid >= (1U << kWordBits) || pr_index >= (1U << kBitsPr) ||
        bon >= (1U << kBonBits) || bol >= (1U << kBolBits)) {
        throw std::runtime_error("slmthread: leaf value exceeds limit");
    }
    PackedLeaf leaf;
    leaf.word0 = (wid & ((1U << kWordBits) - 1U)) |
                 ((pr_index & 0x3FFFU) << kWordBits);
    leaf.word1 = (bon & ((1U << kBonBits) - 1U)) |
                 ((bol & ((1U << kBolBits) - 1U)) << kBonBits) |
                 (((pr_index >> 14U) & 0x3U) << (kBonBits + kBolBits));
    return leaf;
}

ThreadedModel BuildThreaded(const SimpleSlm& model, const Tables& tables) {
    ThreadedModel threaded;
    threaded.nodes.resize(static_cast<std::size_t>(model.Order()));
    std::vector<TokenID> history;
    history.reserve(static_cast<std::size_t>(model.Order()));

    for (int lvl = 0; lvl < model.Order(); ++lvl) {
        const auto level_size = static_cast<std::size_t>(model.LevelSizes()[lvl]);
        const auto actual = model.ActualSize(lvl);
        threaded.nodes[static_cast<std::size_t>(lvl)].resize(level_size);
        const auto& nodes = model.Level(lvl);
        for (std::size_t idx = 0; idx < actual; ++idx) {
            const auto& node = nodes[idx];
            const int pr_idx = LookupIndex(tables.pr_map,
                                           node.pr,
                                           model.UseLog(),
                                           EffectivePr,
                                           OriginalPr);
            const int bow_idx = LookupIndex(tables.bow_map,
                                            node.bow,
                                            model.UseLog(),
                                            EffectiveBow,
                                            OriginalBow);

            unsigned bol = 0;
            unsigned bon = 0;
            if (lvl == 0) {
                model.FindBackoffState(0, nullptr, bol, bon);
            } else {
                model.FillHistory(lvl, static_cast<int>(idx), history);
                model.FindBackoffState(lvl,
                                       history.empty() ? nullptr : history.data(),
                                       bol,
                                       bon);
            }

            threaded.nodes[static_cast<std::size_t>(lvl)][idx] =
                DoPackNode(node.id,
                           static_cast<std::uint32_t>(bow_idx),
                           static_cast<std::uint32_t>(pr_idx),
                           node.child,
                           bon,
                           bol);
        }

        const auto& sentinel = nodes.back();
        threaded
            .nodes[static_cast<std::size_t>(lvl)][level_size - 1U] =
            DoPackNode(0, 0, 0, sentinel.child, 0, 0);
    }

    const auto leaf_size = static_cast<std::size_t>(model.LevelSizes().back());
    const auto leaf_actual = model.LeafCount();
    threaded.leaves.resize(leaf_size);
    const auto& leaves = model.Leaves();
    for (std::size_t idx = 0; idx < leaf_actual; ++idx) {
        const auto& leaf = leaves[idx];
        const int pr_idx = LookupIndex(tables.pr_map,
                                       leaf.pr,
                                       model.UseLog(),
                                       EffectivePr,
                                       OriginalPr);
        unsigned bol = 0;
        unsigned bon = 0;
        model.FillHistory(model.Order(), static_cast<int>(idx), history);
        model.FindBackoffState(model.Order(), history.data(), bol, bon);
        threaded.leaves[idx] = DoPackLeaf(leaf.id,
                                          static_cast<std::uint32_t>(pr_idx),
                                          bon,
                                          bol);
    }
    threaded.leaves[leaf_size - 1U] = PackedLeaf{};
    return threaded;
}

void WriteModel(const SimpleSlm& model,
                const Tables& tables,
                const ThreadedModel& threaded,
                const std::filesystem::path& output) {
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open output slm file");
    }

    int order = model.Order();
    out.write(reinterpret_cast<const char*>(&order), sizeof(order));
    std::uint32_t flag = model.UseLog() ? 1U : 0U;
    out.write(reinterpret_cast<const char*>(&flag), sizeof(flag));
    out.write(reinterpret_cast<const char*>(model.LevelSizes().data()),
              static_cast<std::streamsize>(model.LevelSizes().size() * sizeof(int)));

    const std::size_t pr_limit = 1U << kBitsPr;
    if (tables.pr_table.size() > pr_limit) {
        throw std::runtime_error("pr table exceeds allocated size");
    }
    out.write(reinterpret_cast<const char*>(tables.pr_table.data()),
              static_cast<std::streamsize>(tables.pr_table.size() * sizeof(float)));
    const float zero = 0.0F;
    for (std::size_t idx = tables.pr_table.size(); idx < pr_limit; ++idx) {
        out.write(reinterpret_cast<const char*>(&zero), sizeof(float));
    }

    const std::size_t bow_limit = 1U << kBitsBow;
    if (tables.bow_table.size() > bow_limit) {
        throw std::runtime_error("bow table exceeds allocated size");
    }
    out.write(reinterpret_cast<const char*>(tables.bow_table.data()),
              static_cast<std::streamsize>(tables.bow_table.size() * sizeof(float)));
    for (std::size_t idx = tables.bow_table.size(); idx < bow_limit; ++idx) {
        out.write(reinterpret_cast<const char*>(&zero), sizeof(float));
    }

    for (int lvl = 0; lvl < model.Order(); ++lvl) {
        const auto& nodes = threaded.nodes[static_cast<std::size_t>(lvl)];
        out.write(reinterpret_cast<const char*>(nodes.data()),
                  static_cast<std::streamsize>(nodes.size() * sizeof(PackedNode)));
    }
    out.write(reinterpret_cast<const char*>(threaded.leaves.data()),
              static_cast<std::streamsize>(threaded.leaves.size() * sizeof(PackedLeaf)));
}

} // namespace

void CompactRun(const CompactOptions& options) {
    SimpleSlm model;
    if (!model.Load(options.input)) {
        throw std::runtime_error("failed to load primitive slm");
    }
    model.BuildLinks();
    auto tables = BuildTables(model);
    auto threaded = BuildThreaded(model, tables);
    WriteModel(model, tables, threaded, options.output);
}

} // namespace sime
