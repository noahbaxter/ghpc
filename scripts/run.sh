#!/usr/bin/env bash
# Launch the recompiled GH2. Must run with work/ as cwd so the game finds GEN/.
#
#   ./scripts/run.sh            # vanilla GH2 debug build
#   ./scripts/run.sh --quiet    # hide raylib INFO spam, game output only
#   ./scripts/run.sh --debug    # run the --debug build (bring-up diagnostics)
#   ./scripts/run.sh GH1        # or GH80s, to try the other titles
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build"
WORK="$ROOT/work"

QUIET=0; ELF="$WORK/GH2_debug.elf"
for a in "$@"; do case "$a" in
  --quiet) QUIET=1 ;;
  --debug) BUILD_DIR="build-debug" ;;
  GH1)     ELF="$WORK/elf-debug/GH1_debug.elf" ;;
  GH80s)   ELF="$WORK/elf-debug/GH80s_debug.elf" ;;
  *) echo "unknown arg: $a" >&2; exit 2 ;;
esac; done

BIN="$ROOT/$BUILD_DIR/ps2xRuntime/ps2EntryRunner"
[ -x "$BIN" ]        || { echo "binary missing, run ./scripts/build.sh first" >&2; exit 1; }
[ -f "$ELF" ]        || { echo "missing ELF: $ELF" >&2; exit 1; }
[ -f "$WORK/GEN/MAIN.HDR" ] || echo "WARNING: work/GEN/MAIN.HDR missing, game data not staged"

echo "running $(basename "$ELF")   (Ctrl-C to quit)"
cd "$WORK"
if [ "$QUIET" = 1 ]; then
  "$BIN" "$ELF" 2>&1 | grep -vE '^INFO:'
else
  "$BIN" "$ELF"
fi
