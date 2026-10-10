# Output format

`HEAPLINE_OUTPUT` receives one record per process. This page describes
version 1.

## Layout

A record is a small CSV file of three lines:

```
# heapline 1
rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,vmrss_at_exit_bytes
3,node-a,51234,1655310,12.480,4153,4123,302268416,4.211,483532,331431936,9175040
```

The first line names the format and its version. Tools must check it and
refuse versions they do not know: `heapline-report` does. Adding a column or
changing the meaning of one raises the version.

## Columns

| Column | Unit | Meaning |
|---|---|---|
| `rank` | | MPI rank from `OMPI_COMM_WORLD_RANK`, `PMIX_RANK`, `PMI_RANK`, `MV2_COMM_WORLD_RANK` or `SLURM_PROCID`, in this order; -1 outside MPI |
| `host` | | Host name; commas, quotes and line breaks are replaced with `_` |
| `pid` | | Process id |
| `job` | | `SLURM_JOB_ID`, empty outside Slurm |
| `elapsed_s` | s | Time from the loading of the library to the end of the process; written with three decimals and a dot, whatever the locale of the program |
| `allocs` | | Successful allocations; a `realloc` counts as one |
| `frees` | | Released blocks; a `realloc` counts as one |
| `peak_heap_bytes` | bytes | Highest heap in use, in usable sizes; see the precision below |
| `peak_heap_time_s` | s | Time of that peak since the loading of the library, read from a coarse clock (one kernel tick, 1 to 10 ms); written with three decimals and a dot, whatever the locale of the program |
| `live_heap_at_exit_bytes` | bytes | Heap still in use at exit |
| `vmhwm_bytes` | bytes | Peak resident set size of the process (`VmHWM`), -1 if unknown |
| `vmrss_at_exit_bytes` | bytes | Resident set size at exit (`VmRSS`), -1 if unknown |

The peak is exact as long as only one thread of the process has allocated.
When several threads allocate, each thread counts in its own counters and
publishes its changes in batches, so that threads do not wait for each other.
The peak may then differ from the true peak by up to ceil(log2 N)/8192 of it,
N being the number of threads that have allocated: 0.025% with 4 threads,
0.05% with 16, 0.073% with 64, or 3 MiB for a 4 GiB peak with 64 threads.
`allocs`, `frees` and `live_heap_at_exit_bytes` count every thread exactly,
threads still running at exit included, except for operations in progress at
that instant. The peak and its time are stored separately: under concurrent
allocation, the time may belong to a slightly different peak than the one
reported.

## File names

In `HEAPLINE_OUTPUT`, `%p` is replaced with the process id, `%r` with the
rank and `%h` with the host name. Any other `%` sequence is kept as is. The
directory must exist; when the file cannot be opened, the record goes to
stderr.
