# Sime 训练流程

## 前置条件

- 编译 Sime：`cmake --build build -j$(nproc)`
- 编译 IsmaCut：`cmake --build /path/to/IsmaCut/build -j$(nproc)`

## 源数据

| 文件 | 说明 |
|------|------|
| `sime_dict.v0.txt` | 拼音词典源文件，格式：`word id [pinyin ...]` |
| `dict.txt` | 词频文件（IsmaCut 用），格式：`word\tfreq` |
| `corpus.txt` | 训练语料，每行一句中文 |

## Pipeline

```
sime_dict.v0.txt + dict.txt
  --> gen_dicts.py --> pinyin_dict.txt + freq_dict.txt

corpus.txt
  --> ismacut (分词)
  --> sime-count (N-gram 统计)
  --> sime-construct (构建语言模型)
  --> sime-compact (量化压缩)
  --> lm.t3g

pinyin_dict.txt
  --> sime-converter --> pinyin.ime.bin

运行时: pinyin.ime.bin + lm.t3g --> ime_interpreter
```

## 步骤

### 1. 生成词典

```bash
python3 gen_dicts.py sime_dict.v0.txt dict.txt .
```

从源数据生成两个词典文件（词集合相同，84054 条）：
- `pinyin_dict.txt` — 格式：`word [pinyin1 pinyin2 ...]`（标点无拼音）
- `freq_dict.txt` — 格式：`word\tfreq`（查不到词频的赋 1）

### 2. 分词

```bash
ismacut freq_dict.txt --pipe < corpus.txt 2>/dev/null > corpus_seg.txt
```

IsmaCut DAG+DP 分词，输出空格分隔的分词结果。

### 3. N-gram 统计

```bash
sime-count -n 3 -d freq_dict.txt -s output/swap.bin -o output/raw.3gram corpus_seg.txt
```

| 参数 | 说明 |
|------|------|
| `-n 3` | 统计 trigram |
| `-d freq_dict.txt` | 词典（按行序分配 TokenID） |
| `-s output/swap.bin` | 外部排序临时文件 |
| `-o output/raw.3gram` | 输出文件 |
| 末尾 | 输入分词文件（可多个） |

输出二进制格式：每条记录 `[TokenID x 3, uint32_t freq]`，按字典序排列。

### 4. 构建语言模型

```bash
sime-construct output/raw.3gram \
    -n 3 \
    -o output/raw.slm \
    -c 0,0,2 \
    -w 84054
```

| 参数 | 说明 |
|------|------|
| `-n 3` | 3-gram 模型 |
| `-o` | 输出路径 |
| `-c 0,0,2` | 各层 cutoff（逗号分隔），频次 <= cutoff 的 N-gram 被丢弃 |
| `-w 84054` | 词汇表大小（freq_dict.txt 行数） |

折扣方法固定为 Modified Kneser-Ney，无需指定。

可选剪枝：

```bash
sime-construct output/raw.3gram \
    -n 3 \
    -o output/raw.slm \
    -c 0,0,2 \
    -w 84054 \
    -r 15000,180000,300000
```

| 参数 | 说明 |
|------|------|
| `-r 15000,180000,300000` | 各层保留的最大条目数（逗号分隔），基于 entropy pruning 裁剪低价值 N-gram |

输出 `raw.slm`，未压缩的 backoff 语言模型。

### 5. 压缩

```bash
sime-compact output/raw.slm output/lm.t3g
```

量化概率和 backoff 权重，计算 threaded backoff 索引。输出 `lm.t3g`（~5.5 MB）。

### 6. 构建拼音 Trie

```bash
sime-converter pinyin_dict.txt output/pinyin.ime.bin
```

将拼音词典编译为二进制 Trie。每个拼音音节作为边，节点存储候选词 TokenID。输出 `pinyin.ime.bin`（~2 MB）。

### 7. 验证

```bash
echo "nihao" | ime_interpreter --trie output/pinyin.ime.bin --model output/lm.t3g
```

预期第一个候选为「你好」。

## 完整脚本

```bash
#!/bin/bash
set -e

SIME=./build
ISMACUT=/path/to/IsmaCut/build/ismacut
DICT_SRC=sime_dict.v0.txt
FREQ_SRC=dict.txt
CORPUS=corpus.txt
OUT=output

mkdir -p $OUT

# 1. 生成词典
python3 gen_dicts.py $DICT_SRC $FREQ_SRC .
VOCAB=$(wc -l < freq_dict.txt)

# 2. 分词
$ISMACUT freq_dict.txt --pipe < $CORPUS 2>/dev/null > corpus_seg.txt

# 3. N-gram 统计
$SIME/sime-count -n 3 -d freq_dict.txt -s $OUT/swap.bin -o $OUT/raw.3gram corpus_seg.txt

# 4. 构建语言模型
$SIME/sime-construct $OUT/raw.3gram \
    -n 3 -o $OUT/raw.slm \
    -c 0,0,2 \
    -w $VOCAB

# 5. 压缩
$SIME/sime-compact $OUT/raw.slm $OUT/lm.t3g

# 6. 拼音 Trie
$SIME/sime-converter pinyin_dict.txt $OUT/pinyin.ime.bin

# 7. 验证
echo "nihao" | $SIME/ime_interpreter --trie $OUT/pinyin.ime.bin --model $OUT/lm.t3g
```
