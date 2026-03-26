#!/usr/bin/env python3
"""从 tokenizer.model 提取 piece 并标注拼音，生成 pinyin_dict 格式"""
import sys
import re
from pypinyin import pinyin, Style

def is_chinese_char(ch):
    cp = ord(ch)
    return (0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF or
            0x20000 <= cp <= 0x2A6DF or 0x2A700 <= cp <= 0x2B73F or
            0x2B740 <= cp <= 0x2B81F or 0x2B820 <= cp <= 0x2CEAF or
            0xF900 <= cp <= 0xFAFF or 0x2F800 <= cp <= 0x2FA1F)

def is_all_chinese(text):
    return len(text) > 0 and all(is_chinese_char(ch) for ch in text)

def get_pinyin(text):
    """获取纯中文文本的拼音，返回拼音字符串或 None
    单字返回所有多音字读音（空格分隔），多字返回单一读音（'分隔音节）"""
    if not is_all_chinese(text):
        return None
    if len(text) == 1:
        # 单字：获取所有读音
        py = pinyin(text, style=Style.NORMAL, heteronym=True)
        readings = []
        for p in py[0]:
            if re.match(r'^[a-z]+$', p) and p not in readings:
                readings.append(p)
        return ' '.join(readings) if readings else None
    else:
        # 多字：取默认读音，用'连接
        py = pinyin(text, style=Style.NORMAL, heteronym=False)
        parts = [p[0] for p in py]
        for p in parts:
            if not re.match(r'^[a-z]+$', p):
                return None
        return "'".join(parts)

def parse_model(model_file):
    """解析 tokenizer.model，提取 piece 列表"""
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
                pieces.append(piece)
    return pieces

def main():
    model_file = sys.argv[1] if len(sys.argv) > 1 else '/home/tfbao/Isma/IsmaTokenizer/data/tokenizer.model'
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'pinyin_token_dict.txt'

    pieces = parse_model(model_file)
    print(f"Total NORMAL pieces: {len(pieces)}")

    count = 0
    with open(output_file, 'w') as f:
        for piece in pieces:
            py = get_pinyin(piece)
            if py:
                f.write(f"{piece} {py}\n")
                count += 1
            else:
                # 没拼音的也要写入（只写 piece），保持与 freq_dict 行序一致
                f.write(f"{piece}\n")

    print(f"Pieces with pinyin: {count} / {len(pieces)}")
    print(f"Written to {output_file}")

if __name__ == '__main__':
    main()
