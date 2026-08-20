# M1: PS2Recomp on arm64 macOS

Criteria: builds on arm64, emits C++ that compiles. Both met.

## Toolchain build

No aarch64 changes required. Upstream CMakeLists detects arm64, fetches
sse2neon, and has a macOS ARM64 branch.

- `ps2_recomp`, `ps2_analyzer`: 132/132 targets, 0 errors
- `ps2_runtime`, `ps2EntryRunner`: 98/98 targets, 0 errors
- Missing dependency: `pkg-config` (required by raylib)
- Runtime pulls raylib, glfw, imgui, ffmpeg. Renders via OpenGL 3.3.

## Recompiling GH2

Input: `SLUS_214.47` (GH2 PS2 Final Debug, vanilla, symbolized).

- `ps2_analyzer`: 4.2s. 251 stubs bound by address, 0 skips, 0 jump tables.
- `ps2_recomp`: 8.6s, reported success.
- Output: 12,664 .cpp (one per function), 5.45M LOC, 257 MB.

Symbol resolution used the debug symbols directly, with no Ghidra pass. Named
engine functions resolved: `Draw__11RndDrawable...`, `WorldXfm__16RndTransformable`,
`DrawFaces__6PsMesh`, `Select__5PsTex...`. `PsMesh`/`PsTex` are the PS2
implementations of `RndMesh`/`RndTex`.

## Compilation

Flags taken from the runtime's `compile_commands.json`.

| test | result |
|---|---|
| 1200 random generated files | 1200 pass, 0 fail, 0 warnings |
| `register_functions.cpp` (29 MB dispatch table) | pass, 1.6s |
| `Draw__11RndDrawable...` | pass |
| `Load__7RndMeshR9BinStream` (484 KB) | pass |
| `WorldXfm__16RndTransformable` | pass |
| `DrawFaces__6PsMesh` | pass |
| `SpliceKeys...` (1.8 MB, largest function) | pass |
| `CleanupMesh`, `CacheFrames` | pass |

~0.7s per file single-threaded. Full 12,664-file build ~5 min at -j12 with
ccache and unity build.

## Indirect dispatch

Recompilation emitted 3,386 `unresolved JR/JALR` sites: indirect jumps not
resolvable statically, falling back to `runtime->lookupFunction(addr)`.
Expected for vtable dispatch. Largest: `Load__7RndMeshR9BinStream` 1405 entries,
`LoadObjs__9DirLoader` 303, `WorldXfm__16RndTransformable` 130.

These compile. A target absent from the dispatch table fails at runtime rather
than build time.

## Other observations

Audio and timing classes present and named: `MidiReader::Midi`,
`MultiTempoTempoMap::TempoInfoPoint`, `MeasureMap::TimeSigChange`, WaveFile cue
point handling.

GH2 links FaceFX (`OC3Ent::Face::FxFaceGraph`) for facial animation. Middleware,
candidate for stubbing.
