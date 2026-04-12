#!/usr/bin/env python3
"""从 sentences.units.txt 生成 sime-count 所需的训练数据

读入 pinyin.dict.txt 作为合法音节表，过滤语料中不合法的音节。

输入: sentences.units.txt (中文\t拼音)
输出: data.cut.txt (拼音语料，空格分隔音节)

用法: python3 gen_count_data.py [input] [output] [dict]
"""
import sys

input_file = sys.argv[1] if len(sys.argv) > 1 else "sentences.units.txt"
output_file = sys.argv[2] if len(sys.argv) > 2 else "data.cut.txt"
dict_file = sys.argv[3] if len(sys.argv) > 3 else "pinyin.dict.txt"

# 加载合法音节表
valid = set()
for line in open(dict_file):
    s = line.strip()
    if s:
        valid.add(s)
print(f"loaded {len(valid)} valid syllables", file=sys.stderr)

lines = 0
written = 0
skipped_syllables = 0

with open(input_file, "r") as fin, open(output_file, "w") as fout:
    for line in fin:
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        lines += 1

        syllables = parts[1].split()
        filtered = []
        for s in syllables:
            if s in valid:
                filtered.append(s)
            else:
                skipped_syllables += 1

        if len(filtered) >= 2:
            fout.write(" ".join(filtered) + "\n")
            written += 1

print(f"input: {lines} lines", file=sys.stderr)
print(f"output: {written} lines → {output_file}", file=sys.stderr)
print(f"skipped syllables: {skipped_syllables}", file=sys.stderr)
