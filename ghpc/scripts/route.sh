#!/usr/bin/env bash
# Drive the career loop under --native and summarise what it drew.
#
#   ghpc/scripts/route.sh <outdir> [<name>:<ENV=V,ENV=V> ...]
#
#   DUR=85      seconds per arm
#   SHOT=120    present frames between shots, about 2s at 60 fps
#   DRIVE=...   override the pad route below
#
# The route is main, career, expert, a song mashed on strum until it fails,
# quit, and a second song. Each arm lands in <outdir>/<name>/ with its log,
# the clock and present traces, and its shots, then routesum.py prints one
# line per arm. With no arm given it runs one arm, "a", with no extra env.
# Include an arm identical to the one under test: two identical runs do not
# draw identical frames, and a difference has to clear that.
#
#   ghpc/scripts/route.sh /tmp/route ref: again: fix:GHPC_SCENE_QUEUE=1
set -u

here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?outdir}"
shift
[ $# -gt 0 ] || set -- "a:"
dur="${DUR:-85}"
shot="${SHOT:-120}"
drive="${DRIVE:-main_screen=@main_career.btn+r2,nameprof_screen=r2+start,sel_difficulty_screen=@sd_diff4.btn+r2,game_screen=down,lose_screen=@lose_quit_career.btn+r2,*=r2}"

if pgrep -x ps2EntryRunner >/dev/null 2>&1; then
  echo "a ps2EntryRunner is already running; kill it first" >&2
  exit 1
fi

# The trailing slash matters: /tmp is a symlink, and without it find never
# descends and shots from an earlier run get mixed into this one.
clean_shots() { find /tmp/ -maxdepth 1 -name 'ghpc_vk_*' -delete; }

dirs=()
for spec in "$@"; do
  name="${spec%%:*}"
  envs="${spec#*:}"
  dir="$out/$name"
  rm -rf "$dir"
  mkdir -p "$dir"
  clean_shots
  # shellcheck disable=SC2086
  ( env GHPC_PAD_DRIVE="$drive" GHPC_PAD_DRIVE_SETTLE=1.5 GHPC_PAD_DRIVE_PERIOD=0.2 \
        GHPC_VK_SHOT="$shot" GHPC_VK_SHOT_LIMIT=100000 \
        GHPC_CLOCKTRACE="$dir/clock.csv" GHPC_PRESENTTRACE="$dir/present.csv" \
        ${envs//,/ } \
        "$here/play.sh" --native --guitar --log "$dir/run.log" >/dev/null 2>&1 & )
  sleep "$dur"
  pkill -x ps2EntryRunner
  sleep 2
  find /tmp/ -maxdepth 1 -name 'ghpc_vk_*' -exec mv {} "$dir"/ \;
  dirs+=("$dir")
done

python3 "$here/routesum.py" "${dirs[@]}"
