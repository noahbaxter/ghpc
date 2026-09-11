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

**The VU1 runaway, per `NEXT.md`. The bound is named; find why TOP=0 input is
wrong.**

`notes/evidence/2026-09-10-vu1-loop-bound-provenance.txt` names it: in
microprogram 0x30b0, `ILW vi12, 0(vi5)` at pc 0x31b8 loads the loop bound from
VU1 address 0 (1042) instead of 0x14a0 (18), because its base came from XTOP
and TOP was 0. Boot sets BASE=0, OFFSET=330, so TOP=0 is the real other half of
the double buffer. The fault is upstream of the VU. In order:

1. **Split the census by TOP.** Per (startPc, TOP), count E-bit ends against
   cut-offs. If every TOP=0 run of 0x30b0 is cut off and every TOP=330 run
   ends, the buffer half is the variable. If not, it is not.
2. **Find what is at qw 0 when a TOP=0 MSCAL reads it.** `ghpcLogQwWrite`
   logs UNPACK, store and read events per quadword, windowed by
   `GHPC_RW_FROM` and `GHPC_RW_SPAN` (`gs_cpu_backend.cpp:1173`). Aim it at
   qw 0 and qw 330 around a runaway. Either the input was unpacked to 0 and is
   wrong, or it went to 330 while TOP said 0, which is a VIF1 double-buffer
   bug (`ps2_vif1_interpreter.cpp:959`).
3. **Dump gameplay runaways, not the first ten.** `GHPC_VU1_LOOP` dumps the
   first N, which were all on `qp_selsong_screen` and `loading_screen`. Add a
   skip so the dumps land on `game_screen`, and confirm the same instruction
   is at fault there before assuming it.

Side leads, not this bug: 1015 of 1016 OFFSET VIFcodes in a 300s run carry
NUM != 0, the shape the BASE handler rejects as desync
(`ps2_vif1_interpreter.cpp:873`); they start at mscal 8597, after the runaways
above. And the 0xcd8 runaway path enters through a jump from 0x0d00 to 0x0000.

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
