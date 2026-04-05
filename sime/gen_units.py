#!/usr/bin/env python3
"""从 rime-ice 词库生成 chinese_units.txt（拼音词典）

格式：word pinyin
  单字多音：字 yin1 yin2
  多字词：词 pin'yin
"""

import os
import sys
from collections import defaultdict

RIME_DIR = "/home/tfbao/new/rime-ice/cn_dicts"

# 按优先级排列，靠前的权重优先
DICT_FILES = [
    "8105.dict.yaml",
    "41448.dict.yaml",
    "base.dict.yaml",
    "ext.dict.yaml",
    "tencent.dict.yaml",
]


def parse_rime_dict(path):
    """解析 rime dict.yaml，返回 [(word, pinyin_syllables)]"""
    entries = []
    in_header = True
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if in_header:
                if line == "...":
                    in_header = False
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            word = parts[0]
            pinyin = parts[1]  # space-separated syllables
            entries.append((word, pinyin))
    return entries


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "chinese_units.txt"

    # 单字：收集所有读音
    char_readings = defaultdict(list)
    # 多字词：只取第一个出现的读音
    word_pinyin = {}

    total = 0
    for fname in DICT_FILES:
        path = os.path.join(RIME_DIR, fname)
        if not os.path.exists(path):
            print(f"skip: {path}", file=sys.stderr)
            continue
        entries = parse_rime_dict(path)
        print(f"{fname}: {len(entries)} entries", file=sys.stderr)

        for word, pinyin in entries:
            total += 1
            syllables = pinyin.strip().split()
            # 验证拼音音节都是纯字母
            if not all(s.isalpha() for s in syllables):
                continue
            # 验证音节数和字数匹配
            if len(syllables) != len(word):
                continue

            if len(word) == 1:
                # 单字：收集所有不重复的读音
                for s in syllables:
                    if s not in char_readings[word]:
                        char_readings[word].append(s)
            else:
                # 多字词：取第一个出现的
                if word not in word_pinyin:
                    word_pinyin[word] = "'".join(syllables)

    # 输出
    with open(output, "w") as f:
        # 先输出单字（按 Unicode 排序）
        for ch in sorted(char_readings.keys()):
            readings = char_readings[ch]
            f.write(f"{ch} {' '.join(readings)}\n")

        # 再输出多字词（按词排序）
        for word in sorted(word_pinyin.keys()):
            f.write(f"{word} {word_pinyin[word]}\n")

    n_chars = len(char_readings)
    n_words = len(word_pinyin)
    print(f"\ntotal entries read: {total}", file=sys.stderr)
    print(f"chars: {n_chars}, words: {n_words}, total: {n_chars + n_words}",
          file=sys.stderr)
    print(f"written to {output}", file=sys.stderr)


if __name__ == "__main__":
    main()
