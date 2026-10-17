# How heapline is tested

Each push to `main` builds heapline with GCC and Clang and runs its tests.
They check exact counts wherever the numbers are exact, and the documented
bounds where they are not, such as the peak with several threads (see
[Output format](format.md)). Two more builds run the code under sanitizers.

## What the tests check

| Area | Tests | What they check |
|---|---|---|
| Entry points | `alloc_functions`, `alloc_pattern` | Each allocation function on its own, with `realloc` growing, shrinking and moving a block, failed allocations and `free_sized`: one allocation or release each, and a change of the live bytes equal to the usable size, exactly. Then a known pattern of all of them together |
| Threads | `alloc_threads`, `stale_thread`, `peak_threads_bound`, `peak_threads_monotonic`, `peak_time_threads` | Exact totals with threads that exit holding blocks, a thread that keeps its counters after its exit and whose stack is reused, the peak within its bound and never decreasing, the time of the peak when threads exit |
| Fork | `fork_child` | A child forked while other threads are alive starts a thread, reads the counters without hanging, and holds the counts of its parent once |
| Records | `record_*`, `record`, `peak_time_early`, `peak_time_late` | The rank read from each launcher variable and their order, the fallback to stderr, the CSV under a locale that writes a decimal comma, the time of an early and a late peak |
| Report | `report_*` | `heapline-report` against recorded jobs: summary, `--top`, `--all`, `--csv`, and the warnings for missing, duplicate and huge ranks, truncated records, unknown versions and several jobs |
| Packaging | `library_dependencies`, `report_pie`, `man_page` | The library needs the C library only, `heapline-report` is a position independent executable, and its manual page renders without warnings. The release workflow also checks that the archives need nothing newer than glibc 2.17, see [Getting started](getting-started.md#prebuilt-archives), and installs, runs and removes the RPM on Rocky Linux 8 and 9 and the Debian package on Debian 12 and Ubuntu 22.04 |
| Tutorials | CI job `MPI example` | The MPI example on four ranks and its peaks, and the commands of the [plain tutorial](tutorial.md), with the relations it explains: untouched blocks not resident, an early and a late peak, a wrapper that overwrites the record |

Tests that need more address space than the machine allows, such as the
4 GiB block of `alloc_threads`, skip themselves rather than fail.

## Sanitizers

### Undefined behavior

The whole suite is built with `-fsanitize=undefined
-fno-sanitize-recover=all`, and the library is preloaded as usual. The first
undefined behavior, in the library or in a test, aborts the test.
`library_dependencies` is skipped: the sanitizer runtime adds a dependency.

### Data races

ThreadSanitizer cannot check the library while it replaces `malloc`. Its
runtime allocates while it starts a thread, before the thread exists for it,
and the code of heapline that runs at that moment gives false races and
crashes.

`tsan_stress` builds the library into the test instead, with its entry
points renamed (`malloc` becomes `heapline_test_malloc`), and calls them
itself. Waves of threads allocate, reallocate and release blocks, release
blocks of other threads and exit holding blocks, while a reader takes
snapshots and the main thread forks once per wave. TSan checks the
registration and exit of threads, the snapshots and the fork handlers; the
test checks that the peak never decreases nor falls below the live bytes,
the counts of each forked child, and the exact final totals.

CI builds it with Clang 18. With the TSan runtime of GCC 13 and 14, about one
forked child in a hundred hangs in the TSan allocator, whatever heapline
does.

What this does not cover: under TSan, only the operations of the test are
counted, not those of glibc, and the preloaded library itself is not checked
for races. The thread tests above cover it, without a sanitizer, and so does
the undefined behavior build.

## Run them yourself

The tests:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The data race test, with Clang 18 or newer:

```
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DHEAPLINE_ENABLE_TSAN=ON
cmake --build build-tsan --target tsan_stress
ctest --test-dir build-tsan --output-on-failure -R tsan_stress
```

The undefined behavior build, with GCC:

```
SANITIZE="-fsanitize=undefined -fno-sanitize-recover=all"
cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=g++ "-DCMAKE_CXX_FLAGS=$SANITIZE" \
  "-DCMAKE_SHARED_LINKER_FLAGS=$SANITIZE" "-DCMAKE_EXE_LINKER_FLAGS=$SANITIZE"
cmake --build build-ubsan
ctest --test-dir build-ubsan --output-on-failure -E library_dependencies
```
