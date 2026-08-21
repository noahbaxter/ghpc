#!/usr/bin/env python3
"""Read bytes out of the GH2 debug ELF's loaded segments.

Assert messages, script symbol names and the file/line strings behind
Debug::Fail all live in read-only data, addressed by a lui/addiu pair in the
disassembly. This turns such an address back into what is stored there.

    python3 tools/rodata.py 0x474760          # C string
    python3 tools/rodata.py 0x445240 words 24 # first 24 words
"""

import struct
import sys

ELF = "/Users/noahbaxter/Code/personal/games/ghpc/work/GH2_debug.elf"


def load_segments():
    with open(ELF, "rb") as fh:
        blob = fh.read()
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


def slice_at(segs, addr, length):
    for vaddr, filesz, data in segs:
        if vaddr <= addr < vaddr + filesz:
            start = addr - vaddr
            return data[start:start + length]
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    addr = int(sys.argv[1], 0)
    mode = sys.argv[2] if len(sys.argv) > 2 else "str"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    segs = load_segments()

    if mode == "words":
        raw = slice_at(segs, addr, count * 4)
        if raw is None:
            print("0x%x is not in a loaded segment" % addr)
            return 1
        for i in range(0, len(raw) - 3, 4):
            print("  0x%08x: 0x%08x  %d" % (addr + i,
                                            struct.unpack_from("<I", raw, i)[0],
                                            struct.unpack_from("<i", raw, i)[0]))
        return 0

    raw = slice_at(segs, addr, 512)
    if raw is None:
        print("0x%x is not in a loaded segment" % addr)
        return 1
    end = raw.find(b"\0")
    print(repr(raw[:end if end >= 0 else 128].decode("latin-1")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
