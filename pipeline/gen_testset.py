#!/usr/bin/env python3
"""从 sentences.units.txt 随机抽样生成评测集

输入: 中文\t拼音（空格分隔音节）
输出:
  - test.py: 拼音（空格分隔词，每词音节用连写）
  - test.ali: pinyin_汉字 格式

用法: python3 gen_testset.py [input] [size] [seed]
"""
import random
import sys

input_file = sys.argv[1] if len(sys.argv) > 1 else "nine/sentences.units.txt"
size = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 42

# Read all lines
lines = []
with open(input_file) as f:
    for line in f:
        line = line.rstrip("\n")
        if not line or "\t" not in line:
            continue
        text, pinyin = line.split("\t", 1)
        syllables = pinyin.split()
        # Skip if lengths don't match
        if len(text) != len(syllables):
            continue
        # Skip very short or very long
        if len(text) < 3 or len(text) > 30:
            continue
        lines.append((text, syllables))

print(f"Eligible lines: {len(lines)}", file=sys.stderr)

# Sample
random.seed(seed)
samples = random.sample(lines, min(size, len(lines)))

# Write test.py and test.ali
with open("test.py", "w") as fpy, open("test.ali", "w") as fali:
    for text, syllables in samples:
        # .py: all syllables concatenated (no spaces, like real user input)
        fpy.write("".join(syllables) + "\n")
        # .ali: pinyin_hanzi pairs
        pairs = [f"{syllables[i]}_{text[i]}" for i in range(len(text))]
        fali.write(" ".join(pairs) + "\n")

print(f"Generated {len(samples)} test samples", file=sys.stderr)
print(f"  test.py, test.ali", file=sys.stderr)
