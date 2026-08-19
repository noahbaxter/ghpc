# M0: ELF recon

Goal: find out whether GH1/GH2/80s PS2 binaries carry debug symbols, since that
decides whether we need the Ghidra reversing grind PS2Recomp assumes.

Answer: yes, all three, plus type info.

## Symbol availability

Source: `third_party/milo-executable-library` (Project Deluge PS2 lot, Mar 2021)
and `GH2DX 2.0` ISO. Measured with `llvm-readelf`.

| build | FUNC syms | .mdebug | VU overlays |
|---|---|---|---|
| GH1 Prototype Debug (SLUS_212.24) | 8,433 | yes | 18 |
| GH2 PS2 Final Debug (SLUS_214.47) | 12,663 | yes | 18 |
| GH80s Prototype Debug (SLUS_215.86) | 12,664 | yes | 18 |
| GH2DEBUG.ELF (DX build) | 12,664 | yes | 18 |
| all Vanilla / retail builds | 0 | no | 18 |

Retail is stripped. Debug is not.

GH2's "PS2 Final Debug" is vanilla GH2, not a DX build. We can develop against
stock game code and keep DX purely as a data layer, which is how DX works anyway.

## What the symbols carry

Names are GCC 2.x C++ mangled, so parameter types come along for free:

- `StrumBarPressed__FP10JoypadData` -> `StrumBarPressed(JoypadData*)`
- `NewRndCopy__H1Z7RndMesh_PCX01_PX01` -> template over class `RndMesh`

`.mdebug` is 1.11 MB of MIPS ECOFF debug info: procedure descriptors, line
numbers, local variable names, struct/class layouts.

Milo engine internals are fully named. Symbol substring counts in GH2:
Rnd 1773, Obj 1375, Char 1271, Ui 1241, Stream 996, Track 500, Song 279,
Star 213, Beat 208, Synth 203, Guitar 193. Named code sections `obj_load`,
`rnd_load`, `rnd_draw`.

## VU microcode is small and enumerable

This is the part that normally makes PS2 recomp a research project.

- `.vutext` resident: 14,096 bytes
- 18 `.DVP.overlay.*` sections: 13,872 bytes total, largest single overlay 2 KB
- `.DVP.ovlytab` + `.DVP.ovlystrtab`: an index naming and delimiting every one

~28 KB of VU microcode in 19 named, bounded chunks with a table of contents,
against a 3.37 MB `.text`. Under 1% of the binary.

All three games report exactly 18 overlays, so the VU pipeline looks identical
across GH1/GH2/80s. One VU porting effort should cover all three.

Combined with GH2DX focus mode (disables world rendering, Modifiers menu,
double-tap Select), the two things that normally block PS2 recomp are both
unusually contained in this specific game.

## Related: the Milo engine is already partly decompiled

`DarkRTA/rb3` is a matching decompilation of Rock Band 3 (Wii, build 100901_A,
CodeWarrior 4.3). Reported ~79% of cross-platform code decompiled, ~61%
byte-exact. Same Milo engine, later generation. GH2's symbols confirm shared
class names (RndMesh, RndMat, RndCam, RndEnviron, RndTransAnim).

Byte-matching does not transfer to PS2 (different CPU, different compiler), but
a port does not need matching. It needs correct compilable C++, and that is the
expensive half rb3 has already done.

## Open items

- Confirm the GH1 prototype debug build is close enough to retail GH1 to be a
  useful target (8,433 syms vs GH2's 12,663 suggests an earlier/smaller build).
- Decide shipping target: debug builds carry asserts and debug paths.
- Audio timing remains the real unbounded risk. Nothing here touches it.
