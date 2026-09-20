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
