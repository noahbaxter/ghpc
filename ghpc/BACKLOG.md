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

**The VU1 runaway in 0xcd8, per `NEXT.md`. Its loops are sound and their input
at gameplay TOPs is bad. Find out whether the gameplay OFFSETs are real.**

`notes/evidence/2026-09-11-vu1-gameplay-loops.txt`: the first ten gameplay
cut-offs, all 0xcd8, spread over four loops. Six have a bad count or bound from
input: a zero header, a y == z header of -31872 at TOP 514, a strip count near
900. Four spin in 0x30b0's vertex loop with plausible headers and are not
explained. In order:

1. **Are OFFSET 514 and 515 real?** Gameplay TOPs come from OFFSET VIFcodes
   carrying NUM 2 or 3. At the first few, dump the last parsed commands with
   their consumed sizes (the `g_hist` ring behind `dumpHist`, whose own cap is
   already spent at boot, so give this its own) and the raw words around the
   code. A real OFFSET sits exactly where the previous command's payload ends;
   a misparsed one sits inside a payload. If misparsed, name the command whose
   size is wrong.
2. **If they are real**, the second input half at qw 514 overlaps the constants
   at 680 and the outputs at 704 and 849, so something else has to keep batches
   small or move those. Check the projection matrix going bad at the start of
   gameplay against unpacks landing past qw 680.
3. **0x30b0's vertex loop inside 0xcd8** (dumps 7 to 10): back edges taken ~800
   times against vi3 moving 15 to 20. Decode 0x31b0 to 0x3730 and follow the
   unconditional B at 0x3700.
4. **Left over.** The UNPACK behind the menu zero headers (mscal 3363 onward),
   and the chain tag walker masking bit 31 off the tag ADDR
   (`ps2_memory.cpp:215`), which would read a chain REF into scratchpad from
   RAM the same way.

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
