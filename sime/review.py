#!/usr/bin/env python3
"""评测 Sime 输入法的拼音→汉字准确率

评测集格式: 拼音\t汉字（每行一条）

用法: python3 review.py [--cases cases.1.txt] [--exe ../build/ime_interpreter] [--dict output/sime.dict] [--cnt output/sime.cnt]
"""
import argparse
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", default="cases.1.txt", help="test cases file (pinyin\\thanzi)")
    parser.add_argument("--exe", default="../build/ime_interpreter")
    parser.add_argument("--dict", default="output/sime.dict")
    parser.add_argument("--cnt", default="output/sime.cnt")
    parser.add_argument("--errors", type=int, default=10, help="number of errors to show")
    args = parser.parse_args()

    # Read test cases
    queries = []
    golds = []
    with open(args.cases) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or "\t" not in line:
                continue
            py, gold = line.split("\t", 1)
            queries.append(py)
            golds.append(gold)

    print(f"Test set: {args.cases} ({len(queries)} cases)", file=sys.stderr)

    # Run Sime decoder
    proc = subprocess.Popen(
        [args.exe, "--dict", args.dict, "--cnt", args.cnt, "--num", "1"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    input_text = "\n".join(queries) + "\n:quit\n"
    stdout, _ = proc.communicate(input_text)

    # Parse output
    predictions = []
    for line in stdout.split("\n"):
        line = line.strip()
        if "[0]" in line:
            text = line.split("[0]", 1)[1].strip()
            text = text.split("(score")[0].strip()
            text = text.replace("[", "").replace("]", "")
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
                print(f"  [{i}] query: {queries[i][:60]}")
                print(f"       gold: {golds[i]}")
                print(f"       pred: {predictions[i]}")
                shown += 1


if __name__ == "__main__":
    main()
