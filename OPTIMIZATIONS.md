# Sime IME Performance Optimizations

This document describes the compilation and memory alignment optimizations applied to the Sime input method engine.

## Optimizations Applied

### 1. Compilation Optimizations

The following compiler flags have been added for Release builds in `CMakeLists.txt`:

- **`-O3`**: Maximum optimization level
  - Enables aggressive inlining, loop optimizations, and vectorization
  - Balances performance with code size

- **`-march=native -mtune=native`**: CPU-specific optimizations
  - Generates code optimized for the current CPU architecture
  - Enables SIMD instructions (SSE, AVX, AVX2) if available
  - **Note**: Binaries compiled with these flags may not run on older CPUs

- **`-funroll-loops`**: Loop unrolling
  - Reduces loop overhead by executing multiple iterations per loop cycle
  - Particularly beneficial for the nested loops in `Interpreter::Process()`

- **`-finline-functions`**: Aggressive function inlining
  - Reduces function call overhead for small, frequently called functions
  - Benefits `Scorer::ScoreMove()`, `Trie::DoMove()`, etc.

- **`-ffast-math`**: Relaxed floating-point math
  - Allows optimizations that may slightly affect floating-point precision
  - Improves performance of score calculations
  - **Trade-off**: May affect reproducibility across different platforms

- **`-flto`**: Link-Time Optimization
  - Enables whole-program optimization across translation units
  - Applied to both compilation and linking stages
  - Can significantly improve performance of cross-module calls

### 2. Memory Alignment Optimizations

Cache-aligned data structures have been added to improve CPU cache efficiency and avoid false sharing.

#### State Structure (`include/state.h`)
```cpp
struct alignas(64) State {
    SentenceScore score = 0.0;           // 8 bytes
    std::size_t frame_index = 0;         // 8 bytes
    const State* backtrace = nullptr;    // 8 bytes
    Scorer::State scorer_state{};        // 8 bytes
    TokenID backtrace_token = 0;         // 4 bytes
    // Total: 36 bytes, padded to 64 bytes
};
```

**Benefits**:
- Aligned to cache line size (64 bytes) to avoid false sharing
- When multiple threads or operations work with different States, they won't invalidate each other's cache
- Improves performance in the beam search hot path

#### Scorer::State (`include/score.h`)
```cpp
struct alignas(8) State {
    std::uint32_t level = 0;  // 4 bytes
    std::uint32_t index = 0;  // 4 bytes
    // Total: 8 bytes (naturally aligned)
};
```

**Benefits**:
- Ensures natural alignment for efficient map operations
- Frequently used as map key in `NetStates`
- 8-byte alignment optimizes comparison operations

#### Trie Structures (`include/trie.h`)
```cpp
struct alignas(8) Move {
    std::uint32_t i = 0;    // 4 bytes
    Unit unit{};            // 4 bytes
};

struct alignas(8) Entry {
    std::uint32_t i = 0;      // 4 bytes
    std::uint8_t cost = 0;    // 1 byte
    std::uint8_t empty[3]{};  // 3 bytes
};

struct alignas(4) Node {
    std::uint16_t count = 0;
    std::uint16_t move_count = 0;
};
```

**Benefits**:
- Optimizes binary search operations in `Trie::DoMove()`
- Improves cache line utilization during dictionary lookup
- 8-byte alignment allows potential SIMD optimizations

#### Language Model Structures (`include/score.h`)
```cpp
struct alignas(32) NodeEntry {
    TokenID id = 0;            // 4 bytes
    std::uint32_t child = 0;   // 4 bytes
    std::uint32_t bow = 0;     // 4 bytes
    std::uint32_t pr = 0;      // 4 bytes
    std::uint32_t bon = 0;     // 4 bytes
    std::uint32_t boe = 0;     // 4 bytes
    // Total: 24 bytes, padded to 32 bytes
};

struct alignas(16) LeaveEntry {
    TokenID id = 0;          // 4 bytes
    std::uint32_t pr = 0;    // 4 bytes
    std::uint32_t bon = 0;   // 4 bytes
    std::uint32_t boe = 0;   // 4 bytes
    // Total: 16 bytes
};
```

**Benefits**:
- Optimizes language model queries during backoff chain traversal
- 32-byte alignment for NodeEntry allows cache prefetching
- Reduces cache misses during `Scorer::RawMove()`

## Expected Performance Improvements

Based on the optimizations applied:

1. **Compilation optimizations**: 30-50% speedup
   - `-O3` and `-march=native`: 20-30%
   - `-flto`: 5-10%
   - `-funroll-loops` and `-finline-functions`: 5-10%

2. **Memory alignment**: 10-20% speedup
   - Reduced false sharing in State vectors
   - Better cache utilization in hot loops
   - Improved memory access patterns

**Combined expected improvement**: 40-70% faster decoding

## Build Instructions

### Release Build (Optimized)
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Debug Build (No optimizations)
```bash
mkdir build-debug
cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

## Verification

To verify optimizations are applied:

```bash
# Check compiler flags
grep CMAKE_CXX_FLAGS_RELEASE build/CMakeCache.txt

# Verify binary size and stripping status
size build/ime_interpreter

# Test functionality
echo "nihao" | build/ime_interpreter --pydict pydict_sc.ime.bin --lm lm_sc.t3g
```

## Compatibility Notes

### CPU Architecture
The `-march=native` flag generates code optimized for the build machine's CPU. Binaries may not run on older CPUs. To create portable binaries:

```bash
# Replace -march=native with a specific target
cmake -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=x86-64-v2" ..
```

Architecture levels:
- `x86-64`: Baseline (2003, SSE2)
- `x86-64-v2`: 2009, SSE4.2, POPCNT
- `x86-64-v3`: 2013, AVX2, BMI2, FMA
- `x86-64-v4`: 2017, AVX-512

### ABI Warning
When compiling with 64-byte aligned structures, GCC may emit:
```
note: the ABI for passing parameters with 64-byte alignment has changed in GCC 4.6
```

This is **expected** and **harmless** for this project since:
- All code is compiled together (no ABI mixing)
- The alignment is intentional for performance
- No external libraries are affected

## Trade-offs

### Fast Math (`-ffast-math`)
- **Pro**: Faster floating-point operations
- **Con**: May affect precision in edge cases
- **Impact**: Minimal for IME scoring (relative comparisons)

### Link-Time Optimization (`-flto`)
- **Pro**: Cross-module optimizations
- **Con**: Longer build times
- **Con**: Larger memory usage during linking

### Memory Alignment
- **Pro**: Better cache performance
- **Con**: Increased memory usage (padding)
- **Impact**: ~28 bytes per State (64-36), acceptable for beam width of 48

## Future Optimizations

See `OPTIMIZATIONS_PLAN.md` for additional optimization opportunities:
- SIMD vectorization of scoring
- Hash-based state management
- Backoff result caching
- Profile-Guided Optimization (PGO)

## Benchmarking

To benchmark performance improvements, build both Debug and Release versions:

```bash
# Debug build
time (cat test_sentences.txt | build-debug/ime_interpreter ...)

# Release build
time (cat test_sentences.txt | build/ime_interpreter ...)
```

Compare execution times to verify optimizations.
