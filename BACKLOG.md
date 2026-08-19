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
