# Limitations and alternatives

## Limitations

- Statically linked programs cannot be intercepted with `LD_PRELOAD`.
- Programs that use another allocator, such as jemalloc or tcmalloc, bypass
  heapline.
- Sizes are usable sizes, slightly above the requested sizes.
- Memory obtained with `mmap` directly, or allocated on a GPU, is not heap
  and is not counted.
- A process killed by a signal writes no record. `heapline-report` lists it as
  a missing rank, unless it was the highest rank of the job, which it cannot
  tell apart from a smaller job.
- The summary on stderr is lost when the program closes stderr before
  exiting, as GNU coreutils do. The record written to `HEAPLINE_OUTPUT` is not.
- The cost is paid on every allocation. On a benchmark that does nothing but
  allocate and release from four threads, heapline makes the program about
  five times slower; programs that compute between allocations see much
  less. See [Performance history](performance.md).

## When to use something else

heapline answers "how much, where in the job, and when". When you need to
know which line of code allocates, use a tool that records call stacks:

- **heaptrack** records every allocation with its stack and comes with a
  viewer. Its overhead is much higher than heapline's.
- **Valgrind massif** takes detailed heap snapshots, at the cost of running
  the program under Valgrind, many times slower.
- **jemalloc** and **tcmalloc** have sampling heap profilers, if the program
  already uses, or can switch to, one of those allocators.
