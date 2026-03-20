# Compact 原理与实现

本文整理 `sime-compact` 的压缩流程：如何把 `raw.slm` 转成运行时使用的 `lm.t3g`。对应代码主要在：

- [include/compact.h](../include/compact.h) — `CompactOptions`、`CompactRun` 声明
- [src/compact.cc](../src/compact.cc) — 实现

## 1. 这部分代码要解决什么问题

`sime-construct` 输出的 `raw.slm` 使用 `float` 保存概率和 backoff 权重，每个非叶子节点 16 字节（`RawNode`），每个叶子 8 字节（`RawLeaf`）。对于 8 万词汇量的 trigram 模型，这个文件会很大。

`sime-compact` 的目标是：

1. **量化压缩**——把 `float` 概率和 bow 压缩成查找表索引（pr 16 bit，bow 14 bit）
2. **预计算 backoff 指针**——运行时做 backoff 查询时不用逐层搜索，直接跳到目标位置
3. **位打包**——把所有字段紧凑地打包到固定宽度的结构体中

最终产出 `lm.t3g`，体积约为 `raw.slm` 的 40%-50%，查询也更快。

## 2. 整体流程

```text
CompactRun(options)
  1. Load()        — 读取 raw.slm
  2. BuildLinks()  — 为每个节点建立父节点索引
  3. BuildTables() — 值量化，生成 pr/bow 查找表
  4. BuildThreaded() — 计算 backoff 指针，位打包
  5. WriteModel()  — 写出 lm.t3g
```

## 3. Load：读取 raw.slm

`SimpleSlm::Load()` 按 `raw.slm` 的二进制格式直接 `fread`：

```text
[order][flag][size_0..size_order][level_0 RawNode]...[level_{order-1} RawNode][leaves RawLeaf]
```

其中：

```cpp
struct RawNode {
    TokenID id = 0;
    float pr = 0.0f;
    uint32_t child = 0;
    float bow = 0.0f;
};

struct RawLeaf {
    TokenID id = 0;
    float pr = 0.0f;
};
```

内存布局和磁盘布局完全一致，所以可以直接按结构体数组一次读入。

## 4. BuildLinks：建立父节点索引

`raw.slm` 中每个节点只有 `child`（指向下一层孩子起始位置），没有反向指针。但后续计算 backoff 指针时需要从任意节点向上回溯到 root，所以 `BuildLinks()` 为每一层建立 `parent_links_`：

```text
parent_links_[level][index] = 该节点在上一层的父节点下标
```

算法很简单：对于每一层，顺序遍历节点，同时维护一个递增的 parent 指针。因为节点和孩子区间都是按序排列的，所以 parent 指针只需要向前推进，不需要回退。

## 5. BuildTables：值量化

### 5.1 为什么需要量化

`raw.slm` 中每个概率和 bow 都是一个 `float`（4 字节）。但实际上：

- 很多不同的 n-gram 拥有相同或相近的概率值
- 精确保存每个 float 是浪费

量化的思路是：用一个查找表保存所有可能的概率值，每个节点只保存一个索引。

### 5.2 查找表大小

```text
pr 查找表：2^16 = 65536 个 float 条目（索引用 16 bit）
bow 查找表：2^14 = 16384 个 float 条目（索引用 14 bit）
```

bow 用 14 bit 而不是 16 bit 的原因是：bow 的取值多样性远低于 pr。bow 本质上是一个缩放系数，通常接近 1.0，而 pr 的取值范围从接近 0 到接近 1 分布广泛。14 bit 已经足够表示 bow 的精度需求。

### 5.3 量化算法：基于堆的聚类

`CompressValues()` 实现了一个贪心聚类算法：

1. **初始化**：每个 distinct 值作为一个独立的桶，放入最小堆（按相邻桶距离排序）
2. **合并**：反复取出距离最小的桶，与其相邻桶合并。合并后新桶的代表值取加权平均（按频次加权）
3. **终止**：桶的数量降到 limit（如 65536）时停止
4. **排序**：合并结束后，用最大堆按值排序，确定每个原始值对应的查找表索引

这个过程的核心思想是：把最接近的值合并在一起，用它们的加权平均来代表，损失最小。

### 5.4 有效值转换

量化前，概率和 bow 会先转换成"有效值"（effective value），使得在有效值空间中做等距量化效果更好：

- **pr**：转成 log2 空间。`EffectivePr(value) = -log2(value)`（非 log 模式）或 `value / ln(2)`（log 模式）
- **bow**：直接使用原始值。`EffectiveBow(value) = value`（非 log 模式）或 `exp(-value)`（log 模式）

量化完成后再转回原始空间，存入查找表。

### 5.5 里程碑值

除了实际出现的值之外，还会往量化池中插入一些"里程碑"值（milestones），比如 1/2、1/4、1/8 等常见概率值。这些里程碑的 count 为 0（不影响加权平均），但保证查找表中有这些关键值的精确表示。

## 6. BuildThreaded：backoff 指针和位打包

### 6.1 backoff 指针（bol/bon）

在运行时，如果查不到高阶 n-gram，需要退到低阶模型查询。例如对 trigram P(C|A,B)，如果 (A,B,C) 不存在，需要走 backoff：

```text
P(C|A,B) = bow(A,B) * P(C|B)
```

这时需要找到 bigram 节点 B 的位置。如果 B 也不存在或者 B 没有子节点，还要继续退到 unigram。

**不预计算的做法**：每次 backoff 都要从头搜索，逐层做二分查找。

**预计算的做法**：在压缩阶段，对每个节点提前算好 backoff 目标，存成两个字段：

- `bol`（backoff level）：backoff 到第几层。2 bit，值为 0-3
- `bon`（backoff node）：backoff 到该层的第几个节点。23 bit

例如对 bigram 节点 (A,B)，它的 backoff 目标是 unigram 节点 B：

```text
bol = 1（unigram 层）
bon = B 在 unigram 层的下标
```

但 backoff 目标不一定总是上一层——如果上一层的对应节点没有子节点（不能作为 history），就要继续退到更浅层。

### 6.2 FindBackoffState 的计算

对一个节点的 history `(w1, w2, ..., wn)`，backoff state 的查找过程是：

```text
依次尝试：
  (w2, ..., wn)   — 去掉最左边的词
  (w3, ..., wn)   — 再去掉一个
  ...
  (wn)            — 只剩最后一个词

对每个候选 history：
  1. 在模型中查找该 history 对应的节点
  2. 如果找到了且该节点有子节点 → 这就是 backoff 目标
  3. 如果找不到或无子节点 → 继续缩短

最坏情况：退到 root（bol=0, bon=0）
```

### 6.3 位打包

每个节点被打包成固定宽度的位域：

**PackedNode**（96 bit = 12 字节，用于 level 0 到 level order-1）：

```text
word0 (32 bit):
  [0:17]   id        — 18 bit，TokenID
  [18:31]  bow_index — 14 bit，bow 查找表索引

word1 (32 bit):
  [0:15]   pr_index  — 16 bit，pr 查找表索引
  [16:31]  child_lo  — 16 bit，child 索引低 16 位

word2 (32 bit):
  [0:22]   bon       — 23 bit，backoff 目标节点
  [23:24]  bol       — 2 bit，backoff 目标层
  [25:31]  child_hi  — 7 bit，child 索引高 7 位
```

child 索引一共 23 bit（16 + 7），可以支持最多 8M 个节点。

**PackedLeaf**（64 bit = 8 字节，用于最高阶层）：

```text
word0 (32 bit):
  [0:17]   id        — 18 bit，TokenID
  [18:31]  pr_lo     — 14 bit，pr 查找表索引低 14 位

word1 (32 bit):
  [0:22]   bon       — 23 bit，backoff 目标节点
  [23:24]  bol       — 2 bit，backoff 目标层
  [25:26]  pr_hi     — 2 bit，pr 查找表索引高 2 位
```

叶子节点没有 `child` 和 `bow_index`（最高阶不需要指向更深层，也不需要 backoff 权重）。pr 索引拆成 14 + 2 = 16 bit。

### 6.4 为什么 id 只用 18 bit

18 bit 可以表示最多 262144 个不同的 TokenID，对于中文输入法的词汇量（通常 8-10 万词）足够了。

## 7. WriteModel：写出 lm.t3g

### 7.1 文件整体布局

```text
[order]                    — int32，模型阶数
[flag]                     — uint32，0=普通概率 / 1=-log(p)
[size_0..size_order]       — 每层大小（包含 tail 哨兵）

[pr_table]                 — 65536 个 float（pr 查找表）
[bow_table]                — 16384 个 float（bow 查找表）

[level_0 PackedNode array]
[level_1 PackedNode array]
...
[level_{order-1} PackedNode array]
[leaves PackedLeaf array]
```

### 7.2 trigram 的具体大小

对 trigram 模型：

```text
头部：8 + 4 * 4 = 24 字节
pr 查找表：65536 * 4 = 256 KB
bow 查找表：16384 * 4 = 64 KB
节点层：各层节点数 * 12 字节
叶子层：叶子数 * 8 字节
```

其中查找表是固定大小，即使实际使用的条目不满也会补零到满。

### 7.3 和 raw.slm 的对比

| | raw.slm | lm.t3g |
|---|---------|--------|
| 非叶子节点 | 16 字节 (RawNode) | 12 字节 (PackedNode) |
| 叶子节点 | 8 字节 (RawLeaf) | 8 字节 (PackedLeaf) |
| 概率存储 | 直接 float | 查找表索引 |
| bow 存储 | 直接 float | 查找表索引 |
| backoff | 运行时搜索 | 预计算 bol/bon |
| 额外开销 | 无 | 320 KB 查找表 |

虽然叶子节点大小相同，但非叶子节点从 16 字节缩到 12 字节（节省 25%），加上叶子通常占多数，整体压缩效果显著。

## 8. 一个例子

假设有 unigram 节点 A，pr = 0.001234。

**raw.slm 中**：直接存 float `0.001234`，占 4 字节。

**lm.t3g 中**：
1. `EffectivePr(0.001234)` = `−log2(0.001234)` ≈ 9.66
2. 量化过程中，9.66 和附近的值被归入同一个桶，桶的代表值假设是 9.65
3. 桶的排序位置假设是第 42351 个 → `pr_index = 42351`
4. 查找表第 42351 项存 `OriginalPr(9.65)` ≈ 0.001243
5. 节点中只存 16 bit 的 `42351`

运行时查 P(A) 时，取出 `pr_index = 42351`，查表得到 `0.001243`。和原始值 `0.001234` 有微小差异，但对语言模型质量的影响可以忽略。

## 9. 和代码的对应关系

- 入口：`CompactRun()`
- 读取 raw.slm：`SimpleSlm::Load()`
- 建立父节点索引：`SimpleSlm::BuildLinks()`
- 值量化：`BuildTables()` → `CompressValues()` / `CompressWithReverse()`
- 有效值转换：`EffectivePr()` / `OriginalPr()` / `EffectiveBow()` / `OriginalBow()`
- Backoff 指针：`FindBackoffState()` / `FindState()` / `HasChildren()`
- 位打包：`DoPackNode()` / `DoPackLeaf()`
- 写出 lm.t3g：`WriteModel()`
- 堆操作：`BubbleUp()` / `SiftDown()`
- 里程碑值：`AddMilestones()`

## 10. 一句话总结

```text
Compact 的过程：
  读取 raw.slm（float 概率 + bow）
    → 建立父节点反向索引
    → 把 pr 量化成 16 bit 查找表索引
    → 把 bow 量化成 14 bit 查找表索引
    → 对每个节点预计算 backoff 指针（bol/bon）
    → 把所有字段位打包成 PackedNode / PackedLeaf
    → 写出 lm.t3g（查找表 + 打包节点）
```
