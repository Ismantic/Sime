# GRU training pipeline

This directory is the cleaned entry point for the GRU reranker released in
Sime `v2026.08.13`. The fixed parameters are recorded in `config.json`; the
design and data policy are documented in
[`GRU_TRAINING.md`](GRU_TRAINING.md), and the final metrics are summarized in
[`GRU_EVALUATION.md`](GRU_EVALUATION.md).

`run.py` orchestrates the verified research implementations under
`next/scripts/`. Their historical filenames are retained so the released
checkpoints remain reproducible; product-facing code and artifacts use the
name GRU.

## Stages

1. Train a 32-dimensional fastText skip-gram embedding from the complete
   segmented Sime corpus.
2. Remove exact evaluation `(pinyin, gold)` pairs from the conversational
   corpus.
3. Generate Sime Top-10 candidates for full pinyin and T9.
4. Mix general-domain and conversational hard cases, then train independent
   one-layer bidirectional GRU rankers.
5. Export one shared row-wise INT8 embedding and two FP16 ncnn graphs.
6. Evaluate the exported ncnn artifacts on all six sets.

Large corpora, candidate JSON, checkpoints and generated models must stay out
of Git. Put them in a separate work directory.

## Environment

```bash
python -m venv .venv
.venv/bin/pip install -r pipeline/gru/requirements.txt
```

Native fastText is used to train the embedding. `pnnx` and the Python ncnn
binding are needed only for export and verification.

## Entry point

First inspect the complete command sequence without running it:

```bash
python pipeline/gru/run.py \
  --work /path/to/gru-work \
  --general /path/to/general-candidates.json \
  --touchpal /path/to/touchpal-candidates.json \
  --evaluation /path/to/all.top10.json \
  --embedding /path/to/fasttext-token-embedding.pt \
  --dry-run
```

Remove `--dry-run` to execute it. Inputs and the work directory are always
explicit; the pipeline does not depend on `/tmp` or a particular home path.

## Release outputs

The verified artifacts are renamed to:

```text
gru.embedding.i8
gru.pinyin.ncnn.param
gru.pinyin.ncnn.bin
gru.t9.ncnn.param
gru.t9.ncnn.bin
```

They must be installed beside `sime.dict` and `sime.cnt`.
