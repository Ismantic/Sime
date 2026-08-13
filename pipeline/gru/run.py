#!/usr/bin/env python3
"""Run the finalized Sime GRU training pipeline.

This is intentionally an orchestrator. The implementation modules remain
small, independently testable command-line programs in this directory.
"""

import argparse
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command, dry_run):
    print("+", " ".join(map(str, command)), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="pipeline/gru/config.json")
    parser.add_argument("--work", required=True)
    parser.add_argument("--sime", default="build/sime")
    parser.add_argument("--dict", default="pipeline/output/sime.dict")
    parser.add_argument("--cnt", default="pipeline/output/sime.cnt")
    parser.add_argument("--general", required=True)
    parser.add_argument("--touchpal", required=True)
    parser.add_argument("--evaluation", required=True)
    parser.add_argument("--embedding", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    config = json.load(open(args.config, encoding="utf-8"))
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    common = [
        "--train", args.general,
        "--train", args.touchpal,
        "--eval", args.evaluation,
        "--pretrained-embedding", args.embedding,
        "--pretrained-key", "embedding",
        "--freeze-embedding",
        "--input-kind", "token",
        "--bidirectional",
        "--train-candidates", str(config["reranker"]["candidate_limit"]),
        "--eval-candidates", str(config["reranker"]["candidate_limit"]),
        "--embedding", str(config["embedding"]["dimension"]),
        "--hidden", str(config["reranker"]["hidden"]),
        "--learning-rate", str(config["reranker"]["learning_rate"]),
        "--weight-decay", str(config["reranker"]["weight_decay"]),
        "--seed", str(config["reranker"]["seed"]),
    ]
    for mode in ("pinyin", "t9"):
        run([
            sys.executable, "next/scripts/train_tiny_teacher.py",
            "--mode", mode,
            "--epochs", str(config[mode]["epochs"]), *common,
            "--output", str(work / f"gru.{mode}.pt"),
        ], args.dry_run)
        run([
            sys.executable, "next/scripts/export_tiny_teacher_int8_ncnn.py",
            "--checkpoint", str(work / f"gru.{mode}.pt"),
            "--output", str(work / f"gru.{mode}"),
            "--fp16",
        ], args.dry_run)
    run([
        sys.executable, "next/scripts/eval_tiny_teacher_ncnn.py",
        "--eval", args.evaluation,
        "--embedding-int8", str(work / "gru.pinyin.embedding.i8"),
        "--param", str(work / "gru.pinyin.ncnn.param"),
        "--bin", str(work / "gru.pinyin.ncnn.bin"),
        "--scale", str(config["pinyin"]["scale"]),
        "--t9-param", str(work / "gru.t9.ncnn.param"),
        "--t9-bin", str(work / "gru.t9.ncnn.bin"),
        "--t9-scale", str(config["t9"]["scale"]),
        "--candidate-limit", str(config["reranker"]["candidate_limit"]),
    ], args.dry_run)


if __name__ == "__main__":
    main()
