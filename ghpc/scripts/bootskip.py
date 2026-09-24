#!/usr/bin/env python3
"""Choose the boot sequence by patching symbols in the ARK.

    ./ghpc/scripts/bootskip.py --status
    ./ghpc/scripts/bootskip.py --fast   # load the save, then straight to main_screen
    ./ghpc/scripts/bootskip.py --on     # straight to main_screen, no save load
    ./ghpc/scripts/bootskip.py --off    # restore the stock boot sequence

Stock boot is bootup_load (memory card check and load, which also turns
autosave on), then cut_scene_screen (the intro movie), then the splash screen
and its "press any button", then main_screen.

--on flips `{set first_screen bootup_load}` in ui/gen/init.dtb to main_screen,
skipping all of it, the save load included. Measured on 2026-09-09, debug
build: main_screen at t=6.3 instead of t=38.8. Scripted runs use it for speed;
they do not want the player's profile.

--fast keeps the stock first screen and instead repoints the three exits from
the boot flow in ui/gen/mem_card.dtb (bootup_load, bootup_create_save,
autosave_disable_check) from cut_scene_screen to main_screen. So the save
loads and autosave is on, and the movie, the splash and the button wait go.

Both are byte-for-byte substitutions, no size change and no ARK repack. A DTB
symbol is a u32 length then its bytes, which BinStream::operator>>(Symbol&)
reads into a buffer, terminates and interns as a C string, so main_screen
padded with NULs to cut_scene_screen's 16 bytes loads as main_screen. The DTB
cipher is a synchronous stream cipher whose keystream does not depend on the
plaintext, so encrypting is the same operation as decrypting.

**Caveat.** --on changes the boot path, so anything bootup_load initialises is
skipped. Do not trust a song-load or audio repro taken under --on without
confirming it under --off.
"""
import argparse, importlib.util, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARK = os.path.join(ROOT, "work", "GEN", "MAIN_0.ARK")

INIT = "ui/gen/init.dtb"
INIT_STOCK, INIT_SKIP = b"bootup_load", b"main_screen"

MEMCARD = "ui/gen/mem_card.dtb"
MC_STOCK = struct.pack("<I", 16) + b"cut_scene_screen"
MC_FAST = struct.pack("<I", 16) + b"main_screen" + b"\0" * 5
MC_EXITS = 3

spec = importlib.util.spec_from_file_location(
    "dtb", os.path.join(os.path.dirname(os.path.abspath(__file__)), "dtb.py"))
dtb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dtb)


def locate(target):
    _, files = dtb.read_header()
    if target not in files:
        sys.exit("%s not found in MAIN.HDR" % target)
    off, sz, _ = files[target]
    return off, sz


def load(off, sz):
    with open(ARK, "rb") as f:
        f.seek(off)
        raw = f.read(sz)
    if len(raw) != sz:
        sys.exit("short read at %d" % off)
    return raw, dtb.decrypt(raw)


def init_skips(plain):
    if plain.count(INIT_SKIP) and not plain.count(INIT_STOCK):
        return True
    if plain.count(INIT_STOCK):
        return False
    sys.exit("%s: neither boot symbol found" % INIT)


def mc_fast(plain):
    stock, fast = plain.count(MC_STOCK), plain.count(MC_FAST)
    if (stock, fast) == (MC_EXITS, 0):
        return False
    if (stock, fast) == (0, MC_EXITS):
        return True
    sys.exit("%s: expected %d boot exits, found %d stock and %d fast" % (MEMCARD, MC_EXITS, stock, fast))


def mode(init_plain, mc_plain):
    if init_skips(init_plain):
        return "on"
    return "fast" if mc_fast(mc_plain) else "off"


def patch(target, src, dst, count):
    """Replace every src with dst in target, which must hold exactly `count`."""
    off, sz = locate(target)
    raw, plain = load(off, sz)
    n = plain.count(src)
    if n == 0 and plain.count(dst) == count:
        return 0
    if n != count:
        sys.exit("expected %d of %r in %s, found %d" % (count, src, target, n))
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
    if load(off, sz)[1] != newplain:
        sys.exit("write to %s did not take effect" % target)
    return sum(x != y for x, y in zip(raw, newraw))


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--fast", action="store_true")
    g.add_argument("--on", action="store_true")
    g.add_argument("--off", action="store_true")
    g.add_argument("--status", action="store_true")
    a = ap.parse_args()

    init_plain = load(*locate(INIT))[1]
    mc_plain = load(*locate(MEMCARD))[1]
    cur = mode(init_plain, mc_plain)
    if a.status:
        print("boot skip: %s" % cur)
        return

    want = "fast" if a.fast else "on" if a.on else "off"
    if cur == want:
        print("boot skip already %s, nothing to do" % want)
        return
    changed = 0
    if want == "on":
        changed += patch(INIT, INIT_STOCK, INIT_SKIP, 1)
    else:
        changed += patch(INIT, INIT_SKIP, INIT_STOCK, 1)
    if want == "fast":
        changed += patch(MEMCARD, MC_STOCK, MC_FAST, MC_EXITS)
    else:
        changed += patch(MEMCARD, MC_FAST, MC_STOCK, MC_EXITS)

    now = mode(load(*locate(INIT))[1], load(*locate(MEMCARD))[1])
    if now != want:
        sys.exit("write did not take effect: %s" % now)
    print("boot skip %s  (%d bytes changed)" % (want, changed))


if __name__ == "__main__":
    main()
