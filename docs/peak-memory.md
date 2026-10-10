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
| cgroup `memory.peak` | Peak memory charged to the job's cgroup, page cache and kernel memory included (cgroup v2, Linux 5.19 or newer) | The kernel; what the out-of-memory killer acts on |

Why they differ:

- **Heap peak above `VmHWM`**: memory that was allocated but never written is
  not resident. A large array allocated and only partly used counts fully in
  the heap and partly in `VmHWM`.
- **`VmHWM` above heap peak**: everything that is not heap. MPI libraries
  allocate buffers and map shared memory; code and libraries take resident
  pages; thread stacks too.
- **`MaxRSS` below `VmHWM`**: Slurm samples. A peak shorter than the sampling
  period can fall between two samples.
- **cgroup peak above everything**: the page cache of the files the job reads
  and writes is charged to the cgroup.

To size `--mem`, start from the per-node sum of `VmHWM` given by
`heapline-report`, which is an upper bound for the processes it saw, and add
the margin your site recommends for page cache and system overhead.
