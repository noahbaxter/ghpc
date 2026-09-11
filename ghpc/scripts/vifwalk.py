#!/usr/bin/env python3
"""Walk a VIF1 chunk dumped by GHPC_VIF1_DUMPCHUNK offline.

    python3 ghpc/scripts/vifwalk.py work/vif1chunk-2-mscal8467.bin [--legacy]

Reads the .bin and its sidecar .txt (entry registers, DMA tags, chain pieces,
the runtime's own command trace). Re-parses the bytes with PCSX2's sizing
rules (Vif_Codes.cpp, vifUnpackSetup) and prints every command with its
consumed size, marking where the runtime's trace disagrees and where each
DMA-chain piece begins, so a wrong step can be pinned to either the parser or
the tag walk. --legacy reads STCYCL WL=0 as 1, the old runtime decode.
"""
import argparse
import os
import struct
import sys

NAMES = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE", 0x04: "ITOP",
         0x05: "STMOD", 0x06: "MSKPATH3", 0x07: "MARK", 0x10: "FLUSHE",
         0x11: "FLUSH", 0x13: "FLUSHA", 0x14: "MSCAL", 0x15: "MSCALF",
         0x17: "MSCNT", 0x20: "STMASK", 0x30: "STROW", 0x31: "STCOL",
         0x4A: "MPG", 0x50: "DIRECT", 0x51: "DIRECTHL"}
# bytes per vector, PCSX2 nVifT indexed by (vn<<2)|vl
GSIZE = [4, 2, 1, 0, 8, 4, 2, 0, 12, 6, 3, 0, 16, 8, 4, 2]


def parse_sidecar(path):
    entry, tags, pieces, trace = {}, [], [], []
    with open(path) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "entry" or line.startswith("reason="):
                for kv in w:
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        entry[k] = v
            elif w[0] == "tag":
                tags.append({kv.split("=")[0]: kv.split("=")[1] for kv in w[1:] if "=" in kv})
            elif w[0] == "piece":
                pieces.append({kv.split("=")[0]: int(kv.split("=")[1], 0) for kv in w[1:]})
            elif w[0] == "cmd":
                trace.append((int(w[1].split("=")[1]), int(w[2], 16)))
    return entry, tags, pieces, trace


def walk(data, cycle, legacy, pending_img):
    """Yield (pos, cmd, name, consumed, note) in PCSX2 sizing."""
    pos, n = 0, len(data)
    cl, wl = cycle & 0xFF, (cycle >> 8) & 0xFF
    while pos + 4 <= n:
        if pending_img:
            qw = min(pending_img, (n - pos) // 16)
            yield pos, None, "IMAGE-SPILL", qw * 16, "runtime-only: eats %d qw of stream" % qw
            pos += qw * 16
            pending_img -= qw
            continue
        cmd = struct.unpack_from("<I", data, pos)[0]
        op = (cmd >> 24) & 0x7F
        num = (cmd >> 16) & 0xFF
        imm = cmd & 0xFFFF
        size, note = 4, ""
        if op == 0x01:
            cl, wl = imm & 0xFF, (imm >> 8) & 0xFF
            note = "cl=%d wl=%d" % (cl, wl)
        elif op == 0x20:
            size = 8
        elif op in (0x30, 0x31):
            size = 20
        elif op == 0x4A:
            size = 4 + (num or 256) * 8
        elif op in (0x50, 0x51):
            size = 4 + (imm or 65536) * 16
        elif op & 0x60 == 0x60:
            vn, vl = (op >> 2) & 3, op & 3
            g = GSIZE[(vn << 2) | vl]
            vnum = num or 256
            w = wl if wl else (1 if legacy else 256)
            c = cl if (cl or not legacy) else 1
            if w <= c:
                words = (vnum * g + 3) // 4
                mode = "skip"
            else:
                nn = c * (vnum // w) + min(vnum % w, c)
                words = (nn * g + 3) >> 2
                mode = "fill"
            size = 4 + words * 4
            note = "V%d-%s %s addr=%d%s%s" % (
                vn + 1, {0: "32", 1: "16", 2: "8", 3: "5"}[vl], mode, imm & 0x3FF,
                "+TOPS" if imm & 0x8000 else "", " mask" if op & 0x10 else "")
        elif op not in NAMES:
            note = "INVALID"
        name = NAMES.get(op, "UNPACK" if op & 0x60 == 0x60 else "??")
        yield pos, cmd, name, size, note
        pos += size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--legacy", action="store_true")
    ap.add_argument("--all", action="store_true", help="print NOPs too")
    a = ap.parse_args()
    data = open(a.bin, "rb").read()
    entry, tags, pieces, trace = parse_sidecar(os.path.splitext(a.bin)[0] + ".txt")
    residual = int(entry.get("residual", "0"))
    cycle = int(entry.get("cycle", "0"), 16)
    pending = int(entry.get("pendingImgQw", "0"))
    print("size=%d trigger=%s pos=%s residual=%d entry cycle=0x%04x base=%s ofst=%s"
          % (len(data), entry.get("reason"), entry.get("pos"), residual, cycle,
             entry.get("base"), entry.get("ofst")))
    print("tags:")
    for t in tags:
        print("  chunk+%-6d tag@0x%s id=%s qwc=%-5s addr=%s hi=%s"
              % (int(t["chainOff"]) + residual, t.get("tag@0x", "?"), t["id"], t["qwc"], t["addr"], t["hi"]))
    piece_starts = {p["chainOff"] + residual: p for p in pieces}
    tag_starts = {int(t["chainOff"]) + residual: t for t in tags}
    runtime = dict(trace)
    runtime_pos = sorted(runtime)
    trigger = int(entry.get("pos", "-1"))
    print("walk (PCSX2 sizing%s):" % (", legacy STCYCL" if a.legacy else ""))
    diverged = False
    for pos, cmd, name, size, note in walk(data, cycle, a.legacy, pending):
        marks = []
        if pos in tag_starts:
            t = tag_starts[pos]
            marks.append("<-- tag id=%s qwc=%s addr=%s" % (t["id"], t["qwc"], t["addr"]))
        if pos in piece_starts:
            marks.append("<-- piece ee=0x%x len=%d" % (piece_starts[pos]["ee"], piece_starts[pos]["len"]))
        if cmd is not None and pos not in runtime and not diverged and runtime_pos and pos < runtime_pos[-1]:
            marks.append("<== RUNTIME DID NOT PARSE A COMMAND HERE")
            diverged = True
        if pos == trigger:
            marks.append("<== TRIGGER")
        if pos > trigger >= 0 and pos - size < trigger and pos != trigger:
            pass
        quiet = (name == "NOP" and cmd == 0 and not marks and not a.all)
        if quiet:
            continue
        print("  +%-6d %s %-9s size=%-6d %s %s"
              % (pos, "0x%08x" % cmd if cmd is not None else "----------", name, size, note, " ".join(marks)))
        if pos >= trigger >= 0 and note == "INVALID":
            break


if __name__ == "__main__":
    main()
