#!/bin/bash
# PGO Optimized Build Script
# This script builds the project using collected profile data for optimization

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
GEN_BUILD_DIR="$PROJECT_ROOT/build-pgo-gen"
USE_BUILD_DIR="$PROJECT_ROOT/build-pgo-use"
PROFILE_DIR="$GEN_BUILD_DIR/pgo-profiles"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   PGO Profile-Guided Optimization - Step 2: Build         ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo

# Check for profile data
if [[ ! -d "$PROFILE_DIR" ]] || [[ -z "$(ls -A "$PROFILE_DIR" 2>/dev/null)" ]]; then
    echo "❌ Error: No profile data found at $PROFILE_DIR"
    echo "   Please run ./scripts/pgo_generate.sh first"
    exit 1
fi

echo "📊 Found profile data:"
ls -lh "$PROFILE_DIR" | tail -n +2 | awk '{print "   " $9 " (" $5 ")"}'
echo

# Create build directory
echo "📁 Creating optimized build directory: $USE_BUILD_DIR"
rm -rf "$USE_BUILD_DIR"
mkdir -p "$USE_BUILD_DIR"

# Copy profile data to the new build directory
echo "📋 Copying profile data..."
mkdir -p "$USE_BUILD_DIR/pgo-profiles"
cp -r "$PROFILE_DIR"/* "$USE_BUILD_DIR/pgo-profiles/"

cd "$USE_BUILD_DIR"

# Configure with PGO usage enabled
echo "⚙️  Configuring with PGO optimization..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_PGO_USE=ON \
      -DSIME_ENABLE_SIMD=ON \
      "$PROJECT_ROOT"

# Build
echo
echo "🔨 Building optimized binary with PGO..."
make -j$(nproc)

# Verify build
if [[ -f "./ime_interpreter" ]]; then
    echo
    echo "✅ PGO optimized build complete!"
    echo "   Binary location: $USE_BUILD_DIR/ime_interpreter"
    echo "   Binary size: $(ls -lh ./ime_interpreter | awk '{print $5}')"
    echo
    echo "🚀 Optimizations enabled:"
    echo "   ✓ Profile-Guided Optimization (PGO)"
    echo "   ✓ SIMD (AVX2)"
    echo "   ✓ Link-Time Optimization (LTO)"
    echo "   ✓ Fast Math"
    echo "   ✓ Function Inlining"
    echo
    echo "📊 To benchmark, run:"
    echo "   cd $USE_BUILD_DIR"
    echo "   ../benchmark_optimizations.sh"
else
    echo
    echo "❌ Error: Build failed - binary not found"
    exit 1
fi
