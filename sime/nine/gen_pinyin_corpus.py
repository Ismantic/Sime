#!/usr/bin/env python3
"""将句子语料转为拼音音节序列

每行一句中文 → 每行一句拼音（空格分隔音节）
非中文字符跳过，遇到非中文段落断开。

用法：python3 gen_pinyin_corpus.py [input] [output]
"""
import sys
from pypinyin import pinyin, Style

input_file = sys.argv[1] if len(sys.argv) > 1 else "../sentences.txt"
output_file = sys.argv[2] if len(sys.argv) > 2 else "pinyin_corpus.txt"
max_lines = int(sys.argv[3]) if len(sys.argv) > 3 else 0  # 0 = unlimited


def is_chinese(c):
    cp = ord(c)
    return (0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF or
            0x20000 <= cp <= 0x2A6DF)


def is_punct(c):
    return c in '，。！？、；：""''（）《》【】—…·\u3000 \t'


def is_valid_sentence(line):
    """句子中只能有中文和标点，否则丢弃"""
    for c in line:
        if not is_chinese(c) and not is_punct(c):
            return False
    return True


def convert_line(line):
    """将一行中文按标点断开，每个子句转为 (中文, 拼音序列) 对"""
    segments = []
    chars = []
    syllables = []
    for char in line:
        if is_chinese(char):
            py = pinyin(char, style=Style.NORMAL, heteronym=False)
            s = py[0][0]
            if s.isalpha():
                chars.append(char)
                syllables.append(s)
        elif is_punct(char):
            if len(chars) >= 2:
                segments.append(("".join(chars), syllables))
            chars = []
            syllables = []
    if len(chars) >= 2:
        segments.append(("".join(chars), syllables))
    return segments


lines = 0
written = 0

with open(input_file, "r") as fin, open(output_file, "w") as fout:
    for line in fin:
        line = line.rstrip("\n")
        if not line:
            continue
        lines += 1

        if not is_valid_sentence(line):
            continue

        segments = convert_line(line)
        for text, syllables in segments:
            fout.write(text + "\t" + " ".join(syllables) + "\n")
            written += 1

        if max_lines > 0 and lines >= max_lines:
            break

        if lines % 500000 == 0:
            print(f"{lines} lines → {written} pinyin lines", file=sys.stderr)

print(f"done: {lines} lines → {written} pinyin lines", file=sys.stderr)
print(f"written to {output_file}", file=sys.stderr)
