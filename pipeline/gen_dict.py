#!/usr/bin/env python3
"""生成 Sime token 词典和拼音词典

步骤 1: 从 chars.cnt.txt 筛选 freq >= min_count 的词 → sime.token.dict.txt
步骤 2: 结合 units.txt 给有拼音的 token 标注拼音 → sime.dict.txt

用法：python3 gen_dict.py [--min-count 16] [--cnt chars.cnt.txt] [--units chinese_units.txt] [--output sime.dict.txt] [--token-output sime.token.dict.txt]
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
                        help="pinyin dict output file")
    parser.add_argument("--token-output", default="sime.token.dict.txt",
                        help="token dict output file (all tokens, no pinyin)")
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

    # TokenID is 18-bit; first 70 IDs are reserved (StartToken = 70).
    max_vocab = (1 << 18) - 70  # 262074
    total = len(chars) + len(words)
    if total > max_vocab:
        print(f"WARNING: vocab {total} exceeds 18-bit token limit {max_vocab}, "
              f"truncating words by frequency", file=sys.stderr)
        # 保留全部单字，按词频截断多字词
        words_with_freq = []
        for r in words:
            w = r[:r.index(" ")] if " " in r else r
            words_with_freq.append((freq.get(w, 0), r))
        words_with_freq.sort(key=lambda x: x[0], reverse=True)
        keep = max_vocab - len(chars)
        words = [r for _, r in words_with_freq[:keep]]
        words.sort(key=lambda r: r[:r.index(" ")] if " " in r else r)
        total = len(chars) + len(words)
        print(f"after truncation: {total} ({len(chars)} chars + {len(words)} words)",
              file=sys.stderr)

    # Step 1: 输出 token dict（全部 token，无拼音）
    with open(args.token_output, "w") as fout:
        for line in chars:
            w = line[:line.index(" ")] if " " in line else line
            fout.write(w + "\n")
        for line in words:
            w = line[:line.index(" ")] if " " in line else line
            fout.write(w + "\n")
    print(f"\nmin_count: {args.min_count}", file=sys.stderr)
    print(f"total tokens: {total} ({len(chars)} chars + {len(words)} words)", file=sys.stderr)
    print(f"written to {args.token_output}", file=sys.stderr)

    # Step 2: 输出 pinyin dict（格式: Text Token Units）
    # 多字词额外生成简拼变体（从前往后逐个缩写为首字母）
    def abbrev_variants(unit_str):
        """对 apostrophe 分隔的拼音生成简拼变体。
        最后一个音节保持完整，前面的缩写为首字母，然后从前往后逐个恢复：
        gui'sui'shou → [g's'shou, gui's'shou]
        zhong'guo → [z'guo]
        zhong'guo'zheng'fu → [z'g'z'fu, zhong'g'z'fu, zhong'guo'z'fu]
        """
        syllables = unit_str.split("'")
        n = len(syllables)
        if n < 2:
            return []
        # 前 n-1 个缩写为首字母，最后一个保持
        initials = [s[0] for s in syllables[:n - 1]] + [syllables[-1]]
        variants = []
        # 全缩写（最后一个保持）
        full_abbr = "'".join(initials)
        if full_abbr != unit_str:
            variants.append(full_abbr)
        # 从前往后逐个恢复全拼
        restored = list(initials)
        for i in range(n - 2):
            restored[i] = syllables[i]
            variant = "'".join(restored)
            if variant != unit_str and (not variants or variant != variants[-1]):
                variants.append(variant)
        return variants

    pinyin_count = 0
    abbrev_count = 0
    with open(args.output, "w") as fout:
        for line in chars:
            if " " in line:
                w = line[:line.index(" ")]
                units_str = line[line.index(" ") + 1:]
                fout.write(f"{w} {w} {units_str}\n")
                pinyin_count += 1
        for line in words:
            if " " in line:
                w = line[:line.index(" ")]
                units_str = line[line.index(" ") + 1:]
                fout.write(f"{w} {w} {units_str}\n")
                pinyin_count += 1
                # 生成简拼变体（仅对 apostrophe 分隔的多音节拼音）
                if "'" in units_str:
                    for variant in abbrev_variants(units_str):
                        fout.write(f"{w} {w} {variant}\n")
                        abbrev_count += 1
    print(f"from rime-ice: {from_units}", file=sys.stderr)
    print(f"from pypinyin: {from_pypinyin}", file=sys.stderr)
    print(f"no pinyin (punct/digit/etc): {no_pinyin}", file=sys.stderr)
    print(f"pinyin entries: {pinyin_count}", file=sys.stderr)
    print(f"abbreviated entries: {abbrev_count}", file=sys.stderr)
    print(f"written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
