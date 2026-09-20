# What VU1 does that the native draw does not

2026-09-19. Read statically off the microcode, which turned out to be in the
ELF after all. No runs, no builds.

## Answers

1. **Backface culling: NO.** The microcode contains zero OPMULA/OPMSUB and one
   MSUB in 13,872 bytes. No cross product, no signed area, no normal-versus-view
   dot anywhere. The striper's winding-correction path is compiled in but
   disabled (`OneSided = 0` from both callers), which is what you do when
   nothing downstream culls. Our doubled triangle count is not a bug.
2. **Vertex colour: per-vertex, and VU1 lights it.** RGBA = `FTOI0(qw695 *
   vertexQw2)`. `vertexQw2` is written by a lighting subroutine that VU1 JALRs
   before the T&L loop, chosen by light count, reading the per-vertex normal at
   `vertexQw1` and up to 3 light directions plus colours at qw681..687.
   `0x80,0x80,0x80,0x80` is wrong for any lit mesh.
3. **PRIM flags: abe is NOT always 0, and fge does get set.** The whole GIFtag
   is built on the EE. `PsMat::Select` ORs IIP always, TME when a texture is
   bound, **ABE for every blend mode except 1**, and **FGE when
   `mat+0x128 && [0x444C70]+0x50`**. FST is never set. VU1 only patches NLOOP.
4. **Near-plane clipping: VU1 really clips and generates vertices.** Per
   triangle it drops the whole thing only for "any vertex past the far plane"
   or "all three behind near"; anything else that touches a plane goes to a
   real interpolating clipper at 0x14d8 that lerps position, uv and colour and
   re-emits as a **triangle fan**. Our whole-triangle `clip.w <= 1e-4` reject
   loses geometry.
5. **Strip vs list: same triangles, different order, winding NOT preserved,
   and it does not matter** because nothing culls. Strips are separate (no
   degenerate stitching); the first two vertices of each strip carry a negated
   index and VU1 ADC-suppresses them. Coverage is equal to `Face[]`.

---

## How this was read

The microcode is in the ELF. `.DVP.overlay.*` sections (18 of them, 13,872
bytes total, `ghpc/notes/m0-findings.md:43`) are empty in the file; they are
name/size metadata only. The bytes live in **`.vutext`**, ELF section 3,
vaddr 0x00437100, file offset 0x338100, size 0x3710, which is a VIF command
stream. Walking it for VIFcode 0x4A (MPG) recovers all 18 blocks and their
VU1 load addresses exactly (MPG ADDR is in 8-byte instruction units, so
`ADDR * 8` is the byte pc the census prints):

```
0x09c8 0x0be0 0x0cd8 0x14d8 0x1a38 0x1b48 0x1b98 0x1ca0 0x1e30
0x2080 0x21b0 0x29b0 0x2e60 0x30a0 0x30b0 0x3730 0x3760 0x3e28
```

Sum 13,872 bytes, matching m0 exactly. Tooling written for this round lives in
the session scratchpad (`mpgwalk.py` extracts the blocks, `vudis.py`
disassembles them); opcode tables were lifted from the repo's own interpreter,
`ps2xRuntime/src/lib/vu/ps2_vu1_upper.cpp` (upper op = `instr & 0x3F`, special
group 0x3C..0x3F selected by `(instr & 3) | ((instr >> 4) & 0x7C)`) and
`ps2_vu1_lower.cpp` (lower op = `(instr >> 25) & 0x7F`).

Cross-check that this is the right code: `PsMesh::UpdateFacePacket 0x43a810`
writes the kick as `0x1400019B`, MSCAL imm 0x19B = 411, and `411 * 8 = 0xCD8`.
That is the program the census calls `pc0xcd8` and it is the one disassembled
below.

## The 0xcd8 program, start to end

```
0cd8  XTOP vi05                          ; vi05 = TOP, the input double buffer
0ce0  ILW.w  vi01, 0(vi05)               ; header.w = shader program address
0ce8  ILW.x  vi14, 688(vi00)             ; qw688.x = lighting program address
0cf0  ILW.x  vi15, 689(vi00)             ; qw689.x = material program address
0d00  JALR vi01                          ; three per-batch subroutine calls
0d10  JALR vi14
0d20  JALR vi15
0d30..0d98  compose vf03..vf06 = qw676..679 rows x qw700..703
0da8  IADDIU vi10, vi00, 0x2e            ; 46, the max vertices per GIFtag
0dc8  <LOI 2048>   -> 0dd0 MULi.w vf21, vf00, I   ; vf21.w = 2048.0
0dd8  <LOI 255>                                    ; the fog clamp
```

Those three JALRs are the whole story for question 2. Their targets come from
the packet header and from VU1 memory that `PsMat::Select` and
`PsEnviron::Select` fill, and all of them are real overlays:

| source | value | x8 | overlay |
|---|---|---|---|
| header.w, `UpdateFacePacket 0x43a86c` | 0x369 / 0x373 / 0x394 / 0x3C6 | 0x1b48 / 0x1b98 / 0x1ca0 / 0x1e30 | present |
| qw688.x, from global 0x440E24 set by `PsEnviron::Select 0x1c79c8` | 0x436 / 0x6EC / 0x7C5 | 0x21b0 / 0x3760 / 0x3e28 | present |
| qw689.x, `mat+0x12c` | 0x347, 0x614 | - / 0x30a0 | present |

Reads of the matrix block qw660..679 by overlay, which is how the skin
variants show up: 0x1e30 has 16 (four matrices), 0x1ca0 has 12, 0x1b98 has 8,
0x1b48 is 80 bytes. Reads of the light block qw681..687: 0x3760 has 19,
0x21b0 has 11, 0x3e28 has 2. That is the light-count split
`PsEnviron::Select` selects between.

### The per-vertex loop, 0x0e98..0x10b8

Four vertices in flight in vf14..vf17, software pipelined. Per vertex:

```
MULAw ACC, vf06, vf00w
MADDAx ACC, vf03, vf14x
MADDAy ACC, vf04, vf14y
MADDz  vf14, vf05, vf14z          ; clip = pos * composed matrix, row vector
CLIP.xyz vf14, vf14w              ; frustum flags into the CLIP register
DIV Q, vf00w, vf14w               ; Q = 1/clip.w
MULq.xyz vf14, vf14, Q
MULAw.w  ACC, vf13, vf00w         ; ACC.w  = qw697.w
MULAw.xyz ACC, vf13, vf14w        ; ACC.xyz = qw697.xyz * vf14.w
MADD.xyzw vf14, vf12, vf14        ; screen = clip*Q*qw696 + qw697
MINIi.w vf14, vf14, I(255)        ; clamp the fog lane high
MAXx.w  vf14, vf14, vf00x         ; clamp low at 0
ADD.w   vf14, vf14, vf22          ; + the ADC accumulator (0 or 2048)
FTOI4.xyzw vf14, vf14             ; x,y to 12.4; w to (fog<<4) | (adc?0x8000:0)

LQ.xyzw vf18, 2(viN)              ; per-vertex colour quadword
MUL.xyzw vf18, vf11, vf18         ; vf11 = qw695
FTOI0.xyzw vf18, vf18             ; -> RGBA integers

LQ.xy vf20, 3(viN)                ; uv
MULq.xyz vf20, vf20, Q            ; -> s*q, t*q

SQ vf20, 0(vi04)   SQ vf18, 1(vi04)   SQ vf14, 2(vi04)
```

Output stride is 3 quadwords per vertex in the order **ST, RGBAQ, XYZF2**,
which is exactly `REGS = 0x412, NREG = 3` in the GIFtag template the EE builds
at qw680 (`DrawShowing 0x43f038`, VIF header `0x6C0102A8`, ADDR 0x2A8 = 680).
The transform itself matches the measured convention in
`evidence/2026-09-12-bone-palette.txt` instruction for instruction, so that
result is now confirmed from the code as well as from matched pairs.

Two new facts fall out of the .w lane:

- **`vf14.w` is a fog value.** `qw696.w * clip.w + qw697.w`, clamped to
  [0, 255], packed into F of the PACKED XYZF2 qword. Those two w lanes are the
  ones `PsCam::Select` deliberately does not write (STMASK 0xC0C0C0C0), and
  `PsEnviron::Select 0x1c79c8` writes exactly them with the complementary mask
  `0x3F3F3F3F` behind `0x700202B8`. So fog is an environment property, not a
  camera one.
- **`vf22..vf25` are the ADC flags.** `MR32.w vfNN, vf00` sets the lane to 0
  (draw), `MUL.w vfNN, vf21, vf00` sets it to 2048.0 (suppress), and FTOI4's
  x16 turns 2048 into 0x8000, which is bit 15 of the XYZF2 w word, the ADC bit.

## 1. Backface culling

**No culling. Confident.**

Census of upper ops over all 18 overlays:

- `OPMULA` / `OPMSUB`, the VU cross-product pair: **0 occurrences.**
- `MSUB` / `MSUBA` anywhere: **1**, in overlay 0x2e60 (the 2D/sprite program).
- `ESUM` / `ELENG` / `ERLENG` / `ESADD`, the EFU dot and length ops: **1** in
  total across the whole microcode.

Upper-op histogram of 0x0cd8 (256 pairs) and 0x30b0 (208 pairs), the two
geometry programs, is entirely accounted for by the transform, CLIP, the Q
multiply, the fog clamp and the two FTOI packs. There is no unexplained
arithmetic left over to be a cull test, and nothing reads two vertices'
screen positions at once in the main loop.

Only 4 overlays XGKICK at all: 0x0cd8 (twice), 0x14d8 (the clipper), 0x30b0,
0x2e60. None of them computes an area.

Corroboration from the EE side: `RndMesh::CreateStrip 0x1f2368` passes the
striper's `OneSided` flag from its `b` parameter into `STRIPERCREATE+0x10`,
`Striper::Init 0x32f418` maps it to `striper+0x20`, and
`Striper::ComputeBestStrip 0x32f7e8` gates its whole winding-correction block
on it at 0x32fbfc. **Both callers pass 0**: `UpdateFacePacket` at 0x43a9d8 and
`RndMesh::Save 0x1f2490` at 0x1f2af8, each with `daddu $t1, $zero, $zero` in
the delay slot. A renderer that culled could not ship with that disabled.

Sibling-engine corroboration: `RndMat` does carry a `mCull` bit (RB3
`src/system/rndobj/Mat.h:301-310`, defaulted on), but the only consumer in
that tree is `GXSetCullMode` on the Wii. The GS has no equivalent and GH2's
VU1 does not synthesise one.

**Do not trust the ADC rate as cull evidence.** `work/r6long.log` shows
`t4.s2 = 42,939,243 / 77,991,288` (55%) of tristrip closing vertices
ADC-suppressed, and r5c/r6/r6b sit at 45-51%. That looks like culling and is
not. Those runs predate the projection-matrix fix, and
`ps2_vu1_upper.cpp:478-481` records why: when `w` is zero or denormal the CLIP
limit collapses to the largest denormal and every coordinate clips. The real
sources of ADC are strip restarts and clip rejection, both explained below.

## 2. Vertex colour and lighting

**Per-vertex, lit on VU1, from the normal. Our 0x80 constant is wrong.**

`PsMesh::UpdateFacePacket 0x43a810` packs **4 quadwords per vertex**:

| qw | source | unpack |
|---|---|---|
| 0 | `Vert+0x00` position | `0x68..` V3-32, NUM 2*nVerts, STCYCL CL=4 WL=2 (0x43ad14) |
| 1 | `Vert+0x10` **normal** | same unpack, second V3 of the pair |
| 2 | `Vert+0x20` colour / bone weights | V4-32, NUM nVerts, ADDR base+2 (0x43ad94) |
| 3 | `Vert+0x30` uv | V2-32, NUM nVerts, ADDR base+3 (0x43adfc) |

The dynamic path (`+0x140 & 0x1f` set, 0x43ac84) instead emits one DMAtag REF
per vertex, `((vertBase + index*64) << 32) | 0x30000004`, QWC 4, so the same
4-quadword layout straight out of the live `Vert[]`. **The normal is always
uploaded.** It is never read by 0x0cd8 or 0x30b0, so it is read by one of the
three JALR'd subroutines.

The light data is at qw681..687, uploaded by `PsEnviron::Select 0x1c79c8`
behind `0x6C0702A9` (V4-32, NUM 7, ADDR 0x2A9 = 681) at 0x1c7bf8. Payload is,
per light, a normalized and negated direction (`vmul/vadd/vrsqrt/vmulq` at
0x1c7d70..0x1c7d84 then three `neg.s` at 0x1c7d94..0x1c7db0) plus the light
colour from `light+0xC0` (0x1c7dc0), for up to 3 lights (`slt $s7(2), $s2` at
0x1c7de8). The same function picks the VU1 program address by light count and
stores it to global 0x440E24 (0x436 / 0x6EC / 0x7C5), which `PsMat::Select`
reads at 0x43ec94 and forwards as qw688.x, which 0x0cd8 JALRs at 0x0d10.

Overlays 0x3760 and 0x21b0 read qw681..687 nineteen and eleven times
respectively. That is the lighting.

Material colour reaches VU1 as **qw690**, `lq [mat+0x30]` at 0x43ed30, inside
the `0x6C0802B0` unpack (V4-32, NUM 8, ADDR 688) that `PsMat::Select` issues
at 0x43ec4c, so qw688..695 is one material block:

```
qw688.x  lighting program address (from 0x440E24)
qw689.x  material program address (mat+0x12c)
qw690    mColor, material colour (w overwritten with 0.035f when mat+0x130 == 2)
qw691..694  sphere / environment map matrix (mat+0x170, or 0x4F3210 after
            PsMat::UpdateSphereXfm 0x1c1f40 when mat+0x12c == 0x347)
qw695    mat+0x160, the final RGBA scale the T&L loop multiplies by
```

So the colour chain is: lighting subroutine writes a lit colour into each
vertex's qw2, then the T&L loop does `FTOI0(qw695 * qw2)`.

Consequence for the native draw: on a **skinned** mesh, `Vert+0x20` is the
bone weights, so the lighting subroutine must be overwriting qw2 before the
T&L loop reads it. There is no path where 0x80 is right except a mesh whose
`Vert+0x20` really is white and whose lighting program is a passthrough. The
observed submits agree: `[gs/submit]` in `work/r6.log` and `work/r6b.log`
shows 13 distinct vertex colours across 24 sampled tristrip draws, 12 of them
non-0x80, for example `(83,79,77,128)`, `(105,103,101,128)`,
`(118,114,114,128)`, all at `tfx=0` (MODULATE).

**Unknown, and why.** Which of 0x21b0 / 0x3760 / 0x3e28 implements which light
model, and the exact formula, was not disassembled this round. I confirmed the
inputs and the wiring, not the arithmetic. Reading 0x3760 (217 pairs) end to
end is the next step and needs no runtime.

## 3. PRIM flags

**abe tracks the blend mode, fge exists, fst never sets, and VU1 never writes
PRIM at all.**

`PsMesh::DrawFaces 0x43eea0` writes no VIF or GIF word whatsoever; it locks
`+0x150`, wraps it in DMA tags and sends. The GIFtag template is built by
`PsMesh::DrawShowing 0x43f038` and uploaded as a single quadword:

- **qw680**, VIF header `0x6C0102A8` at 0x43f34c, data at 0x43f3ac..0x43f3d4:
  `NLOOP = 0`, `EOP = 1`, `PRE = 1`, `FLG = 0` (PACKED), `NREG = 3`,
  `REGS = 0x412` = ST, RGBAQ, XYZF2, and `PRIM = [sp+0]`.
- **qw994**, VIF header `0x6C0103E2` at 0x43f360, same tag but
  `PRIM = ([sp+0] & ~7) | 5`, a **triangle fan**. This is the clipper's output
  tag.

`[sp+0]` is seeded to 4 (tristrip) at 0x43f348/0x43f378 and then handed to
`PsMat::Select 0x43ea70` by reference (`$a1 = $sp`, the `RUl` in the mangled
name) at 0x43f374. Select's flag block, 0x43ee14..0x43ee7c, decoded:

```
0x43ee18  v1 = 8                        ; IIP, always
0x43ee1c  a0 = [mat+0x128]              ; material fog participation
0x43ee24  beqz a0 -> 0x43ee40           ; delay: a2 = prim | 8
0x43ee34  v1 = [0x444C70]               ; global renderer
0x43ee38  a0 = [v1+0x50]                ; global fog enable
0x43ee3c  movz a1, zero, a0             ; a1 = (global fog != 0)
0x43ee40  v0 = [mat+0x134]              ; texture pointer
0x43ee44  beqz v0 -> 0x43ee5c           ; delay: v1 = a1 << 5   ; bit 5 = FGE
0x43ee4c  v0 = 0x10                     ; TME
0x43ee50  v0 = v1 | v0
0x43ee58  a1 = a2 | v0
0x43ee60  v1 = [mat+0x2c]               ; blend mode
0x43ee68  beq v1, 1 -> 0x43ee78         ; delay: v0 = 0x40      ; bit 6 = ABE
0x43ee74  v0 = a1 | 0x40                ; blend != 1: ABE ON
0x43ee78  v0 = a1                       ; blend == 1: ABE OFF
0x43ee7c  sd v0, 0($s2)
```

So, per draw:

- **IIP** always 1.
- **TME** 1 iff `mat+0x134` (the RndTex pointer) is non-null. 379 of 407
  material selects carry one (`rnd-seam.md`).
- **FGE** = `mat+0x128 != 0 && [0x444C70]+0x50 != 0`. It does get set, and VU1
  computes a real F value to go with it. Our XYZ2 writes carry no F at all.
- **ABE** = `mat+0x2c != 1`. Blend 1 is `kBlendSrc`, "don't blend this material
  at all" in the engine's own tooltip (RB3 `Mat.h:126-143`), so opaque
  materials get abe=0 and everything else gets abe=1. rnd-seam records modes 1
  and 3 dominating, and the sampled submits in `work/r6.log` are all
  `abe=1 alpha=0x44 test=0x3200d`, i.e. blend 3 = `kBlendSrcAlpha`.
- **FST** (bit 8) is never set, so ST/Q always, matching the measurement.
- **CTXT** is not touched here either.

VU1's only contribution to the tag is NLOOP. At 0x1160:

```
1160  MTIR   vi11, vf02x            ; the tag address
1168  IADDIU vi01, vi00, 0x402e
1170  IADDIU vi01, vi01, 0x4000     ; 0x802E = EOP | NLOOP 46
1178  ISUB   vi01, vi01, vi10       ; minus the remaining count
1180  ISW.x  vi01, 0(vi11)          ; word 0 only, PRIM lives in words 1..3
11a0  XGKICK vi11
```

`ISW.x` writes only bits 0..31, so PRE/PRIM/FLG/NREG/REGS are untouched.

**Action:** the native draw must read the PRIM field out of the qw680 tag the
EE just built (or recompute it from `mat+0x2c`, `mat+0x134`, `mat+0x128` and
the 0x444C70 global) rather than hardcode `iip|tme`.

## 4. Near-plane clipping

**VU1 clips for real, generating interpolated vertices, and emits them as a
triangle fan.**

The per-triangle test in the main loop, once three vertices have gone through
CLIP:

```
0ef0  FCAND 0x03ffff        ; any clip flag set on any of the last 3 vertices?
0ef8  IBNE vi01, vi00 -> 0x1250   (one of five handlers, one per pipeline slot)
```

`0x03ffff` is the low 18 bits, which is three 6-bit CLIP groups. Bit order
from `ps2_vu1_upper.cpp:461-472`: bit0 `x > +w`, bit1 `x < -w`, bit2 `y > +w`,
bit3 `y < -w`, bit4 `z > +w`, bit5 `z < -w`.

The handler at 0x1250 (identical at 0x12a8, 0x1300, 0x1358, 0x13b0):

```
1250  SUB.w vf00, vf21, vf22  | FCAND 0x010410
1258  MUL.w vf22, vf21, vf00  | IBNE vi01, vi00 -> back to the loop
1260                          | FCOR  0xfdf7df
1268                          | IBNE vi01, vi00 -> back to the loop
1270                          | FSAND vi01, 0x001        ; status Z bit
1278                          | IBNE vi01, vi00 -> back to the loop
1280..1290                    | vi13/vi14/vi15 = the three vertex pointers
1298  B 0x1408                | vi02 = the return address
```

Decoded:

- `FCAND 0x010410` is bit 4 in each of the three groups: **any vertex past the
  far plane rejects the whole triangle**, with the ADC set by the
  `MUL.w vf22, vf21, vf00` in the same slot. This is the PS2 behaviour the
  engine documents in RB3 `src/system/rndobj/Cam.h:87-92`: "on the PS2, object
  polys are culled rather than clipped to the far plane".
- `FCOR 0xfdf7df` is true iff bit 5 is set in all three groups: **all three
  vertices behind the near plane rejects the triangle**.
- `SUB.w vf00, vf21, vf22` plus `FSAND vi01, 0x001` tests the status Z bit on
  `2048 - vf22.w`, that is, "is this vertex already ADC'd", which is the strip
  restart case. If so, skip the clipper.
- Anything else, including **any x or y crossing** and a **partial near
  crossing**, falls through to 0x1408, which saves vi03/vi04/vi10/vi12 to
  qw1022 and vi05/vi13/vi14/vi15 to qw1023, writes an empty `0x8000` tag
  (EOP, NLOOP 0) to qw994.x, XGKICKs the batch so far, and enters the clipper.

The clipper is overlay **0x14d8**, and it interpolates every attribute:

```
1640  DIV Q, vf01w, vf07w              ; t from the two w values
1648  SUB.xyzw vf27, vf27, vf26        ; position delta
1650  SUB.xyzw vf29, vf29, vf28        ; uv delta
1658  SUB.xyzw vf31, vf31, vf30        ; colour delta
1678  MULq.xyzw vf27, vf27, Q
1680  MULq.xyzw vf29, vf29, Q
1688  MULq.xyzw vf31, vf31, Q
1698  ADD.xyzw vf26, vf27, vf26
16a0  ADD.xyzw vf28, vf29, vf28
16a8  ADD.xyzw vf30, vf31, vf30
16b8  CLIP.xyz vf26, vf26w | SQ vf26, 2(vi04)
16c0  CLIP.xyz vf26, vf26w | SQ vf28, 0(vi04)
16c8                       | SQ vf30, 1(vi04)
16d0  IADDIU vi04, vi04, 3 ; emit one new vertex, 3 quadwords
16d8  IADDIU vi15, vi15, 1 ; output vertex count
16e0  FCGET vi01           ; keep the new vertex's own clip flags
```

It has its own XGKICK at 0x18d0 and its own 11 CLIP instructions for the
iterative plane passes. The result is drawn under the qw994 tag, PRIM 5,
triangle fan, which is why the kick census shows large `t5` counts alongside
`t4` (`work/r6long.log`: `t4 = 83,886,951`, `t5 = 23,611,664`).

**Consequence for the native draw:** our `clip.w <= 1e-4` whole-triangle
reject is strictly worse than VU1 in two ways. It drops triangles that VU1
would clip and keep, and it does not drop triangles past the far plane that
VU1 does drop. The cheapest faithful replacement is a host Sutherland-Hodgman
against near plus the far-plane whole-triangle reject; x and y can be left to
the GS scissor rather than clipped, since GS coordinates are 12.4 in a 4096
space and our `xyz2()` already clamps.

## 5. Strip vs list

**Same triangles, order not guaranteed, winding not preserved, and winding
does not matter.**

`RndMesh::CacheStrips 0x1f2318` is only a predicate (globals at 0x5A5C08+0x10
and +0xc, owner is self, faces non-empty, verts non-zero). `CreateStrip
0x1f2368` wraps Pierre Terdiman's Striper: `WFaces = mesh+0x108 + firstFace*6`
at 0x1f2370 (the 3 x u16 Face stride), `AskForWords = 1`, `ConnectAllStrips =
0`, `OneSided = $t1`, then `Striper::Init 0x32f418` and `Striper::Compute
0x32f4e8`, and finally a prefix-sum over the per-strip lengths at
0x1f2438..0x1f2474.

`OneSided = 0` from both callers (`UpdateFacePacket` 0x43a9d8,
`RndMesh::Save` 0x1f2af8), so `Striper::ComputeBestStrip 0x32f7e8` skips its
entire correction block at 0x32fbfc, which is where the reference
implementation does the odd-length reversal, the 3/4-vertex index swap and the
leading-vertex duplication. **Winding is therefore whatever the striper
produced, not `Face[]`'s.** `ConnectAllStrips = 0` means strips stay separate,
so no degenerate stitching either.

Strip boundaries are marked by sign instead. `UpdateFacePacket` emits each
index as `localIndex * 4 + 1` (0x43aa68, a VU1 quadword address into the
4-quadword vertex block, +1 for the header at qw0), and at a strip boundary
sets `$t1 = 2` at 0x43aae8 and negates the next two emitted shorts in place
(0x43aaac..0x43aac0, `lhu/negu/sh`). The T&L loop reads them back and branches
on the sign:

```
0ee8  IBGTZ vi00, vi07 -> 0x0f00      ; positive index: skip the next line
0ef8  MUL.w vf23, vf21, vf00 | ISUB vi07, vi00, vi07
                                      ; negative: set ADC, negate to get the index
```

So the first two vertices of every strip are ADC-suppressed, exactly the two
triangles that would otherwise wrap across the boundary. Coverage is one
triangle per source face, no more and no less.

Because neither VU1 nor the GS culls, **a triangle list built straight from
`Face[]` is a correct substitute for the strips**, and our native draw is
already right on this point. The only thing that changes is submission order,
which matters only for coincident-depth ties.

Batch geometry, for reference: the per-batch header quadword goes to TOPS+0
behind `0x6C018000` (0x43abb0), with `x` = unique vertex count, `y` = index
list start (`4*nVerts + 1`), `z` = one past the index list end, `w` = the
shader program address. Those `y` and `z` are the `header.y` / `header.z` the
loop-runaway notes describe. A batch is closed when
`4*nVerts + 1 + nIdxQw >= 0x14B` (331 quadwords, 0x43abd4), and the first
batch kicks `0x1400019B` (MSCAL 0xcd8) while every later batch kicks
`0x17000000` (MSCNT).

## Gap list for the native draw, in priority order

1. **Lighting.** No colour source exists host-side. Needs the normal at
   `Vert+0x10`, the light block at qw681..687, `mColor` at qw690, the scale at
   qw695, and whichever formula 0x3760 / 0x21b0 / 0x3e28 implements. Until
   then every lit mesh is flat grey.
2. **PRIM.** Read it off the qw680 GIFtag rather than hardcoding. ABE and FGE
   both vary per material.
3. **Near clipping.** Replace the whole-triangle reject with a near-plane clip
   plus a far-plane reject.
4. **Fog.** VU1 emits XYZF2 with `F = clamp(qw696.w * clip.w + qw697.w, 0, 255)`
   and FGE can be on. Our XYZ2 carries no F.
5. **Backface culling.** Nothing to do. Do not add it.

## Runtime probes, only where static reading could not close it

- **Lighting formula.** If disassembling 0x3760 turns out to be ambiguous,
  `GHPC_VU1_DUMP` plus a breakpoint-style dump of qw2 for a known vertex before
  and after the qw688.x JALR would give input/output pairs to fit against, the
  same method that settled the vertex transform.
- **Which header.w variant a gameplay mesh actually takes.** Print
  `mat+0x2c / +0x20 / +0x14` and the resulting `header.w` from the
  `UpdateFacePacket` override, one line per mesh, and histogram it. That says
  whether 0x1e30's four-matrix block is the skinning path or something else.
- **Whether FGE is ever actually on in gameplay.** Print
  `[0x444C70]+0x50` and `mat+0x128` from the `PsMat::Select` override. One run
  on `game_screen` settles whether fog is worth implementing at all.
