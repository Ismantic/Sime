# T9 数字序列 → 拼音序列 解码方案

## 1. 问题定义

九宫格键盘上，用户按下数字序列（如 `64426`），需要将其解码为最可能的拼音序列（如 `ni'hao`）。

数字与字母的对应关系：

```
2 → a b c
3 → d e f
4 → g h i
5 → j k l
6 → m n o
7 → p q r s
8 → t u v
9 → w x y z
```

核心挑战：

- **切分歧义**：`64426` 可以切成 `64|426`、`6|44|26`、`6|4|4|2|6` 等
- **编码歧义**：同一段数字对应多个拼音，如 `64` → `mi` 或 `ni`

## 2. 整体架构

```
数字序列 → [T9 Decoder] → 最优完整拼音序列 + 备选首音节列表
                                    ↓
                            [拼音→汉字模块]（与 26 键共用）
```

T9 Decoder 是一个独立模块，输入数字串，输出：

- **bestFullParse**：消费全部数字的最优拼音切分，交给下游出汉字
- **alternativeHeads**：备选的首音节列表，作为拼音候选显示在候选栏

## 3. 数据结构

### 3.1 音节码表

将全部约 400 个有效拼音音节按 T9 规则编码，建立反向索引：

```
T9 编码 → [拼音音节列表]

"2"    → [a, e]            // 单韵母
"24"   → [ah, ai, bi, ci]  // 不一定都是有效拼音，需要过滤
"64"   → [mi, ni]
"426"  → [han, hao, gao, gan, ...]
"6426" → [nian, miao, ...]
...
```

构建方式：遍历所有有效拼音音节，将每个字母替换为对应数字，聚合。

存储为一个 **Trie**，key 为数字序列，每个节点存储该前缀对应的完整音节列表（仅在完整音节结束时非空）。

```
Trie 节点结构：
  children: Map<Char, TrieNode>   // '2'-'9'
  syllables: List<String>         // 该编码对应的完整拼音音节，叶节点非空
```

示例 Trie 路径：

```
root
 └─ 6
     ├─ syllables: [e, o]        // 单字母韵母（如果有效）
     └─ 4
         ├─ syllables: [mi, ni]
         └─ 2
             └─ 6
                 └─ syllables: [nian, miao]
```

### 3.2 拼音 Bigram 模型

统计拼音音节之间的转移概率，用于切分和消歧。

```
P(syllable_j | syllable_i) = count(i, j) / count(i)
```

加上句首概率：

```
P(syllable | <START>)
```

存储方式：一个二维表或稀疏 HashMap。

```
bigram: Map<(String, String), Float>   // (前一音节, 当前音节) → log概率
unigram: Map<String, Float>            // 音节 → log概率（回退用）
```

400 个音节的 bigram 表理论上限 160K 条，实际有效组合远少于此。用稀疏存储 + unigram 回退，整体几百 KB。

### 3.3 输出结构

```kotlin
data class T9DecoderResult(
    /**
     * 最优完整解码（消费全部数字），按概率降序
     * 如 "64426" → [["ni","hao"], ["mi","hao"]]
     * 交给拼音→汉字模块出候选
     */
    val fullParses: List<List<String>>,

    /**
     * 备选首音节（只消费前几位数字），按概率降序
     * 如 "64426" → ["ni","mi","o","m"]
     * 作为拼音候选显示在候选栏
     * 用户选择后，锁定该音节，对剩余数字重新解码
     */
    val alternativeHeads: List<HeadCandidate>,
)

data class HeadCandidate(
    val pinyin: String,     // 音节，如 "mi"
    val digitLength: Int,   // 消费了几位数字，如 2
)
```

## 4. 解码算法

### 4.1 核心：Viterbi 搜索

对数字串从左到右扫描，在每个位置维护 top-k 个最优路径。

```
输入: digits = "64426"
输出: 最优拼音切分 + 备选首音节

算法:

1. 初始化 beam[0] = [(score=0.0, path=[])]

2. 对每个位置 start（从 beam 中取有路径的位置）:
     对每个可能的结束位置 end = start+1 ... start+6:
       segment = digits[start:end]
       syllables = trie.lookup(segment)
       如果 syllables 为空，continue

       对每个 syllable in syllables:
         对每个 (prev_score, prev_path) in beam[start]:
           prev_syllable = prev_path 的最后一个元素，或 "<START>"
           score = prev_score + bigram_score(prev_syllable, syllable)
           将 (score, prev_path + [syllable]) 加入 beam[end]

     beam[end] 只保留 top-k（k=8 足够）

3. 输出:
   fullParses = beam[len(digits)] 中的路径，按 score 降序
   alternativeHeads = 从 beam 中收集所有以 position 0 开始的首音节
```

### 4.2 备选首音节的收集

在搜索过程中，所有从位置 0 出发的第一个音节都记录下来：

```
digits = "64426"

首音节候选:
  "64"   → ni (score=-1.2), mi (score=-2.1)
  "644"  → (无有效音节)
  "6442" → (查 trie，假设有 nian 等)
  "6"    → o (score=-3.5), m (score=-4.0)   // 单键
  ...

按首音节 score + 后续最优路径的联合 score 排序
去掉已在 fullParses 中出现的首音节
```

### 4.3 复杂度分析

- 拼音音节最长 6 个字符 → 每个位置最多检查 6 个结束位置
- 每段最多匹配 ~5 个音节（同码平均 2-3 个）
- beam 宽度 k=8
- 总时间：O(n × 6 × 5 × 8) = O(240n)，n 为数字串长度

对于典型输入长度（<20 位），耗时在微秒级别。

## 5. Bigram 模型的训练

### 5.1 训练数据

任意中文语料 → 分词 → 标注拼音 → 统计音节 bigram。

```
"你好世界" → ["ni", "hao", "shi", "jie"]
统计:
  P(ni | <START>) += 1
  P(hao | ni) += 1
  P(shi | hao) += 1
  P(jie | shi) += 1
```

### 5.2 平滑

使用 Kneser-Ney 平滑或简单的加一平滑 + unigram 回退：

```
score(cur | prev) =
    if bigram(prev, cur) exists:
        λ × log P_bigram(cur | prev) + (1-λ) × log P_unigram(cur)
    else:
        log P_unigram(cur)  // 回退到 unigram
```

λ 取 0.8 左右即可。

### 5.3 模型大小估算

- 400 音节 × 400 音节 = 160K 条 bigram（理论上限）
- 实际有效组合约 20K-30K 条
- 每条存一个 float (4 bytes)
- 总大小：~120KB（不压缩），加上 unigram 和索引 ~200KB

## 6. 音节码表构建

### 6.1 有效音节列表

标准普通话拼音约 400 个有效音节（不含声调）。来源可以是任意标准拼音表。

### 6.2 编码规则

```python
T9_MAP = {
    'a': '2', 'b': '2', 'c': '2',
    'd': '3', 'e': '3', 'f': '3',
    'g': '4', 'h': '4', 'i': '4',
    'j': '5', 'k': '5', 'l': '5',
    'm': '6', 'n': '6', 'o': '6',
    'p': '7', 'q': '7', 'r': '7', 's': '7',
    't': '8', 'u': '8', 'v': '8',
    'w': '9', 'x': '9', 'y': '9', 'z': '9',
}

def encode(pinyin: str) -> str:
    return ''.join(T9_MAP[c] for c in pinyin)
```

### 6.3 冲突统计

编码后的冲突情况：

```
"64"   → [mi, ni]                     2 个
"426"  → [han, hao, gan, gao]         4 个
"5426" → [jian, lian, ...]            多个
"24"   → [ai, bi, ci, ah, ...]        需过滤无效拼音
```

大部分编码只对应 1-3 个有效音节，少数热门编码对应 4-6 个。

## 7. 增量解码

用户是逐键输入的，不是一次性给出完整数字串。需要支持增量更新。

### 7.1 逐键更新

```
用户按 6:    decode("6")      → fullParses: [["o"],["m"],["n"]]
用户按 4:    decode("64")     → fullParses: [["ni"],["mi"]]
用户按 4:    decode("644")    → fullParses: [["nig"],["mih"]] // 可能无效
用户按 2:    decode("6442")   → fullParses: ...
用户按 6:    decode("64426")  → fullParses: [["ni","hao"]]
```

每次按键重新解码整个数字串。由于算法耗时在微秒级，无需缓存中间状态。

### 7.2 用户选择首音节后

```
用户在 "64426" 时选了 [mi]:
  确认: "mi"，消费 2 位数字
  剩余: "426"
  重新解码: decode("426", context="mi")
  → fullParses: [["hao"],["gan"],["gao"]]
  → 候选栏: [米好] [米干] [米高] [han] [gan] ...
```

context 参数传入已确认的前一个音节，用于 bigram 计算首音节概率。

## 8. 接口定义

```kotlin
interface T9Decoder {
    /**
     * 解码数字序列为拼音
     * @param digits 数字序列，如 "64426"
     * @param context 已确认的前一个音节（用于 bigram），如 "mi"，首次为 null
     * @return 解码结果
     */
    fun decode(digits: String, context: String? = null): T9DecoderResult
}

class T9DecoderImpl(
    private val trie: SyllableTrie,        // 音节码表 Trie
    private val bigram: PinyinBigramModel,  // 拼音 bigram 模型
    private val beamWidth: Int = 8,         // beam 宽度
) : T9Decoder {
    override fun decode(digits: String, context: String?): T9DecoderResult {
        // Viterbi beam search
        // ...
    }
}
```

26 键模式不经过 T9Decoder，直接做音节切分后进入拼音→汉字模块。

## 9. 候选栏展示逻辑

```
T9DecoderResult
  ├─ fullParses[0] → 送入拼音→汉字模块 → 汉字候选 [你好, 你号, ...]
  ├─ fullParses[1] → 送入拼音→汉字模块 → 汉字候选 [米好, ...]（按需，可只取 top-1 或 top-2）
  └─ alternativeHeads → 直接显示为拼音候选 [ni, mi, o, m]

候选栏最终排列:
  [你好] [你号] [你] ... [mi] [ni] [o] [m]
   ──────────────────     ─────────────────
   汉字候选               拼音候选（点击后锁定该音节）
```

## 10. 总结

| 组件 | 作用 | 大小 |
|------|------|------|
| 音节 Trie | 数字编码 → 候选拼音查找 | ~400 条，几 KB |
| Bigram 模型 | 音节切分 + 消歧 | ~200KB |
| Viterbi 搜索 | 找最优切分路径 | 运行时微秒级 |

整个 T9 Decoder 是一个无状态的纯函数，不依赖词典或汉字数据，只关心"数字→拼音"这一步。
