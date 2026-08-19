# Backlog

## Next

**M4: fileio IOP service (SID 0x80000001).**
The game now binds and calls, nothing answers. PS2Recomp does not emulate the
IOP CPU or load IRX binaries; it high-level-emulates services per game, and only
three games have profiles (RE Code Veronica X, LOTR Two Towers, Fatal Frame).
GH2 needs its own.

- Confirm the RPC numbers against ps2sdk rather than inferring them from the two
  captured payloads. `rpc=0xff` (8 bytes, two pointers) and `rpc=0xc` (39 bytes,
  leading command selector) are what we have observed so far.
- Route to the host file I/O the runtime already has working. `fioOpen` and
  friends succeeded back in M2, so the plumbing exists underneath.
- Success looks like the game reading `GEN/MAIN.HDR`, then `MAIN_0.ARK`.

## After that

**M5: audio.** The real risk, flagged from day one and unchanged. GH2's IOP audio
is `LGAUD` plus Harmonix's `SYNTH_R`/`SYNTH_S`, all undocumented, saved to
`work/disc/IOP/`. Audio-to-input timing is the entire product for a rhythm game,
so this decides whether the port is real or a tech demo.

**M6: rendering.** Target focus mode first, which disables world rendering and is
already what serious players use. Still needs GS for the note highway, HUD and
menus, but that is mostly textured quads rather than full 3D venues. Move to
Linux before this starts.

**M7: VU microcode.** ~28 KB across 18 named, indexed `.DVP.overlay` sections
with a table of contents. Hand-port rather than building a general VU
recompiler, and prioritize by what the highway actually needs. All three games
report 18 overlays, so this should cover GH1 and 80s too.

**M8: input.** Native HID guitar support, real calibration, latency measurement.
Verify the actual latency win over a tuned PCSX2 rather than assuming it.

**GH1 and 80s.** Symbolized debug builds are saved in `work/elf-debug/`. GH1's
has notably fewer symbols (8,433 vs GH2's 12,663), which suggests an earlier
prototype that may diverge from retail. Check before assuming it is as cheap as
the other two.

## Deferred

- **Upstream the patches.** Not until it works. Patch 0003 (`isStubFunction`
  denylist) is the generally useful one, since it is the root of all four
  name-collision mechanisms. Patch 0001 (`GetRomName` bounds) is a real
  memory-safety bug worth reporting, but the ABI call to ignore `$a1` was made
  from one caller in one game and should be disclosed as such.
- **Audit the remaining 247 stubs** for the same collision class as more code
  gets exercised. GetRomName and the sceFs family were found by crashing into
  them; a systematic pass would find them earlier.
- **Decomp toolchain.** `DarkRTA/rb3` is a Rock Band 3 matching decomp at ~79%
  of cross-platform code, same Milo engine, and `Mikompilation/Himuro` is a PS2
  decomp of Fatal Frame with the established PS2 toolchain (decompals binutils,
  original 32-bit GCC). Both are the map for layer 2's replacement half. Needs
  Linux plus i386 multiarch.
- **Shipping build target.** We develop against debug builds, which carry asserts
  and debug paths. Decide later what ships.
- **ccache tuning.** Default 5 GB cap, currently fine, will matter once full
  rebuilds get frequent.

## Notes to self

- `ps2ResolveGuestPointer` never fails; it masks out-of-range addresses back into
  RAM and returns true. `getMemPtr` therefore cannot reject a bad pointer, so any
  syscall taking a guest length must bound it itself. This bit us once already.
- The `File/CD` debug tab shows a `cdImage` field that nothing ever sets. There
  is no CLI flag or env var for it; the game-override hook is the intended way in.
  Might matter if the fileio service wants a real disc image rather than a
  directory.
