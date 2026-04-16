# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Sime (是语) — a pure C++20 Chinese pinyin input method engine using Modified Kneser-Ney N-gram language models with Viterbi beam search decoding. Supports Linux (Fcitx5 plugin) and Android (JNI).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For the Fcitx5 plugin: add `-DSIME_ENABLE_FCITX5=ON`.

Build outputs in `./build/`: `sime-count`, `sime-construct`, `sime-converter`, `sime`, `sime-dump`.

## Testing

No automated test suite. Use the interactive interpreter to verify behavior:

```bash
./build/sime --dict pipeline/output/sime.dict --cnt pipeline/output/sime.raw.cnt
# Sentence mode: add -s
# T9 mode: add --num
```

Test case files: `pipeline/cases.1.txt`, `pipeline/cases.2.txt`, `pipeline/cases.num.1.txt`, `pipeline/cases.num.2.txt`.

## Training Pipeline

Run from `pipeline/` directory. Requires `sentences.cut.txt` (pre-segmented corpus) and `chinese_units.txt` (pinyin table). Steps are sequential via Makefile targets:

```bash
cd pipeline
make chars      # 1. Count corpus word frequencies
make dict       # 2. Generate token dict + pinyin dictionary
make count      # 3. Parallel n-gram counting
make construct  # 4. Build Kneser-Ney language model
make convert    # 5. Build pinyin Trie
```

Outputs: `pipeline/output/sime.dict` and `pipeline/output/sime.raw.cnt`. Training tools reference `../build/` so the C++ tools must be built first.

## Architecture

**Core library** (`sime_core`): `include/` has headers, `src/` has implementations.

- **Encoding**: `unit.h/cc` — pinyin syllables packed into 20-bit `Unit` structs (initial + final + tone)
- **Trie**: `trie.h/cc` — binary pinyin-to-character prefix trie, loaded from `.trie` files
- **Scorer**: `score.h/cc` — n-gram LM probability lookups with backoff
- **Decoder**: `sime.h/cc` — lattice construction + Viterbi beam search. Key constants: `NodeSize=40`, `BeamSize=60`
- **State**: `state.h/cc` — beam search state heap (`NetStates`)
- **Construction**: `construct.h/cc` — Modified Kneser-Ney with entropy pruning
- **T9**: `nine.h/cc` — digit-to-pinyin decoder for nine-key input

**CLI tools** (`bin/`): Each tool is a thin entry point linking `sime_core`.

**Platform layers**:
- `Linux/fcitx5/` — Fcitx5 engine plugin (`sime.cc`, `sime-state.cc`)
- `Android/` — Java IME service + JNI bridge (`app/src/main/jni/sime_jni.cc`)

## Token ID Conventions

Special tokens: `NotToken=0`, `SentenceStart=10`, `SentenceEnd=11`, `UnknownToken=12`. Vocabulary tokens start at ID 70 (`StartToken`).

## Compiler Settings

C++20 required. Strict warnings: `-Wall -Wextra -pedantic -Wconversion`. Core library is built with `POSITION_INDEPENDENT_CODE ON`.
