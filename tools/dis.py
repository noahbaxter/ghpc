#!/usr/bin/env python3
"""Print the MIPS disassembly a recompiled function file carries in comments.

The generated C++ is 20x the size of the disassembly it wraps. When reading a
function this is the view you want.

    python3 tools/dis.py GetCurrentStreak      # substring match on the symbol
    python3 tools/dis.py 0x110eb8              # or the address
"""

import os
import re
import sys

OUT = "/Users/noahbaxter/Code/personal/games/ghpc/work/output"
DISASM = re.compile(r"^\s*//\s+(0x[0-9a-fA-F]+:\s+0x[0-9a-fA-F]+\s+.*)$")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    pats = sys.argv[1:]
    names = sorted(os.listdir(OUT))
    hits = [n for n in names if all(p.lower() in n.lower() for p in pats)]
    if not hits:
        print("no match")
        return 1
    if len(hits) > 24:
        print("%d matches, narrow it:" % len(hits))
        for h in hits[:40]:
            print("  " + h)
        return 1
    for h in hits:
        print("==== %s" % h)
        with open(os.path.join(OUT, h), errors="replace") as fh:
            for line in fh:
                m = DISASM.match(line)
                if m:
                    print("  " + m.group(1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
