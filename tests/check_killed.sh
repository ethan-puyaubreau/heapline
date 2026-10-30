#!/bin/sh
# Run killed under heapline and check the record it leaves: its last snapshot,
# marked as not complete, or no record at all with HEAPLINE_SNAPSHOT=0.
# Usage: check_killed.sh LIBHEAPLINE PROGRAM on|off
set -eu
lib=$1 prog=$2 mode=$3
unset SLURM_PROCID OMPI_COMM_WORLD_RANK PMI_RANK PMIX_RANK MV2_COMM_WORLD_RANK
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
snapshot=1
if [ "$mode" = off ]; then snapshot=0; fi
status=0
LD_PRELOAD="$lib" HEAPLINE_OUTPUT="$dir/record.csv" HEAPLINE_SNAPSHOT=$snapshot \
  "$prog" 2>"$dir/stderr" || status=$?
# A shell reports a process killed by SIGKILL with 128 + 9.
test "$status" -eq 137 || { echo "exit status $status, expected 137"; exit 1; }
if [ "$mode" = off ]; then
  test ! -e "$dir/record.csv" || { echo "record written with snapshots off"; exit 1; }
  exit 0
fi
test -f "$dir/record.csv" || { echo "no record left"; exit 1; }
# Columns: 8 peak_heap_bytes, 10 live heap, 13 complete.
awk -F, '
  NR == 1 && $0 != "# heapline 2" { print "bad tag: " $0; bad = 1 }
  NR == 3 {
    if (NF != 13) { print "expected 13 values, got " NF; bad = 1 }
    if ($13 != 0) { print "complete " $13 ", expected 0"; bad = 1 }
    if ($8 < 67108864) { print "heap peak " $8 " below 64 MiB"; bad = 1 }
    if ($10 < 67108864) { print "live heap " $10 " below 64 MiB"; bad = 1 }
  }
  END { if (NR != 3) { print "expected 3 lines, got " NR; bad = 1 } exit bad }
' "$dir/record.csv"
