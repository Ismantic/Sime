#!/usr/bin/env python3
"""从 dict.inc 导出完整拼音音节列表（排除纯声母）

用法: python3 gen_pinyin_dict.py [dict.inc] [output]
"""
import re
import sys

input_file = sys.argv[1] if len(sys.argv) > 1 else "../../src/dict.inc"
output_file = sys.argv[2] if len(sys.argv) > 2 else "pinyin.dict.txt"

syllables = []
for line in open(input_file):
    m = re.match(r'\s*\{\s*"(\w+)"\s*,\s*(0x[\da-fA-F]+)', line)
    if m:
        s = m.group(1)
        val = int(m.group(2), 16)
        # 跳过纯声母：Full() = ((value >> 4) & 0xFF) != 0
        if ((val >> 4) & 0xFF) == 0:
            continue
        syllables.append(s)

# 保持 dict.inc 原始顺序（与 NineDecoder 的 TokenID 分配一致）
with open(output_file, "w") as f:
    for s in syllables:
        f.write(s + "\n")

print(f"{len(syllables)} syllables → {output_file}", file=sys.stderr)
