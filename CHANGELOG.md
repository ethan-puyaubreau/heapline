# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/ethan-puyaubreau/heapline/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/ethan-puyaubreau/heapline/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/ethan-puyaubreau/heapline/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/ethan-puyaubreau/heapline/releases/tag/v0.1.0
