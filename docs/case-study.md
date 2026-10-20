# Case study: miniAMR, LULESH and CloverLeaf

This page runs heapline 0.5.0 on three proxy applications built with Spack,
miniAMR, LULESH and CloverLeaf, and checks its figures against GNU time and
against what the applications print.

## Setup

One laptop: Snapdragon X Elite, 12 aarch64 cores, 15.4 GiB of RAM, Ubuntu
24.04 under WSL2 (kernel 6.6.114.1). GCC 13.3.0, Open MPI 4.1.6 from the
system, Spack v1.2.2 (commit `3e19345b`).

| Spec | Hash |
|---|---|
| `heapline@0.5.0` | `r52idoe` |
| `miniamr@1.7.0` | `uzl2rdc` |
| `lulesh@2.0.3+mpi+openmp~visual` | `5lx3vtb` |
| `cloverleaf@1.1~debug~ieee build=ref` | `m5t4mqv` |
| `openmpi@4.1.6` (external, `/usr`) | `myah4ke` |

The runs used an earlier revision of the recipe below, which differs only in
its stand-alone tests. The recipe as printed builds `heapline@0.5.0` as
`rohkjzo`.

Every run is launched as in [Measuring an MPI job](mpi.md#with-mpirun), with
heapline on the program and not on `mpirun`, and a fresh records directory
per run:

```
HL=$(spack location -i heapline@0.5.0)
mkdir -p records
mpirun -n 8 env LD_PRELOAD=$HL/lib/libheapline.so \
    HEAPLINE_OUTPUT=$PWD/records/heapline.%h.%r.csv OMP_NUM_THREADS=1 ./app ...
$HL/bin/heapline-report records
```

Each scenario ran three times with heapline and three times without, and once
more without heapline under GNU time to get the maximum resident set size of
each rank. That run has no heapline, so `/usr/bin/time` as a wrapper
([Wrapper processes](mpi.md#wrapper-processes)) does not matter:

```
mpirun -n 8 sh -c 'exec /usr/bin/time -f %M -o "$0/rss.$OMPI_COMM_WORLD_RANK" "$@"' $PWD ./app ...
```

Every `mpirun` and every `heapline-report` exited 0. Every run left one record
per rank, and `heapline-report` printed no warning.

## miniAMR

8 ranks of one thread each (2x2x2), blocks of 8x8x8 cells, 10 variables,
3 levels of refinement, `--max_blocks 800`, 400 time steps. One sphere surface
crosses the cube diagonally at 0.01 per step, the "one sphere moving
diagonally" example of the miniAMR README at a quarter of the speed over four times the
steps, so the sphere covers the same distance. The scenario was chosen to make the refined region, and the work, move
from rank to rank.

| Measure | Value |
|---|---|
| Highest rank peak, three runs | 120,900,320, 120,900,288 and 120,900,288 bytes (115.3 MiB) |
| Spread between runs | 32 bytes |
| `VmHWM` per rank, with heapline | 98,940 to 99,712 KiB (96.6 to 97.4 MiB) |
| GNU time `%M` per rank, without heapline | 99,456 to 99,712 KiB |
| heap / `VmHWM` | 1.18 to 1.19 |
| Imbalance | 1.00 |

`VmHWM` and GNU time agree within 0.8%: the preload adds no resident memory
visible at this resolution.

The heap peak is above `VmHWM`, the first case of [What "peak memory"
means](peak-memory.md). miniAMR allocates a 32 MB receive buffer and a 6.4 MB
send buffer, both above the `mmap` threshold of glibc, and writes only the
part that messages use. The rest counts in the heap and not in `VmHWM`.
The gap of about 19 MB is consistent with that; it was not broken down term
by term.

The imbalance is 1.00 because miniAMR allocates every block it may ever use
at startup: 1,111 calls to `malloc` per block, times `--max_blocks`, plus the
communication buffers sized from `--max_blocks`. Moving the sphere changes
how many blocks each rank uses (at most 428 on one rank), not how much it
allocates. The peak heap of the ranks differs by 17 to 22 KB out of 120.9 MB.
miniAMR shows no heap imbalance unless its allocation follows the work. The
same preallocation explains the time of the peak: the heap reaches its plateau
during initialization, and the reported time is that of a few extra KB during
the time steps, from 3 s to 95 s depending on the rank and the run. It marks
no phase of the application.

miniAMR prints "Amount malloced", the sum of the sizes it requested through
its own wrapper. The sum never subtracts frees, so it is not a peak, but
almost all of it is allocated at startup, which makes it close to the live
heap: 109.91 to 109.95 MB per rank. heapline's peak is 1.10 times larger. The
80-byte rows that dominate the allocations have a usable size of 88 bytes
with glibc on aarch64 (88/80 = 1.10), and heapline counts usable sizes, plus
the allocations of Open MPI and libc that miniAMR does not count. The two
contributions were not separated.

The output of miniAMR is the same with and without heapline: block
statistics, "Amount malloced" and "Max blocks on a processor".

## LULESH

Two scenarios with the same binary:

| Scenario | Ranks x threads | Arguments | `mpirun` options |
|---|---|---|---|
| `a` | 8 x 1 | `-s 40 -i 400` (64,000 elements per rank) | none |
| `b` | 1 x 8 | `-s 50 -i 400` (125,000 elements) | `--bind-to none` |

| Measure | `a` | `b` |
|---|---|---|
| Highest rank peak, three runs | 53,498,840 to 53,506,784 bytes (51.0 MiB) | 127,883,192 to 127,883,224 bytes (122.0 MiB) |
| Spread between runs | 7,944 bytes (0.015%) | 32 bytes |
| Largest `VmHWM` over the runs | 64,444 KiB | 136,700 to 136,848 KiB |
| GNU time `%M`, without heapline | 64,428 KiB | 136,684 KiB |
| heap / `VmHWM` | 0.81 | 0.91 |
| Imbalance | 1.00 | one rank |

The differences between `VmHWM` and GNU time, 16 KiB for `a` and 16 to
164 KiB for `b`, are of the order of the variation of `VmHWM` itself from run
to run (148 KiB over the three runs of `b`).

`VmHWM` exceeds the heap peak by 11.7 to 12.0 MiB per rank in `a` and 11.5 to
11.7 MiB in `b`, the second case of [What "peak memory"
means](peak-memory.md) and close to the 11 MiB of the [MPI
tutorial](tutorial-mpi.md): code and libraries (Open MPI, libgomp,
libstdc++), stacks, the `mmap` regions of Open MPI and the freed memory the
allocator keeps. The gap is almost the same with one thread and with eight.

The imbalance is 1.00 (1.001 before rounding) because LULESH splits the cube
into 8 equal subdomains: the ranks peak between 50.9 and 51.0 MiB. LULESH
allocates about 760 blocks per cycle and rank and frees almost all of them.
The peak values agree within 0.2% while their times scatter between ranks and
runs (0.5 s to 22.6 s in `a`), so the time of the peak marks no phase of the
run.

LULESH prints no memory figure, so the heap peak has no application figure to
compare with. Its result, "Final Origin Energy", is the same with and without
heapline in every run: 8.644914e+06 for `a`, 2.110575e+06 for `b`.

### Bind the threads of a single rank

With `-n 1`, Open MPI 4.1.6 binds the rank to one core by default.
`mpirun --report-bindings -n 1` prints "bound to socket 0[core 0[hwt 0]]", and
`Cpus_allowed_list` in `/proc/self/status` is `0`: the 8 OpenMP threads of
`b` then share one core. With `--bind-to none`, `Cpus_allowed_list` is `0-11`,
GNU time reports 498% to 581% CPU, and the threads run in parallel. With
`-n 8` the ranks are not bound. Check the binding with `--report-bindings`
before you measure a hybrid MPI and OpenMP job with few ranks per node.

## CloverLeaf

4 ranks of 2 OpenMP threads each, the Fortran version (`build=ref`), with the
`clover_bm_short.in` deck that Spack installs in `doc/tests`, copied unchanged
as `clover.in` into each run directory: 960x960 cells, 87 steps. CloverLeaf
allocates its fields with Fortran `allocate`, which gfortran turns into calls
to `malloc`, so heapline counts them as it counts the allocations of C.

| Measure | Value |
|---|---|
| Highest rank peak, three runs | 51,394,664, 51,394,744 and 51,394,744 bytes (49.0 MiB) |
| Spread between runs | 80 bytes |
| `VmHWM` per rank, with heapline | 62,080 to 62,208 KiB (60.6 to 60.8 MiB) |
| GNU time `%M` per rank, without heapline | 62,080 to 62,208 KiB |
| heap / `VmHWM` | 0.81 |
| Imbalance | 1.00 |

`VmHWM` is the same in the three runs: 62,208 KiB on rank 0 and 62,080 KiB on
ranks 1 to 3. GNU time gives 62,208 KiB on ranks 0, 1 and 3 and 62,080 KiB on
rank 2: the values agree within one 128 KiB step (0.2%).

`VmHWM` exceeds the heap peak by 11.7 to 11.8 MiB per rank, the same case as
LULESH: code and libraries (libgfortran, libgomp, Open MPI), the stacks of the
second thread and of the threads of Open MPI, the `mmap` regions of Open MPI
and the freed memory the allocator keeps. These runs do not split the gap.

Each rank allocates 28,518 to 28,717 blocks. The heap reaches its peak during
initialization, between 0.07 and 0.11 s, when the fields are allocated, and
stays close to it: 47.87 to 47.93 MB are still live at exit, in 1,079 to 1,107
blocks, because CloverLeaf does not deallocate its fields before it ends.

Rank 0 peaks about 157 KB (0.3%) above ranks 1 to 3 in every run, which gives
an imbalance of 1.002 before rounding. Rank 0 is the one that writes
`clover.out`; where the extra bytes come from was not checked. In one of the
three runs, rank 1 set its peak at 3.75 s, about 47 KB above its startup
level, and a later replay showed the same transient at 2.14 s. The time of a
rank's peak can come from such a transient rather than from the startup.

CloverLeaf prints no memory figure. Its result is the same with and without
heapline: "Calculation complete" and the same field summary at step 87 in all
seven runs.

## Run time

The overhead of heapline was not measured here. On this laptop the same LULESH
run without heapline took 20.4 s once and 117.9 s another time, with the load
below 2 before each run and at most 3 other running processes in WSL2.
LULESH's own timer follows the wall time, so the variation is in the
computation. CloverLeaf varies too: without heapline, its own clock read 6.0 s
to 15.6 s for the same deck, with the load below 2 before each run. A
variation of that size hides any overhead.

The clock adds to it: over 60.1 s of `CLOCK_MONOTONIC`, `CLOCK_REALTIME`
advanced 57.1 s under WSL2, in backward steps of about 1.5 s. heapline's
`elapsed_s`, which uses `CLOCK_MONOTONIC`, reads 3.9% to 4.4% above GNU time
`%e` in the three runs of LULESH `a` with heapline, and LULESH's timer 3.3% to
4.2%. In one CloverLeaf run, GNU time read 6.65 s for `mpirun` while heapline's
`elapsed_s` read 8.08 s for a rank inside it.

The memory figures do not depend on the clock or the load: over the three
runs, the heap peaks repeat within 32 bytes for miniAMR and LULESH `b`,
80 bytes for CloverLeaf and 0.015% for LULESH `a`. A fourth LULESH `a` run
under heavy load (337 s instead of about 25 s) stays within 0.05%.

## Limits

- One machine, under WSL2, with one node per job.
- No scenario oversubscribes the 12 cores: each runs at most 8 threads.
- No Slurm, no cluster, so no `sacct` `MaxRSS` and no per-node sum over
  several nodes.
- No overhead figure, for the reasons above.

## Reproduce with Spack

```
git clone --depth 1 --branch v1.2.2 https://github.com/spack/spack.git
. spack/share/spack/setup-env.sh
# heapline is not in the builtin repository yet: add a repository that
# holds heapline/package.py, the recipe given below.
spack repo create ./heapline-repo heapline_local
mkdir -p ./heapline-repo/spack_repo/heapline_local/packages/heapline
# Save the recipe of the next section as
# ./heapline-repo/spack_repo/heapline_local/packages/heapline/package.py
spack repo add ./heapline-repo/spack_repo/heapline_local
spack external find openmpi
spack install heapline@0.5.0 miniamr@1.7.0 lulesh@2.0.3+mpi+openmp~visual \
    cloverleaf@1.1~debug~ieee build=ref
spack test run heapline@0.5.0
spack load miniamr lulesh cloverleaf
```

Once heapline is in the builtin repository, the repository lines go away and
`spack install heapline` is enough.

### The recipe

This is the recipe proposed for the builtin repository. `spack test run` runs its two stand-alone tests:
`heapline-report --version` must print the version of the spec, and the
library preloaded into `true` must print its summary line and write a record.

```python
# Copyright Spack Project Developers. See COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

import os

from spack_repo.builtin.build_systems.cmake import CMakePackage

from spack.package import *


class Heapline(CMakePackage):
    """Heap usage of a Linux program, without recompiling it. heapline is a
    shared library loaded with LD_PRELOAD that intercepts the glibc allocation
    functions and tracks the bytes in use and their peak; heapline-report
    merges the per-rank records of an MPI job."""

    homepage = "https://ethan-puyaubreau.github.io/heapline/"
    url = "https://github.com/ethan-puyaubreau/heapline/archive/refs/tags/v0.5.0.tar.gz"
    git = "https://github.com/ethan-puyaubreau/heapline.git"

    maintainers("ethan-puyaubreau")

    license("Apache-2.0")

    version("main", branch="main")
    version("0.5.0", sha256="2c73bcf2c9cd42af470d64880bf1839ef9978fce403382736fca1580a9f67b7d")
    version("0.4.1", sha256="255b7d3c121b0ffe91ed6d1e21d97391547d74027943d124b3a246c37b36b711")

    depends_on("cxx", type="build")
    depends_on("cmake@3.16:", type="build")

    # The library replaces the glibc allocator through LD_PRELOAD.
    requires("platform=linux", msg="heapline supports Linux only")

    def cmake_args(self):
        return [self.define("HEAPLINE_ENABLE_TESTS", self.run_tests)]

    def test_report_version(self):
        """check that heapline-report prints its version"""
        report = which(join_path(self.prefix.bin, "heapline-report"), required=True)
        out = report("--version", output=str, error=str)
        assert out.startswith("heapline-report "), out
        if not self.spec.satisfies("@main"):
            assert str(self.spec.version) in out, out

    def test_preload(self):
        """preload the library into a dynamically linked program"""
        libs = find_libraries("libheapline", root=self.prefix, shared=True, recursive=True)
        assert libs, f"libheapline.so not found under {self.prefix}"
        true = which("true", required=True)
        csv = join_path(self.test_suite.stage, "heapline.test.csv")
        env = {"LD_PRELOAD": libs[0], "HEAPLINE_OUTPUT": csv}
        err = true(output=str, error=str, extra_env=env)
        # The library always prints a one-line "heapline: ..." summary to
        # stderr at exit and writes the CSV record to HEAPLINE_OUTPUT.
        assert any(line.startswith("heapline:") for line in err.splitlines()), err
        assert os.path.getsize(csv) > 0, f"{csv} is empty"
```

### The runs

The miniAMR arguments:

```
--npx 2 --npy 2 --npz 2 --init_x 1 --init_y 1 --init_z 1 --nx 8 --ny 8 --nz 8
--num_vars 10 --num_refine 3 --max_blocks 800 --num_tsteps 400
--stages_per_ts 10 --checksum_freq 5 --report_perf 4 --num_objects 1
--object 2 0 -1.71 -1.71 -1.71 0.01 0.01 0.01 1.7 1.7 1.7 0.0 0.0 0.0
```

CloverLeaf reads `clover.in` from the current directory and writes
`clover.out` there:

```
cp $(spack location -i cloverleaf)/doc/tests/clover_bm_short.in clover.in
```

Start `miniAMR.x` with `-n 8 ... OMP_NUM_THREADS=1`, `lulesh2.0 -s 40 -i 400`
with `-n 8 ... OMP_NUM_THREADS=1`, `lulesh2.0 -s 50 -i 400` with
`--bind-to none -n 1 ... OMP_NUM_THREADS=8`, and `clover_leaf` with
`-n 4 ... OMP_NUM_THREADS=2`, each with the launch line of [Setup](#setup).
