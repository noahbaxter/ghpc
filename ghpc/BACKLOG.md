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

**R2: the Rnd seam.** Exit criteria in `NEXT.md`. R0 and R1 are done.

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

R2 is where the speed arrives, and everything it needs is already measured:

- Host mesh cache, 99% of gameplay draws find geometry host-side
  (`[ghpc/mesh/cache] hits=115314 misses=1024`). The misses are three named
  meshes, not a class.
- All four backend inputs captured and named in `notes/rnd-seam.md`: geometry,
  material, texture, camera.
- The vertex transform, solved to GS subpixel over six meshes and 171 matched
  pairs, dx and dy medians 0.03 to 0.04 px against a 0.0625 quantisation floor
  (`notes/evidence/2026-09-12-bone-palette.txt`):

      world = sum over b of weight[b] * (pos * Bone[b])   b = 0..3, skinned
      world = pos * World                                 unskinned, qw676..679
      clip  = world * M              M = qw700..703 as rows, row vectors
      q     = 1 / clip.w
      x,y,z = clip.{x,y,z} * q * qw696.{x,y,z} + qw697.{x,y,z}
      s,t   = uv * q

  Gameplay meshes are skinned. The weights are the Vert's four `Hmx::Color`
  floats at +0x20, summing to 1.0, indexing the palette `DrawShowing` uploads
  at qw660..675. qw698 is not subtracted: M carries the view transform.
- VU1 semantics read out of `.vutext`
  (`notes/evidence/2026-09-19-vu1-tl-semantics.md`). Three contradict the old
  MSCAL-era native draw and the Vulkan path must honour them: PRIM comes from
  the qw680 tag (ABE set for every blend mode except 1, FGE real), vertices are
  XYZF2 with fog rather than XYZ2, and colour is per-vertex lit rather than a
  0x80 constant. There is no backface culling anywhere in the 13,872 bytes, so
  a triangle list from `Face[]` is a correct substitute for the strips.

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
