#!/bin/bash
# PGO Profile Generation Script
# This script builds the project with instrumentation and runs training workload

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-pgo-gen"
TRAINING_DATA="$PROJECT_ROOT/pgo_training_data.txt"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   PGO Profile Generation - Step 1: Instrumentation Build  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo

# Check for training data
if [[ ! -f "$TRAINING_DATA" ]]; then
    echo "❌ Error: Training data not found at $TRAINING_DATA"
    exit 1
fi

# Create build directory
echo "📁 Creating build directory: $BUILD_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with PGO generation enabled
echo "⚙️  Configuring with PGO instrumentation..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_PGO_GENERATE=ON \
      -DSIME_ENABLE_SIMD=ON \
      "$PROJECT_ROOT"

# Build
echo
echo "🔨 Building instrumented binary..."
make -j$(nproc)

# Check for required data files
if [[ ! -f "$PROJECT_ROOT/pydict_sc.ime.bin" ]]; then
    echo "❌ Error: Dictionary file not found: pydict_sc.ime.bin"
    echo "   Please run trie_conv to generate it first."
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/lm_sc.t3g" ]]; then
    echo "❌ Error: Language model not found: lm_sc.t3g"
    exit 1
fi

# Run training workload
echo
echo "🏃 Running training workload..."
echo "   Input: $TRAINING_DATA"
echo "   Collecting profile data..."
echo

./ime_interpreter \
    --pydict "$PROJECT_ROOT/pydict_sc.ime.bin" \
    --lm "$PROJECT_ROOT/lm_sc.t3g" \
    --nbest 5 \
    < "$TRAINING_DATA"

# Check for generated profile data
PROFILE_DIR="$BUILD_DIR/pgo-profiles"
if [[ -d "$PROFILE_DIR" ]] && [[ -n "$(ls -A "$PROFILE_DIR" 2>/dev/null)" ]]; then
    echo
    echo "✅ Profile generation complete!"
    echo "   Profile data location: $PROFILE_DIR"
    echo "   Profile files:"
    ls -lh "$PROFILE_DIR" | tail -n +2 | awk '{print "     " $9 " (" $5 ")"}'
    echo
    echo "📊 Next step: Run ./scripts/pgo_build.sh to build optimized binary"
else
    echo
    echo "❌ Error: No profile data generated"
    echo "   Expected location: $PROFILE_DIR"
    exit 1
fi
