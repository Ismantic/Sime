#!/usr/bin/env python3
"""从语料词频生成 en token 词典和英文拼写词典

1. chars.cnt.txt 按频次筛选 → sime.en.token.dict.txt
2. 英文词小写映射 → sime.en.dict.txt
"""

import argparse
import sys

EXCLUDE = {"▁"}


def is_english(word):
    return all(c.isascii() and c.isalpha() for c in word) and len(word) > 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--min-count", type=int, default=16)
    parser.add_argument("--cnt", default="chars.cnt.txt")
    parser.add_argument("--token-output", default="sime.en.token.dict.txt")
    parser.add_argument("--en-output", default="sime.en.dict.txt")
    args = parser.parse_args()

    freq = {}
    for line in open(args.cnt):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2 and parts[0] and parts[1].isdigit():
            freq[parts[0]] = int(parts[1])

    print(f"corpus tokens: {len(freq)}", file=sys.stderr)

    # 按频次降序，过 min_count，排除 ▁
    tokens = []
    for w, c in sorted(freq.items(), key=lambda x: x[1], reverse=True):
        if c < args.min_count:
            continue
        if w in EXCLUDE:
            continue
        if not w.isprintable():
            continue
        tokens.append(w)

    max_vocab = (1 << 18) - 70
    if len(tokens) > max_vocab:
        print(f"WARNING: vocab {len(tokens)} exceeds limit {max_vocab}, truncating",
              file=sys.stderr)
        tokens = tokens[:max_vocab]

    # sime.en.token.dict.txt
    with open(args.token_output, "w") as fout:
        for w in tokens:
            fout.write(w + "\n")
    print(f"total tokens: {len(tokens)}", file=sys.stderr)
    print(f"written to {args.token_output}", file=sys.stderr)

    # sime.en.dict.txt
    en_count = 0
    with open(args.en_output, "w") as fout:
        for w in tokens:
            if is_english(w):
                fout.write(f"{w} {w} {w}\n")
                en_count += 1
    print(f"en tokens: {en_count}", file=sys.stderr)
    print(f"written to {args.en_output}", file=sys.stderr)


if __name__ == "__main__":
    main()
