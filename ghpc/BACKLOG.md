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

**The VU1 runaway is fixed.** The MFIFO drain read a `cnt` payload linearly
past the ring end; it now wraps (`GHPC_MFIFO_NOWRAP_LEGACY=1` for the old
read). Same binary: cut-off microprograms 2331 -> 0, release gameplay
4.13 -> 14.75 Mcycles/sec. `notes/evidence/2026-09-11-mfifo-drain-wrap.txt`
and the third MFIFO section of `notes/boot-sequence-reference.md`.

Closed the same day: the rarer junk source was the same bug (a cnt tag in the
ring's last quadword puts its payload at exactly ring end; the wrap range is
now inclusive), and the picture is confirmed sane
(`notes/evidence/2026-09-11-gameplay-frame-*.png`: venue, fretboard, gems).

**Next: the `Rnd` seam.** Gameplay is at 5% of realtime on release, so the
remaining 20x is VU1 interpretation and the software GS. The map is
`notes/rnd-seam.md`: hierarchy, the hook set (`PsRnd::BeginDrawing`
0x1bfed0, `EndDrawing` 0x1c0070, `FlushPacket` 0x43e400, `PsMesh::DrawFaces`
0x43eea0, `PsMat::Select` 0x43ea70, `PsTex::Select` 0x43f490, `PsCam::Select`
0x1c1770), the data shapes that are known, and what is not. Hand-written
bodies now survive a build through `ghpc/override/` (`scripts/overlay.sh`,
proof override `EIntr_0x3518c8`), so the seam has somewhere to live.

Done: `PsMesh::DrawFaces` and `PsMesh::Sync` overrides log the engine-level
mesh under `GHPC_MESH_LOG`. Geometry is empty at draw time and present at
Sync entry; the contract and the addresses where it is freed are in the
seam note. Rung and speed unchanged with both overrides in.

Done: the host-side mesh cache. 99% of draws on `game_screen` find their
geometry host-side (`[ghpc/mesh/cache] hits=115314 misses=1024`).

**Where the time goes**, `notes/evidence/2026-09-11-vu1-profile.txt`: VU1
interpretation is ~80% of the busy thread, the software GS under 20%, VIF1
nothing. Inside VU1 the pipeline model is two thirds. An early-out on the
pipeline commit was tried and reverted: something is due every cycle, so it
never fires. A ready-ordered queue rewrite is worth ~1.25x at best. The
seam is the 20x.

Next seam round: the other three inputs. Log at `PsMat::Select` 0x43ea70
and `PsTex::Select` 0x43f490 what material and texture state a draw carries
(blend, the texture's `RndBitmap` at +0x28, width/height/bpp at +0x4c..),
and at `PsCam::Select` 0x1c1770 the matrices it uploads (qw696-703), each
as an override that keeps the generated body. With those and the mesh
cache, the first native draw is: at DrawFaces, transform the cached verts by
the owner's world xfm (+0xa0) and the last camera, and rasterise on the host
instead of kicking the packet. Same-build control arm, and the guest must
still run `BeatMatch::Poll` and `PlayerMatcher::Poll`.

Also open: the 1024 cache misses. Say which meshes they are (owner, flags)
before the fretboard turns out to be one of them.

Ruled out this round: VIF command sizing (PCSX2 rules agree with the runtime
on every command before the ring end), TTE tag splicing (2 tags total), and
the STCYCL WL=0 words as a GH2 behaviour (they were bytes past the ring end;
the decode stays because it matches hardware).

Done earlier in the same thread: the VIF1 residual fix
(`GHPC_VIF1_NO_RESIDUAL=1`), the scratchpad DMA decode (0x30b0 runaway), and
the STCYCL WL=0 decode (`GHPC_VIF_STCYCL_LEGACY=1`).

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
