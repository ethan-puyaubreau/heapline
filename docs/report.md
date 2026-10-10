# heapline-report

```
heapline-report [--top N | --all] [--csv FILE] RECORD_OR_DIR...
```

Reads the records given as files or found as `*.csv` files in directories,
and prints a summary of the job.

| Option | Effect |
|---|---|
| `--top N` | List the N ranks with the highest heap peak (10 by default) |
| `--all` | List every rank |
| `--csv FILE` | Also write all records, sorted by rank, to one CSV file. Write it outside the records directory, since every `.csv` file of that directory is read as a record |
| `--version` | Print the version and exit |

Files that are not version 1 records, or that lack their values, are reported
on stderr and skipped. A process killed by a signal writes no file at all: an
empty or truncated file comes from a failed write, or from a kill between the
opening of the file and the write. Gaps in the rank numbers are reported as
missing ranks. Missing ranks are the gaps below the highest rank seen, so a
lost last rank cannot be detected. A warning names two files that hold the
same rank (for example stale records from an earlier run in the same
directory, or the record of a [wrapper process](mpi.md#wrapper-processes)
next to the program's), and lists the jobs when records come from several
Slurm jobs. The exit code is 1 when no record is valid or an error occurs, 0
otherwise.

## Reading the output

**Peak heap**: the highest peak and which rank reached it, when; the median
and the minimum over ranks; and the imbalance, the highest peak divided by the
mean peak. 1.00 means every rank peaked at the same level.

**Per node**: the number of ranks of each node and the sum of their `VmHWM`.
It is an upper bound of what the node needed at once, since ranks do not
always peak together, and it is the figure to compare with `--mem`, which
Slurm applies per node.

**Ranks by peak heap**: for each rank, the heap peak, its time, `VmHWM` and
the ratio of the two. A ratio well below 1 means that much of the resident
memory is not heap in use: libraries, stacks, shared memory and other regions
mapped with `mmap` (MPI libraries do this), or freed memory that the allocator
keeps. See [What "peak memory" means](peak-memory.md).

**Slurm**: how this compares with `sacct`.
