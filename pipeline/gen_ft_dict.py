#!/usr/bin/env python3
"""Build a parallel simplified→traditional vocab file for Sime.

Reads sime.token.dict.txt (one token per line, line index = TokenID-1)
and writes sime.ft.dict.txt with the same line count, where each line
holds the OpenCC-converted (s2t) traditional form of the same token.

Tokens whose simplified form is already identical to the traditional
form are written verbatim — this keeps line indices aligned 1:1 with
sime.token.dict.txt so the Android side can index by TokenID without
fallback logic.

Usage:
    python3 gen_ft_dict.py output/sime.token.dict.txt output/sime.ft.dict.txt
"""

import sys
from pathlib import Path

import opencc


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    # s2t = simplified → traditional (general, OpenCC standard).
    # If you ever want Taiwan / Hong Kong variants, switch to s2tw or s2hk.
    cc = opencc.OpenCC("s2t")

    n_total = 0
    n_changed = 0
    with src.open(encoding="utf-8") as fin, dst.open("w", encoding="utf-8") as fout:
        for line in fin:
            simp = line.rstrip("\n")
            trad = cc.convert(simp)
            fout.write(trad)
            fout.write("\n")
            n_total += 1
            if trad != simp:
                n_changed += 1

    print(
        f"Wrote {n_total} tokens to {dst} "
        f"({n_changed} differ from simplified, "
        f"{n_total - n_changed} unchanged).",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
