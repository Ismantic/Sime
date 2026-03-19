#include "convert.h"
#include "common.h"
#include "unit.h"
#include "ustr.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sime {

namespace convert_internal {

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

void BubbleUpValue(HeapBuffer& heap, ArrayBuffer& arr, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap[index] < heap[parent]) {
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

void IronDownValue(HeapBuffer& heap, ArrayBuffer& arr, int index, int bottom) {
    int left = 0;
    while ((left = 2 * index + 1) < bottom) {
        int max_index = index;
        if (heap[max_index] < heap[left]) {
            max_index = left;
        }
        if (left + 1 < bottom && heap[max_index] < heap[left + 1]) {
            max_index = left + 1;
        }
        if (max_index == index) {
            break;
        }
        for (unsigned h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(max_index);
        }
        for (unsigned h = heap[max_index].first; h < heap[max_index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(index);
        }
        std::swap(heap[max_index], heap[index]);
        index = max_index;
    }
}

void BubbleUpDistance(HeapBuffer& heap, ArrayBuffer& arr, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap[parent].distance <= heap[index].distance) {
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

void IronDownDistance(HeapBuffer& heap, ArrayBuffer& arr, int index, int bottom) {
    int left = 0;
    while ((left = 2 * index + 1) < bottom) {
        int min_index = index;
        if (heap[left].distance < heap[min_index].distance) {
            min_index = left;
        }
        if (left + 1 < bottom && heap[left + 1].distance < heap[min_index].distance) {
            min_index = left + 1;
        }
        if (min_index == index) {
            break;
        }
        for (unsigned h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(min_index);
        }
        for (unsigned h = heap[min_index].first; h < heap[min_index].last; ++h) {
            arr[h].heap_index = static_cast<unsigned>(index);
        }
        std::swap(heap[min_index], heap[index]);
        index = min_index;
    }
}

class ValueCompressor {
public:
    void Compress(std::map<float, int>& values,
                  std::map<float, int>& mapping,
                  std::vector<float>& table,
                  unsigned limit) const {
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
            BubbleUpDistance(heap, arr, i);
        }
        if (!heap.empty()) {
            heap.back().distance = std::numeric_limits<double>::max();
            BubbleUpDistance(heap, arr, static_cast<int>(heap.size()) - 1);
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

            IronDownDistance(heap, arr, next_index, static_cast<int>(heap.size()));
            if (prev_index > 0) {
                IronDownDistance(heap, arr, prev_index, static_cast<int>(heap.size()));
            }

            heap.front() = heap.back();
            for (unsigned h = heap.front().first; h < heap.front().last; ++h) {
                arr[h].heap_index = 0U;
            }
            heap.pop_back();
            IronDownDistance(heap, arr, 0, static_cast<int>(heap.size()));
        }

        for (int i = 1; i < static_cast<int>(heap.size()); ++i) {
            BubbleUpValue(heap, arr, i);
        }
        for (int i = static_cast<int>(heap.size()) - 1; i > 0; --i) {
            for (unsigned h = heap[0].first; h < heap[0].last; ++h) {
                arr[h].heap_index = static_cast<unsigned>(i);
            }
            for (unsigned h = heap[i].first; h < heap[i].last; ++h) {
                arr[h].heap_index = 0U;
            }
            std::swap(heap[0], heap[i]);
            IronDownValue(heap, arr, 0, i);
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
                             unsigned limit) const {
        std::map<float, int> temp;
        Compress(values, temp, table, limit);

        mapping.clear();
        for (const auto& [eff_value, index] : temp) {
            mapping[effective_to_real[eff_value]] = index;
        }
    }
};

constexpr std::uint32_t kBitsBow = 14;
constexpr std::uint32_t kBitsPr = 16;
constexpr std::uint32_t kWordBits = 18;
constexpr std::uint32_t kBonBits = 23;
constexpr std::uint32_t kBolBits = 2;
constexpr std::uint32_t kChildBits = 23;

struct RawLeafOnDisk {
    TokenID id = 0;
    float pr = 0.0f;
};

struct RawNodeOnDisk : RawLeafOnDisk {
    std::int32_t child = 0;
    float bow = 0.0f;
};

static_assert(sizeof(RawLeafOnDisk) == 8, "raw leaf layout mismatch");
static_assert(sizeof(RawNodeOnDisk) == 16, "raw node layout mismatch");

struct RawNode {
    TokenID id = 0;
    std::uint32_t child = 0;
    double freq = 0.0;
    double pr = 0.0;
    double bow = 0.0;
};

struct RawLeaf {
    TokenID id = 0;
    double freq = 0.0;
    double pr = 0.0;
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
        std::size_t count = static_cast<std::size_t>(level_sizes_[lvl]);
        std::vector<RawNodeOnDisk> raw(count);
        if (!in.read(reinterpret_cast<char*>(raw.data()),
                     static_cast<std::streamsize>(raw.size() * sizeof(RawNodeOnDisk)))) {
            return false;
        }
        auto& nodes = levels_[lvl];
        nodes.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            nodes[i].id = raw[i].id;
            nodes[i].child = static_cast<std::uint32_t>(raw[i].child);
            nodes[i].pr = static_cast<double>(raw[i].pr);
            nodes[i].bow = static_cast<double>(raw[i].bow);
            nodes[i].freq = 0.0;
        }
    }

    std::size_t leaf_total = static_cast<std::size_t>(level_sizes_.back());
    std::vector<RawLeafOnDisk> raw_leaves(leaf_total);
    if (!in.read(reinterpret_cast<char*>(raw_leaves.data()),
                 static_cast<std::streamsize>(raw_leaves.size() * sizeof(RawLeafOnDisk)))) {
        return false;
    }
    leaves_.resize(leaf_total);
    for (std::size_t i = 0; i < leaf_total; ++i) {
        leaves_[i].id = raw_leaves[i].id;
        leaves_[i].pr = static_cast<double>(raw_leaves[i].pr);
        leaves_[i].freq = 0.0;
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
        if (level == 0) {
            return;
        }
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

Tables BuildTables(const SimpleSlm& model) {
    Tables tables;
    ValueCompressor compressor;
    std::map<float, float> pr_eff;
    std::map<float, int> pr_counts;
    std::map<float, float> bow_eff;
    std::map<float, int> bow_counts;

    auto add_pr = [&](float value) {
        float eff = EffectivePr(model.UseLog(), value);
        auto it = pr_eff.find(eff);
        if (it == pr_eff.end()) {
            pr_eff[eff] = value;
        } else {
            pr_eff[eff] = OriginalPr(model.UseLog(), eff);
        }
        ++pr_counts[eff];
    };

    auto add_bow = [&](float value) {
        float eff = EffectiveBow(model.UseLog(), value);
        auto it = bow_eff.find(eff);
        if (it == bow_eff.end()) {
            bow_eff[eff] = value;
        } else {
            bow_eff[eff] = OriginalBow(model.UseLog(), eff);
        }
        ++bow_counts[eff];
    };

    for (int lvl = 0; lvl < model.Order(); ++lvl) {
        const auto& nodes = model.Level(lvl);
        auto actual = model.ActualSize(lvl);
        for (std::size_t idx = 0; idx < actual; ++idx) {
            add_pr(static_cast<float>(nodes[idx].pr));
            add_bow(static_cast<float>(nodes[idx].bow));
        }
    }
    const auto& leaves = model.Leaves();
    for (std::size_t idx = 0; idx < model.LeafCount(); ++idx) {
        add_pr(static_cast<float>(leaves[idx].pr));
    }

    static constexpr float kMilestonesPr[] = {
        0.9F, 0.8F, 0.7F, 0.6F, 1.0F / 2.0F, 1.0F / 4.0F, 1.0F / 8.0F,
        1.0F / 16.0F, 1.0F / 32.0F, 1.0F / 64.0F, 1.0F / 128.0F,
        1.0F / 256.0F, 1.0F / 512.0F, 1.0F / 1024.0F, 1.0F / 2048.0F,
        1.0F / 4096.0F, 1.0F / 8192.0F, 1.0F / 16384.0F, 1.0F / 32768.0F,
        1.0F / 65536.0F};
    for (float milestone : kMilestonesPr) {
        float real = model.UseLog() ? -std::log(milestone) : milestone;
        float eff = EffectivePr(model.UseLog(), real);
        if (pr_eff.find(eff) == pr_eff.end()) {
            pr_eff[eff] = real;
        } else {
            pr_eff[eff] = OriginalPr(model.UseLog(), eff);
        }
        pr_counts[eff] = 0;
    }

    static constexpr float kMilestonesBow[] = {
        1.0F,  0.9F,  0.8F,   0.7F,    0.6F,     0.5F,     0.4F,     0.3F,
        0.2F,  0.1F,  0.05F,  0.01F,   0.005F,   0.001F,   0.0005F,  0.0001F,
        0.00005F, 0.00001F, 0.000005F, 0.000001F, 0.0000005F, 0.0000001F};
    for (float milestone : kMilestonesBow) {
        float real = model.UseLog() ? -std::log(milestone) : milestone;
        float eff = EffectiveBow(model.UseLog(), real);
        if (bow_eff.find(eff) == bow_eff.end()) {
            bow_eff[eff] = real;
        } else {
            bow_eff[eff] = OriginalBow(model.UseLog(), eff);
        }
        bow_counts[eff] = 0;
    }

    compressor.CompressWithReverse(pr_eff,
                                   pr_counts,
                                   tables.pr_map,
                                   tables.pr_table,
                                   1U << kBitsPr);
    for (auto& value : tables.pr_table) {
        value = OriginalPr(model.UseLog(), value);
    }

    compressor.CompressWithReverse(bow_eff,
                                   bow_counts,
                                   tables.bow_map,
                                   tables.bow_table,
                                   1U << kBitsBow);
    for (auto& value : tables.bow_table) {
        value = OriginalBow(model.UseLog(), value);
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
                                           static_cast<float>(node.pr),
                                           model.UseLog(),
                                           EffectivePr,
                                           OriginalPr);
            const int bow_idx = LookupIndex(tables.bow_map,
                                            static_cast<float>(node.bow),
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
                                       static_cast<float>(leaf.pr),
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

} // namespace convert_internal

void ConvertRun(const ConvertOptions& options) {
    convert_internal::SimpleSlm model;
    if (!model.Load(options.input)) {
        throw std::runtime_error("failed to load primitive slm");
    }
    model.BuildLinks();
    auto tables = convert_internal::BuildTables(model);
    auto threaded = convert_internal::BuildThreaded(model, tables);
    convert_internal::WriteModel(model, tables, threaded, options.output);
}

namespace {

struct TrieNodeHeader {
    std::uint16_t count = 0;
    std::uint16_t move_count = 0;
};

struct TrieMove {
    std::uint32_t i = 0;
    std::uint32_t unit = 0;
};

struct TrieEntry {
    std::uint32_t i = 0;
    std::uint8_t cost = 0;
    std::uint8_t empty[3]{};
};

bool IsWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool IsPhoneChar(char ch) {
    return (ch >= 'a' && ch <= 'z') || ch == '\'' || ch == '"' ||
           (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

} // namespace

bool TrieConverter::Load(const std::filesystem::path& path) {
    nodes_.clear();
    order_.clear();
    metrics_.clear();
    lexicon_.clear();
    root_ = CreateNode();
    UnitParser parser;

    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    std::string text;
    std::uint32_t next_id = kRealTokenStart;
    std::vector<Phone> phones;
    while (std::getline(in, line)) {
        bool s = ParseLine(line, text, phones);
        if (text.empty()) {
            continue;
        }
        std::uint32_t i = next_id++;
        if (lexicon_.size() <= i) {
            lexicon_.resize(i + 1);
        }
        auto& entry = lexicon_[i];
        if (entry.empty()) {
            entry = text;
        }
        if (!s) {
            continue;
        }
        std::vector<Unit> units;
        for (const auto& phone : phones) {
            if (!parser.ParseUnits(phone.str, units)) {
                continue;
            }
            InsertUnits(i, phone.cost, units);
        }
    }
    return true;
}

bool TrieConverter::ParseLine(const std::string& line,
                              std::string& text_token,
                              std::vector<Phone>& phones) const {
    phones.clear();
    text_token.clear();
    if (line.empty() || line[0] == '#' || line[0] == '\n') {
        return false;
    }

    const char* ptr = line.c_str();
    while (*ptr && IsWhitespace(*ptr)) {
        ++ptr;
    }
    if (*ptr == '\0' || *ptr == '#') {
        return false;
    }
    const char* word_start = ptr;
    while (*ptr && !IsWhitespace(*ptr)) {
        ++ptr;
    }
    text_token.assign(word_start, ptr - word_start);

    std::map<std::string, std::uint8_t> unique;
    while (*ptr) {
        while (*ptr && IsWhitespace(*ptr)) {
            ++ptr;
        }
        if (*ptr == '\0') {
            break;
        }
        const char* token_start = ptr;
        while (*ptr && !IsWhitespace(*ptr)) {
            ++ptr;
        }
        std::string token(token_start, ptr - token_start);
        std::size_t pos = 0;
        while (pos < token.size() && IsPhoneChar(token[pos])) {
            ++pos;
        }
        if (pos == 0 || pos < token.size()) {
            continue;
        }
        auto [it, inserted] =
            unique.emplace(std::move(token), static_cast<std::uint8_t>(0));
        (void)it;
        (void)inserted;
    }

    for (const auto& [p, c] : unique) {
        phones.push_back(Phone{p, c});
    }
    return !phones.empty();
}

void TrieConverter::InsertUnits(std::uint32_t id,
                                std::uint8_t cost,
                                const std::vector<Unit>& units) {
    if (units.empty() || root_ == nullptr) {
        return;
    }
    Node* node = root_;
    for (const auto& u : units) {
        node = InsertMove(node, u);
    }
    InsertText(node, id, cost);
}

TrieConverter::Node* TrieConverter::CreateNode() {
    auto node = std::make_unique<Node>();
    Node* raw = node.get();
    nodes_.push_back(std::move(node));
    order_.push_back(raw);
    return raw;
}

TrieConverter::Node* TrieConverter::InsertMove(Node* node, Unit u) {
    auto it = node->moves.find(u.value);
    if (it != node->moves.end()) {
        return it->second;
    }
    Node* c = CreateNode();
    node->moves[u.value] = c;
    return c;
}

void TrieConverter::InsertText(Node* node,
                               std::uint32_t id,
                               std::uint8_t cost) {
    auto it = node->costs.find(id);
    if (it == node->costs.end() ||
        cost < it->second) {
        node->costs[id] = cost;
    }
}

std::size_t TrieConverter::Count() const {
    return lexicon_.size();
}

std::vector<std::string> TrieConverter::Dump() const {
    std::vector<std::string> result;
    result.reserve(lexicon_.size());
    for (const auto& entry : lexicon_) {
        result.push_back(entry);
    }
    return result;
}

std::size_t TrieConverter::SerializeTree(std::vector<char>& buffer) {
    metrics_.clear();
    const std::uint32_t header = 3 * sizeof(std::uint32_t);
    std::uint32_t offset = header;
    for (Node* node : order_) {
        NodeSize metrics;
        metrics.size =
            sizeof(TrieNodeHeader) +
            node->moves.size() * sizeof(TrieMove) +
            node->costs.size() * sizeof(TrieEntry);
        metrics.i = offset;
        metrics_[node] = metrics;
        offset += static_cast<std::uint32_t>(metrics.size);
    }
    buffer.assign(offset, 0);
    for (Node* node : order_) {
        SerializeNode(node, metrics_.at(node), buffer);
    }
    return offset - header;
}

void TrieConverter::SerializeNode(const Node* node,
                                  const NodeSize& metrics,
                                  std::vector<char>& buffer) {
    auto* base =
        reinterpret_cast<TrieNodeHeader*>(buffer.data() + metrics.i);
    TrieNodeHeader header{};
    auto entry_count =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            node->costs.size(), static_cast<std::size_t>(0xFFF)));
    auto move_count =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            node->moves.size(), static_cast<std::size_t>(0xFFF)));
    header.count = static_cast<std::uint16_t>(entry_count);
    header.move_count = static_cast<std::uint16_t>(move_count);
    *base = header;

    auto* moves = reinterpret_cast<TrieMove*>(base + 1);
    std::size_t idx = 0;
    for (const auto& [unit, child] : node->moves) {
        moves[idx].unit = unit;
        moves[idx].i = metrics_.at(child).i;
        ++idx;
    }

    struct Candidate {
        std::uint32_t id = 0;
        std::uint8_t cost = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(node->costs.size());
    for (const auto& [id, cost] : node->costs) {
        candidates.push_back(Candidate{id, cost});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) {
                  if (lhs.cost != rhs.cost) {
                      return lhs.cost < rhs.cost;
                  }
                  return lhs.id < rhs.id;
              });

    auto* entries = reinterpret_cast<TrieEntry*>(moves + node->moves.size());
    idx = 0;
    for (const auto& candidate : candidates) {
        TrieEntry info{};
        info.i = candidate.id & 0xFFFFFFU;
        info.cost = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(31, candidate.cost));
        entries[idx++] = info;
    }
}

std::size_t TrieConverter::WriteStrTable(std::vector<char>& buffer) {
    std::vector<char32_t> table;
    for (const auto& entry : lexicon_) {
        if (entry.empty()) {
            table.push_back(0);
            continue;
        }
        std::u32string word = ustr::ToU32(entry);
        table.insert(table.end(), word.begin(), word.end());
        table.push_back(0);
    }
    const auto bytes = table.size() * sizeof(char32_t);
    std::size_t offset = buffer.size();
    buffer.resize(buffer.size() + bytes);
    std::memcpy(buffer.data() + offset, table.data(), bytes);
    return bytes;
}

bool TrieConverter::Write(const std::filesystem::path& output) {
    std::vector<char> buffer;
    auto tree_size = SerializeTree(buffer);
    WriteStrTable(buffer);
    std::uint32_t str_count =
        static_cast<std::uint32_t>(lexicon_.size());
    std::uint32_t node_count =
        static_cast<std::uint32_t>(order_.size());
    std::uint32_t str_offset =
        static_cast<std::uint32_t>(tree_size + 3 * sizeof(std::uint32_t));
    auto* header = reinterpret_cast<std::uint32_t*>(buffer.data());
    header[0] = str_count;
    header[1] = node_count;
    header[2] = str_offset;

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(buffer.data(),
              static_cast<std::streamsize>(buffer.size()));
    return out.good();
}

} // namespace sime
