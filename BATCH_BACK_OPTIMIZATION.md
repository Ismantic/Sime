# 批处理与回退缓存优化设计

## 优化目标

1. **回退缓存 (Back Cache)**: 缓存 `Scorer::Back()` 的结果，避免重复计算
2. **批处理 (Batch Processing)**: 批量处理状态转移，减少函数调用开销

## 1. 回退缓存优化

### 问题分析
- `Scorer::Back(State& state)` 在每次状态转移后都会调用
- Back 操作是纯函数：输入相同的 State，输出总是相同
- 在解码过程中，相同的 scorer_state 会重复出现多次
- 每次 Back 操作涉及数组访问和条件判断

### 实现方案
在 Scorer 中添加缓存：
```cpp
// In Scorer class
mutable std::unordered_map<uint64_t, State> back_cache_;

// Helper to convert State to cache key
static uint64_t StateToKey(const State& s) {
    return (static_cast<uint64_t>(s.level) << 32) | s.index;
}

// Cached version of Back
void BackCached(State& state) const {
    uint64_t key = StateToKey(state);
    auto it = back_cache_.find(key);
    if (it != back_cache_.end()) {
        state = it->second;
        return;
    }
    State original = state;
    Back(state);
    back_cache_[key] = state;
}
```

### 优势
- 避免重复的数组访问和计算
- 对于相同的 scorer_state，直接返回缓存结果
- 缓存命中率预计较高（beam search 中状态重复较多）

## 2. 批处理优化

### 问题分析
当前 Process 函数逐个处理状态转移：
```cpp
for (each state in column) {
    for (each word in column.vecs) {
        ScoreMove(state, word, next_state);
        Back(next_state);
        Add next_state to next column
    }
}
```

每次循环都有：
- 函数调用开销
- 缓存局部性差
- 无法向量化

### 实现方案 A: 收集-批处理模式
```cpp
struct Transition {
    const State* from_state;
    const Lattice* word;
    std::size_t target_col;
};

// Collect all transitions
std::vector<Transition> transitions;
for (col : lattice) {
    for (state : col.states) {
        for (word : col.vecs) {
            transitions.push_back({&state, &word, word.right});
        }
    }
}

// Batch process transitions
std::vector<State> next_states(transitions.size());
for (i : transitions) {
    ScoreMove(...);
    BackCached(next_state);
    next_states[i] = next_state;
}

// Add to target columns
for (i : transitions) {
    lattice[transitions[i].target_col].states.Add(next_states[i]);
}
```

### 实现方案 B: 列级批处理（推荐）
不改变整体结构，仅在单列内批处理：
```cpp
void ProcessColumn(Column& column, std::vector<Column>& lattice) const {
    // Collect all state-word pairs for this column
    std::vector<std::pair<const State*, const Lattice*>> pairs;
    for (state : column.states) {
        for (word : column.vecs) {
            pairs.push_back({&state, &word});
        }
    }

    // Batch process
    for (auto [state, word] : pairs) {
        Scorer::State next_state;
        double step = scorer_.ScoreMove(state->scorer_state, word->id, next_state);
        scorer_.BackCached(next_state);
        double next_cost = state->score + step;
        State next(next_cost, word->right, state, next_state, word->id);
        lattice[word->right].states.Add(next);
    }
}
```

### 优势
- 更好的数据局部性
- 可以预分配内存
- 为后续 SIMD 优化铺平道路
- 代码结构清晰，易于维护

## 3. 实现步骤

1. ✅ 在 Scorer 中添加 BackCached 方法和缓存
2. ✅ 修改 interpret.cc 中的 Process 函数，使用 BackCached
3. ✅ 实现列级批处理（可选，作为进一步优化）
4. ✅ 添加编译选项控制是否启用优化
5. ✅ 性能测试和验证

## 4. 编译选项

```cpp
// In interpret.h or common.h
#ifndef SIME_USE_BACK_CACHE
#define SIME_USE_BACK_CACHE 1
#endif

#ifndef SIME_USE_BATCH_PROCESSING
#define SIME_USE_BATCH_PROCESSING 0  // Conservative default
#endif
```

## 5. 预期性能提升

- **回退缓存**: 5-15% 性能提升（取决于缓存命中率）
- **批处理**: 10-20% 性能提升（减少函数调用开销）
- **组合优化**: 15-30% 总性能提升

## 6. 内存开销

- **回退缓存**: O(unique_states) 额外内存，预计 < 1MB
- **批处理**: O(states_per_column * words_per_column) 临时内存，预计 < 100KB

## 7. 风险和限制

- 回退缓存增加内存使用
- 需要定期清理缓存（可选）
- 批处理可能延迟错误检测
