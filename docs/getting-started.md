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

This builds `build/libheapline.so` and `build/heapline-report`. What the
tests check, and the sanitizer builds that CI runs, are described in [How
heapline is tested](testing.md).
To install them, run `cmake --install build --prefix $HOME/opt/heapline`: the
library goes to `$HOME/opt/heapline/lib` (`lib64` on some distributions, such
as RHEL, Rocky or SUSE) and `heapline-report` to `$HOME/opt/heapline/bin`.

## Prebuilt archives

The [release page](https://github.com/ethan-puyaubreau/heapline/releases)
offers `heapline-vX.Y.Z-linux-x86_64.tar.gz` and
`heapline-vX.Y.Z-linux-aarch64.tar.gz`, each with its `.sha256` file. They
are built on Rocky Linux 8 and need nothing newer than glibc 2.17, so they
load on RHEL 8 and later and on current Ubuntu and Debian. The library
depends on the C library only. Check the archive for your machine (`uname -m`
prints its architecture) and unpack it with:

```
sha256sum -c heapline-vX.Y.Z-linux-x86_64.tar.gz.sha256
tar xzf heapline-vX.Y.Z-linux-x86_64.tar.gz
```

On aarch64, replace `x86_64` with `aarch64` in these names and below.
The archive extracts into a directory `heapline-vX.Y.Z-linux-x86_64/`, which
holds `lib/libheapline.so`, `bin/heapline-report`, `include/heapline.h` and
the manual page `share/man/man1/heapline-report.1`.
Use that library and tool where this page says `build/libheapline.so` and
`build/heapline-report`, for example:

```
LD_PRELOAD=$PWD/heapline-vX.Y.Z-linux-x86_64/lib/libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
```

## Packages

The release page also offers an RPM and a Debian package for both
architectures, each with its `.sha256` file. They hold the files of the
archive. On RHEL, Rocky or Alma 8 and later, check and install the RPM with:

```
sha256sum -c heapline-X.Y.Z-1.el8.x86_64.rpm.sha256
sudo dnf install ./heapline-X.Y.Z-1.el8.x86_64.rpm
```

On Debian or Ubuntu, check and install the Debian package with:

```
sha256sum -c heapline_X.Y.Z-1_amd64.deb.sha256
sudo apt install ./heapline_X.Y.Z-1_amd64.deb
```

On aarch64, the names end in `aarch64.rpm` and `arm64.deb`. The RPM installs
the library as `/usr/lib64/libheapline.so`, the Debian package as
`/usr/lib/x86_64-linux-gnu/libheapline.so` (`aarch64-linux-gnu` on aarch64).
Both install `heapline-report` in `/usr/bin`, `heapline.h` in `/usr/include`
and the manual page, read with `man heapline-report`. The library is then in
the default search path of the dynamic linker, so `LD_PRELOAD` takes its name
without a path:

```
LD_PRELOAD=libheapline.so python3 -c "x = [bytes(10**6) for i in range(300)]"
```

On a cluster, the package must be installed on every node that runs the
program, not only on the login node: where the library is missing, the
dynamic linker prints that it cannot be preloaded, ignores it, and the job
runs without writing any record. Otherwise, unpack the archive on a shared
file system and give `LD_PRELOAD` its absolute path.

Use `libheapline.so` and `heapline-report` where this page says
`$PWD/build/libheapline.so` and `build/heapline-report`.

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
job, see the [MPI tutorial](tutorial-mpi.md). The [plain
tutorial](tutorial.md) walks through one program step by step.
