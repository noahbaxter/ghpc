#!/usr/bin/env bash
# Replay a captured draw fixture through the host transform and diff it
# against the primitives VU1 produced for the same draws.
#
# Capture one first, which needs a game run (mode 2 runs the host transform,
# submits nothing and lets VU1 draw, so both sides exist in one run):
#
#   GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=/tmp/gh2.fix GHPC_COUNTIN=0.5 \
#     python3 ghpc/scripts/progress.py --build build
#
# After that the loop is this script alone, which builds and runs in about a
# second. That is the whole point: a 300s game run is a terrible way to find
# out a formula is off by a sign.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FIX="${1:-/tmp/gh2.fix}"
OUT="${TMPDIR:-/tmp}/ghpc_fixreplay"

if [ ! -f "$FIX" ]; then
  echo "no fixture at $FIX" >&2
  echo "capture one with GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=$FIX (see header)" >&2
  exit 2
fi

c++ -std=c++17 -O2 -Wall \
  -I "$ROOT/ps2xRuntime/include" \
  -o "$OUT" "$ROOT/ghpc/tools/fixreplay.cpp"

exec "$OUT" "$FIX"
