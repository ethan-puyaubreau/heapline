"""Warn when the last overhead point of the history is well above the others.

Shared CI runners are noisy, so this only prints a GitHub Actions warning and
always exits with 0.
"""

import json
import statistics
import sys


def main(path):
    with open(path, encoding="utf-8") as history:
        points = [json.loads(line) for line in history if line.strip()]
    if len(points) < 2:
        return
    last = points[-1]["overhead"]
    previous = [p["overhead"] for p in points[-11:-1]]
    reference = statistics.median(previous)
    limit = reference + max(0.25 * abs(reference), 0.02)
    if last > limit:
        print(
            f"::warning::heapline overhead {last:.1%} is above {limit:.1%} "
            f"(median of the last {len(previous)} points: {reference:.1%})"
        )


if __name__ == "__main__":
    main(sys.argv[1])
