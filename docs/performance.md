# Performance history

heapline must stay cheap enough to leave on in production runs. Pushes to
`main` measure its overhead on `bench/malloc_churn`, a benchmark that does
little but allocate and release memory from four threads. That is the worst
case for an allocation counter, and so for heapline: on this benchmark a
program runs about five times slower, which is an overhead of several hundred
percent. A real program does more than allocate, so its overhead is lower than
that. Real programs were not measured here.

The figure is the relative overhead measured within one CI job: the median
time of ten runs with heapline, divided by the median of ten runs without it,
interleaved, minus one. Absolute times are not comparable between runs,
because GitHub's shared runners change machines. The ratio mostly is, but on
this benchmark expect noise of a few tens of percentage points from one
point to the next.

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
point. A push to `main` adds a point, and so does a manual run on `main`;
when pushes come in quick succession, some commits are skipped. The table
lists the last 20 points, newest first.

```{include} _generated/performance.md
:relative-images:
```
