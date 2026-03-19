# Sime 输入法模型训练流程

## 前置条件

- 编译 Sime：`cd build && cmake .. && cmake --build . -j$(nproc)`
- 编译 IsmaCut：`cd /path/to/IsmaCut/build && cmake .. && cmake --build . -j$(nproc)`

## 源数据

| 文件 | 说明 |
|------|------|
| `sime_dict.v0.txt` | 拼音词典源文件，格式：`<word> <id> [pinyin:prob ...]` |
| `dict.txt` | 词频文件（IsmaCut 用），格式：`word\tfreq` |
| `corpus.txt` | 训练语料，每行一句中文 |

## 训练步骤

### 1. 生成词典

从源数据生成两个词典文件：

```bash
python3 gen_dicts.py sime_dict.v0.txt dict.txt .
```

产出：
- `pinyin_dict.txt` — 拼音词典，格式：`word pinyin1 [pinyin2 ...]`
- `freq_dict.txt` — 词频词典，格式：`word\tfreq`

### 2. 分词

使用 IsmaCut 对语料进行 DAG+DP 分词：

```bash
ismacut freq_dict.txt --pipe < corpus.txt 2>/dev/null > corpus_seg.txt
```

产出 `corpus_seg.txt`，每行为空格分隔的分词结果。

### 3. N-gram 统计

```bash
sime-count -n 3 -d freq_dict.txt -s output/swap.bin -o output/raw.3gram corpus_seg.txt
```

参数：
- `-n 3`：统计 3-gram
- `-d freq_dict.txt`：词典文件（按行序分配 TokenID）
- `-s output/swap.bin`：外部排序临时文件
- `-o output/raw.3gram`：输出的排序后 N-gram 二进制文件
- 末尾：输入的分词文件（可指定多个）

产出 `raw.3gram`，二进制格式：`[TokenID × 3, uint32_t freq]` 按字典序排列。

### 4. 构建语言模型

```bash
sime-construct -n 3 \
    -o output/raw.slm \
    -d "ABS" -d "ABS" -d "ABS" \
    -w 84054 \
    -b "10" \
    output/raw.3gram
```

参数：
- `-n 3`：3-gram 模型
- `-o`：输出路径
- `-d "ABS"`：每层折扣方法（Absolute Discounting），需指定 3 次对应 3 层
  - 可选：`LIN [d]`（Linear）
- `-w 84054`：词汇表大小（freq_dict.txt 行数）
- `-b "10"`：句子边界 token（kSentenceToken=10）
- 末尾：输入 N-gram 文件

产出 `raw.slm`，未压缩的语言模型二进制文件。

### 5. 压缩语言模型

```bash
sime-compress output/raw.slm output/lm.t3g
```

将概率和 backoff 权重量化压缩，计算 backoff 状态索引。

产出 `lm.t3g`，压缩后的 threaded 语言模型，可直接用于运行时。

### 6. 构建拼音 Trie

```bash
sime-converter pinyin_dict.txt output/pinyin.ime.bin
```

将拼音词典编译为二进制 Trie 树。每个拼音音节（如 zhong、guo）作为一条边，叶节点存储候选词的 TokenID。

产出 `pinyin.ime.bin`，拼音查找用的二进制 Trie。

### 7. 验证

```bash
echo "nihao" | ime_interpreter --pydict output/pinyin.ime.bin --lm output/lm.t3g
```

预期输出第一个候选为「你好」。

## 运行时文件

最终只需要两个文件：

| 文件 | 大小（参考） | 用途 |
|------|-------------|------|
| `pinyin.ime.bin` | ~2 MB | 拼音 → 候选词查找 |
| `lm.t3g` | ~5.5 MB | 语言模型打分 |

## 完整脚本示例

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
$SIME/sime-construct -n 3 -o $OUT/raw.slm \
    -d "ABS" -d "ABS" -d "ABS" \
    -w $VOCAB -b "10" \
    $OUT/raw.3gram

# 5. 压缩
$SIME/sime-compress $OUT/raw.slm $OUT/lm.t3g

# 6. 拼音 Trie
$SIME/sime-converter pinyin_dict.txt $OUT/pinyin.ime.bin

echo "Done. Test with:"
echo "  echo nihao | $SIME/ime_interpreter --pydict $OUT/pinyin.ime.bin --lm $OUT/lm.t3g"
```
