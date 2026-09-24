#!/usr/bin/env bash
# Spin up the port and play it by hand.
#
# Defaults to the release build with a fast boot: the memory card loads, then
# straight to the main menu, past the intro video, the splash and its button
# wait. Scripted runs skip the card load as well. See bootskip.py.
#
#   ./ghpc/scripts/play.sh                 release, straight to the main menu
#   ./ghpc/scripts/play.sh --ingame        walk the menus and stop in the song
#   ./ghpc/scripts/play.sh --native        Vulkan at the Rnd seam with VU1 off:
#                                          realtime gameplay, see NEXT.md. Game
#                                          time follows the wall clock under
#                                          host load (GHPC_WALL_PACE=0 to stop)
#   ./ghpc/scripts/play.sh --guitar        present the pad as a PS2 guitar, so
#                                          notes need a strum. Without it the
#                                          game sees a joypad and frets alone
#                                          play notes
#
# Keys: 1-5 frets, arrows D-pad (up/down strum), X/C/Z/V cross/circle/
# square/triangle, Enter start. F5 saves the screen on show as
# work/states/menu_<screen>, confirmed in the window title; not under --native.
#   ./ghpc/scripts/play.sh --bind          record a controller's layout into
#                                          work/ghpc.ini, then play. Controllers
#                                          are read under --native only, so:
#                                          --native --guitar --bind
#   ./ghpc/scripts/play.sh --probe         print a controller's raw state live
#
# work/ghpc.ini also sets the window under --native:
#   [display]  resolution = 1280x960  fullscreen = 1
#   ./ghpc/scripts/play.sh --state song    resume a save state from work/states
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
INGAME=0
DO_BUILD=0
LOGFILE=""
COUNTIN=""
VERBOSE=0
BIND=""

while [ $# -gt 0 ]; do
  case "$1" in
    --ingame)   INGAME=1 ;;
    --native)   export GHPC_RENDERER=vulkan GHPC_VK_MESH=2 GHPC_VU1_OFF=1 GHPC_WALL_PACE="${GHPC_WALL_PACE:-1}" ;;
    --guitar)   export GHPC_PAD_GUITAR=1 ;;
    --bind)     BIND=bind ;;
    --probe)    BIND=--probe ;;
    --state)    shift; export GHPC_STATE_LOAD="${1:?--state needs a slot}" ;;
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

if [ -n "$BIND" ]; then
  cmake --build "$ROOT/$BUILD_DIR" --target ghpcbind > /dev/null
  BINDER="$ROOT/$BUILD_DIR/ps2xRuntime/ghpcbind"
  if [ "$BIND" = --probe ]; then
    exec "$BINDER" --probe "$WORK/ghpc.ini"
  fi
  "$BINDER" "$WORK/ghpc.ini"
fi

BIN="$ROOT/$BUILD_DIR/ps2xRuntime/ps2EntryRunner"
[ -x "$BIN" ] || { echo "no binary at $BIN. run with --build, or ./ghpc/scripts/build.sh" >&2; exit 1; }
[ -f "$ELF" ] || { echo "missing ELF: $ELF" >&2; exit 1; }
[ -f "$WORK/GEN/MAIN.HDR" ] || { echo "work/GEN/MAIN.HDR missing, game data is not staged" >&2; exit 1; }

# Put boot skip where this run wants it and leave it there.
#
# An earlier version restored the previous state on exit. That was wrong twice
# over. The ARK state ended up depending on how the process died, since a
# SIGKILL skips the trap entirely. And every run politely reverted the patch to
# whatever it found, so a --ingame run flipped skip on, ran fast, put it back
# to off, and the next run booted the intro again. Boot skip is a persistent
# setting, not a per-run one. Flip it, say so, move on.
#
# Hand play boots fast: the save loads (and autosave comes on), then straight
# to the main menu. A scripted run (a save state, the pad driver, an
# unfocusable test window) skips the save load too, since it has no business
# with the player's profile.
SCRIPTED=0
if [ "$INGAME" = 1 ] || [ -n "${GHPC_STATE_LOAD:-}" ] || [ -n "${GHPC_PAD_DRIVE:-}" ] ||
   [ -n "${GHPC_PAD_AUTO:-}" ] || [ -n "${GHPC_NO_FOCUS:-}" ]; then
  SCRIPTED=1
fi
SKIP_NOW="$(python3 "$ROOT/ghpc/scripts/bootskip.py" --status | awk '{print $3}')"
if [ "$WANT_SKIP" = 0 ]; then WANT=off; elif [ "$SCRIPTED" = 1 ]; then WANT=on; else WANT=fast; fi
if [ "$SKIP_NOW" != "$WANT" ]; then
  python3 "$ROOT/ghpc/scripts/bootskip.py" "--$WANT"
fi

# --ingame walks the three menus and then goes quiet. The "*=none" fallback is
# what stops it: without it an unlisted screen falls through to cross, and the
# driver would strum all the way through the song. Explicit rules only.
if [ "$INGAME" = 1 ]; then
  export GHPC_PAD_DRIVE="main_screen=cross,qp_selsong_screen=cross,qp_diff_screen=cross,*=none"
  [ -z "$COUNTIN" ] && COUNTIN=0.5
fi

[ -n "$COUNTIN" ] && export GHPC_COUNTIN="$COUNTIN"

cat <<EOF
ghpc  $(basename "$ELF")  [$BUILD_DIR]  boot skip $WANT${COUNTIN:+  countin $COUNTIN}

  arrows   d-pad            enter   start
  x        cross (confirm)  z       square
  c        circle (back)    v       triangle
  q / e    L1 / R1          1 / 3   L2 / R2
  w a s d  left stick

Expect the main menu in about $([ "$WANT" = off ] && echo 39 || { [ "$WANT" = fast ] && echo 15 || echo 6; })s. Ctrl-C to quit.

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
