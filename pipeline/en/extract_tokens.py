#!/usr/bin/env python3
"""Extract valid tokens from a SentencePiece piece file."""
import sys

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <piece.txt> <output.txt>", file=sys.stderr)
        sys.exit(1)

    in_pieces = False
    count = 0
    with open(sys.argv[1], errors='replace') as fin, open(sys.argv[2], 'w') as fout:
        for line in fin:
            line = line.rstrip('\n')
            if line == '[Pieces]':
                in_pieces = True
                continue
            if not in_pieces:
                continue
            if line.startswith('size='):
                continue
            parts = line.split('\t')
            if len(parts) < 2:
                continue
            p = parts[1]
            if p in ('<unk>', '<s>', '</s>', '<pad>'):
                continue
            if not p or not p.isprintable() or '\\x' in p or p.isspace():
                continue
            fout.write(p + '\n')
            count += 1
    print(f"extracted {count} tokens", file=sys.stderr)

if __name__ == '__main__':
    main()
