#!/usr/bin/env python3
"""Toggle the boot logo sequence by patching one symbol in the ARK.

    ./ghpc/scripts/bootskip.py --status
    ./ghpc/scripts/bootskip.py --on     # boot straight to main_screen
    ./ghpc/scripts/bootskip.py --off    # restore the stock boot sequence

`ui/gen/init.dtb` contains `{set first_screen bootup_load}`, and the Milo
default (splash.dta `set_defaults`) is already `main_screen`, so flipping that
one symbol skips bootup_load, the intro video and both logo screens.

Measured on 2026-09-09, debug build: main_screen at t=6.3 instead of t=38.8, and
loading_screen at t=41.6 instead of t=86.3. The menu is fully navigable after the
skip, so nothing bootup_load does is needed to reach quickplay.

`bootup_load` and `main_screen` are both 11 bytes, so this is a byte-for-byte
substitution: no size change, no ARK repack. The DTB cipher is a synchronous
stream cipher whose keystream does not depend on the plaintext, so encrypting is
the same operation as decrypting.

**Caveat.** This changes the boot path, so anything bootup_load initialises is
skipped. Do not trust a song-load or audio repro taken under --on without
confirming it under --off.
"""
import argparse, importlib.util, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARK = os.path.join(ROOT, "work", "GEN", "MAIN_0.ARK")
TARGET = "ui/gen/init.dtb"
STOCK, SKIP = b"bootup_load", b"main_screen"

spec = importlib.util.spec_from_file_location(
    "dtb", os.path.join(os.path.dirname(os.path.abspath(__file__)), "dtb.py"))
dtb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dtb)


def locate():
    _, files = dtb.read_header()
    if TARGET not in files:
        sys.exit("%s not found in MAIN.HDR" % TARGET)
    off, sz, _ = files[TARGET]
    return off, sz


def load(off, sz):
    with open(ARK, "rb") as f:
        f.seek(off)
        raw = f.read(sz)
    if len(raw) != sz:
        sys.exit("short read at %d" % off)
    return raw, dtb.decrypt(raw)


def state(plain):
    if plain.count(SKIP) and not plain.count(STOCK):
        return "on"
    if plain.count(STOCK):
        return "off"
    return "unknown"


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--on", action="store_true")
    g.add_argument("--off", action="store_true")
    g.add_argument("--status", action="store_true")
    a = ap.parse_args()

    off, sz = locate()
    raw, plain = load(off, sz)
    cur = state(plain)

    if a.status:
        print("boot skip: %s  (%s at ARK offset %d, %d bytes)" % (cur, TARGET, off, sz))
        return

    want = "on" if a.on else "off"
    src, dst = (STOCK, SKIP) if a.on else (SKIP, STOCK)
    if cur == want:
        print("boot skip already %s, nothing to do" % want)
        return
    n = plain.count(src)
    if n != 1:
        sys.exit("expected exactly one %r in %s, found %d" % (src, TARGET, n))

    newplain = plain.replace(src, dst)
    if len(newplain) != len(plain):
        sys.exit("length changed, refusing to write")
    newraw = raw[:4] + dtb.decrypt(raw[:4] + newplain)
    if len(newraw) != sz or dtb.decrypt(newraw) != newplain:
        sys.exit("round trip failed, refusing to write")

    with open(ARK, "r+b") as f:
        f.seek(off)
        f.write(newraw)
        f.flush()
    _, check = load(off, sz)
    if state(check) != want:
        sys.exit("write did not take effect")
    print("boot skip %s  (%d bytes changed)" % (want, sum(x != y for x, y in zip(raw, newraw))))


if __name__ == "__main__":
    main()
