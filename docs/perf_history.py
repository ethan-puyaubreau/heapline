"""Render the overhead history of heapline for the documentation.

Reads the history.jsonl of the bench-data branch and writes
docs/_generated/performance.md, plus one performance-<scenario>.svg per
scenario when there is data.
"""

import json
import math
import pathlib
import re
import sys

OUT = pathlib.Path(__file__).parent / "_generated"
WIDTH, HEIGHT = 640, 260
LEFT, RIGHT, TOP, BOTTOM = 56, 16, 16, 40
NUMBERS = ("overhead", "allocs_per_s")
STRINGS = ("commit", "date", "cpu")
DEFAULT_SCENARIO = "churn-4t"
ORDER = ("churn-1t", "churn-4t", "resident-4t", "large-4t", "python")
SCENARIO = re.compile(r"[a-z0-9-]{1,32}")


def clean(point):
    """Return the parsed line with float numbers, or None when it lacks a
    field the page needs or holds a number the page cannot draw."""
    if not isinstance(point, dict):
        return None
    if not all(isinstance(point.get(key), str) for key in STRINGS):
        return None
    out = dict(point)
    for key in NUMBERS:
        value = point.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return None
        try:
            out[key] = float(value)
        except (OverflowError, ValueError, TypeError):
            return None
    if not (
        math.isfinite(out["allocs_per_s"]) and math.isfinite(100 * out["overhead"])
    ):
        return None
    scenario = point.get("scenario", DEFAULT_SCENARIO)
    if not isinstance(scenario, str) or not SCENARIO.fullmatch(scenario):
        return None
    out["scenario"] = scenario
    return out


def load(path):
    """Return the valid points of the history and the number of lines skipped
    because they are not JSON or miss a field."""
    history = pathlib.Path(path) if path else None
    if history is None or not history.exists():
        return [], 0
    points, skipped = [], 0
    text = history.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if not line.strip():
            continue
        try:
            point = json.loads(line)
        except (ValueError, RecursionError):
            skipped += 1
            continue
        point = clean(point)
        if point is None:
            skipped += 1
        else:
            points.append(point)
    return points, skipped


def svg(points):
    values = [100 * p["overhead"] for p in points]
    low, high = min(0.0, min(values)), max(values)
    if high - low < 1e-9:
        high = low + 1

    def x(i):
        if len(values) == 1:
            return (LEFT + WIDTH - RIGHT) / 2
        return LEFT + (WIDTH - LEFT - RIGHT) * i / (len(values) - 1)

    def y(v):
        return TOP + (HEIGHT - TOP - BOTTOM) * (1 - (v - low) / (high - low))

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
        'font-family="Charter, Cambria, serif" font-size="12">',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="#fff"/>',
    ]
    for k in range(5):
        v = low + (high - low) * k / 4
        parts.append(
            f'<line x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{y(v):.1f}" '
            f'y2="{y(v):.1f}" stroke="#ccc" stroke-dasharray="2 3"/>'
        )
        parts.append(
            f'<text x="{LEFT - 6}" y="{y(v) + 4:.1f}" text-anchor="end">'
            f"{v:.1f}</text>"
        )
    line = " ".join(f"{x(i):.1f},{y(v):.1f}" for i, v in enumerate(values))
    parts.append(
        f'<polyline points="{line}" fill="none" stroke="#000" stroke-width="1.5"/>'
    )
    for i, v in enumerate(values):
        parts.append(
            f'<circle cx="{x(i):.1f}" cy="{y(v):.1f}" r="2.5" fill="#000"/>'
        )
    parts.append(
        f'<line x1="{LEFT}" x2="{LEFT}" y1="{TOP}" y2="{HEIGHT - BOTTOM}" stroke="#000"/>'
    )
    parts.append(
        f'<line x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{HEIGHT - BOTTOM}" '
        f'y2="{HEIGHT - BOTTOM}" stroke="#000"/>'
    )
    parts.append(
        f'<text x="{(LEFT + WIDTH - RIGHT) / 2:.0f}" y="{HEIGHT - 10}" '
        'text-anchor="middle">Measurements of this scenario, oldest to newest</text>'
    )
    parts.append(
        f'<text transform="translate(14 {(TOP + HEIGHT - BOTTOM) / 2:.0f}) '
        'rotate(-90)" text-anchor="middle">Overhead (%)</text>'
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def cell(text):
    """Make a string safe for a Markdown table cell."""
    text = text.replace("|", "/").replace("`", "")
    return text.replace("\r", "").replace("\n", "")


def table(points):
    rows = [
        "| Commit | Date | Scenario | Overhead | Allocations/s with heapline | Runner CPU |",
        "|---|---|---|---:|---:|---|",
    ]
    for p in reversed(points[-25:]):
        rows.append(
            f"| `{cell(p['commit'])}` | {cell(p['date'])[:10]} "
            f"| {p['scenario']} | {100 * p['overhead']:.1f}% "
            f"| {p['allocs_per_s']:.3g} | {cell(p['cpu'])} |"
        )
    return "\n".join(rows) + "\n"


def main(path):
    points, skipped = load(path)
    if skipped:
        print(
            f"::warning::{skipped} line(s) of the overhead history were "
            "skipped (not JSON or missing a field)"
        )
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob("performance*.svg"):
        old.unlink()
    if not points:
        (OUT / "performance.md").write_text("No measurement yet.\n", encoding="utf-8")
        return
    series = {}
    for p in points:
        series.setdefault(p["scenario"], []).append(p)
    names = [n for n in ORDER if n in series]
    names += sorted(n for n in series if n not in ORDER)
    parts = []
    for name in names:
        (OUT / f"performance-{name}.svg").write_text(svg(series[name]), encoding="utf-8")
        parts.append(
            f"### {name}\n\n"
            f"![Overhead of heapline on {name}, per measurement]"
            f"(performance-{name}.svg)\n"
        )
    parts.append("### Last points\n\n" + table(points))
    (OUT / "performance.md").write_text("\n".join(parts), encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "")
