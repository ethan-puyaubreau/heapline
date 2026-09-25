# heapline

Heap usage of a Linux program, without recompiling it.

heapline is a small shared library loaded with `LD_PRELOAD`. It intercepts
the glibc allocation functions, counts allocations and releases, and tracks
the bytes in use and their peak. A summary is printed to stderr at exit.

```
$ LD_PRELOAD=./libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
heapline: 4153 allocs, 4123 frees, peak 288.3 MiB, live at exit 472.2 KiB
```

## Build

Requirements: Linux with glibc, CMake 3.16 or newer, a C++17 compiler.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

| Option | Default | Description |
|---|---|---|
| `HEAPLINE_ENABLE_TESTS` | `ON` | Build the tests |
| `HEAPLINE_ENABLE_WERROR` | `OFF` | Treat compiler warnings as errors |

## How it works

- `malloc`, `calloc`, `realloc`, `free`, `posix_memalign` and `aligned_alloc`
  are forwarded to the next definition found with `dlsym(RTLD_NEXT, ...)`.
- Block sizes come from `malloc_usable_size`, so no table of live pointers is
  kept and nothing is allocated on the interception path.
- Counters are lock-free atomics. A `realloc` counts as one release and one
  allocation.
- A program can read the counters at runtime through `heapline_get_stats`,
  declared in `include/heapline.h`, by looking the symbol up with `dlsym`.

## Limitations

- Statically linked programs cannot be intercepted.
- Programs using another allocator, such as jemalloc or tcmalloc, bypass
  heapline.
- Blocks from `memalign`, `valloc` and `pvalloc` are not tracked, and
  releasing them lowers the live byte count.
- Sizes are usable sizes, slightly above the requested sizes.
- The summary is lost when the program closes stderr before exiting, as
  GNU coreutils do.

## License

Apache-2.0. See [LICENSE](LICENSE).
