"""Warn when the last overhead point of a scenario is well above the others.

Each scenario of the history is checked against its own previous points.
Shared CI runners are noisy, so this only prints GitHub Actions warnings and
always exits with 0: a missing history, a corrupt line or a missing argument
never fails the job.
"""

import json
import math
import statistics
import sys

DEFAULT_SCENARIO = "churn-4t"


def read_overheads(path):
    """Return the overheads of the valid lines of the history, grouped by
    scenario in file order, and the number of lines skipped because they are
    not JSON, have no numeric overhead or no valid scenario. Lines without a
    scenario come from before scenarios existed and count as churn-4t."""
    series = {}
    skipped = 0
    with open(path, encoding="utf-8", errors="replace") as history:
        for line in history:
            if not line.strip():
                continue
            try:
                point = json.loads(line)
                value = point["overhead"]
                scenario = point.get("scenario", DEFAULT_SCENARIO)
            except (ValueError, KeyError, TypeError, AttributeError, RecursionError):
                skipped += 1
                continue
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                or not isinstance(scenario, str)
                or not scenario
            ):
                skipped += 1
                continue
            series.setdefault(scenario, []).append(value)
    return series, skipped


def main(argv):
    if len(argv) != 2:
        print("::warning::usage: check_regression.py HISTORY.jsonl")
        return
    try:
        series, skipped = read_overheads(argv[1])
    except FileNotFoundError:
        return
    except OSError as error:
        print(f"::warning::cannot read the overhead history: {error}")
        return
    if skipped:
        print(
            f"::warning::{skipped} line(s) of the overhead history "
            "were skipped (not JSON, no numeric overhead or no scenario)"
        )
    for scenario, overheads in series.items():
        if len(overheads) < 2:
            continue
        last = overheads[-1]
        previous = overheads[-11:-1]
        reference = statistics.median(previous)
        limit = reference + max(0.25 * abs(reference), 0.02)
        if last > limit:
            print(
                f"::warning::heapline overhead on {scenario}, {last:.1%}, is "
                f"above {limit:.1%} (median of the last {len(previous)} "
                f"points: {reference:.1%})"
            )


if __name__ == "__main__":
    main(sys.argv)
