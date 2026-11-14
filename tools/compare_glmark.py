#!/usr/bin/env python3
"""Compare two glmark2 result folders scene by scene.

Usage: compare_glmark.py <dirA> <dirB> [file ...]   (default files: onscreen.txt offscreen.txt)
Prints FPS per scene, B/A ratio, and the geometric mean of the ratios.
"""
import math
import re
import sys

LINE = re.compile(r"^\[(\w+)\] (.*?): FPS: (\d+)")


def scenes(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = LINE.match(ln.strip())
            if m:
                opts = m.group(2).replace("duration=5.0:", "").replace("duration=5.0", "")
                out.append((f"{m.group(1)} {opts}".strip(), int(m.group(3))))
    return out


def main():
    a, b = sys.argv[1], sys.argv[2]
    files = sys.argv[3:] or ["onscreen.txt", "offscreen.txt"]
    for fn in files:
        sa, sb = scenes(f"{a}/{fn}"), scenes(f"{b}/{fn}")
        print(f"\n== {fn}: {a} vs {b}")
        print(f"{'scene':58} {'A':>6} {'B':>6} {'B/A':>6}")
        logs = []
        for (name, fa), (_, fb) in zip(sa, sb):
            r = fb / fa if fa else float("nan")
            if fa and fb:
                logs.append(math.log(r))
            print(f"{name[:58]:58} {fa:6d} {fb:6d} {r:6.2f}")
        if logs:
            print(f"{'geometric mean B/A':58} {'':6} {'':6} {math.exp(sum(logs) / len(logs)):6.2f}")


if __name__ == "__main__":
    main()
