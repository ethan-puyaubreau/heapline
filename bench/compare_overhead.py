"""Print a Markdown table comparing the overhead of two builds of heapline.

Both files hold the points that run_overhead.sh printed for the same
scenarios, measured in the same job: BASE for main, HEAD for the branch.
Only the last point of each scenario counts. A scenario missing on one side
is listed with a dash. Invalid lines are skipped, as in check_regression.py.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from check_regression import read_overheads  # noqa: E402

NOISE_NOTE = (
    "Shared runners are noisy: on `churn-4t`, a change of a few tens of "
    "points between two runs of the same code is common. Run the comparison "
    "again before trusting a small difference."
)


def last_points(path):
    """Return the last overhead of each scenario of the file, in file order."""
    try:
        series, _ = read_overheads(path)
    except OSError as error:
        print(f"cannot read {path}: {error}", file=sys.stderr)
        return {}
    return {scenario: values[-1] for scenario, values in series.items()}


def percent(value):
    return "-" if value is None else f"{value:.1%}"


def table(base, head):
    """Return the Markdown lines of the comparison, scenarios of base first."""
    lines = [
        "| Scenario | main | branch | Change |",
        "|---|---:|---:|---:|",
    ]
    for scenario in list(base) + [s for s in head if s not in base]:
        before = base.get(scenario)
        after = head.get(scenario)
        change = "-"
        if before is not None and after is not None:
            change = f"{(after - before) * 100:+.1f} pts"
        lines.append(
            f"| `{scenario}` | {percent(before)} | {percent(after)} | {change} |"
        )
    return lines


def main(argv):
    if len(argv) != 3:
        print("usage: compare_overhead.py BASE.jsonl HEAD.jsonl", file=sys.stderr)
        return 2
    base = last_points(argv[1])
    head = last_points(argv[2])
    print("## Overhead, main against this branch\n")
    print("\n".join(table(base, head)))
    print(f"\n{NOISE_NOTE}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
