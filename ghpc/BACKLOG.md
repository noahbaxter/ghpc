# Backlog

The queue only. Session findings live in `notes/session-findings-archive.md`;
topic notes in `notes/` supersede it. **Rewrite this file, never append to it.**
It rotted once by being appended to until a stale "M4 in progress" header sat on
top of 1570 lines of findings.

No status claim here that a command cannot check. Where one can, cite it.

## Ordering principle

Gameplay-first ordering, seam-level execution. Sequence by what blocks gameplay,
but fix at the seam that also buys speed and sound, rather than a symptom patch
that gets thrown away. Reaching gameplay is not the end goal.

    song load fix  ->  gameplay reachable  ->  Rnd seam  ->  playable speed
                                               (also retires M7 VU work)

## Now

**The `Rnd` seam, first native draw.** Gameplay is ~5% of realtime and VU1
interpretation is ~80% of the busy thread
(`notes/evidence/2026-09-11-vu1-profile.txt`). The win is to stop running VU1
for mesh draws: at `PsMesh::DrawFaces`, for a mesh whose geometry the host
cache holds, transform the cached verts and hand triangles to `GSCpuBackend`
directly. About 4x, so 5% toward 20%. The map is `notes/rnd-seam.md`.

**The vertex transform is solved, to GS subpixel**
(`notes/evidence/2026-09-12-bone-palette.txt`). Gameplay meshes are skinned,
which is why no single object matrix ever fit:

    world = sum over b of weight[b] * (pos * Bone[b])   b = 0..3
    clip  = world * M           M = qw700..703 as rows, row vectors
    q     = 1 / clip.w
    x     = clip.x * q * qw696.x + qw697.x    y and z the same
    s,t   = uv * q              the submit is fst=0, PRIM 4, iip=1

`Bone[0..3]` is the palette `PsMesh::DrawShowing` uploads at VU1 qw660..675
behind `0x6C140294` (V4-32, NUM 0x14, ADDR 660). The weights are the Vert's
four `Hmx::Color` floats at +0x20, which sum to 1.0. qw698 is not subtracted:
M carries the view transform.

Six meshes, 171 matched pairs: dx and dy medians 0.03 to 0.04 px, whole range
[0.00, 0.07], q at -0.000%. A sixteenth of a pixel is 0.0625, so the residual
is entirely GS quantisation.

That also closes the earlier "second DrawFaces call site" puzzle. 0x43f434 is
the only `jal DrawFaces` in the function, inside a per-material loop; the
skinned paths simply branch past 0x43f2ec because they write qw676 themselves,
as the palette's tail. Nothing was missing.

The pairing is exact because DMA, VIF1, VU1 and GIF all run inline on the
guest thread inside the store to D1_CHCR (`ps2_memory.cpp:2094`), so a mesh
tag set at DrawFaces entry covers exactly that draw's submits. `GHPC_TL_CAL=N`
turns it on.

**The native draw is written and it REGRESSED. Round 20, 2026-09-19.**
Release, same binary both arms, `GHPC_COUNTIN=0.5`:

    control, GHPC_NATIVE_DRAW unset               eerate 4.8   fps 2.93
    mode 2, transform runs, submits nothing       eerate 4.9   fps 2.94
    mode 1, transform submits, VU1 skipped        eerate 0.9   fps 0.45

**The cause is found and it is not the draw code.** Skipping a mesh
microprogram makes a *different* microprogram run away
(`notes/evidence/2026-09-19-vu1-runaway-from-skipping.txt`). Same build, both
arms holding `game_screen`, `GHPC_VU1_CENSUS=2000` on `build-debug`:

    pc0x30b0        invocations   cut-offs       maxInstr
    control                1616   0 / 0M              708
    native draw on          662   5336 / 349M       65536

pc0x30b0 terminates on its E-bit in ~708 instructions every time in the
control. With the native draw on, 89% of its invocations run to the full
65536 cycle budget. The mesh T&L program pc0xcd8 shows `budget=0` in both
arms, so the program being skipped is not the victim. The native arm also
reports `song_tick_advanced=no` against the control's `yes`, so the gameplay
chain is damaged, not merely slow.

**What is proven and stays.** The MSCAL seam pairing is exact (`drew=32768`,
`dropped=0`, `queued=0`) and the transform is subpixel against VU1's own
submitted vertices, replayed offline: dx +0.027, dy +0.041, q -0.0000%
against a 0.0625 quantisation floor. The seam is not refuted. Skipping a
microprogram and doing nothing else is.

**Next, pick one.** Either make the skip preserve the side effects pc0x30b0
depends on, or move the seam to packet granularity at `PsRnd::FlushPacket`
0x43e400 rather than per MSCAL. Start by extracting pc0x30b0 with
`scripts/mpgwalk.py` and reading it with `scripts/vudis.py` to find what it
consumes that a mesh draw would have written. VU1 state persists across
MSCALs: TOPS alternates base and base+ofst (`ps2_vu1_core.cpp:1316`), VU
registers carry over, and VU1 patches NLOOP in place in the qw680 GIFtag
(`ISW.x` at 0x1180).

**Four theories died on the way, do not re-chase them**
(`notes/evidence/2026-09-19-native-draw-profile.txt`): the host transform
being expensive (mode 2 measured 4.9 against 4.8), per-vertex
`GS::writeRegister` cost (3.6x fewer triangles, speed unchanged), offscreen
vertices clamped into the scissor (1.96M rejected, speed unchanged), and
triangles straddling the screen edge (fixture measured 5 to 81 px bounding
boxes, worst box 1% of the scissor). The profile settles it: `ghpcNativeDrawMscal`
is 2% of GameThread and `VU1Interpreter::run` is 96%.

**Iteration is no longer 6 minutes.** `GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=<path>`
captures real draws plus the primitives VU1 produced for them, and
`scripts/fixreplay.sh` replays them through the same transform header the
runtime uses and diffs against that oracle in under a second. Use it for the
transform, clipping and lighting work instead of game runs.

**The VU1 microcode is readable.** `.vutext` at vaddr 0x00437100, file offset
0x338100, is a VIF stream; walk it for VIFcode 0x4A and all 18 overlays come
out. Culling, lighting, PRIM flags, clipping and fog are answered in
`notes/rnd-seam.md` and `notes/evidence/2026-09-19-vu1-tl-semantics.md`.
Three of those contradict the current native draw: PRIM must come from the
qw680 tag (ABE is set for every blend mode except 1, FGE is real), vertices
are XYZF2 with fog rather than XYZ2, and colour is per-vertex lit rather than
a 0x80 constant.

**The stored 4.7 mark reproduces after all.** 2026-09-19, release, seven
`[eerate]` samples spanning 4.7 to 4.8 (13.92 to 14.28 Mcycles/sec). The
2026-09-11 note that it measured 3.0 stands as a record of that day, but the
gap was the host, not the tree. Treat 4.8 as the current floor.

Also done and still standing: the host mesh cache (99% of gameplay draws find
their geometry host-side, `[ghpc/mesh/cache] hits=115314 misses=1024`) and all
four backend inputs captured and named in the seam note. Six overrides in,
speed unchanged.

Cheap and still pending: cache three `getenv` calls in the VIF1 hot path
(`ps2_vif1_interpreter.cpp:947` per MSCAL, plus 816 and 901), 45 profile
samples.

## Next

**Rnd seam.** Playback is at 39.2% of realtime (23.5 vblanks/sec vs 60,
`GHPC_EERATE=60`) because rasterisation runs on the EE thread and guest time is
gated on the EE cycle counter. Needs ~2.5x rasteriser throughput, which
micro-optimisation will not deliver. A native GL or Vulkan backend at the `Rnd`
layer also makes VIF1, VU1, GIF, MFIFO and the GS rasterizer dead code. Do not
"fix" the clock by firing vblank on the host deadline alone; see the archive.

## Later

- **Video seam.** Deprioritised: `scripts/bootskip.py --on` skips the intro
  video entirely, so the IPU seam is not on the path to gameplay.
- **M8 input.** HID guitar, calibration, latency against a tuned PCSX2.
- **GH1 and 80s.** Symbolized debug builds in `work/elf-debug/`. GH1 has 8,433
  symbols against GH2's 12,663, suggesting an earlier prototype. Verify before
  assuming parity.
- **SPU handshake stall.** Real and unfixed, but proven not to block the song
  load: `GHPC_SYNTH_ACK=1` removes exactly `SPUSendBusy` and `SynthPoll` from the
  working set and changes nothing else.

**Boot skip is available and doubles iteration speed.**
`ghpc/scripts/bootskip.py --on` flips one 11 byte symbol in `ui/gen/init.dtb` so
boot goes straight to `main_screen`. Measured: `main_screen` at t=6.3 instead of
t=38.8, `loading_screen` at t=41.6 instead of t=86.3, menu fully navigable.
Reversible (`--off` restores byte-identical) and idempotent. **Caveat:** it skips
whatever `bootup_load` initialises, so confirm any song-load or audio repro under
`--off` before trusting it.

## Deferred

- **Retargeting the retail ELF.** Undecided, and it changes how every stage
  works. See `NORTHSTAR.md`, which calls this the biggest gap between the aim and
  reality.
- **Upstreaming.** Not until the port works. Patch 0003 (`isStubFunction`
  denylist) is the general fix covering all four name-collision mechanisms.
  Patch 0001 (`GetRomName` bounds) is a memory-safety fix whose decision to
  ignore `$a1` was derived from one caller in one game and needs disclosing.
- **Audit the remaining 247 stubs** for the same collision class.
- **Shipping target.** Development uses debug builds, which carry asserts and
  debug paths. Undecided.
- **Mod support.** In the aim, not started.

## References

- `gh2-decomp` (sibling repo) is a **reference clone, never a drop-in**, the same
  status `NORTHSTAR.md` gives `third_party/`. It targets byte-matching with
  32-bit GCC; ghpc builds native. When ghpc hits a function it must understand,
  check there for the name and shape first, then disassemble. Do not build a sync
  mechanism. It supplied `mState` at `0x4c` and `kPlaying`/`kStopped` for the
  song load work, and did not have the function that was actually blocking.
- `ps2ResolveGuestPointer` never fails. Out-of-range addresses are masked back
  into RAM and it returns true, so `getMemPtr` cannot reject a bad pointer. Any
  syscall taking a guest length must bound it itself.
- The `File/CD` debug tab exposes a `cdImage` field that nothing sets. No CLI
  flag or env var exists for it; the game-override hook is the intended path.
- Diagnostics have two known defects: `call_hist_dump` splices concurrent stderr
  into its own output, and `EeScheduler.cpp` still masks guest addresses to 25
  bits under a 128MB map. Both in `notes/song-load-crash.md`.
