#!/bin/sh
# Check the merged records of the mpi_imbalance example: rank r must peak at
# (r + 1) * 64 MiB, plus what MPI allocates itself, at most SLACK MiB.
# Usage: check_example.sh MERGED_CSV RANKS [SLACK_MIB]
set -eu
awk -F, -v ranks="$2" -v slack="${3:-32}" '
  NR <= 2 { next }
  {
    mib  = 1024 * 1024
    want = ($1 + 1) * 64 * mib
    if ($8 < want || $8 > want + slack * mib) {
      printf "rank %d peaked at %d bytes, expected %d to %d\n",
             $1, $8, want, want + slack * mib
      bad = 1
    }
    seen++
  }
  END {
    if (seen != ranks) { printf "expected %d ranks, found %d\n", ranks, seen; bad = 1 }
    exit bad
  }
' "$1"
