#!/bin/bash
# Benchmark script for testing back cache and batch processing optimizations

set -e

PYDICT="../pydict_sc.ime.bin"
LM="../lm_sc.t3g"
IME="./ime_interpreter"

# Test cases
TEST_INPUTS=(
    "nihao"
    "beijing"
    "zhongguo"
    "renminribao"
    "gongyewulianguanli"
    "zhonghuarenmingongheguo"
)

echo "=== Sime IME Performance Benchmark ==="
echo "Testing back cache and batch processing optimizations"
echo ""

# Create test input file
TEST_FILE=$(mktemp)
for input in "${TEST_INPUTS[@]}"; do
    echo "$input" >> "$TEST_FILE"
done
echo ":q" >> "$TEST_FILE"

# Run benchmark
echo "Running benchmark with optimizations enabled..."
echo "Test inputs:"
for input in "${TEST_INPUTS[@]}"; do
    echo "  - $input"
done
echo ""

# Measure execution time
echo "Execution time (3 runs):"
for i in {1..3}; do
    echo -n "Run $i: "
    time (cat "$TEST_FILE" | "$IME" --pydict "$PYDICT" --lm "$LM" --nbest 5 > /dev/null 2>&1)
done

# Cleanup
rm -f "$TEST_FILE"

echo ""
echo "=== Benchmark complete ==="
echo ""
echo "Optimizations enabled:"
echo "  - SIME_USE_FAST_STATES: 1"
echo "  - SIME_USE_BATCH_PROCESSING: 1"
echo "  - Scorer::BackCached: enabled"
echo ""
echo "To disable optimizations for comparison, edit interpret.h and set:"
echo "  #define SIME_USE_BATCH_PROCESSING 0"
