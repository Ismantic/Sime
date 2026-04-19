# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Sime (是语) — a pure C++20 Chinese pinyin input method engine using Modified Kneser-Ney N-gram language models with Viterbi beam search decoding. Supports Linux (Fcitx5 plugin) and Android (JNI).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For the Fcitx5 plugin: add `-DSIME_ENABLE_FCITX5=ON`, then `sudo cmake --install build` and `fcitx5 -r`.

Build outputs in `./build/`:
- `sime` — interactive decoder (used for manual testing)
- `sime-count` — n-gram counter (pipeline step 3)
- `sime-construct` — Modified Kneser-Ney LM builder (pipeline step 4)
- `sime-converter` — pinyin Trie builder (pipeline step 5)
- `sime-dump` — inspect a built `.cnt` LM file

The Android app uses its own Gradle/NDK build (see `Android/BUILD.md`), not the root CMake. The C++ engine under `src/` and `include/` is compiled via `Android/app/src/main/jni/CMakeLists.txt`.

## Testing

No automated test suite. Use the interactive interpreter to verify behavior:

```bash
./build/sime --dict pipeline/output/sime.dict --cnt pipeline/output/sime.raw.cnt
# Sentence mode: add -s
# T9 mode: add --num
```

Test case files: `pipeline/cases.1.txt`, `pipeline/cases.2.txt`, `pipeline/cases.num.1.txt`, `pipeline/cases.num.2.txt`.

## Training Pipeline

Run from `pipeline/` directory. Requires a pre-segmented corpus `sentences.cut.txt` and a pinyin table (variable `UNITS` in the Makefile, currently `units.20260408.txt`). Steps are sequential via Makefile targets:

```bash
cd pipeline
make cut        # 0. (optional) segment raw text via cut.py
make chars      # 1. Count corpus word frequencies
make dict       # 2. Generate token dict + pinyin dictionary
make count      # 3. Parallel n-gram counting (parallel_count.sh)
make construct  # 4. Build Kneser-Ney language model
make convert    # 5. Build pinyin Trie
```

Outputs: `pipeline/output/sime.dict` and `pipeline/output/sime.raw.cnt`. Training tools reference `../build/`, so the C++ CLI tools must be built first.

**Sibling pipelines** (same structure, separate Makefiles):
- `pipeline/en/` — English LM (`sime.en.dict`, `sime.en.raw.cnt`). Uses a char-level cut and its own `gen_token_dict.py`.
- `pipeline/nine/` — T9/nine-key bigram LM (`sime.nine`). Builds a pinyin-syllable corpus from sentences and trains a 2-gram model.

## Architecture

**Core library** (`sime_core`): `include/` has headers, `src/` has implementations.

- **Common types** (`common.h`): shared enums, token-ID constants, pinyin encoding primitives.
- **Pinyin dictionary** (`dict.h/cc` + `dict.inc`): embedded pinyin syllable table (`dict.inc` is generated, compiled in).
- **Trie** (`trie.h/cc`): binary pinyin-to-character prefix Trie with `ArrayUnit` nodes, loaded from `.dict` files produced by `sime-converter`.
- **Counting** (`count.h/cc`): external-sort n-gram counter used by `sime-count`.
- **Construction** (`construct.h/cc`): Modified Kneser-Ney smoothing + entropy pruning used by `sime-construct`.
- **Conversion** (`convert.h/cc`): builds the pinyin Trie; driver for `sime-converter`.
- **Scorer** (`score.h/cc`): n-gram LM probability lookups with backoff.
- **Decoder** (`sime.h/cc`): lattice construction + Viterbi beam search for both full-keyboard pinyin and T9/nine-key input (`DecodeNumStr`, `DecodeNumSentence`). Key constants: `NodeSize=40`, `BeamSize=60`.
- **State** (`state.h/cc`): beam search state heap.
- **UTF-8** (`ustr.h/cc`): UTF-8 string utilities.

**CLI tools** (`bin/`): each tool is a thin entry point linking `sime_core` — `counter.cc`, `constructor.cc`, `converter.cc`, `dump.cc`, `sime.cc`.

**Platform layers**:
- `Linux/fcitx5/` — Fcitx5 engine plugin (`sime.cc`, `sime-state.cc`)
- `Android/` — Java IME service + JNI bridge (`app/src/main/jni/sime_jni.cc`)

## Language Model Conventions

**Path B convention (aligned with libime)**: the training pipeline does **not**
emit `<s>` / `</s>` tokens. The corpus is treated as a stream of fragments;
each line/sentence simply resets the sliding window so n-grams don't cross
boundaries. Only `<unk>` (id 12) carries special meaning.

Implications:
- `sime-count` never writes records containing token ids 10 or 11.
- Decoder starts beam from root (`Scorer::StartPos()` returns `Pos{}`); first
  word is scored via unigram continuation probability.
- Cut/Prune only protect `<unk>` at unigram level.
- `<s>` / `</s>` constants exist in `common.h` but are no longer referenced by
  the training / scoring path.

**MKN smoothing**: interpolated Modified Kneser-Ney. Stored `pro` is `P_I(w|h)`
(already interpolated), stored `bow` is MKN gamma `γ(h)`. Query semantics
match KenLM's backoff-form storage via the "interpolated-fits-in-backoff"
trick.

**Entropy pruning**: Stolcke 1998 relative-entropy formula, aligned with
SRILM `NgramLM.cc::pruneProbs`. `CalcScore` computes `-ΔH` per removable
n-gram including the `P(h)` weighting and the `numerator·Δlog γ` term.

## Token ID Conventions

Special tokens: `NotToken=0`, `SentenceStart=10`, `SentenceEnd=11`,
`UnknownToken=12`. Vocabulary tokens start at ID 70 (`StartToken`). Under
Path B only `UnknownToken` is actually emitted into the LM.

## Compiler Settings

C++20 required. Strict warnings: `-Wall -Wextra -pedantic -Wconversion`. Core library is built with `POSITION_INDEPENDENT_CODE ON`.
