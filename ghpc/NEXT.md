# Start here

**This file is the standing handoff.** Whatever state the port is in, this is
what the next session is handed. Read it in full, then `ghpc/BACKLOG.md` for the
queue. Topic notes under `ghpc/notes/` supersede both on their own subject.

Saying "go" means: pick up the `Now` item in `ghpc/BACKLOG.md` and work it under
the rules below.

## Where things stand

Gameplay is reachable, renders correctly, and is far too slow to play.

    menus       ~100% of realtime
    gameplay      4.9% of realtime, 2.98 fps
    audio       none
    picture     correct (notes/evidence/2026-09-11-gameplay-frame-59.png)

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

**Save states are parked, deliberately, and start R0 instead.** They are half
built: the container and the quiescent-frame gate are in and measured, the
remaining chunks and the acceptance gate are in `BACKLOG.md` under Later.

Parked because R0 and R1 do not need them. R0 presents a cleared frame and R1
checks blit parity on the main menu at t=6; neither cares that gameplay is 25s
away. R2 is the first phase that wants fast gameplay iteration, and even there
`fixreplay.sh` already covers the risky part (is the skinned transform right)
at about a second. What is left to build is also the hard part: hand-written
serialisation of the scheduler tables, VU and GS registers, `PS2Memory` state
and IOP state, then a determinism gate whose failure mode is an open-ended
hunt for whatever went uncaptured.

Pick them up at the start of R2 if the drive is actually hurting by then.
Never load a state that has not passed the determinism gate: one that loads
and diverges later is worse than no state at all.

### R0: Vulkan bring-up
raylib is OpenGL-only and owns the window today (`ps2_runtime.cpp`,
`ps2_debug_panel.cpp`, behind the `ps2_host_backend` INTERFACE target). Vulkan
cannot share that window, so the renderer needs its own windowing. Use SDL3.

Stand up SDL3 window, Vulkan instance, device, swapchain, and present a cleared
frame. Selected by `GHPC_RENDERER=vulkan`, default raylib, both paths building.

**Exit:** a Vulkan window presents, and the game still boots unchanged on the
raylib path.

### R1: blit parity
Present the existing software GS framebuffer as a Vulkan texture. Vulkan now
owns the window; the picture must be identical because nothing about drawing
changed.

**Exit:** `play.sh --ingame` produces the same frame on both renderers, checked
by frame signature, not by eye.

### R2: the Rnd seam
Stub `PsRnd::FlushPacket` 0x43e400 so no packet reaches DMA, keeping its
close/swap/reopen bookkeeping and dropping only the MFIFO spin and
`DmaPacket::Send`. Own `PsMesh::DrawShowing` 0x43f038 for geometry and the
three `Select` hooks already written for material, texture and camera. Upload
to GPU buffers, do the skinned transform in a vertex shader.

Everything this needs is already measured and in `notes/rnd-seam.md`: the host
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

- **The oracle needs replacing.** `progress.py` scores `eerate_pct` at the top
  rung, which was right for the VU1 phase and is wrong now. A playability gate
  needs three numbers: frame rate on `game_screen`, audio chunks flowing, and
  the song tick advancing against wall clock. `checkrun.sh` is where that goes.
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
