# Start here

**This file is the standing handoff.** Whatever state the port is in, this is
what the next session is handed. Read it in full, then `ghpc/BACKLOG.md` for the
queue. Topic notes under `ghpc/notes/` supersede both on their own subject.

Saying "go" means: pick up the `Now` item in `ghpc/BACKLOG.md` and work it under
the rules below.

## Where things stand

Gameplay is reachable, renders correctly, and is far too slow to play.

**`eerate` is not game speed, and the difference is not small.** It measures how
fast emulated cycles retire. What a player feels is the rate TaskMgr's own
clocks advance, which `[ghpc/clock]` reads straight out of guest RAM and
reports as `uirate`. They agree only when the cycle-to-seconds conversion is
right, and until 2026-09-20 it was not: COP0 Count ticked every other EE cycle
and halved every clock the game derives from it
(`notes/evidence/2026-09-20-cop0-count-halved-game-time.txt`).

    menus        43% of realtime, game speed 0.43   was 0.21 before the fix
    gameplay     19% with the rasteriser throttled, game speed 0.19
    audio        none
    picture      correct (notes/evidence/2026-09-11-gameplay-frame-59.png)

The old "menus ~100% of realtime" line in this file was `eerate` on the loading
screen. The main menu has never run at full speed, and it is capped by the same
software rasteriser as gameplay: a menu profile puts ~409 samples in
`GSCpuBackend` against ~23 in vblank handling. So R2 and R3 buy back the menus
as well as gameplay, which nothing here had assumed.

**The ceiling is measured, not estimated.** 2026-09-19, release, one binary,
300s per arm, 60s hold, `GHPC_VU1_OFF=1`:

    control     VU1 emulated as normal     eerate   4.9    fps  2.98
    VU1 off     VU1 never runs, no geometry eerate 100.1    fps 59.15

The VU1-off arm also ran a song end to end and reached `lose_screen`, which no
arm had managed inside the run cap. So VU1 interpretation is effectively the
entire gameplay cost, and the recompiled EE code holds full realtime on its
own. Detail in `notes/evidence/2026-09-19-vu1-off-confirms-the-seam.txt`.

`GHPC_VU1_OFF` draws nothing. It is a ceiling measurement, never a build.

## The target, decided 2026-09-20

**A native Vulkan renderer at the `Rnd` seam.** Vulkan everywhere: native on
Linux and Windows, through MoltenVK on macOS. Development stays on macOS while
it can; the fallback host is Bazzite, and moving is a decision to make when
something actually blocks, not pre-emptively.

Why the `Rnd` seam rather than anything lower: it deletes VU1, VIF1, GIF, MFIFO
and the software rasteriser in one move, which is the whole 20x. Lower seams do
not. Round 20 proved a per-MSCAL seam cannot work at all, and the reason
generalises: **a selective skip leaves a microprogram running on state its
skipped predecessor should have written.** Skip everything or skip nothing.

Static recompilation of the 18 VU1 microprograms is a **contingency, not the
plan**. It only matters if programs survive the seam (particles, effects) and
still cost. `.vutext` is already extracted and disassembled if it comes to that.

## The phases

One exit criterion each. Do not start the next before it holds.

**Save states work, with one gap that matters.** Capture and resume both run:

    GHPC_STATE_SAVE=song GHPC_STATE_SAVE_AT=2.0   capture once, unattended
    GHPC_STATE_LOAD=song                          resume straight into the song

A resume restores the chart exactly. `game`, `ui` and `beat` come back at the
capture point and advance at the same rate as a driven run, +0.781 beat per
sample against the driven arm's +0.781. The state is 132 MB, almost all of it
the 128 MB devkit RDRAM. The save trigger keys on the song clock rather than
wall time, so it lands at the same chart position every run; both captures
fired at `song=2.016`.

Chunks: RDRM, SPAD, VRAM, VUMM, EECX, SCHD (kernel tables), MEMS (IO
registers, TLB, EE timers, GS registers, VIF1 residual), VUST (VU register
files), PADS (pad ports).

**The gap: the host mesh cache is not carried, so a resumed run has no
geometry.** `GHPC_RND_SCENE=1` reports `draws=0` resumed against 504 driven.
The cache fills at `PsMesh::Sync`, which ran before the save. Until it is
serialised, a save state cannot be used to iterate on R2, which is the work it
was built for.

That is the same class as the pad bug this found: host state the guest's RDRAM
implies but which lives in C++ and was never written. The pad announced itself
by pausing the game. A cache does not announce anything, it just goes quiet.
Ask it of every host-side cache before trusting a state.

**There is still no determinism gate.** Matching clocks and a matching beat
rate are encouraging, not proof. Treat a loaded state as a fast way back to
roughly where you were, not as ground truth for a measurement, and do not
judge R2's rendering output from one.

### R0: Vulkan bring-up, done 2026-09-20
SDL3 window, Vulkan instance, device, swapchain, cleared frame, selected by
`GHPC_RENDERER=vulkan` with raylib the default. Exit met on one binary, both
arms, in `notes/evidence/2026-09-20-r0-vulkan-bringup.txt`: MoltenVK on an M4
Pro, a 3-image B8G8R8A8_UNORM swapchain, 771 frames presented over a 15s hold,
and the raylib arm still reaching the main menu.

The seam is `ghpc::HostRenderer` in `include/ghpc_host_renderer.h`, implemented
by `src/lib/ghpc_vulkan_renderer.cpp`. `PS2Runtime::m_hostRenderer` is null on
the raylib path and non-null when SDL3 owns the window, in which case there is
no raylib window at all: no audio device, no debug panel, and no pad input,
which the pad stubs already gate on `IsWindowReady()`.

The clear is magenta, matching the software GS sentinel. Black is
indistinguishable from a layer that was created and never presented, and the
first run of this arm cleared to black and proved nothing.

Build deps on macOS: `brew install molten-vk vulkan-loader vulkan-headers
vulkan-validationlayers glslang`, plus `sdl3`. CMake takes an installed SDL3
and falls back to FetchContent. `PS2X_ENABLE_VULKAN=OFF` drops the backend.
`GHPC_VK_VALIDATION=1` turns the layer on.

### R1: blit parity, done 2026-09-20
The software GS framebuffer presents through Vulkan, verified by reading the
swapchain back and comparing it to the bytes handed to `uploadFrame` on a 1:1
blit: 10 checks, 0 mismatched pixels out of 229k to 287k, 0 validation errors,
and no measurable speed cost against raylib (43.3% vs 43.5% eerate, same
binary). `GHPC_VK_PARITY=N` runs the check every Nth frame.

The planned gate, the same frame signature on both renderers, is not
measurable and was replaced deliberately. The signature in
`gs_cpu_backend.cpp:1211` hashes the GS source buffer before either renderer
touches it, so it is identical on both arms by construction, and this file
already says hashes are not stable across runs. Neither form of that
comparison can fail. The readback can, and it covers the swizzle, the filter
and the rect, which a source-side hash never could.

### R2: the Rnd seam, inputs in, pipeline not started
The seam collects. `GHPC_RND_SCENE=1` gathers every draw and draws nothing,
and on `game_screen` it reports about 500 draws, 42k verts and 42k triangles
per frame with a camera and a transform on every one
(`notes/evidence/2026-09-20-r2-seam-inputs-arrive.txt`). Contract in
`include/ghpc_scene.h`.

What is left is the Vulkan mesh pipeline itself, sequenced in `BACKLOG.md`:
GPU buffers keyed by mesh, a vertex shader doing the measured transform, a
depth buffer and a render pass, then material and texture, then stubbing the
packet path last.

Two things that will cost a day each if taken on trust. `g_ghpcInstWorld` is
a probe and not a seam input, by its own file's account: it names a different
mesh chain than the DrawFaces it precedes, so the object matrix comes from the
packet scan in `ghpcSceneSubmit` instead. And a blunt `FlushPacket` stub
wedges the guest, which `ghpc/override/FlushPacket__5PsRnd_0x43e400.cpp`
records in full.

Everything else this needs is measured and in `notes/rnd-seam.md`: the host
mesh cache at 99% hit rate, the four backend inputs, and the vertex transform
verified to GS subpixel over 171 matched pairs.

**Exit:** venue and fretboard render through Vulkan above 30 fps with the
gameplay chain alive (`BeatMatch::Poll` 0x1259c0, `PlayerMatcher::Poll`
0x117dd0) and `song_tick_advanced=yes`.

### R3: coverage
Whatever the native path does not cover yet (2D UI, particles, movie) keeps
going through the emulated GS into the same frame. Ace Combat 5's static
recompilation does exactly this and it is the reason its port shipped
incrementally rather than all at once.

**Exit:** no visual regression against the software path, at playable speed.

### A1: audio, in parallel
Independent of rendering and it gates playability just as hard, because note
timing is slaved to audio position. The protocol is solved and the ack is in;
it is not yet sufficient. Two named candidates in `notes/audio-path.md`, both
distinguishable, neither concluded. Do that next, not a rewrite.

## What to build against

- **`gh2-decomp`** (sibling repo, `../gh2-decomp`) is the interface contract.
  39% byte-matched, 4,922 of 12,663 functions. `src/rndobj` has the
  platform-neutral `Rnd`, `Mat`, `Cam`, `Tex`, `Mesh`, `Environ`,
  `LightPreset`, `Striper`. The `Ps*` files are thin, which is correct: a
  native backend implements the neutral interface, not the PS2 one. Reference
  clone, never a drop-in.
- **`ico-recomp`** (https://github.com/nathanialf/ico-recomp), MIT, is the
  worked example: a PS2 static recompilation with a clean-room GS renderer
  under `src/runtime/gs/render` behind an RHI, Vulkan and D3D12. Their
  clean-room path has not passed its own parity gate and defaults to
  paraLLEl-GS, so read it for structure rather than trusting its output.
- **paraLLEl-GS** (LGPL-3.0-or-later, Vulkan compute) is the fallback if
  writing a rasteriser stops being worth it. Note the licence before linking.

## Fast loops, use them instead of game runs

    ./ghpc/scripts/play.sh                 main menu at t=6.0
    ./ghpc/scripts/play.sh --ingame        in a song at t=25.3, then hands off
    ./ghpc/scripts/fixreplay.sh <fixture>  ~1s, replays captured draws against
                                           VU1's own output

Reporters, all release-safe and all tagged with the screen they were measured
on, because a number off the loading screen is not a gameplay number:

    GHPC_CLOCKRATE=N   [ghpc/clock] game time against wall time, every N vblanks
    GHPC_EERATE=N      [eerate] emulated cycles against wall time
    GHPC_FPS=N         [fps] presented frames

`[ghpc/clock]` prints all four TaskMgr clocks. Which one carries the time
depends on the screen: menus run on `ui`, the chart on `game` and `beat`, and
`game` reads a real zero outside gameplay. A gate watching one clock would call
that a stopped clock.

    GHPC_VBLANK_SCALE=N  multiplies the vblank period. Diagnostic only: any
                         value but 1 is a deliberately wrong machine. It is the
                         only way to separate cost that scales with frames from
                         cost that scales with cycles.

Capture a fixture once, then iterate on it:

    GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=/tmp/gh2.fix GHPC_COUNTIN=0.5 \
      python3 ghpc/scripts/progress.py --build build

There is no runtime state snapshot. Boot skip plus the pad driver is the fast
path and it is good enough; building a real one is days of work for something
the 25s loop already covers.

## Rules that hold every session

- **Measurement before theory.** This project has repeatedly lost time
  reasoning from a symptom the runtime manufactured.
- **Pre-declare the metric** before building or running. Say which outcome the
  change makes impossible. If the answer is "the failing one", the metric is
  wrong.
- **A control arm on the same binary.** A verdict against a stored mark is not
  a control; the mark came from a different build at a different time. Confirm
  both arms reached the same screen before comparing them.
- **Prove a knob does what it says.** `GHPC_VU1_OFF` first gated only the MSCAL
  callback while `setVu1MscntCallback` kept resuming VU1, and it measured 0.8
  instead of 100.1. A profile showing the subsystem gone is the check, not the
  number the knob produces.
- **Three outcomes, never two.** PROGRESSED, SAME, REGRESSED, and
  MEASUREMENT_FAILED, which is never allowed to collapse into SAME.
- **Speed numbers come from the release build.** Debug runs 2.5 to 4x slower.
- **Check for a stray runner** with `pgrep -x ps2EntryRunner` before any timed
  run. Use `-x`, never `-f`.
- **If three fixes on one hypothesis have failed**, it is an architecture
  problem. Write down what each ruled out and change target.
- **Never claim done without evidence.** A render, a log line, a verdict.

## Known defects in the tooling

- **The oracle is half replaced.** Two of the three numbers a playability gate
  needs are in and release-safe: game time against wall clock (`[ghpc/clock]`,
  and it is what found the COP0 bug), and frame rate attributable to a screen,
  since `[fps]` and `[eerate]` now carry `screen=`. Audio chunks flowing is
  still missing, and nothing consumes any of it yet: `checkrun.sh` has no gate
  and `progress.py` still scores `eerate_pct` at the top rung, which is the
  metric this file just spent a section explaining is not game speed. Wiring
  those two up is the remaining work.
  This file no longer carries the generated `PROGRESS` block. That is
  deliberate: it reported a saturated rung and a metric that stopped
  describing the goal. `progress.py --check` now returns 0 on a missing block
  so the pre-commit hook does not fire; `--render` still exits 1, since there
  is nothing to render into. Rebuild the block around the new gate rather than
  restoring the markers.
- **The control arm can fail to produce a number.** Two release control runs
  ended on `game_screen` and emitted no `[eerate]` line at all. At 2.98 fps a
  run can hold the screen for the whole window without the reporter's interval
  landing on it. Fix before the next speed claim.
- **`fps` is noisy.** Identical release runs have reported 0.56 and 2.95 at the
  same `eerate_pct`.
- **The round loop is retired.** `rounds_total` and `rounds_since_gain` in
  `progress.py` counted an unattended loop that no longer runs. They are
  vestigial; ignore them rather than feeding them.
