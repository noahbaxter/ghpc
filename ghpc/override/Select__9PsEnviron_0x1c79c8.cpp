// Override of work/output/Select__9PsEnviron_0x1c79c8.cpp (PsEnviron::Select,
// 0x1c79c8 - 0x1c7f00).
//
// Why: the lights. The body keeps the generated translation verbatim and, at
// the return, hands the Rnd seam the light block it just staged, which is the
// only readable copy of it host-side.
//
// What the body writes, behind 0x6C0702A9 (V4-32, NUM 7, ADDR 681) at
// 0x1c7bf0..0x1c7c50:
//   qw681      ambient, RndEnviron +0x40..+0x48 with w = 1.0 (0x1c7c64)
//   qw682..684 per light colour, from light +0xC0 (0x1c7dc0), up to 3 lights
//   qw685..687 per light, a normalised and negated direction, stored as the
//              columns of a 3x3; the point-light program 0x21b0 reads them
//              as positions instead (it subtracts the vertex position)
// and the VU1 lighting program for the light count to global 0x440E24
// (0x7C5 stored first at 0x1c7c94, then replaced): 0x7C5 ambient only,
// 0x6EC three directional (overlay 0x3760), 0x436 point (overlay 0x21b0).
// PsMat::Select forwards that global as qw688.x when mUseEnviron is set.
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ghpc_scene.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Where 0x1c7c4c stored this call's light block header, 0 when the call took
// a path that uploads none. A scan back from the packet cursor does not work:
// it found headers 500 quadwords back, stale ones from older packets.
static uint32_t g_ghpcEnvironHeader = 0u;

static void ghpcEnvironCapture(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t h = g_ghpcEnvironHeader;
    g_ghpcEnvironHeader = 0u;
    if (!ghpc::scene::active()) return;
    static uint64_t s_calls = 0, s_found = 0;
    ++s_calls;
    // Reported sparsely: a miss here means the recorded header and the body
    // disagree, and it would otherwise look like an unlit scene.
    if ((s_calls & (s_calls - 1u)) == 0u) {
        std::fprintf(stderr, "[ghpc/environ] selects=%llu captured=%llu\n",
                     (unsigned long long)s_calls, (unsigned long long)s_found);
    }
    if (h == 0u || READ32(h + 0xcu) != 0x6C0702A9u) return;
    ghpc::scene::Lights lights{};
    for (uint32_t q = 0; q < 7u; ++q) {
        for (uint32_t k = 0; k < 4u; ++k) {
            const uint32_t bits = READ32(h + 0x10u + q * 0x10u + k * 4u);
            std::memcpy(&lights.qw[q][k], &bits, sizeof(float));
        }
    }
    lights.program = READ32(0x440E24u);
    ghpc::scene::setLights(lights);
    ++s_found;
    if (s_found <= 4u) {
        std::fprintf(stderr, "[ghpc/environ] program 0x%x ambient=(%g %g %g %g) col0=(%g %g %g)\n",
                     lights.program, lights.qw[0][0], lights.qw[0][1], lights.qw[0][2],
                     lights.qw[0][3], lights.qw[1][0], lights.qw[1][1], lights.qw[1][2]);
    }
}

// Function: Select__9PsEnviron
// Address: 0x1c79c8 - 0x1c7f00
void Select__9PsEnviron_0x1c79c8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Select__9PsEnviron_0x1c79c8");
#endif

    switch (ctx->pc) {
        case 0x1c7a24u: goto label_1c7a24;
        case 0x1c7a34u: goto label_1c7a34;
        case 0x1c7a48u: goto label_1c7a48;
        case 0x1c7a6cu: goto label_1c7a6c;
        case 0x1c7a7cu: goto label_1c7a7c;
        case 0x1c7aa4u: goto label_1c7aa4;
        case 0x1c7b04u: goto label_1c7b04;
        case 0x1c7ca8u: goto label_1c7ca8;
        case 0x1c7ce0u: goto label_1c7ce0;
        case 0x1c7d08u: goto label_1c7d08;
        case 0x1c7d50u: goto label_1c7d50;
        case 0x1c7d68u: goto label_1c7d68;
        case 0x1c7e18u: goto label_1c7e18;
        case 0x1c7ec0u: goto label_1c7ec0;
        case 0x1c7eccu: goto label_1c7ecc;
        default: break;
    }

    ctx->pc = 0x1c79c8u;

    // 0x1c79c8: 0x27bdfed0  addiu       $sp, $sp, -0x130
    ctx->pc = 0x1c79c8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294966992));
    // 0x1c79cc: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c79ccu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x1c79d0: 0x7fb00110  sq          $s0, 0x110($sp)
    ctx->pc = 0x1c79d0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 272), GPR_VEC(ctx, 16));
    // 0x1c79d4: 0x7fb10100  sq          $s1, 0x100($sp)
    ctx->pc = 0x1c79d4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 256), GPR_VEC(ctx, 17));
    // 0x1c79d8: 0x7fb200f0  sq          $s2, 0xF0($sp)
    ctx->pc = 0x1c79d8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 240), GPR_VEC(ctx, 18));
    // 0x1c79dc: 0x3c11004f  lui         $s1, 0x4F
    ctx->pc = 0x1c79dcu;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)79 << 16));
    // 0x1c79e0: 0x7fb300e0  sq          $s3, 0xE0($sp)
    ctx->pc = 0x1c79e0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 224), GPR_VEC(ctx, 19));
    // 0x1c79e4: 0x26312780  addiu       $s1, $s1, 0x2780
    ctx->pc = 0x1c79e4u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 17), 10112));
    // 0x1c79e8: 0xe7b40120  swc1        $f20, 0x120($sp)
    ctx->pc = 0x1c79e8u;
    { float f = ctx->f[20]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 288), bits); }
    // 0x1c79ec: 0x80982d  daddu       $s3, $a0, $zero
    ctx->pc = 0x1c79ecu;
    SET_GPR_U64(ctx, 19, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c79f0: 0x7fb400d0  sq          $s4, 0xD0($sp)
    ctx->pc = 0x1c79f0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 208), GPR_VEC(ctx, 20));
    // 0x1c79f4: 0x26720060  addiu       $s2, $s3, 0x60
    ctx->pc = 0x1c79f4u;
    SET_GPR_S32(ctx, 18, (int32_t)ADD32(GPR_U32(ctx, 19), 96));
    // 0x1c79f8: 0x7fb500c0  sq          $s5, 0xC0($sp)
    ctx->pc = 0x1c79f8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 192), GPR_VEC(ctx, 21));
    // 0x1c79fc: 0x7fb600b0  sq          $s6, 0xB0($sp)
    ctx->pc = 0x1c79fcu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 176), GPR_VEC(ctx, 22));
    // 0x1c7a00: 0x7fb700a0  sq          $s7, 0xA0($sp)
    ctx->pc = 0x1c7a00u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 23));
    // 0x1c7a04: 0x7fbe0090  sq          $fp, 0x90($sp)
    ctx->pc = 0x1c7a04u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 144), GPR_VEC(ctx, 30));
    // 0x1c7a08: 0xffbf0080  sd          $ra, 0x80($sp)
    ctx->pc = 0x1c7a08u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 128), GPR_U64(ctx, 31));
    // 0x1c7a0c: 0xac400e0c  sw          $zero, 0xE0C($v0)
    ctx->pc = 0x1c7a0cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x440E0Cu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x440E0Cu, _value); } while (0);
    // 0x1c7a10: 0x3c01437f  lui         $at, 0x437F
    ctx->pc = 0x1c7a10u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17279 << 16));
    // 0x1c7a14: 0x4481a000  mtc1        $at, $f20
    ctx->pc = 0x1c7a14u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[20], &bits, sizeof(bits)); }
    // 0x1c7a18: 0xc66c0060  lwc1        $f12, 0x60($s3)
    ctx->pc = 0x1c7a18u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 96)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[12] = f; }
    // 0x1c7a1c: 0xc040494  jal         func_101250
    ctx->pc = 0x1C7A1Cu;
    SET_GPR_U32(ctx, 31, 0x1C7A24u);
    ctx->pc = 0x1C7A20u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7A1Cu;
    // 0x1c7a20: 0x46146302  mul.s       $f12, $f12, $f20 (Delay Slot)
    ctx->f[12] = FPU_MUL_S(ctx->f[12], ctx->f[20]);
    ctx->in_delay_slot = false;
    ctx->pc = 0x101250u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x101250u, 0x1C7A1Cu, 0x1C7A24u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7A24u;
label_1c7a24:
    // 0x1c7a24: 0xc64c0004  lwc1        $f12, 0x4($s2)
    ctx->pc = 0x1c7a24u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 18), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[12] = f; }
    // 0x1c7a28: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x1c7a28u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7a2c: 0xc040494  jal         func_101250
    ctx->pc = 0x1C7A2Cu;
    SET_GPR_U32(ctx, 31, 0x1C7A34u);
    ctx->pc = 0x1C7A30u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7A2Cu;
    // 0x1c7a30: 0x46146302  mul.s       $f12, $f12, $f20 (Delay Slot)
    ctx->f[12] = FPU_MUL_S(ctx->f[12], ctx->f[20]);
    ctx->in_delay_slot = false;
    ctx->pc = 0x101250u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x101250u, 0x1C7A2Cu, 0x1C7A34u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7A34u;
label_1c7a34:
    // 0x1c7a34: 0xc64c0008  lwc1        $f12, 0x8($s2)
    ctx->pc = 0x1c7a34u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 18), 8)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[12] = f; }
    // 0x1c7a38: 0x21200  sll         $v0, $v0, 8
    ctx->pc = 0x1c7a38u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 8));
    // 0x1c7a3c: 0x2028025  or          $s0, $s0, $v0
    ctx->pc = 0x1c7a3cu;
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 16) | GPR_U64(ctx, 2));
    // 0x1c7a40: 0xc040494  jal         func_101250
    ctx->pc = 0x1C7A40u;
    SET_GPR_U32(ctx, 31, 0x1C7A48u);
    ctx->pc = 0x1C7A44u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7A40u;
    // 0x1c7a44: 0x46146302  mul.s       $f12, $f12, $f20 (Delay Slot)
    ctx->f[12] = FPU_MUL_S(ctx->f[12], ctx->f[20]);
    ctx->in_delay_slot = false;
    ctx->pc = 0x101250u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x101250u, 0x1C7A40u, 0x1C7A48u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7A48u;
label_1c7a48:
    // 0x1c7a48: 0x21400  sll         $v0, $v0, 16
    ctx->pc = 0x1c7a48u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 16));
    // 0x1c7a4c: 0x3c0700ff  lui         $a3, 0xFF
    ctx->pc = 0x1c7a4cu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)255 << 16));
    // 0x1c7a50: 0x34e7ffff  ori         $a3, $a3, 0xFFFF
    ctx->pc = 0x1c7a50u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)65535);
    // 0x1c7a54: 0x2028025  or          $s0, $s0, $v0
    ctx->pc = 0x1c7a54u;
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 16) | GPR_U64(ctx, 2));
    // 0x1c7a58: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c7a58u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7a5c: 0x10803c  dsll32      $s0, $s0, 0
    ctx->pc = 0x1c7a5cu;
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 16) << (32 + 0));
    // 0x1c7a60: 0x2405003d  addiu       $a1, $zero, 0x3D
    ctx->pc = 0x1c7a60u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 61));
    // 0x1c7a64: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x1C7A64u;
    SET_GPR_U32(ctx, 31, 0x1C7A6Cu);
    ctx->pc = 0x1C7A68u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7A64u;
    // 0x1c7a68: 0x10303e  dsrl32      $a2, $s0, 0 (Delay Slot)
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 16) >> (32 + 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x1C7A64u, 0x1C7A6Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7A6Cu;
label_1c7a6c:
    // 0x1c7a6c: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c7a6cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7a70: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c7a70u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c7a74: 0xc10fa18  jal         func_43E860
    ctx->pc = 0x1C7A74u;
    SET_GPR_U32(ctx, 31, 0x1C7A7Cu);
    ctx->pc = 0x1C7A78u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7A74u;
    // 0x1c7a78: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E860u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E860u, 0x1C7A74u, 0x1C7A7Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7A7Cu;
label_1c7a7c:
    // 0x1c7a7c: 0x8e620050  lw          $v0, 0x50($s3)
    ctx->pc = 0x1c7a7cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 19), 80)));
    // 0x1c7a80: 0x5040004d  beql        $v0, $zero, . + 4 + (0x4D << 2)
    ctx->pc = 0x1C7A80u;
    {
        const bool branch_taken_0x1c7a80 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c7a80) {
            ctx->pc = 0x1C7A84u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C7A80u;
            // 0x1c7a84: 0x4600a086  mov.s       $f2, $f20 (Delay Slot)
            ctx->f[2] = FPU_MOV_S(ctx->f[20]);
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C7BB8u;
            goto label_1c7bb8;
        }
    }
    ctx->pc = 0x1C7A88u;
    // 0x1c7a88: 0xc6600058  lwc1        $f0, 0x58($s3)
    ctx->pc = 0x1c7a88u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 88)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7a8c: 0xc6610054  lwc1        $f1, 0x54($s3)
    ctx->pc = 0x1c7a8cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 84)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7a90: 0x46000087  neg.s       $f2, $f0
    ctx->pc = 0x1c7a90u;
    ctx->f[2] = FPU_NEG_S(ctx->f[0]);
    // 0x1c7a94: 0x46000841  sub.s       $f1, $f1, $f0
    ctx->pc = 0x1c7a94u;
    ctx->f[1] = FPU_SUB_S(ctx->f[1], ctx->f[0]);
    // 0x1c7a98: 0x4601a003  div.s       $f0, $f20, $f1
    ctx->pc = 0x1c7a98u;
    if (ctx->f[1] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[0] = copysignf(INFINITY, ctx->f[20] * 0.0f); } else ctx->f[0] = ctx->f[20] / ctx->f[1];
    // 0x1c7a9c: 0x10000047  b           . + 4 + (0x47 << 2)
    ctx->pc = 0x1C7A9Cu;
    {
        const bool branch_taken_0x1c7a9c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7AA0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7A9Cu;
        // 0x1c7aa0: 0x46001082  mul.s       $f2, $f2, $f0 (Delay Slot)
        ctx->f[2] = FPU_MUL_S(ctx->f[2], ctx->f[0]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7a9c) {
            ctx->pc = 0x1C7BBCu;
            goto label_1c7bbc;
        }
    }
    ctx->pc = 0x1C7AA4u;
label_1c7aa4:
    // 0x1c7aa4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7aa4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7aa8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7aa8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7aac: 0xafc30e24  sw          $v1, 0xE24($fp)
    ctx->pc = 0x1c7aacu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 3620), GPR_U32(ctx, 3));
    // 0x1c7ab0: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c7ab0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7ab4: 0x8c830000  lw          $v1, 0x0($a0)
    ctx->pc = 0x1c7ab4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x1c7ab8: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7ab8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7abc: 0x246400c0  addiu       $a0, $v1, 0xC0
    ctx->pc = 0x1c7abcu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 192));
    // 0x1c7ac0: 0xc46100c0  lwc1        $f1, 0xC0($v1)
    ctx->pc = 0x1c7ac0u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 3), 192)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7ac4: 0xc4820008  lwc1        $f2, 0x8($a0)
    ctx->pc = 0x1c7ac4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 4), 8)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c7ac8: 0xc4800004  lwc1        $f0, 0x4($a0)
    ctx->pc = 0x1c7ac8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 4), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7acc: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7accu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7ad0: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7ad0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7ad4: 0xe4a10000  swc1        $f1, 0x0($a1)
    ctx->pc = 0x1c7ad4u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 5), 0), bits); }
    // 0x1c7ad8: 0xe4a00004  swc1        $f0, 0x4($a1)
    ctx->pc = 0x1c7ad8u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 5), 4), bits); }
    // 0x1c7adc: 0xe4a20008  swc1        $f2, 0x8($a1)
    ctx->pc = 0x1c7adcu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 5), 8), bits); }
    // 0x1c7ae0: 0xaca0000c  sw          $zero, 0xC($a1)
    ctx->pc = 0x1c7ae0u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 12), GPR_U32(ctx, 0));
    // 0x1c7ae4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7ae4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7ae8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7ae8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7aec: 0x24420020  addiu       $v0, $v0, 0x20
    ctx->pc = 0x1c7aecu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 32));
    // 0x1c7af0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7af0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7af4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7af4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7af8: 0x8fa30070  lw          $v1, 0x70($sp)
    ctx->pc = 0x1c7af8u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7afc: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x1C7AFCu;
    SET_GPR_U32(ctx, 31, 0x1C7B04u);
    ctx->pc = 0x1C7B00u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7AFCu;
    // 0x1c7b00: 0x8c640000  lw          $a0, 0x0($v1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x1C7AFCu, 0x1C7B04u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7B04u;
label_1c7b04:
    // 0x1c7b04: 0x8fa40070  lw          $a0, 0x70($sp)
    ctx->pc = 0x1c7b04u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7b08: 0x24460030  addiu       $a2, $v0, 0x30
    ctx->pc = 0x1c7b08u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 2), 48));
    // 0x1c7b0c: 0x3c01bf80  lui         $at, 0xBF80
    ctx->pc = 0x1c7b0cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)49024 << 16));
    // 0x1c7b10: 0x44810800  mtc1        $at, $f1
    ctx->pc = 0x1c7b10u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
    // 0x1c7b14: 0x8c850000  lw          $a1, 0x0($a0)
    ctx->pc = 0x1c7b14u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x1c7b18: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c7b18u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7b1c: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c7b1cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x1c7b20: 0xc4a000d0  lwc1        $f0, 0xD0($a1)
    ctx->pc = 0x1c7b20u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 5), 208)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7b24: 0x60202d  daddu       $a0, $v1, $zero
    ctx->pc = 0x1c7b24u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7b28: 0xc4420030  lwc1        $f2, 0x30($v0)
    ctx->pc = 0x1c7b28u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 48)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c7b2c: 0x46000843  div.s       $f1, $f1, $f0
    ctx->pc = 0x1c7b2cu;
    if (ctx->f[0] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[1] = copysignf(INFINITY, ctx->f[1] * 0.0f); } else ctx->f[1] = ctx->f[1] / ctx->f[0];
    // 0x1c7b30: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c7b30u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x1c7b34: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7b34u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7b38: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c7b38u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x1c7b3c: 0xe4820000  swc1        $f2, 0x0($a0)
    ctx->pc = 0x1c7b3cu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 4), 0), bits); }
    // 0x1c7b40: 0xac800004  sw          $zero, 0x4($a0)
    ctx->pc = 0x1c7b40u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 4), GPR_U32(ctx, 0));
    // 0x1c7b44: 0xac800008  sw          $zero, 0x8($a0)
    ctx->pc = 0x1c7b44u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 8), GPR_U32(ctx, 0));
    // 0x1c7b48: 0xe481000c  swc1        $f1, 0xC($a0)
    ctx->pc = 0x1c7b48u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 4), 12), bits); }
    // 0x1c7b4c: 0x8fa50070  lw          $a1, 0x70($sp)
    ctx->pc = 0x1c7b4cu;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7b50: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7b50u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7b54: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7b54u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7b58: 0x8ca40000  lw          $a0, 0x0($a1)
    ctx->pc = 0x1c7b58u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x1c7b5c: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c7b5cu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7b60: 0xc4c10004  lwc1        $f1, 0x4($a2)
    ctx->pc = 0x1c7b60u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 6), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7b64: 0xc48000d0  lwc1        $f0, 0xD0($a0)
    ctx->pc = 0x1c7b64u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 4), 208)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7b68: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7b68u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7b6c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7b6cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7b70: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7b70u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7b74: 0x46000002  mul.s       $f0, $f0, $f0
    ctx->pc = 0x1c7b74u;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[0]);
    // 0x1c7b78: 0xe4610000  swc1        $f1, 0x0($v1)
    ctx->pc = 0x1c7b78u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c7b7c: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c7b7cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x1c7b80: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c7b80u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x1c7b84: 0xe460000c  swc1        $f0, 0xC($v1)
    ctx->pc = 0x1c7b84u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 12), bits); }
    // 0x1c7b88: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7b88u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7b8c: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7b8cu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7b90: 0xc4c00008  lwc1        $f0, 0x8($a2)
    ctx->pc = 0x1c7b90u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 6), 8)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7b94: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c7b94u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7b98: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7b98u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7b9c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7b9cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7ba0: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7ba0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7ba4: 0xe4600000  swc1        $f0, 0x0($v1)
    ctx->pc = 0x1c7ba4u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c7ba8: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c7ba8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c7bac: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c7bacu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x1c7bb0: 0x10000045  b           . + 4 + (0x45 << 2)
    ctx->pc = 0x1C7BB0u;
    {
        const bool branch_taken_0x1c7bb0 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7BB4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7BB0u;
        // 0x1c7bb4: 0xac600008  sw          $zero, 0x8($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7bb0) {
            ctx->pc = 0x1C7CC8u;
            goto label_1c7cc8;
        }
    }
    ctx->pc = 0x1C7BB8u;
label_1c7bb8:
    // 0x1c7bb8: 0x44800000  mtc1        $zero, $f0
    ctx->pc = 0x1c7bb8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
label_1c7bbc:
    // 0x1c7bbc: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7bbcu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7bc0: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7bc0u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7bc4: 0x3c053f3f  lui         $a1, 0x3F3F
    ctx->pc = 0x1c7bc4u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)16191 << 16));
    // 0x1c7bc8: 0x3c037002  lui         $v1, 0x7002
    ctx->pc = 0x1c7bc8u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28674 << 16));
    // 0x1c7bcc: 0x3c071000  lui         $a3, 0x1000
    ctx->pc = 0x1c7bccu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)4096 << 16));
    // 0x1c7bd0: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x1c7bd0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7bd4: 0x3c082000  lui         $t0, 0x2000
    ctx->pc = 0x1c7bd4u;
    SET_GPR_S32(ctx, 8, (int32_t)((uint32_t)8192 << 16));
    // 0x1c7bd8: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7bd8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7bdc: 0x34a53f3f  ori         $a1, $a1, 0x3F3F
    ctx->pc = 0x1c7bdcu;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)16191);
    // 0x1c7be0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7be0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7be4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7be4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7be8: 0x346302b8  ori         $v1, $v1, 0x2B8
    ctx->pc = 0x1c7be8u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)696);
    // 0x1c7bec: 0x44020000  mfc1        $v0, $f0
    ctx->pc = 0x1c7becu;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[0], sizeof(bits)); SET_GPR_U32(ctx, 2, bits); }
    // 0x1c7bf0: 0x3c066c07  lui         $a2, 0x6C07
    ctx->pc = 0x1c7bf0u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)27655 << 16));
    // 0x1c7bf4: 0xac83000c  sw          $v1, 0xC($a0)
    ctx->pc = 0x1c7bf4u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 12), GPR_U32(ctx, 3));
    // 0x1c7bf8: 0x34c602a9  ori         $a2, $a2, 0x2A9
    ctx->pc = 0x1c7bf8u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | (uint64_t)(uint16_t)681);
    // 0x1c7bfc: 0x40482d  daddu       $t1, $v0, $zero
    ctx->pc = 0x1c7bfcu;
    SET_GPR_U64(ctx, 9, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c00: 0xac870000  sw          $a3, 0x0($a0)
    ctx->pc = 0x1c7c00u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 7));
    // 0x1c7c04: 0x44021000  mfc1        $v0, $f2
    ctx->pc = 0x1c7c04u;
    { uint32_t bits; std::memcpy(&bits, &ctx->f[2], sizeof(bits)); SET_GPR_U32(ctx, 2, bits); }
    // 0x1c7c08: 0x502d  daddu       $t2, $zero, $zero
    ctx->pc = 0x1c7c08u;
    SET_GPR_U64(ctx, 10, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c0c: 0xac880004  sw          $t0, 0x4($a0)
    ctx->pc = 0x1c7c0cu;
    WRITE32(ADD32(GPR_U32(ctx, 4), 4), GPR_U32(ctx, 8));
    // 0x1c7c10: 0xac850008  sw          $a1, 0x8($a0)
    ctx->pc = 0x1c7c10u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 8), GPR_U32(ctx, 5));
    // 0x1c7c14: 0x40382d  daddu       $a3, $v0, $zero
    ctx->pc = 0x1c7c14u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c18: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x1c7c18u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x1c7c1c: 0x44811800  mtc1        $at, $f3
    ctx->pc = 0x1c7c1cu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[3], &bits, sizeof(bits)); }
    // 0x1c7c20: 0x3c050044  lui         $a1, 0x44
    ctx->pc = 0x1c7c20u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)68 << 16));
    // 0x1c7c24: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7c24u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7c28: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7c28u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7c2c: 0x240807c5  addiu       $t0, $zero, 0x7C5
    ctx->pc = 0x1c7c2cu;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 0), 1989));
    // 0x1c7c30: 0x26640028  addiu       $a0, $s3, 0x28
    ctx->pc = 0x1c7c30u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 19), 40));
    // 0x1c7c34: 0xa0f02d  daddu       $fp, $a1, $zero
    ctx->pc = 0x1c7c34u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c38: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c7c38u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c3c: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x1c7c3cu;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c40: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7c40u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7c44: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7c44u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7c48: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7c48u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7c4c: 0xac66000c  sw          $a2, 0xC($v1)
    ctx->pc = 0x1c7c4cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 6));
    // $v1 is this call's light block header; the capture reads from it.
    g_ghpcEnvironHeader = GPR_U32(ctx, 3);
    // 0x1c7c50: 0xac690000  sw          $t1, 0x0($v1)
    ctx->pc = 0x1c7c50u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 9));
    // 0x1c7c54: 0xac670004  sw          $a3, 0x4($v1)
    ctx->pc = 0x1c7c54u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 7));
    // 0x1c7c58: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c7c58u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x1c7c5c: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c7c5cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7c60: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c7c60u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c7c64: 0xc6620040  lwc1        $f2, 0x40($s3)
    ctx->pc = 0x1c7c64u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 64)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c7c68: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c7c68u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7c6c: 0xc6600044  lwc1        $f0, 0x44($s3)
    ctx->pc = 0x1c7c6cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 68)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7c70: 0xc6610048  lwc1        $f1, 0x48($s3)
    ctx->pc = 0x1c7c70u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 19), 72)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7c74: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7c74u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7c78: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7c78u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7c7c: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c7c7cu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c7c80: 0xe463000c  swc1        $f3, 0xC($v1)
    ctx->pc = 0x1c7c80u;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 12), bits); }
    // 0x1c7c84: 0xe4620000  swc1        $f2, 0x0($v1)
    ctx->pc = 0x1c7c84u;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 0), bits); }
    // 0x1c7c88: 0xe4600004  swc1        $f0, 0x4($v1)
    ctx->pc = 0x1c7c88u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4), bits); }
    // 0x1c7c8c: 0xe4610008  swc1        $f1, 0x8($v1)
    ctx->pc = 0x1c7c8cu;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 8), bits); }
    // 0x1c7c90: 0xafa00070  sw          $zero, 0x70($sp)
    ctx->pc = 0x1c7c90u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 0));
    // 0x1c7c94: 0xaca80e24  sw          $t0, 0xE24($a1)
    ctx->pc = 0x1c7c94u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 8)); ps2TraceGuestWrite(rdram, 0x440E24u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x440E24u, _value); } while (0);
    // 0x1c7c98: 0x8c820008  lw          $v0, 0x8($a0)
    ctx->pc = 0x1c7c98u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));
    // 0x1c7c9c: 0x1040000a  beqz        $v0, . + 4 + (0xA << 2)
    ctx->pc = 0x1C7C9Cu;
    {
        const bool branch_taken_0x1c7c9c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7CA0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7C9Cu;
        // 0x1c7ca0: 0xafa20070  sw          $v0, 0x70($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7c9c) {
            ctx->pc = 0x1C7CC8u;
            goto label_1c7cc8;
        }
    }
    ctx->pc = 0x1C7CA4u;
    // 0x1c7ca4: 0x0  nop
    ctx->pc = 0x1c7ca4u;
    // NOP
label_1c7ca8:
    // 0x1c7ca8: 0x8fa40070  lw          $a0, 0x70($sp)
    ctx->pc = 0x1c7ca8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7cac: 0x8c820000  lw          $v0, 0x0($a0)
    ctx->pc = 0x1c7cacu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x1c7cb0: 0x8c4300d4  lw          $v1, 0xD4($v0)
    ctx->pc = 0x1c7cb0u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 212)));
    // 0x1c7cb4: 0x5060ff7b  beql        $v1, $zero, . + 4 + (-0x85 << 2)
    ctx->pc = 0x1C7CB4u;
    {
        const bool branch_taken_0x1c7cb4 = (GPR_U64(ctx, 3) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c7cb4) {
            ctx->pc = 0x1C7CB8u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C7CB4u;
            // 0x1c7cb8: 0x24030436  addiu       $v1, $zero, 0x436 (Delay Slot)
            SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 1078));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C7AA4u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7aa4;
        }
    }
    ctx->pc = 0x1C7CBCu;
    // 0x1c7cbc: 0x8c820004  lw          $v0, 0x4($a0)
    ctx->pc = 0x1c7cbcu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 4)));
    // 0x1c7cc0: 0x144afff9  bne         $v0, $t2, . + 4 + (-0x7 << 2)
    ctx->pc = 0x1C7CC0u;
    {
        const bool branch_taken_0x1c7cc0 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 10));
        ctx->pc = 0x1C7CC4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7CC0u;
        // 0x1c7cc4: 0xafa20070  sw          $v0, 0x70($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7cc0) {
            ctx->pc = 0x1C7CA8u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7ca8;
        }
    }
    ctx->pc = 0x1C7CC8u;
label_1c7cc8:
    // 0x1c7cc8: 0x8fa20070  lw          $v0, 0x70($sp)
    ctx->pc = 0x1c7cc8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7ccc: 0x1440007a  bnez        $v0, . + 4 + (0x7A << 2)
    ctx->pc = 0x1C7CCCu;
    {
        const bool branch_taken_0x1c7ccc = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C7CD0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7CCCu;
        // 0x1c7cd0: 0x902d  daddu       $s2, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7ccc) {
            ctx->pc = 0x1C7EB8u;
            goto label_1c7eb8;
        }
    }
    ctx->pc = 0x1C7CD4u;
    // 0x1c7cd4: 0x24020002  addiu       $v0, $zero, 0x2
    ctx->pc = 0x1c7cd4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
    // 0x1c7cd8: 0x2403ffff  addiu       $v1, $zero, -0x1
    ctx->pc = 0x1c7cd8u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x1c7cdc: 0x2442ffff  addiu       $v0, $v0, -0x1
    ctx->pc = 0x1c7cdcu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
label_1c7ce0:
    // 0x1c7ce0: 0x0  nop
    ctx->pc = 0x1c7ce0u;
    // NOP
    // 0x1c7ce4: 0x0  nop
    ctx->pc = 0x1c7ce4u;
    // NOP
    // 0x1c7ce8: 0x0  nop
    ctx->pc = 0x1c7ce8u;
    // NOP
    // 0x1c7cec: 0x0  nop
    ctx->pc = 0x1c7cecu;
    // NOP
    // 0x1c7cf0: 0x1443fffb  bne         $v0, $v1, . + 4 + (-0x5 << 2)
    ctx->pc = 0x1C7CF0u;
    {
        const bool branch_taken_0x1c7cf0 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 3));
        ctx->pc = 0x1C7CF4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7CF0u;
        // 0x1c7cf4: 0x2442ffff  addiu       $v0, $v0, -0x1 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7cf0) {
            ctx->pc = 0x1C7CE0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7ce0;
        }
    }
    ctx->pc = 0x1C7CF8u;
    // 0x1c7cf8: 0x24020002  addiu       $v0, $zero, 0x2
    ctx->pc = 0x1c7cf8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
    // 0x1c7cfc: 0x2403ffff  addiu       $v1, $zero, -0x1
    ctx->pc = 0x1c7cfcu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x1c7d00: 0x2442ffff  addiu       $v0, $v0, -0x1
    ctx->pc = 0x1c7d00u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
    // 0x1c7d04: 0x0  nop
    ctx->pc = 0x1c7d04u;
    // NOP
label_1c7d08:
    // 0x1c7d08: 0x0  nop
    ctx->pc = 0x1c7d08u;
    // NOP
    // 0x1c7d0c: 0x0  nop
    ctx->pc = 0x1c7d0cu;
    // NOP
    // 0x1c7d10: 0x0  nop
    ctx->pc = 0x1c7d10u;
    // NOP
    // 0x1c7d14: 0x0  nop
    ctx->pc = 0x1c7d14u;
    // NOP
    // 0x1c7d18: 0x1443fffb  bne         $v0, $v1, . + 4 + (-0x5 << 2)
    ctx->pc = 0x1C7D18u;
    {
        const bool branch_taken_0x1c7d18 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 3));
        ctx->pc = 0x1C7D1Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7D18u;
        // 0x1c7d1c: 0x2442ffff  addiu       $v0, $v0, -0x1 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7d18) {
            ctx->pc = 0x1C7D08u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7d08;
        }
    }
    ctx->pc = 0x1C7D20u;
    // 0x1c7d20: 0x8e040008  lw          $a0, 0x8($s0)
    ctx->pc = 0x1c7d20u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 8)));
    // 0x1c7d24: 0x122900  sll         $a1, $s2, 4
    ctx->pc = 0x1c7d24u;
    SET_GPR_S32(ctx, 5, (int32_t)SLL32(GPR_U32(ctx, 18), 4));
    // 0x1c7d28: 0x27a30050  addiu       $v1, $sp, 0x50
    ctx->pc = 0x1c7d28u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 29), 80));
    // 0x1c7d2c: 0x121080  sll         $v0, $s2, 2
    ctx->pc = 0x1c7d2cu;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 18), 2));
    // 0x1c7d30: 0x628021  addu        $s0, $v1, $v0
    ctx->pc = 0x1c7d30u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 2)));
    // 0x1c7d34: 0xafa40070  sw          $a0, 0x70($sp)
    ctx->pc = 0x1c7d34u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 4));
    // 0x1c7d38: 0xbd8821  addu        $s1, $a1, $sp
    ctx->pc = 0x1c7d38u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 29)));
    // 0x1c7d3c: 0x24140001  addiu       $s4, $zero, 0x1
    ctx->pc = 0x1c7d3cu;
    SET_GPR_S32(ctx, 20, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c7d40: 0x24170002  addiu       $s7, $zero, 0x2
    ctx->pc = 0x1c7d40u;
    SET_GPR_S32(ctx, 23, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
    // 0x1c7d44: 0x241606ec  addiu       $s6, $zero, 0x6EC
    ctx->pc = 0x1c7d44u;
    SET_GPR_S32(ctx, 22, (int32_t)ADD32(GPR_U32(ctx, 0), 1772));
    // 0x1c7d48: 0x10000024  b           . + 4 + (0x24 << 2)
    ctx->pc = 0x1C7D48u;
    {
        const bool branch_taken_0x1c7d48 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7D4Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7D48u;
        // 0x1c7d4c: 0x27b50060  addiu       $s5, $sp, 0x60 (Delay Slot)
        SET_GPR_S32(ctx, 21, (int32_t)ADD32(GPR_U32(ctx, 29), 96));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7d48) {
            ctx->pc = 0x1C7DDCu;
            goto label_1c7ddc;
        }
    }
    ctx->pc = 0x1C7D50u;
label_1c7d50:
    // 0x1c7d50: 0x8c4300d4  lw          $v1, 0xD4($v0)
    ctx->pc = 0x1c7d50u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 212)));
    // 0x1c7d54: 0x1474001f  bne         $v1, $s4, . + 4 + (0x1F << 2)
    ctx->pc = 0x1C7D54u;
    {
        const bool branch_taken_0x1c7d54 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 20));
        ctx->pc = 0x1C7D58u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7D54u;
        // 0x1c7d58: 0x8fa20070  lw          $v0, 0x70($sp) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7d54) {
            ctx->pc = 0x1C7DD4u;
            goto label_1c7dd4;
        }
    }
    ctx->pc = 0x1C7D5Cu;
    // 0x1c7d5c: 0xafd60e24  sw          $s6, 0xE24($fp)
    ctx->pc = 0x1c7d5cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 3620), GPR_U32(ctx, 22));
    // 0x1c7d60: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x1C7D60u;
    SET_GPR_U32(ctx, 31, 0x1C7D68u);
    ctx->pc = 0x1C7D64u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7D60u;
    // 0x1c7d64: 0x8c840000  lw          $a0, 0x0($a0) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x1C7D60u, 0x1C7D68u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7D68u;
label_1c7d68:
    // 0x1c7d68: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c7d68u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c7d6c: 0xd8440000  lqc2        $vf4, 0x0($v0)
    ctx->pc = 0x1c7d6cu;
    ctx->vu0_vf[4] = _mm_castsi128_ps(READ128(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x1c7d70: 0x4bc4216a  vmul.xyz    $vf5, $vf4, $vf4
    ctx->pc = 0x1c7d70u;
    { __m128 res = PS2_VMUL(ctx->vu0_vf[4], ctx->vu0_vf[4]); __m128i mask = _mm_set_epi32(0, -1, -1, -1); ctx->vu0_vf[5] = PS2_VBLEND(ctx->vu0_vf[5], res, _mm_castsi128_ps(mask)); }
    // 0x1c7d74: 0x4b052941  vaddy.x     $vf5, $vf5, $vf5y
    ctx->pc = 0x1c7d74u;
    { __m128 res = PS2_VADD(ctx->vu0_vf[5], _mm_shuffle_ps(ctx->vu0_vf[5], ctx->vu0_vf[5], _MM_SHUFFLE(1,1,1,1))); __m128i mask = _mm_set_epi32(0, 0, 0, -1); ctx->vu0_vf[5] = _mm_blendv_ps(ctx->vu0_vf[5], res, _mm_castsi128_ps(mask)); }
    // 0x1c7d78: 0x4b052942  vaddz.x     $vf5, $vf5, $vf5z
    ctx->pc = 0x1c7d78u;
    { __m128 res = PS2_VADD(ctx->vu0_vf[5], _mm_shuffle_ps(ctx->vu0_vf[5], ctx->vu0_vf[5], _MM_SHUFFLE(2,2,2,2))); __m128i mask = _mm_set_epi32(0, 0, 0, -1); ctx->vu0_vf[5] = _mm_blendv_ps(ctx->vu0_vf[5], res, _mm_castsi128_ps(mask)); }
    // 0x1c7d7c: 0x4a6503be  vrsqrt      $Q, $vf0w, $vf5x
    ctx->pc = 0x1c7d7cu;
    { float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[5], ctx->vu0_vf[5], _MM_SHUFFLE(0,0,0,0))); ctx->vu0_q = (ft > 0.0f) ? (1.0f / sqrtf(ft)) : 0.0f; }
    // 0x1c7d80: 0x4a0003bf  vwaitq
    ctx->pc = 0x1c7d80u;
    // VWAITQ (Q already resolved in this runtime)
    // 0x1c7d84: 0x4bc0211c  vmulq.xyz   $vf4, $vf4, $Q
    ctx->pc = 0x1c7d84u;
    { __m128 res = PS2_VMUL(ctx->vu0_vf[4], _mm_set1_ps(ctx->vu0_q)); __m128i mask = _mm_set_epi32(0, -1, -1, -1); ctx->vu0_vf[4] = _mm_blendv_ps(ctx->vu0_vf[4], res, _mm_castsi128_ps(mask)); }
    // 0x1c7d88: 0xfaa40000  sqc2        $vf4, 0x0($s5)
    ctx->pc = 0x1c7d88u;
    WRITE128(ADD32(GPR_U32(ctx, 21), 0), _mm_castps_si128(ctx->vu0_vf[4]));
    // 0x1c7d8c: 0xc7a10060  lwc1        $f1, 0x60($sp)
    ctx->pc = 0x1c7d8cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 96)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7d90: 0x26520001  addiu       $s2, $s2, 0x1
    ctx->pc = 0x1c7d90u;
    SET_GPR_S32(ctx, 18, (int32_t)ADD32(GPR_U32(ctx, 18), 1));
    // 0x1c7d94: 0x46000847  neg.s       $f1, $f1
    ctx->pc = 0x1c7d94u;
    ctx->f[1] = FPU_NEG_S(ctx->f[1]);
    // 0x1c7d98: 0xe601ffe0  swc1        $f1, -0x20($s0)
    ctx->pc = 0x1c7d98u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 16), 4294967264), bits); }
    // 0x1c7d9c: 0xc7a00064  lwc1        $f0, 0x64($sp)
    ctx->pc = 0x1c7d9cu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 100)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c7da0: 0x46000007  neg.s       $f0, $f0
    ctx->pc = 0x1c7da0u;
    ctx->f[0] = FPU_NEG_S(ctx->f[0]);
    // 0x1c7da4: 0xe600fff0  swc1        $f0, -0x10($s0)
    ctx->pc = 0x1c7da4u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 16), 4294967280), bits); }
    // 0x1c7da8: 0xc7a10068  lwc1        $f1, 0x68($sp)
    ctx->pc = 0x1c7da8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 104)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c7dac: 0x46000847  neg.s       $f1, $f1
    ctx->pc = 0x1c7dacu;
    ctx->f[1] = FPU_NEG_S(ctx->f[1]);
    // 0x1c7db0: 0xe6010000  swc1        $f1, 0x0($s0)
    ctx->pc = 0x1c7db0u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 16), 0), bits); }
    // 0x1c7db4: 0x26100004  addiu       $s0, $s0, 0x4
    ctx->pc = 0x1c7db4u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 4));
    // 0x1c7db8: 0x8fa30070  lw          $v1, 0x70($sp)
    ctx->pc = 0x1c7db8u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7dbc: 0x8c640000  lw          $a0, 0x0($v1)
    ctx->pc = 0x1c7dbcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x1c7dc0: 0x788200c0  lq          $v0, 0xC0($a0)
    ctx->pc = 0x1c7dc0u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 4), 192)));
    // 0x1c7dc4: 0x7e220000  sq          $v0, 0x0($s1)
    ctx->pc = 0x1c7dc4u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 0), GPR_VEC(ctx, 2));
    // 0x1c7dc8: 0xae20000c  sw          $zero, 0xC($s1)
    ctx->pc = 0x1c7dc8u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 12), GPR_U32(ctx, 0));
    // 0x1c7dcc: 0x26310010  addiu       $s1, $s1, 0x10
    ctx->pc = 0x1c7dccu;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 17), 16));
    // 0x1c7dd0: 0x8fa20070  lw          $v0, 0x70($sp)
    ctx->pc = 0x1c7dd0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
label_1c7dd4:
    // 0x1c7dd4: 0x8c430004  lw          $v1, 0x4($v0)
    ctx->pc = 0x1c7dd4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 4)));
    // 0x1c7dd8: 0xafa30070  sw          $v1, 0x70($sp)
    ctx->pc = 0x1c7dd8u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 3));
label_1c7ddc:
    // 0x1c7ddc: 0x8fa40070  lw          $a0, 0x70($sp)
    ctx->pc = 0x1c7ddcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1c7de0: 0x10800004  beqz        $a0, . + 4 + (0x4 << 2)
    ctx->pc = 0x1C7DE0u;
    {
        const bool branch_taken_0x1c7de0 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7DE4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7DE0u;
        // 0x1c7de4: 0x182d  daddu       $v1, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7de0) {
            ctx->pc = 0x1C7DF4u;
            goto label_1c7df4;
        }
    }
    ctx->pc = 0x1C7DE8u;
    // 0x1c7de8: 0x2f2102a  slt         $v0, $s7, $s2
    ctx->pc = 0x1c7de8u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 23) < (int64_t)GPR_S64(ctx, 18)) ? 1 : 0);
    // 0x1c7dec: 0x280182d  daddu       $v1, $s4, $zero
    ctx->pc = 0x1c7decu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 20) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c7df0: 0x2180b  movn        $v1, $zero, $v0
    ctx->pc = 0x1c7df0u;
    if (GPR_U64(ctx, 2) != 0) SET_GPR_VEC(ctx, 3, GPR_VEC(ctx, 0));
label_1c7df4:
    // 0x1c7df4: 0x5460ffd6  bnel        $v1, $zero, . + 4 + (-0x2A << 2)
    ctx->pc = 0x1C7DF4u;
    {
        const bool branch_taken_0x1c7df4 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c7df4) {
            ctx->pc = 0x1C7DF8u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C7DF4u;
            // 0x1c7df8: 0x8c820000  lw          $v0, 0x0($a0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C7D50u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7d50;
        }
    }
    ctx->pc = 0x1C7DFCu;
    // 0x1c7dfc: 0x8fc30e24  lw          $v1, 0xE24($fp)
    ctx->pc = 0x1c7dfcu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 3620)));
    // 0x1c7e00: 0x240206ec  addiu       $v0, $zero, 0x6EC
    ctx->pc = 0x1c7e00u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1772));
    // 0x1c7e04: 0x1462000c  bne         $v1, $v0, . + 4 + (0xC << 2)
    ctx->pc = 0x1C7E04u;
    {
        const bool branch_taken_0x1c7e04 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 2));
        ctx->pc = 0x1C7E08u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7E04u;
        // 0x1c7e08: 0x2a420003  slti        $v0, $s2, 0x3 (Delay Slot)
        SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 18) < (int64_t)(int32_t)3) ? 1 : 0);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7e04) {
            ctx->pc = 0x1C7E38u;
            goto label_1c7e38;
        }
    }
    ctx->pc = 0x1C7E0Cu;
    // 0x1c7e0c: 0x1040000a  beqz        $v0, . + 4 + (0xA << 2)
    ctx->pc = 0x1C7E0Cu;
    {
        const bool branch_taken_0x1c7e0c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C7E10u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7E0Cu;
        // 0x1c7e10: 0x121100  sll         $v0, $s2, 4 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 18), 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7e0c) {
            ctx->pc = 0x1C7E38u;
            goto label_1c7e38;
        }
    }
    ctx->pc = 0x1C7E14u;
    // 0x1c7e14: 0x5d1821  addu        $v1, $v0, $sp
    ctx->pc = 0x1c7e14u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 29)));
label_1c7e18:
    // 0x1c7e18: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c7e18u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c7e1c: 0x26520001  addiu       $s2, $s2, 0x1
    ctx->pc = 0x1c7e1cu;
    SET_GPR_S32(ctx, 18, (int32_t)ADD32(GPR_U32(ctx, 18), 1));
    // 0x1c7e20: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c7e20u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x1c7e24: 0x2a420003  slti        $v0, $s2, 0x3
    ctx->pc = 0x1c7e24u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 18) < (int64_t)(int32_t)3) ? 1 : 0);
    // 0x1c7e28: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c7e28u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x1c7e2c: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x1c7e2cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x1c7e30: 0x1440fff9  bnez        $v0, . + 4 + (-0x7 << 2)
    ctx->pc = 0x1C7E30u;
    {
        const bool branch_taken_0x1c7e30 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C7E34u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7E30u;
        // 0x1c7e34: 0x24630010  addiu       $v1, $v1, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c7e30) {
            ctx->pc = 0x1C7E18u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c7e18;
        }
    }
    ctx->pc = 0x1C7E38u;
label_1c7e38:
    // 0x1c7e38: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c7e38u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7e3c: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c7e3cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x1c7e40: 0x7ba20000  lq          $v0, 0x0($sp)
    ctx->pc = 0x1c7e40u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x1c7e44: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c7e44u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x1c7e48: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c7e48u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x1c7e4c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7e4cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7e50: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c7e50u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x1c7e54: 0x24640010  addiu       $a0, $v1, 0x10
    ctx->pc = 0x1c7e54u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x1c7e58: 0x24650020  addiu       $a1, $v1, 0x20
    ctx->pc = 0x1c7e58u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 32));
    // 0x1c7e5c: 0x24660030  addiu       $a2, $v1, 0x30
    ctx->pc = 0x1c7e5cu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 3), 48));
    // 0x1c7e60: 0x7ba20010  lq          $v0, 0x10($sp)
    ctx->pc = 0x1c7e60u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x1c7e64: 0x24670040  addiu       $a3, $v1, 0x40
    ctx->pc = 0x1c7e64u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 3), 64));
    // 0x1c7e68: 0x24680050  addiu       $t0, $v1, 0x50
    ctx->pc = 0x1c7e68u;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 3), 80));
    // 0x1c7e6c: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c7e6cu;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x1c7e70: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7e70u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7e74: 0xac240008  sw          $a0, 0x8($at)
    ctx->pc = 0x1c7e74u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 4)); // MMIO: 0x70000008
    // 0x1c7e78: 0x7ba20020  lq          $v0, 0x20($sp)
    ctx->pc = 0x1c7e78u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x1c7e7c: 0x7c620010  sq          $v0, 0x10($v1)
    ctx->pc = 0x1c7e7cu;
    WRITE128(ADD32(GPR_U32(ctx, 3), 16), GPR_VEC(ctx, 2));
    // 0x1c7e80: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7e80u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7e84: 0xac250008  sw          $a1, 0x8($at)
    ctx->pc = 0x1c7e84u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 5)); // MMIO: 0x70000008
    // 0x1c7e88: 0x7ba20030  lq          $v0, 0x30($sp)
    ctx->pc = 0x1c7e88u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x1c7e8c: 0x7c620020  sq          $v0, 0x20($v1)
    ctx->pc = 0x1c7e8cu;
    WRITE128(ADD32(GPR_U32(ctx, 3), 32), GPR_VEC(ctx, 2));
    // 0x1c7e90: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7e90u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7e94: 0xac260008  sw          $a2, 0x8($at)
    ctx->pc = 0x1c7e94u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 6)); // MMIO: 0x70000008
    // 0x1c7e98: 0x7ba20040  lq          $v0, 0x40($sp)
    ctx->pc = 0x1c7e98u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x1c7e9c: 0x7c620030  sq          $v0, 0x30($v1)
    ctx->pc = 0x1c7e9cu;
    WRITE128(ADD32(GPR_U32(ctx, 3), 48), GPR_VEC(ctx, 2));
    // 0x1c7ea0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7ea0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7ea4: 0xac270008  sw          $a3, 0x8($at)
    ctx->pc = 0x1c7ea4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 7)); // MMIO: 0x70000008
    // 0x1c7ea8: 0x7ba20050  lq          $v0, 0x50($sp)
    ctx->pc = 0x1c7ea8u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x1c7eac: 0x7c620040  sq          $v0, 0x40($v1)
    ctx->pc = 0x1c7eacu;
    WRITE128(ADD32(GPR_U32(ctx, 3), 64), GPR_VEC(ctx, 2));
    // 0x1c7eb0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c7eb0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c7eb4: 0xac280008  sw          $t0, 0x8($at)
    ctx->pc = 0x1c7eb4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 8)); // MMIO: 0x70000008
label_1c7eb8:
    // 0x1c7eb8: 0xc077404  jal         func_1DD010
    ctx->pc = 0x1C7EB8u;
    SET_GPR_U32(ctx, 31, 0x1C7EC0u);
    ctx->pc = 0x1C7EBCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7EB8u;
    // 0x1c7ebc: 0x260202d  daddu       $a0, $s3, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1DD010u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1DD010u, 0x1C7EB8u, 0x1C7EC0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7EC0u;
label_1c7ec0:
    // 0x1c7ec0: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x1c7ec0u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x1c7ec4: 0xc10f900  jal         func_43E400
    ctx->pc = 0x1C7EC4u;
    SET_GPR_U32(ctx, 31, 0x1C7ECCu);
    ctx->pc = 0x1C7EC8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C7EC4u;
    // 0x1c7ec8: 0x24442780  addiu       $a0, $v0, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E400u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E400u, 0x1C7EC4u, 0x1C7ECCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C7ECCu;
label_1c7ecc:
    // 0x1c7ecc: 0x7bb00110  lq          $s0, 0x110($sp)
    ctx->pc = 0x1c7eccu;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 272)));
    // 0x1c7ed0: 0x7bb10100  lq          $s1, 0x100($sp)
    ctx->pc = 0x1c7ed0u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 256)));
    // 0x1c7ed4: 0x7bb200f0  lq          $s2, 0xF0($sp)
    ctx->pc = 0x1c7ed4u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 240)));
    // 0x1c7ed8: 0x7bb300e0  lq          $s3, 0xE0($sp)
    ctx->pc = 0x1c7ed8u;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 224)));
    // 0x1c7edc: 0x7bb400d0  lq          $s4, 0xD0($sp)
    ctx->pc = 0x1c7edcu;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 208)));
    // 0x1c7ee0: 0x7bb500c0  lq          $s5, 0xC0($sp)
    ctx->pc = 0x1c7ee0u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 192)));
    // 0x1c7ee4: 0x7bb600b0  lq          $s6, 0xB0($sp)
    ctx->pc = 0x1c7ee4u;
    SET_GPR_VEC(ctx, 22, READ128(ADD32(GPR_U32(ctx, 29), 176)));
    // 0x1c7ee8: 0x7bb700a0  lq          $s7, 0xA0($sp)
    ctx->pc = 0x1c7ee8u;
    SET_GPR_VEC(ctx, 23, READ128(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x1c7eec: 0x7bbe0090  lq          $fp, 0x90($sp)
    ctx->pc = 0x1c7eecu;
    SET_GPR_VEC(ctx, 30, READ128(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x1c7ef0: 0xdfbf0080  ld          $ra, 0x80($sp)
    ctx->pc = 0x1c7ef0u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x1c7ef4: 0xc7b40120  lwc1        $f20, 0x120($sp)
    ctx->pc = 0x1c7ef4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 288)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[20] = f; }
    // The packet is complete here and the program global has its final value.
    ghpcEnvironCapture(rdram, ctx, runtime);
    // 0x1c7ef8: 0x3e00008  jr          $ra
    ctx->pc = 0x1C7EF8u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C7EFCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C7EF8u;
        // 0x1c7efc: 0x27bd0130  addiu       $sp, $sp, 0x130 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 304));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C7EF8u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C7F00u;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&Select__9PsEnviron_0x1c79c8), PS2Runtime::RecompiledFunction>::value,
              "Select__9PsEnviron_0x1c79c8 override no longer matches the recompiled signature");
