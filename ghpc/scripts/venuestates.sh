#!/usr/bin/env bash
# Save one song state per venue, through quickplay.
#
#   ghpc/scripts/venuestates.sh [venue ...]    default: all eight
#
#   AT=12     song seconds to save at
#   CAP=600   seconds before a venue is given up on
#   NATIVE=1  capture under --native: about 40s a venue against about 7 min,
#             but the state only resumes under --native (VU1 never ran)
#
# Quickplay plays a song in its campaign tier's venue, so the first song of
# tier k is k*5 presses down the list (config/gen/campaign.dtb). Writes
# work/states/venue_<name>.ghpcstate, or venue_<name>_native with NATIVE=1.
# unlock_all is forced on so every tier lists.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
at="${AT:-12}"
cap="${CAP:-600}"
venues=(battle small1 small2 big theatre fest arena stone)
[ $# -gt 0 ] && want=("$@") || want=("${venues[@]}")

if pgrep -x ps2EntryRunner >/dev/null 2>&1; then
  echo "a ps2EntryRunner is already running; kill it first" >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
awk '!/^unlock_all/ {print} /^\[cheats\]/ {print "unlock_all = 1"; done = 1}
     END {if (!done) print "[cheats]\nunlock_all = 1"}' "$root/work/ghpc.ini" >"$tmp/ghpc.ini"

for name in "${want[@]}"; do
  k=-1
  for i in "${!venues[@]}"; do [ "${venues[$i]}" = "$name" ] && k=$i; done
  [ "$k" -ge 0 ] || { echo "unknown venue $name" >&2; continue; }
  song="cross"
  for ((n = 0; n < k * 5; n++)); do song="down+$song"; done
  slot="venue_$name"
  mode=()
  if [ "${NATIVE:-0}" = 1 ]; then slot="${slot}_native"; mode=(--native); fi
  state="$root/work/states/$slot.ghpcstate"
  rm -f "$state"
  log="$root/work/logs/$slot.log"
  t0=$(date +%s)
  ( env GHPC_CONFIG="$tmp/ghpc.ini" GHPC_STATE_SAVE="$slot" GHPC_STATE_SAVE_AT="$at" \
        GHPC_PAD_DRIVE="main_screen=cross,qp_selsong_screen=$song,qp_diff_screen=cross,*=none" \
        GHPC_PAD_DRIVE_FOCUSLOG=1 \
        GHPC_HIDE_WINDOW="${GHPC_HIDE_WINDOW:-1}" GHPC_AUDIO="${GHPC_AUDIO:-0}" \
        "$here/play.sh" ${mode[@]+"${mode[@]}"} --log "$log" >/dev/null 2>&1 & )
  while [ ! -f "$state" ] && [ $(( $(date +%s) - t0 )) -lt "$cap" ]; do sleep 2; done
  sleep 3
  pkill -x ps2EntryRunner
  sleep 2
  if [ -f "$state" ]; then
    echo "$name: saved in $(( $(date +%s) - t0 ))s"
  else
    echo "$name: FAILED, no state after ${cap}s, see $log"
  fi
done
