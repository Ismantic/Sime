#!/usr/bin/env python3
"""Extract plain text sentences from Wikipedia JSON corpus."""

import json
import re
import sys

def split_sentences(text):
    text = text.strip()
    parts = re.split(r'[。！？\n]+', text)
    for p in parts:
        p = p.strip('\u3000 \t')
        if len(p) < 4 or len(p) > 200:
            continue
        chinese = len(re.findall(r'[\u4e00-\u9fff]', p))
        if chinese < len(p) * 0.4:
            continue
        yield p

def main():
    src = sys.argv[1]
    out = sys.argv[2]
    with open(src, 'r', encoding='utf-8') as f:
        data = json.load(f)
    total = 0
    with open(out, 'w', encoding='utf-8') as fout:
        for obj in data:
            text = obj.get('completion', '')
            if not text:
                continue
            for sent in split_sentences(text):
                fout.write(sent + '\n')
                total += 1
    print(f"Total: {total} sentences", file=sys.stderr)

if __name__ == '__main__':
    main()
