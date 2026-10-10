# Tutorial: how much memory does my MPI job need?

This tutorial builds on the [plain tutorial](tutorial.md), which explains the
summary line, the record and the figures of the report on a single program. It
measures the heap of a small MPI program whose ranks do not use the same
amount of memory, then reads the job report and turns it into a `--mem`
request. You need an MPI installation, such as Open MPI or MPICH; Slurm is
optional. It takes about fifteen minutes.

## 1. Build heapline and the example

If you built heapline for the plain tutorial, build it again with the example:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEAPLINE_ENABLE_EXAMPLES=ON
cmake --build build
```

Otherwise clone the repository first, as in [step 1 of the plain
tutorial](tutorial.md#1-get-heapline). Without MPI, CMake prints `MPI not
found, the examples are not built`.

## 2. Run an MPI job under heapline

`examples/mpi_imbalance.cpp` makes rank r allocate (r + 1) times 64 MiB.
Run it on four ranks, one record per rank:

```
mkdir records
mpirun -n 4 env LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=$PWD/records/heapline.%h.%r.csv build/examples/mpi_imbalance
```

Each rank prints what it holds, in any order, and heapline prints a summary
line for each rank:

```text
rank 0 holds 64 MiB
rank 1 holds 128 MiB
rank 2 holds 192 MiB
rank 3 holds 256 MiB
heapline: 28812 allocs, 27732 frees, peak 67.4 MiB, live at exit 205.2 KiB
heapline: 28806 allocs, 27723 frees, peak 131.4 MiB, live at exit 205.3 KiB
heapline: 28816 allocs, 27733 frees, peak 195.4 MiB, live at exit 205.3 KiB
heapline: 28805 allocs, 27722 frees, peak 259.4 MiB, live at exit 205.3 KiB
```

The `records` directory now holds one file per rank, named after the host and
the rank, such as `heapline.node01.0.csv`.

`env` sets the variables in each rank only, not in `mpirun` itself. `%h` and
`%r` give each rank its own file. Keep `env` directly in front of the program:
a wrapper script or `/usr/bin/time` between them would be preloaded too, and
its record would overwrite the program's (see [Wrapper
processes](mpi.md#wrapper-processes)).

With Open MPI on a machine with fewer than four cores, `mpirun` refuses to
start four ranks unless you add `--oversubscribe` after it (MPICH has no such
option).

## 3. Read the job report

```
build/heapline-report records
```

Output of a run on a single node:

```text
heapline-report: 4 ranks on 1 node, longest process 0.5 s

Peak heap
  max        259.4 MiB, rank 3 at 0.3 s
  median     163.4 MiB
  min        67.4 MiB
  imbalance  1.59 (max / mean)

Per node, sum of VmHWM (an upper bound to compare with --mem)
  node01  4 ranks  698.4 MiB

Ranks by peak heap (4 of 4)
  rank  host     peak heap  at (s)       VmHWM  heap / VmHWM
     3  node01   259.4 MiB     0.3   270.6 MiB  0.96
     2  node01   195.4 MiB     0.3   206.7 MiB  0.95
     1  node01   131.4 MiB     0.3   142.5 MiB  0.92
     0  node01    67.4 MiB     0.3    78.6 MiB  0.86

Slurm: sacct MaxRSS is a per-task maximum sampled during the job. Compare it
with the largest VmHWM above, 270.6 MiB; a short peak can fall between samples.
```

Your figures will differ slightly: MPI allocates memory of its own, which
depends on the implementation and the machine. Read the report from top to
bottom:

- **Peak heap**: rank 3 peaks highest, at about 260 MiB, rank 0 lowest, at
  about 67 MiB: the 256 and 64 MiB the example allocates, plus a few MiB
  allocated by MPI. The imbalance, about 1.6, says the busiest rank uses 1.6
  times the average.
- **Per node**: the sum of `VmHWM` over the ranks of each node.
- **Ranks by peak heap**: `VmHWM` exceeds the heap peak by about 11 MiB on
  every rank. This constant gap is mostly memory that heapline does not count
  as heap: code, libraries, stacks and the shared memory MPI maps. It weighs
  more on a small heap, which is why the ratio of heap to `VmHWM` is 0.96 on
  rank 3 and only about 0.86 on rank 0. The example writes all the memory it
  allocates, so its heap is entirely resident and the ratio is high on every
  rank.

## 4. Size the job

`--mem` is a per-node limit. Take the per-node sum of `VmHWM`, the largest
one if the job spans several nodes, and add the margin your site recommends.
Since ranks do not always peak at the same time, the sum is an upper bound:
safe, sometimes generous. The imbalance tells whether the work, or the data,
could be spread better between ranks.

For the job above, all on one node, the sum is 698.4 MiB. With a margin of
10 % (take the one your site recommends) that makes 768.2 MiB, which rounds
up to `--mem=800M`.

## 5. When the numbers do not match Slurm

`sacct` reports `MaxRSS`, sampled every 30 s by default. A short peak can be
missed, and resident memory is not heap. [What "peak memory"
means](peak-memory.md) explains each measure.

## 6. Next steps

- [Measuring an MPI job](mpi.md) gives the recipe for `srun` and batch jobs.
- [heapline-report](report.md) describes every option.
- To find which code allocates, see [Limitations and
  alternatives](limitations.md).
