# Tutorial: measure the memory of a program

This tutorial measures the memory that a program allocates, and when it
needs it. It takes about ten minutes on any Linux machine, needs no MPI and no
change to the program, which here is a Python one-liner. For a parallel job,
follow it first, then the [MPI tutorial](tutorial-mpi.md).

## 1. Get heapline

Build it from source:

```
git clone https://github.com/ethan-puyaubreau/heapline.git
cd heapline
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds `build/libheapline.so` and `build/heapline-report`. A prebuilt
archive also exists, see [Getting started](getting-started.md#prebuilt-archives);
its `lib/libheapline.so` and `bin/heapline-report` replace the two files above
in the commands below.

## 2. Run a program under heapline

Preload the library; the program needs no change and no rebuild:

```
LD_PRELOAD=$PWD/build/libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
```

The last line on stderr is the summary:

```text
heapline: 4154 allocs, 4125 frees, peak 288.2 MiB, live at exit 400.2 KiB
```

Python allocated 300 blocks of one million bytes, 286.1 MiB. heapline counts
the usable size of each block, which the allocator rounds up, so they weigh
287.1 MiB, and Python's own allocations add about 1 MiB. `peak` is the highest amount of heap in use at any time, and `live
at exit` what was still allocated when the program ended. The 29 allocations
that were never released are what Python keeps until it exits.

## 3. Keep a record and read it

Set `HEAPLINE_OUTPUT` to also write a record of the process, then read it with
`heapline-report`:

```
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=run.csv python3 -c "x = [bytes(10**6) for i in range(300)]"
cat run.csv
build/heapline-report run.csv
```

The record is a CSV file of three lines, described in [Output
format](format.md):

```text
# heapline 1
rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,vmrss_at_exit_bytes
-1,node01,17438,,0.015,4154,4125,302236152,0.007,409800,11534336,10485760
```

The host name is shown here as `node01`. Your pid will differ, and so will
the times and `VmHWM`, slightly.
The report of this one process reads:

```text
heapline-report: 1 rank on 1 node, longest process 0.0 s

Peak heap
  max        288.2 MiB, rank -1 at 0.0 s
  median     288.2 MiB
  min        288.2 MiB
  imbalance  1.00 (max / mean)

Per node, sum of VmHWM (an upper bound to compare with --mem)
  node01  1 rank   11.0 MiB

Ranks by peak heap (1 of 1)
  rank  host     peak heap  at (s)       VmHWM  heap / VmHWM
    -1  node01   288.2 MiB     0.0    11.0 MiB  26.20

Slurm: sacct MaxRSS is a per-task maximum sampled during the job. Compare it
with the largest VmHWM above, 11.0 MiB; a short peak can fall between samples.
```

The rank is -1 because the program did not run under MPI. The report is made
for jobs of many processes, so with one it repeats the same figures. The lines
about `--mem` and Slurm matter for cluster jobs; see the [MPI
tutorial](tutorial-mpi.md).

## 4. Understand the numbers

Two figures look contradictory: the heap peak is 288.2 MiB, yet `VmHWM`, the
highest amount of memory that the process had in RAM, is only 11.0 MiB, and
the ratio of the two, 26.20, is far above 1. Python asks the allocator for
large zeroed blocks, and the kernel provides them without using RAM until a
page is written. Python writes almost none of them, only the header page of
each block, about 1 MiB in total. The program allocated 288 MiB and used
almost none of it. Make it write every byte:

```
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=touched.csv python3 -c "x = [b'x' * 10**6 for i in range(300)]"
build/heapline-report touched.csv
```

The heap peak is the same, 288.2 MiB, and the ranks table becomes:

```text
Ranks by peak heap (1 of 1)
  rank  host     peak heap  at (s)       VmHWM  heap / VmHWM
    -1  node01   288.2 MiB     0.2   297.1 MiB  0.97
```

Now `VmHWM` exceeds the heap peak by 8.9 MiB. That gap is memory that heapline
does not count as heap: the code of Python, its libraries, stacks. It is
constant, so it matters on a small heap and hardly at all on a large one.

The two figures answer two questions. The heap peak says how much the program
asked for. `VmHWM` says how much RAM it used, which is what a job scheduler
limits, so it is the one to compare with a memory request. [What "peak
memory" means](peak-memory.md) lists every measure.

## 5. When does the peak happen?

The record holds the time of the heap peak, `peak_heap_time_s`. Compare a
program that allocates first and then idles for two seconds with one that idles
first:

```
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=early.csv python3 -c "import time; x = bytes(300 * 10**6); del x; time.sleep(2)"
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=late.csv python3 -c "import time; time.sleep(2); x = bytes(300 * 10**6)"
build/heapline-report early.csv | grep '^  max'
build/heapline-report late.csv | grep '^  max'
```

```text
  max        287.2 MiB, rank -1 at 0.0 s
  max        287.2 MiB, rank -1 at 2.0 s
```

Both peak at 287.2 MiB, one at the start and one at the end. For a job that
shares a node with others, or that must not exceed a limit at one moment, the
time of the peak tells where to look in the program.

## 6. A pitfall: wrapper processes

`LD_PRELOAD` reaches every process that the preloaded command starts. If you
run the program through a shell, the shell is measured too, and it usually
exits last and replaces the record of the program:

```
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=wrapped.csv bash -c "python3 -c \"x = [bytes(10**6) for i in range(300)]\"; echo done"
```

```text
heapline: 4154 allocs, 4125 frees, peak 288.2 MiB, live at exit 400.2 KiB
done
heapline: 897 allocs, 278 frees, peak 84.5 KiB, live at exit 83.9 KiB
```

`wrapped.csv` holds the second line, the shell's, not the 288 MiB of Python.
Preload the program itself, not a wrapper around it; [Wrapper
processes](mpi.md#wrapper-processes) gives the ways to do so.

## 7. Next steps

- [Tutorial: how much memory does my MPI job need?](tutorial-mpi.md) measures
  a job of several processes and sizes its memory request.
- [Measuring an MPI job](mpi.md) gives the recipe for `srun` and batch jobs.
- [Output format](format.md) describes every column of the record, and
  [Limitations and alternatives](limitations.md) says when to use another
  tool.
