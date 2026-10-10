# Performance history

heapline must stay cheap enough to leave on in production runs. Each push to
`main` measures its overhead on five scenarios:

| Scenario | Command | What it shows |
|---|---|---|
| `churn-1t` | `malloc_churn 1 10000000` | Cost per allocation, without contention |
| `churn-4t` | `malloc_churn 4 10000000` | Four threads that only allocate, in a small heap: the worst case |
| `resident-4t` | `malloc_churn 4 10000000 16 4096 512` | The same churn in a process that holds 512 MiB, like a simulation with its arrays |
| `large-4t` | `malloc_churn 4 2500000 65536 1048576` | Blocks of 64 KiB to 1 MiB, published on every operation |
| `python` | `python3 bench/python_churn.py` | A real program: the Python interpreter |

`malloc_churn` keeps 64 blocks per thread and replaces them in turn, with a
pseudo-random size. In a small heap, the threads publish almost every change,
which is what keeps `churn-4t` slow; when the process holds more memory, as in
`resident-4t`, they publish in batches. `large-4t` also publishes on every
operation, but each of its allocations costs more, so the shared counter is a
smaller part of the time. Points measured before scenarios existed belong to
`churn-4t`. Points of `churn-4t` measured before 0.4.0 were timed by the
benchmark itself; later points time the whole command, which adds a few
milliseconds.

The figure is the relative overhead measured within one CI job: the median
time of ten runs with heapline, divided by the median of ten runs without it,
interleaved, minus one. Absolute times are not comparable between runs,
because GitHub's shared runners change machines. The ratio mostly is, but
expect noise of a few tens of percentage points from one point to the next.

## Release comparison

Measured on one machine, under WSL2, with
`bench/run_overhead.sh SCENARIO LIB 10 -- COMMAND` and ten runs per point.
Each figure is the median of three rounds that alternate the two libraries,
with the same benchmark binaries.

| Scenario | 0.3.0 | 0.4.0 |
|---|---:|---:|
| `churn-1t` | 19% | 20% |
| `churn-4t` | 363% | 168% |
| `resident-4t` | 341% | 20% |
| `large-4t` | 39% | 24% |
| `python` | 4% | 5% |

Measured on an Intel Core i7-10700F, 2.90 GHz. On `churn-4t`, 0.2.0 and 0.3.0
measured 409% and 402% in an earlier series. Between 0.3.0 and 0.4.0,
`churn-4t` went from 363% to 168%, `resident-4t` from 341% to 20% and
`large-4t` from 39% to 24%, while `churn-1t` and `python` changed by about one
point. `churn-4t` stays high because in a small heap the threads publish
almost every change.

## History

The history lives in the `bench-data` branch of the repository, one line per
point. A push to `main` adds one point per scenario, and so does a manual run
on `main`; when pushes come in quick succession, some commits are skipped. The
table lists the last 25 points, newest first.

```{include} _generated/performance.md
:relative-images:
```
