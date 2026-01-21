#!/bin/bash
# PGO All-in-One Script
# Executes the complete PGO workflow: generate -> build -> benchmark

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║   PGO Complete Workflow - Profile-Guided Optimization            ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo

# Step 1: Generate profile data
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 1/3: Generating profile data..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
"$SCRIPT_DIR/pgo_generate.sh"

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 2/3: Building optimized binary..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
"$SCRIPT_DIR/pgo_build.sh"

# Step 3: Benchmark (optional)
echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 3/3: Benchmarking (optional)..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

read -p "Run benchmark to compare PGO performance? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cd "$PROJECT_ROOT/build-pgo-use"
    "$PROJECT_ROOT/benchmark_optimizations.sh"
else
    echo "⏭️  Skipping benchmark"
fi

echo
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║   PGO Workflow Complete!                                         ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo
echo "📊 Summary:"
echo "   ✓ Profile data generated and collected"
echo "   ✓ Optimized binary built with PGO + SIMD"
echo "   ✓ Ready for production use"
echo
echo "📍 Optimized binary location:"
echo "   $PROJECT_ROOT/build-pgo-use/ime_interpreter"
echo
echo "🧹 To clean up PGO artifacts, run:"
echo "   ./scripts/pgo_clean.sh"
