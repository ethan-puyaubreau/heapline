# Limitations and alternatives

## Limitations

- Statically linked programs cannot be intercepted with `LD_PRELOAD`.
- Programs that use another allocator, such as jemalloc or tcmalloc, bypass
  heapline.
- Sizes are usable sizes, slightly above the requested sizes.
- With several threads, the peak may differ from the true peak by up to
  1/4096 of it, see [Output format](format.md). heapline must be loaded at
  startup with `LD_PRELOAD`; loading it later with `dlopen` is not supported.
- Memory obtained with `mmap` directly, or allocated on a GPU, is not heap
  and is not counted.
- A process killed by a signal writes no record. `heapline-report` lists it as
  a missing rank, unless it was the highest rank of the job, which it cannot
  tell apart from a smaller job.
- The summary on stderr is lost when the program closes stderr before
  exiting, as GNU coreutils do. The record written to `HEAPLINE_OUTPUT` is not.
- A wrapper process can overwrite the record of the program. A script run
  without `exec`, `/usr/bin/time` or any other process that stays the parent of
  the program, when preloaded with heapline, has the same rank and host as the
  program and exits last. With `HEAPLINE_OUTPUT=heapline.%h.%r.csv`, its record
  replaces the program's without a warning: a `run.sh` that launched a 288 MiB
  Python program left the record of bash, with a peak of 158.7 KiB. Start the
  program with `exec` and preload it alone, or add `%p` to the file name. See
  [Wrapper processes](mpi.md#wrapper-processes).
- The cost is paid on every allocation. On a benchmark that does nothing but
  allocate and release from four threads, heapline makes the program about
  five times slower. Programs that compute between allocations pay it less
  often, but they were not measured here. See
  [Performance history](performance.md).

## When to use something else

heapline answers "how much, where in the job, and when". When you need to
know which line of code allocates, use a tool that records call stacks:

- **heaptrack** records every allocation with its stack and comes with a
  viewer. It records a call stack for every allocation, so each allocation
  costs more than with heapline.
- **Valgrind massif** takes detailed heap snapshots, at the cost of running
  the program under Valgrind, many times slower.
- **jemalloc** and **tcmalloc** have sampling heap profilers, if the program
  already uses, or can switch to, one of those allocators.
