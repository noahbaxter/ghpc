#!/usr/bin/env bash
# Run the game under lldb and stop where it gives up.
#
# The game does not segfault when it "crashes": it calls abort(), which the
# recompiler routes through _exit into ps2_stubs::exit, so the process stops
# with no host fault and no crash report. Breaking on that funnel is the only
# way to see who gave up and why.
#
#   ./ghpc/scripts/debug.sh                # interactive, stops at the exit funnel
#   ./ghpc/scripts/debug.sh --batch        # run to the stop, dump state, quit
#   ./ghpc/scripts/debug.sh --release      # use the release build
#   ./ghpc/scripts/debug.sh GH1            # or GH80s
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="build-debug"
WORK="$ROOT/work"
ELF="$WORK/GH2_debug.elf"
BATCH=0

for a in "$@"; do case "$a" in
  --batch)   BATCH=1 ;;
  --release) BUILD_DIR="build" ;;
  --debug)   BUILD_DIR="build-debug" ;;
  GH1)       ELF="$WORK/elf-debug/GH1_debug.elf" ;;
  GH80s)     ELF="$WORK/elf-debug/GH80s_debug.elf" ;;
  *) echo "unknown arg: $a" >&2; exit 2 ;;
esac; done

BIN="$ROOT/$BUILD_DIR/ps2xRuntime/ps2EntryRunner"
[ -x "$BIN" ] || { echo "binary missing, run ./ghpc/scripts/build.sh first" >&2; exit 1; }
[ -f "$ELF" ] || { echo "missing ELF: $ELF" >&2; exit 1; }
command -v lldb >/dev/null || { echo "lldb not found (install Xcode command line tools)" >&2; exit 1; }

# $a0 is the exit code and $ra names the guest call site. Both live in the
# R5900Context, so print them from the stub's own argument rather than guessing.
COMMANDS=(
  -o "breakpoint set --name ps2_stubs::exit"
  -o "breakpoint set --name abort_0x35c558"
  -o "breakpoint command add --script-type none --one-liner \"script print('--- guest gave up, host stack follows ---')\""
  -o "run"
  -o "bt all"
  -o "frame variable"
)
[ "$BATCH" = 1 ] && COMMANDS+=(-o "quit")

cd "$WORK"
echo "lldb: breaking on the guest exit funnel. 'c' to continue, 'bt' for the host stack."
exec lldb "${COMMANDS[@]}" -- "$BIN" "$ELF"
