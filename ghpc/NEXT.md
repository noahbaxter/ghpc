# Start here

**This file is the standing handoff.** Whatever state the port is in, this is
what the next session is handed. Read it in full, then `ghpc/BACKLOG.md` for the
queue. Topic notes under `ghpc/notes/` supersede both on their own subject.

Saying "go" means: pick up the `Now` item in `ghpc/BACKLOG.md` and work it under
the rules below.

## Where things stand

**Gameplay runs at realtime through the native renderer:** `play.sh --native`,
60 fps, game rate 1.000, lit and textured, close to the software picture but
not identical to it (R2 below). The emulated path is still far too slow.
Songs, menu music and sample SFX play since 2026-09-21 (A1 below).

**As of 2026-09-21 the whole career loop runs under `--native`**, menus,
song, fail, quit and back, and holds game rate 0.98-1.00 under host load.
Flashing, wrong textures, sustains and the kick drum were fixed the same
day (scene frames close at `PsRnd::EndDrawing`; mutable meshes decode from
their DMA-chain face packet). On 2026-09-22 the gem tops (a `PsMultiMesh`)
and a second song's stale meshes were fixed; what `--native` still never
receives is listed in `BACKLOG.md` `Now`. The cyan wedge
that grows over the venue as a song fails is the game's own effect, not a
fault. `ghpc/scripts/route.sh` drives the whole loop in 85s and prints one
line per arm; judge picture changes on a zoomed crop, since `swOnly` drifts
by thousands between identical arms.

Hand play: `play.sh --native --guitar`. Keys 1-5 frets, arrows D-pad (up/down
strum), X/C/Z/V face buttons, Enter start. F5 saves the screen on show as
`menu_<screen>` (emulated path only; the title bar confirms). The pad driver
picks menu items by name: `@main_career.btn` presses down until that
component has focus, and `GHPC_PAD_DRIVE_FOCUSLOG=1` prints the names.

Three things that bit this round and will bite again:
- **Host caches keyed on a guest address go stale.** The game reuses the
  address once a menu unloads. Mesh caches now carry a generation
  (`meshcache::generation`); the texture cache still keys on pointers and
  dims, so suspect it first for a wrong texture after a menu change.
- **Game time is cycles.** A vblank waited on both cycles and wall time, so
  a loaded host played in slow motion and then fast-forwarded.
  `GHPC_WALL_PACE` (on under `--native`) hands a late vblank its cycles.
  `GHPC_CLOCKTRACE` and `GHPC_PRESENTTRACE` log per vblank and per present.
- **`PSPadBackend::readState` always succeeds**, which silently shadowed
  the pad stub's own mapping. The stub no longer consults it.

**`eerate` is not game speed, and the difference is not small.** It measures how
fast emulated cycles retire. What a player feels is the rate TaskMgr's own
clocks advance, which `[ghpc/clock]` reads straight out of guest RAM and
reports as `uirate`. They agree only when the cycle-to-seconds conversion is
right, and until 2026-09-20 it was not: COP0 Count ticked every other EE cycle
and halved every clock the game derives from it
(`notes/evidence/2026-09-20-cop0-count-halved-game-time.txt`).

    menus        43% of realtime, game speed 0.43   was 0.21 before the fix
    gameplay     19% emulated; 100% with --native (2026-09-21)
    audio        streams and samples, SDL3 out (2026-09-21)
    picture      correct on the emulated path; --native in R2

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

`GHPC_VU1_OFF` alone draws nothing. With the native path on (`--native`) it is
the fast path, since the seam replaces everything VU1 drew on `game_screen`.
Screens outside gameplay are not checked yet.

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

**Save states work, and they are how R2 is iterated.** Capture and resume
both run:

    GHPC_STATE_SAVE=songmesh GHPC_STATE_SAVE_AT=2.0   capture once, unattended
    GHPC_STATE_LOAD=songmesh                          resume straight into the song
    ghpc/scripts/vkarms.sh <out> ref:GHPC_VK_MESH=0 a:... a2:...
                                                      beat-paired arms from the state

`vkarms.sh` runs three arms of four beat buckets in under three minutes,
against about twenty for driven arms. Use it for every render comparison.

A resume restores the chart exactly. `game`, `ui` and `beat` come back at the
capture point and advance at the same rate as a driven run, +0.781 beat per
sample against the driven arm's +0.781. The state is 132 MB, almost all of it
the 128 MB devkit RDRAM. The save trigger keys on the song clock rather than
wall time, so it lands at the same chart position every run; both captures
fired at `song=2.016`.

Chunks: RDRM, SPAD, VRAM, VUMM, EECX, SCHD (kernel tables), MEMS (IO
registers, TLB, EE timers, GS registers, VIF1 residual), VUST (VU register
files), PADS (pad ports), MESH (the host mesh cache), CDFS (the CD layer's
pseudo-LBN table; without it a resumed song stalls on its next stream read),
GSST (the GS frontend's drawing state; without it a resumed menu draws with
reset defaults on the emulated path).

Menus save one state per screen: `GHPC_STATE_SAVE_SCREENS=1` writes
`menu_<screen>` once a screen has held for 120 vblanks, under the pad driver
or by hand. Capture on the emulated path, never with `--native`: a state saved
with VU1 off carries VU1 state that never ran. Compare with
`ghpc/scripts/vkscreens.sh`, which shoots on the ui clock
(`GHPC_VK_SHOT_UI`) since menus have no chart.

**The mesh cache is carried since 2026-09-21.** Without it a resumed run
served no draws (`draws=0` against 504 driven), because the cache fills at
`PsMesh::Sync`, which ran before the save. It now lives in the runtime
(`ghpc_mesh_cache.h`) and a resume reports `mesh cache restored, 888 meshes`
and `draws=541`. A state written before the chunk existed (the old `song`
slot) still loads and says it will serve no draws. The texture cache needs no
chunk: it rebuilds from guest RAM on the next material bind.

That was the same class as the pad bug: host state the guest's RDRAM implies
but which lives in C++ and was never written. The pad announced itself by
pausing the game. A cache does not announce anything, it just goes quiet. Ask
it of every host-side cache before trusting a state.

**There is still no determinism gate.** A resumed shot against a driven one
at the same beat is iou 0.99 and 0.92, close and not identical. So a state is
fine for A/B, where every arm loads the same state and one arm repeats
another, and is not ground truth for a single absolute measurement.

### R0: Vulkan bring-up, done 2026-09-20
SDL3 window, Vulkan instance, device, swapchain, cleared frame, selected by
`GHPC_RENDERER=vulkan` with raylib the default. Exit met on one binary, both
arms, in `notes/evidence/2026-09-20-r0-vulkan-bringup.txt`: MoltenVK on an M4
Pro, a 3-image B8G8R8A8_UNORM swapchain, 771 frames presented over a 15s hold,
and the raylib arm still reaching the main menu.

The seam is `ghpc::HostRenderer` in `include/ghpc_host_renderer.h`, implemented
by `src/lib/vk/` (one file per part; `vk_renderer.h` declares the class).
`PS2Runtime::m_hostRenderer` is null on
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

### R2: gameplay renders natively, lit, at realtime with VU1 off
**`./ghpc/scripts/play.sh --native` plays gameplay at 60 fps and game rate
1.000**, against 2.84 fps with VU1 emulated. The picture is iou 0.82-0.87
against the software path with a mean error of 13-15 per channel, where
unlit it was 0.66-0.80 and 42-63. `notes/evidence/2026-09-21-r2-cameras-targets-lighting.txt`.

What it took, in the order it was found, each one measured before it was
fixed:
- **Texture and the alpha test** (`2026-09-21-r2-texture.txt`). Needed, and
  not the fault: `nvOnly` moved 1-5%.
- **A camera per draw.** 5 distinct cameras in a gameplay frame; one frame-wide
  camera put the venue through the track's. Found with `GHPC_VK_MESH_ID`.
- **A render target per draw**, from PsRnd's register shadow. 87 draws render
  into a 128x256 offscreen target and were painted over the screen.
- **The blend equation**, ALPHA_1. The light cones are additive.
- **VU1's lighting**, all three overlays decoded and reproduced in the vertex
  shader, lights captured by a new `PsEnviron::Select` override.

`--native` sets `GHPC_RENDERER=vulkan GHPC_VK_MESH=2 GHPC_VU1_OFF=1`.
`GHPC_VU1_OFF` was a ceiling measurement while nothing replaced VU1; with the
native path drawing everything it is the fast path, and on `game_screen` the
software GS draws nothing at all without VU1.

Meshes whose geometry lives only in PsMesh's converted face packet (the menu
button icons) are decoded from it and draw natively since 2026-09-21. The
window title names the renderer path that came up, e.g.
`[Vulkan: native, VU1 off]` or `[raylib: software GS]`.

**Do this next:** `BACKLOG.md` `Now`, item 1c (`DrawRect`).

Geometry goes through a real Vulkan pipeline: per-mesh GPU buffers, the
measured transform in a vertex shader, a depth buffer, a render pass, one
pipeline per material depth and blend state, and one descriptor set per
texture. Pipeline detail in
`notes/evidence/2026-09-20-r2-vulkan-mesh-pipeline.txt`.

    GHPC_VK_TEX=0             untextured, no alpha test: the control arm
    GHPC_TEX_DUMP=dir         decoded textures as PPM, over a checker by alpha

    GHPC_VK_MESH=1            native geometry over the software blit
    GHPC_VK_MESH=2            native only, no blit
    GHPC_VK_MESH_DEPTH=N      0 none, 1 GREATER, 2 LESS, 3 material (default)
    GHPC_VK_MESH_RANGE=f:c    draw only c draws starting at f
    GHPC_VK_MESH_SKIP_ATE=1   drop the alpha-tested draws, to see behind them
    GHPC_VK_MESH_TINT=1       colour each draw by a hash of its mesh key
    GHPC_VK_MESH_LOG=N        the [ghpc/vk/mesh] line every Nth frame
    GHPC_VK_SHOT_BEAT=N       capture a PPM every N beats. Use this one.
    GHPC_VK_SHOT=N            capture every Nth present frame. Do not compare
                              two runs with it; see below.
    GHPC_VK_SHOT_SCREEN=s     hold the shots until the guest is on screen s
    GHPC_TEX_CENSUS=1         PSM, CBP, CPSM, TFX census at the real read point
    GHPC_SCENE_DROP_LOG=N     what the seam could not serve

`[ghpc/vk/pipeline]` prints the resolved depth compare and write flag for
every pipeline created. Reach for it before reasoning about depth state: it
settled in one 60s run what coverage had "answered" twice in opposite
directions.

Release, 260s driven, on `game_screen`:

    submitted=537 drawn=537 tris=41689 cached=536 hits=7442368 misses=536

**536 uploads in a 15,499-frame run.** Every mesh reaches the GPU once and is
drawn from its own buffers after that, which is what step 1 existed to do.
`drawn == submitted` on every frame, so no draw is being silently discarded
once it arrives.

`GHPC_VK_SHOT` is the change that makes this phase judgeable at all. The
picture is the evidence and a log line cannot be one; before it existed,
checking a render change meant watching the window.

**The alpha test is honoured and it was not the fault.** On `game_screen`,
189 of 528 draws enable it, all ATST GREATER against AREF 0, a punch-out. AFAIL
is 183 ZB_ONLY and every one of them blends, so a discard gets the colour
exact and loses only a depth write. This was the leading explanation for the
wrong picture and texture measured it out: see the R2 headline above.

`GHPC_VK_MESH_TINT=1` makes distinct meshes distinguishable and is the right
tool for the surface fault. `GHPC_VK_MESH_SKIP_ATE=1` is no longer needed.

**Comparing two runs' screenshots needs `GHPC_VK_SHOT_BEAT`, nothing else.**
Read `notes/gotchas.md`, "Screenshots pair on serial", first: shots were once
paired by index and by present frame, neither of which is a chart position,
and that manufactured a large convincing difference between two depth modes
out of nothing. Beat capture fires on the first present past a beat boundary,
names the file after the bucket, and `ghpc/scripts/vkcompare.sh` joins two
capture directories on that. Always include an arm identical to the one under
test: the measured noise floor is 8 of 12 buckets pixel-identical and 0.05 iou
otherwise, and a difference has to clear it.

**Placement is confirmed, beat-paired, one binary.** With
`GHPC_VK_MESH_DEPTH=1` the native path covers every pixel the software
rasteriser covers, `swOnly` exactly 0 in all twelve buckets. That confirms the
bone blend, the projection, the GS pixel mapping, the XYOFFSET subtraction and
the viewport together, which the offline checker cannot reach.

Per-material depth (the default, mode 3) loses about 4k pixels a frame against
that. Real, since it clears the noise floor, and small. **Do not settle the
default on it yet:** `nvOnly` is 44k to 90k in both arms, an order of
magnitude bigger than the gap between them. It is not the punch-outs, which
texture measured out; it is the surfaces covering the frame. Settle depth
after that fault is found, since occlusion is what depth decides.

`swOnly` is approximate once texture is on: the masks are a brightness
threshold, and dark texels fall under it.

**What settled the depth question was a log, not a picture.**
`[ghpc/vk/pipeline]` prints the resolved compare op and write flag for every
pipeline created, at most 32 of them, and one 60s run shows ZTST mapping
through and ZMSK turning the write off where it is set:

    ztst=1 zmsk=1 depthMode=3 compare=7 write=0     compare 7 is ALWAYS
    ztst=3 zmsk=0 depthMode=3 compare=4 write=1     compare 4 is GREATER

So `GHPC_VK_MESH_DEPTH` defaults to 3, the material's own state, because that
is what the hardware does. The coverage numbers above now say a forced GREATER
covers the software path more completely, by about 4k pixels a frame, so the
default is the open question and not the mapping. Settle it after texture,
when `nvOnly` stops dominating.

**The transform is proved, offline, in about a second.**
`ghpc/tools/vkxform.cpp` reimplements the shader's arithmetic and compares it
to the calibrated `ghpc_transform.h` over 19,075 random inputs: worst relative
error 2.2e-05. It exists because the shader deliberately does *not* divide by
w, so it is a different expression of the same transform. Run it before
suspecting the transform of anything.

    c++ -std=c++17 -O2 -I ps2xRuntime/include -o /tmp/vkxform ghpc/tools/vkxform.cpp

**The seam drops 1.08% of draws and none for want of faces.**
`[ghpc/scene/drop] served=356167 cacheMiss=3833 noFaces=0`. The noFaces=0 is
the useful half: every mesh the host cache holds has usable `Face[]`, so the
fretboard is not missing because its triangles live in the PS2 packet. The
1.08% is lost at `PsMesh::Sync`, before the seam, which is the
mutable-geometry case `notes/rnd-seam.md` already names.

**The first picture found a real bug, and it was not the transform.** One
uniform depth state for every draw put a single surface over 87% of the frame.
The cause is in `PsMat::Select`'s own registers: `TEST_1` at +0x148 carries
ZTE and ZTST, `ZBUF_1` at +0x158 carries ZMSK, and a PS2 overlay is drawn with
ZTST always and ZMSK set so it covers the frame without touching depth. A
backend that ignores ZMSK writes depth for it and loses everything behind it.
So per-material depth state is a precondition for a correct frame rather than
step-4 polish, and it moved forward into this change.

Two settled sub-results, so they are not re-derived:

- **GREATER, not LESS.** GS z is 29490.8 * (1 - clip.z/w), so it counts up
  towards the viewer: the test stays GEQUAL/GREATER and the buffer clears to
  zero. Measured rather than assumed, on one binary: the forced-LESS arm
  collapses the frame to two colours against 192 for GREATER.
  `GHPC_VK_MESH_DEPTH` keeps the mapping falsifiable.
- **XYOFFSET is 2048 minus half the framebuffer**, read live and not
  hardcoded: `ofx=1792 ofy=1824` at `src=512x448`, and it does move mid-run.

**Exit, unchanged and not met:** venue and fretboard render through Vulkan
above 30 fps with the gameplay chain alive (`BeatMatch::Poll` 0x1259c0,
`PlayerMatcher::Poll` 0x117dd0) and `song_tick_advanced=yes`.

Neither half holds. The picture is wrong because a few surfaces cover the
frame, and no speed claim belongs to R2 at all until step 5 lands, because
the software rasteriser still does every bit of the work the native path
duplicates.

**Worth saying once:** the last handoff argued texture was load-bearing for
the picture, on the punch-out theory, and moved it ahead of everything. The
measurement says otherwise. Texture was still needed, but a theory that
explains a metric is not the metric moving; pre-declare what the change
should do to the number and check it.

Two things that will cost a day each if taken on trust. `g_ghpcInstWorld` is
a probe and not a seam input, by its own file's account: it names a different
mesh chain than the DrawFaces it precedes, so the object matrix comes from the
packet scan in `ghpcSceneSubmit` instead. And a blunt `FlushPacket` stub
wedges the guest, which `ghpc/override/FlushPacket__5PsRnd_0x43e400.cpp`
records in full.

### R3: coverage
Whatever the native path does not cover yet (2D UI, particles, movie) keeps
going through the emulated GS into the same frame. Ace Combat 5's static
recompilation does exactly this and it is the reason its port shipped
incrementally rather than all at once.

**Exit:** no visual regression against the software path, at playable speed.

### A1: audio, in parallel
**Plays, 2026-09-21.** The IRX that decodes and mixes on a PS2 is replaced
by `ps2x::iop::synth`: streams arrive as raw PS-ADPCM the IOP asks for,
samples play out of an emulated SPU RAM, and the mix goes to an SDL3 device.
Replies to the EE are delivered from a `SynthEE::Poll` override, since
`CtlClientPoll` only flushes when it has something to say. The song is
confirmed by ear; a 75 s run shows 0 underruns and the clock unchanged at
`uirate=1.000`.

Song time is the EE's own timer; the IOP's play position (reply 3) only
snaps it when it drifts past 100 ms. SPU2 reverb is in and only Star Power
uses it. Map, protocol and what is left (ADSR, save states):
`notes/audio-path.md`.

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

For anything in the song, resume a save state instead (see the phases,
above); `vkarms.sh` is built on that.

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
