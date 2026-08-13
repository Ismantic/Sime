# Sime GRU 排序模型训练与部署

日期：2026-08-13
对应版本：Sime `v2026.08.13`、SimeApp `v0.16.0`

## 1. 目标与结论

本轮工作的目标是在保留 Sime 原有词典和 n-gram 语言模型的前提下，提高全拼和
T9 的首选整句准确率，同时把 Android 端新增延迟控制在可接受范围内。

最终方案是在 Sime 解码得到的前 10 个候选上增加一个小型 GRU 排序器：

```text
输入拼音或 T9 数字
        ↓
Sime 字典 + n-gram 解码，生成 Top 10
        ↓
候选词 ID → 32 维预训练 Embedding
        ↓
双向 GRU → 候选残差分数
        ↓
最终分数 = Sime 分数 + scale × GRU 分数
        ↓
重新排序 Top 10
```

原有 `sime.dict` 和 `sime.cnt` 的格式及解码逻辑不变。GRU 文件缺失、加载失败或
构建时未启用 ncnn 时，程序自动退回原始 Sime 排序。

## 2. 训练数据

### 2.1 Embedding 语料

Embedding 使用训练 `sime.cnt` 的全量分词语料 `sentences.cut.txt`，规模约
15.9 亿词。训练工具采用原生 fastText，而不是自行实现的简化版 word2vec：

- 模式：skip-gram；
- 维度：32；
- context window：2；
- negative samples：5；
- epoch：1；
- character n-gram：2～4；
- 覆盖 Sime 的全部 262,094 个 token。

使用大规模同源语料预训练 Embedding，能先学习词之间的语义和搭配关系；GRU
训练时不必从有限的排序样本中重新学习完整词义。

### 2.2 GRU 排序语料

排序训练数据由两部分混合：

1. 约 75,000 条通用候选数据，用于维持新闻、正式文本和 inbox 集合上的泛化；
2. OpenIME TouchPal 口语语料，共 537,639 行，用于补足 Sime 原始语料中不足的
   对话、评论和日常表达。

为了避免评测泄漏，训练前移除了与 `case.2` 完全一致的 `(pinyin, gold)` 样本。
使用 Sime 生成候选后，得到约 98,904 个全拼 hard case 和 79,336 个 T9 hard
case。只有正确答案仍在候选列表中的样本，才能直接教会排序器把它提升到首位；正确
答案未进入 Top 10 的错误属于召回问题，不能由本排序器解决。

## 3. 模型结构

全拼和 T9 使用独立模型，二者共享同一份 Embedding：

- 输入：候选句对应的 Sime token ID 序列；
- Embedding：32 维，来自全量语料 fastText；
- 编码器：单层双向 GRU；
- hidden size：每个方向 64；
- 输出层：拼接正向末状态与反向首状态，线性映射为一个标量；
- 候选数：Top 10；
- 全拼残差系数：`0.60`；
- T9 残差系数：`0.72`。

全拼和 T9 的候选混淆分布差异很大，因此最终没有强行共用同一个 GRU。共享
Embedding 保持模型紧凑，独立 GRU 则允许两种模式学习不同的排序偏好。

## 4. 训练目标

对同一输入产生的一组候选，模型不是单独判断每句话是否正确，而是在候选组内部学习
相对顺序。训练时将 GRU 输出作为 Sime 原始分数的残差：

```text
predicted_score(candidate) = sime_score(candidate) + gru_score(candidate)
```

然后对组内分数计算 softmax/listwise loss。训练信号以语料真值为主，并在早期实验中
使用 Qwen 候选偏好作为软教师信号。最终结果表明，大规模同源 Embedding、真实口语
hard case 和按输入模式分别训练，比单纯扩大教师模型或直接修改 n-gram 概率更有效。

训练使用 PyTorch、AdamW、梯度裁剪，并按 epoch 在六个固定评测集上检查句准确率和
字准确率。模型选择同时考虑整体收益和单集合回退，不只针对 `case.2` 调参。

最终采用的检查点来源为：

- 全拼：`bidir-touchpal537k-mixed-pinyin-h64.pt.epoch8`；
- T9：`bidir-touchpal537k-mixed-t9-h64.pt.epoch7`，随后进行平衡微调和 scale 扫描。

## 5. 导出与量化

部署阶段将 PyTorch 模型导出为 pnnx/ncnn 图：

- Embedding 按行量化为 INT8；
- 每一行保存一个 FP16 scale，运行时恢复为 FP32；
- GRU 和输出层导出为 ncnn 模型；
- 两种输入模式共享量化 Embedding；
- ncnn 单线程执行，Android 使用 ARM/NEON CPU 优化，不启用 Vulkan。

发布文件如下：

| 文件 | 大小（字节） | 用途 |
|---|---:|---|
| `gru.embedding.i8` | 8,912,946 | 共享 INT8 Embedding 与 FP16 scales |
| `gru.pinyin.ncnn.param` | 732 | 全拼网络结构 |
| `gru.pinyin.ncnn.bin` | 75,028 | 全拼网络权重 |
| `gru.t9.ncnn.param` | 732 | T9 网络结构 |
| `gru.t9.ncnn.bin` | 75,028 | T9 网络权重 |
| **合计** | **9,064,466（8.64 MiB）** | |

Embedding 的 SHA-256：

```text
26b6b9f5750fdefcf2a77fdb3177f739d9651702a8c48d1b1fc93ebdd0de1147
```

## 6. 运行时集成

模型文件与 `sime.cnt` 放在同一目录。Sime 初始化时加载五个 GRU 文件，解码完成后
只对前 10 个候选推理并重新排序。相关实现位于：

- `src/gru_reranker.h`；
- `src/gru_reranker.cc`；
- `src/sime.cc`；
- `cmake/Ncnn.cmake`。

桌面和 AUR 使用系统 ncnn；Android 静态链接 CPU-only ncnn。当前方案没有使用手机
GPU，因为模型很小，Vulkan 的提交、同步和数据搬运成本很可能高于计算收益。

## 7. 最终评测结果

评测均关闭个人词库和在线学习，使用实际导出的 ncnn 模型，不是 PyTorch 模拟结果。

| 评测集 | 模式 | 句准确率：基线 → GRU | 绝对提升 | 字准确率：基线 → GRU | 绝对提升 |
|---|---|---:|---:|---:|---:|
| case.1 | 全拼 | 72.99% → 75.39% | +2.40 pp | 94.08% → 94.56% | +0.49 pp |
| case.2 | 全拼 | 38.40% → 46.75% | +8.35 pp | 83.73% → 87.07% | +3.34 pp |
| inbox | 全拼 | 71.44% → 75.57% | +4.13 pp | 93.57% → 94.78% | +1.21 pp |
| case.num.1 | T9 | 62.76% → 65.06% | +2.30 pp | 88.99% → 89.59% | +0.60 pp |
| case.num.2 | T9 | 22.57% → 29.50% | +6.93 pp | 69.27% → 74.66% | +5.40 pp |
| num.inbox | T9 | 51.44% → 57.86% | +6.42 pp | 84.14% → 86.98% | +2.84 pp |
| **六集合整体** | **—** | **52.34% → 57.41%** | **+5.07 pp** | **85.47% → 87.75%** | **+2.28 pp** |

整体句准确率从 `5562/10627` 提升到 `6101/10627`，增加 539 个完全正确的句子；
字准确率从 85.4706% 提升到 87.7516%。收益主要集中在口语化集合和 T9。

## 8. 复现入口

实验代码和数据索引保存在未发布的 `next/` 工作区，主要脚本包括：

- `next/scripts/prepare_openime_touchpal.py`：清洗口语语料并去除评测重合；
- `next/scripts/export_distill_candidates.py`：导出全拼/T9 N-best 候选；
- `next/scripts/train_tiny_teacher.py`：训练 listwise GRU 排序器；
- `next/scripts/eval_tiny_teacher.py`：PyTorch 评测和 scale 扫描；
- `next/scripts/export_tiny_teacher_int8_ncnn.py`：量化 Embedding 并导出 ncnn；
- `next/scripts/eval_tiny_teacher_ncnn.py`：验证实际 ncnn 产物；
- `next/scripts/benchmark_tiny_teacher_ncnn.py`：测量排序器延迟。

历史脚本仍保留 `tiny_teacher` 等实验阶段名称；正式产品、模型和文档统一称为
**GRU 排序模型**。

## 9. 本轮决策

本轮已经完成训练、ncnn 导出、C++/Android 集成、六集合评测、Android 实机验证、
Release 和 AUR 发布。继续增大 GRU 或只做小范围调参的边际收益已经明显下降。

因此本轮 GRU 准确率改进正式关闭。后续若重新开启，应作为独立课题处理，优先研究：

1. 提升 Sime Top 10 召回率，而不只是重新排序；
2. 引入规模更大且分布更贴近日常输入的干净口语语料；
3. 在不增加端到端延迟的前提下实现真正的 INT8 GRU 算子推理。
