"""Allocation-heavy Python workload for the heapline overhead benchmark.

Builds and drops containers whose sizes cross the 512-byte limit of
pymalloc, so the run mixes pymalloc arenas and calls to malloc. Deterministic;
the default takes about one second on a desktop CPU.
Usage: python_churn.py [ROUNDS]
"""

import sys


def main(rounds):
    keep = []
    for i in range(rounds):
        data = [bytes(16 + (i * 7919 + j * 104729) % 2048) for j in range(64)]
        table = {j: str(j) * (1 + j % 40) for j in range(64)}
        keep.append((data, table))
        if len(keep) > 32:
            keep.pop(0)
    return len(keep)


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 40000)
