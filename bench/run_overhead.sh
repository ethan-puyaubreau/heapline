#!/bin/sh
# Measure the overhead of heapline on malloc_churn and print one JSON line.
# Runs without and with heapline alternate, so both see the same machine.
# Exits non-zero, with nothing on stdout, when a run fails or heapline is not
# active, so a wrong point is never published.
# Usage: run_overhead.sh MALLOC_CHURN LIBHEAPLINE [RUNS [THREADS ITERATIONS]]
set -eu
export LC_ALL=C

die() {
  echo "run_overhead: $*" >&2
  exit 1
}

[ "$#" -ge 2 ] ||
  die "usage: run_overhead.sh MALLOC_CHURN LIBHEAPLINE [RUNS [THREADS ITERATIONS]]"
bench=$1 lib=$2 runs=${3:-10}
if [ "$#" -gt 2 ]; then shift 3; else shift 2; fi
# "$@" is now the optional THREADS ITERATIONS passed to the benchmark.

case $runs in
'' | *[!0-9]* | 0) die "RUNS must be a positive integer, got '$runs'" ;;
esac
[ -x "$bench" ] || die "cannot execute $bench"
[ -r "$lib" ] || die "cannot read $lib"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Run the benchmark once; its output is left in $tmp/out.
run_plain() {
  "$bench" "$@" >"$tmp/out" || die "$bench failed"
}

# Run the benchmark with heapline preloaded; its output is left in $tmp/out and
# its stderr in $tmp/err. A failed or ignored preload is an error.
run_preloaded() {
  if ! LD_PRELOAD="$lib" "$bench" "$@" >"$tmp/out" 2>"$tmp/err"; then
    cat "$tmp/err" >&2
    die "$bench failed with LD_PRELOAD=$lib"
  fi
  if grep -q 'cannot be preloaded' "$tmp/err"; then
    cat "$tmp/err" >&2
    die "$lib cannot be preloaded"
  fi
}

# Print field $2 of the benchmark line in file $1 when it is a number.
field() {
  awk -v n="$2" \
    '$1 == "malloc_churn:" && $n ~ /^[0-9]+(\.[0-9]+)?$/ { print $n }' "$1"
}

median() {
  sort -g "$1" | awk '{ v[NR] = $1 }
    END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

# A tiny preloaded run proves the library is loaded and active.
run_preloaded 1 1000
grep -q '^heapline: ' "$tmp/err" ||
  die "no heapline summary on stderr, $lib is not active"

i=0
while [ "$i" -lt "$runs" ]; do
  run_plain "$@"
  field "$tmp/out" 6 >>"$tmp/without"
  run_preloaded "$@"
  field "$tmp/out" 6 >>"$tmp/with"
  i=$((i + 1))
done

for kind in without with; do
  n=$(awk 'END { print NR }' "$tmp/$kind")
  [ "$n" -eq "$runs" ] || die "expected $runs samples in the '$kind' set, got $n"
done

# The last run is a preloaded one and has the same allocation count.
allocs=$(field "$tmp/out" 4)
[ -n "$allocs" ] || die "no allocation count in the benchmark output"
without=$(median "$tmp/without")
with=$(median "$tmp/with")
awk -v a="$without" -v b="$with" 'BEGIN { exit !(a > 0 && b > 0) }' ||
  die "median time is 0 s, raise ITERATIONS"

cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//; s/"//g')
commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
awk -v w="$with" -v wo="$without" -v a="$allocs" -v c="$commit" \
    -v d="$date" -v cpu="$cpu" -v r="$runs" 'BEGIN {
  printf "{\"commit\": \"%s\", \"date\": \"%s\", \"cpu\": \"%s\", \"runs\": %d, ", c, d, cpu, r
  printf "\"seconds_without\": %.4f, \"seconds_with\": %.4f, ", wo, w
  printf "\"overhead\": %.4f, \"allocs_per_s\": %.4g}\n", w / wo - 1, a / w
}'
