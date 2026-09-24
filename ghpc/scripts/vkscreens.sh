#!/usr/bin/env bash
# Compare renderers on every saved menu, one to one.
#
#   ghpc/scripts/vkscreens.sh <outdir> <name>:<ENV=V,ENV=V> [<name>:<envs>...]
#
#   SLOTS="menu_main_screen ..."   default: every work/states/menu_*.ghpcstate
#   SHOTS=3    ui-clock buckets per arm and slot
#   STEP=1     seconds of the guest's ui clock between buckets
#   CAP=120    seconds before an arm is given up on
#
# Capture the slots once, on the emulated path so VU1's state in them is real:
#
#   GHPC_STATE_SAVE_SCREENS=1 GHPC_PAD_DRIVE_SETTLE=10 ghpc/scripts/play.sh --ingame
#
# or navigate by hand with GHPC_STATE_SAVE_SCREENS=1 set, and every screen that
# holds for GHPC_STATE_SAVE_SETTLE vblanks is saved as menu_<screen>.
#
# Every arm loads the same state with no pad input and is shot on the ui
# clock (GHPC_VK_SHOT_UI), so two arms running at different speeds are still
# shot at the same moments of each menu's animation. The first arm is the
# reference. The first DROP shots of each run (default 2) are thrown away:
# one fires on the load frame, and the software GS takes a frame or two to
# redraw after a load.
#
#   ghpc/scripts/vkscreens.sh /tmp/menus ref:GHPC_VK_MESH=0 native:GHPC_VK_MESH=2,GHPC_VU1_OFF=1
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:?outdir}"
shift
shots="${SHOTS:-3}"
# Leading shots thrown away: the load frame, and the software GS's first
# frames after a load, which are still being drawn.
drop="${DROP:-2}"
step="${STEP:-1}"
cap="${CAP:-120}"
slots="${SLOTS:-$(ls "$root"/work/states/menu_*.ghpcstate 2>/dev/null | xargs -n1 basename | sed 's/\.ghpcstate$//')}"

if pgrep -x ps2EntryRunner >/dev/null 2>&1; then
  echo "a ps2EntryRunner is already running; kill it first" >&2
  exit 1
fi
[ -n "$slots" ] || { echo "no menu_* states in work/states" >&2; exit 1; }

arms=()
for spec in "$@"; do
  name="${spec%%:*}"
  envs="${spec#*:}"
  arms+=("$name")
  for slot in $slots; do
    dir="$out/$name/$slot"
    mkdir -p "$dir"
    rm -f /tmp/ghpc_vk_u*.ppm /tmp/ghpc_vk_u*.txt "$dir"/ghpc_vk_u*
    t0=$(date +%s)
    # shellcheck disable=SC2086
    ( env GHPC_STATE_LOAD="$slot" GHPC_RENDERER=vulkan GHPC_VK_SHOT_UI="$step" \
          GHPC_VK_SHOT_LIMIT=$((shots + drop)) \
          GHPC_HIDE_WINDOW="${GHPC_HIDE_WINDOW:-1}" GHPC_AUDIO="${GHPC_AUDIO:-0}" ${envs//,/ } \
          "$here/play.sh" --log "$out/$name.$slot.log" >/dev/null 2>&1 & )
    while [ "$(ls /tmp/ghpc_vk_u*.ppm 2>/dev/null | wc -l)" -lt $((shots + drop)) ] &&
          [ $(( $(date +%s) - t0 )) -lt "$cap" ]; do
      sleep 1
    done
    sleep 1
    pkill -x ps2EntryRunner
    sleep 2
    for first in $(ls /tmp/ghpc_vk_u*.ppm 2>/dev/null | head -"$drop"); do
      rm -f "$first" "${first%.ppm}.txt"
    done
    # The .txt beside a shot is its draw table, under GHPC_VK_MESH_ID.
    mv /tmp/ghpc_vk_u*.ppm /tmp/ghpc_vk_u*.txt "$dir"/ 2>/dev/null
    echo "$name $slot: $(ls "$dir"/*.ppm 2>/dev/null | wc -l | tr -d ' ') shots in $(( $(date +%s) - t0 ))s"
  done
done

ref="${arms[0]}"
for arm in "${arms[@]:1}"; do
  echo "=============== $arm vs $ref"
  for slot in $slots; do
    for refshot in "$out/$ref/$slot"/*.ppm; do
      [ -e "$refshot" ] || continue
      shot="$out/$arm/$slot/$(basename "$refshot")"
      [ -f "$shot" ] || continue
      bucket="$(basename "$refshot" | sed -E 's/^ghpc_vk_u0*([0-9]+)_.*/\1/')"
      printf "  %-26s ui=%-5s %s\n" "$slot" "$bucket" \
        "$(python3 "$here/vkcoverage.py" "$refshot" "$shot" |
           awk '/^iou/{i=$2} /^swOnly/{s=$2} /^nvOnly/{n=$2} END{printf "iou=%-8s swOnly=%-8s nvOnly=%s", i, s, n}')"
    done
  done
done
