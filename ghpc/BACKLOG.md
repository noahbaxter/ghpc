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

**`dtb.py` cannot walk a node tree.** Two defects, both verified against the ARK.
`TYPES` omits the preprocessor tags (32 define, 33 include, 34 merge, 35 ifndef,
36 autorun, 37 undef), and `else`/`endif` carry a 4-byte payload rather than
none, so the reader desyncs on any file using `#ifdef`. `dump()` is a stub that
prints only the header. With both fixed, all 179 ARK DTBs parse byte-exact with
zero trailing bytes, which makes the tool usable as a reference parser to diff
runtime `DataArray` state against.

**Probes only exist in gitignored generated output.** Diagnostics added to
`ps2xRuntime/src/runner/*.cpp` are deleted by any `--from=recompile`, and there
is no injection step in `build.sh`. This is the same failure mode as the lost
`DataArray` workaround: a finding that cannot be reproduced because the code that
produced it is gone. Needs a mechanism (a probe overlay applied post-generation,
or handlers bound by symbol) before any further probe-driven debugging.

**Rebaseline `checkrun.sh`.** `EXPECT_TME=17430 EXPECT_NOTME=2933` came from the
binary carrying the stale-object `DataArray` workaround, so it reports
"BASELINE MOVED" on every run and is currently noise.

**DMA channel 9 (toSPR) was never implemented, and that was the rendering
blocker.** `PsMesh::DrawFaces` (0x43eea0) walks its face data in RAM a chunk at
a time. Each loop iteration reads the scratchpad packet cursor at 0x70000008,
calls `DmaPacket::Send(localPacket, 9, 0, cursor)` to move the chunk RAM ->
scratchpad, advances the cursor by the chunk size (`sw $16, 0x8($1)` at
0x43efec), then `PsRnd::FlushPacket` kicks channel 8 to push the buffer into the
VIF1 MFIFO ring. `DmaPacket::PreSend` (0x43e600) sets MADR = RAM source,
SADR = scratchpad destination, QWC = quadwords, CHCR = 0x101, normal mode.
Every quadword the mesh path draws arrives that way; no CPU store ever touches
it, which is why the `[sprbuild]` store trace saw nothing land in spr
0x2d0-0x390. `ps2_memory.cpp` had no code for channel base 0x1000D400 at all, so
the buffer kept the previous frame's contents and the ring got stale floats
where the tag chain should continue.

This supersedes the "game patches tags after the kick" theory. The late
`CloseDmaTag` store at 0x43e838 is real but harmless: in the DrawFaces loop the
tag opened by `FlushPacket`'s `OpenDmaTag` is rewound and discarded by the next
`CloseDmaTag(id=1)` (0x43e7c4, `mOpenTag == mCursor - 0x10` case), so the packet
sent is raw pre-built face data with its own tag chain, not a CPU-built one.
Deferring the fromSPR copy was chasing a non-problem.

Implementing channel 9 as an immediate RAM -> scratchpad copy fixed it. Two runs,
both: `STALL count=0`, zero `NOT A TAG`, `[fill]` past #1800 (diag window cap,
previously wedged at #1789), `[vu1] pktLen ok=956000 tooLong=0 tooShort=0`,
`regs437f=0`. The blue-stripe garbage screen is gone.

**Still black after the RedOctane logo, and presentation is ruled out.** Raw
PPM dumps of both framebuffers read straight out of VRAM (`[gs/snap] wrote
/tmp/ghpc_vram_fbp{0,56}_NN.ppm`), bypassing the presentation path entirely:
dumps 00-01 hold 9639 non-black pixels in both buffers (the RedOctane logo),
dumps 02 onward hold **0 non-black pixels in both**. So the framebuffers really
are black. It is not an FBP mismatch, not double-buffer selection, not the
latch. Do not go looking there again.

What is actually happening: geometry is rasterized, and it is black.
`fragments=3738140 wroteFb=3738140 ofWhichBlack=3489480`, with
`reject scissor=0 alpha=0 dstAlpha=0 ztest=0`. Nothing is clipped or tested
away; 93% of everything written is black, and the rest gets overdrawn. Only
4 textured draws happen in an entire run (`tme=4 notme=256`), all four with
splash 1's TEX0 (`5408,4,0x13,8,7`). The Activision (PSMT4 512x128) and
Harmonix (PSMT8 512x64) textures upload into VRAM correctly and are never
sampled: `clutReads` freezes at 272496 and the `rawIdx` histogram is
byte-identical in every later report. So the open question is narrow: **why is
the vertex colour zero for essentially all post-splash geometry, and why is
TEX0 never programmed again?**

**GIFtag FLG=3 (DISABLE) was treated as a fault in the VU1 XGKICK path.**
`ps2_vu1_core.cpp` `progressXgkick()` had no case for FLG=3 and fell into
`reportReservedInstruction(false, 0xFFFFFFF8u)`, which cleared
`m_xgkick.active` and set `m_stopRequested`, killing the whole VU1
microprogram partway through. DISABLE is a legal mode with the same quadword
count as IMAGE; `gs_frontend` has no branch for it and correctly discards the
data. Sizing it like IMAGE removed all 10020+ aborts per run (`reserved=0`).

Note for whoever picks this up: that sentinel is not an instruction word.
`0xFFFFFFF8` and friends are per-call-site markers, one per unimplemented case
in `ps2_vu1_core.cpp`. Decode `[VU1 reserved ...] instruction=0x...` by grepping
for the constant, not by treating it as microcode.

**Two verified fixes, no visible change.** Both the toSPR channel 9 fix and the
DISABLE fix are real and confirmed by their own metrics, and neither put a new
pixel on screen. `fragments` and `ofWhichBlack` are byte-identical before and
after the DISABLE fix, so the drawn workload is deterministic and unchanged by
it. Resist stacking a third speculative fix; find the zero-colour source first.

**Intermittent hang right after splash 1, roughly 2 runs in 12.** Runs 9 and 12
both produced exactly 138 log lines and then nothing for a further five minutes,
while other runs of the same binaries emitted 100k+ lines in the same window.
Both stalled at an identical point: immediately after splash 1's first VRAM
snapshot, last line `[gs/copy] reading fbp=0 fbw=8 psm=0x2 512x448 basePages=1
localLayout=1 origin=(0,0)`. The tails are byte-identical apart from the PID.
Not a regression from this session's changes: run 9 and run 10 were the same
binary, one stalled and one ran fine. Practical consequence for anyone
debugging here: a run that returns no probe output may have hit this rather
than disproved anything, so check the line count before reading a null result
as evidence.

**VU1 double buffering: BASE is 932 for one pass, and BASE+OFST wraps.** MSCAL
logging keyed to changes of the (base, ofst) pair, rather than the old
first-ten-kicks cap that could never see the failing pass, gives only three
combinations: `base=0 ofst=330` (TOPS 0 and 330, a sane layout) and `base=932
ofst=330`, where TOPS for the second half is `(932 + 330) & 0x3FF` = **238**, a
10-bit wraparound. Both of the bad TOP values seen in the offscreen tally, 932
at 51.6% bad and 238 at 32.8%, come from that one `base=932` pass; the `base=0`
pass sits at 27-30%. A buffer based at 932 has only 92 quadwords before the end
of VU1 data memory.

`[vu1] UNPACK dest regions` confirms transfers run past the end: `wrapped=7775
maxEndQw=1041` out of 310000 unpacks, so up to 17 quadwords land at addresses
0-16 instead, which is where transform constants live. That gives correct vertex
colours with wrong positions, matching the symptom exactly.

Two cautions for whoever continues this. First, `maxEndQw` is a **lower bound**:
it was computed as `vuAddr + writeVectorCount`, but when CL >= WL the write loop
spans `(num / wl) * cl + (wl - 1)`, which exceeds `num` whenever CL > WL, so
skipping-write transfers reach further than counted. Second, wrapping at
`& 0x3FF` is probably correct hardware behaviour, since VU1 unpack addresses are
10 bits. The wrap is therefore the consequence, not the bug; something upstream
is supplying an address or length it should not. The guard
`if (destOff + 16u > PS2_VU1_DATA_SIZE) continue;` in the unpack write loop is
genuine dead code (after the mask, `destOff + 16` maxes at exactly 16384 and is
never greater), but silencing the writes there would only hide the upstream
fault.

**ADC suppression is the biggest single lever on rendering.** With the ADC bit
honoured, 97% of vertex kicks are suppressed (`set=5347711 clear=152289`) and
the screen is black with occasional flashing triangles. Forcing every kick to
draw (`GHPC_IGNORE_ADC=1`, gs_frontend.cpp:1101) changes it to **94% screen
coverage, 216k distinct colours, and the flicker disappears**: `black=1
content=364` frames versus roughly 50/50 before. The output is still wrong,
horizontal smeared bands rather than a picture, but it is real geometry
rendering continuously instead of nothing. This is the strongest lead for the
next session.

Unresolved and important: it is NOT established whether our ADC read is correct.
Both readings fit the data equally. Either the bit is read correctly and the
geometry is independently broken, or the bit is misread and the geometry is also
broken. The smear cannot distinguish them. **The way to settle it is the PCSX2
reference already cloned at `third_party/pcsx2/pcsx2/GS/GSState.cpp:1438-1460`,
which has the packed XYZF2/XYZ2 ADC handling to diff against.** That comparison
was never done.

Bit layout observed: `hi=0x00008ff0000fbe84`, decoding as F=255, ADC=1, unused
bits clean, which is a well-formed XYZF2 quadword. F=255 on essentially every
vertex is itself odd and worth questioning.

**Clip flags: 99.9% of CLIP executions trip at least one plane, never all six.**
`w` is a sane 768 and the denormal fallback in ps2_vu1_upper.cpp:388 fires on
only 0.005% of calls, so that fallback is not the cause. Early sampled vertices
(the splash quads) come back `flags=0x00`, correctly unclipped, with sane
coordinates. Whichever single plane trips on almost everything was being
measured when the session ended; the per-plane histogram in ps2_vu1_upper.cpp
was built but its run never read back.

**The splash logo quads transform correctly.** A dumped packet gave four
vertices forming a clean axis-aligned rectangle spanning x 127-385, y 158-290 on
a 512x448 screen, which is exactly a centred logo quad. Whatever stops splash 2
and 3 appearing, it is not their vertex transform. Do not re-chase that.

**Counters in this codebase that mislead. Verify sampling before trusting any
aggregate.** Three cost real time this session:
- `ofWhichBlack` in the `[gs/draw]` line counts the game's full-screen black
  clear sprites, so it reads ~93% no matter what. It is not evidence about
  geometry colour.
- `tme=` / `notme=` are never reset but their printer `report()` is only called
  from `ArmTransfer` and `UploadImage`, which stop firing after early boot. The
  last line in a log is an early snapshot, not a total. `tme=4 notme=256` is
  meaningless as a run total; DrawPrimitive actually runs 40k+ times.
- `[vu1] UNPACK dest regions` truncated its bucket list at 10 of 13, hiding 61%
  of all unpack traffic including the region that mattered.

**Intermittent hang, roughly 1 run in 6.** The run stalls right after splash 1
at exactly 138 log lines, last line `[gs/copy] reading fbp=0 ...`. Check the
line count before reading a null probe result as evidence of absence.

## Splash rendering: root cause localized (session findings)

**Result: all three splash logos and the save screen now render** with a
diagnostic override in place (`GHPC_ZERO_PROJ_X=1`). The override is NOT a fix,
it just proves nothing else blocks the splash path.

### Ruled out conclusively
- **ADC read is correct.** Ours is bit 111 (`(hi >> 47) & 1`); PCSX2's
  `GSRegs.h:1053` `Skip()` is `U32[3] & 0x8000`, also bit 111. F (100-107) and
  Z (68-91) match too. The 97% ADC-suppression figure was a *symptom*, not a bug.
- **CLIP matches PCSX2** `_vuCLIP` (`VUops.cpp:909-924`) exactly, including the
  `clipflag <<= 6` shift-register accumulation.
- **XYOFFSET, rasteriser, texture, alpha test are all fine.** The logo draw
  (`tbp0=0x1520`, PSMT8, CLUT `0x1500`) writes 5.4M fragments with real colours.
- VU1 opcode decode for the extended `0x3C..0x3F` group matches PCSX2's
  `_UPPER_FD_xx` tables.

### The actual defect
VU1 loads a projection matrix from VU data qwords 700-703 (`0x2bc0..0x2bff`)
into vf01/vf07/vf08/vf09 (LQ at pc `0x0d38/0x0d40/0x0d48/0x0d68`, `dest=0xF`),
then composes projection x camera at pc `0x0d58/0x0d60/0x0d68`.

Lane x of qwords 701-703 arrives as garbage where 0 is expected:

    qw700 = (0.307812, 0, 0, 0)          correct
    qw701 = (400.171, 0, 2.33333, 1)     lane x should be 0
    qw702 = (400.171, -0.359114, 0, 0)   lane x should be 0
    qw703 = (307731, 0, 458.667, 768)    lane x should be 0

With lane x zeroed the composed matrix is a clean projection:
`out.x=0.307812*cam.x, out.y=-0.359114*cam.z, out.z=2.33333*cam.y+458.667,
out.w=cam.y+768`. With the garbage, `out.x` picks up `400.171*cam.y -
400.171*cam.z + 307731`, so x comes out ~3.2e5 against w=768, every vertex
trips the +x clip plane, the microprogram sets ADC, and 99.99% of tristrip
vertices are suppressed. That is the whole black-screen chain.

### Where it comes from
The VIF unpack is faithful. Run-wide census of every unpack touching the block:

    cmd=0x6c m=0 mode=0 mask=0xc0c0c0c0 num=6 addr=698 -> ok=45755 bad=27406

One single VIFcode shape (V4-32, mask off) produces both good and bad data, so
the source bytes themselves differ. `srcBase` is not inside `m_rdram`
(guest addr reports `0xffffffff`), meaning the packet arrives via the
**flattened-chain feed** - the same path as the known BASE-desync issue.

### The chain flattener is NOT at fault
Raw byte windows around the unpack were compared for an ok and a bad upload.
Both have byte-identical VIFcode structure: `0x10` FLUSHE, `0x20` STMASK
(`mask=0xc0c0c0c0`), `0x7c` masked V4-32 (2 vectors at qw696, the guard band),
then `0x6c` unmasked V4-32 (6 vectors at qw698). Only the data differs - the ok
upload is an ortho setup (guard band +/-32767.5, w=1), the bad one perspective
(w=768). So the bad upload is the *perspective* projection.

Scanning the whole flattened chain for the offending bit patterns
(`0x43c815de` = 400.171, `0x4896426b` = 307731) finds them **only** at the
projection offsets 0x70/0x80/0x90 and nowhere else, across chains of different
sizes (0x8480 and 0x350). Nothing is being mis-placed; the flattener copies
faithfully.

### Traced to EE memory
Mapping flattened-chain offsets back through `appendData` gives the EE source:

    chain 0x70 -> ee=0x00b013a0   (and 0x00b01a50 on the other buffer)
    chain 0x80 -> ee=0x00b013b0
    chain 0x90 -> ee=0x00b013c0

Two buffers ~0x6b0 apart, so the EE builds this packet double-buffered. The
corrupt words are already wrong in EE RAM before DMA reads them.

### Next step
Find the guest code that writes `0x00b013a0`+0/0x10/0x20 lane x. The VIF/VU
layers are exhausted; this needs an EE-side write watchpoint (mprotect on the
`m_rdram` page plus `backtrace()` in the fault handler will name the
recompiled function). GH2 does matrix math in VU0 macro mode, so COP2 - and
specifically whatever computes the perspective (vs ortho) setup - is the prime
suspect, since the ortho path writes lane x = 0 correctly.

### Current tree state
`ps2_vif1_interpreter.cpp` carries a **diagnostic override, on by default**,
that zeroes lane x on writes to qw701..703. It is not a fix. Set
`GHPC_NO_PROJ_HACK=1` to see the raw broken behaviour. Remove it once the EE
writer is found.

## Next: text placement on the loading/save screen

Goal: get the text drawn in the right place. Currently it sits too far right
and is clipped at the screen edge, so only the left part of each line is
visible ("SA...", "pres...", "Aut...").

**Likely connected to the projection override.** The diagnostic hack forces
lane x of qw701..703 to zero, and qw703 lane x is the projection's
**x-translation** (307731 in the raw data). Zeroing a translation-x term is
exactly what would slide a 2D overlay horizontally off position. So the text
may be misplaced *because of* the hack rather than independently. Check this
first, it is one run:

    GHPC_NO_PROJ_HACK=1   # compare text position with the hack off

If the text lands differently, the two problems are the same problem and
finding the real EE-side value fixes both. If it lands in the same wrong spot,
it is a separate issue.

**Tooling already in place** (all behind `GHPC_DIAG`):
- `[gs/submit]` in `gs_frontend.cpp` logs every batch reaching the backend with
  its bbox, tbp0/psm/tw/th, alpha, test and frame regs. Filter it to the text
  texture to get the quad's screen rect directly.
- Screen coords are `vertex.xy - (OFX,OFY)`; OFX/OFY were 1792/1824 for the
  splash. Text drawn in the other GS context (`prim.ctxt`) can carry a
  different XYOFFSET, so check which context the text batches use.
- `[gs/pixel]` watches a single framebuffer pixel in draw order, useful for
  confirming what actually lands where.

Also still open from tonight: the save screen flickers between two states.
Present both with and without the override, so it is independent of the
projection bug. Probably double-buffer selection or the present latch.

## Text placement: first step done, the two problems are one problem

`GHPC_NO_PROJ_HACK=1` was run against the same build as the default run. Result
is unambiguous: with the override off there is no save screen at all. Every
frame is the degenerate shear (frame 9 of that run: a white/red fan collapsing
to a single point at roughly (256,215), black everywhere else), the same
signature as the pre-override black screen. Nothing recognisable is drawn, so
the text cannot be "in the same wrong place" and is not independently
misplaced.

Conclusion: **the text sits too far right because of the override**, not
alongside it. Fixing the projection fixes the text. There is no separate text
placement bug to chase.

Numbers from the default (override on) run, save screen, frame 19:
- Text glyph triangles: `tbp0=6432`, `prim=4`, `fst=0`, screen x 521..625,
  y 128..167, so 9 to 113 pixels past the right edge of a 512 wide frame.
- Splash quads for comparison: GS x 1854..2241 with `ofx=1792`, i.e. screen
  62..449, correctly centred on 2048.
- Framebuffer is genuinely 512 wide: `fbw=8`, `scissor=(0,0)-(511,447)`. This
  is not a 640 wide image being cropped.
- Visible text is ragged on the left and cut at x=512, which reads as centred
  lines whose centre is off screen to the right, not a left aligned block.

The override zeroes lane x on any write to qw701..703 whose magnitude exceeds
1.0. That is indiscriminate: any UI or ortho matrix with a legitimate x
translation gets flattened too. A census keyed on (qword, exact bits) is now in
`ps2_vif1_interpreter.cpp` under `[projzero]`, and the capped `[gs/geom]` log
in `gs_cpu_backend.cpp` has been replaced by a run wide `[gs/geomcensus]`
bucketed by texture base with screen space bboxes, since the old cap of 12
could only ever show the first few draws of a run.

## The corrupt projection words are written by the EE, and they are everywhere

The `[ee/watch]` hook in `EeScheduler.cpp` was repointed from its old address to
the projection packet and made configurable (`GHPC_EE_WATCH`,
`GHPC_EE_WATCH_LEN`). It compares the watched bytes before and after every
dispatched guest function, so it names the dispatch that changed them. It now
also sweeps all 32 MB of RDRAM for the two distinctive words every 100k
dispatches (`[ee/scan]`), because the watch address came from one session's
chain mapping and needed confirming rather than assuming.

**The first write to 0x00b013a0 lays down the corrupt matrix whole:**

    [ee/watch] addr=0x00b013a0 len=48 writes=1 writers=1
      entry=0x0043f674 n=1 badAfter=1
      +0x00 = (400.171, 0, 2.33333, 1)
      +0x10 = (400.171, -0.359114, 0, 0)
      +0x20 = (307731, 0, 458.667, 768)

`0x0043f674` falls inside `Draw__11RndDrawableQ211RndDrawable8DrawType`
(`RndDrawable::Draw`, 0x0043f5b8, 428 bytes). That is dispatch granularity, so
the store itself may be in a callee, but the render path is confirmed and lane x
is not stale memory: something computes and stores it.

0x00b013a0 is inside the VIF1 MFIFO ring (`base=0xb00000 mask=0x7fff0`), so it
is a scratch destination that many unrelated writers reuse. Later `[ee/watch]`
entries are other packets, not the matrix.

**The RAM sweep is the more interesting result.**

    0x4896426b (307731) : 47 hits. All but one are ring copies at 0x00b0xxxx,
                          spaced 0x2cc0. The one outlier is 0x00aa8330.
    0x43c815de (400.171): 425 hits. 0x004fa270 and 0x004fa280 are adjacent
                          columns of one matrix; from 0x007a3580 onward they
                          repeat in a regular object layout (offsets 0, +0x80,
                          +0xc0 within an object, objects at stride 0x190).

So 0x00aa8330 is the master copy the ring gets filled from, and 400.171 is not a
one-off: it sits in hundreds of live game objects at a consistent offset.

**That changes the reading of the defect.** A value present 425 times at a fixed
structure offset is not computational garbage. Two possibilities remain, and
they need different fixes:

1. Lane x genuinely holds 400.171 and VU1 is not supposed to fold it into the
   transform. If the microprogram loads these columns with a dest mask of yzw
   and our LQ ignores the mask, lane x would be polluted with a value hardware
   never reads. The override would then be masking an LQ dest bug, and the text
   offset would be a second consequence of the same bug.
2. Lane x is correct and correctly loaded, and the compose step at
   0x0d58..0x0d68 is what mishandles it.

Exact arithmetic favours reading it as a real projection term, not noise:
0x4896426b is 307731.34375, and 400.17083740234375 x 769 is 307731.374, i.e.
the constant is the centre term times 769 to float precision, where the w row
is `cam.y + 768`. A centre-offset projection wants `cx*768` there and `0` in the
cam.z column; what is actually present is one extra `cx` in both. Whether that
extra term is real hardware data or an artifact of how it is loaded is exactly
what distinguishes case 1 from case 2.

Next: dump the VU1 microcode words for 0x0d00..0x0d90 (`GHPC_VU1_DUMP=0xd00:32`,
now in `ps2_vu1_core.cpp`) and decode the dest fields of the LQ and compose
instructions directly, rather than inferring them from a partial trace.

## Root cause: SQRT.S and RSQRT.S read the wrong source register

The chase ended in the recompiler, not the GS, VIF or VU.

**Chain of evidence.** The VU1 microcode dump (`GHPC_VU1_DUMP`, decoded against
PCSX2's opcode tables) shows the transform is an ordinary 4x4 compose with no
lane masking anywhere:

    0x0d38  LQ.xyzw vf01, 700(vi00)      ; projection columns
    0x0d40  LQ.xyzw vf07, 701(vi00)
    0x0d48  LQ.xyzw vf08, 702(vi00)
    0x0d68  LQ.xyzw vf09, 703(vi00)
    0x0d58  MULAx.xyzw   ACC  = vf01 * vf03.x
    0x0d60  MADDAy.xyzw  ACC += vf07 * vf03.y
    0x0d68  MADDz.xyzw   vf03 = ACC + vf08 * vf03.z
    ... and for the translation row, MADDw.xyzw vf06 = ACC + vf09 * vf00.w

Every dest field is `xyzw`, so lane x is loaded and used exactly like the
others. The microprogram is not misreading anything: the data really is wrong.

Following the data back: the packet's lane x is copied from the `RndCam`
projection matrix, written by `Set__7Frustumffff` (`Frustum::Set`, 0x32cc60),
reached from `RndCam::UpdateLocal` -> `RndCam::SetFrustum`. Inside `Frustum::Set`
is a plain 2D normalise:

    0x32cd50  mul.s  $f0, $f0, $f0
    0x32cd54  mul.s  $f1, $f1, $f1
    0x32cd58  add.s  $f12, $f1, $f0      ; f12 = f0^2 + f1^2
    0x32cd5c  sqrt.s $f0, $f12           ; f0  = length
    0x32cd80  div.s  $f2, $f24, $f0      ; f2  = 1 / length

and the recompiler emitted `ctx->f[0] = FPU_SQRT_S(ctx->f[0]);` - the source is
`fs`, but on the R5900 `SQRT.S fd, ft` takes its operand from **ft**. `fs` is
zero in the encoding, so every SQRT.S in the game was computing
`sqrt(f0)` instead of `sqrt(ft)`. Here that turns a vector length into
`|f0|`, the normalise divides by the wrong number, and the frustum planes come
out skewed. That is where 400.171 comes from.

PCSX2 `FPU.cpp:359` confirms the semantics: `SQRT_S` reads `_FtValUl_` and
writes `_FdValf_`. `RSQRT_S` (line 339) is `fd = fs / sqrt(ft)`, and our
translation had that wrong too, emitting `1.0f / sqrtf(fs)`: wrong numerator and
wrong operand. Both are fixed in `ps2xRecomp/src/lib/fpu_translator.cpp`.

This is a whole-game correctness fix, not a rendering one. Every square root and
every reciprocal square root in the recompiled binary was wrong, so anything
built on a vector normalise, a distance, or a magnitude was wrong: physics,
camera framing, note spacing, audio panning. The projection matrix is just where
it happened to be visible.

Requires a full recompile (`build.sh` with no `--from`), not a rebuild, since
the fix changes generated C++.

## Root cause found and fixed: VU0 vf00 was writable

The SQRT.S fix above is real, but it was not this bug. After it landed the
projection words were unchanged (`laneX=400.171` still), and the frame was still
the degenerate shear. Keep the fix; it is a separate correctness bug.

The actual cause came out of a value-filtered store watch. `ps2TraceGuestWrite`
already sits on every non-special guest store, so `GHPC_WATCH_VALUE=0x43c815de`
turned it into "name the instruction that stores this word", which beats
watching an address the DMA ring keeps moving. It printed:

    [valwatch] NEW pc=0x001d87f0 WRITE128 addr=0x00aa7c50 lo=0x0000000043c815de hi=0x3f80000000000000

`0x001d87f0` is `sqc2 $vf0, 0x0($v0)` inside `RndCam::UpdateLocal`, one of eight
consecutive `sqc2 $vf0` stores that clear the camera's matrix slots. The value
being stored was `(400.171, 0, 0, 1.0)`. **vf00 was not (0,0,0,1).**

Searching the generated code for writes to vf00 gave exactly one function,
`ps2___gt__FRC6SphereRC7Frustum` (`Sphere > Frustum`, 0x32cec8):

    { __m128 res = PS2_VADD(ctx->vu0_vf[3], broadcast(vu0_vf[1].x));
      __m128i mask = _mm_set_epi32(0, 0, 0, -1);
      ctx->vu0_vf[0] = _mm_blendv_ps(ctx->vu0_vf[0], res, mask); }

That is `vaddx.x $vf00, $vf03, $vf01x` and five more like it: the sphere/frustum
test computes plane distances into vf00 purely for the MAC flag side effect. On
hardware **vf00 is hardwired to (0,0,0,1) and writes to it are discarded**, so
the idiom is free. Our recompiler wrote it, so the first frustum cull of the
frame left the plane distance sitting in vf00.x, and from then on every
`sqc2 $vf0` in the game (a very common "store the zero vector" idiom) wrote
`(400.171, 0, 0, 1)`. That is how one value ended up in 425 places in RAM at a
consistent structure offset, and how it reached the projection matrix.

**Fix.** `R5900Context::vu0_vf` is now 33 entries; slot 32 is a sink. A
`codegen::vfDst()` helper in the recompiler maps a vector-float destination of 0
to slot 32, applied to every destination site: the 27 `vfd = inst.sa` ops in
`vu_translation_helpers.cpp`, QMTC2 / VABS / VMOVE / VMR32 in
`vu_translator.cpp`, and LDC2 in `instruction_translator.cpp`. The VU0 state
sync in `ps2_runtime.cpp` already rewrote vf00 to (0,0,0,1) on every crossing,
which is why the corruption looked intermittent rather than permanent.

**Result, with `GHPC_NO_PROJ_HACK=1`, i.e. the override off:** every projection
upload reports `laneX=0`, zero BAD lines out of 20, and the save screen renders
correctly with the text in the right place. "SAVE NOT FOUND" is centred on the
poster, the body text is fully on screen, YES/NO and the SELECT / UP/DOWN bar
are all where they belong. Both of this session's goals fall out of the one fix:
the text was never independently misplaced.

The override in `ps2_vif1_interpreter.cpp` has been deleted.

### Tree state after this session

Kept, all real fixes:
- `ps2xRecomp` vf00 sink (`codegen::vfDst`) plus the 33rd `vu0_vf` slot.
- `ps2xRecomp` SQRT.S / RSQRT.S source-register fix.
- `ps2_memory.cpp` DMA channel 9 (toSPR), previously absent.
- `ps2_vu1_core.cpp` `progressXgkick`, GIFtag FLG=3 no longer a fault.
- `ps2_vif1_interpreter.cpp` guard rejecting malformed BASE VIFcodes.

Removed, having served their purpose: the projection override itself and every
probe built to chase it - `[mtx]`, `[mtx/tops]`, `[projcensus]`,
`[projsrcaddr]`, `[projaddr]`, `[proju]`, `[projraw]`, `[projsrc]`, `[projee]`,
`[projzero]`.

Kept as general tools, all behind `GHPC_DIAG`:
- `[ee/watch]` in `EeScheduler.cpp`, now `GHPC_EE_WATCH` / `GHPC_EE_WATCH_LEN`,
  naming the dispatched guest function that changed a byte range, plus
  `[ee/scan]`, a periodic RDRAM sweep for a bit pattern.
- `[valwatch]`, `GHPC_WATCH_VALUE=<word>` in `ps2TraceGuestWrite`, which names
  the guest instruction storing a given 32-bit value. This is the one that
  cracked the case: use it whenever a bad value's origin matters more than its
  destination.
- `[vu1/dump]`, `GHPC_VU1_DUMP=<startByte>:<count>`, raw VU1 microcode words.
- `[gs/geomcensus]`, per-window screen-space bboxes bucketed by texture base.

### Still open

- Save screen flickers between two states. Present with and without the old
  override, so unrelated to the projection bug. Likely double-buffer selection
  or the present latch.
- VIF1 chain-flatten desync manufactures a bad BASE word (0x030b73a4 ->
  BASE=932), roughly 40 dirty chunks per run. The guard rejects them; the
  desync itself is unfixed.
- Intermittent hang, roughly 1 run in 6, stalling shortly after splash 1.

## Input already works end to end

Confirmed by `GHPC_PAD_AUTO=1` pulsing CROSS (green fret) repeatedly, which
traverses the menus. There was no missing plumbing to build; the chain was
already complete and simply undocumented.

The keyboard path is confirmed separately and by hand: arrow keys move the
selection and X activates it, which is exactly the mapping below. So both ends
are verified, the synthetic path and the real one.

One loose end worth ruling out: a run with auto-pad off still reported
`[pad] buttons changed to 0xfeff` with nothing being touched. That is a single
bit (bit 8, L2) pulsing on its own, which looks like a phantom from a connected
gamepad rather than a real press. Harmless in menus, but it would matter once
note input is timed.

    JoypadPoll (0x2f3f48)
      -> BreugPadRead (0x2f6648), buffer gBreugPadData (0x51e000, 3072 bytes)
        -> scePadRead, bound to the runtime HLE stub, not the recompiled body
          -> readPadPortData (Kernel/Stubs/Pad.cpp)
            -> pad override, else padBackend, else raylib keyboard and gamepad

`scePad*` is deliberately absent from `ghpc/config/stub-denylist.txt`, so those
symbols bind to the runtime handlers rather than to Sony's statically linked
bodies. That is what makes host input possible at all, and it is why nothing
had to be added.

Keyboard mapping, from `applyKeyboardState`:

    D-pad          arrow keys
    Square Cross   Z X
    Circle Triangle C V
    L1 R1          Q E
    L2 R2          1 3
    Start Select   Enter RightShift
    L3 R3          LeftCtrl RightCtrl
    Left analog    WASD

A connected gamepad is picked up automatically through `applyGamepadState`.
`GHPC_PAD_AUTO=1` synthesizes alternating CROSS and START pulses every 1.2s
after a 6s settle, which is how headless runs get past screens that wait for
input. Verified button words: CROSS is 0xbfff, START is 0xfff7, nothing pressed
is 0xffff (active low).

A new `[pad]` / `[padcensus]` probe in `Pad.cpp` counts port opens, reads, and
whether each read found the port open, and prints the delivered button word
whenever it changes. Use it before assuming input is not arriving; absence of
the old `[padread]` lines proves nothing, since those sit behind aggressive
logs rather than `GHPC_DIAG`.

**Still missing, and this is the actual M3 goal:** HID guitar support. The game
already has the guitar layer built in (`JoypadIsGuitar__Fi`,
`StrumBarPressed__FP10JoypadData`, and the `joypad_guitar`,
`joypad_guitar_xbox`, `lefty_joypad_guitar` config symbols), so the work is
host side: enumerate a real HID guitar, map frets, strum bar, whammy and tilt,
and decide what `scePadInfoMode` should report so `JoypadIsGuitar` accepts it.
Today the stub reports plain DIGITAL or DUALSHOCK.

## Loading screen flicker is buffer divergence, not z-fighting

It is not z-fighting. Z-fighting shimmers per pixel and changes with the
camera; this is two whole pictures, each internally stable, swapping every
present.

Captured by tracing presents only when the picture actually changes
(`GHPC_PRESENT_TRACE` with `GHPC_PRESENT_MIN`, plus `GHPC_FRAME_ONCHANGE` for
the matching frame dumps). Both were needed: a fixed trace cap fills with the
splash and never reaches the loading screen, and the old 2 second dump gap
cannot show something that alternates at frame rate.

    #1245 disp=56 nonBlack=190368 sig=0xfd6be71d95d48ec8
    #1246 disp=0  nonBlack=211608 sig=0xb23a22f1b0ce458c
    #1247 disp=56 nonBlack=190368 sig=0xfd6be71d95d48ec8
    #1248 disp=0  nonBlack=211608 sig=0xb23a22f1b0ce458c

Byte-identical repeats, so each buffer holds a fixed, different picture. The
game is double buffering across fbp 0 and fbp 56 (adjacent and non-overlapping:
fbp56 is 56 x 8192 = 458752 bytes, exactly one 512x448 CT16 frame), and
alternating DISPFB between them is correct behaviour. The defect is that the
two buffers disagree.

Diffing the pair:

- **fbp56 is correct.** Full poster, face art, red rays, dark panel, text.
- **fbp0 is missing the entire poster layer.** The brick wall behind it shows
  through, and the text and the bottom SELECT / UP-DOWN bar are drawn on top of
  bare brick. Note the counter-intuitive part: fbp0 has *more* non-black pixels
  (211608 vs 190368) because brick is brighter than the poster's black panel,
  so a naive "more content" heuristic points at the wrong buffer.

Ruled out so far:
- **Not the known BASE desync.** `badBaseRejects=0` over the census window that
  covers this screen, so the guard is not eating the poster's packets.
- **Not a whole texture's draws going missing.** With the geometry census keyed
  on destination fbp, every texture bucket has near-equal counts in both
  buffers (5408: 56412 vs 56333; 7072: 5751 vs 6096; 6688: 1466 vs 1462).
  Caveat: that census is a run-wide window, not scoped to the loading screen,
  so treat it as suggestive rather than settled.

So the poster geometry appears to be submitted to both buffers, yet only lands
in one. The next question is whether it is drawn into fbp0 and then painted
over. `[gs/pixel]` is now a rolling per-buffer window (`GHPC_PIXEL=x,y`, last 12
writes per destination buffer) rather than the first 80 writes overall, so it
can show which draw touched a poster pixel *last* in each buffer.

### The two buffers receive different draw lists, not one missing draw

Watching a pixel that actually diverges, (246,99), with the rolling per-buffer
window. Repeating unit per frame:

    fbp0   0x1c40 prim=6 tme=0  black (0,0,0,128)
           0x1620 tme=1         cream (212,214,208,255)

    fbp56  0x1620 tme=1         cream (212,214,208,255)
           0x1c40 prim=6 tme=0  black (0,0,0,128)
           0x1520 tme=1         dark  (42,34,30,128)

Two facts fall out, and both matter more than "a draw went missing":

1. **The buffers get different numbers of draws.** fbp0 sees two writes at this
   pixel per frame, fbp56 sees three. The `0x1520` dark overlay reaches fbp56
   only. So the frame's draw list is not being replayed identically into both.
2. **The order differs too.** fbp0 paints black then cream; fbp56 paints cream,
   then black, then dark. Same draws, different sequence, so this is not a
   simple dropped packet.

Also note **which buffer is correct is not stable between runs**. In the frame
dumps, fbp0 was the broken one and ended black at this pixel. In the pixel probe
run, fbp0's last write was cream. So neither buffer is inherently the bad one,
which argues for a timing or ordering fault rather than one buffer being skipped.

Working hypothesis: a single frame's draws are being split across both
framebuffers instead of each buffer receiving the whole scene, so whichever
buffer is presented is missing whatever went to the other. That would produce
exactly this alternation.

Next probe: log FRAME register writes interleaved with a draw sequence number,
and check whether `fbp` changes partway through one frame's draw list. If it
does, the question becomes whether the game legitimately switches render target
mid-frame (GH2 does render to texture in places) or whether the runtime latches
FRAME at the wrong point relative to the queued draws. `buildDrawBatch` captures
`m_ctx[prim.ctxt]` at submit time, so a mismatch would have to come from the
register write landing at a different point in the stream than the draws it
should apply to.

Do not re-chase: the present path itself is clean. `disp` always equals `sel`,
`usedPreferred` is always 0, the black-content fallback never fires, and
`fieldMode` is 0, so nothing in presentation is choosing the wrong buffer.

## Disassembly pretty printer is missing cvt.w.s

Found while ranking functions by difficulty. `cvt.w.s` has no entry in the
recompiler's disassembly mnemonic table, so the comment above the translated
instruction reads `.word 0x46000064 # cvt.w.s` instead of naming it.

The emitted C++ is correct: it is a proper `FPU_CVT_W_S`. Only the comment is
wrong. 134 generated files are affected.

Worth fixing anyway. The comments are the specification anything reading these
files works from, including tooling that parses them, so a missing mnemonic
hides COP1 instructions from any such consumer. It already skewed the decomp
difficulty ranking until that tool was worked around.

## COP2 is the easiest dense code, not the hardest

The decomp difficulty ranking initially weighted COP2 (VU0 macro mode) at 9.0
per instruction, the heaviest weight in the model, on the assumption that vector
code is hard to read. That is backwards.

VU0 macro mode is ordinary MIPS with an explicit field mask. It is dense but
mechanical. `Transform::Multiply` and `Transform::Multiply2` differ only in mask
bits, so each cross-checks the other for free. The weight should be about 2.0.

Two related structural facts about this binary, both from the same sweep. There
are **zero computed jumps**, so no jump tables anywhere in the ELF. And the
population breaks down as 1,636 leaf functions, 2,336 under 13 instructions, 203
that are a bare `jr $ra`, 2,140 touching COP1, 219 touching COP2, and 1,729 with
an indirect call.

## The decomp wall is macro expansion, not comprehension

`StarPower::SetUsing(bool)` at 0x122718 is 313 instructions, of which about 20
are StarPower logic. The rest is two inlined expansions of Milo's "build a
static message DataArray and export it" macro: a function-local static Symbol
behind a .bss guard, `PoolAlloc`, `DataArray(int)`, three `DataNode::operator=`,
a 16-bit refcount at +0x0a with a conditional destructor, and `atexit`
registration. 28 calls in one function.

Following it is possible. Writing it out is not a decompilation, it is a
transliteration of 290 lines of refcount juggling, which is the thing decomp
exists to replace. The honest output is the one line macro the original source
had, and a macro leaves no trace in a binary. RB3 has equivalents but the
GH1/GH2 era macros differ, so reconstructing the macro shape from GH2 evidence
alone would be a guess presented as source.

Scope of the blocker: **142 functions contain the inlined DataArray
construction and 228 contain the PoolAlloc**, so 1 to 2 percent of the binary
sits behind it.

No general comprehension wall was hit. Everything attempted below rank ~10,500
of 12,422 came out high or medium confidence.

## The boot hang is mostly tooling, not the game

Three earlier entries in this file claim an intermittent boot hang at roughly 1
run in 6. Treat that number with suspicion. Measured against about 18 real runs
in one session, nearly all reached the loading screen, and `checkrun.sh` booted
on its first attempt once its own bug was fixed.

Two false hang reports were traced to their actual causes:

1. **`wait` on a process that is not your child.** A watchdog written as
   `sleep N & SLEEPER=$!` in the parent, then `( wait $SLEEPER; pkill ... ) &`
   in a subshell, does not work. `wait` only accepts the calling shell's own
   children, so it returns an error immediately and the watchdog fires about a
   second after launch, SIGKILLing the run. Every attempt then looks like a
   boot hang. The correct form keeps the sleep inside the watchdog subshell and
   uses `pkill -P $WATCHDOG` before killing the watchdog, so no orphaned sleep
   survives to fire a PID-agnostic `pkill` at a later attempt.

2. **CPU starvation.** A run given a fixed 250 second window will miss it if a
   12,664 file `ninja -j14` build is running at the same time. Do not run the
   game and a full build concurrently and then read the result as a game defect.

Before treating a failed run as a hang, check the wall clock. Three attempts
finishing in three seconds is not three hangs, it is a harness killing its own
runs.

## Regression gate

`ghpc/scripts/checkrun.sh` boots the game headless and asserts two things: that
boot reached the loading screen, and that the presented picture is not
alternating between two stable frames. About 4 minutes per attempt, up to 3
attempts, and it prints how many it used.

It currently exits 1, correctly, because the flicker is real:

    [gate] presents=2200 sigChanges=1950 alternating=1 distinct=2

The signal comes from `ghpcNotePresentDecision` in `gs_cpu_backend.cpp`, which
emits a `[gate]` line every 200 qualifying presents. `alternating=1` means the
last 8 presents strictly alternated between exactly two signatures. Hashes are
not stable across runs, so never compare them between runs.

## Loading screen flicker: four causes eliminated, mechanism localized

The gate still fails. What follows is what the evidence actually supports, so
the next pass does not re-derive it.

### Eliminated by experiment, not by argument

1. **Split draw list.** The standing hypothesis was that one frame's draws were
   being split across both buffers. A monotonic run-length counter keyed on
   destination fbp in `GSCpuBackend::DrawPrimitive` reports `runs == presents`
   with a run length of 904, window after window. Every frame's whole draw list
   goes to one buffer. The hypothesis is wrong.
2. **Double-buffer overlap.** A 289 quadword batch does not fit the 143
   quadword gap between the halves at `base=49 ofst=143`, so an unpack at
   `TOP=49` was caught writing quadword 193. Forcing a non-overlapping layout
   (`GHPC_FORCE_DBUF=0:330`, verified live as `top=0/330`) still flickers.
3. **Unwritten unpack lanes.** V3-32 leaves W and V2-32 leaves Z and W, and the
   runtime preserves whatever VU memory held, which differs per half. Hardware
   does not: PCSX2 writes `v1v0v1v0` for V2 and runs V3 through the V4 path,
   both confirmed against real hardware. Implementing that changed the data,
   verified in the dumps, and left the mismatch at exactly 106 draws with an
   identical signature. Reverted. Still worth doing for accuracy, separately.
4. **Lazy XGKICK.** PATH1 streams the packet out of live VU memory while the
   program keeps storing into it. Snapshotting the source at kick time
   (`GHPC_XGKICK_SNAPSHOT=1`) changes nothing.

### What the divergence actually is

Comparing the two exact MSCALs behind the first diverging draw, in one run,
aligned by MSCAL index rather than by batch index or header contents:

    first diverging draw 46 from mscal 20431 (top=49) vs 20452 (top=192)
    A at its top=49:   +0 qw=49  00000046 00000119 00000135 00000614
    B at its top=192:  +0 qw=192 00000046 00000119 00000135 00000614

Same batch header, same input, quadwords 0 to 48 byte-identical, and only the
W lanes differing, which point 3 proved do not matter. Same program, same
input, different TOP, different output. The corrupt vertices land on raw
(2048, 2048), which is the screen centre, the transform of a zeroed position.

The difference is where the program writes. Its output pointer `vi4` walks
1, 13, 25, 37, 49, 61 and so on, step 12, from a base that does not track TOP.
Measured store ranges:

    top=49:  stores span qw 1 .. 333   (other half starts at 192)
    top=192: stores span qw 1 .. 472   (other half is 49..192)

So the program builds its packet in place across a region covering both halves,
and the two parities land on the other half very differently. Classifying the
failing run's input by last writer gives 114 of 290 quadwords last written by
the program's own stores at `top=49` against 70 at `top=192`.

### The open question

Ordering. The program legitimately writes behind its read pointer, trailing by
48 quadwords at `top=49` and 191 at `top=192`, so writing over its own input is
expected and safe on its own. What is not established is whether a quadword was
clobbered **before** it was read. The last-writer map is sampled at report time
and cannot tell those apart. Establishing that is the next measurement, and it
either confirms this mechanism or kills it like the other four.

### Related facts worth keeping

- Parity flips every frame: 29 DBF toggles per frame (21 MSCAL plus 8 MSCNT),
  an odd number, stable. No MSCALs are rejected and none are duplicated, so the
  guest really does emit an odd count. PCSX2 toggles DBF on MSCNT exactly as on
  MSCAL, so matching that is correct and is not the parity leak.
- `BASE` and `OFFSET` are genuine game VIFcodes (NUM 0, no high immediate bits),
  re-affirmed periodically, never malformed. Nothing is dropped:
  `truncUnpack=0`, `bytesDiscarded=0`, `badBaseRejects=0`.
- Which buffer looks correct is not stable between runs, consistent with a
  parity that depends on startup phase.

### Instrumentation added, all behind GHPC_DIAG and an env flag

`GHPC_DRAWSEQ` (two consecutive frames compared draw for draw, plus a VU1 input
diff of the two MSCALs behind the first diverging draw), `GHPC_QW_WATCH` with
`GHPC_QW_FROM` (every writer of one quadword, VIF and VU alike, in a window),
`GHPC_VI_WATCH` (VI register writes with the setting pc), `GHPC_STORE_PC` (store
address derivation), `GHPC_TOPMAP`, `GHPC_BATCHDIFF`, `GHPC_FORCE_DBUF`,
`GHPC_XGKICK_SNAPSHOT`.

Two traps cost time here and are worth restating. A count cap on a watch fills
at the first event past its threshold and never reaches the interesting one, so
watches need a window, not a cap. And instrumentation has its own bugs: the
per-MSCAL ring was labelled before the MSCAL counter incremented, so every
window dumped was off by one run, which produced a confident and completely
wrong conclusion until it was caught.

## Menu and loading screen performance

Measured on the loading screen with `GHPC_FPS=120` (frames per second reported
every N frames, works in every build, env gated so it costs nothing when off).

    --debug build      10.3 fps    97.1 ms/frame
    release build      20.4 fps    49.0 ms/frame
    release, fn ptrs   23.5 fps    42.6 ms/frame

**Do not play on the `--debug` build.** Both build modes are `CMAKE_BUILD_TYPE=Release`;
`--debug` only adds `PS2X_GHPC_DIAG`, and that costs exactly half the frame rate.
The worst offender is `WritePixel`, which does a `std::map` lookup on
`g_fragByFbp` for **every rasterized fragment**. If the diag build is ever
needed at playable speed, replace that map with the small fixed-size array
pattern `fbpSlot` already uses.

A 15 second `sample` of the release build shows the cost is almost entirely the
software rasterizer, by samples at top of stack:

    2805  SampleTexture inner lambda
    2189  WritePixel
    1704  LookupCLUT
    1061  DrawTriangle
     899  SampleTexture
     914  ReadCT32 + ReadP8
     375  std::__function dispatch

That last entry was pure overhead: `m_readVramFuncs` and `m_writeVramFuncs` were
`std::function` arrays called once per pixel, holding nothing but plain
functions. Now raw function pointers, worth the 49.0 to 42.6 ms above. Every
slot is assigned in the constructor (`default:` fills `ReadNull`/`WriteNull`),
so there is no null pointer risk.

Next targets, in profile order: `LookupCLUT` runs per texel and re-derives the
palette entry every time, so a 256 entry cache invalidated on `tex0` change
should remove most of it. After that the texture sampler itself.

## Why the game "crashes" silently, and where the message went

It does not crash. It calls `assert`, and the whole path was invisible.

`__assert` at 0x35c568 loads `_impure_ptr`, reads `+0xc` from newlib's reent
struct (that is `_stderr`), calls `fiprintf` with it, then falls into `abort` at
0x35c558. `abort` calls `_exit`, which reaches `ps2_stubs::exit`, which called
`requestStop()` and printed nothing. So the process ends with no message, no
host fault, and no crash report in ~/Library/DiagnosticReports.

Both halves of that were fixed:

1. **The assert text was being thrown away.** The game passes the guest address
   of its own static FILE struct (0x448d8c, inside `impure_data`). Our stubs
   only understood the small integer handles `fopen` hands out, so every write
   the game made to its own stderr was dropped and replaced with
   "fprintf error: Invalid file handle". `resolve_file_ptr` now reads the fd out
   of the FILE struct (newlib keeps `_file` at +14) and maps it to a host
   stream, falling back to stderr rather than dropping the write. Wired into
   `fprintf`, `vfprintf`, `fwrite` and `fflush`. Reads keep the strict lookup.
2. **The exit was silent.** `ps2_stubs::exit` now prints the exit code, the
   guest return address and pc before stopping.

Tooling: `ghpc/scripts/debug.sh` runs the game under lldb with breakpoints on
the exit funnel, and `ghpc/scripts/whereis.sh <addr>` maps a guest address to
its containing function, which is how the chain above was identified.

## Slow motion is not the frame rate, it is the EE cycle rate

The game plays in slow motion even when the frame rate looks fine. An event in
`EeScheduler` only fires when **both** deadlines pass:

    item.deadlineCycle <= m_eeCycle && item.hostDeadline <= pacedNow

So guest time is gated on the emulated EE cycle counter, not on wall clock.
Measured with `GHPC_EERATE=60`:

    [eerate] 39.2% of realtime  (115.63 Mcycles/sec vs 294.91 nominal)  23.5 vblanks/sec

The guest gets 23.5 vblanks per second instead of 60, so the game runs at about
40 percent speed. Note that vblanks/sec tracks the render frame rate exactly:
rasterisation runs on the EE thread, so pixel work directly starves guest
execution. Reaching full speed needs roughly 2.5x more rasteriser throughput,
which micro-optimisation will not deliver. The structural fixes are getting
rasterisation off the EE thread or a GPU backend.

Do not "fix" this by firing vblank on the host deadline alone. That would make
the clock right while the guest executes less code per frame, which trades a
visible symptom for a subtle one.

## Rejected: CLUT palette cache

`LookupCLUT` runs per texel and re-reads the palette from VRAM, and caching the
resolved palette looked like the obvious next win after the function pointer
change. It was tried twice, eagerly rebuilding 256 entries on a key change and
then lazily per entry with a filled bitmask, keyed on everything the lookup
reads plus a generation bumped by whole-image transfers. Both measured inside
noise (23.5 to 24.4 fps) **and the lazy version visibly corrupted the picture**,
so the key is missing an input or the invalidation misses a path that writes
palettes. Reverted. Do not retry without a frame comparison in the loop: the
regression was obvious on screen and invisible in the frame rate.

## Song launch crash is a flex lexer fatal, and the four candidates are known

Reproduction is exact: **launch a song**. It is not intermittent and not timing
dependent. It does not reproduce on a plain boot that never starts a song, so a
headless run that skips that step proves nothing about it.

The game does not fault. `yy_fatal_error` (0x307cc0) prints its reason with
`fprintf(stderr, "%s\n", msg)` through fprintf at 0x35b068, then calls `exit(2)`,
which is flex's `YY_EXIT_FAILURE`. GH2 parses DTA with a flex scanner, so a song
load is feeding it something it rejects.

Five call sites reach it, carrying four distinct messages:

    0x3075a4  yylex               0x4d4590  "fatal flex scanner internal error--no action found"
    0x307658  yy_get_next_buffer  0x4d4658  "fatal flex scanner internal error--end of buffer missed"
    0x307750  yy_get_next_buffer  0x4d4690  "fatal error - scanner input buffer overflow"
    0x307bb4  yy_create_buffer    0x4d46e0  "out of dynamic memory in yy_create_buffer()"
    0x307bd4  yy_create_buffer    0x4d46e0  (same message)

The last two mean a failed allocation. The middle two are the classic symptom of
`YY_INPUT` returning an inconsistent count, which points at the file read path
rather than at the data.

Note `fread` deliberately still uses the strict handle lookup, so a guest `FILE *`
it cannot resolve returns 0 items read. Check that against the two buffer
candidates before blaming game data.

## GHPC_PAD_AUTO already exists

`Pad.cpp` synthesizes START and CROSS button edges so a headless run can get past
screens that wait for input:

    GHPC_PAD_AUTO=1 ./ghpc/scripts/run.sh --quiet --debug

It pulses rather than holds, because the UI reacts to a press edge, and it is
behind GHPC_DIAG. It alternates the two buttons on a fixed 1.2 second cycle after
a 6 second settle, enough to walk menus but not to select a specific song.
Extending it into a scripted timeline (button, at time) plus logging what the game
actually reads is the missing piece for reproducing the song launch crash without
a human driving.

Check this before ever claiming a crash cannot be reproduced headlessly.

## The flex fatal is a drained heap, and guest malloc is stubbed away

Resolved which of the four messages fires, and it is none of the interesting
ones. The site is `yy_create_buffer+0x5c`, the **second** allocation:

    yylex+0xa0       yy_create_buffer(yyin, 0x4000)
    yy_create_buffer yy_flex_alloc(0x28)   -> ok      (307b9c)
    yy_create_buffer yy_flex_alloc(0x4002) -> NULL    (307bc0)  dies here
    yy_fatal_error   "out of dynamic memory in yy_create_buffer()"

Both confirmations agreed: the message printed (so cb0d0e3 works), and the
guest stack dump's first word was 0x307bdc, which is 0x307bd4 + 8, the
delay-slot return of that call site. `whereis.sh` names all of it.

So the DTA content is irrelevant. It is an allocation failure.

### Guest malloc never runs

`malloc_0x35c778.cpp` is two lines: set pc, call `ps2_stubs::malloc`. The
recompiler discarded the real body and bound the name. The whole family goes
the same way: malloc, free, realloc, memalign, _malloc_r, _free_r, _realloc_r,
_calloc_r. None of them are in `ghpc/config/stub-denylist.txt`. This is the
fifth instance of the name-collision hazard in CLAUDE.md.

The game ships the complete allocator and it is stock newlib, not an Hmx
override: `malloc` (80 bytes) is `__malloc_lock` / `_malloc_r` (1840 bytes,
real dlmalloc) / `__malloc_unlock`, and it reaches `sbrk` (0x34b9b0), whose
only ceiling test is the `EndOfHeap` syscall (0x3e). Proof that none of it
runs: an instrumented `EndOfHeap` printed nothing at all across a whole run.

### What actually exhausts the arena

Every guest allocation lands in `PS2Runtime::guestMalloc`, a first-fit free
list over a fixed arena. The game sizes its pools by asking for a huge block
and halving until one fits:

    REFUSED 536870912, 268435456, 134217728, 67108864, 33554432, 25165824,
    25034752, ... 24939121   then takes 24939120

It does that twice, taking about 19MB and then about 5MB. At the moment flex
asks for its buffer:

    guestMalloc REFUSED size=16386 | arena=25863KB
    used=225/25857KB free=35/6KB largestFree=1296B

6KB left in 35 scraps, biggest hole 1296 bytes. The arena is gone, so every
later allocation fails and flex is just the first caller to check its result.

Note the runtime allocates from this same arena (ps2_iop_host, EeScheduler
thread stacks, Font.cpp, GS.cpp, MPEG.cpp), so after the game's pool grab the
runtime's own allocations fail too. Any fix has to keep the guest heap and the
runtime arena from overlapping, since real `sbrk` would hand out the same
addresses `guestMalloc` does.

### Reproduction is not what the earlier note said

On the current tree this fires seconds into boot, right after
`sceCdSearchFile failed: \VIDEOS\INTRO.PSS;1`, with no input at all. It
reproduces headlessly:

    (cd work && GHPC_QUIET=1 ../build/ps2xRuntime/ps2EntryRunner ./GH2_debug.elf)

The claim above that it needs a song launch and that a plain boot runs
indefinitely does not hold here. Do not spend time building pad automation for
it.

### Fixed by reserving the runtime's own working set

`kGuestHeapRuntimeReserve` (1MB, `GHPC_HEAP_RESERVE` to override) is held back
from any allocation larger than itself, so the pool probe converges lower and
ordinary allocations still get served afterwards. Confirmed: probe 2 now stops
at 4267010 instead of taking the whole 5309120, the 16386 request succeeds, and
the run goes from dying in about 5 seconds to running indefinitely and
rendering the loaded level.

This is not a workaround for the stubbing. A PS2 game may take all of RAM; a
recompiled one may not, because the host layer needs guest addressable memory
the console never had (GIF packets, Font glyph and kerning tables, MPEG
callback data, IOP host buffers, and a stack per guest thread). Reserving that
is a requirement of the design. Sized from those consumers: about 58KB is
fixed, the rest is headroom for thread stacks, which are game sized.

### Still open: un-stub the allocator family

Denylisting malloc, free, realloc, memalign and the four `_r` variants is still
the right thing, because the game's real newlib heap is better than a first-fit
arena and the stubbing will keep surfacing. It is **not** a fix for this crash,
and that distinction matters: real `_malloc_r` grows through `sbrk` up to
`EndOfHeap` and stops, so the probe would converge on the true ceiling, the
game would take it, and the next allocation would fail exactly the same way.

Doing it requires separating the two allocators first. The runtime's arena and
the guest's `sbrk` region both start near the end of the ELF, so real `sbrk`
would hand out addresses `guestMalloc` has already given away. Move the runtime
arena to a reserved region and have `EndOfHeap` return its base so the guest
stops below it. Land it on a branch and diff against this build.

## The endless load is not I/O bound, so the 39% EE is not what blocks it

Measured after the heap fix, with the game sitting on what looks like a loading
screen. Sampled the ARK read offset rather than adding logging, so this is the
same binary you would actually run:

    18 samples over 6 minutes, 20s apart
    arkOffset  never open, not once
    cpu        110 to 112 percent, flat from t=40s to t=360s
    logBytes   frozen at 5138 from t=40s onward

Also polled `lsof` 100 times inside one 30 second window looking for brief
opens, in case the game opens, reads 64KB and closes. Zero hits. `MAIN_0.ARK`
is not held open, not opened briefly, and not memory mapped.

So there is no streaming to accelerate. Making the EE 2.5x faster would reach
the same non-progressing state 2.5x sooner. **Do not start the rasterization
move or the GPU backend expecting it to fix the load.** That work is still
worth doing on its own terms, it is just not this.

What this does *not* prove is that the load completed. It only proves nothing
is being read. The stall could sit on either side of that.

Two leads visible in the log at the point it goes quiet, neither chased:

  - `[IOP/RPC trace:unhandled] sid=0x75433178` against synth_r.irx, libsd.irx
    and sdrdrv.irx. The sound path goes unanswered. A game waiting on an audio
    service that never replies looks exactly like this. Only 4 such lines and
    they do not repeat, so check whether the trace dedupes before concluding
    the game is not retrying.
  - `[guest-branch:missing-target] op=JALR target=0x0 source=0x126508`. An
    indirect call through a null pointer, and the dumped vtable words decode as
    ASCII rather than code addresses, so the object pointer is aimed at string
    data.
