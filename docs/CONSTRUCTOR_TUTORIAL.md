# Constructor 原理与实现

本文整理 `sime-construct` 背后的语言模型公式、`Constructor` 的构建流程，以及 `raw.slm` 的二进制格式。对应代码主要在：

- [include/construct.h](../include/construct.h) — `ConstructOptions`、`Constructor` 声明
- [src/construct.cc](../src/construct.cc) — 实现
- [app/constructor.cc](../app/constructor.cc) — CLI 入口

## 1. 这部分代码要解决什么问题

`sime-count` 的输出是一个按字典序排列的 n-gram 计数文件。对 trigram 来说，每条记录形如：

```text
[w1, w2, w3, freq]
```

其中：

- `w1, w2, w3` 是词的 `TokenID`
- `freq` 是该 trigram 在语料中出现的次数

`Constructor` 要把这些计数转成一个支持 back-off 的 n-gram 语言模型，并写成中间文件 `raw.slm`。后续再由 `sime-compact` 压缩成运行时使用的 `lm.t3g`。

## 2. 语言模型的核心公式

### 2.1 最大似然概率

对 history `h` 和候选词 `w`：

```text
Pml(w|h) = C(h,w) / C(h)
```

其中：

- `C(h,w)` 是 n-gram `(h,w)` 的出现次数
- `C(h)` 是 history `h` 的出现次数

对 trigram：

```text
Pml(w3 | w1,w2) = C(w1,w2,w3) / C(w1,w2)
```

### 2.2 discount 后的显式概率

直接用最大似然会把 seen n-gram 的概率质量占满，导致 unseen n-gram 概率为 0。为此需要先对 seen 事件做 discount：

```text
Pd(w|h) = D(C(h,w)) / C(h),    when C(h,w) > 0
```

这里：

- `D(.)` 是折扣函数
- `Pd` 是 seen n-gram 的显式概率

当前仓库保留两种 discount：

Absolute Discounting:

```text
D(r) = r - c
Pd(w|h) = (r - c) / C(h)
```

Linear Discounting:

```text
D(r) = d * r
Pd(w|h) = d * r / C(h)
```

### 2.3 back-off 公式

令：

- `h` 是当前 history
- `h'` 是将 `h` 去掉最左边一个词后的更短 history

例如对 trigram：

```text
h  = (A,B)
h' = (B)
```

那么最终平滑后的概率为：

```text
Ps(w|h) =
    Pd(w|h),                    if C(h,w) > 0
    bow(h) * Ps(w|h'),         if C(h,w) = 0 and h exists
    Ps(w|h'),                  if h itself does not exist
```

含义是：

- 高阶 seen n-gram：直接用显式概率 `Pd`
- 高阶 unseen n-gram：乘回退权重，退到低阶模型
- 当前 history 本身不存在：直接用更低阶 history

### 2.4 back-off weight

记：

```text
Seen(h) = { w : C(h,w) > 0 }
```

则：

```text
bow(h) =
    (1 - Σ[w∈Seen(h)] Pd(w|h))
    /
    (1 - Σ[w∈Seen(h)] Ps(w|h'))
```

解释：

- 分子是当前 history 在 seen 项上用掉概率之后，剩给 unseen 项的质量
- 分母是低阶模型在这些 unseen 项上原本拥有的质量
- 两者相除，就得到回退时对低阶概率的缩放系数

这个公式看起来像递归，但没有循环依赖问题，因为：

- `bow(h)` 依赖的是更低阶的 `Ps(w|h')`
- 不是依赖同一层未定义的 `Ps(w|h)`

因此模型是按阶数从低到高逐层定义的。

## 3. level 是什么

对 trigram 模型，`Constructor` 内部是按层组织的：

- `node_levels_[0]`：pseudo root
- `node_levels_[1]`：unigram 层
- `node_levels_[2]`：bigram 层
- `leaves_`：trigram 叶子层

一个 trigram `(A,B,C)` 在结构上表示为：

```text
root
└── A
    └── B
        └── C
```

其中：

- `A` 节点的 `pr` 表示 `P(A)`
- `B` 节点的 `pr` 表示 `P(B|A)`
- `C` 叶子的 `pr` 表示 `P(C|A,B)`

所以 `level` 表示的是 n-gram 的阶层，不同层的统计分布不同，因此 discount 也要按层分别计算。

## 4. `TokenID` 是什么

`TokenID` 始终表示一个词的 ID，而不是某个 unigram/bigram/trigram 的“整体 ID”。

例如词表：

```text
1 -> A
2 -> B
3 -> C
4 -> D
```

对于 trigram `(A,B,C)`：

- level 1 上的节点保存 `id = 1`
- level 2 上的节点保存 `id = 2`
- leaf 上保存 `id = 3`

因此：

- gram 的阶数由“所在层”决定
- 完整 n-gram 由“从 root 到该节点的路径”决定
- `TokenID` 只表示当前这个位置上的词是谁

## 5. Constructor 的构建流程

### 5.1 读取有序 n-gram 计数

入口是 `RunConstruct()`。它从输入文件反复读取：

- `num` 个 `TokenID`
- 一个 `uint32_t freq`

然后调用：

```cpp
builder.InsertItem(ids, freq);
```

### 5.2 `InsertItem()`：建多层结构

`InsertItem()` 假设输入已经按字典序排序，这样同一前缀的 n-gram 会连续出现。构建过程不是任意插入树，而是顺序追加：

- 前缀不变时，复用上一条路径上的节点并累加频次
- 前缀变化时，新建节点

同时它还处理句边界 token（`IsBreaker`）：遇到句边界时截断当前 n-gram，不跨句统计。

### 5.3 `CountCnt()`：统计 count-of-counts

构建结束后，先统计每层的 `nr_`：

```text
nr_[level][r]
```

表示该层频次为 `r` 的项的统计量。它是 discount 参数估计所需要的基础数据。

### 5.4 `AppendTails()`：补哨兵尾节点

每一层最后都追加一个 tail 节点。原因是当前结构里只保存“孩子区间起点”，而不保存终点：

```text
children(node) = [node.child, next_sibling.child)
```

因此必须有一个“下一个兄弟”，最后一个真实节点才能知道自己的孩子区间终点。

### 5.5 `Cut()`：按频次裁剪

`cutoffs_` 用于按频次去掉低频项，控制模型规模。裁剪时同时更新父节点的 `child` 下标，保持结构连续。

### 5.6 `Discount()`：把频次变成显式概率

对每一层：

1. 用该层的 `nr_` 初始化对应的 discount 参数
2. 对每个父节点的孩子区间做 discount

其核心公式在代码里就是：

```text
Pd(w|h) = D(C(h,w)) / C(h)
```

### 5.7 `CalcBow()`：计算回退权重

对每个非叶子节点：

1. 遍历所有 seen children，累加 `Pd(w|h)`
2. 对这些同样的 children，查询低阶概率 `Ps(w|h')`
3. 套用：

```text
bow(h) =
    (1 - Σ_seen Pd(w|h))
    /
    (1 - Σ_seen Ps(w|h'))
```

### 5.8 `Prune()`：可选熵剪枝

如果指定了 `--prune-reserve`，`Constructor` 会执行基于损失近似的剪枝：

- 优先删除没有子节点的低价值显式项
- 删除后重新计算 `bow`

## 6. 一个最小 trigram 例子

假设词表：

```text
1 -> A
2 -> B
3 -> C
4 -> D
```

我们只保留：

```text
P(A)
P(B|A)
P(C|A,B)
P(D|A,B)
```

逻辑树是：

```text
root
└── A
    └── B
        ├── C
        └── D
```

按层数组存储，并加上 tail 之后，大致是：

`level_0`:

```text
idx 0: root
idx 1: tail0
```

`level_1`:

```text
idx 0: A
idx 1: tail1
```

`level_2`:

```text
idx 0: B
idx 1: tail2
```

`leaves_`:

```text
idx 0: C
idx 1: D
idx 2: tail3
```

此时：

- `root.child = 0`
- `tail0.child = 1`

所以 root 的孩子区间是：

```text
[0, 1) -> {A}
```

同理：

- `A.child = 0`
- `tail1.child = 1`

所以 A 的孩子区间是：

```text
[0, 1) -> {B}
```

再看：

- `B.child = 0`
- `tail2.child = 2`

所以 B 的孩子区间是：

```text
[0, 2) -> {C, D}
```

这就是 `child` 的含义：

它不是“指向某个单独孩子”，而是“指向我在下一层孩子数组中的起始位置”。

## 7. `raw.slm` 的二进制格式

`Constructor` 最终输出的是 `raw.slm`，写入逻辑在 `Constructor::Write()`。

### 7.1 文件整体布局

```text
[order]
[flag]
[size_0][size_1]...[size_order]
[level_0 DiskNode array]
[level_1 DiskNode array]
...
[level_{order-1} DiskNode array]
[level_order DiskLeave array]
```

### 7.2 头部字段

#### `order`

模型阶数。例如：

- `1` -> unigram
- `2` -> bigram
- `3` -> trigram

#### `flag`

概率存储方式：

- `0`：存普通概率值
- `1`：存 `-log(p)`

#### `size_0 ... size_order`

每一层的元素个数：

- 前 `order` 个是 `DiskNode` 层大小
- 最后一个是 `DiskLeave` 层大小

注意：

- 这些大小包含 tail 节点

### 7.3 磁盘结构体

```cpp
struct DiskNode {
    TokenID id = 0;
    float pr = 0.0f;
    std::uint32_t child = 0;
    float bow = 0.0f;
};

struct DiskLeave {
    TokenID id = 0;
    float pr = 0.0f;
};
```

语义如下：

#### `DiskNode`

- `id`：当前词的 `TokenID`
- `pr`：显式概率
- `child`：下一层孩子区间起始下标
- `bow`：该 history 的回退权重

#### `DiskLeave`

- `id`：当前词的 `TokenID`
- `pr`：显式概率

最高阶 n-gram 的最后一个词存在 leaf 层。

### 7.4 trigram 文件布局示例

对 trigram，文件可以视为：

```text
offset 0:  int32 order
offset 4:  uint32 flag
offset 8:  uint32 size_0
offset 12: uint32 size_1
offset 16: uint32 size_2
offset 20: uint32 size_3

offset 24: size_0 * DiskNode
offset ... size_1 * DiskNode
offset ... size_2 * DiskNode
offset ... size_3 * DiskLeave
```

因此 trigram 的头部长度固定为：

```text
8 + 4 * 4 = 24 bytes
```

## 8. `raw.slm` 和 `lm.t3g` 的关系

`raw.slm` 是训练输出的中间格式：

- 已经完成了 discount 和 bow 计算
- 结构清晰，适合进一步压缩
- 使用 float 保存概率和回退权重

后续 `sime-compact` 会把它压缩成 `lm.t3g`：

- 概率做量化压缩
- 预计算 threaded backoff state
- 变成更适合运行时查询的格式

所以：

- `raw.slm`：训练中间产物
- `lm.t3g`：运行时使用格式

## 9. 和代码的对应关系

- 读取 n-gram 输入：`RunConstruct()`
- 构建层级结构：`InsertItem()`
- 统计 `nr_`：`CountCnt()`
- 补 tail：`AppendTails()`
- cutoff：`Cut()`
- discount：`Discount()` / `DiscountLevel()`
- 计算 backoff：`CalcBow()` / `CalcNodeBow()`
- 回退查询：`GetPr()`
- 写出 `raw.slm`：`Write()`

## 10. 一句话总结

`Constructor` 干的事情可以概括为：

```text
排序后的 n-gram 计数
    -> 分层前缀结构
    -> discount 得到显式概率
    -> 计算 bow 得到 back-off 模型
    -> 写成 raw.slm
```

而 `raw.slm` 的本质是：

```text
文件头 + 各层大小 + 分层 DiskNode 数组 + 最后一层 DiskLeave 数组
```

完整的 n-gram 不是通过单独编号表示，而是通过“层级 + 路径 + TokenID”共同确定。
