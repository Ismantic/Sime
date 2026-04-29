# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Sime (是语) — a pure C++20 Chinese pinyin input method engine using Interpolated Absolute Discounting (Ney 1994) N-gram language models with Viterbi beam search decoding. Supports Linux (Fcitx5 plugin), Android (JNI), and macOS (`macOS/` has its own CMake build + installer package).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For the Fcitx5 plugin: add `-DSIME_ENABLE_FCITX5=ON`, then `sudo cmake --install build` and `fcitx5 -r`.

Build outputs in `./build/`:
- `sime` — interactive decoder (used for manual testing)
- `sime-count` — n-gram counter (pipeline step 3)
- `sime-construct` — IAD LM builder (pipeline step 4)
- `sime-converter` — pinyin Trie builder (pipeline step 5)
- `sime-compact` — compacts a `.raw.cnt` into `.cnt` (final deployment format)
- `sime-dump` — inspect a built `.cnt` LM file
- `sime-cut` — Chinese text segmenter using the LM + dict

The Android app uses its own Gradle/NDK build (see `Android/BUILD.md`), not the root CMake. The C++ engine under `src/` and `include/` is compiled via `Android/app/src/main/jni/CMakeLists.txt`.

## Testing

No C++ unit-test suite. Use the interactive interpreter to verify engine behavior:

```bash
./build/sime --dict pipeline/output/sime.dict --cnt pipeline/output/sime.cnt
# Sentence mode: add -s
# T9 mode: add --num
# English mode: add --en
```

Use the compacted `.cnt` (not `.raw.cnt`) — that's the deployment format and matches what platform builds load. Test case files: `pipeline/cases.1.txt`, `pipeline/cases.2.txt`, `pipeline/cases.num.1.txt`, `pipeline/cases.num.2.txt`.

Android does ship JUnit tests under `Android/app/src/test/java/` (buffer state, candidate selection, mode switching). Run from `Android/`:

```bash
./gradlew testDebugUnitTest
```

## Training Pipeline

Run from `pipeline/` directory. Requires a pre-segmented corpus `sentences.cut.txt` and a pinyin table (`UNITS` variable in the Makefile, defaults to `dict.unit`). Steps are sequential via Makefile targets:

```bash
cd pipeline
make cut        # 0. (optional) segment raw text via cut.py
make chars      # 1. Count corpus word frequencies
make dict       # 2. Generate token dict + pinyin dictionary
make count      # 3. Parallel n-gram counting (parallel_count.sh)
make construct  # 4. Build IAD language model -> sime.raw.cnt
make compact    # 4.5 Strip bow/boe -> sime.cnt (deployment format)
make convert    # 5. Build pinyin Trie -> sime.dict
```

Outputs land in `$(OUT)`, which defaults to `pipeline/output-new/` in the Makefile (older `pipeline/output/` is sometimes referenced by docs/test commands — adjust paths to match wherever the active run wrote). Final deployable artifacts: `sime.dict` and `sime.cnt`. Training tools reference `../build/`, so the C++ CLI tools must be built first.

**Sibling pipelines** (same structure, separate Makefiles):
- `pipeline/en/` — English LM (`sime.en.dict`, `sime.en.raw.cnt`). Uses a char-level cut and its own `gen_token_dict.py`.
- `pipeline/nine/` — T9/nine-key bigram LM (`sime.nine`). Builds a pinyin-syllable corpus from sentences and trains a 2-gram model.

## Architecture

**Core library** (`sime_core`): `include/` has headers, `src/` has implementations.

- **Common types** (`common.h`): `TokenID`, `TokenMap`, `LoadTokenMap`. Token IDs start at 1 (`StartToken`); 0 is `NotToken`.
- **Dict** (`dict.h/cc` + `dict.inc`): pinyin dictionary with 4 double-array tries (`LetterPinyin`, `LetterEn`, `NumPinyin`, `NumEn`). `dict.inc` is the embedded pinyin syllable table.
- **Trie** (`trie.h/cc`): generic `DoubleArray` with exact/prefix/pinyin-aware/T9 search modes. Used by both `Dict` and `Cutter`.
- **Counting** (`count.h/cc`): external-sort n-gram counter used by `sime-count`.
- **Construction** (`construct.h/cc`): Interpolated Absolute Discounting (IAD) smoothing + entropy pruning used by `sime-construct`. Uses `NeyDiscounter` with per-order fixed D values (defaults: unigram=0.0005, bigram=0.5, trigram=0.5). CLI `-d` flag allows custom D values.
- **Conversion** (`convert.h/cc`): builds the pinyin Trie; driver for `sime-converter`.
- **Compact** (`compact.h/cc`): compacts raw `.cnt` LM files into deployment format; driver for `sime-compact`.
- **Scorer** (`score.h/cc`): n-gram LM probability lookups with backoff.
- **Decoder** (`sime.h/cc`): lattice construction + Viterbi beam search for both full-keyboard pinyin and T9/nine-key input (`DecodeNumStr`, `DecodeNumSentence`). Key constants in `include/sime.h`: `NodeSize=40`, `BeamSize=20`.
- **State** (`state.h/cc`): beam search state heap.
- **Cutter** (`cut.h/cc`): Chinese text segmenter reusing the Sime LM + dict via a byte-level DAT and Viterbi beam search.
- **UTF-8** (`ustr.h/cc`): UTF-8 string utilities.

**CLI tools** (`bin/`): each tool is a thin entry point linking `sime_core`.

**Platform layers**:
- `Linux/fcitx5/` — Fcitx5 engine plugin (`sime.cc`, `sime-state.cc`)
- `Android/` — Java IME service + JNI bridge (`app/src/main/jni/sime_jni.cc`). Java package is `com.shiyu.sime` (renamed from `com.semantic.sime` — older AGENTS.md references are stale).
- `macOS/` — IMK input method app + installer; built via `macOS/CMakeLists.txt`.

## Language Model Conventions

No `<s>`/`</s>` sentence boundary tokens. The corpus is treated as a stream of fragments; each line resets the sliding window so n-grams don't cross boundaries. Decoder starts beam from root; first word is scored via unigram continuation probability.

**IAD smoothing**: Interpolated Absolute Discounting (Ney 1994). All levels use raw counts with a single fixed discount D per order (no continuation counts). Stored `pro` is `P_I(w|h)` (already interpolated), stored `bow` is the interpolation gamma `γ(h)`. Bigram pruning uses count-weighted PMI; trigram pruning uses Stolcke KL.

**Entropy pruning**: Stolcke 1998 relative-entropy formula, aligned with SRILM `NgramLM.cc::pruneProbs`.

## Token ID Conventions

`NotToken=0` (empty/sentinel), `StartToken=1` (first real vocabulary token). Dict tokens are numbered sequentially from 1 in `sime.token.dict.txt` line order. `CutUnkToken = (uint32_t)-1` is used only by `Cutter` for OOV fragments.

## Deploying Assets to Platform Builds

Platform builds (Android/Fcitx5) need LM assets copied from pipeline output — these are gitignored:

```bash
cp pipeline/output/sime.cnt pipeline/output/sime.dict Android/app/src/main/assets/
cp pipeline/output/sime.cnt pipeline/output/sime.dict Linux/fcitx5/data/
```

## Compiler Settings

C++20 required. Strict warnings: `-Wall -Wextra -pedantic -Wconversion`. Core library is built with `POSITION_INDEPENDENT_CODE ON`.
