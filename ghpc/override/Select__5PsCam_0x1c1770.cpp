// Override of work/output/Select__5PsCam_0x1c1770.cpp (PsCam::Select,
// 0x1c1770 - 0x1c1f18).
//
// Why: the only guest writer of VU1 qw696-703, the projection upload. The
// body keeps the generated translation verbatim and, when GHPC_MESH_LOG is
// set, prints the RndCam frustum and the eight quadwords it just staged,
// once per call, at the return path (the packet is only complete there).
//
// Where the packet lives: the body reads the DMA packet cursor from
// scratchpad word 0x70000008 (`lui 0x7000; lw $v0, 8($v0)`), stores each
// quadword through it and advances it by 0x10. So at the epilogue the
// cursor points one past the last quadword and the tail of the packet is:
//   cur-0xa0  VIF: FLUSHE, STMASK 0xC0C0C0C0, 0x7C0202B8  (V4-32 masked, NUM 2, ADDR 696)
//   cur-0x90  qw696 = (w*rect.w/2*gx, h*rect.h/2*gy, (zr.x-zr.y)*0xFFFF00, [w masked])
//   cur-0x80  qw697 = ((cx-.5)*w+2048, (cy-.5)*h+2048, (2-zr.x-zr.y)*0xFFFF00, [w masked])
//   cur-0x70  VIF: 0, 0, 0, 0x6C0602BA                     (V4-32, NUM 6, ADDR 698)
//   cur-0x60  qw698 = WorldXfm(this)->v, the camera position (lq +0x30 of the Transform)
//   cur-0x50  qw699 = (m[0].x/gx, -(m[2].y/gy), 0, 0)
//   cur-0x40  qw700..703 = Multiply2(this+0xc0, m, m): the 4 rows of the
//   ..cur-0x10             projection built from lq +0x100..+0x130 with the
//                          far/near terms patched into row1.z/row1.w/row3.z
// where w/h are the draw target's size (target_tex +0x4c/+0x50, else
// ThePsRnd +0x40/+0x44), cx/cy the screen_rect centre, gx/gy the guard band
// (sGuardBand 0x4f3038/0x4f303c when nonzero, else derived from rect and
// 2048). The 0xC0C0C0C0 STMASK protects the W column of qw696/697, so their
// fourth float is whatever the packet buffer held. The log checks both VIF
// headers before trusting the layout and prints hdrOk=0 otherwise.
//
// RndCam fields, from gh2-decomp src/rndobj/Cam.h (SyncProperty names them,
// high confidence): +0x2c0 near_plane, +0x2c4 far_plane, +0x2c8 y_fov,
// +0x2cc Vector2 z_range, +0x2d4 Hmx::Rect screen_rect (x,y,w,h),
// +0x2ec RndTex* target_tex. y_fov == 0 selects the orthographic z terms
// (0x1c1e04), anything else the perspective ones (0x1c1e24).
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

// The last camera staged, kept host-side for the seam. A native DrawFaces
// needs the projection this call just built, and this is the only place it
// exists in a readable form: qw700..703 are the rows, 696/697 the viewport
// scale and offset, 698 the camera position. Captured on every call, not
// just the logged ones, and only when both VIF headers check out, so a
// reader never sees a half-built packet. Consumed by the DrawFaces override.
float g_ghpcCamQw[8][4] = {};
uint32_t g_ghpcCamThis = 0u;      // the PsCam that staged them
uint64_t g_ghpcCamSelects = 0ull; // total selects with a good packet

namespace {

// Guest float through the same masked path the generated code uses; READ32
// routes the scratchpad cursor and anything it points into through the runtime.
inline float ghpcCamF32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    const uint32_t bits = READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void ghpcCamCapture(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    const uint32_t cur = runtime->Load32(rdram, ctx, 0x70000008u);
    if (READ32(cur - 0xa0u + 0xcu) != 0x7C0202B8u) return;
    if (READ32(cur - 0x70u + 0xcu) != 0x6C0602BAu) return;
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t q = (i < 2) ? (cur - 0x90u + i * 0x10u) : (cur - 0x60u + (i - 2) * 0x10u);
        for (uint32_t k = 0; k < 4; ++k)
            g_ghpcCamQw[i][k] = ghpcCamF32(rdram, ctx, runtime, q + k * 4u);
    }
    g_ghpcCamThis = self;
    ++g_ghpcCamSelects;
}

void ghpcCamLog(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self, uint64_t n) {
    const uint32_t cur = runtime->Load32(rdram, ctx, 0x70000008u);
    const uint32_t hdr1 = READ32(cur - 0xa0u + 0xcu);
    const uint32_t hdr2 = READ32(cur - 0x70u + 0xcu);
    const int hdrOk = (hdr1 == 0x7C0202B8u && hdr2 == 0x6C0602BAu) ? 1 : 0;

    std::fprintf(stderr,
        "[ghpc/cam] #%llu this=0x%08x near=%g far=%g yfov=%g zrange=(%g %g) rect=(%g %g %g %g) targetTex=0x%08x cursor=0x%08x hdrOk=%d hdr1=0x%08x hdr2=0x%08x\n",
        (unsigned long long)n, self,
        ghpcCamF32(rdram, ctx, runtime, self + 0x2c0u), ghpcCamF32(rdram, ctx, runtime, self + 0x2c4u),
        ghpcCamF32(rdram, ctx, runtime, self + 0x2c8u),
        ghpcCamF32(rdram, ctx, runtime, self + 0x2ccu), ghpcCamF32(rdram, ctx, runtime, self + 0x2d0u),
        ghpcCamF32(rdram, ctx, runtime, self + 0x2d4u), ghpcCamF32(rdram, ctx, runtime, self + 0x2d8u),
        ghpcCamF32(rdram, ctx, runtime, self + 0x2dcu), ghpcCamF32(rdram, ctx, runtime, self + 0x2e0u),
        READ32(self + 0x2ecu), cur, hdrOk, hdr1, hdr2);
    if (!hdrOk) return;

    // qw696, 697 sit above the second header; qw698..703 below it.
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t q = (i < 2) ? (cur - 0x90u + i * 0x10u) : (cur - 0x60u + (i - 2) * 0x10u);
        std::fprintf(stderr, "[ghpc/cam]   qw%u=(%g %g %g %g)\n", 696u + i,
            ghpcCamF32(rdram, ctx, runtime, q + 0x0u), ghpcCamF32(rdram, ctx, runtime, q + 0x4u),
            ghpcCamF32(rdram, ctx, runtime, q + 0x8u), ghpcCamF32(rdram, ctx, runtime, q + 0xcu));
    }
}

} // namespace

// Function: Select__5PsCam
// Address: 0x1c1770 - 0x1c1f18
void Select__5PsCam_0x1c1770(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Select__5PsCam_0x1c1770");
#endif

    switch (ctx->pc) {
        case 0x1c17c8u: goto label_1c17c8;
        case 0x1c17d0u: goto label_1c17d0;
        case 0x1c17e8u: goto label_1c17e8;
        case 0x1c1814u: goto label_1c1814;
        case 0x1c1824u: goto label_1c1824;
        case 0x1c1850u: goto label_1c1850;
        case 0x1c1860u: goto label_1c1860;
        case 0x1c1880u: goto label_1c1880;
        case 0x1c19ecu: goto label_1c19ec;
        case 0x1c1a08u: goto label_1c1a08;
        case 0x1c1a1cu: goto label_1c1a1c;
        case 0x1c1a48u: goto label_1c1a48;
        case 0x1c1a58u: goto label_1c1a58;
        case 0x1c1a80u: goto label_1c1a80;
        case 0x1c1a94u: goto label_1c1a94;
        case 0x1c1aacu: goto label_1c1aac;
        case 0x1c1b44u: goto label_1c1b44;
        case 0x1c1d98u: goto label_1c1d98;
        case 0x1c1eb0u: goto label_1c1eb0;
        case 0x1c1ec0u: goto label_1c1ec0;
        default: break;
    }

    ctx->pc = 0x1c1770u;

    // 0x1c1770: 0x27bdff00  addiu       $sp, $sp, -0x100
    ctx->pc = 0x1c1770u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967040));
    // 0x1c1774: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c1774u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c1778: 0x7fb000d0  sq          $s0, 0xD0($sp)
    ctx->pc = 0x1c1778u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 208), GPR_VEC(ctx, 16));
    // 0x1c177c: 0x7fb100c0  sq          $s1, 0xC0($sp)
    ctx->pc = 0x1c177cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 192), GPR_VEC(ctx, 17));
    // 0x1c1780: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x1c1780u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1784: 0x8c434c60  lw          $v1, 0x4C60($v0)
    ctx->pc = 0x1c1784u;
    SET_GPR_S32(ctx, 3, (int32_t)FAST_READ32(0x444C60u));
    // 0x1c1788: 0x882d  daddu       $s1, $zero, $zero
    ctx->pc = 0x1c1788u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c178c: 0x7fb200b0  sq          $s2, 0xB0($sp)
    ctx->pc = 0x1c178cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 176), GPR_VEC(ctx, 18));
    // 0x1c1790: 0x7fb300a0  sq          $s3, 0xA0($sp)
    ctx->pc = 0x1c1790u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 19));
    // 0x1c1794: 0x7fb40090  sq          $s4, 0x90($sp)
    ctx->pc = 0x1c1794u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 144), GPR_VEC(ctx, 20));
    // 0x1c1798: 0x7fb50080  sq          $s5, 0x80($sp)
    ctx->pc = 0x1c1798u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 128), GPR_VEC(ctx, 21));
    // 0x1c179c: 0xffbf0070  sd          $ra, 0x70($sp)
    ctx->pc = 0x1c179cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 112), GPR_U64(ctx, 31));
    // 0x1c17a0: 0xe7b600f0  swc1        $f22, 0xF0($sp)
    ctx->pc = 0x1c17a0u;
    { float f = ctx->f[22]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 240), bits); }
    // 0x1c17a4: 0xe7b500e8  swc1        $f21, 0xE8($sp)
    ctx->pc = 0x1c17a4u;
    { float f = ctx->f[21]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 232), bits); }
    // 0x1c17a8: 0x10600003  beqz        $v1, . + 4 + (0x3 << 2)
    ctx->pc = 0x1C17A8u;
    {
        const bool branch_taken_0x1c17a8 = (GPR_U64(ctx, 3) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C17ACu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C17A8u;
        // 0x1c17ac: 0xe7b400e0  swc1        $f20, 0xE0($sp) (Delay Slot)
        { float f = ctx->f[20]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 224), bits); }
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c17a8) {
            ctx->pc = 0x1C17B8u;
            goto label_1c17b8;
        }
    }
    ctx->pc = 0x1C17B0u;
    // 0x1c17b0: 0x8c6202ec  lw          $v0, 0x2EC($v1)
    ctx->pc = 0x1c17b0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 748)));
    // 0x1c17b4: 0x2882b  sltu        $s1, $zero, $v0
    ctx->pc = 0x1c17b4u;
    SET_GPR_U64(ctx, 17, ((uint64_t)GPR_U64(ctx, 0) < (uint64_t)GPR_U64(ctx, 2)) ? 1 : 0);
label_1c17b8:
    // 0x1c17b8: 0x12200003  beqz        $s1, . + 4 + (0x3 << 2)
    ctx->pc = 0x1C17B8u;
    {
        const bool branch_taken_0x1c17b8 = (GPR_U64(ctx, 17) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c17b8) {
            ctx->pc = 0x1C17C8u;
            goto label_1c17c8;
        }
    }
    ctx->pc = 0x1C17C0u;
    // 0x1c17c0: 0xc07187a  jal         func_1C61E8
    ctx->pc = 0x1C17C0u;
    SET_GPR_U32(ctx, 31, 0x1C17C8u);
    ctx->pc = 0x1C17C4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C17C0u;
    // 0x1c17c4: 0x8c6402ec  lw          $a0, 0x2EC($v1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 748)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C61E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C61E8u, 0x1C17C0u, 0x1C17C8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C17C8u;
label_1c17c8:
    // 0x1c17c8: 0xc075e8c  jal         func_1D7A30
    ctx->pc = 0x1C17C8u;
    SET_GPR_U32(ctx, 31, 0x1C17D0u);
    ctx->pc = 0x1C17CCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C17C8u;
    // 0x1c17cc: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D7A30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D7A30u, 0x1C17C8u, 0x1C17D0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C17D0u;
label_1c17d0:
    // 0x1c17d0: 0x260202e4  addiu       $v0, $s0, 0x2E4
    ctx->pc = 0x1c17d0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 16), 740));
    // 0x1c17d4: 0x8c440008  lw          $a0, 0x8($v0)
    ctx->pc = 0x1c17d4u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 8)));
    // 0x1c17d8: 0x10800025  beqz        $a0, . + 4 + (0x25 << 2)
    ctx->pc = 0x1C17D8u;
    {
        const bool branch_taken_0x1c17d8 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C17DCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C17D8u;
        // 0x1c17dc: 0x40902d  daddu       $s2, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c17d8) {
            ctx->pc = 0x1C1870u;
            goto label_1c1870;
        }
    }
    ctx->pc = 0x1C17E0u;
    // 0x1c17e0: 0xc071802  jal         func_1C6008
    ctx->pc = 0x1C17E0u;
    SET_GPR_U32(ctx, 31, 0x1C17E8u);
    ctx->pc = 0x1C6008u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C6008u, 0x1C17E0u, 0x1C17E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C17E8u;
label_1c17e8:
    // 0x1c17e8: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c17e8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
    // 0x1c17ec: 0x5440001d  bnel        $v0, $zero, . + 4 + (0x1D << 2)
    ctx->pc = 0x1C17ECu;
    {
        const bool branch_taken_0x1c17ec = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c17ec) {
            ctx->pc = 0x1C17F0u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C17ECu;
            // 0x1c17f0: 0x8c54004c  lw          $s4, 0x4C($v0) (Delay Slot)
            SET_GPR_S32(ctx, 20, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 76)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C1864u;
            goto label_1c1864;
        }
    }
    ctx->pc = 0x1C17F4u;
    // 0x1c17f4: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c17f4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c17f8: 0x3c050049  lui         $a1, 0x49
    ctx->pc = 0x1c17f8u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)73 << 16));
    // 0x1c17fc: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c17fcu;
    SET_GPR_S32(ctx, 4, (int32_t)FAST_READ32(0x444FC0u));
    // 0x1c1800: 0x3c070049  lui         $a3, 0x49
    ctx->pc = 0x1c1800u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)73 << 16));
    // 0x1c1804: 0x24a5c1c0  addiu       $a1, $a1, -0x3E40
    ctx->pc = 0x1c1804u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294951360));
    // 0x1c1808: 0x24e7c1e8  addiu       $a3, $a3, -0x3E18
    ctx->pc = 0x1c1808u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294951400));
    // 0x1c180c: 0xc0da03a  jal         func_3680E8
    ctx->pc = 0x1C180Cu;
    SET_GPR_U32(ctx, 31, 0x1C1814u);
    ctx->pc = 0x1C1810u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C180Cu;
    // 0x1c1810: 0x24060063  addiu       $a2, $zero, 0x63 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C180Cu, 0x1C1814u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1814u;
label_1c1814:
    // 0x1c1814: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x1c1814u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
    // 0x1c1818: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c1818u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c181c: 0xc0baf6a  jal         func_2EBDA8
    ctx->pc = 0x1C181Cu;
    SET_GPR_U32(ctx, 31, 0x1C1824u);
    ctx->pc = 0x1C1820u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C181Cu;
    // 0x1c1820: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C181Cu, 0x1C1824u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1824u;
label_1c1824:
    // 0x1c1824: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c1824u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
    // 0x1c1828: 0x1440000e  bnez        $v0, . + 4 + (0xE << 2)
    ctx->pc = 0x1C1828u;
    {
        const bool branch_taken_0x1c1828 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C182Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1828u;
        // 0x1c182c: 0x8c54004c  lw          $s4, 0x4C($v0) (Delay Slot)
        SET_GPR_S32(ctx, 20, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 76)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1828) {
            ctx->pc = 0x1C1864u;
            goto label_1c1864;
        }
    }
    ctx->pc = 0x1C1830u;
    // 0x1c1830: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c1830u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c1834: 0x3c050049  lui         $a1, 0x49
    ctx->pc = 0x1c1834u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)73 << 16));
    // 0x1c1838: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c1838u;
    SET_GPR_S32(ctx, 4, (int32_t)FAST_READ32(0x444FC0u));
    // 0x1c183c: 0x3c070049  lui         $a3, 0x49
    ctx->pc = 0x1c183cu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)73 << 16));
    // 0x1c1840: 0x24a5c1c0  addiu       $a1, $a1, -0x3E40
    ctx->pc = 0x1c1840u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294951360));
    // 0x1c1844: 0x24e7c1e8  addiu       $a3, $a3, -0x3E18
    ctx->pc = 0x1c1844u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294951400));
    // 0x1c1848: 0xc0da03a  jal         func_3680E8
    ctx->pc = 0x1C1848u;
    SET_GPR_U32(ctx, 31, 0x1C1850u);
    ctx->pc = 0x1C184Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1848u;
    // 0x1c184c: 0x24060063  addiu       $a2, $zero, 0x63 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C1848u, 0x1C1850u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1850u;
label_1c1850:
    // 0x1c1850: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x1c1850u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
    // 0x1c1854: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c1854u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1858: 0xc0baf6a  jal         func_2EBDA8
    ctx->pc = 0x1C1858u;
    SET_GPR_U32(ctx, 31, 0x1C1860u);
    ctx->pc = 0x1C185Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1858u;
    // 0x1c185c: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C1858u, 0x1C1860u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1860u;
label_1c1860:
    // 0x1c1860: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c1860u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
label_1c1864:
    // 0x1c1864: 0x3c15004f  lui         $s5, 0x4F
    ctx->pc = 0x1c1864u;
    SET_GPR_S32(ctx, 21, (int32_t)((uint32_t)79 << 16));
    // 0x1c1868: 0x10000008  b           . + 4 + (0x8 << 2)
    ctx->pc = 0x1C1868u;
    {
        const bool branch_taken_0x1c1868 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C186Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1868u;
        // 0x1c186c: 0x8c530050  lw          $s3, 0x50($v0) (Delay Slot)
        SET_GPR_S32(ctx, 19, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 80)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1868) {
            ctx->pc = 0x1C188Cu;
            goto label_1c188c;
        }
    }
    ctx->pc = 0x1C1870u;
label_1c1870:
    // 0x1c1870: 0x12200003  beqz        $s1, . + 4 + (0x3 << 2)
    ctx->pc = 0x1C1870u;
    {
        const bool branch_taken_0x1c1870 = (GPR_U64(ctx, 17) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C1874u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1870u;
        // 0x1c1874: 0x3c15004f  lui         $s5, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 21, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1870) {
            ctx->pc = 0x1C1880u;
            goto label_1c1880;
        }
    }
    ctx->pc = 0x1C1878u;
    // 0x1c1878: 0xc06fc12  jal         func_1BF048
    ctx->pc = 0x1C1878u;
    SET_GPR_U32(ctx, 31, 0x1C1880u);
    ctx->pc = 0x1C187Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1878u;
    // 0x1c187c: 0x26a42780  addiu       $a0, $s5, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 21), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BF048u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BF048u, 0x1C1878u, 0x1C1880u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1880u;
label_1c1880:
    // 0x1c1880: 0x26a22780  addiu       $v0, $s5, 0x2780
    ctx->pc = 0x1c1880u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 21), 10112));
    // 0x1c1884: 0x8c530044  lw          $s3, 0x44($v0)
    ctx->pc = 0x1c1884u;
    SET_GPR_S32(ctx, 19, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 68)));
    // 0x1c1888: 0x8c540040  lw          $s4, 0x40($v0)
    ctx->pc = 0x1c1888u;
    SET_GPR_S32(ctx, 20, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 64)));
label_1c188c:
    // 0x1c188c: 0xc60002d4  lwc1        $f0, 0x2D4($s0)
    ctx->pc = 0x1c188cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 724)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1890: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c1890u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c1894: 0x44810800  mtc1        $at, $f1
    ctx->pc = 0x1c1894u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
    // 0x1c1898: 0x44802000  mtc1        $zero, $f4
    ctx->pc = 0x1c1898u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c189c: 0x46000834  c.lt.s      $f1, $f0
    ctx->pc = 0x1c189cu;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[1], ctx->f[0])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c18a0: 0x0  nop
    ctx->pc = 0x1c18a0u;
    // NOP
    // 0x1c18a4: 0x45000003  bc1f        . + 4 + (0x3 << 2)
    ctx->pc = 0x1C18A4u;
    {
        const bool branch_taken_0x1c18a4 = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C18A8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C18A4u;
        // 0x1c18a8: 0x46000086  mov.s       $f2, $f0 (Delay Slot)
        ctx->f[2] = FPU_MOV_S(ctx->f[0]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c18a4) {
            ctx->pc = 0x1C18B4u;
            goto label_1c18b4;
        }
    }
    ctx->pc = 0x1C18ACu;
    // 0x1c18ac: 0x10000006  b           . + 4 + (0x6 << 2)
    ctx->pc = 0x1C18ACu;
    {
        const bool branch_taken_0x1c18ac = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C18B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C18ACu;
        // 0x1c18b0: 0x460008c6  mov.s       $f3, $f1 (Delay Slot)
        ctx->f[3] = FPU_MOV_S(ctx->f[1]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c18ac) {
            ctx->pc = 0x1C18C8u;
            goto label_1c18c8;
        }
    }
    ctx->pc = 0x1C18B4u;
label_1c18b4:
    // 0x1c18b4: 0x46041034  c.lt.s      $f2, $f4
    ctx->pc = 0x1c18b4u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[2], ctx->f[4])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c18b8: 0x0  nop
    ctx->pc = 0x1c18b8u;
    // NOP
    // 0x1c18bc: 0x45000002  bc1f        . + 4 + (0x2 << 2)
    ctx->pc = 0x1C18BCu;
    {
        const bool branch_taken_0x1c18bc = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C18C0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C18BCu;
        // 0x1c18c0: 0x460010c6  mov.s       $f3, $f2 (Delay Slot)
        ctx->f[3] = FPU_MOV_S(ctx->f[2]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c18bc) {
            ctx->pc = 0x1C18C8u;
            goto label_1c18c8;
        }
    }
    ctx->pc = 0x1C18C4u;
    // 0x1c18c4: 0x460020c6  mov.s       $f3, $f4
    ctx->pc = 0x1c18c4u;
    ctx->f[3] = FPU_MOV_S(ctx->f[4]);
label_1c18c8:
    // 0x1c18c8: 0x44940000  mtc1        $s4, $f0
    ctx->pc = 0x1c18c8u;
    { uint32_t bits = GPR_U32(ctx, 20); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x1c18cc: 0x0  nop
    ctx->pc = 0x1c18ccu;
    // NOP
    // 0x1c18d0: 0x46800020  cvt.s.w     $f0, $f0
    ctx->pc = 0x1c18d0u;
    { int32_t tmp; std::memcpy(&tmp, &ctx->f[0], sizeof(tmp)); ctx->f[0] = FPU_CVT_S_W(tmp); }
    // 0x1c18d4: 0xc60102dc  lwc1        $f1, 0x2DC($s0)
    ctx->pc = 0x1c18d4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 732)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c18d8: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c18d8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c18dc: 0x44812000  mtc1        $at, $f4
    ctx->pc = 0x1c18dcu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c18e0: 0x46011040  add.s       $f1, $f2, $f1
    ctx->pc = 0x1c18e0u;
    ctx->f[1] = FPU_ADD_S(ctx->f[2], ctx->f[1]);
    // 0x1c18e4: 0x46000546  mov.s       $f21, $f0
    ctx->pc = 0x1c18e4u;
    ctx->f[21] = FPU_MOV_S(ctx->f[0]);
    // 0x1c18e8: 0x44801000  mtc1        $zero, $f2
    ctx->pc = 0x1c18e8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }
    // 0x1c18ec: 0x46001802  mul.s       $f0, $f3, $f0
    ctx->pc = 0x1c18ecu;
    ctx->f[0] = FPU_MUL_S(ctx->f[3], ctx->f[0]);
    // 0x1c18f0: 0x46012034  c.lt.s      $f4, $f1
    ctx->pc = 0x1c18f0u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[4], ctx->f[1])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c18f4: 0x460000e4  .word       0x460000E4                   # cvt.w.s     $f3, $f0 # 00000000 <InstrIdType: CPU_COP1_FPUS>
    ctx->pc = 0x1c18f4u;
    { int32_t tmp = FPU_CVT_W_S(ctx->f[0]); std::memcpy(&ctx->f[3], &tmp, sizeof(tmp)); }
    // 0x1c18f8: 0x44041800  mfc1        $a0, $f3
    ctx->pc = 0x1c18f8u;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[3], sizeof(bits)); SET_GPR_U32(ctx, 4, bits); }
    // 0x1c18fc: 0x45010006  bc1t        . + 4 + (0x6 << 2)
    ctx->pc = 0x1C18FCu;
    {
        const bool branch_taken_0x1c18fc = ((ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1900u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C18FCu;
        // 0x1c1900: 0x46002006  mov.s       $f0, $f4 (Delay Slot)
        ctx->f[0] = FPU_MOV_S(ctx->f[4]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c18fc) {
            ctx->pc = 0x1C1918u;
            goto label_1c1918;
        }
    }
    ctx->pc = 0x1C1904u;
    // 0x1c1904: 0x46020834  c.lt.s      $f1, $f2
    ctx->pc = 0x1c1904u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[1], ctx->f[2])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c1908: 0x0  nop
    ctx->pc = 0x1c1908u;
    // NOP
    // 0x1c190c: 0x45000002  bc1f        . + 4 + (0x2 << 2)
    ctx->pc = 0x1C190Cu;
    {
        const bool branch_taken_0x1c190c = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1910u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C190Cu;
        // 0x1c1910: 0x46000806  mov.s       $f0, $f1 (Delay Slot)
        ctx->f[0] = FPU_MOV_S(ctx->f[1]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c190c) {
            ctx->pc = 0x1C1918u;
            goto label_1c1918;
        }
    }
    ctx->pc = 0x1C1914u;
    // 0x1c1914: 0x46001006  mov.s       $f0, $f2
    ctx->pc = 0x1c1914u;
    ctx->f[0] = FPU_MOV_S(ctx->f[2]);
label_1c1918:
    // 0x1c1918: 0x46150042  mul.s       $f1, $f0, $f21
    ctx->pc = 0x1c1918u;
    ctx->f[1] = FPU_MUL_S(ctx->f[0], ctx->f[21]);
    // 0x1c191c: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c191cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c1920: 0x44811800  mtc1        $at, $f3
    ctx->pc = 0x1c1920u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[3], &bits, sizeof(bits)); }
    // 0x1c1924: 0xc60002d8  lwc1        $f0, 0x2D8($s0)
    ctx->pc = 0x1c1924u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 728)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1928: 0x44802000  mtc1        $zero, $f4
    ctx->pc = 0x1c1928u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c192c: 0x46000964  .word       0x46000964                   # cvt.w.s     $f5, $f1 # 00000000 <InstrIdType: CPU_COP1_FPUS>
    ctx->pc = 0x1c192cu;
    { int32_t tmp = FPU_CVT_W_S(ctx->f[1]); std::memcpy(&ctx->f[5], &tmp, sizeof(tmp)); }
    // 0x1c1930: 0x44022800  mfc1        $v0, $f5
    ctx->pc = 0x1c1930u;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[5], sizeof(bits)); SET_GPR_U32(ctx, 2, bits); }
    // 0x1c1934: 0x46000086  mov.s       $f2, $f0
    ctx->pc = 0x1c1934u;
    ctx->f[2] = FPU_MOV_S(ctx->f[0]);
    // 0x1c1938: 0x46001834  c.lt.s      $f3, $f0
    ctx->pc = 0x1c1938u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[3], ctx->f[0])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c193c: 0x0  nop
    ctx->pc = 0x1c193cu;
    // NOP
    // 0x1c1940: 0x45010006  bc1t        . + 4 + (0x6 << 2)
    ctx->pc = 0x1C1940u;
    {
        const bool branch_taken_0x1c1940 = ((ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1944u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1940u;
        // 0x1c1944: 0x2442ffff  addiu       $v0, $v0, -0x1 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1940) {
            ctx->pc = 0x1C195Cu;
            goto label_1c195c;
        }
    }
    ctx->pc = 0x1C1948u;
    // 0x1c1948: 0x46041034  c.lt.s      $f2, $f4
    ctx->pc = 0x1c1948u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[2], ctx->f[4])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c194c: 0x0  nop
    ctx->pc = 0x1c194cu;
    // NOP
    // 0x1c1950: 0x45000002  bc1f        . + 4 + (0x2 << 2)
    ctx->pc = 0x1C1950u;
    {
        const bool branch_taken_0x1c1950 = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1954u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1950u;
        // 0x1c1954: 0x460010c6  mov.s       $f3, $f2 (Delay Slot)
        ctx->f[3] = FPU_MOV_S(ctx->f[2]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1950) {
            ctx->pc = 0x1C195Cu;
            goto label_1c195c;
        }
    }
    ctx->pc = 0x1C1958u;
    // 0x1c1958: 0x460020c6  mov.s       $f3, $f4
    ctx->pc = 0x1c1958u;
    ctx->f[3] = FPU_MOV_S(ctx->f[4]);
label_1c195c:
    // 0x1c195c: 0x44930000  mtc1        $s3, $f0
    ctx->pc = 0x1c195cu;
    { uint32_t bits = GPR_U32(ctx, 19); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x1c1960: 0x0  nop
    ctx->pc = 0x1c1960u;
    // NOP
    // 0x1c1964: 0x46800020  cvt.s.w     $f0, $f0
    ctx->pc = 0x1c1964u;
    { int32_t tmp; std::memcpy(&tmp, &ctx->f[0], sizeof(tmp)); ctx->f[0] = FPU_CVT_S_W(tmp); }
    // 0x1c1968: 0xc60102e0  lwc1        $f1, 0x2E0($s0)
    ctx->pc = 0x1c1968u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 736)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c196c: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c196cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c1970: 0x44812000  mtc1        $at, $f4
    ctx->pc = 0x1c1970u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c1974: 0x46011040  add.s       $f1, $f2, $f1
    ctx->pc = 0x1c1974u;
    ctx->f[1] = FPU_ADD_S(ctx->f[2], ctx->f[1]);
    // 0x1c1978: 0x46000506  mov.s       $f20, $f0
    ctx->pc = 0x1c1978u;
    ctx->f[20] = FPU_MOV_S(ctx->f[0]);
    // 0x1c197c: 0x44801000  mtc1        $zero, $f2
    ctx->pc = 0x1c197cu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }
    // 0x1c1980: 0x46001802  mul.s       $f0, $f3, $f0
    ctx->pc = 0x1c1980u;
    ctx->f[0] = FPU_MUL_S(ctx->f[3], ctx->f[0]);
    // 0x1c1984: 0x46012034  c.lt.s      $f4, $f1
    ctx->pc = 0x1c1984u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[4], ctx->f[1])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c1988: 0x460000e4  .word       0x460000E4                   # cvt.w.s     $f3, $f0 # 00000000 <InstrIdType: CPU_COP1_FPUS>
    ctx->pc = 0x1c1988u;
    { int32_t tmp = FPU_CVT_W_S(ctx->f[0]); std::memcpy(&ctx->f[3], &tmp, sizeof(tmp)); }
    // 0x1c198c: 0x44031800  mfc1        $v1, $f3
    ctx->pc = 0x1c198cu;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[3], sizeof(bits)); SET_GPR_U32(ctx, 3, bits); }
    // 0x1c1990: 0x45010006  bc1t        . + 4 + (0x6 << 2)
    ctx->pc = 0x1C1990u;
    {
        const bool branch_taken_0x1c1990 = ((ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1994u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1990u;
        // 0x1c1994: 0x46002006  mov.s       $f0, $f4 (Delay Slot)
        ctx->f[0] = FPU_MOV_S(ctx->f[4]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1990) {
            ctx->pc = 0x1C19ACu;
            goto label_1c19ac;
        }
    }
    ctx->pc = 0x1C1998u;
    // 0x1c1998: 0x46020834  c.lt.s      $f1, $f2
    ctx->pc = 0x1c1998u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[1], ctx->f[2])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c199c: 0x0  nop
    ctx->pc = 0x1c199cu;
    // NOP
    // 0x1c19a0: 0x45000002  bc1f        . + 4 + (0x2 << 2)
    ctx->pc = 0x1C19A0u;
    {
        const bool branch_taken_0x1c19a0 = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C19A4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C19A0u;
        // 0x1c19a4: 0x46000806  mov.s       $f0, $f1 (Delay Slot)
        ctx->f[0] = FPU_MOV_S(ctx->f[1]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c19a0) {
            ctx->pc = 0x1C19ACu;
            goto label_1c19ac;
        }
    }
    ctx->pc = 0x1C19A8u;
    // 0x1c19a8: 0x46001006  mov.s       $f0, $f2
    ctx->pc = 0x1c19a8u;
    ctx->f[0] = FPU_MOV_S(ctx->f[2]);
label_1c19ac:
    // 0x1c19ac: 0x40302d  daddu       $a2, $v0, $zero
    ctx->pc = 0x1c19acu;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c19b0: 0x46140002  mul.s       $f0, $f0, $f20
    ctx->pc = 0x1c19b0u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[20]);
    // 0x1c19b4: 0x63438  dsll        $a2, $a2, 16
    ctx->pc = 0x1c19b4u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) << 16);
    // 0x1c19b8: 0x3183c  dsll32      $v1, $v1, 0
    ctx->pc = 0x1c19b8u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << (32 + 0));
    // 0x1c19bc: 0x863025  or          $a2, $a0, $a2
    ctx->pc = 0x1c19bcu;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 4) | GPR_U64(ctx, 6));
    // 0x1c19c0: 0x26b12780  addiu       $s1, $s5, 0x2780
    ctx->pc = 0x1c19c0u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 21), 10112));
    // 0x1c19c4: 0x46000064  .word       0x46000064                   # cvt.w.s     $f1, $f0 # 00000000 <InstrIdType: CPU_COP1_FPUS>
    ctx->pc = 0x1c19c4u;
    { int32_t tmp = FPU_CVT_W_S(ctx->f[0]); std::memcpy(&ctx->f[1], &tmp, sizeof(tmp)); }
    // 0x1c19c8: 0x44020800  mfc1        $v0, $f1
    ctx->pc = 0x1c19c8u;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[1], sizeof(bits)); SET_GPR_U32(ctx, 2, bits); }
    // 0x1c19cc: 0xc33025  or          $a2, $a2, $v1
    ctx->pc = 0x1c19ccu;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | GPR_U64(ctx, 3));
    // 0x1c19d0: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c19d0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c19d4: 0x24050040  addiu       $a1, $zero, 0x40
    ctx->pc = 0x1c19d4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 64));
    // 0x1c19d8: 0x2407ffff  addiu       $a3, $zero, -0x1
    ctx->pc = 0x1c19d8u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x1c19dc: 0x2442ffff  addiu       $v0, $v0, -0x1
    ctx->pc = 0x1c19dcu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
    // 0x1c19e0: 0x2143c  dsll32      $v0, $v0, 16
    ctx->pc = 0x1c19e0u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << (32 + 16));
    // 0x1c19e4: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C19E4u;
    SET_GPR_U32(ctx, 31, 0x1C19ECu);
    ctx->pc = 0x1C19E8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C19E4u;
    // 0x1c19e8: 0xc23025  or          $a2, $a2, $v0 (Delay Slot)
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | GPR_U64(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C19E4u, 0x1C19ECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C19ECu;
label_1c19ec:
    // 0x1c19ec: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c19ecu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
    // 0x1c19f0: 0x10400050  beqz        $v0, . + 4 + (0x50 << 2)
    ctx->pc = 0x1C19F0u;
    {
        const bool branch_taken_0x1c19f0 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C19F4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C19F0u;
        // 0x1c19f4: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c19f0) {
            ctx->pc = 0x1C1B34u;
            goto label_1c1b34;
        }
    }
    ctx->pc = 0x1C19F8u;
    // 0x1c19f8: 0x24050047  addiu       $a1, $zero, 0x47
    ctx->pc = 0x1c19f8u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 71));
    // 0x1c19fc: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x1c19fcu;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1a00: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C1A00u;
    SET_GPR_U32(ctx, 31, 0x1C1A08u);
    ctx->pc = 0x1C1A04u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A00u;
    // 0x1c1a04: 0x24070001  addiu       $a3, $zero, 0x1 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C1A00u, 0x1C1A08u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A08u;
label_1c1a08:
    // 0x1c1a08: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c1a08u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1a0c: 0x2405004a  addiu       $a1, $zero, 0x4A
    ctx->pc = 0x1c1a0cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 74));
    // 0x1c1a10: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x1c1a10u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1a14: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C1A14u;
    SET_GPR_U32(ctx, 31, 0x1C1A1Cu);
    ctx->pc = 0x1C1A18u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A14u;
    // 0x1c1a18: 0x24070001  addiu       $a3, $zero, 0x1 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C1A14u, 0x1C1A1Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A1Cu;
label_1c1a1c:
    // 0x1c1a1c: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c1a1cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
    // 0x1c1a20: 0x5440000f  bnel        $v0, $zero, . + 4 + (0xF << 2)
    ctx->pc = 0x1C1A20u;
    {
        const bool branch_taken_0x1c1a20 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c1a20) {
            ctx->pc = 0x1C1A24u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C1A20u;
            // 0x1c1a24: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
            SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C1A60u;
            goto label_1c1a60;
        }
    }
    ctx->pc = 0x1C1A28u;
    // 0x1c1a28: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c1a28u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c1a2c: 0x3c050049  lui         $a1, 0x49
    ctx->pc = 0x1c1a2cu;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)73 << 16));
    // 0x1c1a30: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c1a30u;
    SET_GPR_S32(ctx, 4, (int32_t)FAST_READ32(0x444FC0u));
    // 0x1c1a34: 0x3c070049  lui         $a3, 0x49
    ctx->pc = 0x1c1a34u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)73 << 16));
    // 0x1c1a38: 0x24a5c1c0  addiu       $a1, $a1, -0x3E40
    ctx->pc = 0x1c1a38u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294951360));
    // 0x1c1a3c: 0x24e7c1e8  addiu       $a3, $a3, -0x3E18
    ctx->pc = 0x1c1a3cu;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294951400));
    // 0x1c1a40: 0xc0da03a  jal         func_3680E8
    ctx->pc = 0x1C1A40u;
    SET_GPR_U32(ctx, 31, 0x1C1A48u);
    ctx->pc = 0x1C1A44u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A40u;
    // 0x1c1a44: 0x24060063  addiu       $a2, $zero, 0x63 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C1A40u, 0x1C1A48u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A48u;
label_1c1a48:
    // 0x1c1a48: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x1c1a48u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
    // 0x1c1a4c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c1a4cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1a50: 0xc0baf6a  jal         func_2EBDA8
    ctx->pc = 0x1C1A50u;
    SET_GPR_U32(ctx, 31, 0x1C1A58u);
    ctx->pc = 0x1C1A54u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A50u;
    // 0x1c1a54: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C1A50u, 0x1C1A58u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A58u;
label_1c1a58:
    // 0x1c1a58: 0x8e420008  lw          $v0, 0x8($s2)
    ctx->pc = 0x1c1a58u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 8)));
    // 0x1c1a5c: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c1a5cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
label_1c1a60:
    // 0x1c1a60: 0x2405004e  addiu       $a1, $zero, 0x4E
    ctx->pc = 0x1c1a60u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 78));
    // 0x1c1a64: 0x34078000  ori         $a3, $zero, 0x8000
    ctx->pc = 0x1c1a64u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)32768);
    // 0x1c1a68: 0x73c78  dsll        $a3, $a3, 17
    ctx->pc = 0x1c1a68u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 17);
    // 0x1c1a6c: 0x8c460048  lw          $a2, 0x48($v0)
    ctx->pc = 0x1c1a6cu;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 72)));
    // 0x1c1a70: 0x38c60002  xori        $a2, $a2, 0x2
    ctx->pc = 0x1c1a70u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) ^ (uint64_t)(uint16_t)2);
    // 0x1c1a74: 0x6302b  sltu        $a2, $zero, $a2
    ctx->pc = 0x1c1a74u;
    SET_GPR_U64(ctx, 6, ((uint64_t)GPR_U64(ctx, 0) < (uint64_t)GPR_U64(ctx, 6)) ? 1 : 0);
    // 0x1c1a78: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C1A78u;
    SET_GPR_U32(ctx, 31, 0x1C1A80u);
    ctx->pc = 0x1C1A7Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A78u;
    // 0x1c1a7c: 0x6303c  dsll32      $a2, $a2, 0 (Delay Slot)
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) << (32 + 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C1A78u, 0x1C1A80u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A80u;
label_1c1a80:
    // 0x1c1a80: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c1a80u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1a84: 0x24050047  addiu       $a1, $zero, 0x47
    ctx->pc = 0x1c1a84u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 71));
    // 0x1c1a88: 0x3c060002  lui         $a2, 0x2
    ctx->pc = 0x1c1a88u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)2 << 16));
    // 0x1c1a8c: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C1A8Cu;
    SET_GPR_U32(ctx, 31, 0x1C1A94u);
    ctx->pc = 0x1C1A90u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1A8Cu;
    // 0x1c1a90: 0x3c070006  lui         $a3, 0x6 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)6 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C1A8Cu, 0x1C1A94u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1A94u;
label_1c1a94:
    // 0x1c1a94: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c1a94u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1a98: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c1a98u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c1a9c: 0x24060004  addiu       $a2, $zero, 0x4
    ctx->pc = 0x1c1a9cu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
    // 0x1c1aa0: 0x24075510  addiu       $a3, $zero, 0x5510
    ctx->pc = 0x1c1aa0u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 21776));
    // 0x1c1aa4: 0xc10fa62  jal         func_43E988
    ctx->pc = 0x1C1AA4u;
    SET_GPR_U32(ctx, 31, 0x1C1AACu);
    ctx->pc = 0x1C1AA8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1AA4u;
    // 0x1c1aa8: 0x402d  daddu       $t0, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 8, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E988u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E988u, 0x1C1AA4u, 0x1C1AACu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1AACu;
label_1c1aac:
    // 0x1c1aac: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1aacu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1ab0: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1ab0u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1ab4: 0x24050006  addiu       $a1, $zero, 0x6
    ctx->pc = 0x1c1ab4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 6));
    // 0x1c1ab8: 0x24060800  addiu       $a2, $zero, 0x800
    ctx->pc = 0x1c1ab8u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 2048));
    // 0x1c1abc: 0x131843  sra         $v1, $s3, 1
    ctx->pc = 0x1c1abcu;
    SET_GPR_S32(ctx, 3, SRA32(GPR_S32(ctx, 19), 1));
    // 0x1c1ac0: 0x40382d  daddu       $a3, $v0, $zero
    ctx->pc = 0x1c1ac0u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1ac4: 0xc31823  subu        $v1, $a2, $v1
    ctx->pc = 0x1c1ac4u;
    SET_GPR_S32(ctx, 3, (int32_t)SUB32(GPR_U32(ctx, 6), GPR_U32(ctx, 3)));
    // 0x1c1ac8: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1ac8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1acc: 0x142043  sra         $a0, $s4, 1
    ctx->pc = 0x1c1accu;
    SET_GPR_S32(ctx, 4, SRA32(GPR_S32(ctx, 20), 1));
    // 0x1c1ad0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1ad0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1ad4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1ad4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1ad8: 0xc43023  subu        $a2, $a2, $a0
    ctx->pc = 0x1c1ad8u;
    SET_GPR_S32(ctx, 6, (int32_t)SUB32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
    // 0x1c1adc: 0xfce50000  sd          $a1, 0x0($a3)
    ctx->pc = 0x1c1adcu;
    WRITE64(ADD32(GPR_U32(ctx, 7), 0), GPR_U64(ctx, 5));
    // 0x1c1ae0: 0x31900  sll         $v1, $v1, 4
    ctx->pc = 0x1c1ae0u;
    SET_GPR_S32(ctx, 3, (int32_t)SLL32(GPR_U32(ctx, 3), 4));
    // 0x1c1ae4: 0xfce00008  sd          $zero, 0x8($a3)
    ctx->pc = 0x1c1ae4u;
    WRITE64(ADD32(GPR_U32(ctx, 7), 8), GPR_U64(ctx, 0));
    // 0x1c1ae8: 0x132900  sll         $a1, $s3, 4
    ctx->pc = 0x1c1ae8u;
    SET_GPR_S32(ctx, 5, (int32_t)SLL32(GPR_U32(ctx, 19), 4));
    // 0x1c1aec: 0x652821  addu        $a1, $v1, $a1
    ctx->pc = 0x1c1aecu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 5)));
    // 0x1c1af0: 0x63100  sll         $a2, $a2, 4
    ctx->pc = 0x1c1af0u;
    SET_GPR_S32(ctx, 6, (int32_t)SLL32(GPR_U32(ctx, 6), 4));
    // 0x1c1af4: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c1af4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1af8: 0x8c840008  lw          $a0, 0x8($a0)
    ctx->pc = 0x1c1af8u;
    SET_GPR_S32(ctx, 4, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 4), 8))); // MMIO: 0x70000008
    // 0x1c1afc: 0x141100  sll         $v0, $s4, 4
    ctx->pc = 0x1c1afcu;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 20), 4));
    // 0x1c1b00: 0xc21021  addu        $v0, $a2, $v0
    ctx->pc = 0x1c1b00u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 2)));
    // 0x1c1b04: 0x80382d  daddu       $a3, $a0, $zero
    ctx->pc = 0x1c1b04u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1b08: 0x52c38  dsll        $a1, $a1, 16
    ctx->pc = 0x1c1b08u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) << 16);
    // 0x1c1b0c: 0x24840010  addiu       $a0, $a0, 0x10
    ctx->pc = 0x1c1b0cu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 16));
    // 0x1c1b10: 0x451025  or          $v0, $v0, $a1
    ctx->pc = 0x1c1b10u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | GPR_U64(ctx, 5));
    // 0x1c1b14: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1b14u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1b18: 0xac240008  sw          $a0, 0x8($at)
    ctx->pc = 0x1c1b18u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 4)); // MMIO: 0x70000008
    // 0x1c1b1c: 0x31c38  dsll        $v1, $v1, 16
    ctx->pc = 0x1c1b1cu;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << 16);
    // 0x1c1b20: 0xfce20008  sd          $v0, 0x8($a3)
    ctx->pc = 0x1c1b20u;
    WRITE64(ADD32(GPR_U32(ctx, 7), 8), GPR_U64(ctx, 2));
    // 0x1c1b24: 0xc31825  or          $v1, $a2, $v1
    ctx->pc = 0x1c1b24u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 6) | GPR_U64(ctx, 3));
    // 0x1c1b28: 0xfce30000  sd          $v1, 0x0($a3)
    ctx->pc = 0x1c1b28u;
    WRITE64(ADD32(GPR_U32(ctx, 7), 0), GPR_U64(ctx, 3));
    // 0x1c1b2c: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c1b2cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c1b30: 0xac400e0c  sw          $zero, 0xE0C($v0)
    ctx->pc = 0x1c1b30u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x440E0Cu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x440E0Cu, _value); } while (0);
label_1c1b34:
    // 0x1c1b34: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c1b34u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1b38: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c1b38u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c1b3c: 0xc10fa18  jal         func_43E860
    ctx->pc = 0x1C1B3Cu;
    SET_GPR_U32(ctx, 31, 0x1C1B44u);
    ctx->pc = 0x1C1B40u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1B3Cu;
    // 0x1c1b40: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E860u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E860u, 0x1C1B3Cu, 0x1C1B44u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1B44u;
label_1c1b44:
    // 0x1c1b44: 0x260202d4  addiu       $v0, $s0, 0x2D4
    ctx->pc = 0x1c1b44u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 16), 724));
    // 0x1c1b48: 0x3c013f00  lui         $at, 0x3F00
    ctx->pc = 0x1c1b48u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16128 << 16));
    // 0x1c1b4c: 0x44810800  mtc1        $at, $f1
    ctx->pc = 0x1c1b4cu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
    // 0x1c1b50: 0xc4420008  lwc1        $f2, 0x8($v0)
    ctx->pc = 0x1c1b50u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 8)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1b54: 0x3c03004f  lui         $v1, 0x4F
    ctx->pc = 0x1c1b54u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)79 << 16));
    // 0x1c1b58: 0xc443000c  lwc1        $f3, 0xC($v0)
    ctx->pc = 0x1c1b58u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 12)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c1b5c: 0x60202d  daddu       $a0, $v1, $zero
    ctx->pc = 0x1c1b5cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1b60: 0x46011082  mul.s       $f2, $f2, $f1
    ctx->pc = 0x1c1b60u;
    ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[1]);
    // 0x1c1b64: 0xc60002d4  lwc1        $f0, 0x2D4($s0)
    ctx->pc = 0x1c1b64u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 724)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1b68: 0x460118c2  mul.s       $f3, $f3, $f1
    ctx->pc = 0x1c1b68u;
    ctx->f[3] = FPU_MUL_S(ctx->f[3], ctx->f[1]);
    // 0x1c1b6c: 0xc4440004  lwc1        $f4, 0x4($v0)
    ctx->pc = 0x1c1b6cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[4] = f; }
    // 0x1c1b70: 0xe7a10024  swc1        $f1, 0x24($sp)
    ctx->pc = 0x1c1b70u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 36), bits); }
    // 0x1c1b74: 0x46020000  add.s       $f0, $f0, $f2
    ctx->pc = 0x1c1b74u;
    ctx->f[0] = FPU_ADD_S(ctx->f[0], ctx->f[2]);
    // 0x1c1b78: 0xe7a10020  swc1        $f1, 0x20($sp)
    ctx->pc = 0x1c1b78u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 32), bits); }
    // 0x1c1b7c: 0x46032100  add.s       $f4, $f4, $f3
    ctx->pc = 0x1c1b7cu;
    ctx->f[4] = FPU_ADD_S(ctx->f[4], ctx->f[3]);
    // 0x1c1b80: 0x46000946  mov.s       $f5, $f1
    ctx->pc = 0x1c1b80u;
    ctx->f[5] = FPU_MOV_S(ctx->f[1]);
    // 0x1c1b84: 0x46000886  mov.s       $f2, $f1
    ctx->pc = 0x1c1b84u;
    ctx->f[2] = FPU_MOV_S(ctx->f[1]);
    // 0x1c1b88: 0xc4633038  lwc1        $f3, 0x3038($v1)
    ctx->pc = 0x1c1b88u;
    { uint32_t bits = FAST_READ32(0x4F3038u); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c1b8c: 0xe7a00010  swc1        $f0, 0x10($sp)
    ctx->pc = 0x1c1b8cu;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 16), bits); }
    // 0x1c1b90: 0xe7a40014  swc1        $f4, 0x14($sp)
    ctx->pc = 0x1c1b90u;
    { float f = ctx->f[4]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 20), bits); }
    // 0x1c1b94: 0x46000046  mov.s       $f1, $f0
    ctx->pc = 0x1c1b94u;
    ctx->f[1] = FPU_MOV_S(ctx->f[0]);
    // 0x1c1b98: 0x46020881  sub.s       $f2, $f1, $f2
    ctx->pc = 0x1c1b98u;
    ctx->f[2] = FPU_SUB_S(ctx->f[1], ctx->f[2]);
    // 0x1c1b9c: 0x46002006  mov.s       $f0, $f4
    ctx->pc = 0x1c1b9cu;
    ctx->f[0] = FPU_MOV_S(ctx->f[4]);
    // 0x1c1ba0: 0x44800800  mtc1        $zero, $f1
    ctx->pc = 0x1c1ba0u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
    // 0x1c1ba4: 0x46050001  sub.s       $f0, $f0, $f5
    ctx->pc = 0x1c1ba4u;
    ctx->f[0] = FPU_SUB_S(ctx->f[0], ctx->f[5]);
    // 0x1c1ba8: 0x46011832  c.eq.s      $f3, $f1
    ctx->pc = 0x1c1ba8u;
    ctx->fcr31 = (FPU_C_EQ_S(ctx->f[3], ctx->f[1])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c1bac: 0xe7a20000  swc1        $f2, 0x0($sp)
    ctx->pc = 0x1c1bacu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 0), bits); }
    // 0x1c1bb0: 0x4500000f  bc1f        . + 4 + (0xF << 2)
    ctx->pc = 0x1C1BB0u;
    {
        const bool branch_taken_0x1c1bb0 = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1BB4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1BB0u;
        // 0x1c1bb4: 0xe7a00004  swc1        $f0, 0x4($sp) (Delay Slot)
        { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 4), bits); }
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1bb0) {
            ctx->pc = 0x1C1BF0u;
            goto label_1c1bf0;
        }
    }
    ctx->pc = 0x1C1BB8u;
    // 0x1c1bb8: 0x46151082  mul.s       $f2, $f2, $f21
    ctx->pc = 0x1c1bb8u;
    ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[21]);
    // 0x1c1bbc: 0xc60002dc  lwc1        $f0, 0x2DC($s0)
    ctx->pc = 0x1c1bbcu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 732)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1bc0: 0x3c014500  lui         $at, 0x4500
    ctx->pc = 0x1c1bc0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17664 << 16));
    // 0x1c1bc4: 0x44810800  mtc1        $at, $f1
    ctx->pc = 0x1c1bc4u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
    // 0x1c1bc8: 0x46150002  mul.s       $f0, $f0, $f21
    ctx->pc = 0x1c1bc8u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[21]);
    // 0x1c1bcc: 0x3c013f02  lui         $at, 0x3F02
    ctx->pc = 0x1c1bccu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16130 << 16));
    // 0x1c1bd0: 0x34218f5c  ori         $at, $at, 0x8F5C
    ctx->pc = 0x1c1bd0u;
    SET_GPR_U64(ctx, 1, GPR_U64(ctx, 1) | (uint64_t)(uint16_t)36700);
    // 0x1c1bd4: 0x44811800  mtc1        $at, $f3
    ctx->pc = 0x1c1bd4u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[3], &bits, sizeof(bits)); }
    // 0x1c1bd8: 0x46001085  abs.s       $f2, $f2
    ctx->pc = 0x1c1bd8u;
    ctx->f[2] = FPU_ABS_S(ctx->f[2]);
    // 0x1c1bdc: 0x46030002  mul.s       $f0, $f0, $f3
    ctx->pc = 0x1c1bdcu;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[3]);
    // 0x1c1be0: 0x46020841  sub.s       $f1, $f1, $f2
    ctx->pc = 0x1c1be0u;
    ctx->f[1] = FPU_SUB_S(ctx->f[1], ctx->f[2]);
    // 0x1c1be4: 0x46000843  div.s       $f1, $f1, $f0
    ctx->pc = 0x1c1be4u;
    if (ctx->f[0] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[1] = copysignf(INFINITY, ctx->f[1] * 0.0f); } else ctx->f[1] = ctx->f[1] / ctx->f[0];
    // 0x1c1be8: 0x10000002  b           . + 4 + (0x2 << 2)
    ctx->pc = 0x1C1BE8u;
    {
        const bool branch_taken_0x1c1be8 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C1BECu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1BE8u;
        // 0x1c1bec: 0xe7a10010  swc1        $f1, 0x10($sp) (Delay Slot)
        { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 16), bits); }
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1be8) {
            ctx->pc = 0x1C1BF4u;
            goto label_1c1bf4;
        }
    }
    ctx->pc = 0x1C1BF0u;
label_1c1bf0:
    // 0x1c1bf0: 0xe7a30010  swc1        $f3, 0x10($sp)
    ctx->pc = 0x1c1bf0u;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 16), bits); }
label_1c1bf4:
    // 0x1c1bf4: 0x24823038  addiu       $v0, $a0, 0x3038
    ctx->pc = 0x1c1bf4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 4), 12344));
    // 0x1c1bf8: 0x44800000  mtc1        $zero, $f0
    ctx->pc = 0x1c1bf8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x1c1bfc: 0xc4410004  lwc1        $f1, 0x4($v0)
    ctx->pc = 0x1c1bfcu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1c00: 0x46000832  c.eq.s      $f1, $f0
    ctx->pc = 0x1c1c00u;
    ctx->fcr31 = (FPU_C_EQ_S(ctx->f[1], ctx->f[0])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c1c04: 0x0  nop
    ctx->pc = 0x1c1c04u;
    // NOP
    // 0x1c1c08: 0x4502000f  bc1fl       . + 4 + (0xF << 2)
    ctx->pc = 0x1C1C08u;
    {
        const bool branch_taken_0x1c1c08 = (!(ctx->fcr31 & 0x800000));
        if (branch_taken_0x1c1c08) {
            ctx->pc = 0x1C1C0Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C1C08u;
            // 0x1c1c0c: 0xe7a10014  swc1        $f1, 0x14($sp) (Delay Slot)
            { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 20), bits); }
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C1C48u;
            goto label_1c1c48;
        }
    }
    ctx->pc = 0x1C1C10u;
    // 0x1c1c10: 0xc7a10004  lwc1        $f1, 0x4($sp)
    ctx->pc = 0x1c1c10u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1c14: 0xc60202e0  lwc1        $f2, 0x2E0($s0)
    ctx->pc = 0x1c1c14u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 736)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1c18: 0x46140842  mul.s       $f1, $f1, $f20
    ctx->pc = 0x1c1c18u;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[20]);
    // 0x1c1c1c: 0x3c014500  lui         $at, 0x4500
    ctx->pc = 0x1c1c1cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17664 << 16));
    // 0x1c1c20: 0x44810000  mtc1        $at, $f0
    ctx->pc = 0x1c1c20u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x1c1c24: 0x46141082  mul.s       $f2, $f2, $f20
    ctx->pc = 0x1c1c24u;
    ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[20]);
    // 0x1c1c28: 0x3c013f02  lui         $at, 0x3F02
    ctx->pc = 0x1c1c28u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16130 << 16));
    // 0x1c1c2c: 0x34218f5c  ori         $at, $at, 0x8F5C
    ctx->pc = 0x1c1c2cu;
    SET_GPR_U64(ctx, 1, GPR_U64(ctx, 1) | (uint64_t)(uint16_t)36700);
    // 0x1c1c30: 0x44811800  mtc1        $at, $f3
    ctx->pc = 0x1c1c30u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[3], &bits, sizeof(bits)); }
    // 0x1c1c34: 0x46000845  abs.s       $f1, $f1
    ctx->pc = 0x1c1c34u;
    ctx->f[1] = FPU_ABS_S(ctx->f[1]);
    // 0x1c1c38: 0x46031082  mul.s       $f2, $f2, $f3
    ctx->pc = 0x1c1c38u;
    ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[3]);
    // 0x1c1c3c: 0x46010001  sub.s       $f0, $f0, $f1
    ctx->pc = 0x1c1c3cu;
    ctx->f[0] = FPU_SUB_S(ctx->f[0], ctx->f[1]);
    // 0x1c1c40: 0x46020003  div.s       $f0, $f0, $f2
    ctx->pc = 0x1c1c40u;
    if (ctx->f[2] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[0] = copysignf(INFINITY, ctx->f[0] * 0.0f); } else ctx->f[0] = ctx->f[0] / ctx->f[2];
    // 0x1c1c44: 0xe7a00014  swc1        $f0, 0x14($sp)
    ctx->pc = 0x1c1c44u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 20), bits); }
label_1c1c48:
    // 0x1c1c48: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1c48u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1c4c: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1c4cu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1c50: 0x3c06c0c0  lui         $a2, 0xC0C0
    ctx->pc = 0x1c1c50u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)49344 << 16));
    // 0x1c1c54: 0x3c037c02  lui         $v1, 0x7C02
    ctx->pc = 0x1c1c54u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)31746 << 16));
    // 0x1c1c58: 0x34c6c0c0  ori         $a2, $a2, 0xC0C0
    ctx->pc = 0x1c1c58u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | (uint64_t)(uint16_t)49344);
    // 0x1c1c5c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c1c5cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1c60: 0x346302b8  ori         $v1, $v1, 0x2B8
    ctx->pc = 0x1c1c60u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)696);
    // 0x1c1c64: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1c64u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1c68: 0x3c041000  lui         $a0, 0x1000
    ctx->pc = 0x1c1c68u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)4096 << 16));
    // 0x1c1c6c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1c6cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1c70: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1c70u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1c74: 0x3c072000  lui         $a3, 0x2000
    ctx->pc = 0x1c1c74u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)8192 << 16));
    // 0x1c1c78: 0xaca3000c  sw          $v1, 0xC($a1)
    ctx->pc = 0x1c1c78u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 12), GPR_U32(ctx, 3));
    // 0x1c1c7c: 0x3c086c06  lui         $t0, 0x6C06
    ctx->pc = 0x1c1c7cu;
    SET_GPR_S32(ctx, 8, (int32_t)((uint32_t)27654 << 16));
    // 0x1c1c80: 0xaca40000  sw          $a0, 0x0($a1)
    ctx->pc = 0x1c1c80u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 0), GPR_U32(ctx, 4));
    // 0x1c1c84: 0x350802ba  ori         $t0, $t0, 0x2BA
    ctx->pc = 0x1c1c84u;
    SET_GPR_U64(ctx, 8, GPR_U64(ctx, 8) | (uint64_t)(uint16_t)698);
    // 0x1c1c88: 0xaca60008  sw          $a2, 0x8($a1)
    ctx->pc = 0x1c1c88u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 8), GPR_U32(ctx, 6));
    // 0x1c1c8c: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x1c1c8cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1c90: 0xaca70004  sw          $a3, 0x4($a1)
    ctx->pc = 0x1c1c90u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 4), GPR_U32(ctx, 7));
    // 0x1c1c94: 0x3c013f00  lui         $at, 0x3F00
    ctx->pc = 0x1c1c94u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16128 << 16));
    // 0x1c1c98: 0x44812800  mtc1        $at, $f5
    ctx->pc = 0x1c1c98u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[5], &bits, sizeof(bits)); }
    // 0x1c1c9c: 0xc60102dc  lwc1        $f1, 0x2DC($s0)
    ctx->pc = 0x1c1c9cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 732)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1ca0: 0xc60002e0  lwc1        $f0, 0x2E0($s0)
    ctx->pc = 0x1c1ca0u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 736)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1ca4: 0x4601a842  mul.s       $f1, $f21, $f1
    ctx->pc = 0x1c1ca4u;
    ctx->f[1] = FPU_MUL_S(ctx->f[21], ctx->f[1]);
    // 0x1c1ca8: 0xc60202d0  lwc1        $f2, 0x2D0($s0)
    ctx->pc = 0x1c1ca8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 720)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1cac: 0x4600a002  mul.s       $f0, $f20, $f0
    ctx->pc = 0x1c1cacu;
    ctx->f[0] = FPU_MUL_S(ctx->f[20], ctx->f[0]);
    // 0x1c1cb0: 0xc60302cc  lwc1        $f3, 0x2CC($s0)
    ctx->pc = 0x1c1cb0u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 716)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c1cb4: 0xc7a40014  lwc1        $f4, 0x14($sp)
    ctx->pc = 0x1c1cb4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 20)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[4] = f; }
    // 0x1c1cb8: 0x460218c1  sub.s       $f3, $f3, $f2
    ctx->pc = 0x1c1cb8u;
    ctx->f[3] = FPU_SUB_S(ctx->f[3], ctx->f[2]);
    // 0x1c1cbc: 0xc7a60010  lwc1        $f6, 0x10($sp)
    ctx->pc = 0x1c1cbcu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 16)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[6] = f; }
    // 0x1c1cc0: 0x46050002  mul.s       $f0, $f0, $f5
    ctx->pc = 0x1c1cc0u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[5]);
    // 0x1c1cc4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1cc4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1cc8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1cc8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1ccc: 0x46050842  mul.s       $f1, $f1, $f5
    ctx->pc = 0x1c1cccu;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[5]);
    // 0x1c1cd0: 0x3c0146ff  lui         $at, 0x46FF
    ctx->pc = 0x1c1cd0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)18175 << 16));
    // 0x1c1cd4: 0x3421ff00  ori         $at, $at, 0xFF00
    ctx->pc = 0x1c1cd4u;
    SET_GPR_U64(ctx, 1, GPR_U64(ctx, 1) | (uint64_t)(uint16_t)65280);
    // 0x1c1cd8: 0x44813800  mtc1        $at, $f7
    ctx->pc = 0x1c1cd8u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[7], &bits, sizeof(bits)); }
    // 0x1c1cdc: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c1cdcu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1ce0: 0x3c014000  lui         $at, 0x4000
    ctx->pc = 0x1c1ce0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16384 << 16));
    // 0x1c1ce4: 0x4481b000  mtc1        $at, $f22
    ctx->pc = 0x1c1ce4u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[22], &bits, sizeof(bits)); }
    // 0x1c1ce8: 0x46040002  mul.s       $f0, $f0, $f4
    ctx->pc = 0x1c1ce8u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[4]);
    // 0x1c1cec: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1cecu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1cf0: 0x46060842  mul.s       $f1, $f1, $f6
    ctx->pc = 0x1c1cf0u;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[6]);
    // 0x1c1cf4: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1cf4u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1cf8: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1cf8u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1cfc: 0x460718c2  mul.s       $f3, $f3, $f7
    ctx->pc = 0x1c1cfcu;
    ctx->f[3] = FPU_MUL_S(ctx->f[3], ctx->f[7]);
    // 0x1c1d00: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c1d00u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c1d04: 0xe4600004  swc1        $f0, 0x4($v1)
    ctx->pc = 0x1c1d04u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4), bits); }
    // 0x1c1d08: 0xe4610000  swc1        $f1, 0x0($v1)
    ctx->pc = 0x1c1d08u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c1d0c: 0xe4630008  swc1        $f3, 0x8($v1)
    ctx->pc = 0x1c1d0cu;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 8), bits); }
    // 0x1c1d10: 0x3c014500  lui         $at, 0x4500
    ctx->pc = 0x1c1d10u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17664 << 16));
    // 0x1c1d14: 0x44812000  mtc1        $at, $f4
    ctx->pc = 0x1c1d14u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c1d18: 0xc60002cc  lwc1        $f0, 0x2CC($s0)
    ctx->pc = 0x1c1d18u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 716)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1d1c: 0xc7a20000  lwc1        $f2, 0x0($sp)
    ctx->pc = 0x1c1d1cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 0)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1d20: 0x4600b001  sub.s       $f0, $f22, $f0
    ctx->pc = 0x1c1d20u;
    ctx->f[0] = FPU_SUB_S(ctx->f[22], ctx->f[0]);
    // 0x1c1d24: 0xc7a10004  lwc1        $f1, 0x4($sp)
    ctx->pc = 0x1c1d24u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1d28: 0xc60302d0  lwc1        $f3, 0x2D0($s0)
    ctx->pc = 0x1c1d28u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 720)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c1d2c: 0x46151082  mul.s       $f2, $f2, $f21
    ctx->pc = 0x1c1d2cu;
    ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[21]);
    // 0x1c1d30: 0x46140842  mul.s       $f1, $f1, $f20
    ctx->pc = 0x1c1d30u;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[20]);
    // 0x1c1d34: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1d34u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1d38: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1d38u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1d3c: 0x46030001  sub.s       $f0, $f0, $f3
    ctx->pc = 0x1c1d3cu;
    ctx->f[0] = FPU_SUB_S(ctx->f[0], ctx->f[3]);
    // 0x1c1d40: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c1d40u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1d44: 0x46041080  add.s       $f2, $f2, $f4
    ctx->pc = 0x1c1d44u;
    ctx->f[2] = FPU_ADD_S(ctx->f[2], ctx->f[4]);
    // 0x1c1d48: 0x46040840  add.s       $f1, $f1, $f4
    ctx->pc = 0x1c1d48u;
    ctx->f[1] = FPU_ADD_S(ctx->f[1], ctx->f[4]);
    // 0x1c1d4c: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1d4cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1d50: 0x46070002  mul.s       $f0, $f0, $f7
    ctx->pc = 0x1c1d50u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[7]);
    // 0x1c1d54: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1d54u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1d58: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1d58u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1d5c: 0xe4620000  swc1        $f2, 0x0($v1)
    ctx->pc = 0x1c1d5cu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c1d60: 0xe4610004  swc1        $f1, 0x4($v1)
    ctx->pc = 0x1c1d60u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4), bits); }
    // 0x1c1d64: 0xe4600008  swc1        $f0, 0x8($v1)
    ctx->pc = 0x1c1d64u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 8), bits); }
    // 0x1c1d68: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c1d68u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c1d6c: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1d6cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1d70: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1d70u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1d74: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c1d74u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1d78: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1d78u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1d7c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1d7cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1d80: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1d80u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1d84: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x1c1d84u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x1c1d88: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c1d88u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x1c1d8c: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c1d8cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x1c1d90: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x1C1D90u;
    SET_GPR_U32(ctx, 31, 0x1C1D98u);
    ctx->pc = 0x1C1D94u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1D90u;
    // 0x1c1d94: 0xac68000c  sw          $t0, 0xC($v1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 8));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x1C1D90u, 0x1C1D98u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1D98u;
label_1c1d98:
    // 0x1c1d98: 0x78430030  lq          $v1, 0x30($v0)
    ctx->pc = 0x1c1d98u;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 2), 48)));
    // 0x1c1d9c: 0x27a60030  addiu       $a2, $sp, 0x30
    ctx->pc = 0x1c1d9cu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
    // 0x1c1da0: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1da0u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1da4: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1da4u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1da8: 0xc0882d  daddu       $s1, $a2, $zero
    ctx->pc = 0x1c1da8u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 6) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1dac: 0x44802000  mtc1        $zero, $f4
    ctx->pc = 0x1c1dacu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c1db0: 0x7c430000  sq          $v1, 0x0($v0)
    ctx->pc = 0x1c1db0u;
    WRITE128(ADD32(GPR_U32(ctx, 2), 0), GPR_VEC(ctx, 3));
    // 0x1c1db4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1db4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1db8: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c1db8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c1dbc: 0x44811000  mtc1        $at, $f2
    ctx->pc = 0x1c1dbcu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }
    // 0x1c1dc0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1dc0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1dc4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1dc4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1dc8: 0xc60002c8  lwc1        $f0, 0x2C8($s0)
    ctx->pc = 0x1c1dc8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 712)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1dcc: 0x7a030100  lq          $v1, 0x100($s0)
    ctx->pc = 0x1c1dccu;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 16), 256)));
    // 0x1c1dd0: 0x7a040110  lq          $a0, 0x110($s0)
    ctx->pc = 0x1c1dd0u;
    SET_GPR_VEC(ctx, 4, READ128(ADD32(GPR_U32(ctx, 16), 272)));
    // 0x1c1dd4: 0x46040032  c.eq.s      $f0, $f4
    ctx->pc = 0x1c1dd4u;
    ctx->fcr31 = (FPU_C_EQ_S(ctx->f[0], ctx->f[4])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
    // 0x1c1dd8: 0x7a050120  lq          $a1, 0x120($s0)
    ctx->pc = 0x1c1dd8u;
    SET_GPR_VEC(ctx, 5, READ128(ADD32(GPR_U32(ctx, 16), 288)));
    // 0x1c1ddc: 0x7a020130  lq          $v0, 0x130($s0)
    ctx->pc = 0x1c1ddcu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 304)));
    // 0x1c1de0: 0x7fa30030  sq          $v1, 0x30($sp)
    ctx->pc = 0x1c1de0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 48), GPR_VEC(ctx, 3));
    // 0x1c1de4: 0x7fa40040  sq          $a0, 0x40($sp)
    ctx->pc = 0x1c1de4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 64), GPR_VEC(ctx, 4));
    // 0x1c1de8: 0x7fa50050  sq          $a1, 0x50($sp)
    ctx->pc = 0x1c1de8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 80), GPR_VEC(ctx, 5));
    // 0x1c1dec: 0x7fa20060  sq          $v0, 0x60($sp)
    ctx->pc = 0x1c1decu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 96), GPR_VEC(ctx, 2));
    // 0x1c1df0: 0xe7a4005c  swc1        $f4, 0x5C($sp)
    ctx->pc = 0x1c1df0u;
    { float f = ctx->f[4]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 92), bits); }
    // 0x1c1df4: 0xe7a4004c  swc1        $f4, 0x4C($sp)
    ctx->pc = 0x1c1df4u;
    { float f = ctx->f[4]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 76), bits); }
    // 0x1c1df8: 0xe7a4003c  swc1        $f4, 0x3C($sp)
    ctx->pc = 0x1c1df8u;
    { float f = ctx->f[4]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 60), bits); }
    // 0x1c1dfc: 0x45000009  bc1f        . + 4 + (0x9 << 2)
    ctx->pc = 0x1C1DFCu;
    {
        const bool branch_taken_0x1c1dfc = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x1C1E00u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1DFCu;
        // 0x1c1e00: 0xe4c2003c  swc1        $f2, 0x3C($a2) (Delay Slot)
        { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 6), 60), bits); }
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1dfc) {
            ctx->pc = 0x1C1E24u;
            goto label_1c1e24;
        }
    }
    ctx->pc = 0x1C1E04u;
    // 0x1c1e04: 0xc60102c4  lwc1        $f1, 0x2C4($s0)
    ctx->pc = 0x1c1e04u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 708)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1e08: 0xc60002c0  lwc1        $f0, 0x2C0($s0)
    ctx->pc = 0x1c1e08u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 704)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1e0c: 0x46000801  sub.s       $f0, $f1, $f0
    ctx->pc = 0x1c1e0cu;
    ctx->f[0] = FPU_SUB_S(ctx->f[1], ctx->f[0]);
    // 0x1c1e10: 0x4600b003  div.s       $f0, $f22, $f0
    ctx->pc = 0x1c1e10u;
    if (ctx->f[0] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[0] = copysignf(INFINITY, ctx->f[22] * 0.0f); } else ctx->f[0] = ctx->f[22] / ctx->f[0];
    // 0x1c1e14: 0x46000842  mul.s       $f1, $f1, $f0
    ctx->pc = 0x1c1e14u;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[0]);
    // 0x1c1e18: 0xe7a00048  swc1        $f0, 0x48($sp)
    ctx->pc = 0x1c1e18u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 72), bits); }
    // 0x1c1e1c: 0x1000000c  b           . + 4 + (0xC << 2)
    ctx->pc = 0x1C1E1Cu;
    {
        const bool branch_taken_0x1c1e1c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C1E20u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1E1Cu;
        // 0x1c1e20: 0x46011041  sub.s       $f1, $f2, $f1 (Delay Slot)
        ctx->f[1] = FPU_SUB_S(ctx->f[2], ctx->f[1]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1e1c) {
            ctx->pc = 0x1C1E50u;
            goto label_1c1e50;
        }
    }
    ctx->pc = 0x1C1E24u;
label_1c1e24:
    // 0x1c1e24: 0xc60102c4  lwc1        $f1, 0x2C4($s0)
    ctx->pc = 0x1c1e24u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 708)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1e28: 0xc60002c0  lwc1        $f0, 0x2C0($s0)
    ctx->pc = 0x1c1e28u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 704)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1e2c: 0xc7a20048  lwc1        $f2, 0x48($sp)
    ctx->pc = 0x1c1e2cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 72)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1e30: 0x460008c1  sub.s       $f3, $f1, $f0
    ctx->pc = 0x1c1e30u;
    ctx->f[3] = FPU_SUB_S(ctx->f[1], ctx->f[0]);
    // 0x1c1e34: 0xe7a4006c  swc1        $f4, 0x6C($sp)
    ctx->pc = 0x1c1e34u;
    { float f = ctx->f[4]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 108), bits); }
    // 0x1c1e38: 0x46000800  add.s       $f0, $f1, $f0
    ctx->pc = 0x1c1e38u;
    ctx->f[0] = FPU_ADD_S(ctx->f[1], ctx->f[0]);
    // 0x1c1e3c: 0xe7a2004c  swc1        $f2, 0x4C($sp)
    ctx->pc = 0x1c1e3cu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 76), bits); }
    // 0x1c1e40: 0x46030003  div.s       $f0, $f0, $f3
    ctx->pc = 0x1c1e40u;
    if (ctx->f[3] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[0] = copysignf(INFINITY, ctx->f[0] * 0.0f); } else ctx->f[0] = ctx->f[0] / ctx->f[3];
    // 0x1c1e44: 0x46000882  mul.s       $f2, $f1, $f0
    ctx->pc = 0x1c1e44u;
    ctx->f[2] = FPU_MUL_S(ctx->f[1], ctx->f[0]);
    // 0x1c1e48: 0xe7a00048  swc1        $f0, 0x48($sp)
    ctx->pc = 0x1c1e48u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 72), bits); }
    // 0x1c1e4c: 0x46020841  sub.s       $f1, $f1, $f2
    ctx->pc = 0x1c1e4cu;
    ctx->f[1] = FPU_SUB_S(ctx->f[1], ctx->f[2]);
label_1c1e50:
    // 0x1c1e50: 0xe7a10068  swc1        $f1, 0x68($sp)
    ctx->pc = 0x1c1e50u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 104), bits); }
    // 0x1c1e54: 0xc7a30030  lwc1        $f3, 0x30($sp)
    ctx->pc = 0x1c1e54u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 48)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c1e58: 0x260400c0  addiu       $a0, $s0, 0xC0
    ctx->pc = 0x1c1e58u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 192));
    // 0x1c1e5c: 0xc7a10054  lwc1        $f1, 0x54($sp)
    ctx->pc = 0x1c1e5cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 84)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c1e60: 0x220282d  daddu       $a1, $s1, $zero
    ctx->pc = 0x1c1e60u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1e64: 0xc7a20010  lwc1        $f2, 0x10($sp)
    ctx->pc = 0x1c1e64u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 16)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c1e68: 0x220302d  daddu       $a2, $s1, $zero
    ctx->pc = 0x1c1e68u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1e6c: 0xc7a00014  lwc1        $f0, 0x14($sp)
    ctx->pc = 0x1c1e6cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 20)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c1e70: 0x460218c3  div.s       $f3, $f3, $f2
    ctx->pc = 0x1c1e70u;
    if (ctx->f[2] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[3] = copysignf(INFINITY, ctx->f[3] * 0.0f); } else ctx->f[3] = ctx->f[3] / ctx->f[2];
    // 0x1c1e74: 0x46000843  div.s       $f1, $f1, $f0
    ctx->pc = 0x1c1e74u;
    if (ctx->f[0] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[1] = copysignf(INFINITY, ctx->f[1] * 0.0f); } else ctx->f[1] = ctx->f[1] / ctx->f[0];
    // 0x1c1e78: 0xe7a30030  swc1        $f3, 0x30($sp)
    ctx->pc = 0x1c1e78u;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 48), bits); }
    // 0x1c1e7c: 0xe7a10054  swc1        $f1, 0x54($sp)
    ctx->pc = 0x1c1e7cu;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 84), bits); }
    // 0x1c1e80: 0x46000847  neg.s       $f1, $f1
    ctx->pc = 0x1c1e80u;
    ctx->f[1] = FPU_NEG_S(ctx->f[1]);
    // 0x1c1e84: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c1e84u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1e88: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c1e88u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c1e8c: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c1e8cu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1e90: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c1e90u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c1e94: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1e94u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1e98: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c1e98u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c1e9c: 0xe4630000  swc1        $f3, 0x0($v1)
    ctx->pc = 0x1c1e9cu;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c1ea0: 0xe4610004  swc1        $f1, 0x4($v1)
    ctx->pc = 0x1c1ea0u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4), bits); }
    // 0x1c1ea4: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c1ea4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c1ea8: 0xc0cbc62  jal         func_32F188
    ctx->pc = 0x1C1EA8u;
    SET_GPR_U32(ctx, 31, 0x1C1EB0u);
    ctx->pc = 0x1C1EACu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C1EA8u;
    // 0x1c1eac: 0xac600008  sw          $zero, 0x8($v1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x32F188u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32F188u, 0x1C1EA8u, 0x1C1EB0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C1EB0u;
label_1c1eb0:
    // 0x1c1eb0: 0x2406ffff  addiu       $a2, $zero, -0x1
    ctx->pc = 0x1c1eb0u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x1c1eb4: 0x220282d  daddu       $a1, $s1, $zero
    ctx->pc = 0x1c1eb4u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c1eb8: 0x24040003  addiu       $a0, $zero, 0x3
    ctx->pc = 0x1c1eb8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x1c1ebc: 0x0  nop
    ctx->pc = 0x1c1ebcu;
    // NOP
label_1c1ec0:
    // 0x1c1ec0: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c1ec0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1ec4: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c1ec4u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x1c1ec8: 0x2484ffff  addiu       $a0, $a0, -0x1
    ctx->pc = 0x1c1ec8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294967295));
    // 0x1c1ecc: 0x78a20000  lq          $v0, 0x0($a1)
    ctx->pc = 0x1c1eccu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x1c1ed0: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c1ed0u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x1c1ed4: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c1ed4u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x1c1ed8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c1ed8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c1edc: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c1edcu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x1c1ee0: 0x1486fff7  bne         $a0, $a2, . + 4 + (-0x9 << 2)
    ctx->pc = 0x1C1EE0u;
    {
        const bool branch_taken_0x1c1ee0 = (GPR_U64(ctx, 4) != GPR_U64(ctx, 6));
        ctx->pc = 0x1C1EE4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1EE0u;
        // 0x1c1ee4: 0x24a50010  addiu       $a1, $a1, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c1ee0) {
            ctx->pc = 0x1C1EC0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c1ec0;
        }
    }
    ctx->pc = 0x1C1EE8u;
    // Return path, reached once per call by falling out of the copy loop
    // above (the only `jr $ra` is at 0x1c1f10). $s0 is still `this` here;
    // the next instruction restores it. The packet cursor already points
    // past qw703.
    ghpcCamCapture(rdram, ctx, runtime, GPR_U32(ctx, 16));
    {
        static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
        if (s_log) {
            static uint64_t s_calls = 0;
            const uint64_t n = ++s_calls;
            if (n <= 40 || (n % 500) == 0) {
                ghpcCamLog(rdram, ctx, runtime, GPR_U32(ctx, 16), n);
            }
        }
    }
    // 0x1c1ee8: 0x7bb000d0  lq          $s0, 0xD0($sp)
    ctx->pc = 0x1c1ee8u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 208)));
    // 0x1c1eec: 0x7bb100c0  lq          $s1, 0xC0($sp)
    ctx->pc = 0x1c1eecu;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 192)));
    // 0x1c1ef0: 0x7bb200b0  lq          $s2, 0xB0($sp)
    ctx->pc = 0x1c1ef0u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 176)));
    // 0x1c1ef4: 0x7bb300a0  lq          $s3, 0xA0($sp)
    ctx->pc = 0x1c1ef4u;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x1c1ef8: 0x7bb40090  lq          $s4, 0x90($sp)
    ctx->pc = 0x1c1ef8u;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x1c1efc: 0x7bb50080  lq          $s5, 0x80($sp)
    ctx->pc = 0x1c1efcu;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x1c1f00: 0xdfbf0070  ld          $ra, 0x70($sp)
    ctx->pc = 0x1c1f00u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c1f04: 0xc7b600f0  lwc1        $f22, 0xF0($sp)
    ctx->pc = 0x1c1f04u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 240)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[22] = f; }
    // 0x1c1f08: 0xc7b500e8  lwc1        $f21, 0xE8($sp)
    ctx->pc = 0x1c1f08u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 232)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[21] = f; }
    // 0x1c1f0c: 0xc7b400e0  lwc1        $f20, 0xE0($sp)
    ctx->pc = 0x1c1f0cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 224)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[20] = f; }
    // 0x1c1f10: 0x3e00008  jr          $ra
    ctx->pc = 0x1C1F10u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C1F14u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C1F10u;
        // 0x1c1f14: 0x27bd0100  addiu       $sp, $sp, 0x100 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 256));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C1F10u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C1F18u;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&Select__5PsCam_0x1c1770), PS2Runtime::RecompiledFunction>::value,
              "Select__5PsCam_0x1c1770 override no longer matches the recompiled signature");
