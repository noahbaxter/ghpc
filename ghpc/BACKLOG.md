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

**The VU1 runaway, per `NEXT.md`. The program writes its output over its own
input header because vf2.x is 0. Find what writes vf2.**

`notes/evidence/2026-09-10-vu1-top-clobber.txt`: 0x30b0's output store
`SQI vf1, (vi4++)` at 0x3170 takes its pointer from `MTIR vi4, vf2.x` at
0x3148, and vf2.x is 0, so the GIFtag lands on qw 0. That is the input header
whenever TOP=0: 66 of 66 cut-offs, 1 of 302 ended runs. In order:

1. **Trace vf2 at runtime.** vf2 at MSCAL entry, every write to it during the
   run (pc, lower and upper word, value), and its value at the MTIR. Split
   TOP=0 against TOP=330 and cut against ended. Also dump the code at
   qw689.x*8, the second callee, which is the one place left in 0x30b0's path
   that could set it.
2. **If vf2 is carried in**, find the program that last wrote it before 0x30b0
   runs. VF registers persist across MSCALs, so a program sharing vf2 as
   scratch, or an init program that never ran or ran on bad data, would both
   look like this.
3. **Then 0xcd8.** It also takes its output pointer from `MTIR vi4, vf2.x`
   (0x0da0). Its runaway loop is different (header 0, and a store riding the
   runaway counter), so confirm whether fixing vf2 moves it before assuming.

VU code is readable offline: `work/GH2_debug.elf` holds the overlays as data.
Search for a known instruction pair from a runtime dump to get VU 0's file
offset, then decode by hand. Two overlays found so far: 0x3352e0 (0x30b0
program) and 0x334c08 (the 0x3e28 callee).

Done this round: the VIF1 residual fix. Commands straddling two DMA chunks are
now carried, not dropped (`GHPC_VIF1_NO_RESIDUAL=1` for the old behaviour).
Invalid opcodes 118,798 to 1,018, speed unchanged. Still open from that thread:
the 0xcd8 menu runaways whose freshly unpacked header is all zeros (mscal 3363
onward), 6 dirty chunks with no truncation behind them, and whether TOP
514/515/753/768 at gameplay are offsets the game really sets. The 0xcd8 runaway
path also enters through a jump from 0x0d00 to 0x0000.

**Then the `Rnd` seam.** Not before the runaway: a native backend built on top
of a runaway inherits it. The cost is VU1 interpretation, not the GS
rasteriser.

Cheap and still pending: cache three `getenv` calls in the VIF1 hot path
(`ps2_vif1_interpreter.cpp:947` per MSCAL, plus 816 and 901), 45 profile
samples.

## Next

**Override layer.** No hand-written function body can survive a build today:
staging does `rsync --delete` from generated output into a gitignored
`ps2xRuntime/src/runner/`. This is very likely what produced the `DataArray`
workaround that existed only as a stale object file with no source. Design in
`.planning/2026-09-09-work-spine-design.md`. Must fail the build, not warn, when
an override's target no longer exists.

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
