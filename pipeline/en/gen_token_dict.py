#!/usr/bin/env python3
"""从语料词频生成 token 词典（无拼音）

读入 chars.cnt.txt，只保留 freq >= min_count 的词条，
按单字/多字分组，各自 Unicode 排序输出。

用法：python3 gen_token_dict.py [--min-count 16] [--cnt chars.cnt.txt] [--output sime.en.token.dict.txt]
"""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--min-count", type=int, default=16)
    parser.add_argument("--cnt", default="chars.cnt.txt")
    parser.add_argument("--output", default="sime.en.token.dict.txt")
    args = parser.parse_args()

    freq = {}
    for line in open(args.cnt):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2 and parts[0] and parts[1].isdigit():
            freq[parts[0]] = int(parts[1])

    print(f"corpus tokens: {len(freq)}", file=sys.stderr)

    tokens = [w for w, c in freq.items() if c >= args.min_count]

    chars = sorted([w for w in tokens if len(w) == 1])
    words = sorted([w for w in tokens if len(w) > 1])

    max_vocab = (1 << 18) - 70
    total = len(chars) + len(words)
    if total > max_vocab:
        print(f"WARNING: vocab {total} exceeds 18-bit limit {max_vocab}, "
              f"truncating words by frequency", file=sys.stderr)
        words_with_freq = sorted([(freq[w], w) for w in words], reverse=True)
        keep = max_vocab - len(chars)
        words = sorted([w for _, w in words_with_freq[:keep]])
        total = len(chars) + len(words)

    with open(args.output, "w") as fout:
        for w in chars:
            fout.write(w + "\n")
        for w in words:
            fout.write(w + "\n")

    print(f"total: {total} ({len(chars)} chars + {len(words)} words)", file=sys.stderr)
    print(f"written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
