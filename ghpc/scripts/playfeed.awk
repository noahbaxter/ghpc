# The console view of a play.sh run. The full output goes to the log; this
# keeps what a player acts on: what came up, which screen the game is on, a
# frame rate dip, and anything that looks like a failure.
#
# Reads the runtime's stderr. [fps] lines (GHPC_FPS) carry the screen and
# drive the screen and dip reports; every other line is matched by tag.

function say(tag, msg) { printf "  %-10s %s\n", tag, msg; fflush() }

# [ghpc/input] using "raphnet ... v2.2": 16 buttons, 6 axes, 0 hats
/^\[ghpc\/input\] using / {
  split($0, q, "\""); say("controller", q[2]); next
}
/^\[ghpc\/input\] no joystick matches/ { sub(/^\[ghpc\/input\] /, ""); say("controller", $0); next }
/^\[ghpc\/config\] loaded / { say("config", $3); next }
# [ghpc/vk] device=Apple M4 Pro api=1.1.357 queueFamily=0
/^\[ghpc\/vk\] device=/ {
  d = $0; sub(/^\[ghpc\/vk\] device=/, "", d); sub(/ api=.*/, "", d); say("gpu", d); next
}
# [ghpc/vk] title: PS2-Recomp | GH2_debug.elf  [Vulkan: native, VU1 off]
/^\[ghpc\/vk\] title: / {
  r = $0; sub(/.*\[/, "", r); sub(/\].*/, "", r); say("renderer", r); next
}
# Held until the first screen: the window opens at 4:3 a moment before the
# widescreen default applies, and only where it settles is worth a line.
/^\[ghpc\/display\] / {
  d = $0; sub(/^\[ghpc\/display\] /, "", d); sub(/ \(mAspect=.*/, "", d)
  if (d != display) { display = d; if (screen != "") say("display", d) }
  next
}
/^\[ghpc\/audio\] (SDL3 output|no output device|off)/ { sub(/^\[ghpc\/audio\] /, ""); say("audio", $0); next }
/^\[ghpc\/state\] (loaded|saved|wrote)/ { sub(/^\[ghpc\/state\] /, ""); say("state", $0); next }

# A screen's frame rate, once, when it is left: the average and the worst
# second. Load screens are slow on purpose and are left out.
function screenSummary() {
  if (screen != "" && screen !~ /load/ && secs > 0)
    say("fps", sprintf("%-18s avg %.0f  min %.0f  over %ds", screen, total / secs, worst, secs))
  total = 0; secs = 0; worst = 1000; low = 0
}
END { screenSummary() }

# [fps] 59.94 frames/sec  16.68 ms/frame  over 60 frames  swFrags/frame=0  screen=main_screen
/^\[fps\] / {
  fps = $2 + 0
  s = $0; sub(/.*screen=/, "", s); sub(/ .*/, "", s)
  if (s == "none") s = ""
  # Nothing before the first screen: that is window and device bring-up.
  if (s == "") next
  if (s != screen) {
    screenSummary()
    if (screen == "" && display != "") say("display", display)
    screen = s; say("screen", s)
  }
  total += fps; secs++; if (fps < worst) worst = fps
  # Only a sustained drop is worth a line as it happens: three seconds
  # running under 45. A one-second dip into the 50s is sampling noise.
  if (s !~ /load/) {
    if (fps < 45) { if (++low == 3) say("slow", sprintf("under 45 fps for 3s on %s (%.0f)", s, fps)) }
    else low = 0
  }
  next
}

# Anything that reads as a failure, matched on a whole word so counters like
# texFailed=0 stay out.
{
  l = tolower($0)
  if (l ~ /(^|[^a-z])(error|errors|failed|fatal|exhausted|stuck|abort|aborted|assert|panic)([^a-z]|$)/)
    say("!", $0)
}
