# The Rnd seam

A map of GH2's renderer abstraction, for replacing the PS2 path (VIF1 -> VU1
-> GIF -> software GS) with a native backend at the engine's own layer.
Addresses come from the per-function filenames in `work/output/`
(`Name__Mangled_0xADDR.cpp`), cross-checked against the sibling `gh2-decomp`
(`src/rndobj/`, `docs/notes/`), which is a read-only reference. Written
2026-09-11, before any seam work started.

## Hierarchy

Platform-neutral (Milo engine, `gh2-decomp/src/rndobj/`):

- `Rnd : Hmx::Object`, the renderer singleton base. vtable 0x4511d0. The
  drawing interface is vtable slots +0x078..+0x0f8, all empty in the base
  (`Rnd.h:90-109`). Stubs: `DrawRect 0x3c53c0`, `DrawLine 0x3c53d0`,
  `MakeDrawTarget 0x3c53d8`, `SetSync 0x3c53e0`, `SetShadowMap 0x3c53e8`,
  `SetDepthOfField 0x3c53f0`, `SetAspect 0x3c5400`, `SetClearColor 0x3c53a8`,
  `ScreenDump 0x3c53b8`. Real bodies: `Init 0x200d90`, `Terminate 0x201400`,
  `BeginDrawing 0x201ea0`, `EndDrawing 0x201fb0`, `Handle 0x202780`,
  `DrawTimers 0x201760`, `RegisterEndDraw 0x201d58`.
- `RndDrawable`, the draw-tree node. `Draw 0x43f5b8`, `DrawShowing 0x1dc178`.
  Root of a multi-vtable diamond with `RndHighlightable` and
  `RndTransformable` (`docs/notes/RndDrawable.md`).
- `RndMesh : RndDrawable`: `ct 0x1f15a0`, `Load 0x43b5a0`, `Sync 0x43cb98`,
  `CacheStrips 0x1f2318`, `SetMat 0x1f1318`.
- `RndTex : Hmx::Object`: `ct 0x205cf8`, `SetBitmap 0x205eb0`,
  `LockBitmap 0x206618`, `PlatformBppOrder 0x205d88`. Size 0x70.
- `RndMat`: `ct 0x1e7880`, `Load 0x1e8ba0`, `LoadShader 0x1e84f0`.
- `RndCam : RndTransformable`: `ct 0x1d8138`, `Select 0x1d7a30`,
  `SetFrustum 0x1d8668`, `SetTargetTex 0x1d7a60`, `ProjectZ 0x3b3fc8`.

PS2-specific:

- `PsRnd : Rnd`, vtable 0x44f2b8, singleton `ThePsRnd` at 0x4f2780, 1328
  bytes.
- `PsMesh : RndMesh`, 0x190 bytes, four vtables.
- `PsTex : RndTex`, 0xb0 bytes, vtable 0x44f6f0.
- `PsCam : RndCam`, 0x320 bytes.
- `PsMat : RndMat`, `PsMultiMesh : RndMultiMesh`, `PsParticleSys`,
  `PsEnviron`. `PsMovie` is outside Rnd (`NORTHSTAR.md`).

Factory names drop the prefix (`PsTex::StaticClassName` is `"Tex"`, `PsCam`
is `"Cam"`, `PsMesh` is `"Mesh"`), so DTA and Milo data never name the PS2
class.

## Entry points a native backend hooks

Per frame:

- `PsRnd::BeginDrawing 0x1bfed0` (seeds MFIFO TADR, kicks `D1_CHCR=0x105`)
- `PsRnd::EndDrawing 0x1c0070` -> `UpdateStats 0x1c00a8`, `Rnd::EndDrawing`,
  `StopDrawing 0x1be488`
- `PsRnd::SwapBuffers 0x1bfbb8`, `VSync 0x1bf8e8`, `ClearZBuffer 0x1bfdc0`,
  `MakeDrawTarget 0x1bf048`, `Reset 0x1beb10`, `Init 0x1be9d8`,
  `Terminate 0x1bef38`
- `PsRnd::FlushPacket 0x43e400`, the one chokepoint where every built packet
  is kicked to DMA channel 8. Stubbing this alone kills the GIF path.

Geometry:

- `PsMesh::DrawFaces 0x43eea0`, the real geometry submit
- `PsMesh::DrawShowing 0x43f038`, `Sync 0x43afa8`, `UpdateFacePacket
  0x43a810`, `FixVerts 0x1c3020`, `GetMultiMeshPacket 0x1c3678`
- `PsMultiMesh::DrawShowing 0x1c88b0`, `PsParticleSys::DrawShowing 0x1c85d8`
- 2D: `PsRnd::DrawRect 0x1c0258`, `DrawLine 0x1be1c8`, `DrawString 0x1bde80`

Material and texture:

- `PsMat::Select 0x43ea70`, `Update 0x1c23a8`, `Init 0x1c2ec0`
- `PsTex::Select 0x43f490`, `LoadImage 0x1c5a08`, `LoadVram 0x1c5b58`,
  `AllocateVram 0x1c53b0`, `SyncBitmap 0x1c6bd0`, `LockBitmap 0x1c76a8`,
  `MakeDrawTarget 0x1c6008`, `FinishDrawTarget 0x1c61e8`,
  `CopyFromScreen 0x1c6518`

Camera:

- `PsCam::Select 0x1c1770`, the only guest writer of VU1 qw696-703 (the
  projection upload). `Init 0x1c1110`, `ProjectZ 0x1c16f8`.

Raw GS packet builders a backend deletes rather than hooks:
`PsRnd::SetRegister 0x43e320`, `DoPointTests 0x1bf740`, `CopyBuf 0x1bf238`,
`SetDepthOfField 0x1bf690`, `OnDebugDma 0x1c0e50`, `OnReset 0x1c0d48`.

## Data crossing the seam

- Vertex: `RndMesh::Vert` is 64 bytes (`VertVector::resize` does
  `sll $a0, $s1, 6`): `Vector3 mPos` @0x00, `Vector3 mNorm` @0x10,
  `Hmx::Color` (4 floats) @0x20, `Vector2 mUv` @0x30, two unread floats at
  0x38 and 0x3c (`RndMesh.h:270-289`). `VertVector` is hand-rolled on
  MemAlloc, not STL.
- Index: `RndMesh::Face` is three `unsigned short`, stride 6, triangle list.
  Strips are cached separately (`CreateStrip 0x1f2368`, `CacheStrips
  0x1f2318`), and the PS2 path consumes strips. `PsMesh` keeps the converted
  result in `mFacePacket`, a `MemHandle*` at +0x150 sized `mUnk154 * 0x10`
  bytes (a quadword count), heap `"rnd"`.
- Texture: `RndTex` has `RndBitmap mBitmap` (0x1c bytes) @0x28, `mMipMapK`
  @0x44, width/height/bpp @0x4c/0x50/0x54, `FilePath` @0x58. Channel order
  is platform-dependent via `PlatformBppOrder`. VRAM residency is `PsTex`'s
  own allocator (`NumTotalBlocks 0x1c4848` = 0x4000 minus the buffers'
  end, in 64-word blocks).
- Camera: `PsCam::Select` writes qw696-703 as two UNPACKs, `0x7C0202B8`
  (V4-32 masked, NUM 2, ADDR 696) and `0x6C0602BA` (V4-32, NUM 6, ADDR 698),
  FLG clear. Frustum params are near, far, y_fov (degrees) plus a z_range
  pair at RndCam+0x2cc/+0x2d0. Object transforms come from
  `RndTransformable::WorldXfm 0x43f768`.

## What the runtime already does

- No Rnd-level hooks exist. Everything under `ps2xRuntime/src/runner/` is
  generated. Hand-written bodies go through `ghpc/override/` (see
  `scripts/overlay.sh`), which is the layer a backend is built in.
- Below the seam and made dead by a backend: DMA channel 9 (toSPR), the
  MFIFO ring and channel 8, the MMIO lui/ori folding fix, the MFIFO drain
  wrap that ended the VU1 runaway.
- `PsCam::Select`'s VIF path was probed and cleared (`ui-draw-hang.md`); do
  not re-chase it. `_5PsCam$sGuardBand` (0x4f3038) reads 0.0 in every run.
- `GHPC_STREAM_READY=1` reached `game_screen` sooner and produced a deader
  guest (`song-load-crash.md`). Anything standing in for a subsystem ships
  with a same-build control arm and proves `BeatMatch::Poll 0x1259c0` and
  `PlayerMatcher::Poll 0x117dd0` still run.

## Unknown

- The PS2 packet format is not decompiled. `PsMesh::DrawFaces`,
  `PsMat::Select`, `PsTex::Select`, `PsRnd::SetRegister`, `ClearZBuffer`,
  `DoPointTests`, `MakeDrawTarget` are "read and dropped" in the decomp. What
  `DrawFaces` hands VU1 (strip layout, skin data, clip flags) has to be read
  out of the packet at runtime.
- VU1 microcode semantics: 18 overlays, assumed pure T&L, untested.
- No `RndCam.h` or `RndMat.h` in the decomp, so camera and material field
  layouts are not modelled beyond the frustum params.
- `RndTex::Type` has 2 of N enumerators; palette and swizzle formats are
  unrecovered.
- Lighting, `PsEnviron`, shadow maps, post-processing and `RndScreenMask`
  are additional seam surface not covered above.
- Skinning (`RndMesh::mBones` @+0x13c, `ReplaceBones 0x1f1450`): whether it
  runs on EE or VU1 is unknown and decides the backend's vertex contract.
