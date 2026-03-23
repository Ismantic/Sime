# Wikipedia 语料训练记录

## 语料来源

中文维基百科 2023-07-20 过滤版，254,547 篇文章。

源文件：`wikipedia-cn-20230720-filtered.json`（501 MB），JSON 数组格式，每条含 `completion`（正文）和 `source` 字段。

## 训练步骤

### 1. 提取语料

```bash
python3 extract_wiki.py wikipedia-cn-20230720-filtered.json corpus_wiki.txt
```

按句号/问号/叹号/换行切句，过滤规则同 THUCNews（长度 4–200，中文占比 ≥ 40%）。

产出 `corpus_wiki.txt`：4,997,195 句，约为 THUCNews 的 29%。

### 2. 分词

```bash
ismacut freq_dict.txt --pipe < corpus_wiki.txt 2>/dev/null > corpus_wiki_seg.txt
```

### 3. N-gram 统计

```bash
sime-count -n 3 -d freq_dict.txt -s output/swap_wiki.bin -o output/wiki.3gram corpus_wiki_seg.txt
```

### 4. 构建语言模型

```bash
sime-construct output/wiki.3gram \
    -n 3 \
    -o output/wiki.slm \
    -c 0,2,3 \
    -w 84054 \
    -r 50000,4000000,4000000
```

| 参数 | 值 | 说明 |
|------|----|------|
| `-c 0,2,3` | cutoff | bigram 频次 ≤2 丢弃，trigram 频次 ≤3 丢弃 |
| `-r 50000,4000000,4000000` | prune | 各层最大保留节点数 |

不剪枝时 bigram 层 14,997,610 节点，超出 compact 位宽限制（8,388,608）。

产出模型各层节点数：

| 层级 | 节点数 |
|------|--------|
| root | 2 |
| unigram | 50,150 |
| bigram | 3,614,366 |
| trigram | 2,139,101 |
| **合计** | **5,803,619** |

### 5. 压缩

```bash
sime-compact output/wiki.slm output/wiki.t3g
```

产出 `output/wiki.t3g`：59 MB。

## 效果对比

| 拼音 | 期望 | THUCNews | Wikipedia |
|------|------|----------|-----------|
| woguojingjikuaisuzengzhang | 我国经济快速增长 | **正确** | 倭国经济快速增长 |
| zhongguokejidefazhan | 中国科技的发展 | **正确** | **正确** |
| zuqiubisaidefenxijieguo | 足球比赛的分析结果 | **正确** | **正确** |
| xueshengmenyinggainulixuexi | 学生们应该努力学习 | **正确** | **正确** |
| shoujichangshangfabuxinchanpin | 手机厂商发布新产品 | **正确** | **正确** |
| zhengfugongzuobaogao | 政府工作报告 | **正确** | **正确** |
| womenquchifanba | 我们去吃饭吧 | **正确** | 我们区吃饭吧 |
| fangyuanbaili | 方圆百里 | 房源百丽 | 方圆伯里 |
| cengjingcanghainanweishui | 曾经沧海难为水 | 曾经沧海南纬说 | 曾经藏海南为水 |
| zhonghuarenmingongheguo | 中华人民共和国 | **正确** | **正确** |
| kexuejishushidiyishengchanli | 科学技术是第一生产力 | **正确** | **正确** |
| taikongshangbumanxingxing | 太空上布满星星 | 太空上布满行星 | 太空上布满行星 |

Wikipedia 单独效果不如 THUCNews：
- 「倭国」替代「我国」——维基百科有大量倭寇相关文章，「倭」字频率偏高
- 「我们区」替代「我们去」——百科体裁缺乏口语表达
- 「方圆」修正了 THUCNews 的「房源」偏差，但「百里」仍错为「伯里」

## 词表影响分析

词表对结果的影响分三个层面：

### 1. 词表覆盖决定分词质量

分词器 IsmaCut 用 `freq_dict.txt`（84,054 词）。词表缺词时分词拆得过碎，影响 n-gram 统计。例如「曾经沧海」整词在词表中，能正确切分；但「难为水」不在词表里，只能拆成「难为」+「水」，导致 trigram 无法捕捉这个搭配。

### 2. freq_dict 词频与语料实际频次不一致

`freq_dict.txt` 的词频来自外部 `dict.txt`，不是从训练语料统计的：

| 词 | dict freq | THUCNews 实际 | Wikipedia 实际 |
|----|-----------|--------------|----------------|
| 们 | 362,902 | 1,462,367 | 125,811 |
| 门 | 414,922 | 567,770 | 103,767 |
| 把 | 125,538 | 338,121 | 37,981 |
| 我国 | 1,481,261 | 64,103 | 353 |

freq_dict 词频只影响 IsmaCut 分词的 DP 选路，不影响语言模型概率。但分词错误会间接影响 n-gram 质量。

### 3. 单字候选过多

词表含 26,655 个单字，包括大量生僻字（如「倭」freq=122）。同音单字候选多，全靠 trigram 消歧，beam search 宽度有限时容易选错。
