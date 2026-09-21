# Backlog

The queue only. `ghpc/NEXT.md` is the standing handoff and carries the working
rules. Session findings live in `notes/session-findings-archive.md`; topic notes
in `notes/` supersede it. **Rewrite this file, never append to it.** It rotted
once by being appended to until a stale "M4 in progress" header sat on top of
1570 lines of findings.

No status claim here that a command cannot check. Where one can, cite it.

## Ordering principle

Two tracks, worked in parallel, because neither blocks the other and both gate
playability:

    rendering   Vulkan at the Rnd seam   speed
    audio       SPU upload, then output  note timing

Rendering is sequenced in `NEXT.md` as R0 to R3. Audio is A1.

## Now

**`--native` plays gameplay at realtime. Make it the whole game.** State in
`NEXT.md`, numbers in `notes/evidence/2026-09-21-r2-cameras-targets-lighting.txt`.
In order:

1. **Menus under `--native`: compared one to one, the icons are in.** Each
   menu is a save state (`menu_<screen>`, `GHPC_STATE_SAVE_SCREENS=1`) and
   `ghpc/scripts/vkscreens.sh` shoots every arm on it at the same ui-clock
   moments. The emulated reference repeats at iou 1.0000.

       native vs ref   main 0.890   setlist 0.974   difficulty 0.878
       swOnly          main ~53     setlist 1       difficulty 13

   The button icons draw from PsMesh's converted face packet
   (`ghpcDecodeFacePacket`, DrawFaces override), served per frame as
   `submitDynamicMesh`. `GHPC_SCENE_PACKET=0` turns it off for a control
   arm. Numbers in `notes/evidence/2026-09-21-icon-face-packet.txt`.
   Left here: cache the decode by mesh instead of walking it every frame.
   Gameplay decodes two small quads the same way and its picture is
   unchanged.

   Not yet shot: career, multiplayer, training, options, results. Capture by
   hand with `GHPC_STATE_SAVE_SCREENS=1`, or give the driver a route.
   `lose_screen` drew a solid black panel behind its text in an earlier run.

   Material programs still unreproduced, from `[ghpc/scene/light]` on
   gameplay: 8 draws sphere map (0x347), 9 others. The texture transform
   (0x17c) is in and took gameplay from iou 0.82-0.87 to 0.89-0.93.
2. **What the 128x256 offscreen target is for.** 87 draws render into it
   through the venue's second camera and are skipped. If a screen draw samples
   it (a render-target PsTex, `renderTarget=` in `[ghpc/scene/tex]`, is 0 on
   game_screen) it needs an offscreen Vulkan target; if nothing does, it is
   something the GS path composites outside the seam.
3. **The remaining picture error**, 13-15 per channel. Fog (XYZF2, never
   implemented), the point light's unclamped dot, ZB_ONLY depth writes, the
   depth default. Shoot with `vkarms.sh`, read with `vkdrawid.py`.
4. **Audio (A1).** With gameplay at realtime it is now the thing between the
   port and playable.

Everything below this line in `Now` is the history of getting here, kept
for its measurements.

**R2: the Rnd seam.** Exit criteria in `NEXT.md`. Steps 1 to 4 are in; step
5 (stub the packet path) turned out to be `GHPC_VU1_OFF` once the seam drew
everything. Numbers and knobs in
`notes/evidence/2026-09-20-r2-vulkan-mesh-pipeline.txt`.

What arrives per frame on `game_screen`, and what the GPU does with it:

    [ghpc/scene]   draws=537 verts=41732 tris=41979 skinned=296 cam=1
    [ghpc/vk/mesh] submitted=537 drawn=537 tris=41689 cached=536
                   hits=7442368 misses=536

536 uploads in a 15,499-frame run, so every mesh reaches the GPU once. The
contract is `ps2xRuntime/include/ghpc_scene.h`. The collector is
`src/lib/gs/ghpc_scene.cpp`, the submit is `ghpcSceneSubmit` in the DrawFaces
override, the frame boundary is vblank in `EeScheduler`, and the backend is
`src/lib/ghpc_vulkan_renderer.cpp`.

The sequence, with what is left:

1. ~~Vertex and index buffers on the GPU, keyed by `meshKey`.~~ Done.
   misses=536 against hits=7442368.
2. ~~A vertex shader doing the measured transform.~~ Done, and proved offline
   against the calibrated `ghpc_transform.h` by `ghpc/tools/vkxform.cpp`:
   19,075 inputs, worst relative error 2.2e-05. The shader does not divide by
   w, so it is a different expression of the same transform and needed its own
   check. Run it before suspecting the transform of anything.
3. ~~A depth buffer and a render pass.~~ Done. D32_SFLOAT, compare GREATER,
   clear zero, because GS z counts up towards the viewer.
4. **Material and texture.** Depth, blend, texture and the alpha test are in.
   `notes/evidence/2026-09-21-r2-texture.txt` has the numbers. Texels come
   from PsTex's `RndBitmap` in guest RAM, not GS VRAM, so `SampleTexture` was
   not reused: that bitmap is linear and needs no swizzle.

   Left here, none of them the reason the picture is wrong:
   - The blend equation. `ALPHA_1` at PsMat+0x138 carries it; what went in is
     source-alpha-over for any `mBlend` but 1.
   - Lighting. VU1 lights per vertex (qw695 times the vertex colour); mColor
     stands in, so textured surfaces are flat lit.
   - ZB_ONLY alpha-test failures are discarded, losing a depth write the GS
     makes. 183 of 189 alpha-tested draws are ZB_ONLY.
   - Render-target textures (PsTex +0x48 & 8) have no bitmap and draw
     untextured. None on game_screen; counted as `renderTarget=`.
   - A bitmap rewritten in place keeps its pointers and is not re-decoded.
     Nothing on game_screen shows it; the signature is pointers and dims.
5. Only then stub the packet path, so the rasteriser stops doing the work
   twice. Note that a blunt `FlushPacket` stub wedges the guest (see Parked).
   **No speed claim belongs to R2 until this lands**, because until it does
   the software rasteriser still does all of the work.

**The seam drops draws before the backend sees them, and that gap is now
counted.** `[ghpc/scene/drop]` reports `served`, `cacheMiss` and `noFaces`
from the DrawFaces override. `ghpcSceneSubmit` serves a draw only when the
host mesh cache has it *and* `Face[]` is non-empty, so a mesh whose triangles
live in the converted PS2 packet rather than the engine's face vector never
reaches Vulkan at all. rnd-seam.md's miss census is `verts=4 faces=0` for the
mutable-geometry mesh and `verts=0 faces=0` for two others, which is exactly
that shape. Read the counter before concluding anything about missing
geometry, and note that R2's exit names the fretboard specifically.

**The picture is NOT verified against the software path, and the coverage
comparison is the first thing to fix.** `ghpc/scripts/vkcoverage.py` works and
its masks are sound; the pairing is not. Shots were paired by index and then
by `frame=`, and neither is a chart position: the present loop runs free at
about 60 Hz while the guest advances at about a tenth of that, so two arms of
one batch sat at guest serials 653 and 857 at the same present frame. Coverage
moves about 0.2 iou over 240 present frames as the chart advances, so
mispaired shots gave a large, smooth, convincing difference between two depth
modes across seven pairings, and it was the pairing error. Full account in
`notes/gotchas.md`, "Screenshots pair on serial".

**Fixed, and this is how to use it.** `GHPC_VK_SHOT_BEAT=N` captures every N
beats of the chart instead of every N present frames. It fires on the first
present past a beat boundary, which lands within a few hundredths of a beat
(measured: buckets at 1.015, 2.004, 3.019, 4.008), and names the file after
the bucket, so two runs write the same filenames for the same chart positions.
`ghpc/scripts/vkcompare.sh <refdir> <armdir>...` joins two capture
directories on that.

N=1 is about right: the measured rate is roughly 1563 present frames per beat,
so N=4 yielded 2 shots in a 300s arm and N=1 gives about 9. Always include an
arm identical to the one under test. That control measured the noise floor at
8 of 12 buckets pixel-identical and 0.05 iou otherwise, which is what makes
everything else interpretable, and without it this metric already produced two
confident wrong answers.

**What it says now, beat-paired on one binary, twelve buckets.** `swOnly` is
pixels the software path covered and the native path did not:

    forced GREATER (mode 1)      iou 0.61-0.80  swOnly 0 in all 12
    per-material depth (mode 3)  iou 0.59-0.79  swOnly 107-4243

So placement is confirmed: with a forced depth state the native path covers
every pixel the PS2 path covers. Per-material depth loses about 4k a frame,
which clears the noise floor and is therefore real, and is also five times
smaller than the mispaired comparison claimed.

`nvOnly` is 44k to 90k in both arms, an order of magnitude larger than the gap
between them. It is not the punch-outs: texture and the alpha test moved it
1-5%. **The depth default is not settled on this**; settle it once the
surfaces below are found.

**Solved: a few surfaces covered the frame.** It was one camera per frame
where GH2 uses five, and 87 offscreen draws painted onto the screen. Per-draw
camera and render target fixed it; `GHPC_VK_MESH_ID` and `vkdrawid.py` found
it in two runs, after `GHPC_VK_MESH_RANGE` sweeps had not.

**Per-material depth is the default and its mapping is verified by log, not by
picture.** `[ghpc/vk/pipeline]` prints the resolved compare op and write flag
for every pipeline created, and one 60s run shows ZTST mapping through with
ZMSK turning the write off where set (compare 7 is ALWAYS, 4 is GREATER):

    ztst=1 zmsk=1 depthMode=3 compare=7 write=0
    ztst=3 zmsk=0 depthMode=3 compare=4 write=1

Whether a forced state (`GHPC_VK_MESH_DEPTH=1`) gives a better picture is
open. Two places the mapping could still be wrong, neither a known fault:

- The 16 ZTST=always draws. An unconditional test that also writes depth
  writes a near value and blocks geometry behind it. If they are overlays they
  should carry ZMSK too, and 48 draws do, so the question is whether the two
  sets are the same. `[ghpc/scene]` reports both counts; cross-tabulate them.
- Material attribution. A draw gets whichever material was selected last, and
  one frame has 263 binds for 532 draws. If any draw is served by another
  draw's material, its depth state is another draw's too. Needs a per-draw tap;
  `[ghpc/vk/pipeline]` shows states created, not which draw got which.

**Depth sense is settled, three arms on one binary.** Distinct colours in the
same frame: GREATER 192, no-depth 3, LESS 2. GS z is 29490.8 * (1 - clip.z/w),
so it counts up towards the viewer and GREATER is right.
`GHPC_VK_MESH_RANGE=first:count` isolates a slice of the 532 draws, but note
that `0:64` covers nothing visible at all: the low indices are the wrong place
to look for a named mesh, so sweep the range rather than assuming front to
back.

**XYOFFSET is 2048 minus half the framebuffer**, read live from the GS and not
hardcoded: `ofx=1792 ofy=1824` at `src=512x448`. It moves mid-run.

VU1 semantics the pipeline must honour
(`evidence/2026-09-19-vu1-tl-semantics.md`): PRIM comes from the qw680 tag
with ABE set for every blend mode except 1, vertices are XYZF2 with fog rather
than XYZ2, colour is per-vertex lit rather than a 0x80 constant, and there is
no backface culling anywhere in the 13,872 bytes, so a triangle list off
`Face[]` is a correct substitute for the strips.

**Do not use `g_ghpcInstWorld` as the object matrix.** DrawShowing's own file
calls it a probe and not a seam input, because it names a different mesh chain
than the DrawFaces it precedes. The matrices come from the packet scan in
`ghpcSceneSubmit`, which names the right draw by construction.

**Save states iterate R2.** The host mesh cache is a MESH chunk now, and a
resume reports `draws=541` against 0 before. Capture `songmesh` once, then
`ghpc/scripts/vkarms.sh` for every render comparison.

R1 landed with a different gate than the one written here, deliberately. The
planned check was the same frame on both renderers by frame signature, and that
is not measurable: the signature in `gs_cpu_backend.cpp:1211` hashes the GS
source buffer before either renderer touches it, so it is identical on both
arms by construction, and this file already says hashes are not stable across
runs. Neither form of the comparison can fail.

What went in instead reads the presented swapchain back and compares it to the
bytes handed to `uploadFrame`, on a 1:1 blit so there is no resampling to
excuse a difference. `GHPC_VK_PARITY=N`, every Nth frame. It caught nothing
because nothing was wrong, but it can catch the swizzle, the filter and the
rect, which a source-side hash never could: 10 checks, 0 mismatched pixels out
of 229k to 287k, 0 validation errors, and no measurable speed cost against
raylib (43.3% vs 43.5% eerate on the same binary).

Still unsettled from R1: whether ImGui keeps the raylib path (rlImGui) or moves
to the Vulkan backend. The Vulkan path has no debug panel and no pad input at
all, since both are raylib's and `IsWindowReady()` is false there.

**The R2 ceiling arm is not measured yet, and the blunt version of it is a
dead end.** Returning early from `PsRnd::FlushPacket` wedges the guest: `ui`
froze at 0.505 for 90s while `eerate` read 100.0% of realtime, and nothing past
`main_screen` was reachable. The reason is in
`ghpc/override/FlushPacket__5PsRnd_0x43e400.cpp`, which keeps the knob only as
a recorded dead end. A surgical stub has to keep the bookkeeping calls and drop
only the MMIO kick and the spin.

Until that exists, the live-guest proxy for the size of the win is
`GHPC_VBLANK_SCALE=4`, which rasterises a quarter of the frames and takes
gameplay from 0.050 to 0.192 with the chart still advancing.

R2 is where the speed arrives, and every input it needs was measured before
the pipeline was written. Where each one lives, rather than a fourth copy of
it here:

- The vertex transform: `notes/evidence/2026-09-12-bone-palette.txt` for the
  calibration, `ps2xRuntime/include/runtime/gs/ghpc_transform.h` for the code
  the replay tool and the shader checker both run.
- The four backend inputs, and the host mesh cache: `notes/rnd-seam.md`.
- VU1's own semantics: `notes/evidence/2026-09-19-vu1-tl-semantics.md`.

**A1 audio, in parallel.** The SPU upload protocol is solved and the ack is
implemented; it is not sufficient. `chunks=1` in both arms, control
`GHPC_SPU_ACK=0`. Two candidates in `notes/audio-path.md`, both
distinguishable, neither concluded:

1. The ack is dropped before it is sent, because StreamInfo outranks it when
   they share a flush.
2. The game only calls `SPUStartSend` once here, so there is no second transfer
   to pace.

Lower `kSpuChunkLogEvery` from 32 before measuring; it currently hides chunks 2
through 31.

**Finish the oracle.** Game time against wall clock is in (`[ghpc/clock]`,
`GHPC_CLOCKRATE=N`) and `[fps]`/`[eerate]` now carry `screen=`, so a loading
screen number can no longer be read as a gameplay one. What is left:

- Audio chunks flowing, the third number.
- A gate in `checkrun.sh` that asserts on `uirate` for the screen it is on.
- `progress.py` still scores `eerate_pct` at the top rung. It should score
  `uirate`, since `eerate` measures cycles retiring and not game speed, and the
  two differed by 2x until 2026-09-20.
- The control arm can still produce no `[eerate]` line at all, seen twice on
  release.

## Parked

**Save states, half built and not usable.** `ps2xRuntime/src/lib/ghpc_state.cpp`
writes a chunked file holding RDRAM, the scratchpad, GS VRAM, both VU memory
banks and the EE context, and saves only at a vblank where
`EeScheduler::isQuiescentForState()` reports no host callback outstanding.

That gate is the whole design. `GuestThread` carries three `std::function`
members (`wait.completion`, `resumeCompletion`, and one per queued
`GuestInvocation`) which cannot be serialised, so a state written while any is
live would restore a thread waiting on a callback that no longer exists: it
loads, looks right, and diverges later. `GHPC_STATE_PROBE=N` measured 61
samples to and on `game_screen`, all quiescent, 3 threads, nothing queued. The
probe only shows quiescent frames are common; the runtime check is what makes
a written state trustworthy.

Parked on purpose, ahead of R0. R0 presents a cleared frame and R1 checks blit
parity on the main menu at t=6, so neither needs gameplay to be close. R2 is
the first phase that wants it, and `fixreplay.sh` already covers the risky part
there at about a second. Pick this up at the start of R2 if the 25s drive is
actually hurting by then.

Still missing before a state can be loaded at all:

1. The `EeScheduler` tables: threads (POD fields only), semaphores, event
   flags, alarms, INTC and DMAC handlers, ready queues, the id counters and
   the vsync tick.
2. VU0 and VU1 interpreter registers, not just their memory banks.
3. GS registers and draw state, plus `PS2Memory`'s IO registers, EE timers,
   TLB and VIF1 residual.
4. IOP service state.
5. A trigger. A hotkey is the right shape, since the point is to capture
   wherever the player happens to be, and `GHPC_STATE_LOAD=<slot>` to return.
6. A determinism check as the acceptance gate: save at frame N, run to N+K and
   save again; separately load N, run K frames, save. The two N+K states must
   match byte for byte. Anything uncaptured shows up as a diff.

Never load a state that has not passed item 6.

## Later

- **VU1 static recompilation.** Contingency, not the plan. Only if programs
  survive the Rnd seam and still cost. `.vutext` at vaddr 0x00437100 is a VIF
  stream; `scripts/mpgwalk.py` walks it for VIFcode 0x4A and all 18 overlays
  come out, `scripts/vudis.py` disassembles them. 13,872 bytes total. The hard
  part is the indirect `JALR` at 0x0d10 taking its target from qw688.x.
  Prior art: ico-recomp statically recompiles five VU1 microprograms.
- **M8 input.** HID guitar, calibration, latency against a tuned PCSX2.
- **GH1 and 80s.** Symbolized debug builds in `work/elf-debug/`. GH1 has 8,433
  symbols against GH2's 12,663, suggesting an earlier prototype. Verify before
  assuming parity.
- **Video seam.** Deprioritised. `scripts/bootskip.py --on` skips the intro
  entirely, so the IPU seam is not on the path to gameplay.
- **SPU handshake stall.** Real and unfixed, but proven not to block the song
  load: `GHPC_SYNTH_ACK=1` removes exactly `SPUSendBusy` and `SynthPoll` from
  the working set and changes nothing else.

## Deferred

- **Retargeting the retail ELF.** Undecided, and it changes how every stage
  works. See `ghpc/NORTHSTAR.md`.
- **Upstreaming.** Not until the port works. Patch 0003 (`isStubFunction`
  denylist) is the general fix covering all four name-collision mechanisms.
- **Audit the remaining 247 stubs** for the same collision class.
- **Shipping target.** Development uses debug builds, which carry asserts and
  debug paths. Undecided.
- **Mod support.** In the aim, not started.

## Solved, do not re-chase

Each cost at least a session. Evidence files hold the detail.

- **The VU1 runaway.** The MFIFO drain read a `cnt` tag's payload past the ring
  end while the fill side wrapped. Fixed in `appendData` by address. Cut-off
  microprograms 2331 to 0 in 74,000 MSCALs; release gameplay 4.13 to 14.75
  Mcycles/sec. `GHPC_MFIFO_NOWRAP_LEGACY=1` is the control arm.
- **Everything that looked like a VIF1 parser bug was that same ring read.**
  Three fixes made along the way are kept because they match hardware, but none
  of them was the cause.
- **The corrupted picture was the runaway.** Gameplay frames on the fixed build
  show venue, fretboard and gems.
- **A per-MSCAL seam, in any form.** Round 20's selective native draw regressed
  to eerate 0.9. A skip that gates MSCAL while `setVu1MscntCallback` keeps
  resuming lands at 0.8. Gating both reaches 100.1. The failure mode is a
  microprogram left running on state a skipped predecessor should have written.
  Skip everything or skip nothing.
- **`PsCam::Select`'s VIF path.** Probed and cleared; the packet is right.
- **An early-out on the VU1 pipeline commit.** Correct and worth nothing: an
  entry becomes ready almost every cycle, so it never fires. Reverted.
- **Four native-draw cost theories**
  (`notes/evidence/2026-09-19-native-draw-profile.txt`): the host transform
  being expensive, per-vertex `GS::writeRegister` cost, offscreen vertices
  clamped into the scissor, and triangles straddling the screen edge. All dead.
- **`GHPC_STREAM_READY`** reached `game_screen` sooner and produced a deader
  guest. Anything that shortens a wait ships with a same-build control and
  proves the gameplay chain still runs (`BeatMatch::Poll` 0x1259c0,
  `PlayerMatcher::Poll` 0x117dd0).

## References

- `gh2-decomp` (sibling repo) is a **reference clone, never a drop-in**. 39%
  byte-matched, 4,922 of 12,663 functions. `src/rndobj` carries the
  platform-neutral `Rnd`, `Mat`, `Cam`, `Tex`, `Mesh`, `Environ`,
  `LightPreset`, `Striper`, which is the interface a native backend implements.
  `docs/notes/VU1CameraUpload.md` independently works out the qw696..703
  upload. `tools/cop2.py` censuses VU0 macro-mode functions, which is a
  different corpus from VU1 microcode.
- `ps2ResolveGuestPointer` never fails. Out-of-range addresses are masked back
  into RAM and it returns true, so any syscall taking a guest length must bound
  it itself.
- Diagnostics have two known defects: `call_hist_dump` splices concurrent
  stderr into its own output, and `EeScheduler.cpp` masks guest addresses to 25
  bits under a 128MB map. Both in `notes/song-load-crash.md`.
- `notes/song-load-crash.md` carries a ruled-out list. Re-chasing something on
  it is the most expensive mistake available here.
