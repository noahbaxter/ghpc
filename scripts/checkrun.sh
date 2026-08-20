#!/bin/bash
# Run the debug build and assert the boot still progresses.
# tme/notme measure whether the game is rendering at all, independent of
# whatever probe is under investigation. A change here means a regression was
# introduced, not that the experiment worked.
set -u
SECS="${1:-50}"
LOG="${2:-/tmp/ghpc_checkrun.log}"
EXPECT_TME=17430
EXPECT_NOTME=2933

cd "$(dirname "$0")/.."
( ./scripts/run.sh --quiet --debug > "$LOG" 2>&1 & echo $! > /tmp/ghpc_checkrun.pid )
sleep "$SECS"
kill "$(cat /tmp/ghpc_checkrun.pid)" 2>/dev/null
pkill -f ps2EntryRunner 2>/dev/null

line=$(grep '\[gs/draw\] tme=' "$LOG" | tail -1)
tme=$(echo "$line"   | grep -oE '\] tme=[0-9]+'  | grep -oE '[0-9]+')
notme=$(echo "$line" | grep -oE 'notme=[0-9]+' | grep -oE '[0-9]+')

echo "baseline: tme=${tme:-none} notme=${notme:-none} (expect $EXPECT_TME/$EXPECT_NOTME)"
if [ "${tme:-0}" != "$EXPECT_TME" ] || [ "${notme:-0}" != "$EXPECT_NOTME" ]; then
  echo "BASELINE MOVED -- treat as a regression before trusting any other number in this run"
  exit 1
fi
echo "baseline OK"
