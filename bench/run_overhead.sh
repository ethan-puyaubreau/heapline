#!/bin/sh
# Measure the overhead of heapline on malloc_churn and print one JSON line.
# Runs without and with heapline alternate, so both see the same machine.
# Usage: run_overhead.sh MALLOC_CHURN LIBHEAPLINE [RUNS]
set -eu
bench=$1 lib=$2 runs=${3:-10}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

median() {
  sort -g "$1" | awk '{ v[NR] = $1 }
    END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

i=0
while [ "$i" -lt "$runs" ]; do
  "$bench" | awk '{ print $6 }' >>"$tmp/without"
  LD_PRELOAD="$lib" "$bench" 2>/dev/null | awk '{ print $6 }' >>"$tmp/with"
  i=$((i + 1))
done

without=$(median "$tmp/without")
with=$(median "$tmp/with")
allocs=$("$bench" | awk '{ print $4 }')
cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//; s/"//g')
commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
awk -v w="$with" -v wo="$without" -v a="$allocs" -v c="$commit" \
    -v d="$date" -v cpu="$cpu" -v r="$runs" 'BEGIN {
  printf "{\"commit\": \"%s\", \"date\": \"%s\", \"cpu\": \"%s\", \"runs\": %d, ", c, d, cpu, r
  printf "\"seconds_without\": %.4f, \"seconds_with\": %.4f, ", wo, w
  printf "\"overhead\": %.4f, \"allocs_per_s\": %.4g}\n", w / wo - 1, a / w
}'
