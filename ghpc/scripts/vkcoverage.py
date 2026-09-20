#!/usr/bin/env python3
"""Compare where the native path draws against where the software path draws.

The R2 mesh pipeline has no texture yet, so its colours are nothing like the
software rasteriser's: one gameplay frame comes out at about 190 distinct
colours against about 1070. Comparing pixels or colours therefore says nothing
useful, and comparing frame hashes says less, since this project's own notes
record that hashes are not stable across runs.

What IS comparable, and what step 3 should actually be judged on, is
placement: the set of pixels each path covered. Both arms are captured from
the same swapchain by GHPC_VK_SHOT, so they are the same size with the same
letterbox, and a coverage mask from one lines up with the other by
construction.

    ghpc/scripts/vkcoverage.py <software-shot.ppm> <native-shot.ppm>

Reports, over the letterboxed picture only:

    both      covered by both paths
    swOnly    software drew here, native did not. Geometry the seam is not
              serving, or a transform that put it elsewhere.
    nvOnly    native drew here, software did not. Geometry in the wrong place,
              or geometry the GS punched out with the alpha test and this
              pipeline does not.
    iou       intersection over union of the two masks, so one number.

This is a placement check and not a correctness gate. It cannot see a wrong
colour, a wrong texture or a depth ordering error between two surfaces that
both cover the same pixel. It can see the thing the flat-shaded phase is for:
whether the native geometry lands where the PS2 puts it.

Read swOnly as a lower bound. "Covered" is a brightness threshold, so dark
geometry counts as background: a gameplay reference reports about 166k covered
of the 229k-pixel picture rect, and not all of that gap is empty frame. Every
dark pixel the software path drew is one swOnly cannot charge the native path
for.
"""

import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6 with the three ASCII header fields, which is what both writers emit.
    parts = []
    i = 0
    while len(parts) < 4:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i] != 0x0A:
                i += 1
            continue
        j = i
        while j < len(data) and not data[j : j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    magic, w, h, maxval = parts[0], int(parts[1]), int(parts[2]), int(parts[3])
    if magic != b"P6" or maxval != 255:
        raise SystemExit("%s is not an 8-bit P6 PPM" % path)
    return w, h, data[i : i + w * h * 3]


def covered_mask(w, h, px, bg_tolerance=20):
    """Pixels that are not background.

    Background is either black (the software path's clear, and also the
    letterbox on that arm) or the mode-2 clear at (15,15,26). Both are dark, so
    one threshold covers them, and a real surface that happens to be near-black
    is counted as background. That is a known blind spot and it is why this
    reports masks rather than a pass or a fail.
    """
    mask = bytearray(w * h)
    for p in range(w * h):
        r = px[p * 3]
        g = px[p * 3 + 1]
        b = px[p * 3 + 2]
        if r > bg_tolerance or g > bg_tolerance or b > bg_tolerance + 12:
            mask[p] = 1
    return mask


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)

    sw_w, sw_h, sw_px = read_ppm(sys.argv[1])
    nv_w, nv_h, nv_px = read_ppm(sys.argv[2])
    if (sw_w, sw_h) != (nv_w, nv_h):
        raise SystemExit(
            "size mismatch %dx%d vs %dx%d; both shots must come from the same swapchain"
            % (sw_w, sw_h, nv_w, nv_h)
        )

    sw = covered_mask(sw_w, sw_h, sw_px)
    nv = covered_mask(nv_w, nv_h, nv_px)

    both = sw_only = nv_only = neither = 0
    for p in range(sw_w * sw_h):
        if sw[p] and nv[p]:
            both += 1
        elif sw[p]:
            sw_only += 1
        elif nv[p]:
            nv_only += 1
        else:
            neither += 1

    union = both + sw_only + nv_only
    iou = (both / union) if union else 0.0
    print("size      %dx%d" % (sw_w, sw_h))
    print("swCovered %d" % (both + sw_only))
    print("nvCovered %d" % (both + nv_only))
    print("both      %d" % both)
    print("swOnly    %d" % sw_only)
    print("nvOnly    %d" % nv_only)
    print("neither   %d" % neither)
    print("iou       %.4f" % iou)


if __name__ == "__main__":
    main()
