#!/usr/bin/env python3
"""Decrypt and dump a PS2 Harmonix DTB, straight out of the ARK.

    ./scripts/dtb.py ui/gen/ui.dtb           # look up in MAIN.HDR, decrypt, dump
    ./scripts/dtb.py --list                  # list every file in the ARK
    ./scripts/dtb.py --raw <offset> <size>   # decrypt an explicit ARK range

Cipher is the PS2 variant used by ArkTool and Mackiloha: a 256 entry LCG table
keyed by the first four bytes, walked with two rolling indices.
"""
import argparse, struct, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR = os.path.join(ROOT, "work", "GEN", "MAIN.HDR")
ARK = os.path.join(ROOT, "work", "GEN", "MAIN_0.ARK")
M = 0xFFFFFFFF


def decrypt(buf):
    key = struct.unpack_from("<i", buf, 0)[0]
    val1 = key & M
    table = [0] * 0x100
    for i in range(0x100):
        val2 = (val1 * 0x41C64E6D + 0x3039) & M
        val1 = (val2 * 0x41C64E6D + 0x3039) & M
        table[i] = (val1 & 0x7FFF0000) | (val2 >> 16)
    i1, i2 = 0x00, 0x67
    out = bytearray()
    for b in buf[4:]:
        table[i1] ^= table[i2]
        out.append((b ^ table[i1]) & 0xFF)
        i1 = 0 if (i1 + 1) >= 0xF9 else i1 + 1
        i2 = 0 if (i2 + 1) >= 0xF9 else i2 + 1
    return bytes(out)


def read_header():
    d = open(HDR, "rb").read()
    ver, narks, nsizes = struct.unpack_from("<III", d, 0)
    p = 12 + 4 * nsizes
    strsz = struct.unpack_from("<I", d, p)[0]; p += 4
    tbl = d[p:p + strsz]; p += strsz
    noff = struct.unpack_from("<I", d, p)[0]; p += 4
    offs = list(struct.unpack_from("<%dI" % noff, d, p)); p += 4 * noff
    nfiles = struct.unpack_from("<I", d, p)[0]; p += 4
    def name(i):
        o = offs[i]
        return tbl[o:tbl.index(b"\x00", o)].decode("latin1")
    files = {}
    for i in range(nfiles):
        off, fi, di, sz, usz = struct.unpack_from("<IIIII", d, p + 20 * i)
        try:
            files["%s/%s" % (name(di), name(fi))] = (off, sz, usz)
        except Exception:
            pass
    return ver, files


TYPES = {0: "int", 1: "float", 2: "var", 3: "func", 4: "object", 5: "symbol",
         6: "unhandled", 7: "ifdef", 8: "else", 9: "endif",
         16: "array", 17: "command", 18: "string", 19: "property", 20: "glob"}


def dump(d, pos=0, depth=0, limit=400):
    """Walk the node tree, printing structure only (types and counts)."""
    if depth == 0:
        flag = d[0]
        cnt = struct.unpack_from("<H", d, 1)[0]
        idv = struct.unpack_from("<I", d, 3)[0]
        print("flag=%d rootCount=%d id=%d size=%d" % (flag, cnt, idv, len(d)))
        pos = 7
    return pos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--raw", nargs=2, type=int, metavar=("OFFSET", "SIZE"))
    ap.add_argument("-o", "--out")
    a = ap.parse_args()

    if a.raw:
        with open(ARK, "rb") as f:
            f.seek(a.raw[0]); enc = f.read(a.raw[1])
    else:
        ver, files = read_header()
        if a.list:
            for k in sorted(files):
                off, sz, usz = files[k]
                print("%10d %8d  %s" % (off, sz, k))
            return
        if not a.path:
            ap.error("need a path, --list, or --raw")
        if a.path not in files:
            hits = [k for k in files if a.path in k]
            print("not found; %d similar:" % len(hits), file=sys.stderr)
            for h in hits[:20]:
                print("  ", h, file=sys.stderr)
            sys.exit(1)
        off, sz, usz = files[a.path]
        print("# %s  offset=%d size=%d" % (a.path, off, sz))
        with open(ARK, "rb") as f:
            f.seek(off); enc = f.read(sz)

    dec = decrypt(enc)
    if a.out:
        open(a.out, "wb").write(dec)
        print("# wrote %d bytes to %s" % (len(dec), a.out))
    dump(dec)
    zeros = dec.count(0)
    print("# zeros=%d/%d (%.1f%%) distinct=%d" % (zeros, len(dec), 100.0 * zeros / len(dec), len(set(dec))))


if __name__ == "__main__":
    main()
