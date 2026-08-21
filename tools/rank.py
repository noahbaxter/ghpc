#!/usr/bin/env python3
"""Score every recompiled GH2 function for decompilation difficulty.

Input is the PS2Recomp output tree from ghpc (read only). Each .cpp there is a
machine translation of one original function, one C++ statement per MIPS
instruction, with the disassembly preserved in comments like

    // 0x32cd58: 0x46000b00  add.s  $f12, $f1, $f0

We parse only those comments. They are the ground truth for what the function
does, and they are far cheaper to parse than the emitted C++.

Score is a weighted sum of difficulty signals. Lower is easier.
Usage:
    python3 tools/rank.py [--output-dir DIR] [--out FILE] [--json FILE]
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gnuv2

GHPC = "/Users/noahbaxter/Code/personal/games/ghpc"
DEFAULT_OUTPUT = os.path.join(GHPC, "work", "output")

DISASM = re.compile(
    r"^\s*//\s+0x([0-9a-fA-F]+):\s+0x([0-9a-fA-F]+)\s+(\S+)\s*(.*?)\s*(?:\(Delay Slot\))?$"
)
FILENAME = re.compile(r"^(.*)_0x([0-9a-fA-F]+)\.cpp$")

# COP1 (FPU) mnemonics. Single precision plus the moves and the loads/stores.
COP1_PREFIX = ("add.s", "sub.s", "mul.s", "div.s", "sqrt.s", "abs.s", "mov.s",
               "neg.s", "rsqrt.s", "adda.s", "suba.s", "mula.s", "madd", "msub",
               "max.s", "min.s", "cvt.", "trunc.", "round.", "ceil.", "floor.",
               "c.f.s", "c.un", "c.eq.s", "c.lt.s", "c.le.s")
COP1_EXACT = {"lwc1", "swc1", "ldc1", "sdc1", "mtc1", "mfc1", "ctc1", "cfc1",
              "bc1t", "bc1f", "bc1tl", "bc1fl"}

# COP2 / VU0 macro mode. Every VU0 macro op starts with 'v', plus the transfer
# and branch ops.
COP2_EXACT = {"qmfc2", "qmtc2", "cfc2", "ctc2", "lqc2", "sqc2", "bc2t", "bc2f"}

BRANCH_PREFIX = ("b", "j")
BRANCH_EXACT = {
    "b", "bal", "beq", "bne", "bgez", "bgtz", "blez", "bltz", "bgezal",
    "bltzal", "beql", "bnel", "bgezl", "bgtzl", "blezl", "bltzl", "beqz",
    "bnez", "beqzl", "bnezl", "bgezall", "bltzall",
    "j", "jal", "jr", "jalr",
}

LOAD_STORE = {
    "lb", "lbu", "lh", "lhu", "lw", "lwu", "lwl", "lwr", "ld", "ldl", "ldr",
    "lq", "sb", "sh", "sw", "swl", "swr", "sd", "sdl", "sdr", "sq",
    "lwc1", "swc1", "lqc2", "sqc2", "ll", "sc",
}

# Symbol-name shape hints. Matched against the demangled method name.
SIMPLE_NAME = re.compile(
    r"^(Get|Set|Is|Has|Num|Size|Empty|Clear|Reset|Front|Back|Begin|End|"
    r"Type|Name|Count|Length|Find|Add|Remove|Enabled|Value|Data)", re.I)


def parse_file(path):
    """Return a dict of raw counts for one recompiled function file."""
    with open(path, "r", errors="replace") as fh:
        text = fh.read()

    insns = []
    for line in text.splitlines():
        m = DISASM.match(line)
        if m:
            insns.append((m.group(3).lower(), m.group(4)))

    # The basic-block dispatch table at the top of the function. Its size is a
    # direct measure of how many entry points the block graph has.
    dispatch = len(re.findall(r"^\s*case 0x[0-9a-fA-F]+u?:", text, re.M))
    labels = len(re.findall(r"^label_[0-9a-fA-F]+:", text, re.M))

    calls = 0
    tail_calls = 0
    indirect = 0
    cop1 = 0
    cop2 = 0
    branches = 0
    loads = 0
    stores = 0
    mult_div = 0
    mem_offsets = set()
    callees = set()

    for mnem, ops in insns:
        if mnem in ("jal",):
            calls += 1
            callees.add(ops.strip())
        elif mnem == "jalr":
            calls += 1
            indirect += 1
        elif mnem == "j":
            # A bare j to another function is a tail call, j inside the body is
            # a loop or an if. Treat targets named func_/a symbol as calls.
            if re.search(r"\b(func_[0-9a-fA-F]+|[A-Za-z_]\w*__\w+)", ops):
                tail_calls += 1
                callees.add(ops.strip())
            else:
                branches += 1
        elif mnem == "jr":
            if "$ra" not in ops:
                indirect += 1
        elif mnem in BRANCH_EXACT:
            branches += 1

        if mnem in COP1_EXACT or mnem.startswith(COP1_PREFIX):
            cop1 += 1
        if mnem in COP2_EXACT or (mnem.startswith("v") and "." in mnem) or \
                mnem.startswith(("vadd", "vsub", "vmul", "vdiv", "vmove",
                                 "vitof", "vftoi", "vmr32", "vopmula",
                                 "vclip", "vsqrt", "vrsqrt", "vrget",
                                 "vrnext", "vrinit", "vrxor", "vcallms",
                                 "vwaitq", "vnop", "vmadd", "vmsub", "vmax",
                                 "vmini", "vabs", "viadd", "vilwr", "viswr",
                                 "vlqi", "vsqi", "vcall")):
            cop2 += 1
        if mnem in ("mult", "multu", "div", "divu", "dmult", "dmultu",
                    "ddiv", "ddivu", "mult1", "div1", "madd", "madd1"):
            mult_div += 1

        if mnem in LOAD_STORE:
            if mnem[0] in "ls":
                if mnem.startswith("l"):
                    loads += 1
                else:
                    stores += 1
            m = re.search(r"(-?0x[0-9a-fA-F]+|-?\d+)\(\$(\w+)\)", ops)
            if m:
                mem_offsets.add((m.group(2), m.group(1)))

    return {
        "insns": len(insns),
        "dispatch": dispatch,
        "labels": labels,
        "calls": calls,
        "tail_calls": tail_calls,
        "indirect": indirect,
        "cop1": cop1,
        "cop2": cop2,
        "branches": branches,
        "loads": loads,
        "stores": stores,
        "mult_div": mult_div,
        "distinct_mem": len(mem_offsets),
        "callees": sorted(callees)[:12],
    }


def name_bonus(symbol):
    """Negative score for symbol names whose shape implies a trivial body."""
    d = gnuv2.demangle(symbol)
    bonus = 0.0
    kind = ""
    if d:
        cls, name, args = d
        if name.startswith("~"):
            kind = "dtor"
            bonus -= 6.0
        elif cls and name == cls.split("::")[-1]:
            kind = "ctor"
            bonus -= 5.0
        elif SIMPLE_NAME.match(name):
            kind = "accessor"
            bonus -= 8.0
        if cls:
            bonus -= 2.0  # a known class means a known this-pointer type
        bonus -= min(len(args), 4) * 0.5  # known parameter types are free info
    else:
        bonus += 4.0  # unmangled or unparsed: no type information at all
        kind = "opaque"
    return bonus, kind


def score(c, symbol):
    """Weighted difficulty. Lower is easier."""
    s = 0.0
    s += c["insns"] * 0.35
    s += c["dispatch"] * 2.0
    s += c["labels"] * 1.5
    s += c["branches"] * 2.5
    s += c["calls"] * 6.0
    s += c["tail_calls"] * 4.0
    s += c["indirect"] * 14.0          # indirect call or jump table, hard
    s += c["cop1"] * 1.2               # FPU is readable but adds float typing
    s += c["cop2"] * 9.0               # VU0 macro mode, hardest
    s += c["distinct_mem"] * 1.6       # each distinct offset is a struct field
    s += c["mult_div"] * 1.0
    nb, kind = name_bonus(symbol)
    s += nb
    return s, kind


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output-dir", default=DEFAULT_OUTPUT)
    ap.add_argument("--out", default="data/ranked.tsv")
    ap.add_argument("--json", default="data/ranked.json")
    args = ap.parse_args()

    rows = []
    stubs = []
    for fn in sorted(os.listdir(args.output_dir)):
        if not fn.endswith(".cpp"):
            continue
        m = FILENAME.match(fn)
        if not m:
            continue
        symbol, addr = m.group(1), m.group(2)
        c = parse_file(os.path.join(args.output_dir, fn))
        if c["insns"] == 0:
            # PS2Recomp replaced the body with a runtime syscall shim. Not a
            # real translated function, so it has nothing to decompile.
            stubs.append(fn)
            continue
        sc, kind = score(c, symbol)
        rows.append({
            "file": fn,
            "symbol": symbol,
            "demangled": gnuv2.pretty(symbol),
            "addr": "0x" + addr,
            "score": round(sc, 2),
            "kind": kind,
            **c,
        })

    rows.sort(key=lambda r: (r["score"], r["insns"], r["symbol"]))

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    cols = ["score", "insns", "calls", "indirect", "branches", "dispatch",
            "cop1", "cop2", "distinct_mem", "kind", "addr", "demangled", "file"]
    with open(args.out, "w") as fh:
        fh.write("\t".join(cols) + "\n")
        for r in rows:
            fh.write("\t".join(str(r[c]) for c in cols) + "\n")
    with open(args.json, "w") as fh:
        json.dump(rows, fh, indent=1)

    with open(os.path.join(os.path.dirname(args.out) or ".",
                           "syscall_stubs.txt"), "w") as fh:
        fh.write("\n".join(stubs) + "\n")

    print("syscall shims  %d (excluded)" % len(stubs))
    leaves = sum(1 for r in rows if r["calls"] == 0 and r["tail_calls"] == 0)
    tiny = sum(1 for r in rows if r["insns"] <= 12)
    print("functions      %d" % len(rows))
    print("leaf (no call) %d" % leaves)
    print("<=12 insns     %d" % tiny)
    print("uses COP2      %d" % sum(1 for r in rows if r["cop2"]))
    print("uses COP1      %d" % sum(1 for r in rows if r["cop1"]))
    qs = [rows[int(len(rows) * p)]["score"] for p in (0, .1, .25, .5, .75, .9)]
    print("score p0/p10/p25/p50/p75/p90: " +
          " ".join("%.1f" % q for q in qs) + " max %.1f" % rows[-1]["score"])
    print("wrote %s and %s" % (args.out, args.json))


if __name__ == "__main__":
    main()
