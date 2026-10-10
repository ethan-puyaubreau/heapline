#!/bin/sh
# Run peak_time under heapline and check when the heap peak was reached.
# Usage: check_peak_time.sh LIBHEAPLINE PROGRAM early|late
set -eu
lib=$1 prog=$2 mode=$3
unset SLURM_PROCID OMPI_COMM_WORLD_RANK PMI_RANK PMIX_RANK MV2_COMM_WORLD_RANK
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
LD_PRELOAD="$lib" HEAPLINE_OUTPUT="$dir/record.csv" "$prog" "$mode" \
  2>"$dir/stderr" || { echo "program failed"; cat "$dir/stderr"; exit 1; }
test -f "$dir/record.csv" || { echo "no record written"; exit 1; }
# Columns: 5 elapsed_s, 8 peak_heap_bytes, 9 peak_heap_time_s.
awk -F, -v mode="$mode" '
  NR == 3 {
    if (NF != 12) { print "expected 12 values, got " NF; bad = 1 }
    if ($8 < 16777216) { print "heap peak " $8 " below 16 MiB"; bad = 1 }
    if (mode == "early") {
      if ($9 >= 0.5) { print "early peak time " $9 ", expected below 0.5"; bad = 1 }
      if ($5 < 1) { print "elapsed " $5 ", expected at least 1"; bad = 1 }
    } else if (mode == "late") {
      if ($9 < 0.9) { print "late peak time " $9 ", expected at least 0.9"; bad = 1 }
      if ($9 > $5) { print "peak time " $9 " after elapsed " $5; bad = 1 }
    } else { print "unknown mode " mode; bad = 1 }
  }
  END { if (NR != 3) { print "expected 3 lines, got " NR; bad = 1 } exit bad }
' "$dir/record.csv"
