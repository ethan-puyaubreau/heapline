# What "peak memory" means

Several tools report a "peak memory" for the same run, and they rarely agree.
They do not measure the same thing.

| Measure | Counts | Reported by |
|---|---|---|
| Requested size | Bytes passed to `malloc` and friends | The program itself |
| Usable size | Bytes the allocator actually reserves for each block, slightly above the request | `malloc_usable_size`; heapline |
| Heap peak | Highest sum of usable sizes in use at once | heapline (`peak_heap_bytes`) |
| Peak resident set size | Highest number of pages of the process in RAM: heap pages actually touched, but also code, libraries, stacks, `mmap` regions | Linux, `VmHWM` in `/proc/<pid>/status`; heapline (`vmhwm_bytes`) |
| Slurm `MaxRSS` | Largest resident set size of the tasks of a job step, sampled by the accounting plugin (every 30 s by default, `JobAcctGatherFrequency`) | `sacct` |
| cgroup `memory.peak` | Peak memory charged to the job's cgroup, page cache and kernel memory included (cgroup v2, Linux 5.19 or newer). The out-of-memory killer acts when the cgroup reaches its limit (`memory.max`) after the kernel has reclaimed what it can, such as clean page cache, so `memory.peak` can overstate what the job strictly needs | The kernel |

Why they differ:

- **Heap peak above `VmHWM`**: memory that was allocated but never written is
  not resident. A large array allocated and only partly used counts fully in
  the heap and partly in `VmHWM`.
- **`VmHWM` above heap peak**: everything that is not counted as heap in
  use. Code and libraries take resident pages, and so do thread stacks. MPI
  libraries map shared memory and other regions with `mmap`, which heapline
  does not see; the buffers they obtain with `malloc` are counted. The
  allocator also keeps freed memory and fragments it (arenas per thread,
  pages it retains instead of returning them), so resident memory can stay
  above the heap in use.
- **`MaxRSS` below `VmHWM`**: Slurm samples. A peak shorter than the sampling
  period can fall between two samples.
- **cgroup peak usually the highest**: the page cache of the files the job
  reads and writes is charged to the cgroup. The per-node sum of `VmHWM` can
  still exceed it, since shared pages are counted once per rank.

To size `--mem`, start from the per-node sum of `VmHWM` given by
`heapline-report`, which is an upper bound for the processes it saw, and add
the margin your site recommends for page cache and system overhead.
