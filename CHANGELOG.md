# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/ethan-puyaubreau/heapline/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ethan-puyaubreau/heapline/releases/tag/v0.1.0
