#!/bin/sh
# Measure the overhead of heapline on one command and print one JSON line.
# Runs without and with heapline alternate, so both see the same machine.
# Exits non-zero, with nothing on stdout, when a run fails or heapline is not
# active, so a wrong point is never published.
# Usage: run_overhead.sh SCENARIO LIBHEAPLINE RUNS -- COMMAND [ARGS...]
set -eu
export LC_ALL=C

die() {
  echo "run_overhead: $*" >&2
  exit 1
}

usage="usage: run_overhead.sh SCENARIO LIBHEAPLINE RUNS -- COMMAND [ARGS...]"
[ "$#" -ge 5 ] || die "$usage"
[ "$4" = "--" ] || die "$usage"
scenario=$1 lib=$2 runs=$3
shift 4

case $scenario in
'' | *[!a-z0-9-]*) die "SCENARIO must match [a-z0-9-]+, got '$scenario'" ;;
esac
case $runs in
'' | *[!0-9]* | 0*) die "RUNS must be a positive integer, got '$runs'" ;;
esac
[ -r "$lib" ] || die "cannot read $lib"
command -v "$1" >/dev/null 2>&1 || die "cannot execute $1"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Run the command once and append its wall time, in seconds, to $tmp/$1
# ("without" or "with"). A preloaded run must print the heapline summary on
# stderr; the allocation count of its last summary is left in $tmp/allocs.
timed() {
  kind=$1
  shift
  start=$(date +%s.%N)
  if [ "$kind" = with ]; then
    LD_PRELOAD="$lib" "$@" >/dev/null 2>"$tmp/err" || {
      cat "$tmp/err" >&2
      die "$1 failed with LD_PRELOAD=$lib"
    }
  else
    "$@" >/dev/null 2>"$tmp/err" || {
      cat "$tmp/err" >&2
      die "$1 failed"
    }
  fi
  end=$(date +%s.%N)
  awk -v s="$start" -v e="$end" 'BEGIN { printf "%.6f\n", e - s }' >>"$tmp/$kind"
  [ "$kind" = with ] || return 0
  allocs=$(awk '$1 == "heapline:" && $2 ~ /^[0-9]+$/ { n = $2 } END { print n }' "$tmp/err")
  [ -n "$allocs" ] || {
    cat "$tmp/err" >&2
    die "no heapline summary on stderr, $lib is not active"
  }
  echo "$allocs" >"$tmp/allocs"
}

median() {
  sort -g "$1" | awk '{ v[NR] = $1 }
    END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'
}

i=0
while [ "$i" -lt "$runs" ]; do
  timed without "$@"
  timed with "$@"
  i=$((i + 1))
done

for kind in without with; do
  n=$(awk 'END { print NR }' "$tmp/$kind")
  [ "$n" -eq "$runs" ] || die "expected $runs samples in the '$kind' set, got $n"
done

allocs=$(cat "$tmp/allocs")
without=$(median "$tmp/without")
with=$(median "$tmp/with")
awk -v a="$without" -v b="$with" 'BEGIN { exit !(a > 0 && b > 0) }' ||
  die "median time is 0 s"

cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//; s/"//g')
commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
awk -v s="$scenario" -v w="$with" -v wo="$without" -v a="$allocs" \
  -v c="$commit" -v d="$date" -v cpu="$cpu" -v r="$runs" 'BEGIN {
  printf "{\"scenario\": \"%s\", \"commit\": \"%s\", \"date\": \"%s\", ", s, c, d
  printf "\"cpu\": \"%s\", \"runs\": %d, ", cpu, r
  printf "\"seconds_without\": %.4f, \"seconds_with\": %.4f, ", wo, w
  printf "\"overhead\": %.4f, \"allocs_per_s\": %.4g}\n", w / wo - 1, a / w
}'
