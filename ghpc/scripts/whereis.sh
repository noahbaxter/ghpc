#!/usr/bin/env bash
# Map a guest address to the function containing it.
#
# The exit funnel prints a raw return address, which is where the game gave up.
# This turns it into a name.
#
#   ./ghpc/scripts/whereis.sh 0x35c568
#   ./ghpc/scripts/whereis.sh 0x35c568 GH1
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ADDR="${1:-}"
TITLE="${2:-GH2}"
[ -n "$ADDR" ] || { echo "usage: whereis.sh <address> [GH2|GH1|GH80s]" >&2; exit 2; }

case "$TITLE" in
  GH2)   ELF="$ROOT/work/GH2_debug.elf" ;;
  GH1)   ELF="$ROOT/work/elf-debug/GH1_debug.elf" ;;
  GH80s) ELF="$ROOT/work/elf-debug/GH80s_debug.elf" ;;
  *) echo "unknown title: $TITLE" >&2; exit 2 ;;
esac
[ -f "$ELF" ] || { echo "missing ELF: $ELF" >&2; exit 1; }

READELF="$(command -v llvm-readelf || echo /opt/homebrew/opt/llvm/bin/llvm-readelf)"
[ -x "$READELF" ] || { echo "llvm-readelf not found" >&2; exit 1; }

"$READELF" --syms "$ELF" | python3 -c '
import sys
target = int(sys.argv[1], 0)
rows = []
for line in sys.stdin:
    p = line.split()
    if len(p) < 8:
        continue
    try:
        val, size = int(p[1], 16), int(p[2])
    except ValueError:
        continue
    rows.append((val, size, p[3], p[7]))

hit = [r for r in rows if r[0] <= target < r[0] + max(r[1], 1)]
if hit:
    for v, s, t, n in hit:
        print(f"0x{target:x} is in {n}  (starts 0x{v:x}, size {s}, {t}, +0x{target - v:x})")
else:
    pre = sorted(r for r in rows if r[0] <= target)[-5:]
    print(f"0x{target:x} is inside no symbol. Nearest preceding:")
    for v, s, t, n in pre:
        print(f"  0x{v:x} {t:8} {n}  (+0x{target - v:x})")
' "$ADDR"
