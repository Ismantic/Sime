# Sime 训练流程

## 前置条件

- 编译 Sime：`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- 准备好 `pinyin_dict.txt`（格式：`word pinyin`，音节用 `'` 连接，如 `中国 zhong'guo`）
- 准备好 `freq_dict.txt`（格式：`word\tfreq`，词条与 pinyin_dict.txt 一致）
- 准备好分词语料 `corpus_seg.txt`（每行一句，空格分隔，使用与 freq_dict.txt 相同的词典由 IsmaCut 分词产生）

## Pipeline

```
pinyin_dict.txt → sime-converter → trie.bin

corpus_seg.txt → sime-count (trigram 统计)
             → sime-construct (构建 backoff 模型)
             → sime-compact (量化压缩)
             → model.bin

运行时: trie.bin + model.bin → ime_interpreter
```

## 步骤

### 1. 构建拼音 Trie

```bash
sime-converter pinyin_dict.txt output/trie.bin
```

将拼音词典编译为二进制 Trie。每个拼音音节作为边，节点存储候选词 TokenID。

### 2. N-gram 统计

```bash
sime-count -n 3 \
    -d freq_dict.txt \
    -s output/swap.bin \
    -o output/raw.3gram \
    -c 83886080 \
    corpus_seg.txt
```

| 参数 | 说明 |
|------|------|
| `-n 3` | 统计 trigram |
| `-d freq_dict.txt` | 词典（按行序分配 TokenID） |
| `-s output/swap.bin` | 外部排序临时文件 |
| `-o output/raw.3gram` | 输出文件 |
| `-c 83886080` | 内存中最大 trigram 条目数，越大 flush 越少，推荐按可用内存调整 |
| 末尾 | 输入分词文件（可多个） |

输出二进制格式：每条记录 `[TokenID x 3, uint32_t freq]`，按字典序排列。

### 3. 构建语言模型

```bash
sime-construct output/raw.3gram \
    -n 3 \
    -o output/raw.slm \
    -c 0,0,2 \
    -w $VOCAB \
    -r 50000,600000,1000000
```

| 参数 | 说明 |
|------|------|
| `-n 3` | 3-gram 模型 |
| `-o` | 输出路径 |
| `-c 0,0,2` | 各层 cutoff（逗号分隔），频次 ≤ cutoff 的 N-gram 被丢弃 |
| `-w $VOCAB` | 词汇表大小（freq_dict.txt 行数） |
| `-r 50000,600000,1000000` | 各层保留最大条目数（entropy pruning），按模型体积需求调整 |

折扣方法固定为 Modified Kneser-Ney。输出 `raw.slm`，未压缩的 backoff 语言模型。

### 4. 压缩

```bash
sime-compact output/raw.slm output/model.bin
```

量化概率和 backoff 权重，计算 threaded backoff 索引。

### 5. 验证

```bash
echo "nihao" | ime_interpreter --trie output/trie.bin --model output/model.bin
```

预期第一个候选为「你好」。

## 完整脚本

```bash
#!/bin/bash
set -e

SIME=./build
CORPUS_SEG=corpus_seg.txt
OUT=output
VOCAB=$(wc -l < freq_dict.txt)

mkdir -p $OUT

# 1. 拼音 Trie
$SIME/sime-converter pinyin_dict.txt $OUT/trie.bin

# 2. N-gram 统计
$SIME/sime-count -n 3 -d freq_dict.txt \
    -s $OUT/swap.bin -o $OUT/raw.3gram \
    -c 83886080 $CORPUS_SEG

# 3. 构建语言模型（含剪枝）
$SIME/sime-construct $OUT/raw.3gram \
    -n 3 -o $OUT/raw.slm \
    -c 0,0,2 -w $VOCAB \
    -r 50000,600000,1000000

# 4. 压缩
$SIME/sime-compact $OUT/raw.slm $OUT/model.bin

# 5. 验证
echo "nihao" | $SIME/ime_interpreter --trie $OUT/trie.bin --model $OUT/model.bin
```

## 注意事项

- 词典大小不应超过 262,143（TokenBits=18 的上限），超过需调整 `include/compact.h` 中的位域常量
- `-c`（count_max）越大，swap 文件越小、速度越快，但内存占用越高（每条约 80 字节）
- `-r` 参数控制模型体积，数值越小模型越小但精度下降
