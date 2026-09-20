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

**Save states, in flight and not yet usable.** `ps2xRuntime/src/lib/ghpc_state.cpp`
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

**R0: Vulkan bring-up.** SDL3 window, Vulkan instance, device, swapchain,
present a cleared frame, behind `GHPC_RENDERER=vulkan` with raylib as the
default. Both paths build.

raylib is OpenGL-only and cannot share a window with Vulkan. It is used in
exactly two files, `ps2_runtime.cpp` and `ps2_debug_panel.cpp`, behind the
`ps2_host_backend` INTERFACE target at `ps2xRuntime/CMakeLists.txt:240`. That
target is the seam a second host backend goes in beside.

**Exit:** a Vulkan window presents, and the game still boots unchanged on the
raylib path.

Settle in R0, not before: whether ImGui keeps the raylib path (rlImGui) or
moves to the Vulkan backend. The debug panel is worth keeping, and losing it for
one phase is acceptable if it buys a simpler R0.

## Next

**R1 blit parity, then R2 the Rnd seam.** Exit criteria in `NEXT.md`.

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

**Replace the oracle.** `progress.py` scores `eerate_pct`, which was right for
the VU1 phase and is wrong now. Needs frame rate on `game_screen`, audio chunks
flowing, and the song tick advancing against wall clock. Also fix the control
arm producing no `[eerate]` line at all, seen twice on release.

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
