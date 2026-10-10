# heapline

Heap usage of a Linux program, rank by rank, without recompiling it.

heapline is a small shared library loaded with `LD_PRELOAD`. It counts
allocations and releases, tracks the bytes in use and their peak, and writes
one record per process. `heapline-report` merges the records of a job and
tells how much memory each rank used and when it peaked: what you need to size
the `--mem` request of a Slurm job and to spot imbalanced ranks.

It is not a debugger and not a full heap profiler. It keeps lock-free counters
on the allocation path and allocates nothing there. Its cost is paid per
allocation: a program that computes between allocations barely notices it,
while a loop that does nothing but allocate from several threads runs several
times slower. Its overhead is measured on pushes to the main branch:
see [Performance history](performance.md). When you need every allocation with
its call stack, use heaptrack or Valgrind's massif instead (see
[Limitations and alternatives](limitations.md)).

```{toctree}
:maxdepth: 1

getting-started
tutorial
mpi
report
format
peak-memory
limitations
performance
changelog
```
