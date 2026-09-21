#!/usr/bin/env python3
"""Which draws own a GHPC_VK_MESH_ID shot's pixels.

    ghpc/scripts/vkdrawid.py <shot.ppm> [top]

Each draw painted its index + 1 as R low byte, G high byte, B 0x40, and the
shot's .txt beside it is the frame's draw table. This counts pixels per id
and prints the top draws with their table rows, largest first.
"""
import collections
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = []
    pos = 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    pos += 1
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos:pos + w * h * 3]


def main():
    shot = sys.argv[1]
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    w, h, px = read_ppm(shot)
    counts = collections.Counter()
    other = 0
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        if b == 0x40:
            counts[r | (g << 8)] += 1
        else:
            other += 1

    table = {}
    header = ""
    with open(shot[:-4] + ".txt") as f:
        for line in f:
            if line.startswith("#"):
                header = line[2:].rstrip()
                continue
            cols = line.split()
            table[int(cols[0])] = line.rstrip()

    total = w * h
    print(f"{shot}: {w}x{h}, {len(counts)} draws visible of {len(table)}, "
          f"{other} pixels not a draw id")
    print(f"  pixels  share  {header}")
    for draw_id, n in counts.most_common(top):
        print(f"  {n:6d}  {100.0 * n / total:5.1f}%  {table.get(draw_id, '?')}")


if __name__ == "__main__":
    main()
