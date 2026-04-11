# Sime 是语输入法

纯 C++ 实现的中文拼音输入法引擎，当前支持 Linux 和 Android。

## 特性

- Modified Kneser-Ney N-gram 语言模型
- Entropy pruning 控制模型体积
- 量化压缩（概率/backoff 权重量化 + threaded backoff 索引）
- 拼音 Trie 前缀匹配，支持多音字和不完整拼音
- Viterbi beam search 解码
- 九宫格 (T9) 输入

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使用

```bash
# 拼音模式
./build/sime-interpreter --trie output/sime.trie --cnt output/sime.cnt

# 拼音句子模式（支持部分匹配）
./build/sime-interpreter --trie output/sime.trie --cnt output/sime.cnt -s

# 九宫格模式
./build/sime-interpreter --trie output/sime.trie --cnt output/sime.cnt --num

# 九宫格句子模式
./build/sime-interpreter --trie output/sime.trie --cnt output/sime.cnt --num -s

# NineDecoder 独立测试（拼音解码）
./build/sime-interpreter --nine nine/output/sime.nine
```

```
> nihao
  [0] 你好 [ni'hao] (score -18.962)
> zhonghuarenmingongheguo
  [0] 中华人民共和国 [zhong'hua'ren'min'gong'he'guo] (score -24.935)
```

## 训练

训练流程在 `sime/` 目录下，依赖已经做好切分的数据文件 `sentences.cut.txt`。

### 前置准备

- `sentences.cut.txt` — 切词后的语料（空格分隔）
- `chinese_units.txt` — 拼音词典

### 训练步骤

```bash
cd sime

# 1. 统计语料词频
make chars

# 2. 生成拼音词典
make dict

# 3. N-gram 统计
make count

# 4. 构建语言模型
make construct

# 5. 编译拼音 Trie
make convert

# 6. 量化压缩
make compact
```

产出文件在 `sime/output/`：
- `sime.trie` — 拼音 Trie（二进制）
- `sime.cnt` — 压缩语言模型（二进制）

### 参数调整

在 `sime/Makefile` 中：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `MIN_COUNT` | 16 | 词频阈值，低于此值的词不进入词表 |
| `COUNT_MAX` | 83886080 | sime-count 内存缓存条目数 |

## 平台

- **Linux** — Fcitx5 插件，见 `Linux/fcitx5/`
- **Android** — 独立输入法应用，见 `Android/`

## License

Apache-2.0
