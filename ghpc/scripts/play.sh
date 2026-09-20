#!/usr/bin/env bash
# Spin up the port and play it by hand.
#
# Defaults to the release build with boot skip on, which lands on the main
# menu in about six seconds instead of thirty-nine. There is no state snapshot
# in the runtime, so boot skip is the fast path: it flips one symbol in
# ui/gen/init.dtb so bootup jumps straight to main_screen, past the intro
# video and both logo screens.
#
#   ./ghpc/scripts/play.sh                 release, straight to the main menu
#   ./ghpc/scripts/play.sh --intro         boot the whole way through the intro
#   ./ghpc/scripts/play.sh --debug         diagnostics build, 2.5-4x slower
#   ./ghpc/scripts/play.sh --build         rebuild before launching
#   ./ghpc/scripts/play.sh --countin 0.5   clamp the song count-in
#   ./ghpc/scripts/play.sh --log run.log   tee everything to a file
#   ./ghpc/scripts/play.sh GH1             or GH80s
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$ROOT/work"
BUILD_DIR="build"
ELF="$WORK/GH2_debug.elf"
WANT_SKIP=1
DO_BUILD=0
LOGFILE=""
COUNTIN=""
VERBOSE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --intro)    WANT_SKIP=0 ;;
    --debug)    BUILD_DIR="build-debug" ;;
    --calls)    BUILD_DIR="build-calls" ;;
    --build)    DO_BUILD=1 ;;
    --verbose)  VERBOSE=1 ;;
    --countin)  shift; COUNTIN="${1:?--countin needs a value}" ;;
    --log)      shift; LOGFILE="${1:?--log needs a path}" ;;
    GH1)        ELF="$WORK/elf-debug/GH1_debug.elf" ;;
    GH80s)      ELF="$WORK/elf-debug/GH80s_debug.elf" ;;
    -h|--help)  awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

# A second runner eats a core and makes every timing number a lie. -x matches
# the process name only; -f would match this script's own command line.
if pgrep -x ps2EntryRunner >/dev/null 2>&1; then
  echo "a ps2EntryRunner is already running. kill it first:" >&2
  echo "    pkill -x ps2EntryRunner" >&2
  exit 1
fi

if [ "$DO_BUILD" = 1 ]; then
  case "$BUILD_DIR" in
    build-debug) "$ROOT/ghpc/scripts/build.sh" --from=build --debug ;;
    build-calls) "$ROOT/ghpc/scripts/build.sh" --from=build --calls ;;
    *)           "$ROOT/ghpc/scripts/build.sh" --from=build ;;
  esac
fi

BIN="$ROOT/$BUILD_DIR/ps2xRuntime/ps2EntryRunner"
[ -x "$BIN" ] || { echo "no binary at $BIN. run with --build, or ./ghpc/scripts/build.sh" >&2; exit 1; }
[ -f "$ELF" ] || { echo "missing ELF: $ELF" >&2; exit 1; }
[ -f "$WORK/GEN/MAIN.HDR" ] || { echo "work/GEN/MAIN.HDR missing, game data is not staged" >&2; exit 1; }

# Put boot skip where this run wants it, and put it back afterwards if we were
# the ones who moved it. It patches the ARK, so leaving it flipped would
# silently change the next person's repro.
skip_state() { python3 "$ROOT/ghpc/scripts/bootskip.py" --status | awk '{print $3}'; }
BEFORE="$(skip_state)"
WANT=$([ "$WANT_SKIP" = 1 ] && echo on || echo off)
RESTORE=0
if [ "$BEFORE" != "$WANT" ]; then
  python3 "$ROOT/ghpc/scripts/bootskip.py" "--$WANT" >/dev/null
  RESTORE=1
fi
cleanup() {
  if [ "$RESTORE" = 1 ]; then
    python3 "$ROOT/ghpc/scripts/bootskip.py" "--$BEFORE" >/dev/null || true
  fi
}
trap cleanup EXIT

[ -n "$COUNTIN" ] && export GHPC_COUNTIN="$COUNTIN"

cat <<EOF
ghpc  $(basename "$ELF")  [$BUILD_DIR]  boot skip $WANT${COUNTIN:+  countin $COUNTIN}

  arrows   d-pad            enter   start
  x        cross (confirm)  z       square
  c        circle (back)    v       triangle
  q / e    L1 / R1          1 / 3   L2 / R2
  w a s d  left stick

Expect the main menu in about $([ "$WANT" = on ] && echo 6 || echo 39)s. Gameplay currently runs at
a few frames per second and has no audio; that is the known state, not a
broken install. Ctrl-C to quit.

EOF

# --line-buffered matters: without it grep holds output in a 4K block and a
# tailed log sits empty for minutes while the game is plainly running.
cd "$WORK"
filter() {
  if [ "$VERBOSE" = 1 ]; then cat; else grep --line-buffered -vE '^INFO:'; fi
}
if [ -n "$LOGFILE" ]; then
  "$BIN" "$ELF" 2>&1 | filter | tee "$LOGFILE"
else
  "$BIN" "$ELF" 2>&1 | filter
fi
