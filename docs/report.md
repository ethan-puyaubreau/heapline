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

Files that are not version 1 records, or that lack their values (a process
killed before it could write them), are reported on stderr and skipped. Gaps
in the rank numbers are reported as missing ranks. Missing ranks are the gaps
below the highest rank seen, so a lost last rank cannot be detected. A warning
names two files that hold the same rank (for example stale records from an
earlier run in the same directory), and lists the jobs when records come from
several Slurm jobs. The exit code is 1 when no record is valid or an error
occurs, 0 otherwise.

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
memory is not heap: libraries, stacks, MPI buffers or memory mapped directly
with `mmap`. See [What "peak memory" means](peak-memory.md).

**Slurm**: how this compares with `sacct`.
