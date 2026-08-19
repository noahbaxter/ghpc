# M0: ELF recon

Question: do the GH1/GH2/80s PS2 binaries carry debug symbols, which determines
whether the Ghidra reversing workflow PS2Recomp assumes is required.

Result: all three do, with type information.

## Symbol availability

Source: `milo-executable-library` (Project Deluge PS2 lot, Mar 2021) and the
GH2DX 2.0 ISO. Measured with `llvm-readelf`.

| build | FUNC syms | .mdebug | VU overlays |
|---|---|---|---|
| GH1 Prototype Debug (SLUS_212.24) | 8,433 | yes | 18 |
| GH2 PS2 Final Debug (SLUS_214.47) | 12,663 | yes | 18 |
| GH80s Prototype Debug (SLUS_215.86) | 12,664 | yes | 18 |
| GH2DEBUG.ELF (DX build) | 12,664 | yes | 18 |
| all Vanilla / retail builds | 0 | no | 18 |

Retail is stripped; debug is not.

GH2's "PS2 Final Debug" is vanilla GH2, not a DX build, so development can target
stock game code with DX applied as a data layer.

## Symbol content

Names use GCC 2.x C++ mangling, which encodes parameter types:

- `StrumBarPressed__FP10JoypadData` -> `StrumBarPressed(JoypadData*)`
- `NewRndCopy__H1Z7RndMesh_PCX01_PX01` -> template over class `RndMesh`

`.mdebug` is 1.11 MB of MIPS ECOFF debug info: procedure descriptors, line
numbers, local variable names, struct/class layouts.

Milo engine symbol substring counts in GH2: Rnd 1773, Obj 1375, Char 1271,
Ui 1241, Stream 996, Track 500, Song 279, Star 213, Beat 208, Synth 203,
Guitar 193. Named code sections `obj_load`, `rnd_load`, `rnd_draw`.

## VU microcode

- `.vutext` resident: 14,096 bytes
- 18 `.DVP.overlay.*` sections: 13,872 bytes total, largest overlay 2 KB
- `.DVP.ovlytab` + `.DVP.ovlystrtab`: index naming and delimiting each

~28 KB across 19 named bounded chunks with a table of contents, against a
3.37 MB `.text`, under 1% of the binary.

All three titles report 18 overlays, indicating a shared VU pipeline.

GH2DX focus mode (Modifiers menu, double-tap Select) disables world rendering.

## Related work

`DarkRTA/rb3` is a matching decompilation of Rock Band 3 (Wii, build 100901_A,
CodeWarrior 4.3), reported ~79% of cross-platform code decompiled, ~61%
byte-exact. Same Milo engine, later generation. GH2's symbols share class names
(RndMesh, RndMat, RndCam, RndEnviron, RndTransAnim).

Byte-matching does not transfer to PS2 (different CPU and compiler). A port
requires compilable C++ rather than matching output.

## Open items

- Confirm the GH1 prototype debug build tracks retail GH1. 8,433 symbols against
  GH2's 12,663 suggests an earlier or smaller build.
- Shipping target undecided; debug builds carry asserts and debug paths.
- Audio timing untested.
