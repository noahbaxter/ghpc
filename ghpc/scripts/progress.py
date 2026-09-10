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
NEXT = os.path.join(ROOT, "ghpc", "NEXT.md")
BEGIN, END = "<!-- PROGRESS:BEGIN -->", "<!-- PROGRESS:END -->"

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
DETAIL = {
    "streamEE_state": (re.compile(r"^\[ghpc/strm\].*\bstate=(\d+)"), 1),
    "song_tick": (re.compile(r"^\[ghpc/song\].*\btick=(-?[\d.]+)"), 1),
}

# Sub-rungs whose whole meaning is whether the number moved. `game_screen` is
# the top of LADDER, so once the song load lands the rung cannot say anything
# more; the chart advancing is the only thing left that separates "a screen
# appeared" from "the song is playing". For these keys the first and last value
# are both kept and the span is reported.
ADVANCE = ["song_tick"]


def probe_env():
    """The GHPC_* knobs this run inherited, minus the two we always set.

    A mark recorded with a probe enabled is not comparable to one without, and
    the file used to have no way to say so. A rung 9 recorded under
    GHPC_STREAM_READY once became the floor for every later round.
    """
    always = {"GHPC_HIDE_WINDOW", "GHPC_PAD_DRIVE"}
    return {k: v for k, v in sorted(os.environ.items())
            if k.startswith("GHPC_") and k not in always}


def confirmed_rung(timeline, end, hold):
    """Highest rung the run actually stayed on.

    `timeline` is [(elapsed, rung, name)] in order. A rung counts only if,
    after it was first reached, no lower rung was ever reported again AND it
    survived `hold` seconds. Reaching a screen and falling out of it is not
    progress, and the previous version of this file could not tell the two
    apart: it broke out of the read loop the moment the top rung appeared, so
    it observed zero frames afterwards and scored a bounce as a win.
    """
    best = 0
    for i, (t, rung, name) in enumerate(timeline):
        if any(later_rung < rung for _, later_rung, _ in timeline[i + 1:]):
            continue  # fell out of it later, so it was never really reached
        if (end - t) < hold:
            continue  # not observed long enough to call it held
        if rung > best:
            best = rung
    return best


def measure(build, secs, hold, verbose):
    """One run. Returns (result_dict, reason_if_failed).

    Always runs to the cap. It must never stop early on reaching the top rung:
    the single most important question about a new best is whether it stays,
    and halting on arrival makes that question unanswerable.
    """
    binary = os.path.join(ROOT, build, "ps2xRuntime", "ps2EntryRunner")
    if not os.path.exists(binary):
        return None, "binary missing: %s" % binary
    if hold >= secs:
        return None, "hold (%ds) must be shorter than the cap (%ds)" % (hold, secs)
    env = dict(os.environ, GHPC_HIDE_WINDOW="1", GHPC_PAD_DRIVE="cross")
    started = time.time()
    p = subprocess.Popen([binary, "GH2_debug.elf"], cwd=WORK, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace", preexec_fn=os.setsid)
    timeline, detail, first = [], {}, {}
    deadline = started + secs

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
                timeline.append((time.time() - started, RUNG[name], name))
            for key, (rx, g) in DETAIL.items():
                d = rx.match(line)
                if d:
                    detail[key] = d.group(g)
                    first.setdefault(key, d.group(g))
            if time.time() > deadline:
                break
    finally:
        timer.cancel()
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        p.wait()
    end = time.time() - started
    if not timeline:
        return None, "no [drive] lines: run produced no screen data"

    for key in ADVANCE:
        if key not in detail:
            continue
        try:
            span = float(detail[key]) - float(first[key])
        except ValueError:
            continue
        # Named rather than folded into a bool: "polled once and never again"
        # and "polled 4000 times at the same tick" are the same verdict here but
        # different bugs, and the span plus the first value keeps them apart.
        detail[key + "_from"] = first[key]
        detail[key + "_advanced"] = "yes" if span > 0.0 else "no"

    peak = max(r for _, r, _ in timeline)
    held = confirmed_rung(timeline, end, hold)
    final = timeline[-1][1]
    return {
        "rung": held,
        "screen": LADDER[held - 1] if held else None,
        "peak_rung": peak,
        "peak_screen": LADDER[peak - 1],
        "final_screen": timeline[-1][2],
        # peak above held means it got somewhere and did not stay. That is the
        # exact shape a metric-satisfying poke produces, so it is named rather
        # than averaged away.
        "bounced": peak > held,
        "ran_secs": round(end, 1),
        "hold_secs": hold,
        "detail": detail,
        "env": probe_env(),
    }, None


def load_mark():
    if not os.path.exists(MARK):
        return None
    with open(MARK) as f:
        return json.load(f)


def _rendered_text(text, m):
    """Return `text` with the generated block rebuilt from mark `m`."""
    body = ["| | value |", "|---|---|",
            "| furthest screen | `%s` |" % m["screen"],
            "| rung | %d of %d |" % (m["rung"], len(LADDER)),
            "| recorded | %s |" % m["recorded"]]
    if m.get("build"):
        body.append("| build | `%s` |" % m["build"])
    if m.get("hold_secs"):
        body.append("| held for | %ss |" % m["hold_secs"])
    for k, v in sorted(m.get("detail", {}).items()):
        body.append("| %s | `%s` |" % (k.replace("_", " "), v))
    # A floor set with a probe on is not a floor for a default build. Naming the
    # probes here is what stops the next round inheriting a number it cannot
    # reproduce.
    env = m.get("env", {})
    body.append("| probes | %s |" % (
        "  ".join("`%s=%s`" % kv for kv in sorted(env.items())) if env
        else "none, stock build"))
    block = "%s\n<!-- generated by scripts/progress.py --render, do not edit -->\n\n%s\n\n%s" % (
        BEGIN, "\n".join(body), END)
    if BEGIN not in text or END not in text:
        sys.exit("NEXT.md is missing the PROGRESS markers")
    pre, rest = text.split(BEGIN, 1)
    _, post = rest.split(END, 1)
    return pre + block + post


def render():
    """Rewrite NEXT.md's generated block from the stored mark. Never hand-typed."""
    m = load_mark()
    if m is None:
        sys.exit("no mark recorded; run progress.py --record first")
    if not os.path.exists(NEXT):
        sys.exit("missing %s" % NEXT)
    # Read fully, transform, then write. Opening for write first truncates the
    # file before the read runs, which destroys it if the transform then fails.
    text = open(NEXT).read()
    updated = _rendered_text(text, m)
    open(NEXT, "w").write(updated)
    print("rendered %s: rung %d (%s)" % (os.path.basename(NEXT), m["rung"], m["screen"]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--record", action="store_true", help="store the result as the new mark")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if NEXT.md's generated block is stale")
    ap.add_argument("--render", action="store_true",
                    help="rewrite the generated block in NEXT.md from the stored mark")
    ap.add_argument("--build", default="build-debug")
    ap.add_argument("--secs", type=int, default=300, help="cap per attempt")
    ap.add_argument("--hold", type=int, default=60,
                    help="seconds a screen must survive before it counts")
    ap.add_argument("--attempts", type=int, default=3)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    if a.check:
        m = load_mark()
        if m is None:
            sys.exit("no mark recorded")
        if not os.path.exists(NEXT):
            sys.exit("missing %s" % NEXT)
        cur = open(NEXT).read()
        # Staleness is computed by re-rendering and comparing, never judged.
        stale = _rendered_text(cur, m) != cur
        print("NEXT.md is %s" % ("STALE" if stale else "fresh"))
        return 1 if stale else 0

    if a.render:
        return render()

    if a.show:
        m = load_mark()
        print(json.dumps(m, indent=2) if m else "no mark recorded")
        return 0

    run, reasons = None, []
    for i in range(a.attempts):
        run, why = measure(a.build, a.secs, a.hold, a.verbose)
        if why is None:
            break
        reasons.append("attempt %d: %s" % (i + 1, why))
        sys.stderr.write("attempt %d failed to measure (%s), retrying\n" % (i + 1, why))

    if run is None:
        out = {"verdict": "MEASUREMENT_FAILED", "reasons": reasons}
        print(json.dumps(out) if a.json else
              "MEASUREMENT_FAILED after %d attempts. Do not conclude anything.\n  %s"
              % (a.attempts, "\n  ".join(reasons)))
        return 2

    rung, screen, detail = run["rung"], run["screen"], run["detail"]
    mark = load_mark()
    prev = mark["rung"] if mark else 0
    verdict = "PROGRESSED" if rung > prev else ("SAME" if rung == prev else "REGRESSED")
    if mark is None:
        verdict = "PROGRESSED"

    result = dict(run, verdict=verdict, previous_rung=prev,
                  previous_screen=mark["screen"] if mark else None,
                  ladder_size=len(LADDER), build=a.build,
                  failed_attempts=reasons)

    # A mark set under a probe is not a floor the next round can be judged
    # against. Comparing across different GHPC_* knobs is a category error, so
    # it is refused rather than reported as a number.
    prev_env = mark.get("env", {}) if mark else {}
    if verdict != "SAME" and prev_env != run["env"]:
        result["env_mismatch"] = {"mark": prev_env, "run": run["env"]}

    if a.record and verdict != "REGRESSED":
        if run["bounced"]:
            sys.stderr.write(
                "refusing to record: reached %s but fell out of it. A rung that "
                "does not hold is not a floor.\n" % run["peak_screen"])
            result["recorded"] = False
        else:
            with open(MARK, "w") as f:
                json.dump({"rung": rung, "screen": screen, "detail": detail,
                           "env": run["env"], "build": a.build,
                           "hold_secs": run["hold_secs"],
                           "recorded": time.strftime("%Y-%m-%d %H:%M:%S")},
                          f, indent=2)
                f.write("\n")
            result["recorded"] = True
        # Recording without re-rendering is how the handoff goes stale, so the
        # two are the same action rather than two things to remember. A render
        # failure must not exit 1 here: exit 1 means REGRESSED to the caller, and
        # a cosmetic failure signalling a regression would be worse than a stale
        # block.
        try:
            render()
        except SystemExit as e:
            sys.stderr.write("warning: could not render NEXT.md (%s)\n" % e)
            result["render_failed"] = str(e)

    if a.json:
        print(json.dumps(result))
    else:
        print("%s  rung %d/%d (%s), was %d (%s)"
              % (verdict, rung, len(LADDER), screen, prev, result["previous_screen"]))
        print("  ran %ss, a screen counts after %ss held; ended on %s"
              % (run["ran_secs"], run["hold_secs"], run["final_screen"]))
        if run["bounced"]:
            print("  BOUNCED: touched %s (rung %d) and fell out of it. Not scored."
                  % (run["peak_screen"], run["peak_rung"]))
        if run["env"]:
            print("  probes active: "
                  + "  ".join("%s=%s" % kv for kv in sorted(run["env"].items())))
        if "env_mismatch" in result:
            print("  ENV MISMATCH: the mark was set under %s, this run under %s. "
                  "Not comparable."
                  % (result["env_mismatch"]["mark"] or "no probes",
                     result["env_mismatch"]["run"] or "no probes"))
        if detail:
            print("  detail: " + "  ".join("%s=%s" % kv for kv in sorted(detail.items())))
        if reasons:
            print("  %d attempt(s) failed to measure first: %s"
                  % (len(reasons), "; ".join(reasons)))
    return 1 if verdict == "REGRESSED" else 0


if __name__ == "__main__":
    sys.exit(main())
