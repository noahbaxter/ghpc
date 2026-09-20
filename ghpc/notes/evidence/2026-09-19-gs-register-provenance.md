# GS register provenance at the MSCAL seam

Read-only analysis, 2026-09-19. Evidence is the generated bodies in
`work/output/` (disassembly is in the `// 0xADDR:` comments), the overrides in
`ghpc/override/`, and the runtime GS/VU1 sources. Nothing was built or run.

## Answers

1. **CONFIRMED.** `PsRnd::SetRegister` 0x43e320 appends 16-byte A+D pairs
   (value at +0x0, GS register address at +0x8) at the scratchpad cursor
   0x70000008, inside a GIFtag opened by `DmaPacket::OpenGifTag` 0x43e988 with
   REGS=0xE, FLG=PACKED, NREG=1, and that GIFtag lives inside a DMAtag whose
   TTE slot holds VIFcode `0x50000000` (DIRECT). It goes to the GIF through
   VIF1 DIRECT. VU1 never sees it. The design holds.
2. **Ordering holds, but not by the route assumed.** For a normal gameplay mesh
   (`+0x140 & 0x1f == 0`) `DrawFaces` never enters the chunk loop and never
   calls `FlushPacket`: it closes the current tag and appends a REF DMAtag
   (id 3) pointing at `mFacePacket` in main RAM (0x43ef34). The register A+D
   block and the geometry ride out on the same later kick, registers first in
   the byte stream, because the cursor is monotonic and `PsMat::Select` runs
   before `DrawFaces` (0x43f374 vs 0x43f434). The chunk loop at
   0x43ef70..0x43effc is the mutable-geometry path only, and there
   `FlushPacket` at 0x43eff0 fires **after** that chunk's bytes are already in
   the packet, so again registers precede geometry in one chain.
3. Material pushes ALPHA_1 0x42, DIMX 0x44, TEST_1 0x47, FBA_1 0x4a,
   CLAMP_1 0x08, ZBUF_1 0x4e (plus RGBAQ/ST/UV setup and a VU1 unpack);
   texture pushes TEX0_1 0x06, TEX1_1 0x14, MIPTBP1_1 0x34, MIPTBP2_1 0x36.
   Sources are the ready-made u64 images at PsMat +0x138/+0x140/+0x148/+0x150/
   +0x158 and PsTex +0x70/+0x78/+0x80/+0x88, plus PsMat +0xa4 for FBA.
4. **VU1 emits PRIM itself.** No guest `SetRegister` on the mesh path writes GS
   reg 0x00; the only three callers that do are `CopyBuf`, `ClearZBuffer` and
   `PsTex::FinishDrawTarget`. So the measured `prim=4 iip=1 tme=1 fst=0 abe=0`
   can only come from the VU1 kick (PRE/PRIM in the GIFtag, or PACKED reg 0).
   A native draw must write PRIM itself, which `ghpc_native_draw.cpp:197`
   already does. The runtime also carries a diagnostic that walks every chained
   tag in a VU1 kick counting A+D writes, added because TEX0 was seen being
   programmed that way; that census has no recorded result, so "VU1 emits only
   PRIM plus vertices" is not yet proven.
5. **SCISSOR_1 comes from `PsCam::Select` 0x1c19e4, once per camera select, not
   per draw. FRAME_1 and XYOFFSET_1 come from `MakeDrawTarget`
   (`PsRnd` 0x1bf074/0x1bf088, `PsTex` 0x1c6148), `PsTex::FinishDrawTarget` and
   `CopyBuf`, i.e. per render target, not per draw. XYOFFSET is NOT 0:** the
   one recorded gameplay sample reads `ofx=31744 ofy=30720` (1984, 1920 px).

---

## 1. SetRegister is VIF1 DIRECT

`work/output/SetRegister__5PsRndiUlUl_0x43e320.cpp`. Signature
`(PsRnd*, int reg, u64 value, u64 mask)`.

- 0x43e33c..0x43e370: read-modify-write of the shadow copy at `this+0xf0 +
  reg*8`, masked; returns early at 0x43e358 if the masked value already
  matches, so redundant writes never reach the packet.
- 0x43e390: loads 0x7000000c, the DmaPacket's "currently open GIFtag" pointer.
  0x43e39c reads `[giftag+8]` (REGS) and 0x43e3a4 compares it against 0xE. If
  an A+D block is already open, it appends to it.
- 0x43e3b8: otherwise `jal func_43E988` with `$a1=0` (FLG PACKED), `$a2=1`
  (NREG), `$a3=0xE` (REGS = A+D), `$t0=0` (PRE clear).
- 0x43e3c0..0x43e3e4: load the cursor from 0x70000008, bump it by 0x10, write
  the value at +0x0 and `$s0` (the register address) at +0x8. Exactly the A+D
  layout claimed.

`work/output/OpenGifTag__9DmaPacketQ29DmaPacket6FormatiUli_0x43e988.cpp` is
where DIRECT is pinned:

- 0x43e9d0: if no DMAtag is open (packet+0x10 == 0), open one at the cursor.
- 0x43e9f8/0x43e9fc: the DMAtag's own 64 bits are zeroed (QWC and ID patched
  later by `CloseDmaTag`).
- 0x43e9f4: `sw $a1, 0x8($v0)` with `$a1 = 0x10000000` -> VIFcode 0 = cmd 0x10
  FLUSHE.
- 0x43e9f0: `sw $a0, 0xC($v0)` with `$a0 = 0x50000000` -> VIFcode 1 = **cmd
  0x50 DIRECT**, IMM 0.
- 0x43ea04..0x43ea60: builds the GIFtag, FLG at bits 58-59 from `$s2`, NREG at
  60-63 from `$s3`, PRIM at 47-57 and PRE at 46 from `$s1` (both 0 here), REGS
  (`$s4` = 0xE) stored at +0x8.

`work/output/CloseGifTag__9DmaPacketbi_0x43e860.cpp` closes the loop:

- 0x43e8d4..0x43e908: NLOOP = (qwords - 1) / (NREG >> FLG), stored as the
  GIFtag's low halfword.
- 0x43e930..0x43e948: `lhu`/`sh` at **DMAtag+0xC**, i.e. the low 16 bits of the
  DIRECT VIFcode, incremented by the qword count. That is the DIRECT IMM being
  patched, which only makes sense if the block is a VIF1 DIRECT transfer.
- 0x43e95c: sets EOP (bit 15) on the GIFtag when asked.

Verdict: the register block is a VIF1 DIRECT payload handed straight to the
GIF. Nothing in it reaches VU1.

## 2. Ordering inside DrawFaces 0x43eea0

Entry: 0x43eef4 `jal func_321150` on `+0x150` (MemHandle lock) -> `$s3` = the
face packet pointer in main RAM. 0x43eefc reads `+0x140`, masks 0x1f, and
0x43ef04 branches on it.

**Path A, `(+0x140 & 0x1f) == 0` (static geometry, the common gameplay case).
Falls through to 0x43ef0c:**

    0x43ef18  CloseDmaTag(0x70000000, id=1 CNT, 0, 0)   close the A+D block's tag
    0x43ef20  OpenDmaTag(0x70000000)
    0x43ef34  CloseDmaTag(0x70000000, id=3 REF, $s3 facePacket, qwc=+0x154)
    0x43ef3c  OpenDmaTag(0x70000000)
    0x43ef44  b -> 0x43f004 epilogue

No `FlushPacket`, no SPR copy. The geometry is a REF tag appended to the same
scratchpad chain, after the A+D block. `CloseDmaTag` 0x43e788 confirms the id
and addr encoding: `$a1` = id -> bits 28-30, `$a2` = addr -> upper 32 bits with
the SPR bit shifted in at 0x43e80c, `$a3` = explicit QWC or, when zero, the
computed span at 0x43e7f0.

**Path B, `(+0x140 & 0x1f) != 0` (mutable geometry). The loop at
0x43ef50..0x43effc:**

    0x43ef78  CloseDmaTag(0x70000000, CNT, 0, 0)
    0x43ef80  PacketQWords(ThePsRnd) 0x1bf020 -> 0x1FF - (cursor - base)/16
    0x43ef98  chunk = min(free, remaining)          movz at 0x43efa0
    0x43efc4  DmaPacket::Set(local, src=$s3, 0) 0x1c10d0, cursor = $s1
    0x43efdc  DmaPacket::Send(local, type 9 toSPR, 0, dest=$s2 cursor) 0x43e6b8
    0x43efec  0x70000008 += chunkBytes
    0x43eff0  jal func_43E400 FlushPacket
    0x43effc  loop while remaining != 0

So the chunk's geometry bytes are copied into the scratchpad packet **before**
`FlushPacket` on that iteration. `FlushPacket` 0x43e400 is a single kick of the
whole packet: `CloseDmaTag(CNT)` at 0x43e414, early-out at 0x43e42c when cursor
== base, the MFIFO free-space spin at 0x43e478 (D1_TADR 0x10009030 against
D9_MADR 0x1000d010, masked 0x7fff0), `Send(type 8)` at 0x43e4b0, then the
scratchpad double-buffer flip between 0x70000020 and 0x70002010 via
`DmaPacket::Set` and a fresh `OpenDmaTag` at 0x43e4f8.

Either way the invariant the seam needs holds: **within one VIF1 chain, the A+D
register block always precedes the geometry that consumes it.** Call order in
`PsMesh::DrawShowing` 0x43f038 backs this up: `PsMat::Select` at 0x43f374,
`PsMesh::DrawFaces` at 0x43f434, `PsRnd::FlushPacket` at 0x43f458, all inside
the per-material loop. `CloseGifTag` at 0x43f090 closes any open A+D block
before DrawShowing's own VIF unpacks.

Caveat for a native seam: on path A nothing is flushed at DrawFaces time. If
the native transform runs at the MSCAL, the MSCAL is reached only on a later
kick, which is fine; but a native path that fires at DrawFaces entry instead
would run before those registers have been kicked.

## 3. The register set for a gameplay mesh draw

From `ghpc/override/Select__5PsMatRUl_0x43ea70.cpp:13-18,28` and
`ghpc/override/Select__5PsTexQ25PsTex5Blend_0x43f490.cpp:9-17`. Reg is the GS
register address passed as `$a1` to 0x43e320; mask is the `$a3` write mask.

`PsMat::Select` 0x43ea70:

| addr | reg | value from | mask |
| --- | --- | --- | --- |
| 0x43eb6c | 0x42 ALPHA_1 | PsMat +0x138 | 0xFF000000FF |
| 0x43eb90 | 0x44 DIMX | PsMat +0x140 | 0x7777777777777777 |
| 0x43ebbc | 0x47 TEST_1 | PsMat +0x148 | 0x6FFFF |
| 0x43ebd4 | 0x4a FBA_1 | `!(PsMat +0xa4)`, i.e. `!mAlphaWrite` | 1 |
| 0x43ebf4 | 0x08 CLAMP_1 | PsMat +0x150 | 0xF |
| 0x43ec08 | 0x4e ZBUF_1 | PsMat +0x158 | 1<<32 (ZMSK) |

Then 0x43ec20: if `PsMat +0x134` (PsTex*) is nonzero, `PsTex::Select(tex,
+0x130 as TFX)`. 379 of 407 logged draws carry a texture. After that, 0x43ec44
is a VIF UNPACK `0x6C0802B0` (V4-32, NUM 8, ADDR 688) of 8 quadwords of VU1
lighting/colour constants, including `mColor` at +0x30 into qw690. Those go to
VU1, not the GS.

`PsTex::Select` 0x43f490:

| addr | reg | value from | mask |
| --- | --- | --- | --- |
| 0x43f520 | 0x06 TEX0_1 | PsTex +0x70, after TFX (bits 35-36) <- blend at 0x43f4bc and TBP0 (bits 0-13) <- VRAM/render-target patch at 0x43f4f4 | 0 |
| 0x43f534 | 0x14 TEX1_1 | PsTex +0x78 | 0xFFF001803FD |
| 0x43f564 | 0x34 MIPTBP1_1 | PsTex +0x80, gated on +0x6c != 0 | 0x0FFFFFFFFFFFFFFF |
| 0x43f578 | 0x36 MIPTBP2_1 | PsTex +0x88, same gate | same |

Note the TEX0 mask is 0, so `SetRegister`'s "already equal" early-out at
0x43e358 can never fire for TEX0: it is re-emitted on every texture select.

Not set by either: PRIM 0x00, PRMODE, RGBAQ/ST/UV per vertex, SCISSOR, FRAME,
XYOFFSET, TEXA, TEXCLUT, COLCLAMP, PABE, DTHE, FOGCOL.

## 4. What VU1 emits itself

Census of every `jal func_43E320` in `work/output/`, resolving `$a1`:

    ClearZBuffer 0x1bfdc0        0x00 PRIM, 0x01, 0x47, 0x4e
    CopyBuf 0x1bf238             0x00 PRIM, 0x01, 0x06, 0x08, 0x14, 0x18,
                                 0x42, 0x44, 0x47, 0x4a, 0x4c, 0x4e
    PsTex::FinishDrawTarget      0x00 PRIM, 0x01, 0x16, 0x18, 0x3f, 0x47,
                                 0x4a, 0x4c, 0x08
    PsCam::Select 0x1c1770       0x01, 0x06, 0x40 SCISSOR_1, 0x47, 0x4a, 0x4e
    PsRnd::MakeDrawTarget        0x01, 0x18 XYOFFSET_1, 0x45, 0x4c FRAME_1, 0x4e
    PsTex::MakeDrawTarget        0x18, 0x45, 0x4c, 0x4e
    PsRnd::SwapBuffers           0x01, 0x1a, 0x46, 0x47, 0x4a, 0x4e
    PsMat::Select / PsTex::Select as in section 3
    PsRnd::Reset 0x1beb10        0x01, 0x3b, 0x49
    PsEnviron::Select 0x1c79c8   0x01, 0x3d
    DrawRect / DrawLine / DrawString / LoadImage / LoadMovie / LoadVram /
    CopyFromScreen / PsParticleSys::DrawShowing: 2D and upload paths only

The only three writers of GS reg 0x00 (PRIM) are `CopyBuf`, `ClearZBuffer` and
`FinishDrawTarget`, none of which is on the mesh draw path. Yet
`ghpc/notes/evidence/2026-09-11-vertex-transform-calibration.txt:20` records
mesh draws arriving at the frontend as `PRIM 4 (tristrip), iip=1 tme=1 fst=0
abe=0`. Therefore **VU1 supplies PRIM**, through either PRE+PRIM in the GIFtag
it builds (`gs_frontend.cpp:779` and `:886` write `GS_REG_PRIM` from
`(tagLo >> 47) & 0x7FF` when PRE is set) or a PACKED reg-0 descriptor
(`gs_frontend.cpp:1016`). A native draw that only wrote vertices would inherit
whatever PRIM was left over. `ghpc_native_draw.cpp:197` already writes PRIM, so
this is covered.

Open risk, not resolved here: `ps2_vu1_core.cpp:1219-1257` walks every chained
GIFtag in a VU1 kick counting A+D (reg 0xE) writes and their destination
registers, and the comment at `:1219-1223` states it exists because "TEX0 is
programmed through A+D (reg 0xE) writes that live in their own tag" and "it
stops being programmed after splash 1". If GH2's geometry microprograms also
patch TEX0 (or anything else) from inside the kick, a native draw must
reproduce it. That diagnostic prints `[vu1/ad] tagsWalked=... adWrites=...
destRegs:` every 20000 packets; there is no recorded run of it in
`ghpc/notes/evidence/`. **Run it and read `destRegs` on a gameplay frame before
trusting "vertices plus PRIM only".**

## 5. SCISSOR, FRAME, XYOFFSET

- **SCISSOR_1 (0x40)**: only `PsCam::Select` 0x1c1770, at 0x1c19e4. Per camera
  select (56 in a 150 s run against 407 material selects), not per draw.
- **FRAME_1 (0x4c)**: `PsRnd::MakeDrawTarget` at 0x1bf074, `PsTex::MakeDrawTarget`
  at 0x1c6148, `PsTex::FinishDrawTarget` at 0x1c6424, `CopyBuf`. Per render
  target.
- **XYOFFSET_1 (0x18)**: `PsRnd::MakeDrawTarget` at 0x1bf088,
  `PsTex::MakeDrawTarget` at 0x1c612c, `PsTex::FinishDrawTarget` at 0x1c6224
  region, `CopyBuf`. Per render target.

All three are stable across a frame apart from render-target switches, so they
are already at the GS when any MSCAL fires.

**XYOFFSET is not 0.** `ghpc/notes/evidence/2026-09-11-vertex-transform-calibration.txt:21`
records the first gameplay draw as `fbp=176 fbw=2, ofx 31744, ofy 30720`, i.e.
1984 and 1920 pixels in the GS 1/16-pixel units. The GS subtracts them:
`gs_frontend.cpp:2131-2134` and `gs_cpu_backend.cpp:1736-1737, 2255-2256,
2372-2373, 2489-2490` all do `x - (ofx >> 4)`. The runtime resets them to
`{0, 0}` at `gs_frontend.cpp:202`, which is a reset default, not the running
value.

Consequence for the native transform: writing 2048-centred vertices is correct
**only** because the guest's XYOFFSET is what un-centres them onto the target.
Since the transform already reproduces `x = clip.x*q*qw696.x + qw697.x` with
qw697 = 2048, it must write XYZ2 in that same window space and leave XYOFFSET
alone. Do not add a second re-centring, and do not assume ofx/ofy are 0; read
them from `GSContext` if the native path ever needs pixel coordinates. That
sample was a 128-pixel render target, so the values differ for the main frame;
neither is 0.
