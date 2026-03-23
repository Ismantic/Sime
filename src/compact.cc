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
    std::uint32_t heap_index = 0;
};

struct HeapItem {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::uint32_t count = 0;
    float_t approx = 0.0;
    float_t sum = 0.0;
    float_t distance = 0.0;

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
        for (std::uint32_t h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<std::uint32_t>(parent);
        }
        for (std::uint32_t h = heap[parent].first; h < heap[parent].last; ++h) {
            arr[h].heap_index = static_cast<std::uint32_t>(index);
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
        for (std::uint32_t h = heap[index].first; h < heap[index].last; ++h) {
            arr[h].heap_index = static_cast<std::uint32_t>(best);
        }
        for (std::uint32_t h = heap[best].first; h < heap[best].last; ++h) {
            arr[h].heap_index = static_cast<std::uint32_t>(index);
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
                    std::uint32_t limit) {
        ArrayBuffer arr;
        HeapBuffer heap;
        arr.reserve(values.size());
        heap.reserve(values.size());

        for (const auto& [val, freq] : values) {
            std::uint32_t idx = static_cast<std::uint32_t>(arr.size());
            arr.push_back(ArrayItem{val, idx});
            float_t sum = static_cast<float_t>(val);
            if (freq > 0) {
                sum *= static_cast<float_t>(freq);
            }
            heap.push_back(
                HeapItem{idx, idx + 1, static_cast<std::uint32_t>(freq), val, sum, 0.0});
        }

        const int heap_size = static_cast<int>(heap.size());
        for (int i = 0; i < heap_size - 1; ++i) {
            if (heap[i].count == 0 || heap[i + 1].count == 0) {
                heap[i].distance = std::numeric_limits<float_t>::max();
            } else {
                heap[i].distance = heap[i + 1].approx - heap[i].approx;
            }
            BubbleUp(heap, arr, i, CmpByDistance);
        }
        if (!heap.empty()) {
            heap.back().distance = std::numeric_limits<float_t>::max();
            BubbleUp(heap, arr, static_cast<int>(heap.size()) - 1, CmpByDistance);
        }

        while (static_cast<int>(heap.size()) > static_cast<int>(limit)) {
            const auto current_first = heap.front().first;
            const auto current_last = heap.front().last;
            int prev_index = (current_first == 0)
                                 ? -1
                                 : static_cast<int>(arr[current_first - 1].heap_index);
            int next_index = static_cast<int>(arr[current_last].heap_index);

            for (std::uint32_t h = current_first; h < current_last; ++h) {
                arr[h].heap_index = static_cast<std::uint32_t>(next_index);
            }

            const float_t merged_val = (heap.front().sum + heap[next_index].sum) /
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
            for (std::uint32_t h = heap.front().first; h < heap.front().last; ++h) {
                arr[h].heap_index = 0U;
            }
            heap.pop_back();
            SiftDown(heap, arr, 0, static_cast<int>(heap.size()), CmpByDistance);
        }

        for (int i = 1; i < static_cast<int>(heap.size()); ++i) {
            BubbleUp(heap, arr, i, CmpByValue);
        }
        for (int i = static_cast<int>(heap.size()) - 1; i > 0; --i) {
            for (std::uint32_t h = heap[0].first; h < heap[0].last; ++h) {
                arr[h].heap_index = static_cast<std::uint32_t>(i);
            }
            for (std::uint32_t h = heap[i].first; h < heap[i].last; ++h) {
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
                         std::uint32_t limit) {
    std::map<float, int> temp;
    CompressValues(values, temp, table, limit);

    mapping.clear();
    for (const auto& [eff_value, index] : temp) {
        mapping[effective_to_real[eff_value]] = index;
    }
}

constexpr std::uint32_t BowBits = 14;
constexpr std::uint32_t ProBits = 16;
constexpr std::uint32_t TokenBits = 18;
constexpr std::uint32_t BonBits = 23;
constexpr std::uint32_t BolBits = 2;
constexpr std::uint32_t DownBits = 23;

struct RawNode {
    TokenID id = 0;
    float pro = 0.0f;
    std::uint32_t down = 0;
    float bow = 0.0f;
};

struct RawLeave {
    TokenID id = 0;
    float pro = 0.0f;
};

class RawModel {
public:
    bool Load(const std::filesystem::path& path);
    void LinkUps();

    int Num() const { return num_; }
    const std::vector<int>& LevelSizes() const { return level_sizes_; }
    const std::vector<RawNode>& Level(int idx) const { return levels_.at(idx); }
    const std::vector<RawLeave>& Leaves() const { return leaves_; }

    std::size_t LevelCount(int level) const;
    std::size_t LeaveCount() const;

    void GetTokens(int level, int index, std::vector<TokenID>& out) const;
    int GetIndex(int length, const TokenID* seq) const;
    bool HasDown(int level, int index) const;
    void GetBack(int length,
                 const TokenID* seq,
                 std::uint32_t& boe,
                 std::uint32_t& bon) const;

private:
    std::pair<std::size_t, std::size_t> DownRange(int level, int index) const;

    int num_ = 0;
    std::vector<int> level_sizes_;
    std::vector<std::vector<RawNode>> levels_;
    std::vector<RawLeave> leaves_;
    std::vector<std::vector<int>> node_ups_;
    std::vector<int> leave_ups_;
};

struct CompactNode {
    std::uint32_t w0 = 0;
    std::uint32_t w1 = 0;
    std::uint32_t w2 = 0;
};

struct CompactLeave {
    std::uint32_t w0 = 0;
    std::uint32_t w1 = 0;
};

float EffectivePro(float value) {
    return value / std::log(2.0F);
}

float OriginalPro(float effective) {
    return effective * std::log(2.0F);
}

float EffectiveBow(float value) {
    return std::exp(-value);
}

float OriginalBow(float effective) {
    return -std::log(effective);
}

using EffFn1 = float(*)(float);

int LookupIndex(const std::map<float, int>& mapping,
                float real_value,
                EffFn1 eff_fn,
                EffFn1 orig_fn) {
    auto it = mapping.find(real_value);
    if (it != mapping.end()) {
        return it->second;
    }

    float effective = eff_fn(real_value);
    float canonical = orig_fn(effective);
    it = mapping.find(canonical);
    if (it != mapping.end()) {
        return it->second;
    }
    throw std::runtime_error("quantized value not found");
}

struct Tables {
    std::map<float, int> pro_map;
    std::map<float, int> bow_map;
    std::vector<float> pro_table;
    std::vector<float> bow_table;
};

struct CompactModel {
    std::vector<std::vector<CompactNode>> nodes;
    std::vector<CompactLeave> leaves;
};

bool RawModel::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    if (!in.read(reinterpret_cast<char*>(&num_), sizeof(num_))) {
        return false;
    }
    std::uint32_t flag = 0;
    if (!in.read(reinterpret_cast<char*>(&flag), sizeof(flag))) {
        return false;
    }
    // flag is reserved (always 1 = log mode)

    level_sizes_.assign(num_ + 1, 0);
    if (!in.read(reinterpret_cast<char*>(level_sizes_.data()),
                 static_cast<std::streamsize>(level_sizes_.size() * sizeof(int)))) {
        return false;
    }

    levels_.resize(num_);
    for (int lvl = 0; lvl < num_; ++lvl) {
        auto& nodes = levels_[lvl];
        nodes.resize(static_cast<std::size_t>(level_sizes_[lvl]));
        if (!in.read(reinterpret_cast<char*>(nodes.data()),
                     static_cast<std::streamsize>(nodes.size() * sizeof(RawNode)))) {
            return false;
        }
    }

    leaves_.resize(static_cast<std::size_t>(level_sizes_.back()));
    if (!in.read(reinterpret_cast<char*>(leaves_.data()),
                 static_cast<std::streamsize>(leaves_.size() * sizeof(RawLeave)))) {
        return false;
    }
    return true;
}

void RawModel::LinkUps() {
    node_ups_.clear();
    node_ups_.resize(static_cast<std::size_t>(num_));
    for (int level = 1; level < num_; ++level) {
        auto actual = LevelCount(level);
        node_ups_[static_cast<std::size_t>(level)].resize(actual);
        const auto& ups = levels_[static_cast<std::size_t>(level - 1)];
        std::size_t up = 0;
        for (std::size_t idx = 0; idx < actual; ++idx) {
            while (up + 1 < ups.size() &&
                   ups[up + 1].down <= idx) {
                ++up;
            }
            node_ups_[static_cast<std::size_t>(level)][idx] = static_cast<int>(up);
        }
    }

    leave_ups_.clear();
    if (num_ == 0) {
        return;
    }
    auto total_leaves = LeaveCount();
    leave_ups_.resize(total_leaves);
    const auto& ups = levels_[static_cast<std::size_t>(num_ - 1)];
    std::size_t up = 0;
    for (std::size_t idx = 0; idx < total_leaves; ++idx) {
        while (up + 1 < ups.size() &&
               ups[up + 1].down <= idx) {
            ++up;
        }
        leave_ups_[idx] = static_cast<int>(up);
    }
}

std::size_t RawModel::LevelCount(int level) const {
    if (level_sizes_.empty()) {
        return 0;
    }
    const auto size = level_sizes_.at(level);
    return (size == 0) ? 0 : static_cast<std::size_t>(size - 1);
}

std::size_t RawModel::LeaveCount() const {
    if (level_sizes_.empty()) {
        return 0;
    }
    const auto size = level_sizes_.back();
    return (size == 0) ? 0 : static_cast<std::size_t>(size - 1);
}

std::pair<std::size_t, std::size_t> RawModel::DownRange(int level, int index) const {
    const auto& nodes = levels_.at(static_cast<std::size_t>(level));
    std::size_t begin = nodes.at(static_cast<std::size_t>(index)).down;
    std::size_t end = nodes.at(static_cast<std::size_t>(index + 1)).down;
    return {begin, end};
}

void RawModel::GetTokens(int level, int index, std::vector<TokenID>& out) const {
    out.assign(static_cast<std::size_t>(level), TokenID{0});
    if (level == 0) {
        return;
    }
    if (level == num_) {
        int current = index;
        out.back() = leaves_.at(static_cast<std::size_t>(current)).id;
        int up_idx = leave_ups_.at(static_cast<std::size_t>(current));
        int current_level = num_ - 1;
        for (int pos = level - 1; pos > 0; --pos) {
            const auto& nodes = levels_.at(static_cast<std::size_t>(current_level));
            out[static_cast<std::size_t>(pos - 1)] =
                nodes.at(static_cast<std::size_t>(up_idx)).id;
            if (pos - 1 == 0) {
                break;
            }
            up_idx = node_ups_.at(static_cast<std::size_t>(current_level))
                         .at(static_cast<std::size_t>(up_idx));
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
        current_idx = node_ups_.at(static_cast<std::size_t>(current_level))
                          .at(static_cast<std::size_t>(current_idx));
        --current_level;
    }
}

int RawModel::GetIndex(int length, const TokenID* seq) const {
    if (length == 0) {
        return 0;
    }
    const TokenID* hw = seq;
    int n = length;
    while (n > num_) {
        --n;
        ++hw;
    }

    int node_index = 0;
    for (int lvl = 0; lvl < n; ++lvl) {
        auto [begin, end] = DownRange(lvl, node_index);
        if (begin >= end) {
            return -1;
        }
        if (lvl + 1 == num_) {
            const auto it = std::lower_bound(
                leaves_.begin() + static_cast<std::ptrdiff_t>(begin),
                leaves_.begin() + static_cast<std::ptrdiff_t>(end),
                hw[lvl],
                [](const RawLeave& leaf, TokenID id) { return leaf.id < id; });
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

bool RawModel::HasDown(int level, int index) const {
    if (level >= num_) {
        return false;
    }
    const auto& nodes = levels_.at(static_cast<std::size_t>(level));
    const auto begin = nodes.at(static_cast<std::size_t>(index)).down;
    const auto end = nodes.at(static_cast<std::size_t>(index + 1)).down;
    return end > begin;
}

void RawModel::GetBack(int length,
                       const TokenID* seq,
                       std::uint32_t& boe,
                       std::uint32_t& bon) const {
    boe = 0;
    bon = 0;
    if (length <= 1 || seq == nullptr) {
        return;
    }
    const TokenID* hw = seq;
    int n = length;
    while (n > 1) {
        --n;
        ++hw;
        int idx = GetIndex(n, hw);
        if (idx >= 0 && HasDown(n, idx)) {
            boe = static_cast<std::uint32_t>(n);
            bon = static_cast<std::uint32_t>(idx);
            return;
        }
    }
}

void CollectValue(float value, EffFn1 eff_fn, EffFn1 orig_fn,
                  std::map<float, float>& eff_map, std::map<float, int>& counts) {
    float eff = eff_fn(value);
    if (eff_map.find(eff) == eff_map.end()) {
        eff_map[eff] = value;
    } else {
        eff_map[eff] = orig_fn(eff);
    }
    ++counts[eff];
}

template <std::size_t N>
void AddMilestones(const float (&milestones)[N], EffFn1 eff_fn, EffFn1 orig_fn,
                   std::map<float, float>& eff_map, std::map<float, int>& counts) {
    for (float milestone : milestones) {
        float real = -std::log(milestone);
        float eff = eff_fn(real);
        if (eff_map.find(eff) == eff_map.end()) {
            eff_map[eff] = real;
        } else {
            eff_map[eff] = orig_fn(eff);
        }
        counts[eff] = 0;
    }
}

Tables Compress(const RawModel& model) {
    Tables tables;

    std::map<float, float> pro_eff;
    std::map<float, int> pro_counts;
    std::map<float, float> bow_eff;
    std::map<float, int> bow_counts;

    for (int lvl = 0; lvl < model.Num(); ++lvl) {
        const auto& nodes = model.Level(lvl);
        auto actual = model.LevelCount(lvl);
        for (std::size_t idx = 0; idx < actual; ++idx) {
            CollectValue(nodes[idx].pro,
                         EffectivePro, OriginalPro, pro_eff, pro_counts);
            CollectValue(nodes[idx].bow,
                         EffectiveBow, OriginalBow, bow_eff, bow_counts);
        }
    }
    const auto& leaves = model.Leaves();
    for (std::size_t idx = 0; idx < model.LeaveCount(); ++idx) {
        CollectValue(leaves[idx].pro,
                     EffectivePro, OriginalPro, pro_eff, pro_counts);
    }

    static constexpr float kMilestonesPro[] = {
        0.9F, 0.8F, 0.7F, 0.6F, 1.0F / 2.0F, 1.0F / 4.0F, 1.0F / 8.0F,
        1.0F / 16.0F, 1.0F / 32.0F, 1.0F / 64.0F, 1.0F / 128.0F,
        1.0F / 256.0F, 1.0F / 512.0F, 1.0F / 1024.0F, 1.0F / 2048.0F,
        1.0F / 4096.0F, 1.0F / 8192.0F, 1.0F / 16384.0F, 1.0F / 32768.0F,
        1.0F / 65536.0F};
    AddMilestones(kMilestonesPro, EffectivePro, OriginalPro, pro_eff, pro_counts);

    static constexpr float kMilestonesBow[] = {
        1.0F,  0.9F,  0.8F,   0.7F,    0.6F,     0.5F,     0.4F,     0.3F,
        0.2F,  0.1F,  0.05F,  0.01F,   0.005F,   0.001F,   0.0005F,  0.0001F,
        0.00005F, 0.00001F, 0.000005F, 0.000001F, 0.0000005F, 0.0000001F};
    AddMilestones(kMilestonesBow, EffectiveBow, OriginalBow, bow_eff, bow_counts);

    CompressWithReverse(pro_eff, pro_counts, tables.pro_map, tables.pro_table, 1U << ProBits);
    for (auto& value : tables.pro_table) {
        value = OriginalPro(value);
    }

    CompressWithReverse(bow_eff, bow_counts, tables.bow_map, tables.bow_table, 1U << BowBits);
    for (auto& value : tables.bow_table) {
        value = OriginalBow(value);
    }
    return tables;
}

CompactNode DoPackNode(TokenID wid,
                       std::uint32_t bow_index,
                       std::uint32_t pro_index,
                       std::uint32_t down_index,
                       std::uint32_t bon,
                       std::uint32_t boe) {
    if (wid >= (1U << TokenBits) || bow_index >= (1U << BowBits) ||
        pro_index >= (1U << ProBits) || down_index >= (1U << DownBits) ||
        bon >= (1U << BonBits) || boe >= (1U << BolBits)) {
        throw std::runtime_error("compact: value exceeds bit-field limit");
    }

    CompactNode node;
    node.w0 = (wid & ((1U << TokenBits) - 1U)) |
                 ((bow_index & ((1U << BowBits) - 1U)) << TokenBits);
    node.w1 = (pro_index & 0xFFFFU) | ((down_index & 0xFFFFU) << 16U);
    node.w2 = (bon & ((1U << BonBits) - 1U)) |
                 ((boe & ((1U << BolBits) - 1U)) << BonBits) |
                 (((down_index >> 16U) & 0x7FU) << (BonBits + BolBits));
    return node;
}

CompactLeave DoPackLeave(TokenID wid,
                         std::uint32_t pro_index,
                         std::uint32_t bon,
                         std::uint32_t boe) {
    if (wid >= (1U << TokenBits) || pro_index >= (1U << ProBits) ||
        bon >= (1U << BonBits) || boe >= (1U << BolBits)) {
        throw std::runtime_error("compact: leave value exceeds limit");
    }
    CompactLeave leaf;
    leaf.w0 = (wid & ((1U << TokenBits) - 1U)) |
                 ((pro_index & 0x3FFFU) << TokenBits);
    leaf.w1 = (bon & ((1U << BonBits) - 1U)) |
                 ((boe & ((1U << BolBits) - 1U)) << BonBits) |
                 (((pro_index >> 14U) & 0x3U) << (BonBits + BolBits));
    return leaf;
}

CompactModel Compact(const RawModel& model, const Tables& tables) {
    CompactModel pact;
    pact.nodes.resize(static_cast<std::size_t>(model.Num()));
    std::vector<TokenID> history;
    history.reserve(static_cast<std::size_t>(model.Num()));

    for (int lvl = 0; lvl < model.Num(); ++lvl) {
        const auto level_size = static_cast<std::size_t>(model.LevelSizes()[lvl]);
        const auto actual = model.LevelCount(lvl);
        pact.nodes[static_cast<std::size_t>(lvl)].resize(level_size);
        const auto& nodes = model.Level(lvl);
        for (std::size_t idx = 0; idx < actual; ++idx) {
            const auto& node = nodes[idx];
            const int pro_idx = LookupIndex(tables.pro_map,
                                            node.pro,
                                            EffectivePro,
                                            OriginalPro);
            const int bow_idx = LookupIndex(tables.bow_map,
                                            node.bow,
                                            EffectiveBow,
                                            OriginalBow);

            std::uint32_t boe = 0;
            std::uint32_t bon = 0;
            if (lvl == 0) {
                model.GetBack(0, nullptr, boe, bon);
            } else {
                model.GetTokens(lvl, static_cast<int>(idx), history);
                model.GetBack(lvl,
                              history.empty() ? nullptr : history.data(),
                              boe,
                              bon);
            }

            pact.nodes[static_cast<std::size_t>(lvl)][idx] =
                DoPackNode(node.id,
                           static_cast<std::uint32_t>(bow_idx),
                           static_cast<std::uint32_t>(pro_idx),
                           node.down,
                           bon,
                           boe);
        }

        const auto& sentinel = nodes.back();
        pact
            .nodes[static_cast<std::size_t>(lvl)][level_size - 1U] =
            DoPackNode(0, 0, 0, sentinel.down, 0, 0);
    }

    const auto leaf_size = static_cast<std::size_t>(model.LevelSizes().back());
    const auto leaf_actual = model.LeaveCount();
    pact.leaves.resize(leaf_size);
    const auto& leaves = model.Leaves();
    for (std::size_t idx = 0; idx < leaf_actual; ++idx) {
        const auto& leaf = leaves[idx];
        const int pro_idx = LookupIndex(tables.pro_map,
                                        leaf.pro,
                                        EffectivePro,
                                        OriginalPro);
        std::uint32_t boe = 0;
        std::uint32_t bon = 0;
        model.GetTokens(model.Num(), static_cast<int>(idx), history);
        model.GetBack(model.Num(), history.data(), boe, bon);
        pact.leaves[idx] = DoPackLeave(leaf.id,
                                       static_cast<std::uint32_t>(pro_idx),
                                       bon,
                                       boe);
    }
    pact.leaves[leaf_size - 1U] = CompactLeave{};
    return pact;
}

void Save(const RawModel& model,
          const Tables& tables,
          const CompactModel& pact,
          const std::filesystem::path& output) {
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open output slm file");
    }

    int num = model.Num();
    out.write(reinterpret_cast<const char*>(&num), sizeof(num));
    std::uint32_t flag = 1U;
    out.write(reinterpret_cast<const char*>(&flag), sizeof(flag));
    out.write(reinterpret_cast<const char*>(model.LevelSizes().data()),
              static_cast<std::streamsize>(model.LevelSizes().size() * sizeof(int)));

    const std::size_t pro_limit = 1U << ProBits;
    if (tables.pro_table.size() > pro_limit) {
        throw std::runtime_error("pro table exceeds allocated size");
    }
    out.write(reinterpret_cast<const char*>(tables.pro_table.data()),
              static_cast<std::streamsize>(tables.pro_table.size() * sizeof(float)));
    const float zero = 0.0F;
    for (std::size_t idx = tables.pro_table.size(); idx < pro_limit; ++idx) {
        out.write(reinterpret_cast<const char*>(&zero), sizeof(float));
    }

    const std::size_t bow_limit = 1U << BowBits;
    if (tables.bow_table.size() > bow_limit) {
        throw std::runtime_error("bow table exceeds allocated size");
    }
    out.write(reinterpret_cast<const char*>(tables.bow_table.data()),
              static_cast<std::streamsize>(tables.bow_table.size() * sizeof(float)));
    for (std::size_t idx = tables.bow_table.size(); idx < bow_limit; ++idx) {
        out.write(reinterpret_cast<const char*>(&zero), sizeof(float));
    }

    for (int lvl = 0; lvl < model.Num(); ++lvl) {
        const auto& nodes = pact.nodes[static_cast<std::size_t>(lvl)];
        out.write(reinterpret_cast<const char*>(nodes.data()),
                  static_cast<std::streamsize>(nodes.size() * sizeof(CompactNode)));
    }
    out.write(reinterpret_cast<const char*>(pact.leaves.data()),
              static_cast<std::streamsize>(pact.leaves.size() * sizeof(CompactLeave)));
}

} // namespace

void RunCompact(const std::filesystem::path& input,
                const std::filesystem::path& output) {
    RawModel model;
    if (!model.Load(input)) {
        throw std::runtime_error("failed to load primitive slm");
    }
    model.LinkUps();
    auto tables = Compress(model);
    auto pact = Compact(model, tables);
    Save(model, tables, pact, output);
}

} // namespace sime
