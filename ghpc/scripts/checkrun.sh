#!/bin/bash
# Regression gate. Asserts two things after a timed headless run:
#   1. boot reached the loading screen (a [gate] line was emitted at all)
#   2. the presented picture is not alternating between two stable frames
# Signature alternation is measured within a single run. Hashes are not stable
# across runs, so never compare them between runs.
#
# Boot hangs intermittently before reaching the loading screen (roughly 2 in 3
# runs at the time this gate was written), so a single attempt cannot tell a
# hang apart from a real regression. Up to 3 attempts are made; an attempt
# that hangs (no [gate] line) is retried, and only failure of all 3 attempts
# is reported as "did not reach the loading screen". The first attempt that
# does produce a [gate] line is asserted on immediately, no further retries.
set -u
SECS="${1:-250}"
LOG="${2:-/tmp/ghpc_checkrun.log}"
PAT='build-debug/ps2xRuntime/ps2EntryRunner .*\.elf'
MAX_ATTEMPTS=3

cd "$(dirname "$0")/../.."

line=""
attempt=0
while [ "$attempt" -lt "$MAX_ATTEMPTS" ]; do
  attempt=$((attempt + 1))

  pkill -9 -f "$PAT" 2>/dev/null
  sleep 1
  ( cd work && GHPC_DIAG=1 GHPC_PRESENT_MIN=150000 \
      ../ghpc/scripts/run.sh --quiet --debug > "$LOG" 2>&1 ) &
  RUNNER=$!
  sleep "$SECS" &
  SLEEPER=$!
  ( wait $SLEEPER 2>/dev/null; pkill -9 -f "$PAT" 2>/dev/null; kill -9 $RUNNER 2>/dev/null ) &
  WATCHDOG=$!
  wait $RUNNER 2>/dev/null
  kill $WATCHDOG 2>/dev/null
  kill -9 $SLEEPER 2>/dev/null
  pkill -9 -f "$PAT" 2>/dev/null

  line=$(grep '^\[gate\]' "$LOG" | tail -1)
  if [ -n "$line" ]; then
    break
  fi
  echo "attempt $attempt/$MAX_ATTEMPTS: no [gate] line (hang). retrying." >&2
done

echo "attempts used: $attempt/$MAX_ATTEMPTS"

if [ -z "$line" ]; then
  echo "FAIL: no [gate] line after $attempt attempts. Boot did not reach the loading screen."
  echo "      log lines: $(wc -l < "$LOG")."
  exit 1
fi

alt=$(echo "$line" | grep -oE 'alternating=[01]' | cut -d= -f2)
distinct=$(echo "$line" | grep -oE 'distinct=[0-9]+' | cut -d= -f2)
echo "gate: $line"

if [ "${alt:-1}" = "1" ]; then
  echo "FAIL: presented picture alternates between $distinct stable frames (flicker)."
  exit 1
fi

echo "gate OK"
