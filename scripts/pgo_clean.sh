#!/bin/bash
# PGO Cleanup Script
# Removes all PGO-related build directories and profile data

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   PGO Cleanup - Removing Profile Data and Build Dirs      ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo

# Remove PGO build directories
for dir in "build-pgo-gen" "build-pgo-use"; do
    full_path="$PROJECT_ROOT/$dir"
    if [[ -d "$full_path" ]]; then
        echo "🗑️  Removing $dir..."
        rm -rf "$full_path"
    else
        echo "⏭️  Skipping $dir (not found)"
    fi
done

# Remove any stray .gcda files in project root
if ls "$PROJECT_ROOT"/*.gcda 1> /dev/null 2>&1; then
    echo "🗑️  Removing .gcda files in project root..."
    rm -f "$PROJECT_ROOT"/*.gcda
fi

echo
echo "✅ Cleanup complete!"
