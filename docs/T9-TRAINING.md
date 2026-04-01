# T9 拼音 Bigram 模型训练

T9 输入法需要一个拼音音节级的 bigram 语言模型，用于将数字序列解码为拼音序列。
训练流程完全复用现有的 sime-count → sime-construct → sime-compact 管线。

## 前置条件

- 构建好的 sime 工具链（`sime-count`, `sime-construct`, `sime-compact`）
- Python 3 + `datasets` 库（用于下载语料）

```bash
pip install datasets
```

## 1. 准备语料

运行 `scripts/prepare_t9_corpus.py`，从 [Duyu/Pinyin-Hanzi](https://huggingface.co/datasets/Duyu/Pinyin-Hanzi) 数据集生成训练数据：

```bash
python scripts/prepare_t9_corpus.py
```

产出：
- `dict_pinyin.txt` — 从 `src/dict.inc` 提取的 ~417 个有效拼音音节，一行一个
- `corpus_pinyin.txt` — 164 万句拼音语料，每行空格分隔（去声调、去标点）

示例语料：
```
wo men shi shi kan
ta you yi jia dan che
wo xiang yao yi liang che dan wo mei qian mai
```

如果已有本地 parquet 文件，可用 `--parquet` 跳过下载：

```bash
python scripts/prepare_t9_corpus.py --parquet /path/to/data.parquet
```

## 2. 统计 Bigram 计数

```bash
./build/sime-count -n 2 -d dict_pinyin.txt -o pinyin_counts.bin -s /tmp/t9_swap corpus_pinyin.txt
```

## 3. 构建 MKN 平滑语言模型

```bash
./build/sime-construct -n 2 -w 417 -o pinyin_lm_raw.bin pinyin_counts.bin
```

`-w 417` 对应拼音音节总数。

## 4. 量化压缩

```bash
./build/sime-compact pinyin_lm_raw.bin pinyin_model.bin
```

产出 `pinyin_model.bin`（约 1.3MB），即 T9 解码器所需的拼音 bigram 模型。

## 5. 测试

数字 → 拼音（不需要 trie.bin / model.bin）：

```bash
./build/ime_interpreter --t9model pinyin_model.bin --t9 --num 10
> 64426
  [0] ni'hao
> 94664486
  [0] zhong'guo
```

数字 → 汉字（完整管线）：

```bash
./build/ime_interpreter --trie dict/trie.bin --model dict/model.bin --t9model pinyin_model.bin --t9 --num 10
> 64426
  [0] 你好
```

## T9 数字编码参考

```
2 → a b c    5 → j k l    8 → t u v
3 → d e f    6 → m n o    9 → w x y z
4 → g h i    7 → p q r s
```

常见词编码：

| 词语 | 拼音 | T9 编码 |
|------|------|---------|
| 你好 | ni hao | 64426 |
| 中国 | zhong guo | 94664486 |
| 我的 | wo de | 9633 |
| 今天 | jin tian | 5468426 |
| 学习 | xue xi | 98394 |
