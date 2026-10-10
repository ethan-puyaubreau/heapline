# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.1] - 2026-10-11

### Added

- Tests `library_dependencies`, `peak_threads_bound` and
  `peak_threads_monotonic`.
- The `HEAPLINE_STATIC_RUNTIME` option links `heapline-report` with libstdc++
  and libgcc statically. It is off by default, and the release archive turns
  it on.

### Changed

- The release archive is built on Rocky Linux 8 and checked in CI:
  `libheapline.so` needs only the C library and nothing newer than glibc 2.17,
  so it loads on RHEL, Rocky and Alma 8 and later. Earlier archives, built on
  Ubuntu 24.04, need glibc 2.34 (0.1.0 and 0.2.0) or glibc 2.38 (0.3.0 and
  0.4.0) and libstdc++; on older systems build those versions from source.
- The library is compiled without exceptions and run-time type information
  and no longer depends on libstdc++ or libgcc_s.

### Fixed

- `heapline_get_stats` no longer reports a lower peak than on a previous call.

## [0.4.0] - 2026-10-10

### Added

- `malloc_churn` options for the size range and a resident block, a Python
  workload, and `run_overhead.sh` for any command. The benchmark workflow
  measures five scenarios and the performance page plots each one.

### Changed

- On the `resident-4t` benchmark, four threads went from about 340% to about
  20% of overhead, and on `churn-4t`, where the heap is small, from about 365%
  to about 170%.
- Each thread counts its allocations in its own counters and publishes its
  bytes in batches, so threads no longer contend for the shared counters.
  When several threads allocate or release memory, the heap peak may differ
  from the true peak by up to (ceil(log2 N) + 1)/8192 of it for N threads
  (0.037% with 4 threads, 0.085% with 64); it stays exact as long as only one
  thread has allocated or released memory. In both cases, blocks that other
  threads are allocating or releasing at that instant may be missing. The
  other counters stay exact, threads still running at exit included. Folding
  in the bytes that a thread held back does not move the time of the peak, and
  the peak is never below the live bytes.
- `heapline_get_stats` takes a mutex, so it must not be called from a signal
  handler or a fork handler.
- heapline must be loaded at startup with `LD_PRELOAD`; loading it with
  `dlopen` is not supported.
- The library links with the threads library.

## [0.3.0] - 2026-10-10

### Added

- The record written to `HEAPLINE_OUTPUT` holds the MPI rank, the host, the
  Slurm job id, the time of the heap peak and the peak resident set size
  (`VmHWM`) of the process. The rank is read from `OMPI_COMM_WORLD_RANK`,
  `PMIX_RANK`, `PMI_RANK`, `MV2_COMM_WORLD_RANK`, then `SLURM_PROCID`, so
  ranks started by `mpirun` inside a Slurm allocation get their MPI rank.
- `%r` and `%h` in `HEAPLINE_OUTPUT` expand to the rank and the host name.
- `heapline-report` summarizes the records of a job: peak heap per rank and
  its time, median, minimum and imbalance, and the sum of `VmHWM` per node.
- `heapline-report` options `--top N`, `--all`, `--csv FILE` and `--version`.
  It warns about records that share a rank and about records from several
  Slurm jobs.
- `HEAPLINE_ENABLE_EXAMPLES` option, which builds the MPI program
  `examples/mpi_imbalance.cpp`, whose ranks allocate unevenly.
- `HEAPLINE_ENABLE_BENCH` option, which builds the `bench/malloc_churn.cpp`
  benchmark, and `bench/run_overhead.sh`, which measures the overhead of
  heapline on it.
- Documentation site with a tutorial, an MPI guide, the output format and a
  performance history: <https://ethan-puyaubreau.github.io/heapline/>

### Changed

- **Breaking:** `HEAPLINE_OUTPUT` now receives a versioned CSV record instead
  of the one-line summary, which always goes to stderr. Scripts that read the
  summary from the file should read stderr or the CSV columns instead.

## [0.2.0] - 2026-09-27

### Added

- `HEAPLINE_OUTPUT` writes the summary to a file instead of stderr. `%p` in the
  name is replaced with the process id, so each MPI rank gets its own file.
- `memalign`, `valloc` and `pvalloc` are intercepted. Releasing their blocks no
  longer lowers the live byte count below what is actually allocated.

## [0.1.0] - 2026-09-25

### Added

- `LD_PRELOAD` interception of `malloc`, `calloc`, `realloc`, `free`,
  `posix_memalign` and `aligned_alloc`.
- Allocation and release counts, live bytes and peak bytes, with block sizes
  read from `malloc_usable_size`.
- Summary printed to stderr at exit.
- `heapline_get_stats`, declared in `include/heapline.h`, to read the counters
  at runtime.
- CI on GCC and Clang with a clang-format check, and release archives built on
  version tags.

[Unreleased]: https://github.com/ethan-puyaubreau/heapline/compare/v0.4.1...HEAD
[0.4.1]: https://github.com/ethan-puyaubreau/heapline/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/ethan-puyaubreau/heapline/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/ethan-puyaubreau/heapline/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/ethan-puyaubreau/heapline/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/ethan-puyaubreau/heapline/releases/tag/v0.1.0
