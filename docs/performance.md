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

`malloc_churn` keeps 64 blocks per thread and replaces one at random on each
iteration. In a small heap, the threads publish almost every change, which is
what keeps `churn-4t` slow; when the process holds more memory, as in
`resident-4t`, they publish in batches. Points measured before scenarios
existed belong to `churn-4t`.

The figure is the relative overhead measured within one CI job: the median
time of ten runs with heapline, divided by the median of ten runs without it,
interleaved, minus one. Absolute times are not comparable between runs,
because GitHub's shared runners change machines. The ratio mostly is, but
expect noise of a few tens of percentage points from one point to the next.

## Release comparison

Measured on one machine, under WSL2, with
`sh bench/run_overhead.sh build/bench/malloc_churn <lib> 10`. Each figure is
the median overhead of three rounds of ten runs each, alternating the two
libraries, with the same `malloc_churn` binary for both. Single rounds ranged
from 377% to 417%. The 0.3.0 figure was measured at commit 6a12b65, whose
library code is the released one.

| Version | Overhead on malloc_churn | Machine |
|---|---:|---|
| 0.2.0 | 409% | Intel Core i7-10700F, 2.90 GHz |
| 0.3.0 | 402% | Intel Core i7-10700F, 2.90 GHz |

No difference beyond the noise of the measurement was detected between the
two versions.

## History

The history lives in the `bench-data` branch of the repository, one line per
point. A push to `main` adds one point per scenario, and so does a manual run
on `main`; when pushes come in quick succession, some commits are skipped. The
table lists the last 25 points, newest first.

```{include} _generated/performance.md
:relative-images:
```
