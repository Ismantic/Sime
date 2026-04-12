#!/usr/bin/env python3
"""生成 Sime 拼音词典

读入 chars.cnt.txt（语料词频）和 chinese_units.txt（拼音表），
只保留语料中出现次数 >= min_count 的词条，输出 sime.dict.txt。
不在拼音表中的纯中文词用 pypinyin 补标。

格式：word pinyin（与 sime-converter 输入格式一致）

用法：python3 gen_dict.py [--min-count 16] [--cnt chars.cnt.txt] [--units chinese_units.txt] [--output sime.dict.txt]
"""

import argparse
import re
import sys

from pypinyin import pinyin, Style


def is_chinese(word):
    return all(0x4E00 <= ord(c) <= 0x9FFF or
               0x3400 <= ord(c) <= 0x4DBF or
               0x20000 <= ord(c) <= 0x2FA1F for c in word)


def get_pinyin(word):
    """用 pypinyin 标注拼音，返回格式化字符串或 None"""
    if len(word) == 1:
        py = pinyin(word, style=Style.NORMAL, heteronym=True)
        readings = [p for p in py[0] if re.match(r'^[a-z]+$', p)]
        # 去重保持顺序
        seen = []
        for r in readings:
            if r not in seen:
                seen.append(r)
        return ' '.join(seen) if seen else None
    else:
        py = pinyin(word, style=Style.NORMAL, heteronym=False)
        parts = [p[0] for p in py]
        if all(re.match(r'^[a-z]+$', p) for p in parts):
            return "'".join(parts)
        return None


def main():
    parser = argparse.ArgumentParser(description="Generate Sime pinyin dictionary")
    parser.add_argument("--min-count", type=int, default=16,
                        help="minimum corpus frequency (default: 16)")
    parser.add_argument("--cnt", default="chars.cnt.txt",
                        help="corpus word frequency file (word\\tfreq)")
    parser.add_argument("--units", default="chinese_units.txt",
                        help="pinyin dictionary file (word pinyin)")
    parser.add_argument("--output", default="sime.dict.txt",
                        help="output file")
    args = parser.parse_args()

    # 读语料词频
    freq = {}
    for line in open(args.cnt):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2 and parts[0] and parts[1].isdigit():
            freq[parts[0]] = int(parts[1])

    print(f"corpus tokens: {len(freq)}", file=sys.stderr)

    # 读拼音表，建立已有拼音的词集合
    units = {}
    for line in open(args.units):
        line = line.rstrip("\n")
        if not line:
            continue
        idx = line.index(" ")
        word = line[:idx]
        units[word] = line  # 保留完整行

    print(f"pinyin entries: {len(units)}", file=sys.stderr)

    # 收集所有 freq >= min_count 的词
    from_units = 0
    from_pypinyin = 0
    skipped = 0
    results = []

    for word, cnt in freq.items():
        if cnt < args.min_count:
            continue
        if word in units:
            results.append(units[word])
            from_units += 1
        elif is_chinese(word):
            py = get_pinyin(word)
            if py:
                results.append(f"{word} {py}")
                from_pypinyin += 1
            else:
                results.append(word)  # 中文但 pypinyin 也标不了
                skipped += 1

    # 没有拼音的 token（标点、数字、字母等）也要放进来
    no_pinyin = 0
    for word, cnt in freq.items():
        if cnt < args.min_count:
            continue
        if word in units or is_chinese(word):
            continue  # 已处理过
        results.append(word)  # 无拼音，只有 word
        no_pinyin += 1

    # 排序输出：单字在前，多字在后，各自按 Unicode 排序
    chars = []
    words = []
    for r in results:
        # 有拼音的格式是 "word pinyin"，无拼音的只有 word
        w = r[:r.index(" ")] if " " in r else r
        if len(w) == 1:
            chars.append(r)
        else:
            words.append(r)

    chars.sort(key=lambda r: r[:r.index(" ")] if " " in r else r)
    words.sort(key=lambda r: r[:r.index(" ")] if " " in r else r)

    with open(args.output, "w") as fout:
        for line in chars:
            fout.write(line + "\n")
        for line in words:
            fout.write(line + "\n")

    total = len(chars) + len(words)
    print(f"\nmin_count: {args.min_count}", file=sys.stderr)
    print(f"from rime-ice: {from_units}", file=sys.stderr)
    print(f"from pypinyin: {from_pypinyin}", file=sys.stderr)
    print(f"no pinyin (punct/digit/etc): {no_pinyin}", file=sys.stderr)
    print(f"total: {total} ({len(chars)} chars + {len(words)} words)", file=sys.stderr)
    print(f"written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
