# Backlog

## Next

**M4 fileio IOP service (SID 0x80000001).** In progress, see `notes/m4-progress.md`.

An instrumented service is registered and absorbing the calls
(`patches/0004`). GH2's request layout is captured: a 16-byte header followed by
an inline path. The game is asking for `cdrom0:\GEN\MAIN.HDR;1`.

- Implement fn=0x0c as open, with cdrom0 path translation.
- Identify read, lseek and close by observing subsequent calls.
- Done when the game reads `GEN/MAIN.HDR`, then `MAIN_0.ARK`.

## Subsequent

**M5: audio.** Not yet attempted. GH2's IOP audio is `LGAUD` plus Harmonix
`SYNTH_R`/`SYNTH_S`, undocumented, saved in `work/disc/IOP/`. Audio-to-input
timing accuracy is the primary unproven requirement.

**M6: rendering.** Target focus mode first, which disables world rendering. Still
requires GS for note highway, HUD and menus, largely textured quads rather than
3D venues. Move to Linux beforehand.

**M7: VU microcode.** ~28 KB across 18 named indexed `.DVP.overlay` sections with
an overlay table. Hand-port rather than writing a general VU recompiler;
prioritize by what the highway requires. All three titles report 18 overlays.

**M8: input.** HID guitar support, calibration, latency measurement. Measure
actual latency against a tuned PCSX2.

**GH1 and 80s.** Symbolized debug builds in `work/elf-debug/`. GH1's has 8,433
symbols against GH2's 12,663, suggesting an earlier prototype that may diverge
from retail. Verify before assuming parity.

## Deferred

- **Upstreaming.** Not until the port works. Patch 0003 (`isStubFunction`
  denylist) is the general fix, covering all four name-collision mechanisms.
  Patch 0001 (`GetRomName` bounds) addresses a memory-safety bug; the decision to
  ignore `$a1` was derived from one caller in one game and needs disclosing.
- **Audit the remaining 247 stubs** for the same collision class. GetRomName and
  the sceFs family were found by hitting them at runtime.
- **Decomp toolchain.** `DarkRTA/rb3` is a Rock Band 3 matching decomp at ~79% of
  cross-platform code on the same Milo engine. `Mikompilation/Himuro` is a PS2
  decomp of Fatal Frame using decompals binutils and the original 32-bit GCC.
  Both inform the replacement half of layer 2. Requires Linux with i386 multiarch.
- **Shipping target.** Development uses debug builds, which carry asserts and
  debug paths. Shipping target undecided.
- **ccache.** 5 GB default cap, sufficient now.

## References

- `ps2ResolveGuestPointer` never fails. Out-of-range addresses are masked back
  into RAM and it returns true, so `getMemPtr` cannot reject a bad pointer. Any
  syscall taking a guest length must bound it itself.
- The `File/CD` debug tab exposes a `cdImage` field that nothing sets. No CLI
  flag or env var exists for it; the game-override hook is the intended path.
  Relevant if the fileio service requires a disc image rather than a directory.

## From the M5 bring-up pass

- **DTA merge leaves a nested array in a script body.** `ExecuteScript` asserts on
  element 14 of a 241-element array: it is a `kDataArray` from
  `ui/manage_bands.dtb:225` sitting among commands from `ui/ui.dtb:54`. An
  include or merge placed the other file's content in without splicing its
  elements. Full structure offsets, type constants and addresses in
  `notes/m5-progress.md`. This is the only thing between the current build and
  the UI drawing.
- **Debug scaffolding in generated files is fragile.** The ARK-path probes and
  the `GHPC_SKIP_MODAL` lever live in `ps2xRuntime/src/runner/*.cpp` and are
  destroyed by `./scripts/build.sh --from=recomp`. Everything else is behind
  `PS2X_GHPC_DIAG` in patches 0004-0013 and survives. If the probes become
  permanently useful, move them into the recompiler's emitter instead.
- **ccache is capped at 5 GB and barely used.** Far too small for ~12.7k
  generated objects, which is why full rebuilds never hit cache. `ccache -M 25G`
  would help every build, especially now that debug and release are separate
  trees.
- **Pad input is never delivered.** The USB keyboard service reports one device
  so `sceUsbKbInit` succeeds, but no keystrokes are ever produced, and no
  controller input reaches the game either. Harmonix debug builds drive their
  in-game debug menus from the keyboard, so wiring host keys through would
  unlock GH2's own debug tools.
- **VU1 is loaded but never executed.** VIF1 receives 18 MPG microprogram
  uploads and 21 UNPACKs but zero MSCAL. Expected while the game is not drawing;
  revisit only once the UI script runs, otherwise it is a false lead.

## From the M6 rendering pass

- **Do not chase the IPU.** `ps2xRuntime/src/lib/Kernel/Stubs/IPU.cpp` is 108
  lines that fake the init handshake and implement no BDEC/IDEC/VDEC/CSC, and
  the DMA channels for it (`0x1000B000` ch3, `0x1000B400` ch4) have no handling
  in `ps2_memory.cpp` at all. None of that matters: instrumenting IPU register
  writes and channel starts over a 90 s run gives **zero** game-originated
  traffic. The only 8 IPU register writes are `completeIpuInit`'s own canned
  sequence. The boot logos are not video. A full-screen `(0, 128, 0)` green is
  exactly YCbCr->RGB of an all-zero input, which is a coincidence that looks
  like undecoded video and is not.
- **Several GHPC_DIAG counters are lies.** Read the emit condition before
  believing any of them:
  - `[frame]` logs the first 8 frames then only on `nonBlack` change
    (`ps2_runtime.cpp`), so "8 frames" never meant 8 frames.
  - `[gif] submit` was capped at 12, `[gs/stat]` at 6, `[vif1] data` at 8.
  - `gifPackets` / `imageUploads` in `[gs/stat]` sit on
    `GS::processNativePackedGIFPacket` and `tryProcessNativeImageUploadPacket`.
    `processGIFPacket` (`gs_frontend.cpp:681`) never calls the former, so those
    two read 0 no matter how much is drawn. Either wire them up or delete them.
  Prefer time-based counters on the path actually taken.
- **`ps2_vif1_interpreter.cpp` had `imm`/`num`/`irq` and the `vif1_regs.code/
  num/stat` updates inside a `#if GHPC_DIAG` block.** Non-diag builds did not
  compile, and would have been semantically wrong if they had. Fixed in the
  working tree by closing the guard around the histogram only. **Now captured
  as `patches/0016-vif1-diag-guard-scope.patch`.**
- **`Splash::Wait` bounded-wait workaround is probably obsolete.** It spins on
  COP0 `Count` accumulating into `[this+0x10]`, which appeared to wrap at 2^24.
  That was not arithmetic truncation, it was the alarm/IRQ callback writing
  through the interrupted thread's live stack frame. With the stale-`$sp` fix
  the accumulator should telescope correctly, so try deleting the
  `GHPC_SPLASHWAIT_MAX` bound and confirm `Wait` terminates on its own.
- **Reference stills of the real boot sequence** are in `reference/` at the repo
  root, in order, described by `notes/boot-sequence-reference.md`. Use them to
  tell "wrong pixels" apart from "wrong sequence" before theorising.

## Build and measurement gotchas

- **`scripts/build.sh --from=build` does not reliably pick up an edit on the
  first invocation.** Verified: a new string was present in the source, the
  first build reported success without it reaching the binary, a second build
  included it. Always build twice, or confirm with
  `strings build-debug/ps2xRuntime/ps2EntryRunner | grep <marker>` before
  concluding a change had no effect.
- **`scripts/checkrun.sh` asserts the boot still progresses** (`tme=17430
  notme=2933` from `[gs/draw]`). Those two counters measure whether the game is
  rendering at all, independent of whatever probe is under investigation. Run it
  before trusting any other number in a run. A regression went unnoticed for
  roughly eight iterations because probe output was compared against
  expectations rather than against this baseline.
- **Do not re-add a GIFtag size guard in `processGIFPacket`.** Rejecting PACKED
  or REGLIST tags whose declared size exceeds the bytes remaining looks correct
  and cleans up bad TEX0 counts, but it rejects legitimate packets and drops
  textured draws from 17430 to 4. Tried and reverted.
- **Do not trust decoder metadata or hand-decoded VU1 instruction fields.** The
  opcode field extracted as bits 31-25 is not the interpreter's opcode (XGKICK
  reads 0x40 that way, the interpreter matches 0x6C). Field extraction should
  come from `ps2_vu1_detail.h`, or better, be printed by the interpreter itself.
- **`strings <binary> | grep <marker>` is NOT a build-freshness check.** It
  silently passes for probes whose source no longer exists. `--from=stage` runs
  `rsync -a --delete work/output/ -> src/runner/`, and because `rsync -a`
  restores the source's older mtimes, ninja judges the reverted TUs up to date
  and never recompiles them. The stale object keeps getting relinked, so the
  marker stays in the binary with no source behind it. Verified: the entire
  `[dta]` probe surface lived only in `unity_48_cxx.cxx.o` (mtime 18:03) while
  the binary linked at 23:40 and no source file in the repo contained the
  literals. Compare source and object mtimes, or force a rebuild of the TU.
- **Probes written into `work/output` can never be exported.** That tree is
  regenerated by `--from=recomp` and rsynced over by `--from=stage`: two sinks,
  no source of truth. Diagnostics belong in PS2Recomp proper behind
  `PS2X_GHPC_DIAG`, where they live in a real file and can become a patch. If a
  probe needs to sit inside a generated function body, the hook belongs in the
  recompiler or in `ps2_log.h`, not in the generated output.
- **M6 caveat: video is a second hardware seam.** Replacing the renderer at the
  Rnd layer does not cover it. `StartDecode__7PsMovie` (0x1c4248),
  `Decode__5Video` (0x255430), `ProcessPicture__5Video` and `PlayMovie__FPCcf`
  drive IPU and DMA directly outside Rnd, 22 register sites across 7 functions,
  plus the `sceMpeg*`/`sceIpu*` library beneath. Intro and cutscene video needs
  a working IPU or a native decoder substituted at `PsMovie`/`Video`.
- **`checkrun.sh`'s baseline silently depends on memory card state on disk, not
  just on code.** The game writes a save to `work/mc0/BASLUS-21447/` once it
  reaches the main menu. On the next run it finds that save, takes a different
  boot path, and the baseline moves to `tme=1214 notme=4272`, which reads
  exactly like a rendering regression and is not one. Verified by A/B: two
  consecutive runs gave 1214/4272 identically, and moving `work/mc0` aside
  restored `17430/2933` and `baseline OK` on the next run. Before treating a
  moved baseline as a regression, check whether `work/mc0` exists. The 17430
  figure is only valid for a virgin card.
- **Open question: the distribution story assumes a debug ELF the user cannot
  have.** `scripts/build.sh:21` builds from
  `third_party/milo-executable-library/gh2/PS2 Final Debug/SLUS_214.47`, the
  symbolized debug executable, not the retail executable on a disc. That ELF is
  not on the retail disc and a user cannot produce it from their own copy, so
  "point the tool at a disc and get a native binary" is not true today for
  anyone but Noah. The existing "shipping target undecided" entry covers part of
  this, but the distribution framing has been assuming the question is settled.
  Either the project eventually retargets the retail ELF, which costs all 12663
  symbols and changes how every stage works, or the one-command story needs a
  different shape. Decide before any public distribution claim.
- **ISO-derived output lives in two ignored places, not one.** `work/` and
  `ps2xRuntime/src/runner/`. State both wherever the "nothing game-derived is
  committed" rule is written down, so nobody trusts a simplified version of it.
  The generated function table was tracked upstream despite the second rule and
  had to be untracked explicitly; gitignore does not apply to tracked files.
