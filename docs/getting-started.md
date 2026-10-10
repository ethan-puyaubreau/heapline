# Getting started

## Build

heapline needs Linux with glibc, CMake 3.16 or newer and a C++17 compiler.

```
git clone https://github.com/ethan-puyaubreau/heapline.git
cd heapline
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `build/libheapline.so` and `build/heapline-report`.
`cmake --install build --prefix <dir>` installs them under `<dir>/lib` and
`<dir>/bin`.

## Run a program under heapline

Preload the library; the program needs no change and no rebuild:

```
LD_PRELOAD=$PWD/build/libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
```

A one-line summary is printed to stderr at exit:

```
heapline: 4153 allocs, 4123 frees, peak 288.3 MiB, live at exit 472.2 KiB
```

`peak` is the highest amount of heap in use at any time, `live at exit` what
was still allocated when the program ended.

## Keep a record

Set `HEAPLINE_OUTPUT` to also write a CSV record of the process:

```
LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=run.csv python3 -c "x = [bytes(10**6) for i in range(300)]"
build/heapline-report run.csv
```

The record adds the time of the peak and the peak resident set size of the
process; its columns are described in [Output format](format.md). For an MPI
job, see the tutorial.
