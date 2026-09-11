#!/usr/bin/env bash
# Overlay hand-written function bodies from ghpc/override/ onto the staged
# runner. Run by build.sh after the rsync that fills ps2xRuntime/src/runner/,
# and again before every compile so --from=build picks up override edits.
#
#   ./ghpc/scripts/overlay.sh            # check, then copy
#   ./ghpc/scripts/overlay.sh --check    # check only, copy nothing
#
# An override is a .cpp named exactly like the generated file it replaces,
# e.g. ghpc/override/EIntr_0x3518c8.cpp. Copying it over the staged copy is the
# whole mechanism: the runner target globs src/runner/*.cpp, so a same-named
# file is picked up by both the unity and per-TU builds with no CMake changes.
#
# Every override must match a generated file that was just staged AND a
# declaration of the same function in ps2_recompiled_functions.h. Either miss
# fails the build. A missing target means the address moved, the function was
# split, or the ELF changed; keeping a stale override silently is exactly the
# DataArray incident (a fix that lived only as an unreproducible object file).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OVR="$ROOT/ghpc/override"
RUNNER="$ROOT/ps2xRuntime/src/runner"
HDR="$RUNNER/ps2_recompiled_functions.h"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

shopt -s nullglob
files=("$OVR"/*.cpp)
[ ${#files[@]} -gt 0 ] || { echo "    no overrides"; exit 0; }

[ -f "$HDR" ] || { echo "overlay: $HDR missing, stage first (build.sh --to=stage)" >&2; exit 1; }

bad=0
for f in "${files[@]}"; do
  name="$(basename "$f")"; fn="${name%.cpp}"
  if [ ! -f "$RUNNER/$name" ]; then
    echo "overlay: FAIL $name: no generated $RUNNER/$name" >&2; bad=1; continue
  fi
  if ! grep -q "^void $fn(uint8_t\* rdram, R5900Context\* ctx, PS2Runtime \*runtime);" "$HDR"; then
    echo "overlay: FAIL $name: $fn not declared in $(basename "$HDR") with the recompiled signature" >&2; bad=1; continue
  fi
  # The override must define the same symbol it claims to replace.
  if ! grep -q "^void $fn(" "$f"; then
    echo "overlay: FAIL $name: file does not define $fn" >&2; bad=1; continue
  fi
done
[ "$bad" = 0 ] || { echo "overlay: refusing to build with stale overrides. Rename or delete them under $OVR" >&2; exit 1; }

[ "$CHECK_ONLY" = 1 ] && { echo "    ${#files[@]} override(s) verified"; exit 0; }
for f in "${files[@]}"; do cp -p "$f" "$RUNNER/$(basename "$f")"; done
echo "    ${#files[@]} override(s) applied: $(for f in "${files[@]}"; do basename "$f" .cpp; done | tr '\n' ' ')"
