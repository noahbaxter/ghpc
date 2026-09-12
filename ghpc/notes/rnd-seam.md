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
  MemAlloc, not STL. **The `Hmx::Color` field is not a colour on skinned
  meshes**: its four floats are the bone weights, summing to 1.0 and indexing
  qw660..675 in order (`evidence/2026-09-12-bone-palette.txt`).
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

## Measured at the seam

`ghpc/override/DrawFaces__6PsMesh_0x43eea0.cpp` logs the RndMesh subobject on
entry under `GHPC_MESH_LOG`. Release, 150s, 387 calls on the way to and on
`game_screen`: **every mesh reports verts=0 faces=0** while `packetQw` is
nonzero (19 for the first). The engine-level vectors are empty by draw time;
GH2 keeps only the converted PS2 face packet (`mFacePacket` @+0x150). The
body itself confirms it: `DrawFaces` reads +0x150, +0x154, +0x158/+0x15a and
+0x140 and nothing else on `this`. So a native backend cannot read geometry
at draw time. Also: `this` in `DrawFaces` is `mOwner` (+0x138), passed by
`DrawShowing` at 0x43f388, so instances draw through their owner's packet.

Where they die, all inside `PsMesh::Sync` 0x43afa8 (read from the generated
body; the decomp has no `PsMesh::Sync`):

- 0x43afc4: return if `+0x138` (owner) is not `this`.
- 0x43afe0: `jal UpdateFacePacket` 0x43a810, which only reads the vectors.
- 0x43b040 / 0x43b048: `mFaces` start and finish stored as 0 (a swap with an
  empty vector), gated on sync flags bit 0x20 and `+0x140 & 0x20` clear;
  0x43b0e0 `MemOrPoolFree` on the old buffer.
- 0x43b128: `VertVector::resize(this+0x100, 0)`, gated on sync flags & 0x1f
  and `+0x140 & 0x1f == 0`. Meshes with those bits set keep their verts and
  get `SyncDCache` instead.

`RndMesh::Load` 0x43b5a0 ends with a virtual `Sync(0x3f)` or `Sync(0xbf)`
(0x43cb5c), both carrying 0x1f and 0x20, so the Load-time Sync frees both.
An entry hook on Sync sees the data on that call.
`ghpc/override/Sync__6PsMeshi_0x43afa8.cpp` logs it as `[ghpc/mesh/sync-in]`.

Measured, release, 150s, 67 Sync entries logged: 42 carry geometry, 25 are
re-syncs of already converted meshes (verts=0). The first is a 320x128 UI
quad, `Sync(0xbf)`:

    v0 pos=(-160 64 0) norm=(0 0 1) uv=(0 0) color=(1 1 1 1)
    v3 pos=(160 -64 0) norm=(0 0 1) uv=(1 1) color=(1 1 1 1)
    f0 0 1 2   f1 3 2 1

and at DrawFaces the same `this` reports verts=0 with packetQw=19. So the
vertex contract for a backend is: capture `Vert[]` (64 B, pos/norm/color/uv)
and `Face[]` (3 x u16) at Sync entry keyed by mesh address, and draw from
that cache at DrawFaces with the owner's cached world transform (+0xa0).
Meshes with `+0x140 & 0x1f` set keep their verts (mutable geometry, the
fretboard presumably) and can be read at draw time.

The cache exists: the Sync override copies `Vert[]` and `Face[]` into a host
map keyed by guest address when the owner is `this`, the mutable bits are
clear and verts > 0; the DrawFaces override looks `this` up and counts.
Release, 150s to and on `game_screen`: hits 115314, misses 1024
(`[ghpc/mesh/cache]`). So at draw time the backend has the geometry for
99% of draws host-side, with no guest behaviour change (rung 9, speed same).
The misses are three meshes, not a class of them. Release, 150s, the first
24 named by `[ghpc/mesh/miss]`:

    16   flags140=0x1f  verts=4  faces=0   packetQw=12   one mesh
     8   flags140=0     verts=0  faces=0   packetQw=19   two meshes

The 0x1f one is mutable geometry, which the cache skips on purpose because
Sync leaves its verts alone. Those verts are therefore still there at draw
time and a backend reads them directly, so that mesh needs no cache. The
other two were converted before the first Sync this run observed. Nothing
in the miss set is a class the cache cannot reach.

## The other three inputs, measured

Three more overrides log at `PsMat::Select`, `PsTex::Select` and
`PsCam::Select` under `GHPC_MESH_LOG`. Release, 150s: 407 material selects,
212 texture selects, 56 camera selects. Speed unchanged with all six
overrides in (4.9 to 5.1%).

**Camera.** All 56 calls verified both VIF headers (`0x7C0202B8` then
`0x6C0602BA`), so the 8 quadwords are read where the game finished building
them, in the DMA cursor at scratchpad 0x70000008. They decode as:

    qw696  viewport scale       (2007.84 2007.84 -29490.8 0)
    qw697  viewport offset      (2048 2048 29490.8 0)
    qw698  camera position      = WorldXfm(this)->v
    qw699  guard band scale
    qw700..703  projection rows, far and near patched in, then
                Multiply2(this+0xc0, m, m)

2048 is the centre of the GS 4096 coordinate space, and the W lanes of 696
and 697 are stale buffer content because STMASK 0xC0C0C0C0 protects them.
Frustum fields at +0x2c0 near, +0x2c4 far, +0x2c8 y_fov, +0x2cc z_range.
The menus use near 1 far 1000, gameplay near 400 far 1060, both y_fov
0.6024 radians. y_fov 0 takes the orthographic branch.

**Material.** Per draw: blend mode at +0x2c (1 and 3 dominate, 4 modes seen),
`mColor` RGBA at +0x30, and ready-made GS register images for ALPHA_1
(+0x138), DIMX (+0x140), TEST_1 (+0x148), CLAMP_1 (+0x150) and ZBUF_1
(+0x158), each pinned by the `SetRegister` calls in the body. 379 of 407
draws carry a texture pointer at +0x134; TFX at +0x130 is 0 on all but 6.

**Texture.** Dimensions and depth at +0x4c/+0x50/+0x54, the `RndBitmap` at
+0x28, and PS2 register images TEX0_1 (+0x70), TEX1_1 (+0x78) and the two
MIPTBP words. Sizes run 32x64 to 512x256 at 4, 8 and 16 bpp. Caveat on
format: TEX0 is logged as read at entry, before the body patches TBP0 and
TFX, so the PSM histogram (0, 2, 19, 20) includes uninitialised first
selects and is not a reliable format census yet. Read it after the patch
if the format matters.

With these and the mesh cache, every input a native `DrawFaces` needs is
available host-side: geometry, world transform, projection, blend and
texture state.

## The vertex transform, measured

Read off matched pairs rather than from the microcode, which is not
decompiled: the object-space vert the cache holds against the screen-space
vert the PS2 path submits for the same mesh. Full method, numbers and control
in `evidence/2026-09-11-vertex-transform-calibration.txt`.

    world = sum over b of weight[b] * (pos * Bone[b])   b = 0..3, skinned
    world = pos * World      unskinned; World rows 0..2 the Matrix3,
                             row 3 the translation
    clip  = world * M        M = qw700..703 as rows, implied pos.w of 1
    q     = 1 / clip.w
    x     = clip.x * q * qw696.x + qw697.x    GS pixels, 2048-centred
    y     = clip.y * q * qw696.y + qw697.y
    z     = clip.z * q * qw696.z + qw697.z
    s,t   = uv * q           the submit is fst=0, so ST/Q not UV

The camera position at qw698 is **not** subtracted before M: M already carries
the view transform. Meshes submit as PRIM 4 (tristrip), iip=1 tme=1 abe=0.

Over six meshes and 171 matched pairs this lands **inside GS subpixel**: dx
and dy medians 0.03 to 0.04 px, whole range [0.00, 0.07], q at -0.000%. One
sixteenth of a pixel is 0.0625, so nothing is left over. Full table in
`evidence/2026-09-12-bone-palette.txt`.

**Gameplay meshes are skinned, and that is why no single matrix ever fit.**
`PsMesh::DrawShowing` branches at 0x43f098 on `mBones` (+0x13c):

- `mBones == 0`: 0x43f2b8 uploads `WorldXfm(this+0x40)` behind `0x6C0402A4`
  (V4-32, NUM 4, ADDR 676). This is the only case with a single `World`.
- `mBones != 0`: 0x43f0a4 uploads one `0x6C140294` block, V4-32 **NUM 0x14 =
  20 quadwords at ADDR 660**, spanning qw660..679. Bones 0..3 land at
  qw660..675 as `Multiply(bone.xfm, WorldXfm(bone.obj))`. qw676..679 is the
  tail: **identity** when more than one bone is in play (0x43f24c), or bone
  0's matrix when only bone 0 is (0x43f294).

Both skinned blocks jump straight to 0x43f330 and never run 0x43f2ec, which
is the whole of the earlier "reached DrawFaces without passing 0x43f2ec".
There was no missing writer. 0x43f434 is also not a second call site: it is
the only `jal DrawFaces` here, inside a per-material loop (`bnez $16,
0x43f370` at 0x43f43c) that all three paths fall into.

**The weights are the Vert's four "colour" floats** at +0x20, which the decomp
names `Hmx::Color`. They sum to 1.0 on every gameplay vert and index the
palette in order. The owner's cached `this+0xa0` was never stale in a way that
mattered: it is a rigid approximation of a weighted blend, which is why it
fit to a few pixels and could not close.

## Calibrating again

`GHPC_TL_CAL=N` prints the first N primitives the PS2 path submits, tagged
with the mesh that produced them; `GHPC_TL_CAL_DRAWS` caps the draws (default
3) and `GHPC_TL_CAL_ANY` drops the wait for `GamePanel::StartGame`. The
pairing is exact because DMA, VIF1, VU1 and GIF all run inline on the guest
thread inside the store to D1_CHCR (`ps2_memory.cpp:2094`), so a tag set at
DrawFaces entry covers exactly that draw's submits. The tap is
`ghpcTlCalNoteSubmit` in `gs_frontend.cpp`, off unless the mesh tag is set.

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
- ~~Skinning~~. Settled: it runs on VU1, against a four-matrix palette at
  qw660..675 weighted by the Vert's `Hmx::Color` floats. The backend's vertex
  contract therefore needs the weights and the palette, not just pos/norm/uv.
  Still open is what happens above four bones, since `DrawShowing` only ever
  builds four slots (`mBones+0x08/+0x14/+0x20/+0x2c`).
