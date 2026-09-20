#!/usr/bin/env python3
"""Pull every MPG payload out of .vutext and write one .bin per VU load address."""
import os
import struct
import sys

path = sys.argv[1]
out = sys.argv[2]
data = open(path, "rb").read()
os.makedirs(out, exist_ok=True)

i = 0
found = []
while i + 4 <= len(data):
    w = struct.unpack_from("<I", data, i)[0]
    cmd = (w >> 24) & 0xFF
    if cmd == 0x4A:
        num = (w >> 16) & 0xFF
        n = num if num else 256
        addr = w & 0xFFFF
        payload = data[i + 4:i + 4 + n * 8]
        if len(payload) == n * 8:
            vuaddr = addr * 8
            found.append((vuaddr, n, i))
            with open(os.path.join(out, "mpg_%04x.bin" % vuaddr), "wb") as f:
                f.write(payload)
            i += 4 + n * 8
            continue
    i += 4

for vuaddr, n, off in sorted(found):
    print("VU 0x%04x..0x%04x  %3d pairs  fileoff 0x%x" % (vuaddr, vuaddr + n * 8, n, off))
print("total", len(found), "MPG blocks", sum(n * 8 for _, n, _ in found), "bytes")
