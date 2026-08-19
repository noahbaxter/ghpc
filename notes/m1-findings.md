# M1: PS2Recomp on arm64 macOS

Kill criterion was "builds on arm64, emits C++ that compiles". Both pass.

## Toolchain build

No aarch64 fixes needed. Upstream CMakeLists already detects arm64, auto-fetches
sse2neon, and has an explicit macOS ARM64 branch. Contradicts the README's
"tested mainly with MSVC" warning.

- `ps2_recomp`, `ps2_analyzer`: 132/132 targets, 0 errors
- `ps2_runtime`, `ps2EntryRunner`: 98/98 targets, 0 errors
- Only missing dep was `pkg-config` (needed by raylib). `brew install pkg-config`.
- Runtime pulls raylib + glfw + imgui + ffmpeg. Renders via OpenGL 3.3.

## Recompiling GH2

Input: `SLUS_214.47` (GH2 PS2 Final Debug, vanilla, symbolized).

- `ps2_analyzer`: 4.2s. 251 stubs bound by address, 0 skips, 0 jump tables.
- `ps2_recomp`: 8.6s. "Recompilation completed successfully".
- Output: 12,664 .cpp (one per function), 5.45M LOC, 257 MB.

Symbol resolution worked off the debug symbols directly, no Ghidra pass. The
analyzer named real engine functions: `Draw__11RndDrawable...`,
`WorldXfm__16RndTransformable`, `DrawFaces__6PsMesh`, `Select__5PsTex...`.
`PsMesh`/`PsTex` are the PS2 implementations of `RndMesh`/`RndTex`.

## Does it compile

Flags taken from the runtime's own `compile_commands.json`.

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

Roughly 0.7s per file single-threaded, so a full 12,664-file build is on the
order of 15 minutes at -P12. Worth setting up ccache.

## The real warning from this stage

Recompilation succeeded but emitted many `unresolved JR/JALR` warnings: indirect
jumps it could not resolve statically, so it falls back to
`runtime->lookupFunction(addr)` at runtime. Expected for vtable-heavy C++.
Counts are large in places: `Load__7RndMeshR9BinStream` promoted 1405 fallback
entries, `LoadObjs__9DirLoader` 303, `WorldXfm__16RndTransformable` 130.

This is a correctness risk, not just a perf one. If a target address is not in
the dispatch table at runtime, it fails there rather than at build time. This is
the most likely source of M2 failures.

## Incidentally

Audio and timing classes are all present and named: `MidiReader::Midi`,
`MultiTempoTempoMap::TempoInfoPoint`, `MeasureMap::TimeSigChange`, and WaveFile
cue point handling. Relevant for M3, which remains the real risk.

GH2 also links FaceFX (`OC3Ent::Face::FxFaceGraph` etc) for facial animation.
Middleware, so a candidate for stubbing rather than porting.
