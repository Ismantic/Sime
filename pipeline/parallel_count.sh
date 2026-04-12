#!/bin/bash
set -euo pipefail

# Usage: parallel_count.sh -n <ngram> -d <dict> -o <output> -c <count_max> [-j nproc] <corpus>

NGRAM=""
DICT=""
OUTPUT=""
COUNT_MAX=83886080
NPROC=$(nproc)
CORPUS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -n) NGRAM=$2; shift 2;;
        -d) DICT=$2; shift 2;;
        -o) OUTPUT=$2; shift 2;;
        -c) COUNT_MAX=$2; shift 2;;
        -j) NPROC=$2; shift 2;;
        *)  CORPUS=$1; shift;;
    esac
done

if [[ -z "$NGRAM" || -z "$DICT" || -z "$OUTPUT" || -z "$CORPUS" ]]; then
    echo "usage: parallel_count.sh -n <ngram> -d <dict> -o <output> [-c count_max] [-j nproc] <corpus>" >&2
    exit 1
fi

SIME="$(dirname "$0")/../build/sime-count"
OUTDIR="$(dirname "$OUTPUT")"
mkdir -p "$OUTDIR"

# Split corpus
echo "=== Splitting corpus into $NPROC chunks ===" >&2
split -n "l/$NPROC" "$CORPUS" "$OUTDIR/chunk."
CHUNKS=("$OUTDIR"/chunk.*)
echo "Split into ${#CHUNKS[@]} chunks" >&2

# Divide count_max by nproc so total memory stays the same
PER_PROC_MAX=$((COUNT_MAX / NPROC))
if [[ $PER_PROC_MAX -lt 1024 ]]; then
    PER_PROC_MAX=1024
fi
echo "=== Counting n-grams (count_max per proc: $PER_PROC_MAX) ===" >&2

pids=()
outputs=()
for i in "${!CHUNKS[@]}"; do
    chunk="${CHUNKS[$i]}"
    out="$OUTDIR/ngram.$i"
    swap="$OUTDIR/swap.$i"
    outputs+=("$out")
    "$SIME" -n "$NGRAM" -d "$DICT" -s "$swap" -o "$out" -c "$PER_PROC_MAX" "$chunk" &
    pids+=($!)
done

failed=0
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
if [[ $failed -ne 0 ]]; then
    echo "some sime-count processes failed" >&2
    exit 1
fi

# Merge
echo "=== Merging ===" >&2
python3 "$(dirname "$0")/merge_ngram.py" -n "$NGRAM" -o "$OUTPUT" "${outputs[@]}"

# Clean up intermediate files
rm -f "$OUTDIR"/chunk.* "$OUTDIR"/ngram.* "$OUTDIR"/swap.*
echo "=== Done: $OUTPUT ===" >&2
