#!/bin/sh
# Run stale_thread under heapline with HEAPLINE_OUTPUT set, and check that the
# summary and the record count at least the operations the program made.
# Exits with 77, which ctest reports as skipped, when the program does.
# Usage: check_stale_thread.sh LIBHEAPLINE PROGRAM
set -eu
lib=$1 prog=$2
unset SLURM_PROCID OMPI_COMM_WORLD_RANK PMI_RANK PMIX_RANK MV2_COMM_WORLD_RANK
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
status=0
LD_PRELOAD="$lib" HEAPLINE_OUTPUT="$dir/record.csv" "$prog" \
  >"$dir/stdout" 2>"$dir/stderr" || status=$?
cat "$dir/stderr" >&2
if [ "$status" -ne 0 ] && [ "$status" -ne 77 ]; then
  echo "program failed"
  exit 1
fi
read -r allocs frees <"$dir/stdout"
# The summary on stderr and the record hold the same counts.
summary=$(sed -n 's/^heapline: \([0-9]*\) allocs, \([0-9]*\) frees.*/\1 \2/p' \
  "$dir/stderr")
test -n "$summary" || { echo "no summary on stderr"; exit 1; }
set -- $summary
awk -F, -v allocs="$allocs" -v frees="$frees" -v sa="$1" -v sf="$2" '
  NR == 3 {
    if ($6 < allocs) { print "record counts " $6 " allocs, expected " allocs; bad = 1 }
    if ($7 < frees) { print "record counts " $7 " frees, expected " frees; bad = 1 }
    if (sa != $6) { print "summary counts " sa " allocs, record " $6; bad = 1 }
    if (sf != $7) { print "summary counts " sf " frees, record " $7; bad = 1 }
  }
  END { if (NR != 3) { print "expected 3 lines, got " NR; bad = 1 } exit bad }
' "$dir/record.csv"
if [ "$status" -eq 77 ]; then exit 77; fi
