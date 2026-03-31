# 可借鉴 KenLM 的优化点

## 1. State 零 backoff 截断

KenLM 在查询 `P(w | context)` 时，如果上下文中某个词的 backoff 权重为 0，就从 state 中丢弃该词，因为它对后续查询没有影响。

标记方式：利用浮点数符号位，+0.0 表示上下文会继续扩展，-0.0 表示不会扩展，几乎零开销。

收益：beam search 时 state 更短，更多路径可以合并（hypothesis recombination），减少搜索空间，加速解码。

对应文件：`score.cc`、`interpret.cc`

## 2. 单次遍历查询

SRILM 查一个 n-gram 需要遍历 trie 两次（一次查概率，一次查 backoff）。KenLM 在同一次 trie 遍历中同时取出概率和 backoff 值，省掉一次完整的树查找。

对应文件：`score.cc`

## 3. lmplz 磁盘排序 n-gram 计数

KenLM 的 `lmplz` 支持指定内存上限，超出后自动做磁盘归并排序。用这个方法在单台机器上处理过 9750 亿 token 的语料。

当前 `sime-count` 已经用了外部排序，但如果以后要扩大训练规模（数十 GB 级语料），可以参考 lmplz 的内存控制策略：固定大小的内存块 + 多路归并。

对应文件：`count.cc`

参考：https://kheafield.com/code/kenlm/estimation/

## 4. 量化压缩的细节差异

KenLM 的量化方案：

- `-q N` 控制概率量化位数，`-b N` 控制 backoff 量化位数，可独立设置
- 每个 n-gram 阶数单独计算量化桶（聚类分桶）
- Unigram 概率不量化，只对 bigram 及以上量化
- Trie 结构配合 bit-level packing，word index / pointer 只用最少 bit 数
- Bhiksha 指针压缩（`-a N`）：用偏移数组进一步压缩 pointer 字段

可以对比 `sime-compact` 的实现，看是否有进一步压缩的空间。

对应文件：`compact.cc`

参考：https://kheafield.com/code/kenlm/structures/

## 5. Cache LM

输入法场景下用户经常重复打同样的词。维护一个小的最近上屏历史的 cache 模型，和主模型做插值：

```
score = λ × main_model_score + (1-λ) × cache_model_score
```

cache 模型实现很简单：一个衰减的 unigram/bigram 计数器，用户每上屏一个词就更新计数，时间越久权重越低。

λ 可以固定（如 0.9），也可以根据 cache 命中率动态调整。

对体验提升的性价比最高。

## 6. 整句拼音纠错

用户打错拼音（如 `niaho` → `nihao`）时的容错处理。

方案：在音节 trie 上做编辑距离 1-2 的模糊搜索，为纠错结果施加惩罚分数（如 log(0.01) ≈ -4.6），让正确拼写优先但错误拼写也能出候选。

对应文件：`trie.cc`、`interpret.cc`

## 参考资料

- KenLM 论文：https://aclanthology.org/W11-2123.pdf
- KenLM 数据结构：https://kheafield.com/code/kenlm/structures/
- KenLM 训练（lmplz）：https://kheafield.com/code/kenlm/estimation/
- KenLM 源码：https://github.com/kpu/kenlm
