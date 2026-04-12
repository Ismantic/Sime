#!/usr/bin/env python3
"""Merge multiple sorted binary n-gram files produced by sime-count.

Each record is N x uint32 (token IDs) + 1 x uint32 (count).
Files must be sorted by token IDs (ascending). Same-key counts are summed.

Usage: merge_ngram.py -n <ngram_order> -o <output> input1 input2 ...
"""

import argparse
import heapq
import struct
import sys


def record_iter(path: str, rec_size: int, fmt: str):
    """Yield (key_tuple, count) from a binary n-gram file."""
    with open(path, "rb") as f:
        while True:
            data = f.read(rec_size)
            if len(data) < rec_size:
                break
            vals = struct.unpack(fmt, data)
            key = vals[:-1]
            cnt = vals[-1]
            yield key, cnt


def main():
    parser = argparse.ArgumentParser(description="Merge sorted binary n-gram files")
    parser.add_argument("-n", type=int, required=True, help="n-gram order (1, 2, or 3)")
    parser.add_argument("-o", required=True, help="output file")
    parser.add_argument("inputs", nargs="+", help="input binary n-gram files")
    args = parser.parse_args()

    n = args.n
    rec_size = (n + 1) * 4  # n x uint32 + 1 x uint32
    fmt = f"<{n + 1}I"      # little-endian unsigned ints

    # Build heap: (key, count, file_index)
    iters = []
    for i, path in enumerate(args.inputs):
        import os
        fsize = os.path.getsize(path)
        print(f"  input[{i}]: {path} size={fsize} bytes ({fsize // rec_size} records)", file=sys.stderr)
        it = record_iter(path, rec_size, fmt)
        iters.append(it)

    # Use heapq.merge-style with file index for stable ordering
    heap = []
    for i, it in enumerate(iters):
        rec = next(it, None)
        if rec is not None:
            key, cnt = rec
            heap.append((key, cnt, i))
    heapq.heapify(heap)

    merged = 0
    with open(args.o, "wb") as out:
        cur_key = None
        cur_cnt = 0

        while heap:
            key, cnt, idx = heapq.heappop(heap)

            if key != cur_key:
                if cur_key is not None:
                    out.write(struct.pack(fmt, *cur_key, cur_cnt))
                    merged += 1
                cur_key = key
                cur_cnt = cnt
            else:
                cur_cnt += cnt

            # Advance the iterator
            rec = next(iters[idx], None)
            if rec is not None:
                nkey, ncnt = rec
                heapq.heappush(heap, (nkey, ncnt, idx))

        if cur_key is not None:
            out.write(struct.pack(fmt, *cur_key, cur_cnt))
            merged += 1

    print(f"merged {len(args.inputs)} files -> {merged} records", file=sys.stderr)


if __name__ == "__main__":
    main()
