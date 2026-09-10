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

**Find out why the gameplay poll chain never runs.** Measured 2026-09-10 on
`build-debug` with a same-build control arm, 300s each: under
`GHPC_STREAM_READY=1` the run holds rung 9 `game_screen` for the full 300s,
without it the same binary holds rung 8 `loading_screen`. So the stand-in does
cause the transition. But at `game_screen`, `PlayerMatcher::Poll` (0x117dd0),
`Player::Poll` (0x112fb0), `BeatMatch::Poll` (0x1259c0) and `BeatMatcher::Poll`
(0x2727c0) are called **zero** times, while `GamePanel::Poll` (0x107140) is
called exactly twice in 300s. `StreamEE::Poll` is hot throughout. The screen is
up and nothing is driving it. `GamePanel::Poll` firing twice and stopping is the
thread to pull. Evidence and probe addresses in `notes/song-load-crash.md`.

**Retire `GHPC_STREAM_READY` with a real IOP answer.** Still the underlying
gap, and the poll-chain finding is more evidence for it: the stand-in forges the
result of draining a type 2 `StreamOp` without the op, and the state word it
writes reverts from 3 to 2 within three `IsReady` calls on the same object. The
EE's state 2 handler sends `StreamInfoArg` to the IOP as CTL 0x190 and waits for
CTL command 2 back, which `CtlDispatch_impl__7SynthEE` (0x3eb828, jump table
0x4eb680 entry 2) turns into `StreamEE::Dispatch(id, 2, 0)`. No IOP synth module
exists, so it never comes. Same missing-module class as the SPU handshake.

**Scoring for both is in place.** `progress.py` carries a `song_tick` sub-rung
from `[ghpc/song]`, so rung 9 no longer scores a win on its own: it reports
`song_tick`, `song_tick_from` and `song_tick_advanced`. Rung 9 with no
`song_tick` is the screen without the song. Audio is still unmeasured.

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
