# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Sime (是语输入法) is a Chinese Input Method Engine (IME) experimental project. It converts pinyin input to Chinese characters using a trie-based dictionary and statistical language model (n-gram).

The system uses:
- Sunpinyin dictionary data (`pydict_sc.bin`) converted to a custom trie format
- Sunpinyin language model (`lm_sc.t3g`) for scoring candidate sequences
- Viterbi-style decoding with beam search for optimal candidate selection

## Build System

The project uses CMake with C++20 and includes aggressive optimizations for Release builds:

```bash
# Optimized Release build (recommended)
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Debug build (for development)
mkdir build-debug
cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

**Release build optimizations**:
- `-O3`: Maximum optimization level
- `-march=native`: CPU-specific optimizations (AVX2, etc.)
- `-flto`: Link-time optimization
- `-ffast-math`: Relaxed floating-point math
- Memory-aligned data structures (64-byte cache line alignment)

See `OPTIMIZATIONS.md` for detailed information about performance optimizations.

This produces several executables:
- `ime_interpreter` - Interactive pinyin-to-hanzi converter (main demo application)
- `ime_cutter` - Text cutting utility
- `sime-segment` - Text segmentation tool
- `sime-count` - Token counting utility
- `sime-construct` - Dictionary construction tool
- `sime-converter` - Format conversion utility
- `trie_conv` (standalone) - Dictionary format converter

Build from repository root:
```bash
# Build all targets
cd build && make

# Rebuild after changes
cd build && make
```

## Data Preparation

Required before running the IME:

```bash
# Install Sunpinyin data files (Arch Linux)
sudo pacman -S sunpinyin-data
cp /usr/share/sunpinyin/pydict_sc.bin .
cp /usr/share/sunpinyin/lm_sc.t3g .

# Convert dictionary to internal trie format
g++ trie_conv.cc -o trie_conv
./trie_conv --input pydict_sc.bin --output pydict_sc.ime.bin
```

The conversion step (`trie_conv`) is required because the original Sunpinyin dictionary format is restructured for faster lookups.

## Running the IME

```bash
cd build
./ime_interpreter --pydict ../pydict_sc.ime.bin --lm ../lm_sc.t3g --nbest 5
```

Options:
- `--pydict` / `-d`: Path to converted dictionary file
- `--lm` / `-l`: Path to language model file
- `--nbest`: Number of candidate results to display (default: 5)

Interactive commands:
- Type pinyin and press Enter to get candidates
- `:quit` or `:q` to exit

## Architecture

### Core Pipeline

The decoding process follows this flow:
1. **Input parsing** (`UnitParser` in `unit.h/cc`): Converts pinyin strings to phonetic `Unit` structures (Initial + Accent + Tone)
2. **Lattice construction** (`Interpreter` in `interpret.h/cc`): Builds a graph of possible segmentations using the `Trie`
3. **Scoring** (`Scorer` in `score.h/cc`): Language model assigns probabilities to token sequences
4. **Beam search** (`NetStates` in `state.h/cc`): Viterbi-style decoding maintains top hypotheses
5. **Backtrace** (`Interpreter::Backtrace`): Recovers best path from final states

### Key Data Structures

**Unit** (`unit.h/cc`): Encodes pinyin syllables as packed 32-bit values containing:
- I (Initial): Consonant sound (8 bits)
- A (Accent): Vowel/rhyme (8 bits)
- T (Tone): Tone marker (4 bits)

**Trie** (`trie.h/cc`): Prefix tree for pinyin-to-token lookup
- Loaded from converted dictionary (`pydict_sc.ime.bin`)
- Each node has Move transitions (syllable → child node) and Entry terminals (token ID + cost)
- Supports multi-syllable word matching

**Scorer** (`score.h/cc`): N-gram language model (typically trigram)
- Loaded from `lm_sc.t3g` format
- Maintains hierarchical state (level + index) for context
- Returns log probabilities for token sequences
- Handles backoff smoothing for unseen n-grams

**State** (`state.h/cc`): Hypothesis in beam search
- Tracks accumulated score, frame position, backtrace pointer
- `NetStates` manages beam of hypotheses grouped by `Scorer::State`
- Beam width and pruning thresholds control search space

**Interpreter** (`interpret.h/cc`): Main decoding coordinator
- Builds lattice from input units using Trie
- Expands states through lattice using Scorer
- Returns n-best results with scores

### Supporting Components

**Dict** (`dict.h/cc`): Simple trie-based dictionary for text segmentation (different from main Trie)
- Used by `Segmenter` for preprocessing text
- Not used in main IME pipeline

**Segmenter** (`segment.h/cc`): Tokenizes Chinese text using Dict
- Outputs token IDs or segmented text
- Used for preparing training data

**Sentence** (`sentence.h`): Utilities for sentence boundary detection

**Ustr** (`ustr.h/cc`): UTF-8 ↔ UTF-32 conversion utilities

## Code Organization

```
include/          Public headers defining core interfaces
src/              Implementation files
app/              Executable entry points (main() functions)
trie_conv.cc      Standalone dictionary converter
```

Core library: `sime_core` (built from `src/*.cc`)
Executables: Link against `sime_core`

## Development Notes

### Token IDs
- `kNotToken` (0): Invalid/missing token
- `kSentenceToken` (10): Sentence boundary marker
- `kRealTokenStart` (70): Actual word tokens begin here

These constants appear in `common.h` and are used throughout for special token handling.

### Scoring Convention
Scores are negative log probabilities, negated (higher = better). This explains why `State::operator<` compares scores directly and `NetStates` maintains a max-heap structure.

### Memory-Mapped Binary Formats
Both Trie and Scorer load large binary blobs into memory (`blob_` vectors) and use pointer arithmetic for fast access. The format is little-endian by default but `trie_conv` supports `--endian` flag.

### Compiler Flags
All targets compile with `-Wall -Wextra -pedantic -Wconversion` to catch common errors.
