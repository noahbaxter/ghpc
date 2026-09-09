#!/usr/bin/env bash
# Build pipeline: GH2 PS2 ELF -> recompiled C++ -> native binary.
#
#   ./scripts/build.sh              # full pipeline
#   ./scripts/build.sh --from=build # skip recompile, just rebuild
#   ./scripts/build.sh --run        # build then launch
#   ./scripts/build.sh --no-unity   # one TU per function (slow, better errors)
#   ./scripts/build.sh --to=stage   # stop before the long compile
#   ./scripts/build.sh --fast       # drop LTO, ~90s off every relink
#   ./scripts/build.sh --debug      # bring-up diagnostics: thread census, GS/CD/ARK tracing
#   ./scripts/build.sh --restore    # put PS2Recomp's stock runner back
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GHPC="$ROOT/ghpc"
PS2R="$ROOT"
BUILD="$PS2R/build"   # recomputed after arg parsing (see DIAG)
RUNNER="$PS2R/ps2xRuntime/src/runner"
WORK="$ROOT/work"
GEN="$WORK/output"
ELF="$WORK/GH2_debug.elf"
SRC_ELF="$ROOT/third_party/milo-executable-library/gh2/PS2 Final Debug/SLUS_214.47"
JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)"

FROM=all; TO=build; RUN=0; UNITY=ON; RESTORE=0; LTO=ON; DIAG=OFF; HIST=OFF
for a in "$@"; do case "$a" in
  --from=*)   FROM="${a#*=}" ;;
  --to=*)     TO="${a#*=}" ;;
  --run)      RUN=1 ;;
  --no-unity) UNITY=OFF ;;
  --fast)     LTO=OFF ;;
  --debug)    DIAG=ON ;;
  --calls)    HIST=ON; DIAG=ON ;;
  --restore)  RESTORE=1 ;;
  -h|--help)  sed -n '2,11p' "$0"; exit 0 ;;
  *) echo "unknown arg: $a" >&2; exit 2 ;;
esac; done

# Debug and release live in separate build trees so they coexist and neither
# forces a full recompile of the other when you switch.
[ "$DIAG" = ON ] && BUILD="$PS2R/build-debug"
# The histogram changes every generated TU, so it gets its own tree rather than
# forcing build-debug through a full rebuild on every toggle.
[ "$HIST" = ON ] && BUILD="$PS2R/build-calls"

b(){ printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
ok(){ printf '\033[1;32m    %s\033[0m\n' "$*"; }
warn(){ printf '\033[1;33m    %s\033[0m\n' "$*"; }
t0=$SECONDS; stage_t(){ printf '\033[2m    [%ds]\033[0m\n' "$((SECONDS-t0))"; }

if [ "$RESTORE" = 1 ]; then
  b "Restoring stock runner"
  rm -rf "$RUNNER"; mkdir -p "$RUNNER"
  git -C "$PS2R" checkout -- ps2xRuntime/src/runner 2>/dev/null || true
  ok "done"; exit 0
fi

idx(){ local order="tools recomp stage build" i=0 s
  for s in $order; do [ "$s" = "$1" ] && { echo $i; return; }; i=$((i+1)); done; echo -1; }
want(){
  local cur; cur=$(idx "$1")
  local lo=0; [ "$FROM" != all ] && lo=$(idx "$FROM")
  local hi; hi=$(idx "$TO")
  [ "$cur" -ge "$lo" ] && [ "$cur" -le "$hi" ]
}

command -v ccache >/dev/null && LAUNCHER="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache" || LAUNCHER=""

# ---------------------------------------------------------------- tools
if want tools; then
  b "1/4  Building recompiler + analyzer"
  cmake -S "$PS2R" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=ON \
    -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF \
    -DPS2X_ENABLE_RUNNER_UNITY_BUILD=$UNITY -DPS2X_ENABLE_LTO=$LTO -DPS2X_GHPC_DIAG=$DIAG -DPS2X_ENABLE_CALL_HISTOGRAM=$HIST $LAUNCHER
  cmake --build "$BUILD" --target ps2_recomp ps2_analyzer -j "$JOBS"
  ok "ps2_recomp + ps2_analyzer ready"; stage_t
fi

# ---------------------------------------------------------------- recomp
if want recomp; then
  b "2/4  Recompiling GH2 ELF -> C++"
  mkdir -p "$WORK"
  [ -f "$ELF" ] || { cp "$SRC_ELF" "$ELF"; ok "staged $(basename "$SRC_ELF")"; }
  "$BUILD/ps2xAnalyzer/ps2_analyzer" "$ELF" "$WORK/gh2.toml" | tail -8
  # analyzer writes absolute-ish paths; make sure output lands in $GEN
  python3 - "$WORK/gh2.toml" "$GEN" <<'PY'
import sys,re,pathlib
p,out=pathlib.Path(sys.argv[1]),sys.argv[2]
t=p.read_text()
t=re.sub(r'^output\s*=.*$', f'output = "{out}/"', t, flags=re.M)
p.write_text(t)
PY
  # Drop stubs we refuse to let the analyzer bind (see ghpc/config/stub-denylist.txt)
  python3 - "$WORK/gh2.toml" "$GHPC/config/stub-denylist.txt" <<'PYDENY'
import sys,re,pathlib
toml,deny = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
names = {l.strip() for l in deny.read_text().splitlines()
         if l.strip() and not l.startswith('#')}
t = toml.read_text()
m = re.search(r'^(stubs\s*=\s*\[)(.*?)(\])', t, re.S|re.M)
if m and names:
    kept, dropped = [], []
    for item in m.group(2).split(','):
        s = item.strip()
        if not s: continue
        (dropped if s.strip('"').split('@')[0] in names else kept).append(s)
    block = m.group(1) + "\n  " + ",\n  ".join(kept) + "\n" + m.group(3)
    # no_reloc_bind also stops callsite relocation auto-binding, which the
    # stubs list alone does not cover.
    block += "\nno_reloc_bind = [\n  " + ",\n  ".join(f'"{n}"' for n in sorted(names)) + "\n]"
    t = t[:m.start()] + block + t[m.end():]
    toml.write_text(t)
    print(f"  denylist: dropped {len(dropped)} stub(s), kept {len(kept)}, no_reloc_bind={len(names)}")
    for d in dropped: print(f"    - {d}")
PYDENY
  rm -rf "$GEN"; mkdir -p "$GEN"
  "$BUILD/ps2xRecomp/ps2_recomp" "$WORK/gh2.toml" > "$WORK/recomp.log" 2>&1 || {
    warn "recompiler failed, tail of work/recomp.log:"; tail -20 "$WORK/recomp.log"; exit 1; }
  tail -3 "$WORK/recomp.log"
  ok "$(find "$GEN" -name '*.cpp' | wc -l | tr -d ' ') .cpp files, $(du -sh "$GEN" | cut -f1)"
  warn "$(grep -c 'unresolved JR/JALR' "$WORK/recomp.log" || true) unresolved-indirect-jump sites -> work/recomp.log"
  stage_t
fi

# ---------------------------------------------------------------- stage
if want stage; then
  b "3/4  Staging generated code into runner"
  [ -d "$RUNNER.stock" ] || { cp -r "$RUNNER" "$RUNNER.stock"; ok "backed up stock runner"; }
  mkdir -p "$RUNNER"
  rsync -a --delete --include='*.cpp' --include='*.h' --exclude='*' "$GEN/" "$RUNNER/"
  ok "$(find "$RUNNER" -type f | wc -l | tr -d ' ') files staged"; stage_t
fi

# ---------------------------------------------------------------- build
if want build; then
  b "4/4  Building ps2EntryRunner  (unity=$UNITY, lto=$LTO, diag=$DIAG, -j$JOBS)"
  warn "this is the long one: ~12.7k generated files"
  cmake -S "$PS2R" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=ON \
    -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF \
    -DPS2X_ENABLE_RUNNER_UNITY_BUILD=$UNITY -DPS2X_ENABLE_LTO=$LTO -DPS2X_GHPC_DIAG=$DIAG -DPS2X_ENABLE_CALL_HISTOGRAM=$HIST $LAUNCHER > /dev/null
  cmake --build "$BUILD" --target ps2EntryRunner -j "$JOBS"
  ok "binary: $BUILD/ps2xRuntime/ps2EntryRunner"
  ls -lh "$BUILD/ps2xRuntime/ps2EntryRunner" | awk '{print "    size: "$5}'
  stage_t
fi

command -v ccache >/dev/null && ccache -s 2>/dev/null | grep -iE 'hits|misses' | head -3 || true

if [ "$RUN" = 1 ]; then
  b "Launching"
  cd "$WORK" && "$BUILD/ps2xRuntime/ps2EntryRunner" "$ELF" 2>&1 | tee "$WORK/run.log"
fi

b "Done in $((SECONDS-t0))s"
