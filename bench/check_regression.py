"""Warn when the last overhead point of the history is well above the others.

Shared CI runners are noisy, so this only prints GitHub Actions warnings and
always exits with 0: a missing history, a corrupt line or a missing argument
never fails the job.
"""

import json
import math
import statistics
import sys


def read_overheads(path):
    """Return the overhead of every valid line of the history and the number
    of lines skipped because they are not JSON or have no numeric overhead."""
    overheads = []
    skipped = 0
    with open(path, encoding="utf-8", errors="replace") as history:
        for line in history:
            if not line.strip():
                continue
            try:
                value = json.loads(line)["overhead"]
            except (ValueError, KeyError, TypeError, RecursionError):
                skipped += 1
                continue
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
            ):
                skipped += 1
                continue
            overheads.append(value)
    return overheads, skipped


def main(argv):
    if len(argv) != 2:
        print("::warning::usage: check_regression.py HISTORY.jsonl")
        return
    try:
        overheads, skipped = read_overheads(argv[1])
    except FileNotFoundError:
        return
    except OSError as error:
        print(f"::warning::cannot read the overhead history: {error}")
        return
    if skipped:
        print(
            f"::warning::{skipped} line(s) of the overhead history "
            "were skipped (not JSON or no numeric overhead)"
        )
    if len(overheads) < 2:
        return
    last = overheads[-1]
    previous = overheads[-11:-1]
    reference = statistics.median(previous)
    limit = reference + max(0.25 * abs(reference), 0.02)
    if last > limit:
        print(
            f"::warning::heapline overhead {last:.1%} is above {limit:.1%} "
            f"(median of the last {len(previous)} points: {reference:.1%})"
        )


if __name__ == "__main__":
    main(sys.argv)
