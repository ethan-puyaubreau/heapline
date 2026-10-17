# heapline

Heap usage of a Linux program, without recompiling it.

heapline is a small shared library loaded with `LD_PRELOAD`. It intercepts
the glibc allocation functions, counts allocations and releases, and tracks
the bytes in use and their peak. A summary is printed to stderr at exit.

Documentation, tutorials (one program, and an MPI job) and performance
history:
<https://ethan-puyaubreau.github.io/heapline/>

```
$ LD_PRELOAD=./libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
heapline: 4153 allocs, 4123 frees, peak 288.3 MiB, live at exit 472.2 KiB
```

## Build

Prebuilt archives for Linux x86_64 and aarch64, and RPM and Debian packages,
are on the
[release page](https://github.com/ethan-puyaubreau/heapline/releases); the
"Getting started" page of the documentation describes them. To build from
source:

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
| `HEAPLINE_ENABLE_EXAMPLES` | `OFF` | Build the examples, which need MPI |
| `HEAPLINE_ENABLE_TSAN` | `OFF` | Build the ThreadSanitizer stress test |
| `HEAPLINE_ENABLE_BENCH` | `OFF` | Build the overhead benchmark |
| `HEAPLINE_STATIC_RUNTIME` | `OFF` | Link `heapline-report` with libstdc++ and libgcc statically, which needs `libstdc++-static` on RHEL |

## Output

A one-line summary always goes to stderr. Set `HEAPLINE_OUTPUT` to also write
a record of the process as CSV, read by `heapline-report`, pandas or a
spreadsheet:

```
# heapline 1
rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,vmrss_at_exit_bytes
3,node-a,51234,1655310,12.480,4153,4123,302268416,4.211,483532,331431936,9175040
```

In the file name, `%p` is replaced with the process id, `%r` with the MPI
rank and `%h` with the host name, so each rank gets its own file:

```
mpirun -n 4 env HEAPLINE_OUTPUT=heapline.%h.%r.csv LD_PRELOAD=$PWD/libheapline.so ./app
```

Preload the program itself, as `env` does above: a wrapper script run without
`exec`, or `/usr/bin/time`, that is preloaded too has the same rank and host,
and its record overwrites the program's unless the file name contains `%p`.

The rank is read from `OMPI_COMM_WORLD_RANK`, `PMIX_RANK`, `PMI_RANK`,
`MV2_COMM_WORLD_RANK` or `SLURM_PROCID`, in this order, and is -1 outside MPI.
If the file cannot be opened, the record goes to stderr.

## Job report

`heapline-report` merges the records of a job and prints the peak heap of
each rank, when it was reached, the imbalance between ranks and, per node, the
sum of `VmHWM` to compare with the `--mem` request of a Slurm job:

```
heapline-report records/
```

`--top N` lists the N ranks with the highest peak (10 by default), `--all`
lists every rank and `--csv FILE` writes all records, sorted by rank, to one
file for further analysis.

## How it works

- `malloc`, `calloc`, `realloc`, `free`, `posix_memalign`, `aligned_alloc`,
  `memalign`, `valloc` and `pvalloc` are forwarded to the next definition
  found with `dlsym(RTLD_NEXT, ...)`.
- Block sizes come from `malloc_usable_size`, so no table of live pointers is
  kept and nothing is allocated on the interception path.
- Each thread counts in its own counters and publishes its changes in
  batches; reading the totals takes a mutex. A `realloc` counts as one release
  and one allocation.
- A program can read the counters at runtime through `heapline_get_stats`,
  declared in `include/heapline.h`, by looking the symbol up with `dlsym`.

## Limitations

- Statically linked programs cannot be intercepted.
- Programs using another allocator, such as jemalloc or tcmalloc, bypass
  heapline.
- Sizes are usable sizes, slightly above the requested sizes.
- The summary on stderr is lost when the program closes stderr before
  exiting, as GNU coreutils do. The record written to `HEAPLINE_OUTPUT` is not.

## License

Apache-2.0. See [LICENSE](LICENSE).
