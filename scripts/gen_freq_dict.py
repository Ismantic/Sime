#!/usr/bin/env python3
"""从 tokenizer.model 生成 freq_dict.txt，格式：piece\tfreq"""
import sys

def main():
    model_file = sys.argv[1] if len(sys.argv) > 1 else '/home/tfbao/Isma/IsmaTokenizer/data/tokenizer.model'
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'freq_dict.txt'

    pieces = []
    in_pieces = False
    with open(model_file, 'r') as f:
        for line in f:
            line = line.rstrip('\n')
            if line == '[Pieces]':
                in_pieces = True
                continue
            if line.startswith('[') and line.endswith(']'):
                in_pieces = False
                continue
            if not in_pieces:
                continue
            if line.startswith('size='):
                continue
            parts = line.split('\t')
            if len(parts) >= 4:
                piece = parts[1]
                score = parts[2]
                pieces.append((piece, score))

    with open(output_file, 'w') as f:
        for piece, score in pieces:
            f.write(f"{piece}\t{score}\n")

    print(f"Written {len(pieces)} entries to {output_file}")

if __name__ == '__main__':
    main()
