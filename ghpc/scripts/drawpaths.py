#!/usr/bin/env python3
"""Which guest routines draw, and which of them the Rnd seam hooks.

    GHPC_DRAWPATH_CENSUS=2000 ./ghpc/scripts/play.sh --native --log run.log
    python3 ghpc/scripts/drawpaths.py run.log

Reads the last [ghpc/drawpath] table of each kind (PsRnd::FlushPacket and
PsMat::Select callers by return address), names each call site from the
function start addresses in work/output, and sums per function. A function
that flushes or selects and is not in SEAM draws nothing under --native.
"""
import bisect
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Routines whose draws reach the seam, and how.
SEAM = {
    "DrawShowing__6PsMeshQ211RndDrawable8DrawType": "DrawFaces override submits",
    "DrawFaces__6PsMesh": "DrawFaces override submits",
    "DrawShowing__11PsMultiMeshQ211RndDrawable8DrawType": "per instance, billboards skipped",
}


def functions():
    starts, names = [], []
    for fn in os.listdir(os.path.join(ROOT, "work", "output")):
        m = re.match(r"(.+)_0x([0-9a-f]+)\.cpp$", fn)
        if m:
            starts.append(int(m.group(2), 16))
            names.append(m.group(1))
    order = sorted(range(len(starts)), key=starts.__getitem__)
    return [starts[i] for i in order], [names[i] for i in order]


def main():
    tables = {"flush": {}, "select": {}}
    for line in open(sys.argv[1], errors="replace"):
        m = re.search(r"\[ghpc/drawpath\] (\w+) ra=0x([0-9a-f]+) n=(\d+)", line)
        if m:
            # Cumulative tables: a later line for the same ra supersedes.
            tables[m.group(1)][int(m.group(2), 16)] = int(m.group(3))
    starts, names = functions()
    per = collections.defaultdict(lambda: [0, 0])
    for col, kind in enumerate(("flush", "select")):
        for ra, n in tables[kind].items():
            site = ra - 8  # the jal, two instructions before its return
            i = bisect.bisect_right(starts, site) - 1
            name = names[i] if i >= 0 else "?"
            per[(name, starts[i] if i >= 0 else 0)][col] += n
    rows = sorted(per.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))
    print("%10s %10s  %-8s %s" % ("flushes", "selects", "seam", "function"))
    for (name, addr), (fl, se) in rows:
        seam = "yes" if name in SEAM else "NO"
        print("%10d %10d  %-8s %s 0x%x" % (fl, se, seam, name, addr))


if __name__ == "__main__":
    main()
