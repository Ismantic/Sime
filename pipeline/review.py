#!/usr/bin/env python3
"""评测 Sime 输入法准确率

评测集格式:
  拼音模式: pinyin\thanzi
  数字模式: nums\tpinyin\thanzi

用法:
  python3 review.py --cases cases.1.txt
  python3 review.py --cases cases.num.1.txt --num
"""
import argparse
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", default="cases.1.txt")
    parser.add_argument("--exe", default="../build/sime")
    parser.add_argument("--dict", default="output/sime.dict")
    parser.add_argument("--cnt", default="output/sime.cnt")
    parser.add_argument("--num", action="store_true", help="num-key mode")
    parser.add_argument("-s", "--sentence", action="store_true", help="sentence mode")
    parser.add_argument("--cache", action="store_true",
                        help="use cache-backed sentence decoders")
    parser.add_argument("--bench-append", action="store_true",
                        help="benchmark append-only prefix latency")
    parser.add_argument("--repeats", type=int, default=1,
                        help="repeat benchmark N times")
    parser.add_argument("--exact-len", type=int, default=0,
                        help="only use cases whose query length equals N")
    parser.add_argument("--errors", type=int, default=10)
    parser.add_argument("--limit", type=int, default=0,
                        help="max cases to evaluate (0=all)")
    args = parser.parse_args()

    # Read test cases
    queries = []
    pinyins = []
    golds = []
    with open(args.cases) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            parts = line.split("\t")
            if args.num and len(parts) >= 3:
                queries.append(parts[0])
                pinyins.append(parts[1])
                golds.append(parts[2])
            elif len(parts) >= 2:
                queries.append(parts[0])
                pinyins.append(parts[0])
                golds.append(parts[1])
    if args.exact_len > 0:
        filtered = [
            (q, p, g)
            for q, p, g in zip(queries, pinyins, golds)
            if len(q) == args.exact_len
        ]
        queries = [q for q, _, _ in filtered]
        pinyins = [p for _, p, _ in filtered]
        golds = [g for _, _, g in filtered]
    if args.limit > 0:
        queries = queries[:args.limit]
        pinyins = pinyins[:args.limit]
        golds = golds[:args.limit]

    mode = "num" if args.num else "pinyin"
    print(f"Test set: {args.cases} ({len(queries)} cases, mode={mode})",
          file=sys.stderr)

    # Run Sime decoder
    cmd = [args.exe, "--dict", args.dict, "--cnt", args.cnt, "-n", "1"]
    if args.sentence:
        cmd.append("-s")
    if args.num:
        cmd.extend(["--num", "-s"])
    if args.cache:
        cmd.append("--cache")

    if args.bench_append:
        stream = []
        total_prefixes = 0
        for q in queries:
            for i in range(1, len(q) + 1):
                stream.append(q[:i])
                total_prefixes += 1
        input_text = "\n".join(stream) + "\n:quit\n"
        times = []
        for _ in range(max(args.repeats, 1)):
            t0 = time.perf_counter()
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            proc.communicate(input_text.encode())
            times.append(time.perf_counter() - t0)
        best = min(times)
        avg = sum(times) / len(times)
        print(f"Prefixes: {total_prefixes}")
        print(f"Best time: {best:.6f}s")
        print(f"Avg time: {avg:.6f}s")
        print(f"Best ms/prefix: {best * 1000 / max(total_prefixes, 1):.4f}")
        print(f"Avg ms/prefix: {avg * 1000 / max(total_prefixes, 1):.4f}")
        return

    input_text = "\n".join(queries) + "\n:quit\n"
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    stdout, _ = proc.communicate(input_text.encode())

    predictions = []
    for line in stdout.decode(errors="replace").split("\n"):
        line = line.strip()
        if "[0]" in line:
            text = line.split("[0]", 1)[1].strip()
            if " [" in text:
                text = text[:text.index(" [")]
            text = text.split("(score")[0].strip()
            predictions.append(text)
        elif "no candidates" in line:
            predictions.append("")

    n = min(len(predictions), len(golds))
    if len(predictions) != len(golds):
        print(f"Warning: predictions ({len(predictions)}) != golds ({len(golds)})",
              file=sys.stderr)

    # Score
    total_chars = 0
    correct_chars = 0
    sentence_correct = 0

    for i in range(n):
        pred = predictions[i]
        gold = golds[i]
        sent_ok = True
        for j in range(min(len(pred), len(gold))):
            total_chars += 1
            if pred[j] == gold[j]:
                correct_chars += 1
            else:
                sent_ok = False
        total_chars += abs(len(pred) - len(gold))
        if len(pred) != len(gold):
            sent_ok = False
        if sent_ok:
            sentence_correct += 1

    char_acc = correct_chars / total_chars if total_chars > 0 else 0
    sent_acc = sentence_correct / n if n > 0 else 0

    print(f"Sentences: {n}")
    print(f"Char accuracy: {correct_chars}/{total_chars} = {char_acc:.4f} ({char_acc*100:.2f}%)")
    print(f"Sentence accuracy: {sentence_correct}/{n} = {sent_acc:.4f} ({sent_acc*100:.2f}%)")

    # Show errors
    if args.errors > 0:
        print(f"\nFirst {args.errors} errors:")
        shown = 0
        for i in range(n):
            if shown >= args.errors:
                break
            if predictions[i] != golds[i]:
                print(f"  [{i}] query:  {queries[i][:60]}")
                if args.num:
                    print(f"       pinyin: {pinyins[i][:60]}")
                print(f"       gold:   {golds[i]}")
                print(f"       pred:   {predictions[i]}")
                shown += 1


if __name__ == "__main__":
    main()
