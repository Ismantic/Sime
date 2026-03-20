# Prune 原理与实现

本文整理 `Constructor::Prune()` 的剪枝流程。对应代码主要在：

- [include/construct.h](../include/construct.h) — `NodeScore`、`Prune`、`PruneLevel`、`CalcScore` 声明
- [src/construct.cc](../src/construct.cc) — 实现

## 1. 这部分代码要解决什么问题

`Finalize()` 之后，语言模型已经有了完整的概率和 backoff 权重。但模型可能太大——节点太多占内存。

`Prune()` 的目标是：在尽可能少损失模型质量的前提下，删掉一部分节点来缩小模型。

## 2. 什么样的节点可以删

一个 n-gram 节点被删掉后，运行时查不到它，就会走 backoff 路径——退到更短的 history 去查。所以删除一个节点的代价就是：用 backoff 替代它带来的概率偏差有多大。

两条规则：

1. **有子节点的不能删**——删了它，它的子节点就没有父亲了，整棵子树断掉
2. **没有子节点的**——可以删，按"删掉它造成多大损失"排序，优先删损失最小的

## 3. 如何衡量损失

对一个节点 `w`，它在 history `h` 下的显式概率是 `Pd(w|h)`。如果删掉它，运行时会走 backoff：

```text
P_backoff(w|h) = bow(h) * Ps(w|h')
```

损失用 KL 散度的贡献来近似：

```text
score(w|h) = Pd(w|h) * (log Pd(w|h) - log P_backoff(w|h))
```

这个值越小，说明 backoff 能很好地替代它，删掉它的影响就越小。

代码中 `CalcScore()` 就是算这个值。

## 4. 整体流程

```text
Prune(reserves)
  1. 记录每层当前大小 → prune_sizes_
  2. 根据 reserves 算出每层要删多少个 → prune_cutoffs_
  3. 从最深层到第 1 层，逐层调用 PruneLevel(level)
  4. 重新计算 bow → CalcBow()
```

### 4.1 reserves 参数

`reserves` 指定每层要**保留**多少个节点。例如 `--prune-reserve "50000 30000 20000"` 表示：

- level 1 保留 50000 个
- level 2 保留 30000 个
- level 3 保留 20000 个

要删的数量 = 当前数量 - 保留数量：

```text
prune_cutoffs_[lvl] = max(0, prune_sizes_[lvl] - 1 - reserve)
```

减 1 是因为 tail 哨兵不算。

### 4.2 为什么从最深层开始

从 trigram → bigram → unigram 的顺序剪。原因是：先删深层的叶子节点，它们没有子节点，删起来简单。删完之后，一些原来有子节点的浅层节点可能变成无子节点，就可以在下一轮被删掉。

## 5. PruneLevel 的细节

`PruneLevel(level)` 对某一层做剪枝：

### 5.1 构建候选列表

遍历该层所有节点（不含 tail），为每个节点算出：

```cpp
struct NodeScore {
    double score;       // 删除造成的损失
    uint32_t index;     // 节点在该层的下标
    bool has_child;     // 是否有子节点
};
```

有子节点的 `score` 设为 0，但后面会跳过不删。

### 5.2 排序和标记

按 `score` 升序排序（`has_child` 的排在最后）。然后取前 `prune_cutoffs_[level]` 个，将它们的 `pr` 设为特殊标记值（非 log 模式下是 1.0，log 模式下是 0.0），跳过 `has_child` 的。

### 5.3 物理删除

调用 `CutLevelByMark()` 把标记值的节点从数组中移除，同时更新父节点的 `child` 下标。

### 5.4 重算 bow

所有层剪完后，统一调用 `CalcBow()` 重新计算 backoff 权重。

注意：**不需要重新 Discount**。因为 Discount 是从频次算概率，剪掉子节点不影响剩余节点的频次和概率。但 bow 的公式依赖"当前 history 下所有 seen children 的概率之和"，删了几个 children 后这个和变了，所以 bow 必须重算。

## 6. CalcScore 的计算

`CalcScore(level, indices, words)` 计算删除 `words[level]` 这个节点造成的信息损失。

### 6.1 核心公式

```text
score = Pd(w|h) * (log Pd(w|h) - log(bow(h) * Ps(w|h')))
```

代码中：

```cpp
double phw_backoff = bow * ph_w;
double score = phw * (std::log(phw) - std::log(phw_backoff));
```

其中：

- `phw` = 该节点的显式概率 `Pd(w|h)`
- `bow` = 父节点的 backoff 权重
- `ph_w` = 低阶概率 `Ps(w|h')`，通过 `GetPr()` 递归查询

### 6.2 缓存机制

同一个父节点下的多个子节点会被连续计算。`CalcScore` 内部缓存了上一次父节点的 `pa`（剩余概率质量）和 `pb`（低阶剩余质量），通过 `prune_cache_*` 成员变量实现。如果父节点没变就复用缓存，避免重复遍历。

## 7. 一个例子

假设 bigram 层有这些节点：

```text
A→B  pr=0.3  score=0.001   (很小，backoff 能很好替代)
A→C  pr=0.5  score=0.1     (较大，backoff 替代效果差)
A→D  pr=0.2  score=0.05    (中等)
```

如果 `prune_cutoffs_[2] = 1`（要删 1 个），排序后：

```text
A→B  score=0.001  ← 最小，删这个
A→D  score=0.05
A→C  score=0.1
```

删掉 A→B 后，A 节点的 bow 重算。运行时查 P(B|A) 时，因为 A→B 不存在了，会走 backoff：

```text
P(B|A) = bow(A) * P(B)
```

因为 score 很小，说明 `bow(A) * P(B)` 本来就接近 `Pd(B|A)`，损失很小。

## 8. 和代码的对应关系

- 入口：`Prune(reserves)`
- 逐层剪枝：`PruneLevel(level)`
- 计算损失：`CalcScore(level, indices, words)`
- 物理删除：`CutLevelByMark()`
- 重算 bow：`CalcBow()`
- 候选结构：`NodeScore`
- 缓存：`prune_cache_level_`/`prune_cache_index_`/`prune_cache_pa_`/`prune_cache_pb_`

## 9. 一句话总结

```text
Prune 的过程：
  对每层从深到浅
    → 给每个无子节点的 n-gram 算 KL 散度损失
    → 按损失排序，删掉损失最小的若干个
    → 物理移除并更新父节点索引
  全部剪完后
    → 重算 backoff 权重
```
