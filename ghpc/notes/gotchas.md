# Gotchas

Things that look like bugs and are not, and tooling that is easy to misread.
Each one cost someone time at least once. `CLAUDE.md` points here rather than
carrying them, because none of them is needed until the moment it is.

## Symptoms that are not faults

**Magenta framebuffer** is the "nothing was drawn here" sentinel, not a
rendering fault. The software GS raises it from `GenImageColor(..., MAGENTA)`
and the Vulkan renderer clears to the same colour when no frame has reached
`uploadFrame` yet. Black would be indistinguishable from a window that was
created and never presented, which is why the sentinel is not black.

**`eerate` is not game speed.** It measures how fast emulated cycles retire.
What a player feels is the rate TaskMgr's clocks advance, which `[ghpc/clock]`
reports as `uirate`. The two differed by exactly 2x until 2026-09-20, when COP0
Count was found ticking every other EE cycle
(`evidence/2026-09-20-cop0-count-halved-game-time.txt`). Gate on `uirate`.

**A speed number without a screen is meaningless.** `[fps]` and `[eerate]`
carry `screen=` for this reason. `progress.py` once reported the loading
screen's 20.9% as the gameplay figure, because "last value seen" cannot tell
them apart.

**`[ghpc/clock] game=0.000` on a menu is a real zero.** TaskMgr has four
clocks and which one carries the time depends on the screen: menus run on
`ui`, the chart on `game` and `beat`. Watching one clock reports a stopped
clock for half the run.

## Tooling

**Logging.** `PS2X_ENABLE_RUNTIME_LOGS=ON`, or `PS2X_ENABLE_AGRESSIVE_LOGS=ON`
for a per-function call trace using symbol names. The latter covers 12,664
functions.

**Debug UI.** The runtime ships an ImGui panel with IOP/SIF, RPC History and
File/CD tabs exposing IOP and file state. It is on the raylib path only. The
Vulkan path has no panel and no pad input, because both are raylib's and
`IsWindowReady()` is false when SDL3 owns the window.

**Probes are diag-only.** The `GHPCSONG`, `GHPCGAME`, `GHPCSTRM` and friends in
`dispatchGuestBranch` sit inside `#if GHPC_DIAG`, so they do not exist in the
release build. Speed numbers have to come from release, so a reporter that only
exists in diag cannot be part of a speed gate. The `[ghpc/clock]`, `[eerate]`,
`[fps]` and `[drive]` reporters are unconditional for exactly that reason.

**Deliberately wrong machines.** `GHPC_VU1_OFF` draws no geometry and
`GHPC_VBLANK_SCALE` changes the vblank rate. Both are measurement rigs, never
builds. Prove a knob does what it says before trusting a number it produced: a
profile showing the subsystem gone is the check, not the number the knob
prints.

**Screenshots pair on `serial`, never on shot index or present frame.** This
one cost most of a session and produced two confident and opposite wrong
conclusions before it was found, so it is worth reading in full.

`[ghpc/vk/shot]` carries `frame=` (the host present counter) and `serial=`
(the guest frame the scene came from). Only `serial` pairs two runs. Measured
2026-09-20, two arms of one batch at the same present frames sat at guest
serials 653, 764, 859, 1015 and 857, 1091, 1121, 1150.

The ratio is what makes it so sharp. Measured on the same build: shots 40
present frames apart came back at serials 1075, 1076, 1077, 1078, so about 40
presents per guest frame on `game_screen`. The present loop runs free while
the guest advances at gameplay speed, so a 40-present shot interval steps the
chart by one frame and any phase difference between runs lands on a different
one. The ratio is not stable within a run either, since it tracks load.

Why it is so easy to be fooled: a coverage comparison against the software
path moves about 0.2 iou over 240 present frames as the chart advances. That
is far larger than anything a render change does, so mispaired shots produce a
large, smooth, entirely convincing difference. Two arms differing only in a
depth knob "separated cleanly" into a 0.78 band and a 0.63 band, held that
separation across seven pairings, and the separation was the pairing error.
Arms whose shot sequences happened to align matched to four decimal places,
which looked like proof of reproducibility and was proof of nothing.

So: pair on `serial`. If the serials do not match, the comparison is not
evidence, whatever it shows. And prefer a log that states what the code did
over an outcome number that implies it: the depth question above was settled
in one 60s run by `[ghpc/vk/pipeline]`, which prints the compare op and the
write flag for every pipeline created, after coverage had "answered" it twice
in opposite directions.

## Environment

**Toolchain.** cmake, ninja, ccache, pkg-config, llvm (`llvm-readelf`,
`llvm-objdump`), chdman, 7z. The Vulkan renderer also needs sdl3 and the
Vulkan loader.

**Hosts.** Builds on arm64 macOS with no upstream changes. Linux is required
for the decomp toolchain (32-bit GCC, i386 multiarch). The fallback host is
Bazzite, and moving is a decision to make when something actually blocks, not
pre-emptively.

**Disk.** The ARK is 3.1 GB; reference clones reached 9 GB. `third_party/`
clones are all re-clonable.

**CHD format.** `chdman info` reports 2448-byte units, so `extractdvd` produces
raw sectors rather than an ISO. Convert by taking the first 2048 bytes of each
2448-byte unit. `CD001` then lands at 0x8000.

**Vulkan on macOS.** `brew install molten-vk vulkan-loader vulkan-headers
vulkan-validationlayers glslang`, plus `sdl3`. The loader finds MoltenVK with no
env setup. `GHPC_VK_VALIDATION=1` enables the layer, `GHPC_RENDERER=vulkan`
selects the backend, and `PS2X_ENABLE_VULKAN=OFF` drops it from the build.
