// Override of work/output/Select__5PsTexQ25PsTex5Blend_0x43f490.cpp
// (PsTex::Select(PsTex::Blend), 0x43f490 - 0x43f5b4).
//
// Why: the texture bind. The body keeps the generated translation verbatim
// and, when GHPC_MESH_LOG is set, prints on entry the RndTex fields a native
// backend would consume plus the GS register images the body itself sends.
//
// What the body does with `this` ($s1) and the blend arg ($a1 & 3):
//   0x43f4bc  ld  +0x70   TEX0_1 image; bits 35-36 (TFX) <- blend, stored back
//   0x43f4c0  lw  +0x48   type word; & 8 -> a render target: TBP0 (bits 0-13)
//                         <- (*BufferEnv(ThePsRnd, +0x48 == 8) & 0x1ff) << 5,
//                         else jal PsTex::LoadVram 0x1c5b58
//   0x43f520  ld  +0x70   -> SetRegister(0x06 TEX0_1,    v, mask 0)
//   0x43f534  ld  +0x78   -> SetRegister(0x14 TEX1_1,    v, mask 0xFFF001803FD)
//   0x43f558  lw  +0x6c   nonzero -> the two MIPTBP writes below
//   0x43f564  ld  +0x80   -> SetRegister(0x34 MIPTBP1_1, v, mask 0x0FFFFFFFFFFFFFFF)
//   0x43f578  ld  +0x88   -> SetRegister(0x36 MIPTBP2_1, v, mask same)
//   0x43f598  *0x440e18 += 1 (a select counter)
// Register numbers are GS register addresses and the TEX1 mask is exactly
// TEX1's field set (LCM, MXL, MMAG, MMIN, MTBA, L, K), so the names are high
// confidence. TEX0 is logged as read at entry, before the TFX and TBP0 edits.
//
// RndTex layout, from gh2-decomp src/rndobj/RndTex.h (sizeof 0x70; PsTex is
// 0xb0, so +0x70.. is PsTex's own):
//   +0x28  RndBitmap mBitmap (0x1c bytes)  high, src/rndobj/Bitmap.h pins
//          u16 w @0, u16 h @2, u16 rowBytes @4, u8 bpp @6, u32 order @8,
//          pixels @0xc, palette @0x10, buffer @0x14, mip @0x18
//   +0x44  float mMipMapK                  high (ctor -8.0f and SetMipMapK)
//   +0x48  int, unnamed there              this body: & 8 = render target (medium)
//   +0x4c  int mWidth, +0x50 mHeight, +0x54 mBpp   high (SyncProperty names)
//   +0x6c  int, SetBitmap's bool arg        this body: gates MIPTBP, so mip
//          maps present (medium)
//   +0x70  u64 TEX0_1, +0x78 u64 TEX1_1 (K at bits 32-43 per SetMipMapK),
//   +0x80  u64 MIPTBP1_1, +0x88 u64 MIPTBP2_1   high, from this body
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

namespace {

void ghpcTexLog(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self, uint32_t blend, uint64_t n) {
    const uint32_t mipkBits = READ32(self + 0x44u);
    float mipk;
    std::memcpy(&mipk, &mipkBits, sizeof(mipk));
    const uint32_t type48  = READ32(self + 0x48u);
    const int32_t  width   = (int32_t)READ32(self + 0x4cu);
    const int32_t  height  = (int32_t)READ32(self + 0x50u);
    const int32_t  bpp     = (int32_t)READ32(self + 0x54u);
    const int32_t  mips6c  = (int32_t)READ32(self + 0x6cu);
    const uint64_t tex0    = READ64(self + 0x70u);
    const uint64_t tex1    = READ64(self + 0x78u);
    const uint64_t miptbp1 = READ64(self + 0x80u);
    const uint64_t miptbp2 = READ64(self + 0x88u);

    // GS TEX0 fields: TBP0 0-13, TBW 14-19, PSM 20-25, TW 26-29, TH 30-33,
    // TCC 34, TFX 35-36, CBP 37-50, CPSM 51-54, CSM 55, CSA 56-60, CLD 61-63.
    std::fprintf(stderr,
        "[ghpc/tex] #%llu this=0x%08x blend=%u w=%d h=%d bpp=%d mipk=%g type48=0x%08x mips6c=%d tex0=0x%016llx tbp0=0x%x tbw=%u psm=%u tw=%u th=%u tcc=%u tfx=%u cbp=0x%x cpsm=%u tex1=0x%016llx miptbp1=0x%016llx miptbp2=0x%016llx\n",
        (unsigned long long)n, self, blend, width, height, bpp, mipk, type48, mips6c,
        (unsigned long long)tex0,
        (unsigned)(tex0 & 0x3fffu), (unsigned)((tex0 >> 14) & 0x3fu), (unsigned)((tex0 >> 20) & 0x3fu),
        (unsigned)((tex0 >> 26) & 0xfu), (unsigned)((tex0 >> 30) & 0xfu), (unsigned)((tex0 >> 34) & 1u),
        (unsigned)((tex0 >> 35) & 3u), (unsigned)((tex0 >> 37) & 0x3fffu), (unsigned)((tex0 >> 51) & 0xfu),
        (unsigned long long)tex1, (unsigned long long)miptbp1, (unsigned long long)miptbp2);

    const uint32_t b = self + 0x28u;
    uint32_t w[7];
    for (uint32_t i = 0; i < 7; ++i) w[i] = READ32(b + i * 4u);
    std::fprintf(stderr,
        "[ghpc/tex]   bitmap28=(0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x) w=%u h=%u rowBytes=%u bpp=%u order=0x%08x pixels=0x%08x palette=0x%08x buffer=0x%08x mip=0x%08x\n",
        w[0], w[1], w[2], w[3], w[4], w[5], w[6],
        (unsigned)READ16(b + 0u), (unsigned)READ16(b + 2u), (unsigned)READ16(b + 4u), (unsigned)READ8(b + 6u),
        w[2], w[3], w[4], w[5], w[6]);
}

} // namespace

// Function: Select__5PsTexQ25PsTex5Blend
// Address: 0x43f490 - 0x43f5b4
void Select__5PsTexQ25PsTex5Blend_0x43f490(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Select__5PsTexQ25PsTex5Blend_0x43f490");
#endif

    switch (ctx->pc) {
        case 0x43f4ecu: goto label_43f4ec;
        case 0x43f518u: goto label_43f518;
        case 0x43f534u: goto label_43f534;
        case 0x43f558u: goto label_43f558;
        case 0x43f578u: goto label_43f578;
        case 0x43f590u: goto label_43f590;
        default: break;
    }

    // Fresh entry only (a resume above jumps past this). $a0 is `this`, $a1
    // the Blend, which the body masks to 2 bits before packing it as TFX.
    {
        static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
        if (s_log) {
            static uint64_t s_calls = 0;
            const uint64_t n = ++s_calls;
            if (n <= 40 || (n % 500) == 0) {
                ghpcTexLog(rdram, ctx, runtime, GPR_U32(ctx, 4), GPR_U32(ctx, 5) & 3u, n);
            }
        }
    }

    ctx->pc = 0x43f490u;

    // 0x43f490: 0x27bdffd0  addiu       $sp, $sp, -0x30
    ctx->pc = 0x43f490u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967248));
    // 0x43f494: 0x30a50003  andi        $a1, $a1, 0x3
    ctx->pc = 0x43f494u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) & (uint64_t)(uint16_t)3);
    // 0x43f498: 0x7fb10010  sq          $s1, 0x10($sp)
    ctx->pc = 0x43f498u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 16), GPR_VEC(ctx, 17));
    // 0x43f49c: 0x3c039fff  lui         $v1, 0x9FFF
    ctx->pc = 0x43f49cu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)40959 << 16));
    // 0x43f4a0: 0x3463ffff  ori         $v1, $v1, 0xFFFF
    ctx->pc = 0x43f4a0u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)65535);
    // 0x43f4a4: 0x319b8  dsll        $v1, $v1, 6
    ctx->pc = 0x43f4a4u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << 6);
    // 0x43f4a8: 0x3463003f  ori         $v1, $v1, 0x3F
    ctx->pc = 0x43f4a8u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)63);
    // 0x43f4ac: 0x7fb00020  sq          $s0, 0x20($sp)
    ctx->pc = 0x43f4acu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 32), GPR_VEC(ctx, 16));
    // 0x43f4b0: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x43f4b0u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f4b4: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x43f4b4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x43f4b8: 0x528fc  dsll32      $a1, $a1, 3
    ctx->pc = 0x43f4b8u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) << (32 + 3));
    // 0x43f4bc: 0xde220070  ld          $v0, 0x70($s1)
    ctx->pc = 0x43f4bcu;
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 17), 112)));
    // 0x43f4c0: 0x8e260048  lw          $a2, 0x48($s1)
    ctx->pc = 0x43f4c0u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 72)));
    // 0x43f4c4: 0x431024  and         $v0, $v0, $v1
    ctx->pc = 0x43f4c4u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & GPR_U64(ctx, 3));
    // 0x43f4c8: 0x451025  or          $v0, $v0, $a1
    ctx->pc = 0x43f4c8u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | GPR_U64(ctx, 5));
    // 0x43f4cc: 0x30c40008  andi        $a0, $a2, 0x8
    ctx->pc = 0x43f4ccu;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 6) & (uint64_t)(uint16_t)8);
    // 0x43f4d0: 0x1080000f  beqz        $a0, . + 4 + (0xF << 2)
    ctx->pc = 0x43F4D0u;
    {
        const bool branch_taken_0x43f4d0 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F4D4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F4D0u;
        // 0x43f4d4: 0xfe220070  sd          $v0, 0x70($s1) (Delay Slot)
        WRITE64(ADD32(GPR_U32(ctx, 17), 112), GPR_U64(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f4d0) {
            ctx->pc = 0x43F510u;
            goto label_43f510;
        }
    }
    ctx->pc = 0x43F4D8u;
    // 0x43f4d8: 0x38c50008  xori        $a1, $a2, 0x8
    ctx->pc = 0x43f4d8u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 6) ^ (uint64_t)(uint16_t)8);
    // 0x43f4dc: 0x3c10004f  lui         $s0, 0x4F
    ctx->pc = 0x43f4dcu;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)79 << 16));
    // 0x43f4e0: 0x26042780  addiu       $a0, $s0, 0x2780
    ctx->pc = 0x43f4e0u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 10112));
    // 0x43f4e4: 0xc06f73a  jal         func_1BDCE8
    ctx->pc = 0x43F4E4u;
    SET_GPR_U32(ctx, 31, 0x43F4ECu);
    ctx->pc = 0x43F4E8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F4E4u;
    // 0x43f4e8: 0x2ca50001  sltiu       $a1, $a1, 0x1 (Delay Slot)
    SET_GPR_U64(ctx, 5, ((uint64_t)GPR_U64(ctx, 5) < (uint64_t)(int64_t)(int32_t)1) ? 1 : 0);
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BDCE8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BDCE8u, 0x43F4E4u, 0x43F4ECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F4ECu;
label_43f4ec:
    // 0x43f4ec: 0xdc430000  ld          $v1, 0x0($v0)
    ctx->pc = 0x43f4ecu;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x43f4f0: 0x2404c000  addiu       $a0, $zero, -0x4000
    ctx->pc = 0x43f4f0u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 0), 4294950912));
    // 0x43f4f4: 0xde220070  ld          $v0, 0x70($s1)
    ctx->pc = 0x43f4f4u;
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 17), 112)));
    // 0x43f4f8: 0x306301ff  andi        $v1, $v1, 0x1FF
    ctx->pc = 0x43f4f8u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) & (uint64_t)(uint16_t)511);
    // 0x43f4fc: 0x441024  and         $v0, $v0, $a0
    ctx->pc = 0x43f4fcu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & GPR_U64(ctx, 4));
    // 0x43f500: 0x31978  dsll        $v1, $v1, 5
    ctx->pc = 0x43f500u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << 5);
    // 0x43f504: 0x431025  or          $v0, $v0, $v1
    ctx->pc = 0x43f504u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | GPR_U64(ctx, 3));
    // 0x43f508: 0x10000004  b           . + 4 + (0x4 << 2)
    ctx->pc = 0x43F508u;
    {
        const bool branch_taken_0x43f508 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F50Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F508u;
        // 0x43f50c: 0xfe220070  sd          $v0, 0x70($s1) (Delay Slot)
        WRITE64(ADD32(GPR_U32(ctx, 17), 112), GPR_U64(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f508) {
            ctx->pc = 0x43F51Cu;
            goto label_43f51c;
        }
    }
    ctx->pc = 0x43F510u;
label_43f510:
    // 0x43f510: 0xc0716d6  jal         func_1C5B58
    ctx->pc = 0x43F510u;
    SET_GPR_U32(ctx, 31, 0x43F518u);
    ctx->pc = 0x43F514u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F510u;
    // 0x43f514: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C5B58u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C5B58u, 0x43F510u, 0x43F518u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F518u;
label_43f518:
    // 0x43f518: 0x3c10004f  lui         $s0, 0x4F
    ctx->pc = 0x43f518u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)79 << 16));
label_43f51c:
    // 0x43f51c: 0x26102780  addiu       $s0, $s0, 0x2780
    ctx->pc = 0x43f51cu;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 10112));
    // 0x43f520: 0xde260070  ld          $a2, 0x70($s1)
    ctx->pc = 0x43f520u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 112)));
    // 0x43f524: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f524u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f528: 0x24050006  addiu       $a1, $zero, 0x6
    ctx->pc = 0x43f528u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 6));
    // 0x43f52c: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43F52Cu;
    SET_GPR_U32(ctx, 31, 0x43F534u);
    ctx->pc = 0x43F530u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F52Cu;
    // 0x43f530: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43F52Cu, 0x43F534u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F534u;
label_43f534:
    // 0x43f534: 0xde260078  ld          $a2, 0x78($s1)
    ctx->pc = 0x43f534u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 120)));
    // 0x43f538: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f538u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f53c: 0x3407fff0  ori         $a3, $zero, 0xFFF0
    ctx->pc = 0x43f53cu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65520);
    // 0x43f540: 0x73df8  dsll        $a3, $a3, 23
    ctx->pc = 0x43f540u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 23);
    // 0x43f544: 0x34e7c01f  ori         $a3, $a3, 0xC01F
    ctx->pc = 0x43f544u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)49183);
    // 0x43f548: 0x73978  dsll        $a3, $a3, 5
    ctx->pc = 0x43f548u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 5);
    // 0x43f54c: 0x34e7001d  ori         $a3, $a3, 0x1D
    ctx->pc = 0x43f54cu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)29);
    // 0x43f550: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43F550u;
    SET_GPR_U32(ctx, 31, 0x43F558u);
    ctx->pc = 0x43F554u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F550u;
    // 0x43f554: 0x24050014  addiu       $a1, $zero, 0x14 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 20));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43F550u, 0x43F558u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F558u;
label_43f558:
    // 0x43f558: 0x8e22006c  lw          $v0, 0x6C($s1)
    ctx->pc = 0x43f558u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 108)));
    // 0x43f55c: 0x1040000c  beqz        $v0, . + 4 + (0xC << 2)
    ctx->pc = 0x43F55Cu;
    {
        const bool branch_taken_0x43f55c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F560u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F55Cu;
        // 0x43f560: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f55c) {
            ctx->pc = 0x43F590u;
            goto label_43f590;
        }
    }
    ctx->pc = 0x43F564u;
    // 0x43f564: 0xde260080  ld          $a2, 0x80($s1)
    ctx->pc = 0x43f564u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 128)));
    // 0x43f568: 0x2407ffff  addiu       $a3, $zero, -0x1
    ctx->pc = 0x43f568u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43f56c: 0x7393a  dsrl        $a3, $a3, 4
    ctx->pc = 0x43f56cu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) >> 4);
    // 0x43f570: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43F570u;
    SET_GPR_U32(ctx, 31, 0x43F578u);
    ctx->pc = 0x43F574u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F570u;
    // 0x43f574: 0x24050034  addiu       $a1, $zero, 0x34 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 52));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43F570u, 0x43F578u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F578u;
label_43f578:
    // 0x43f578: 0xde260088  ld          $a2, 0x88($s1)
    ctx->pc = 0x43f578u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 136)));
    // 0x43f57c: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f57cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f580: 0x2407ffff  addiu       $a3, $zero, -0x1
    ctx->pc = 0x43f580u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43f584: 0x7393a  dsrl        $a3, $a3, 4
    ctx->pc = 0x43f584u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) >> 4);
    // 0x43f588: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43F588u;
    SET_GPR_U32(ctx, 31, 0x43F590u);
    ctx->pc = 0x43F58Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F588u;
    // 0x43f58c: 0x24050036  addiu       $a1, $zero, 0x36 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 54));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43F588u, 0x43F590u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F590u;
label_43f590:
    // 0x43f590: 0x3c030044  lui         $v1, 0x44
    ctx->pc = 0x43f590u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)68 << 16));
    // 0x43f594: 0x7bb00020  lq          $s0, 0x20($sp)
    ctx->pc = 0x43f594u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x43f598: 0x8c620e18  lw          $v0, 0xE18($v1)
    ctx->pc = 0x43f598u;
    SET_GPR_S32(ctx, 2, (int32_t)FAST_READ32(0x440E18u));
    // 0x43f59c: 0x7bb10010  lq          $s1, 0x10($sp)
    ctx->pc = 0x43f59cu;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x43f5a0: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x43f5a0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
    // 0x43f5a4: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x43f5a4u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43f5a8: 0xac620e18  sw          $v0, 0xE18($v1)
    ctx->pc = 0x43f5a8u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 2)); ps2TraceGuestWrite(rdram, 0x440E18u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x440E18u, _value); } while (0);
    // 0x43f5ac: 0x3e00008  jr          $ra
    ctx->pc = 0x43F5ACu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43F5B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F5ACu;
        // 0x43f5b0: 0x27bd0030  addiu       $sp, $sp, 0x30 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43F5ACu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43F5B4u;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&Select__5PsTexQ25PsTex5Blend_0x43f490), PS2Runtime::RecompiledFunction>::value,
              "Select__5PsTexQ25PsTex5Blend_0x43f490 override no longer matches the recompiled signature");
