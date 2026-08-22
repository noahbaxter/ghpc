#!/usr/bin/env python3
"""Count decompiled functions and check them against the ranking.

A function is counted when a definition line in src/ carries a trailing
// 0xADDRESS comment naming the GH2 address it came from. Prints the tally and
where those functions sit in the difficulty ranking.
"""

import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF = re.compile(r"^[A-Za-z_].*[){].*//\s*(0x[0-9a-f]{5,8})")


def main():
    done = {}
    for base, _, names in os.walk(os.path.join(ROOT, "src")):
        for n in sorted(names):
            if not n.endswith(".cpp"):
                continue
            path = os.path.join(base, n)
            with open(path) as fh:
                for line in fh:
                    m = DEF.match(line)
                    if m:
                        done[m.group(1)] = os.path.relpath(path, ROOT)

    with open(os.path.join(ROOT, "data", "ranked.json")) as fh:
        rows = json.load(fh)
    rank = {r["addr"]: i for i, r in enumerate(rows)}

    print("decompiled functions: %d" % len(done))
    print("of %d ranked" % len(rows))
    hit = [rank[a] for a in done if a in rank]
    miss = sorted(a for a in done if a not in rank)
    if hit:
        hit.sort()
        print("rank positions: min %d, median %d, max %d"
              % (hit[0], hit[len(hit) // 2], hit[-1]))
        print("inside the easiest 1000: %d" % sum(1 for h in hit if h < 1000))
    if miss:
        print("addresses not found in the ranking (%d): %s"
              % (len(miss), " ".join(miss[:12])))
    by_file = {}
    for a, f in done.items():
        by_file[f] = by_file.get(f, 0) + 1
    for f in sorted(by_file):
        print("  %3d  %s" % (by_file[f], f))


if __name__ == "__main__":
    main()
