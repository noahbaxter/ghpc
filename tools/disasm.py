#!/usr/bin/env python3
"""Print the MIPS disassembly a recompiled function file carries in comments.

The generated C++ is 20x the size of the disassembly it wraps. When reading a
function this is the view you want.

    python3 tools/disasm.py GetCurrentStreak      # substring match on the symbol
    python3 tools/disasm.py 0x110eb8              # or the address
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gnuv2

OUT = "/Users/noahbaxter/Code/personal/games/ghpc/work/output"
SYMS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "data", "symbols.txt")
DISASM = re.compile(r"^\s*//\s+(0x[0-9a-fA-F]+:\s+0x[0-9a-fA-F]+\s+.*)$")
SYMLINE = re.compile(r"^\s*\d+:\s+([0-9a-f]{8})\s+\d+\s+FUNC\s+\S+\s+\S+\s+\S+\s+(\S+)")
FUNCREF = re.compile(r"\bfunc_([0-9A-Fa-f]+)\b")


def load_symbols():
    """address -> demangled name, for the FUNC entries in the debug ELF."""
    table = {}
    try:
        with open(SYMS, errors="replace") as fh:
            for line in fh:
                m = SYMLINE.match(line)
                if m:
                    table.setdefault(int(m.group(1), 16), gnuv2.pretty(m.group(2)))
    except IOError:
        pass
    return table


def annotate(line, table):
    """Replace func_ADDR placeholders with the real symbol where we have one."""
    def sub(m):
        name = table.get(int(m.group(1), 16))
        return "%s <%s>" % (m.group(0), name) if name else m.group(0)
    return FUNCREF.sub(sub, line)


def main():
    table = load_symbols()
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
                    print("  " + annotate(m.group(1), table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
