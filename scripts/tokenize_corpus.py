#!/usr/bin/env python3
"""用 tokenizer.model 对语料做 tokenize，输出 token 序列供 sime-count 使用"""
import sys
import os

sys.path.insert(0, '/home/tfbao/Isma/IsmaTokenizer/build/python')
import isma_tokenizer

def main():
    model_file = sys.argv[1] if len(sys.argv) > 1 else '/home/tfbao/Isma/IsmaTokenizer/data/tokenizer.model'
    input_file = sys.argv[2] if len(sys.argv) > 2 else '/home/tfbao/Isma/IsmaCut/corpus_wikipedia.txt'
    output_file = sys.argv[3] if len(sys.argv) > 3 else 'corpus_tokenized.txt'

    tok = isma_tokenizer.Tokenizer()
    if not tok.load(model_file):
        print(f"Failed to load model: {model_file}", file=sys.stderr)
        sys.exit(1)

    print(f"Model: {model_file} (method={tok.method}, vocab={tok.vocab_size()})")
    print(f"Input: {input_file}")
    print(f"Output: {output_file}")

    line_count = 0
    with open(input_file, 'r') as fin, open(output_file, 'w') as fout:
        for line in fin:
            line = line.strip()
            if not line:
                continue
            pieces = tok.encode_as_pieces(line)
            fout.write(' '.join(pieces) + '\n')
            line_count += 1
            if line_count % 500000 == 0:
                print(f"Processed {line_count} lines")

    print(f"Done! {line_count} lines tokenized")

if __name__ == '__main__':
    main()
