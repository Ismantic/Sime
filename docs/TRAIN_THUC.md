# THUCNews 语料训练记录

## 语料来源

THUCNews 新闻分类语料，14 个类别，共 836,075 篇文章。

| 类别 | 篇数 |
|------|------|
| 科技 | 162,929 |
| 股票 | 154,398 |
| 体育 | 131,604 |
| 娱乐 | 92,632 |
| 时政 | 63,086 |
| 社会 | 50,849 |
| 教育 | 41,936 |
| 财经 | 37,098 |
| 家居 | 32,586 |
| 游戏 | 24,373 |
| 房产 | 20,050 |
| 时尚 | 13,368 |
| 彩票 | 7,588 |
| 星座 | 3,578 |

源文件路径：`/home/tfbao/new/THUCNews/*.jsonl`，JSONL 格式，每行含 `id`、`title`、`content` 字段。

## 训练步骤

### 1. 提取语料

```bash
python3 extract_corpus.py /home/tfbao/new/THUCNews corpus_thuc.txt
```

从 title 和 content 中按句号/问号/叹号/换行切句，过滤规则：
- 长度 < 4 或 > 200 的句子丢弃
- 中文字符占比 < 50% 的句子丢弃（排除 URL、代码等）

产出 `corpus_thuc.txt`：17,030,129 句，1.9 GB。

### 2. 分词

```bash
ismacut freq_dict.txt --pipe < corpus_thuc.txt 2>/dev/null > corpus_thuc_seg.txt
```

产出 `corpus_thuc_seg.txt`：2.3 GB。

### 3. N-gram 统计

```bash
sime-count -n 3 -d freq_dict.txt -s output/swap_thuc.bin -o output/thuc.3gram corpus_thuc_seg.txt
```

产出 `output/thuc.3gram`：2.3 GB。

### 4. 构建语言模型

```bash
sime-construct output/thuc.3gram \
    -n 3 \
    -o output/thuc.slm \
    -c 0,3,5 \
    -w 84054 \
    -r 50000,2000000,4000000
```

| 参数 | 值 | 说明 |
|------|----|------|
| `-c 0,3,5` | cutoff | unigram 不裁，bigram 频次 ≤3 丢弃，trigram 频次 ≤5 丢弃 |
| `-r 50000,2000000,4000000` | prune | 各层最大保留节点数，entropy pruning 裁剪低价值 n-gram |
| `-w 84054` | 词汇量 | freq_dict.txt 行数 |

cutoff 和 prune 的选择原因：不剪枝时 bigram 层 1015 万、trigram 层 1124 万节点，超出 compact 的位宽限制（DownBits=23，最大 8,388,608）。

产出模型各层节点数：

| 层级 | 节点数 |
|------|--------|
| root | 2 |
| unigram | 50,001 |
| bigram | 2,000,001 |
| trigram | 4,000,001 |
| **合计** | **6,050,005** |

产出 `output/thuc.slm`：62 MB。

### 5. 压缩

```bash
sime-compact output/thuc.slm output/thuc.t3g
```

产出 `output/thuc.t3g`：55 MB。

## 效果对比

旧模型：corpus.txt（10,000 句），节点 230,258，模型 5.5 MB。
新模型：THUCNews（17,030,129 句），节点 6,050,005，模型 55 MB。

| 拼音 | 旧模型 | 新模型 |
|------|--------|--------|
| woguojingjikuaisuzengzhang | 我过竞技快速增长 | **我国经济快速增长** |
| zhongguokejidefazhan | 中国可基德发展 | **中国科技的发展** |
| zuqiubisaidefenxijieguo | 足球比赛得分细节过 | **足球比赛的分析结果** |
| xueshengmenyinggainulixuexi | 学生门应该努力学习 | **学生们应该努力学习** |
| shoujichangshangfabuxinchanpin | 手机厂商发布心产品 | **手机厂商发布新产品** |
| zhengfugongzuobaogao | 征服工作报告 | **政府工作报告** |
| womenquchifanba | 我们取吃饭把 | **我们去吃饭吧** |
| gupiaoshichangjintianxiadie | 股票市场今天下跌 | 股票市场今天下跌 |

10 个测试例中 7 个显著改善。

## 已知问题

新闻语料存在领域偏差：
- 「房源」(7540次) 压过「方圆」(1076次)，fangyuan 首选变成「房源」
- 古诗词「曾经沧海难为水」无法正确输出，因新闻语料几乎不含文学文本
- 房产、股票类新闻占比大，相关词汇频率偏高

改善方向：混入通用语料（日常对话、文学作品等）来平衡分布。
