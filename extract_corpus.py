#!/usr/bin/env python3
"""Extract plain text sentences from THUCNews JSONL files."""

import json
import re
import sys
import glob
import os

def split_sentences(text):
    """Split text into sentences by Chinese punctuation and newlines."""
    # Remove whitespace-only prefixes (e.g. \u3000\u3000)
    text = text.strip()
    # Split on sentence-ending punctuation and newlines
    parts = re.split(r'[。！？\n]+', text)
    for p in parts:
        p = p.strip()
        # Remove leading/trailing whitespace chars like \u3000
        p = p.strip('\u3000 \t')
        # Skip empty, too short, or too long
        if len(p) < 4 or len(p) > 200:
            continue
        # Skip lines with too many non-Chinese chars (URLs, codes, etc.)
        chinese = len(re.findall(r'[\u4e00-\u9fff]', p))
        if chinese < len(p) * 0.5:
            continue
        yield p

def main():
    src_dir = sys.argv[1]
    out_file = sys.argv[2]

    files = sorted(glob.glob(os.path.join(src_dir, '*.jsonl')))
    total = 0
    with open(out_file, 'w', encoding='utf-8') as fout:
        for f in files:
            name = os.path.basename(f)
            count = 0
            with open(f, 'r', encoding='utf-8') as fin:
                for line in fin:
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    # Use both title and content
                    for field in ['title', 'content']:
                        text = obj.get(field, '')
                        if not text:
                            continue
                        for sent in split_sentences(text):
                            fout.write(sent + '\n')
                            count += 1
            total += count
            print(f"  {name}: {count} sentences", file=sys.stderr)
    print(f"Total: {total} sentences", file=sys.stderr)

if __name__ == '__main__':
    main()
