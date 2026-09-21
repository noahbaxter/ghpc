#!/usr/bin/env bash
# Coverage of a native arm against a software reference, paired by beat.
#
#   ghpc/scripts/vkcompare.sh <reference-dir> <arm-dir> [<arm-dir>...]
#
# Capture both arms with GHPC_VK_SHOT_BEAT=N. That names each shot after its
# beat bucket, so two runs write the same filenames for the same chart
# positions and pairing is a filename join. Nothing else pairs: shot index and
# the host present counter are not chart positions, and about 40 presents pass
# per guest frame on game_screen, so anything sampled on the present loop
# drifts against the chart. notes/gotchas.md, "Screenshots pair on serial",
# has what that cost.
#
# Include an arm identical to the one under test. Coverage moves as the chart
# advances and the masks are a brightness threshold, so the identical arm is
# what says how much of a difference is just run to run.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
ref="${1:?reference dir}"
shift

for arm in "$@"; do
  echo "=============== $(basename "$arm") vs $(basename "$ref")"
  shot_count=0
  for refshot in "$ref"/ghpc_vk_b*.ppm; do
    [ -e "$refshot" ] || continue
    name="$(basename "$refshot")"
    # Same bucket, same screen, so the same filename.
    cur="$arm/$name"
    [ -f "$cur" ] || continue
    out="$(python3 "$here/vkcoverage.py" "$refshot" "$cur")"
    bucket="$(echo "$name" | sed -E 's/^ghpc_vk_b0*([0-9]+)_.*/\1/')"
    printf "  beatBucket=%-6s %s\n" "$bucket" \
      "$(echo "$out" | awk '/^iou/{i=$2} /^swOnly/{s=$2} /^nvOnly/{n=$2} END{printf "iou=%-8s swOnly=%-8s nvOnly=%s", i, s, n}')"
    shot_count=$((shot_count + 1))
  done
  if [ "$shot_count" = 0 ]; then
    echo "  no shared beat buckets. Were both arms captured with GHPC_VK_SHOT_BEAT?"
  fi
done
