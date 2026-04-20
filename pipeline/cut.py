#!/usr/bin/env python3
"""Parallel word segmentation using iscut."""

import argparse
import os
import sys
from multiprocessing import Pool

import iscut

_cutter = None


def _init_worker(dict_path: str):
    global _cutter
    if dict_path:
        _cutter = iscut.Cutter(dict_path)
    else:
        _cutter = iscut.Cutter()


def _cut_line(line: str) -> str:
    line = line.rstrip("\n")
    if not line:
        return ""
    return " ".join(_cutter.cut(line))


BATCH = 4096


def main():
    ap = argparse.ArgumentParser(description="Parallel segmentation with iscut")
    ap.add_argument("--dict", default="", help="dictionary file (word\\tfreq); omit for char-level segmentation")
    ap.add_argument("input", help="input text file")
    ap.add_argument("output", help="output segmented file")
    ap.add_argument("-j", type=int, default=os.cpu_count(),
                    help="number of processes (default: all cores)")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8", errors="replace") as fin, \
         open(args.output, "w", encoding="utf-8") as fout, \
         Pool(args.j, initializer=_init_worker, initargs=(args.dict,)) as pool:
        n = 0
        for result in pool.imap(_cut_line, fin, chunksize=BATCH):
            fout.write(result + "\n")
            n += 1
            if n % 500000 == 0:
                print(f"  {n} lines", file=sys.stderr)

    print(f"done: {n} lines -> {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
