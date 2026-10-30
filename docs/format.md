# Output format

`HEAPLINE_OUTPUT` receives one record per process. This page describes
version 2, written since heapline 0.6.0.

## Layout

A record is a small CSV file of three lines:

```
# heapline 2
rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,vmrss_at_exit_bytes,complete
3,node-a,51234,1655310,12.480,4153,4123,302268416,4.211,483532,331431936,9175040,1
```

The first line names the format and its version. Tools must check it and
refuse versions they do not know: `heapline-report` does. Adding a column or
changing the meaning of one raises the version.

Version 2 adds the `complete` column at the end. Version 1, written by
heapline 0.5 and earlier, has the other twelve columns with the same meaning,
and all its records are complete. `heapline-report` reads both.

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
| `complete` | | 1 when the record was written at exit; 0 when it is the last snapshot of a process that did not reach its exit, see [Snapshots](#snapshots) |

The peak is exact as long as only one thread of the process has allocated or
released memory. When several threads do, each thread counts in its own
counters and publishes its changes in batches, so that threads do not wait for
each other. The peak may then differ from the true peak by up to
(ceil(log2 N) + 1)/8192 of it, N being the number of threads that have
allocated or released memory: 0.037% with 4 threads, 0.061% with 16, 0.085%
with 64, or 3.5 MiB for a 4 GiB peak with 64 threads. In both cases, as
before, blocks that other threads are allocating or releasing at that instant
may be missing. `allocs`, `frees` and `live_heap_at_exit_bytes` count every
thread exactly, threads still running at exit included, except for operations
in progress at that instant. The peak and its time are stored separately:
under concurrent allocation, the time may belong to a slightly different peak
than the one reported, and with several threads it may be the time of an
earlier moment, before bytes held back by threads were published.

## Snapshots

A process killed with SIGKILL, as the OOM killer does, runs no code at exit.
So that it still leaves a record, heapline rewrites the record while the
process runs: once the heap peak reaches 16 MiB, then each time the peak has
grown by 1/64. That is about 700 snapshots at most from 16 MiB to 1 TiB, and
none for a process that stays below 16 MiB. The record written at exit
replaces the last snapshot.

In a snapshot, `complete` is 0, `elapsed_s` is the time of the snapshot, and
`live_heap_at_exit_bytes`, `vmhwm_bytes` and `vmrss_at_exit_bytes` are the
values at that moment. The heap peak grew by less than 1/64 after the last
snapshot, unless a snapshot was skipped because another thread held the lock
of heapline and the peak did not rise again. Pages touched after it are not in `vmhwm_bytes`, and the OOM killer
acts on resident memory, so the resident peak at the kill can be well above
the one recorded.

A process that ends without running the destructors of its libraries, with
`_exit`, an abort or a crash, also leaves its last snapshot. So does a process
that replaces itself with `exec`, unless the new program is preloaded with
heapline too and writes its own record to the same file. A forked child takes
its own snapshots, in its own file when the name holds `%p`.

Set `HEAPLINE_SNAPSHOT=0` to write the record at exit only, as heapline 0.5
did.

## File names

In `HEAPLINE_OUTPUT`, `%p` is replaced with the process id, `%r` with the
rank and `%h` with the host name. Any other `%` sequence is kept as is. The
directory must exist; when the file cannot be opened, the record goes to
stderr.
