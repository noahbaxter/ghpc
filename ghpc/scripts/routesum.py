#!/usr/bin/env python3
"""One summary line for a route.sh arm.

    ghpc/scripts/routesum.py <armdir> [<armdir>...]

Reads what route.sh leaves in an arm directory: clock.csv (GHPC_CLOCKTRACE),
present.csv (GHPC_PRESENTTRACE) and run.log. Every number here is one the
route's items are judged on, so a missing input prints as MISSING rather than
as zero: a trace that never got written is not a clean run.

    screens   distinct screens in order, consecutive repeats collapsed
    songs     game_screen visits
    stale     mesh cache rebuilds on a reused guest address (last mesh line)
    noCam     draws with no camera (last mesh line)
    texStale  texture binds with the old pointers and dims but new bytes
    presents  presents in the trace
    hz        presents a second, which is the display's refresh
    repeat    presents that showed the same scene serial as the one before
    skip      scene serials never presented, against the serial span
    blank     presents with no camera, which show an empty frame
    short     presents under 80% of the draw median of their neighbours
    game      game clock rate on game_screen, min-max over 1s windows
    ui        ui clock rate off game_screen, min-max over 1s windows
"""
import csv
import os
import re
import statistics
import sys

WINDOW = 1.0      # seconds per rate window
SETTLE = 2.0      # seconds after a screen change before a window counts
NEIGHBOURS = 15   # presents either side for the short-frame median


def read_csv(path):
    if not os.path.exists(path):
        return None
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def screens(clock):
    seq = []
    for row in clock:
        s = row["screen"]
        if s and (not seq or seq[-1] != s):
            seq.append(s)
    return seq


def rates(clock, want_game):
    """Min and max clock rate over windows that sit on one screen."""
    out = []
    start = None
    entered = None
    last_screen = None
    for row in clock:
        wall = float(row["wall"])
        screen = row["screen"]
        if screen != last_screen:
            last_screen, entered, start = screen, wall, None
            continue
        on_game = screen == "game_screen"
        if on_game != want_game or wall - entered < SETTLE:
            continue
        key = "game" if want_game else "ui"
        value = float(row[key])
        if start is None:
            start = (wall, value)
            continue
        if wall - start[0] >= WINDOW:
            rate = (value - start[1]) / (wall - start[0])
            # A clock that reads zero here is a screen that does not run it,
            # not a stopped game; lose_screen holds game at its last value.
            if value > 0.0:
                out.append(rate)
            start = (wall, value)
    if not out:
        return "none"
    return "%.2f-%.2f" % (min(out), max(out))


def presents(rows):
    n = len(rows)
    if n == 0:
        return {"presents": 0}
    serials = [int(r["serial"]) for r in rows]
    repeat = sum(1 for a, b in zip(serials, serials[1:]) if a == b)
    skipped = sum(b - a - 1 for a, b in zip(serials, serials[1:]) if b > a + 1)
    span = max(1, serials[-1] - serials[0])
    wall = float(rows[-1]["wall"]) - float(rows[0]["wall"])
    out = {
        "presents": n,
        # repeat scales with this: a 120 Hz display repeats every guest frame.
        "hz": "%.0f" % (n / wall if wall > 0 else 0.0),
        "repeat": "%.1f%%" % (100.0 * repeat / max(1, n - 1)),
        "skip": "%.1f%%" % (100.0 * skipped / span),
    }
    if "draws" in rows[0]:
        draws = [int(r["draws"]) for r in rows]
        cams = [int(r["camera"]) for r in rows]
        short = 0
        for i, d in enumerate(draws):
            near = draws[max(0, i - NEIGHBOURS):i + NEIGHBOURS + 1]
            med = statistics.median(near)
            if med > 0 and d < 0.8 * med:
                short += 1
        out["blank"] = sum(1 for c in cams if c == 0)
        out["short"] = "%.1f%%" % (100.0 * short / n)
    return out


def last_line(log, prefix):
    if not os.path.exists(log):
        return None
    last = None
    with open(log, errors="replace") as f:
        for line in f:
            if line.startswith(prefix):
                last = line
    return last


def summarise(arm):
    parts = [os.path.basename(os.path.normpath(arm)) + ":"]
    clock = read_csv(os.path.join(arm, "clock.csv"))
    if clock is None:
        parts.append("clock=MISSING")
    else:
        seq = screens(clock)
        parts.append("screens=" + ">".join(s.replace("_screen", "") for s in seq))
        parts.append("songs=%d" % seq.count("game_screen"))
    line = last_line(os.path.join(arm, "run.log"), "[ghpc/vk/mesh] ")
    if line is None:
        parts.append("mesh=MISSING")
    else:
        # The shutdown line spells it noCamera, the periodic one noCam.
        for key, pattern in (("stale", r"\bstale=(\d+)"), ("noCam", r"\bnoCam(?:era)?=(\d+)")):
            m = re.search(pattern, line)
            parts.append("%s=%s" % (key, m.group(1) if m else "MISSING"))
    tex = last_line(os.path.join(arm, "run.log"), "[ghpc/scene/tex] ")
    m = re.search(r"\btexStale=(\d+)", tex or "")
    parts.append("texStale=%s" % (m.group(1) if m else "MISSING"))
    rows = read_csv(os.path.join(arm, "present.csv"))
    if rows is None:
        parts.append("present=MISSING")
    else:
        parts.extend("%s=%s" % kv for kv in presents(rows).items())
    if clock is not None:
        parts.append("game=" + rates(clock, True))
        parts.append("ui=" + rates(clock, False))
    shots = [f for f in os.listdir(arm) if f.endswith(".ppm")]
    parts.append("shots=%d" % len(shots))
    return " ".join(parts)


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    for arm in sys.argv[1:]:
        print(summarise(arm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
