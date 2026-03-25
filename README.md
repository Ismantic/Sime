# Sime 是语输入法

基于 Trigram 语言模型的拼音输入法引擎。使用 Modified Kneser-Ney 平滑、entropy pruning 和量化压缩，配合拼音 Trie 实现拼音到汉字的转换。

## 特性

- **Modified Kneser-Ney 平滑** — N-gram backoff 语言模型
- **Entropy Pruning** — 基于信息增益裁剪低价值 N-gram
- **量化压缩** — 概率/backoff 权重量化 + threaded backoff 索引，模型体积压缩约 50%
- **拼音 Trie** — 音节前缀树，支持多音字和词组匹配
- **Viterbi 解码** — Beam search 求解最优汉字序列
- **内置模型** — 基于维基百科、人民日报、THUCNews 语料训练，274,506 词条

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使用

```bash
./build/ime_interpreter --trie dict/trie.bin --model dict/model.bin
```

输入拼音，输出候选汉字序列：

```
> nihao
  [0] 你好 (score -15.234)
> zhonghuarenmingongheguo
  [0] 中华人民共和国 (score -24.935)
```

输入 `:quit` 退出。

## 训练

训练流程依赖 [IsmaCut](https://github.com/Ismantic/IsmaCut) 分词器。完整步骤见 [docs/TRAINING.md](docs/TRAINING.md)，V1 训练记录见 [docs/TRAIN_V1.md](docs/TRAIN_V1.md)。

简要流程：

```
语料 → IsmaCut 分词 → sime-count (trigram 统计)
     → sime-construct (构建 backoff 模型)
     → sime-compact (量化压缩) → model.bin

词典 + pypinyin → pinyin_dict.txt
     → sime-converter → trie.bin
```

## 项目结构

```
dict/
  trie.bin       - 拼音 Trie（二进制）
  model.bin      - 压缩语言模型（二进制）
include/
  compact.h      - 压缩模型结构与位域定义
  construct.h    - 语言模型构建
  convert.h      - 拼音 Trie 构建
  count.h        - N-gram 计数
  score.h        - 语言模型评分
  interpret.h    - Viterbi 解码
  unit.h         - 拼音音节编码
src/
  compact.cc     - 量化压缩实现
  construct.cc   - MKN 平滑 + entropy pruning
  convert.cc     - 拼音 Trie 序列化
  count.cc       - 外部排序 N-gram 统计
  score.cc       - 压缩模型加载与查询
  interpret.cc   - Beam search 解码
app/
  interpreter.cc - 交互式输入法 CLI
  compact.cc     - sime-compact 入口
  construct.cc   - sime-construct 入口
  converter.cc   - sime-converter 入口
  counter.cc     - sime-count 入口
docs/
  TRAINING.md    - 训练流程文档
  TRAIN_V1.md    - V1 训练记录
```

## License

MIT
