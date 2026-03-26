# 基于 BPE Tokenizer 的训练流程

使用 [IsmaTokenizer](https://github.com/Ismantic/Tokenizer) 训练的 BPE 模型作为词典，替代传统的字/词级别分词。

## Pipeline

```
tokenizer.model → gen_freq_dict.py    → freq_dict.txt
               → gen_token_pinyin.py → pinyin_token_dict.txt → sime-converter → trie.bin

corpus.txt → tokenize_corpus.py（用 tokenizer.model 分词）→ corpus_tokenized.txt
          → sime-count → sime-construct → sime-compact → model.bin
```

## 步骤

### 0. 准备 tokenizer.model

使用 IsmaTokenizer 训练 BPE 模型：

```bash
isma-tokenizer train --method bytepiece \
    --input corpus.txt \
    --model tokenizer \
    --vocab-size 80000
```

词表大小不应超过 262,143（TokenBits=18 的上限）。

### 1. 生成词典

从 tokenizer.model 生成两个词典文件，两者行数和行序必须一致：

```bash
# freq_dict.txt — 所有 piece 及其 score，供 sime-count 分配 TokenID
python3 gen_freq_dict.py tokenizer.model freq_dict.txt

# pinyin_token_dict.txt — 所有 piece 附拼音，供 sime-converter 构建 Trie
# 单字多音字自动标注所有读音，非中文 piece 只输出 piece 不附拼音（converter 会跳过）
python3 gen_token_pinyin.py tokenizer.model pinyin_token_dict.txt
```

`gen_token_pinyin.py` 依赖 `pypinyin` 库。

### 2. 语料 Tokenize

用训练好的 BPE 模型对语料做分词，每行输出空格分隔的 piece 序列：

```bash
python3 tokenize_corpus.py tokenizer.model corpus.txt corpus_tokenized.txt
```

`tokenize_corpus.py` 依赖编译好的 `isma_tokenizer` Python 模块（位于 IsmaTokenizer/build/python/）。

可以对多个语料分别 tokenize，然后在 sime-count 时传入多个文件。

### 3. 构建拼音 Trie

```bash
sime-converter pinyin_token_dict.txt output/trie.bin
```

### 4. N-gram 统计 → 语言模型 → 压缩

与标准流程相同：

```bash
VOCAB=$(wc -l < freq_dict.txt)

sime-count -n 3 -d freq_dict.txt \
    -s output/swap.bin -o output/raw.3gram \
    -c 83886080 \
    corpus_tokenized.txt

sime-construct output/raw.3gram \
    -n 3 -o output/raw.slm \
    -c 0,0,2 -w $VOCAB \
    -r 50000,600000,1000000

sime-compact output/raw.slm output/model.bin
```

### 5. 验证

```bash
echo "nihao" | ime_interpreter --trie output/trie.bin --model output/model.bin
```

## 与传统方案的区别

| | 传统方案 | BPE 方案 |
|---|---|---|
| 词典来源 | IsmaCut 分词词典 | IsmaTokenizer BPE 模型 |
| 分词方式 | IsmaCut 最大匹配 | BPE tokenize |
| 粒度 | 字/词 | BPE piece（子词） |
| 词典大小 | ~27 万 | ~8 万 |
| 多字 piece | 固定词典 | 数据驱动学习 |
