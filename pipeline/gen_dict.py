#!/usr/bin/env python3
"""生成 Sime token 词典和拼音词典

步骤 1: dict.txt 全部词 + chars.cnt.txt 按频次补到上限 → sime.token.dict.txt
步骤 2: 中文词标注拼音 → sime.dict.txt
步骤 3: 英文词小写映射 → sime.en.dict.txt
"""

import argparse
import re
import sys

from pypinyin import pinyin, Style


def is_chinese(word):
    return all(0x4E00 <= ord(c) <= 0x9FFF or
               0x3400 <= ord(c) <= 0x4DBF or
               0x20000 <= ord(c) <= 0x2FA1F for c in word)


def is_english(word):
    return (all(c.isascii() and (c.isalpha() or c == "'") for c in word)
            and len(word) > 0 and word[0].isalpha())


def is_punct(word):
    """标点符号（中英文标点、数学符号等，不含数字和字母）"""
    return all(not c.isalnum() and c.isprintable() for c in word) and len(word) > 0


def get_pinyin(word):
    """用 pypinyin 标注拼音，返回格式化字符串或 None"""
    if len(word) == 1:
        py = pinyin(word, style=Style.NORMAL, heteronym=True)
        readings = [p for p in py[0] if re.match(r'^[a-z]+$', p)]
        seen = []
        for r in readings:
            if r not in seen:
                seen.append(r)
        return ' '.join(seen) if seen else None
    else:
        py = pinyin(word, style=Style.NORMAL, heteronym=False)
        parts = [p[0] for p in py]
        if all(re.match(r'^[a-z]+$', p) for p in parts):
            return "/".join(parts)
        return None


def abbrev_variants(unit_str):
    """对多音节拼音生成简拼变体。
    最后一个音节保持完整，前面的缩写为首字母，然后从前往后逐个恢复。
    """
    syllables = unit_str.split("/")
    n = len(syllables)
    if n < 2:
        return []
    initials = [s[0] for s in syllables[:n - 1]] + [syllables[-1]]
    variants = []
    full_abbr = "/".join(initials)
    if full_abbr != unit_str:
        variants.append(full_abbr)
    restored = list(initials)
    for i in range(n - 2):
        restored[i] = syllables[i]
        variant = "/".join(restored)
        if variant != unit_str and (not variants or variant != variants[-1]):
            variants.append(variant)
    return variants


def main():
    parser = argparse.ArgumentParser(description="Generate Sime dictionaries")
    parser.add_argument("--min-count", type=int, default=16,
                        help="minimum corpus frequency (default: 16)")
    parser.add_argument("--cnt", default="chars.cnt.txt",
                        help="corpus word frequency file (word\\tfreq)")
    parser.add_argument("--units", default="chinese_units.txt",
                        help="pinyin dictionary file (word pinyin)")
    parser.add_argument("--dict", default="dict.txt",
                        help="Chinese word dictionary (word\\tcount)")
    parser.add_argument("--token-output", default="sime.token.dict.txt",
                        help="token dict output")
    parser.add_argument("--cn-output", default="sime.dict.txt",
                        help="Chinese pinyin dict output")
    parser.add_argument("--en-output", default="sime.en.dict.txt",
                        help="English dict output")
    parser.add_argument("--en-words", default="",
                        help="English word list for filtering (one word per line)")
    parser.add_argument("--punct", default="",
                        help="Punctuation whitelist (one per line)")
    args = parser.parse_args()

    # ── 读英文词表 ──
    en_words = set()
    if args.en_words:
        for line in open(args.en_words):
            w = line.rstrip("\n")
            if w:
                en_words.add(w)
        print(f"en word list: {len(en_words)}", file=sys.stderr)

    # ── 读语料词频 ──
    freq = {}
    for line in open(args.cnt):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2 and parts[0] and parts[1].isdigit():
            freq[parts[0]] = int(parts[1])
    print(f"corpus tokens: {len(freq)}", file=sys.stderr)

    # ── 读拼音表 ──
    units = {}
    for line in open(args.units):
        line = line.rstrip("\n")
        if not line:
            continue
        idx = line.index(" ")
        word = line[:idx]
        units[word] = line
    print(f"pinyin entries: {len(units)}", file=sys.stderr)

    # ── Step 1: sime.token.dict.txt (closed vocabulary, whitelist only) ──
    max_vocab = (1 << 18) - 70  # 262074

    all_tokens = []
    all_seen = set()

    # 1a. 中文白名单（dict.txt）: corpus 里至少出现 1 次
    cn_kept = cn_dropped = 0
    for line in open(args.dict):
        w = line.rstrip("\n").split("\t")[0]
        if not w or w in all_seen:
            continue
        if freq.get(w, 0) < 1:
            cn_dropped += 1
            continue
        all_tokens.append(w)
        all_seen.add(w)
        cn_kept += 1
    print(f"CN whitelist (dict.txt): kept {cn_kept}, dropped {cn_dropped} "
          f"(freq<1)", file=sys.stderr)

    # 1b. 英文白名单（en_words）: corpus 里至少出现 min_count 次
    en_kept = en_dropped = 0
    for w in en_words:
        if not w or w in all_seen:
            continue
        if freq.get(w, 0) < args.min_count:
            en_dropped += 1
            continue
        all_tokens.append(w)
        all_seen.add(w)
        en_kept += 1
    print(f"EN whitelist (en_words): kept {en_kept}, dropped {en_dropped} "
          f"(freq<{args.min_count})", file=sys.stderr)

    # 1c. 标点白名单（punct.txt）: corpus 里至少出现 1 次
    pn_kept = pn_dropped = 0
    if args.punct:
        for line in open(args.punct):
            w = line.rstrip("\n")
            if not w or w in all_seen:
                continue
            if freq.get(w, 0) < 1:
                pn_dropped += 1
                continue
            all_tokens.append(w)
            all_seen.add(w)
            pn_kept += 1
    print(f"Punct whitelist: kept {pn_kept}, dropped {pn_dropped} (freq<1)",
          file=sys.stderr)

    if len(all_tokens) > max_vocab:
        all_tokens = all_tokens[:max_vocab]
    with open(args.token_output, "w") as fout:
        for w in all_tokens:
            fout.write(w + "\n")
    print(f"total tokens: {len(all_tokens)} "
          f"(cn:{cn_kept} + en:{en_kept} + punct:{pn_kept})", file=sys.stderr)
    print(f"written to {args.token_output}", file=sys.stderr)

    # ── Step 2: sime.dict.txt (中文拼音词典) ──
    from_units = 0
    from_pypinyin = 0
    pinyin_count = 0
    abbrev_count = 0

    # 收集中文词的拼音
    cn_entries = []  # (word, pinyin_str)
    token_set = set(all_tokens)
    for word in all_tokens:
        if not is_chinese(word):
            continue
        if word in units:
            py_str = units[word][units[word].index(" ") + 1:].replace("'", "/")
            cn_entries.append((word, py_str))
            from_units += 1
        else:
            py_str = get_pinyin(word)
            if py_str:
                cn_entries.append((word, py_str))
                from_pypinyin += 1

    with open(args.cn_output, "w") as fout:
        for word, py_str in cn_entries:
            fout.write(f"{word} {py_str}\n")
            pinyin_count += 1

    print(f"\nfrom units: {from_units}", file=sys.stderr)
    print(f"from pypinyin: {from_pypinyin}", file=sys.stderr)
    print(f"pinyin entries: {pinyin_count}", file=sys.stderr)
    print(f"written to {args.cn_output}", file=sys.stderr)

    # ── Step 3: sime.en.dict.txt (英文词典) ──
    en_count = 0
    with open(args.en_output, "w") as fout:
        for word in all_tokens:
            if is_english(word):
                fout.write(f"{word} {word.lower()}\n")
                en_count += 1

    print(f"\nen tokens: {en_count}", file=sys.stderr)
    print(f"written to {args.en_output}", file=sys.stderr)


if __name__ == "__main__":
    main()
