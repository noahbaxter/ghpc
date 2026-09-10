#!/usr/bin/env python3
"""Progress oracle. Answers "did the game get further" without a human.

    ./ghpc/scripts/progress.py                 # measure, compare to the mark
    ./ghpc/scripts/progress.py --record        # measure and store a new mark
    ./ghpc/scripts/progress.py --json          # machine readable verdict
    ./ghpc/scripts/progress.py --show          # print the stored mark

Exit codes, which are the point of the tool:

    0  PROGRESSED or SAME     nothing broke
    1  REGRESSED              got less far than the stored mark
    2  MEASUREMENT_FAILED     the run told us nothing, so do not conclude

The third outcome exists because conflating "no change" with "the run did not
tell us anything" is the documented way this project loses days. A failed
measurement is retried, and if every attempt fails that is reported as its own
verdict rather than as a regression.

Progress is the furthest rung of the screen ladder that `GHPC_PAD_DRIVE` reports.
`bootskip.py --on` jumps straight to `main_screen`, which is why the ladder is
scored by index rather than by counting transitions. When the rung does not move,
the sub-rung detail is what distinguishes "stuck at 2" from "stuck, unknown".
"""
import argparse, json, os, re, signal, subprocess, sys, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(ROOT, "work")
MARK = os.path.join(ROOT, "ghpc", "progress.json")

# Ordered. Index is the score. Add new screens here; an unknown screen is a hard
# error, never a silent zero.
LADDER = [
    "bootup_load", "cut_scene_screen", "guitar_help_screen", "splash_screen",
    "main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen",
    "game_screen",
]
RUNG = {name: i + 1 for i, name in enumerate(LADDER)}

DRIVE = re.compile(r"^\[drive\] (?:enter (\w+)|(\w+) -> (\w+) after)")
# Sub-rung probes: name -> (regex, group). Extend as blockers move.
DETAIL = {"streamEE_state": (re.compile(r"^\[ghpc/strm\].*\bstate=(\d+)"), 1)}


def measure(build, secs, verbose):
    """One run. Returns (rung, screen, detail, reason_if_failed)."""
    binary = os.path.join(ROOT, build, "ps2xRuntime", "ps2EntryRunner")
    if not os.path.exists(binary):
        return None, None, {}, "binary missing: %s" % binary
    env = dict(os.environ, GHPC_HIDE_WINDOW="1", GHPC_PAD_DRIVE="cross")
    p = subprocess.Popen([binary, "GH2_debug.elf"], cwd=WORK, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace", preexec_fn=os.setsid)
    best, screen, detail, seen_any = 0, None, {}, False
    deadline = time.time() + secs

    # A silent guest must not outlive the cap. The read loop below only notices
    # the deadline when a line arrives, so the kill is armed independently.
    def watchdog():
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
    timer = threading.Timer(secs, watchdog)
    timer.daemon = True
    timer.start()
    try:
        for line in p.stdout:
            if verbose:
                sys.stderr.write(line)
            m = DRIVE.match(line)
            if m:
                name = m.group(1) or m.group(3)
                if name not in RUNG:
                    raise SystemExit(
                        "unknown screen %r. Add it to LADDER in progress.py "
                        "rather than letting it score zero." % name)
                seen_any = True
                if RUNG[name] > best:
                    best, screen = RUNG[name], name
            for key, (rx, g) in DETAIL.items():
                d = rx.match(line)
                if d:
                    detail[key] = d.group(g)
            if best >= len(LADDER) or time.time() > deadline:
                break
    finally:
        timer.cancel()
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        p.wait()
    if not seen_any:
        return None, None, {}, "no [drive] lines: run produced no screen data"
    return best, screen, detail, None


def load_mark():
    if not os.path.exists(MARK):
        return None
    with open(MARK) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--record", action="store_true", help="store the result as the new mark")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--build", default="build-debug")
    ap.add_argument("--secs", type=int, default=180, help="cap per attempt")
    ap.add_argument("--attempts", type=int, default=3)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    if a.show:
        m = load_mark()
        print(json.dumps(m, indent=2) if m else "no mark recorded")
        return 0

    rung = screen = None
    detail, reasons = {}, []
    for i in range(a.attempts):
        rung, screen, detail, why = measure(a.build, a.secs, a.verbose)
        if why is None:
            break
        reasons.append("attempt %d: %s" % (i + 1, why))
        sys.stderr.write("attempt %d failed to measure (%s), retrying\n" % (i + 1, why))

    if rung is None:
        out = {"verdict": "MEASUREMENT_FAILED", "reasons": reasons}
        print(json.dumps(out) if a.json else
              "MEASUREMENT_FAILED after %d attempts. Do not conclude anything.\n  %s"
              % (a.attempts, "\n  ".join(reasons)))
        return 2

    mark = load_mark()
    prev = mark["rung"] if mark else 0
    verdict = "PROGRESSED" if rung > prev else ("SAME" if rung == prev else "REGRESSED")
    if mark is None:
        verdict = "PROGRESSED"

    result = {"verdict": verdict, "rung": rung, "screen": screen,
              "previous_rung": prev, "previous_screen": mark["screen"] if mark else None,
              "detail": detail, "ladder_size": len(LADDER)}

    if a.record and verdict != "REGRESSED":
        with open(MARK, "w") as f:
            json.dump({"rung": rung, "screen": screen, "detail": detail,
                       "recorded": time.strftime("%Y-%m-%d %H:%M:%S")}, f, indent=2)
            f.write("\n")
        result["recorded"] = True

    if a.json:
        print(json.dumps(result))
    else:
        print("%s  rung %d/%d (%s), was %d (%s)"
              % (verdict, rung, len(LADDER), screen, prev, result["previous_screen"]))
        if detail:
            print("  detail: " + "  ".join("%s=%s" % kv for kv in sorted(detail.items())))
    return 1 if verdict == "REGRESSED" else 0


if __name__ == "__main__":
    sys.exit(main())
