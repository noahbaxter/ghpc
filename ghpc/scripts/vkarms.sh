#!/usr/bin/env bash
# Capture beat-paired Vulkan arms from one save state, then compare them.
#
#   ghpc/scripts/vkarms.sh <outdir> <name>:<ENV=V,ENV=V> [<name>:<envs>...]
#
#   SLOT=songmesh   the state every arm resumes from (GHPC_STATE_LOAD)
#   SHOTS=4         beat buckets per arm; an arm stops when it has them
#   CAP=240         seconds before an arm is given up on
#
# The first arm is the reference the others are compared against, and one arm
# should repeat another exactly: that pair is the noise floor, and without it
# a difference means nothing. Capture the slot once with
#
#   GHPC_STATE_SAVE=songmesh GHPC_STATE_SAVE_AT=2.0 ./ghpc/scripts/play.sh --ingame
#
# Arms stop on shot count rather than a timer, because buckets are what get
# compared and wall time is not. From a state a bucket lands about every 13s;
# a driven arm spends its first 60s reaching the song.
#
# Do not resize the window during a run: shots from two swapchain sizes cannot
# be compared, and vkcompare.sh says so rather than guessing.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:?outdir}"
shift
slot="${SLOT:-songmesh}"
shots="${SHOTS:-4}"
cap="${CAP:-240}"

if pgrep -x ps2EntryRunner >/dev/null 2>&1; then
  echo "a ps2EntryRunner is already running; kill it first" >&2
  exit 1
fi
[ -f "$root/work/states/$slot.ghpcstate" ] || { echo "no state $slot in work/states" >&2; exit 1; }

dirs=()
for spec in "$@"; do
  name="${spec%%:*}"
  envs="${spec#*:}"
  dir="$out/$name"
  mkdir -p "$dir"
  rm -f /tmp/ghpc_vk_b*.ppm "$dir"/ghpc_vk_b*.ppm
  t0=$(date +%s)
  # shellcheck disable=SC2086
  ( env GHPC_STATE_LOAD="$slot" GHPC_RENDERER=vulkan GHPC_VK_SHOT_BEAT=1 \
        GHPC_VK_SHOT_SCREEN=game_screen GHPC_VK_SHOT_LIMIT=$((shots + 1)) ${envs//,/ } \
        "$here/play.sh" --log "$out/$name.log" >/dev/null 2>&1 & )
  # One more shot than asked for: the first fires on the load frame, before
  # anything has presented, and is dropped below.
  while [ "$(ls /tmp/ghpc_vk_b*.ppm 2>/dev/null | wc -l)" -lt $((shots + 1)) ] &&
        [ $(( $(date +%s) - t0 )) -lt "$cap" ]; do
    sleep 1
  done
  sleep 1
  pkill -x ps2EntryRunner
  sleep 2
  first="$(ls /tmp/ghpc_vk_b*.ppm 2>/dev/null | head -1)"
  [ -n "$first" ] && rm -f "$first"
  mv /tmp/ghpc_vk_b*.ppm "$dir"/ 2>/dev/null
  echo "$name: $(ls "$dir" | wc -l | tr -d ' ') shots in $(( $(date +%s) - t0 ))s"
  dirs+=("$dir")
done

[ "${#dirs[@]}" -ge 2 ] && "$here/vkcompare.sh" "${dirs[@]}"
