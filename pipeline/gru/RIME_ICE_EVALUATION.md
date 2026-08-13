# 雾凇拼音 + 万象语言模型与 Sime GRU 对比评测

日期：2026-08-13

## 评测对象

本次评测对比：

- Sime `v2026.08.13` + GRU；
- 最新版雾凇拼音 `rime-ice` + 万象 LTS 语言模型。

雾凇配置参考《Fcitx 最佳配置实践》中的方案，并使用当前官方替代模型：

- 雾凇 commit：`569ff3bc65dd4aec0a26b33c49c8bbdfa8b5fd57`；
- 语言模型：`wanxiang-lts-zh-hans.gram`；
- `collocation_min_length: 2`；
- `collocation_max_length: 5`；
- `translator/contextual_suggestions: true`；
- `translator/max_homophones: 7`；
- `translator/max_homographs: 7`。

文章中原先使用的 `amz-v2n3m1-zh-hans.gram` 已被官方 LTS 模型替代，目前雾凇
官方 grammar recipe 同样使用 `wanxiang-lts-zh-hans.gram`。

模型校验信息：

```text
size:   420251692 bytes
sha256: 90d2385f65337f8b8c7b1ba5cbe874df3f2d91b462d68fa2f9fe90c57aa3bc66
```

## 评测口径

- 使用相同的三个全拼测试集合，共 5,354 条；
- 统计 Top-1 整句准确率和编辑距离字准确率；
- 使用全新隔离用户目录；
- 不加载个人词库和历史调频数据；
- 不提交候选，避免评测过程产生在线学习；
- 关闭预测功能；
- 使用完整且 SHA-256 校验通过的万象 LTS 模型。

隔离评测环境中的 OpenCC Emoji/繁简过滤器存在 ABI 崩溃，因此评测时只关闭了这两个
输出格式过滤器。测试集目标均为简体中文，这两个过滤器不参与拼音解码或语言模型分数
计算。

## 句准确率

| 评测集 | 雾凇 + 万象 LM | Sime GRU | 差值（Sime − 雾凇） |
|---|---:|---:|---:|
| case.1 | 62.13%（1242/1999） | 75.39%（1507/1999） | **+13.26 pp** |
| case.2 | 44.80%（896/2000） | 46.75%（935/2000） | **+1.95 pp** |
| inbox | 76.31%（1034/1355） | 75.57%（1024/1355） | **−0.74 pp** |
| **全拼整体** | **59.25%（3172/5354）** | **64.74%（3466/5354）** | **+5.49 pp** |

## 字准确率

| 评测集 | 雾凇 + 万象 LM | Sime GRU | 差值（Sime − 雾凇） |
|---|---:|---:|---:|
| case.1 | 89.45% | 94.56% | **+5.12 pp** |
| case.2 | 85.03% | 87.07% | **+2.03 pp** |
| inbox | 94.71% | 94.78% | **+0.07 pp** |
| **全拼整体** | **89.08%** | **91.89%** | **+2.81 pp** |

`pp` 表示百分点（percentage points）。

## 结论

在三个全拼集合上，Sime GRU 的整体句准确率高出 5.49 个百分点，字准确率高出
2.81 个百分点。Sime 在 `case.1` 正式长句集合上的优势最大；雾凇在 inbox 句准确率
上领先 10 句，但两者字准确率基本持平。

这说明在当前评测分布下，约 8.64 MiB 的 Sime GRU 排序模型配合原有 n-gram 模型，
整体准确率高于雾凇词库配合约 400.8 MiB 万象 LTS 语言模型的配置。


## 参考

- [雾凇拼音](https://github.com/iDvel/rime-ice)
- [万象语言模型](https://github.com/amzxyz/RIME-LMDG/releases/tag/LTS)
- [Fcitx 最佳配置实践](https://manateelazycat.github.io/2026/03/17/fcitx-best-config/#%E5%AE%89%E8%A3%85%E9%9B%BE%E5%87%87%E6%8B%BC%E9%9F%B3)
