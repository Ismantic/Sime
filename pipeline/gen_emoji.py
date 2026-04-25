#!/usr/bin/env python3
"""Parse Unicode's emoji-test.txt into Sime's emoji asset format.

Reads the upstream test file (downloadable from
https://unicode.org/Public/emoji/<version>/emoji-test.txt) and writes
one emoji per line, with category headers ``# group: <name>`` between
groups. Skipped:
  * Component-only entries (skin-tone modifiers, hair colors, etc.).
  * Skin-tone variants of base emoji (we keep only the default tone).
  * Anything not "fully-qualified" (we want canonical codepoint forms).

Mapping group names → Chinese category labels happens here so the
runtime side just reads what's emitted.

Usage:
    python3 gen_emoji.py emoji-test.txt output/emoji.txt
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Group → Chinese label.  Groups not listed are skipped.
GROUPS = {
    "Smileys & Emotion": "表情",
    "People & Body":     "人物",
    "Animals & Nature":  "动物",
    "Food & Drink":      "食物",
    "Activities":        "活动",
    "Travel & Places":   "旅行",
    "Objects":           "物品",
    "Symbols":           "符号",
    # "Flags" omitted — country flags balloon the file ~250 entries
    # and are rarely used by Chinese IME users.
}

# Skin-tone modifier code points (Fitzpatrick scale).
SKIN_TONES = {0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF}

# Hair-color modifiers (we drop variants with these).
HAIR_MODS = {0x1F9B0, 0x1F9B1, 0x1F9B2, 0x1F9B3}


def parse(text: str):
    """Yield (chinese_label, emoji) pairs in document order."""
    current_label = None
    line_re = re.compile(r"^([0-9A-Fa-f ]+)\s*;\s*([\w-]+)\s*#\s*(\S+)")

    for line in text.splitlines():
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
            current_label = GROUPS.get(group)
            continue
        if not line or line.startswith("#"):
            continue
        if current_label is None:
            continue

        m = line_re.match(line)
        if not m:
            continue
        codes_str, status, emoji = m.group(1), m.group(2), m.group(3)
        if status != "fully-qualified":
            continue

        codes = [int(c, 16) for c in codes_str.split()]
        if any(c in SKIN_TONES for c in codes):
            continue
        if any(c in HAIR_MODS for c in codes):
            continue

        yield current_label, emoji


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    pairs = list(parse(src.read_text(encoding="utf-8")))

    counts = {}
    last_label = None
    out_lines = []
    for label, emoji in pairs:
        if label != last_label:
            out_lines.append(f"# group: {label}")
            last_label = label
        out_lines.append(emoji)
        counts[label] = counts.get(label, 0) + 1

    dst.write_text("\n".join(out_lines) + "\n", encoding="utf-8")

    total = sum(counts.values())
    print(f"Wrote {total} emojis to {dst}", file=sys.stderr)
    for label, n in counts.items():
        print(f"  {label}: {n}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
