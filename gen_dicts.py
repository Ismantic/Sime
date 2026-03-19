#!/usr/bin/env python3
"""Generate pinyin_dict.txt and freq_dict.txt from sime_dict.v0.txt + dict.txt."""

import sys
import os

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <sime_dict.v0.txt> <dict.txt> [output_dir]", file=sys.stderr)
        sys.exit(1)

    sime_dict_path = sys.argv[1]
    freq_src_path = sys.argv[2]
    output_dir = sys.argv[3] if len(sys.argv) > 3 else "."

    # 1. Build word -> freq map from dict.txt
    freq_map = {}
    with open(freq_src_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            parts = line.split("\t", 1)
            if len(parts) == 2:
                word, freq_str = parts
                try:
                    freq_map[word] = int(freq_str)
                except ValueError:
                    pass

    # 2. Process sime_dict.v0.txt
    pinyin_out = os.path.join(output_dir, "pinyin_dict.txt")
    freq_out = os.path.join(output_dir, "freq_dict.txt")

    kept = 0
    skipped = 0

    with open(sime_dict_path, "r", encoding="utf-8") as f_in, \
         open(pinyin_out, "w", encoding="utf-8") as f_pinyin, \
         open(freq_out, "w", encoding="utf-8") as f_freq:

        for line in f_in:
            line = line.rstrip("\n")
            if not line:
                continue
            parts = line.split()
            if not parts:
                continue

            word = parts[0]

            # Skip <xxx> special tokens
            if word.startswith("<") and word.endswith(">"):
                skipped += 1
                continue

            # parts[1] is the old ID (discard it)
            # parts[2:] are pinyins (if any), strip :prob suffix
            if len(parts) >= 3:
                pinyins = [p.split(":")[0] for p in parts[2:]]
            else:
                pinyins = []

            # Skip punctuation (no pinyin)
            if not pinyins:
                skipped += 1
                continue

            # Write pinyin_dict.txt: word pinyin1 [pinyin2 ...]
            f_pinyin.write(f"{word} {' '.join(pinyins)}\n")

            # Write freq_dict.txt: word\tfreq
            freq = freq_map.get(word, 1)
            f_freq.write(f"{word}\t{freq}\n")

            kept += 1

    print(f"Kept: {kept}, Skipped: {skipped}", file=sys.stderr)
    print(f"Written: {pinyin_out}, {freq_out}", file=sys.stderr)


if __name__ == "__main__":
    main()
