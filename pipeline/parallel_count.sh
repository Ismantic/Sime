#!/bin/bash
set -euo pipefail

# Usage: parallel_count.sh -n <max_order> -d <dict> -o <output_prefix> \
#                          [-c count_max] [-j nproc] <corpus>
#
# Emits <output_prefix>.1gram .. .<N>gram (one merged file per order).

NGRAM=""
DICT=""
OUTPUT=""
COUNT_MAX=83886080
NPROC=$(nproc)
PUNCT=""
CORPUS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -n) NGRAM=$2; shift 2;;
        -d) DICT=$2; shift 2;;
        -o) OUTPUT=$2; shift 2;;
        -c) COUNT_MAX=$2; shift 2;;
        -j) NPROC=$2; shift 2;;
        -p) PUNCT=$2; shift 2;;
        *)  CORPUS=$1; shift;;
    esac
done

if [[ -z "$NGRAM" || -z "$DICT" || -z "$OUTPUT" || -z "$CORPUS" ]]; then
    echo "usage: parallel_count.sh -n <max_order> -d <dict> -o <output_prefix> [-c count_max] [-j nproc] [-p punct] <corpus>" >&2
    exit 1
fi

SIME="$(dirname "$0")/../build/sime-count"
OUTDIR="$(dirname "$OUTPUT")"
mkdir -p "$OUTDIR"

# Split corpus
echo "=== Splitting corpus into $NPROC chunks ===" >&2
split -n "r/$NPROC" "$CORPUS" "$OUTDIR/chunk."
CHUNKS=("$OUTDIR"/chunk.*)
echo "Split into ${#CHUNKS[@]} chunks" >&2

# Divide count_max by nproc so total memory stays the same
PER_PROC_MAX=$((COUNT_MAX / NPROC))
if [[ $PER_PROC_MAX -lt 1024 ]]; then
    PER_PROC_MAX=1024
fi
echo "=== Counting n-grams (count_max per proc: $PER_PROC_MAX) ===" >&2

all_pids=()

cleanup() {
    echo "Interrupted, killing child processes..." >&2
    for pid in "${all_pids[@]}"; do
        kill "$pid" 2>/dev/null
    done
    wait 2>/dev/null
    exit 1
}
trap cleanup INT TERM HUP

chunk_prefixes=()
for i in "${!CHUNKS[@]}"; do
    chunk="${CHUNKS[$i]}"
    out="$OUTDIR/ngram.$i"        # sime-count writes $out.1gram .. .Ngram
    swap="$OUTDIR/swap.$i"        # ... and $swap.1 .. .N
    chunk_prefixes+=("$out")
    punct_args=()
    if [[ -n "$PUNCT" ]]; then
        punct_args+=(-p "$PUNCT")
    fi
    "$SIME" -n "$NGRAM" -d "$DICT" -s "$swap" -o "$out" -c "$PER_PROC_MAX" "${punct_args[@]}" "$chunk" &
    all_pids+=($!)
done

failed=0
for pid in "${all_pids[@]}"; do
    wait "$pid" || failed=1
done
if [[ $failed -ne 0 ]]; then
    echo "some sime-count processes failed" >&2
    exit 1
fi

# Merge per order (parallel)
MERGE_BIN="$(dirname "$0")/merge.bin"
merge_order() {
    local order=$1
    local suffix=".${order}gram"
    local inputs=()
    for pfx in "${chunk_prefixes[@]}"; do
        inputs+=("${pfx}${suffix}")
    done
    echo "=== Merging order $order ===" >&2
    if [[ -x "$MERGE_BIN" ]]; then
        "$MERGE_BIN" -n "$order" -o "${OUTPUT}${suffix}" "${inputs[@]}"
    else
        python3 "$(dirname "$0")/merge_ngram.py" -n "$order" \
            -o "${OUTPUT}${suffix}" "${inputs[@]}"
    fi
}

all_pids=()
for order in $(seq 1 "$NGRAM"); do
    merge_order "$order" &
    all_pids+=($!)
done
for pid in "${all_pids[@]}"; do
    wait "$pid" || { echo "merge failed" >&2; exit 1; }
done

# Clean up intermediate files
rm -f "$OUTDIR"/chunk.* "$OUTDIR"/ngram.*.[0-9]gram "$OUTDIR"/swap.*.[0-9]
echo "=== Done: ${OUTPUT}.1gram .. ${OUTPUT}.${NGRAM}gram ===" >&2
