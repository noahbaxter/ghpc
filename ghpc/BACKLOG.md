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

**The VU1 runaway in 0xcd8, per `NEXT.md`. 0x30b0's is fixed; 0xcd8's is the
gameplay cost.**

`notes/evidence/2026-09-10-vu1-top-clobber.txt`: the vf2 init at VU 0x3730
never ran, because `PsRnd::Reset` sends it as a normal-mode scratchpad DMA
(MADR 0x80000020) and the runtime read it from RAM. Fixed, with
`GHPC_DMA_SPR_LEGACY=1` for the old decode. 0x30b0 now ends 110 of 110.
Release speed SAME. 0xcd8 still cuts off 13% of its runs, and those burn 187M
VU1 instructions against 48M for the rest. In order:

1. **Characterize the gameplay cut-offs.** They are the cost, and their headers
   look sane (w = 0x614, 0x373, 0x3c6, ...). `GHPC_VU1_LOOP` dumps the first N
   runaways, which are menu ones; add a skip so the dumps land on
   `game_screen`, then name the loop and its bound as was done for 0x30b0.
2. **The all-zero header, which is the jump to 0x0000.** 0xcd8 starts with
   `XTOP vi5; ILW.w vi1, 0(vi5); ... JALR vi2, vi1` (0x0cd8 to 0x0d00): its
   first call goes to header.w * 8, a per-batch routine selector (0x614 is the
   `JR vi2` stub at 0x30a0; 0x373/0x3c6/0x369/0x394 point into loaded
   overlays). A zero header sends it to 0x0000, where no MPG ever loads code
   (MPG ranges start at 0x9c8). So the menu runaways (mscal 3363 onward) are
   the zero headers. Find that UNPACK and where its source came from.
3. **Other bit-31 DMA addresses.** The chain tag walker masks bit 31 off the tag
   ADDR (`ps2_memory.cpp:215`), so a chain REF into scratchpad would be read
   from RAM the same way. Check whether GH2 issues any.

VU code is readable offline. `work/GH2_debug.elf` has a `.DVP.overlay..<addr>`
section per overlay naming its VU load address and size, and the code itself
sits in `.vutext` as `.vu.N` symbols, each right after its `.vif.N` MPG
VIFcode. Decode by hand, checked against instructions whose effect a runtime
dump already confirmed.

Done: the VIF1 residual fix (`GHPC_VIF1_NO_RESIDUAL=1` for the old behaviour,
invalid opcodes 118,798 to 1,018, speed unchanged) and the scratchpad DMA
decode (0x30b0 runaway gone, speed unchanged). Still open from the VIF1 thread:
6 dirty chunks with no truncation behind them, and whether TOP
514/515/753/768 at gameplay are offsets the game really sets.

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
