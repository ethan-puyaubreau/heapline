#!/bin/sh
# Run a program under heapline with HEAPLINE_OUTPUT set and check its record.
# Usage: check_record.sh LIBHEAPLINE PROGRAM EXPECTED_RANK [VAR=value...]
set -eu
lib=$1 prog=$2 rank=$3
shift 3
unset SLURM_PROCID OMPI_COMM_WORLD_RANK PMI_RANK PMIX_RANK MV2_COMM_WORLD_RANK
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
env "$@" LD_PRELOAD="$lib" HEAPLINE_OUTPUT="$dir/record.%r.%p.csv" \
  "$prog" 2>"$dir/stderr"
grep -q '^heapline: ' "$dir/stderr" || { echo "no summary on stderr"; exit 1; }
set -- "$dir"/record.*.csv
test "$#" -eq 1 && test -f "$1" || { echo "expected one record"; exit 1; }
# The test program allocates little next to the libraries mapped in the
# process, so its VmHWM must cover the heap peak.
awk -F, -v rank="$rank" '
  NR == 1 && $0 != "# heapline 1" { print "bad tag: " $0; bad = 1 }
  NR == 2 && $0 != "rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,vmrss_at_exit_bytes" {
    print "bad columns: " $0; bad = 1 }
  NR == 3 {
    if (NF != 12) { print "expected 12 values, got " NF; bad = 1 }
    if ($1 != rank) { print "rank " $1 ", expected " rank; bad = 1 }
    if ($6 <= 0) { print "no allocation counted"; bad = 1 }
    if ($9 < 0 || $9 > $5) { print "peak time " $9 " outside [0, " $5 "]"; bad = 1 }
    if ($11 < $8) { print "VmHWM " $11 " below heap peak " $8; bad = 1 }
  }
  END { if (NR != 3) { print "expected 3 lines, got " NR; bad = 1 } exit bad }
' "$1"
