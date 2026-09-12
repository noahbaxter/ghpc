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

Done: the vertex transform, measured rather than guessed
(`notes/evidence/2026-09-11-vertex-transform-calibration.txt`). Pairing the
cached object-space verts against the screen-space verts the PS2 path submits
for the same mesh pins the convention:

    clip = (pos * World) * M    M = qw700..703 as rows, row vectors
    q    = 1 / clip.w
    x    = clip.x * q * qw696.x + qw697.x     y and z the same
    s,t  = uv * q               the submit is fst=0, PRIM 4, iip=1

qw698, the camera position, is not subtracted: M carries the view transform.
On the sample mesh this lands x to a median 0.56 px and q to 0.74%.

The pairing is exact because DMA, VIF1, VU1 and GIF all run inline on the
guest thread inside the store to D1_CHCR (`ps2_memory.cpp:2094`), so a mesh
tag set at DrawFaces entry covers exactly that draw's submits. `GHPC_TL_CAL=N`
turns it on. The taps cost nothing: same tree with and without, rung 9,
eerate_pct 3.0 and fps 1.85 on both.

**Next, and the one thing blocking the draw: where the object matrix comes
from.** y is off by a near-constant 4 px, and the obvious source is wrong.
`PsMesh::DrawShowing` at 0x43f2d0 calls `WorldXfm` on the instance and uploads
it to VU1 qw676, but capturing that puts the mesh 21 px off in x and 19 in y,
and the instance it names owns a different mesh: the draw arrived through
DrawShowing's second `DrawFaces` call site at 0x43f434 without passing
0x43f2ec. The owner's cached `this+0xa0` fits much better but its dirty word
at +0xe0 reads 1 on every gameplay draw, so it is stale, which is the likely
4 px. Find the writer the 0x43f434 path uses. Do not re-capture at 0x43f2f4,
and do not read the cursor back at DrawFaces: the packet is already flushed.

Then the draw itself. Keep the PS2 path for cache misses and behind an env
knob so both arms are the same binary. Pass is `eerate_pct` up on that binary
with the rung still 9 and the gameplay chain alive (`BeatMatch::Poll`
0x1259c0, `PlayerMatcher::Poll` 0x117dd0), plus a frame capture that still
shows the venue.

**The stored mark of 4.7 does not reproduce.** The tree as of the previous
round measures 3.0 on this host. Re-measure the mark's own tree before
claiming a speed win against it.

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
