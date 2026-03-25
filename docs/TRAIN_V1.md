# V1 模型训练记录（2026-03-25）

基于维基百科、人民日报、THUCNews 三份语料，训练 IsmaCut 分词词典和 Sime 拼音输入法语言模型的完整流程。

## 数据来源

| 语料 | 路径 | 格式 | 规模 |
|------|------|------|------|
| 维基百科 | `Data/wikipedia-cn-20230720-filtered.json` | JSON 数组，`completion` 字段 | 254,547 篇 |
| 人民日报 | `Data/PeopleDaily/*.jsonl.gz` | JSONL，`text` 字段 | 79 个文件（1946-2025） |
| THUCNews | `Data/THUCNews/*.jsonl` | JSONL，`content` 字段 | 14 个分类 |

词典来源：`Dict/dict-20260324-main-lite+single.txt`，326,655 词条（纯词表，无词频）。

## 第一阶段：IsmaCut 分词词典训练

### 1.1 语料预处理

从三份 JSON 语料中提取纯文本，按 `\n` 分行，去掉空行：

```python
# Wikipedia: json.load → item["completion"].split("\n")
# PeopleDaily: gzip.open → json.loads(line)["text"].split("\n")
# THUCNews: json.loads(line)["content"].split("\n")
```

产出：

| 文件 | 行数 |
|------|------|
| `corpus_wikipedia.txt` | 3,679,146 |
| `corpus_peopledaily.txt` | 13,518,412 |
| `corpus_thucnews.txt` | 8,532,361 |
| `corpus_all.txt`（合并） | **25,729,919** |

### 1.2 EM 训练

使用 IsmaCut 的 `train.sh` 脚本，冷启动（最长匹配）→ DP 分词 → 词频统计循环迭代：

```bash
scripts/train.sh dict-main.txt corpus_all.txt 5
```

单轮约 15 分钟，5 轮后变化行数仍在下降：

| 轮次 | 活跃词数 | 变化行数 |
|------|---------|---------|
| Cold start | 284,087 | — |
| Round 0 | 278,630 | — |
| Round 1 | 274,858 | 1,886,282 |
| Round 2 | 274,538 | 297,481 |
| Round 3 | 274,511 | 82,710 |
| Round 4 | 274,507 | 40,088 |

续跑 5 轮（Round 5-9），Round 6 后完全收敛（274,506 词）。

### 1.3 产出

- `IsmaCut/dict/dict.txt` — 274,506 词条，格式 `word\tfreq`
- `IsmaCut/output/seg_r9.txt` — 最终分词结果（25,729,919 行），用于下游语言模型训练

## 第二阶段：Sime 词典生成

### 2.1 拼音词典

旧词典 `sime_dict.v0.txt` 中有 84,054 个词的拼音标注。IsmaCut 词典扩展到 274,506 词后，需要为新增的 190,452 个词补充拼音。

使用 pypinyin 库为缺失词生成拼音，音节用 `'` 连接以匹配 Sime 格式：

```python
from pypinyin import pinyin, Style

pys = pinyin(word, style=Style.NORMAL)
joined = "'".join([p[0] for p in pys])  # e.g. "hu'lian'wang"
```

产出：

| 文件 | 条目数 | 说明 |
|------|--------|------|
| `pinyin_dict.txt` | 274,506 | 格式：`word pinyin`（如 `互联网 hu'lian'wang`） |
| `freq_dict.txt` | 274,506 | 格式：`word\tfreq`（频次来自 IsmaCut dict.txt） |

### 2.2 拼音 Trie

```bash
sime-converter pinyin_dict.txt output/pinyin.ime.bin
```

产出 `pinyin.ime.bin`（6.9 MB）。

## 第三阶段：语言模型训练

### 3.1 Trigram 统计

使用 IsmaCut 最终分词结果 `seg_r9.txt` 作为语料：

```bash
sime-count -n 3 \
    -d freq_dict.txt \
    -s output/swap_new.bin \
    -o output/new.3gram \
    -c 83886080 \
    seg_r9.txt
```

`-c 83886080`（80M）将内存中的 trigram 计数上限从默认 1M 调到 80M，减少 flush 次数，降低 swap 文件大小。实际内存占用约 5GB，运行时间约 40 分钟。

产出 `new.3gram`（7.5 GB）。

### 3.2 构建模型（含剪枝）

```bash
sime-construct output/new.3gram \
    -n 3 \
    -o output/new_p1.slm \
    -c 0,0,2 \
    -w 274506 \
    -r 50000,600000,1000000
```

| 参数 | 说明 |
|------|------|
| `-c 0,0,2` | 各层 cutoff，trigram 频次 ≤ 2 丢弃 |
| `-w 274506` | 词汇表大小 |
| `-r 50000,600000,1000000` | 各层保留最大条目数（entropy pruning） |

产出 `new_p1.slm`（25 MB）。

### 3.3 量化压缩

```bash
sime-compact output/new_p1.slm output/new_p1.t3g
```

产出 `new_p1.t3g`（21 MB）。

### 3.4 代码改动

词典从 84,054 扩展到 274,506 后，TokenID 超过了原 18 位上限（262,143）。修改 bit-field 布局：

| 字段 | 旧值 | 新值 |
|------|------|------|
| TokenBits | 18 | 19 |
| BowBits | 14 | 13 |

涉及文件：`include/compact.h`、`src/compact.cc`、`src/score.cc`。leaf 节点的 pro_index 拆分从硬编码改为基于常量计算。

## 第四阶段：效果验证

测试用例对比（旧模型 → 新模型）：

| 输入 | 旧模型 | 新模型 |
|------|--------|--------|
| jintiandtianqizhenhao | 今天[d]天气真好 ✓ | 今天[d]天气真好 ✓ |
| woguojingjikuaisuzengzhang | 我过竞技快速增长 ✗ | 我国经济快速增长 ✓ |
| zhongguokejidefazhan | 中国可基德发展 ✗ | 中国科技的发展 ✓ |
| zuqiubisaidefenxijieguo | 足球比赛得分细节过 ✗ | 足球比赛的分析结果 ~ |
| gupiaoshichangjintianxiadie | 股票市场今天下跌 ✓ | 股票市场今天下跌 ✓ |
| xueshengmenyinggainulixuexi | 学生门应该努力学习 ✗ | 学生们应该努力学习 ✓ |
| shoujichangshangfabuxinchanpin | 手机厂商发布心产品 ✗ | 手机场上发布新产品 ~ |
| zhengfugongzuobaogao | 征服工作报告 ✗ | 政府工作报告 ✓ |
| womenquchifanba | 我们取吃饭把 ✗ | 我们去吃饭把 ✓ |
| fangyuanbaili | 这全球变暖的问题 ✗ | 方圆百里 ✓ |

显著提升：10 个测试中，旧模型 2 个正确，新模型 7 个正确。

## 产出文件

| 文件 | 大小 | 说明 |
|------|------|------|
| `dict/trie.bin` | 6.9 MB | 拼音 Trie（pinyin.ime.bin） |
| `dict/model.bin` | 21 MB | 压缩语言模型（lm.t3g） |

## 运行

```bash
ime_interpreter --trie dict/trie.bin --model dict/model.bin
```
