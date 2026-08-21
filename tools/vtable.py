#!/usr/bin/env python3
"""Dump a class's g++ 2.x vtable from the GH2 debug ELF.

Virtual calls in this build look like

    lw   $v1, 0x7c($this)   ; vtable pointer
    lh   $a0, 0xb0($v1)     ; this-adjustment delta, a halfword
    lw   $v0, 0xb4($v1)     ; function pointer
    jalr $v0

so a call site only tells you a byte offset into the table. This resolves that
offset to a name.

The GNU v2 layout is a header of two words followed by 8 byte entries:
{ short delta, short index, void *fn }. The pointer for the entry at byte
offset N therefore lives at N + 4.

    python3 tools/vtable.py Submix
    python3 tools/vtable.py RndDrawable 0xb4     # just the slot at that offset
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gnuv2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = "/Users/noahbaxter/Code/personal/games/ghpc/work/GH2_debug.elf"
SYMS = os.path.join(ROOT, "data", "symbols.txt")
SYMLINE = re.compile(r"^\s*\d+:\s+([0-9a-f]{8})\s+(\d+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s+(\S+)")


def load_symbols():
    funcs, objects = {}, []
    with open(SYMS, errors="replace") as fh:
        for line in fh:
            m = SYMLINE.match(line)
            if not m:
                continue
            addr, size, kind, name = int(m.group(1), 16), int(m.group(2)), m.group(3), m.group(4)
            if kind == "FUNC":
                funcs.setdefault(addr, name)
            elif kind == "OBJECT" and name.startswith("_vt$"):
                objects.append((name, addr, size))
    return funcs, objects


def load_segments():
    """Return [(vaddr, filesz, data)] for the ELF's PT_LOAD segments."""
    with open(ELF, "rb") as fh:
        blob = fh.read()
    assert blob[:4] == b"\x7fELF", "not an ELF"
    phoff = struct.unpack_from("<I", blob, 0x1c)[0]
    phentsize = struct.unpack_from("<H", blob, 0x2a)[0]
    phnum = struct.unpack_from("<H", blob, 0x2c)[0]
    segs = []
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", blob, off)
        if p_type == 1 and p_filesz:
            segs.append((p_vaddr, p_filesz, blob[p_offset:p_offset + p_filesz]))
    return segs


def read_word(segs, addr):
    for vaddr, filesz, data in segs:
        if vaddr <= addr < vaddr + filesz - 3:
            return struct.unpack_from("<I", data, addr - vaddr)[0]
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    want = sys.argv[1]
    only = int(sys.argv[2], 0) if len(sys.argv) > 2 else None

    funcs, objects = load_symbols()
    segs = load_segments()

    hits = [o for o in objects if want.lower() in o[0].lower()]
    if not hits:
        print("no vtable symbol matching %r" % want)
        return 1

    for name, addr, size in sorted(hits):
        print("==== %s  at 0x%x, %d bytes" % (name, addr, size))
        # Entries start after the two-word header. Walk on an 8 byte stride and
        # report the offset the call sites actually use, which is the offset of
        # the delta halfword, not of the pointer.
        for off in range(0, size, 8):
            if only is not None and off != only:
                continue
            delta = read_word(segs, addr + off)
            fn = read_word(segs, addr + off + 4)
            if fn is None:
                continue
            if delta == 0 and fn == 0:
                continue
            sym = funcs.get(fn)
            label = gnuv2.pretty(sym) if sym else ("0x%08x" % fn)
            print("  +0x%03x  delta %-6d  %s" % (off, (delta & 0xffff) - (0x10000 if delta & 0x8000 else 0), label))
    return 0


if __name__ == "__main__":
    sys.exit(main())
