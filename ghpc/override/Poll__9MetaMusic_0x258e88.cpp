// Override of work/output/Poll__9MetaMusic_0x258e88.cpp (MetaMusic::Poll,
// 0x258e88 - 0x2591bc).
//
// Why: the menu music came in 6.3 s late on every boot. MetaMusic fades its
// stream with a LinearInterpolator (this+0x80) keyed on TaskMgr::UISeconds,
// set up in Poll at 0x2590bc as Reset(-96 dB, target, ui*1000, ui*1000 +
// 1000/rate). UIManager::Poll zeroes UISeconds on every screen transition
// (SetUISeconds(0) at 0x24c7cc). If the fade starts before a transition, the
// interpolator's start time is left in the future and the volume clamps at
// -96 dB until the UI clock climbs back to it.
//
// How much it climbs back is the UI time spent before the first screen,
// which is App::App's boot splashes (0x453B8000, 3000 ms each, two of them
// after UIManager's timer starts). The fade lands on the wrong side of the
// transition because the port's disc reads are instant: ReadyToPlay only
// waits for 200 KB of the music file, which a real drive would not have
// delivered that fast.
//
// The hook rebases the interpolator by however far the UI clock jumped back,
// so a fade in progress carries on from where it was. The generated body is
// otherwise verbatim.
#include <stdexcept>
#include <cstring>
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
constexpr uint32_t kTheTaskMgr = 0x5A5870u;

float ghpcReadF32(uint8_t* rdram, uint32_t addr) {
    const uint32_t bits = FAST_READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void ghpcWriteF32(uint8_t* rdram, uint32_t addr, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    FAST_WRITE32(addr, bits);
}

// UISeconds__C7TaskMgr 0x316448: lw $v0, 0x28($a0); lwc1 $f0, 0x34($v0).
// LinearInterpolator, from Reset 0x32d410: +0 y0, +4 y1, +8 x0, +0xC x1,
// +0x14 slope, +0x18 intercept, value = slope * x + intercept.
void ghpcRebaseMetaMusicFade(uint8_t* rdram, uint32_t self) {
    static float s_lastUi = 0.0f;
    const uint32_t clock = FAST_READ32(kTheTaskMgr + 0x28u);
    if (clock == 0u) return;
    const float ui = ghpcReadF32(rdram, clock + 0x34u);
    const float drop = s_lastUi - ui;
    s_lastUi = ui;
    // +0x30 is set while fading in, +0x34 while ramping down.
    if (drop <= 0.0f || (FAST_READ32(self + 0x30u) == 0u && FAST_READ32(self + 0x34u) == 0u)) return;
    const uint32_t li = self + 0x80u;
    const float d = -drop * 1000.0f;
    ghpcWriteF32(rdram, li + 0x08u, ghpcReadF32(rdram, li + 0x08u) + d);
    ghpcWriteF32(rdram, li + 0x0Cu, ghpcReadF32(rdram, li + 0x0Cu) + d);
    ghpcWriteF32(rdram, li + 0x18u,
                 ghpcReadF32(rdram, li + 0x18u) - ghpcReadF32(rdram, li + 0x14u) * d);
}
}

// Function: Poll__9MetaMusic
// Address: 0x258e88 - 0x2591bc
void Poll__9MetaMusic_0x258e88(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Poll__9MetaMusic_0x258e88");
#endif

    switch (ctx->pc) {
        case 0x258e88u: goto label_258e88;
        case 0x258e8cu: goto label_258e8c;
        case 0x258e90u: goto label_258e90;
        case 0x258e94u: goto label_258e94;
        case 0x258e98u: goto label_258e98;
        case 0x258e9cu: goto label_258e9c;
        case 0x258ea0u: goto label_258ea0;
        case 0x258ea4u: goto label_258ea4;
        case 0x258ea8u: goto label_258ea8;
        case 0x258eacu: goto label_258eac;
        case 0x258eb0u: goto label_258eb0;
        case 0x258eb4u: goto label_258eb4;
        case 0x258eb8u: goto label_258eb8;
        case 0x258ebcu: goto label_258ebc;
        case 0x258ec0u: goto label_258ec0;
        case 0x258ec4u: goto label_258ec4;
        case 0x258ec8u: goto label_258ec8;
        case 0x258eccu: goto label_258ecc;
        case 0x258ed0u: goto label_258ed0;
        case 0x258ed4u: goto label_258ed4;
        case 0x258ed8u: goto label_258ed8;
        case 0x258edcu: goto label_258edc;
        case 0x258ee0u: goto label_258ee0;
        case 0x258ee4u: goto label_258ee4;
        case 0x258ee8u: goto label_258ee8;
        case 0x258eecu: goto label_258eec;
        case 0x258ef0u: goto label_258ef0;
        case 0x258ef4u: goto label_258ef4;
        case 0x258ef8u: goto label_258ef8;
        case 0x258efcu: goto label_258efc;
        case 0x258f00u: goto label_258f00;
        case 0x258f04u: goto label_258f04;
        case 0x258f08u: goto label_258f08;
        case 0x258f0cu: goto label_258f0c;
        case 0x258f10u: goto label_258f10;
        case 0x258f14u: goto label_258f14;
        case 0x258f18u: goto label_258f18;
        case 0x258f1cu: goto label_258f1c;
        case 0x258f20u: goto label_258f20;
        case 0x258f24u: goto label_258f24;
        case 0x258f28u: goto label_258f28;
        case 0x258f2cu: goto label_258f2c;
        case 0x258f30u: goto label_258f30;
        case 0x258f34u: goto label_258f34;
        case 0x258f38u: goto label_258f38;
        case 0x258f3cu: goto label_258f3c;
        case 0x258f40u: goto label_258f40;
        case 0x258f44u: goto label_258f44;
        case 0x258f48u: goto label_258f48;
        case 0x258f4cu: goto label_258f4c;
        case 0x258f50u: goto label_258f50;
        case 0x258f54u: goto label_258f54;
        case 0x258f58u: goto label_258f58;
        case 0x258f5cu: goto label_258f5c;
        case 0x258f60u: goto label_258f60;
        case 0x258f64u: goto label_258f64;
        case 0x258f68u: goto label_258f68;
        case 0x258f6cu: goto label_258f6c;
        case 0x258f70u: goto label_258f70;
        case 0x258f74u: goto label_258f74;
        case 0x258f78u: goto label_258f78;
        case 0x258f7cu: goto label_258f7c;
        case 0x258f80u: goto label_258f80;
        case 0x258f84u: goto label_258f84;
        case 0x258f88u: goto label_258f88;
        case 0x258f8cu: goto label_258f8c;
        case 0x258f90u: goto label_258f90;
        case 0x258f94u: goto label_258f94;
        case 0x258f98u: goto label_258f98;
        case 0x258f9cu: goto label_258f9c;
        case 0x258fa0u: goto label_258fa0;
        case 0x258fa4u: goto label_258fa4;
        case 0x258fa8u: goto label_258fa8;
        case 0x258facu: goto label_258fac;
        case 0x258fb0u: goto label_258fb0;
        case 0x258fb4u: goto label_258fb4;
        case 0x258fb8u: goto label_258fb8;
        case 0x258fbcu: goto label_258fbc;
        case 0x258fc0u: goto label_258fc0;
        case 0x258fc4u: goto label_258fc4;
        case 0x258fc8u: goto label_258fc8;
        case 0x258fccu: goto label_258fcc;
        case 0x258fd0u: goto label_258fd0;
        case 0x258fd4u: goto label_258fd4;
        case 0x258fd8u: goto label_258fd8;
        case 0x258fdcu: goto label_258fdc;
        case 0x258fe0u: goto label_258fe0;
        case 0x258fe4u: goto label_258fe4;
        case 0x258fe8u: goto label_258fe8;
        case 0x258fecu: goto label_258fec;
        case 0x258ff0u: goto label_258ff0;
        case 0x258ff4u: goto label_258ff4;
        case 0x258ff8u: goto label_258ff8;
        case 0x258ffcu: goto label_258ffc;
        case 0x259000u: goto label_259000;
        case 0x259004u: goto label_259004;
        case 0x259008u: goto label_259008;
        case 0x25900cu: goto label_25900c;
        case 0x259010u: goto label_259010;
        case 0x259014u: goto label_259014;
        case 0x259018u: goto label_259018;
        case 0x25901cu: goto label_25901c;
        case 0x259020u: goto label_259020;
        case 0x259024u: goto label_259024;
        case 0x259028u: goto label_259028;
        case 0x25902cu: goto label_25902c;
        case 0x259030u: goto label_259030;
        case 0x259034u: goto label_259034;
        case 0x259038u: goto label_259038;
        case 0x25903cu: goto label_25903c;
        case 0x259040u: goto label_259040;
        case 0x259044u: goto label_259044;
        case 0x259048u: goto label_259048;
        case 0x25904cu: goto label_25904c;
        case 0x259050u: goto label_259050;
        case 0x259054u: goto label_259054;
        case 0x259058u: goto label_259058;
        case 0x25905cu: goto label_25905c;
        case 0x259060u: goto label_259060;
        case 0x259064u: goto label_259064;
        case 0x259068u: goto label_259068;
        case 0x25906cu: goto label_25906c;
        case 0x259070u: goto label_259070;
        case 0x259074u: goto label_259074;
        case 0x259078u: goto label_259078;
        case 0x25907cu: goto label_25907c;
        case 0x259080u: goto label_259080;
        case 0x259084u: goto label_259084;
        case 0x259088u: goto label_259088;
        case 0x25908cu: goto label_25908c;
        case 0x259090u: goto label_259090;
        case 0x259094u: goto label_259094;
        case 0x259098u: goto label_259098;
        case 0x25909cu: goto label_25909c;
        case 0x2590a0u: goto label_2590a0;
        case 0x2590a4u: goto label_2590a4;
        case 0x2590a8u: goto label_2590a8;
        case 0x2590acu: goto label_2590ac;
        case 0x2590b0u: goto label_2590b0;
        case 0x2590b4u: goto label_2590b4;
        case 0x2590b8u: goto label_2590b8;
        case 0x2590bcu: goto label_2590bc;
        case 0x2590c0u: goto label_2590c0;
        case 0x2590c4u: goto label_2590c4;
        case 0x2590c8u: goto label_2590c8;
        case 0x2590ccu: goto label_2590cc;
        case 0x2590d0u: goto label_2590d0;
        case 0x2590d4u: goto label_2590d4;
        case 0x2590d8u: goto label_2590d8;
        case 0x2590dcu: goto label_2590dc;
        case 0x2590e0u: goto label_2590e0;
        case 0x2590e4u: goto label_2590e4;
        case 0x2590e8u: goto label_2590e8;
        case 0x2590ecu: goto label_2590ec;
        case 0x2590f0u: goto label_2590f0;
        case 0x2590f4u: goto label_2590f4;
        case 0x2590f8u: goto label_2590f8;
        case 0x2590fcu: goto label_2590fc;
        case 0x259100u: goto label_259100;
        case 0x259104u: goto label_259104;
        case 0x259108u: goto label_259108;
        case 0x25910cu: goto label_25910c;
        case 0x259110u: goto label_259110;
        case 0x259114u: goto label_259114;
        case 0x259118u: goto label_259118;
        case 0x25911cu: goto label_25911c;
        case 0x259120u: goto label_259120;
        case 0x259124u: goto label_259124;
        case 0x259128u: goto label_259128;
        case 0x25912cu: goto label_25912c;
        case 0x259130u: goto label_259130;
        case 0x259134u: goto label_259134;
        case 0x259138u: goto label_259138;
        case 0x25913cu: goto label_25913c;
        case 0x259140u: goto label_259140;
        case 0x259144u: goto label_259144;
        case 0x259148u: goto label_259148;
        case 0x25914cu: goto label_25914c;
        case 0x259150u: goto label_259150;
        case 0x259154u: goto label_259154;
        case 0x259158u: goto label_259158;
        case 0x25915cu: goto label_25915c;
        case 0x259160u: goto label_259160;
        case 0x259164u: goto label_259164;
        case 0x259168u: goto label_259168;
        case 0x25916cu: goto label_25916c;
        case 0x259170u: goto label_259170;
        case 0x259174u: goto label_259174;
        case 0x259178u: goto label_259178;
        case 0x25917cu: goto label_25917c;
        case 0x259180u: goto label_259180;
        case 0x259184u: goto label_259184;
        case 0x259188u: goto label_259188;
        case 0x25918cu: goto label_25918c;
        case 0x259190u: goto label_259190;
        case 0x259194u: goto label_259194;
        case 0x259198u: goto label_259198;
        case 0x25919cu: goto label_25919c;
        case 0x2591a0u: goto label_2591a0;
        case 0x2591a4u: goto label_2591a4;
        case 0x2591a8u: goto label_2591a8;
        case 0x2591acu: goto label_2591ac;
        case 0x2591b0u: goto label_2591b0;
        case 0x2591b4u: goto label_2591b4;
        case 0x2591b8u: goto label_2591b8;
        default: break;
    }

    ctx->pc = 0x258e88u;

label_258e88:
    ghpcRebaseMetaMusicFade(rdram, GPR_U32(ctx, 4));
    // 0x258e88: 0x27bdff80  addiu       $sp, $sp, -0x80
    ctx->pc = 0x258e88u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967168));
label_258e8c:
    // 0x258e8c: 0x7fb00050  sq          $s0, 0x50($sp)
    ctx->pc = 0x258e8cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 80), GPR_VEC(ctx, 16));
label_258e90:
    // 0x258e90: 0x7fb10040  sq          $s1, 0x40($sp)
    ctx->pc = 0x258e90u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 64), GPR_VEC(ctx, 17));
label_258e94:
    // 0x258e94: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x258e94u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
label_258e98:
    // 0x258e98: 0x7fb20030  sq          $s2, 0x30($sp)
    ctx->pc = 0x258e98u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 48), GPR_VEC(ctx, 18));
label_258e9c:
    // 0x258e9c: 0x7fb30020  sq          $s3, 0x20($sp)
    ctx->pc = 0x258e9cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 32), GPR_VEC(ctx, 19));
label_258ea0:
    // 0x258ea0: 0xffbf0010  sd          $ra, 0x10($sp)
    ctx->pc = 0x258ea0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 31));
label_258ea4:
    // 0x258ea4: 0xe7b60070  swc1        $f22, 0x70($sp)
    ctx->pc = 0x258ea4u;
    { float f = ctx->f[22]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 112), bits); }
label_258ea8:
    // 0x258ea8: 0xe7b50068  swc1        $f21, 0x68($sp)
    ctx->pc = 0x258ea8u;
    { float f = ctx->f[21]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 104), bits); }
label_258eac:
    // 0x258eac: 0xe7b40060  swc1        $f20, 0x60($sp)
    ctx->pc = 0x258eacu;
    { float f = ctx->f[20]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 29), 96), bits); }
label_258eb0:
    // 0x258eb0: 0x8e020050  lw          $v0, 0x50($s0)
    ctx->pc = 0x258eb0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 80)));
label_258eb4:
    // 0x258eb4: 0x50400030  beql        $v0, $zero, . + 4 + (0x30 << 2)
label_258eb8:
    if (ctx->pc == 0x258EB8u) {
        ctx->pc = 0x258EB8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258EB4u;
        // 0x258eb8: 0x8e02003c  lw          $v0, 0x3C($s0) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 60)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258EBCu;
        goto label_258ebc;
    }
    ctx->pc = 0x258EB4u;
    {
        const bool branch_taken_0x258eb4 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x258eb4) {
            ctx->pc = 0x258EB8u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x258EB4u;
            // 0x258eb8: 0x8e02003c  lw          $v0, 0x3C($s0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 60)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x258F78u;
            goto label_258f78;
        }
    }
    ctx->pc = 0x258EBCu;
label_258ebc:
    // 0x258ebc: 0x8e02006c  lw          $v0, 0x6C($s0)
    ctx->pc = 0x258ebcu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 108)));
label_258ec0:
    // 0x258ec0: 0x5440002d  bnel        $v0, $zero, . + 4 + (0x2D << 2)
label_258ec4:
    if (ctx->pc == 0x258EC4u) {
        ctx->pc = 0x258EC4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258EC0u;
        // 0x258ec4: 0x8e02003c  lw          $v0, 0x3C($s0) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 60)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258EC8u;
        goto label_258ec8;
    }
    ctx->pc = 0x258EC0u;
    {
        const bool branch_taken_0x258ec0 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x258ec0) {
            ctx->pc = 0x258EC4u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x258EC0u;
            // 0x258ec4: 0x8e02003c  lw          $v0, 0x3C($s0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 60)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x258F78u;
            goto label_258f78;
        }
    }
    ctx->pc = 0x258EC8u;
label_258ec8:
    // 0x258ec8: 0x3c02004b  lui         $v0, 0x4B
    ctx->pc = 0x258ec8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)75 << 16));
label_258ecc:
    // 0x258ecc: 0x2453b8a8  addiu       $s3, $v0, -0x4758
    ctx->pc = 0x258eccu;
    SET_GPR_S32(ctx, 19, (int32_t)ADD32(GPR_U32(ctx, 2), 4294949032));
label_258ed0:
    // 0x258ed0: 0xc0c8410  jal         func_321040
label_258ed4:
    if (ctx->pc == 0x258ED4u) {
        ctx->pc = 0x258ED4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258ED0u;
        // 0x258ed4: 0x260202d  daddu       $a0, $s3, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258ED8u;
        goto label_258ed8;
    }
    ctx->pc = 0x258ED0u;
    SET_GPR_U32(ctx, 31, 0x258ED8u);
    ctx->pc = 0x258ED4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258ED0u;
    // 0x258ed4: 0x260202d  daddu       $a0, $s3, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321040u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321040u, 0x258ED0u, 0x258ED8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258ED8u;
label_258ed8:
    // 0x258ed8: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x258ed8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_258edc:
    // 0x258edc: 0x37a50004  ori         $a1, $sp, 0x4
    ctx->pc = 0x258edcu;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 29) | (uint64_t)(uint16_t)4);
label_258ee0:
    // 0x258ee0: 0x37a60008  ori         $a2, $sp, 0x8
    ctx->pc = 0x258ee0u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 29) | (uint64_t)(uint16_t)8);
label_258ee4:
    // 0x258ee4: 0xc0c7d6e  jal         func_31F5B8
label_258ee8:
    if (ctx->pc == 0x258EE8u) {
        ctx->pc = 0x258EE8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258EE4u;
        // 0x258ee8: 0x37a7000c  ori         $a3, $sp, 0xC (Delay Slot)
        SET_GPR_U64(ctx, 7, GPR_U64(ctx, 29) | (uint64_t)(uint16_t)12);
        ctx->in_delay_slot = false;
        ctx->pc = 0x258EECu;
        goto label_258eec;
    }
    ctx->pc = 0x258EE4u;
    SET_GPR_U32(ctx, 31, 0x258EECu);
    ctx->pc = 0x258EE8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258EE4u;
    // 0x258ee8: 0x37a7000c  ori         $a3, $sp, 0xC (Delay Slot)
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 29) | (uint64_t)(uint16_t)12);
    ctx->in_delay_slot = false;
    ctx->pc = 0x31F5B8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x31F5B8u, 0x258EE4u, 0x258EECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258EECu;
label_258eec:
    // 0x258eec: 0x8e020078  lw          $v0, 0x78($s0)
    ctx->pc = 0x258eecu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 120)));
label_258ef0:
    // 0x258ef0: 0x8fa3000c  lw          $v1, 0xC($sp)
    ctx->pc = 0x258ef0u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 12)));
label_258ef4:
    // 0x258ef4: 0x24420020  addiu       $v0, $v0, 0x20
    ctx->pc = 0x258ef4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 32));
label_258ef8:
    // 0x258ef8: 0x43102a  slt         $v0, $v0, $v1
    ctx->pc = 0x258ef8u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 2) < (int64_t)GPR_S64(ctx, 3)) ? 1 : 0);
label_258efc:
    // 0x258efc: 0x104000a5  beqz        $v0, . + 4 + (0xA5 << 2)
label_258f00:
    if (ctx->pc == 0x258F00u) {
        ctx->pc = 0x258F00u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258EFCu;
        // 0x258f00: 0x3c120050  lui         $s2, 0x50 (Delay Slot)
        SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)80 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F04u;
        goto label_258f04;
    }
    ctx->pc = 0x258EFCu;
    {
        const bool branch_taken_0x258efc = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x258F00u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258EFCu;
        // 0x258f00: 0x3c120050  lui         $s2, 0x50 (Delay Slot)
        SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)80 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x258efc) {
            ctx->pc = 0x259194u;
            goto label_259194;
        }
    }
    ctx->pc = 0x258F04u;
label_258f04:
    // 0x258f04: 0x8e42a06c  lw          $v0, -0x5F94($s2)
    ctx->pc = 0x258f04u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 4294942828)));
label_258f08:
    // 0x258f08: 0x14400006  bnez        $v0, . + 4 + (0x6 << 2)
label_258f0c:
    if (ctx->pc == 0x258F0Cu) {
        ctx->pc = 0x258F0Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F08u;
        // 0x258f0c: 0x3c110050  lui         $s1, 0x50 (Delay Slot)
        SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)80 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F10u;
        goto label_258f10;
    }
    ctx->pc = 0x258F08u;
    {
        const bool branch_taken_0x258f08 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x258F0Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F08u;
        // 0x258f0c: 0x3c110050  lui         $s1, 0x50 (Delay Slot)
        SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)80 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x258f08) {
            ctx->pc = 0x258F24u;
            goto label_258f24;
        }
    }
    ctx->pc = 0x258F10u;
label_258f10:
    // 0x258f10: 0xc0c8410  jal         func_321040
label_258f14:
    if (ctx->pc == 0x258F14u) {
        ctx->pc = 0x258F14u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F10u;
        // 0x258f14: 0x260202d  daddu       $a0, $s3, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F18u;
        goto label_258f18;
    }
    ctx->pc = 0x258F10u;
    SET_GPR_U32(ctx, 31, 0x258F18u);
    ctx->pc = 0x258F14u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F10u;
    // 0x258f14: 0x260202d  daddu       $a0, $s3, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321040u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321040u, 0x258F10u, 0x258F18u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F18u;
label_258f18:
    // 0x258f18: 0x24030001  addiu       $v1, $zero, 0x1
    ctx->pc = 0x258f18u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
label_258f1c:
    // 0x258f1c: 0xae22a068  sw          $v0, -0x5F98($s1)
    ctx->pc = 0x258f1cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 4294942824), GPR_U32(ctx, 2));
label_258f20:
    // 0x258f20: 0xae43a06c  sw          $v1, -0x5F94($s2)
    ctx->pc = 0x258f20u;
    WRITE32(ADD32(GPR_U32(ctx, 18), 4294942828), GPR_U32(ctx, 3));
label_258f24:
    // 0x258f24: 0xc0c814c  jal         func_320530
label_258f28:
    if (ctx->pc == 0x258F28u) {
        ctx->pc = 0x258F28u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F24u;
        // 0x258f28: 0x8e24a068  lw          $a0, -0x5F98($s1) (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4294942824)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F2Cu;
        goto label_258f2c;
    }
    ctx->pc = 0x258F24u;
    SET_GPR_U32(ctx, 31, 0x258F2Cu);
    ctx->pc = 0x258F28u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F24u;
    // 0x258f28: 0x8e24a068  lw          $a0, -0x5F98($s1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4294942824)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x320530u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x320530u, 0x258F24u, 0x258F2Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F2Cu;
label_258f2c:
    // 0x258f2c: 0x8e040078  lw          $a0, 0x78($s0)
    ctx->pc = 0x258f2cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 120)));
label_258f30:
    // 0x258f30: 0x3c05004b  lui         $a1, 0x4B
    ctx->pc = 0x258f30u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)75 << 16));
label_258f34:
    // 0x258f34: 0xc0c8488  jal         func_321220
label_258f38:
    if (ctx->pc == 0x258F38u) {
        ctx->pc = 0x258F38u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F34u;
        // 0x258f38: 0x24a5b8b0  addiu       $a1, $a1, -0x4750 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294949040));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F3Cu;
        goto label_258f3c;
    }
    ctx->pc = 0x258F34u;
    SET_GPR_U32(ctx, 31, 0x258F3Cu);
    ctx->pc = 0x258F38u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F34u;
    // 0x258f38: 0x24a5b8b0  addiu       $a1, $a1, -0x4750 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294949040));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321220u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321220u, 0x258F34u, 0x258F3Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F3Cu;
label_258f3c:
    // 0x258f3c: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x258f3cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_258f40:
    // 0x258f40: 0xc0c8454  jal         func_321150
label_258f44:
    if (ctx->pc == 0x258F44u) {
        ctx->pc = 0x258F44u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F40u;
        // 0x258f44: 0xae020068  sw          $v0, 0x68($s0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 16), 104), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F48u;
        goto label_258f48;
    }
    ctx->pc = 0x258F40u;
    SET_GPR_U32(ctx, 31, 0x258F48u);
    ctx->pc = 0x258F44u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F40u;
    // 0x258f44: 0xae020068  sw          $v0, 0x68($s0) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 16), 104), GPR_U32(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321150u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321150u, 0x258F40u, 0x258F48u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F48u;
label_258f48:
    // 0x258f48: 0x8e070070  lw          $a3, 0x70($s0)
    ctx->pc = 0x258f48u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
label_258f4c:
    // 0x258f4c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x258f4cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_258f50:
    // 0x258f50: 0xae02006c  sw          $v0, 0x6C($s0)
    ctx->pc = 0x258f50u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 108), GPR_U32(ctx, 2));
label_258f54:
    // 0x258f54: 0x8e060078  lw          $a2, 0x78($s0)
    ctx->pc = 0x258f54u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 120)));
label_258f58:
    // 0x258f58: 0x8ce30000  lw          $v1, 0x0($a3)
    ctx->pc = 0x258f58u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 0)));
label_258f5c:
    // 0x258f5c: 0x84640018  lh          $a0, 0x18($v1)
    ctx->pc = 0x258f5cu;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 3), 24)));
label_258f60:
    // 0x258f60: 0x8c62001c  lw          $v0, 0x1C($v1)
    ctx->pc = 0x258f60u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 28)));
label_258f64:
    // 0x258f64: 0x40f809  jalr        $v0
label_258f68:
    if (ctx->pc == 0x258F68u) {
        ctx->pc = 0x258F68u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F64u;
        // 0x258f68: 0xe42021  addu        $a0, $a3, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 7), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F6Cu;
        goto label_258f6c;
    }
    ctx->pc = 0x258F64u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 2);
        SET_GPR_U32(ctx, 31, 0x258F6Cu);
        ctx->pc = 0x258F68u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F64u;
        // 0x258f68: 0xe42021  addu        $a0, $a3, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 7), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x258F64u, 0x258F6Cu, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x258F6Cu;
label_258f6c:
    // 0x258f6c: 0xc0c8170  jal         func_3205C0
label_258f70:
    if (ctx->pc == 0x258F70u) {
        ctx->pc = 0x258F74u;
        goto label_258f74;
    }
    ctx->pc = 0x258F6Cu;
    SET_GPR_U32(ctx, 31, 0x258F74u);
    ctx->pc = 0x3205C0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3205C0u, 0x258F6Cu, 0x258F74u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F74u;
label_258f74:
    // 0x258f74: 0x8e02003c  lw          $v0, 0x3C($s0)
    ctx->pc = 0x258f74u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 60)));
label_258f78:
    // 0x258f78: 0x50400009  beql        $v0, $zero, . + 4 + (0x9 << 2)
label_258f7c:
    if (ctx->pc == 0x258F7Cu) {
        ctx->pc = 0x258F7Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F78u;
        // 0x258f7c: 0x8e060070  lw          $a2, 0x70($s0) (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F80u;
        goto label_258f80;
    }
    ctx->pc = 0x258F78u;
    {
        const bool branch_taken_0x258f78 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x258f78) {
            ctx->pc = 0x258F7Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x258F78u;
            // 0x258f7c: 0x8e060070  lw          $a2, 0x70($s0) (Delay Slot)
            SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x258FA0u;
            goto label_258fa0;
        }
    }
    ctx->pc = 0x258F80u;
label_258f80:
    // 0x258f80: 0xc096390  jal         func_258E40
label_258f84:
    if (ctx->pc == 0x258F84u) {
        ctx->pc = 0x258F84u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F80u;
        // 0x258f84: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F88u;
        goto label_258f88;
    }
    ctx->pc = 0x258F80u;
    SET_GPR_U32(ctx, 31, 0x258F88u);
    ctx->pc = 0x258F84u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F80u;
    // 0x258f84: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x258E40u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x258E40u, 0x258F80u, 0x258F88u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F88u;
label_258f88:
    // 0x258f88: 0x50400005  beql        $v0, $zero, . + 4 + (0x5 << 2)
label_258f8c:
    if (ctx->pc == 0x258F8Cu) {
        ctx->pc = 0x258F8Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F88u;
        // 0x258f8c: 0x8e060070  lw          $a2, 0x70($s0) (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F90u;
        goto label_258f90;
    }
    ctx->pc = 0x258F88u;
    {
        const bool branch_taken_0x258f88 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x258f88) {
            ctx->pc = 0x258F8Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x258F88u;
            // 0x258f8c: 0x8e060070  lw          $a2, 0x70($s0) (Delay Slot)
            SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x258FA0u;
            goto label_258fa0;
        }
    }
    ctx->pc = 0x258F90u;
label_258f90:
    // 0x258f90: 0xae00003c  sw          $zero, 0x3C($s0)
    ctx->pc = 0x258f90u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 60), GPR_U32(ctx, 0));
label_258f94:
    // 0x258f94: 0xc096470  jal         func_2591C0
label_258f98:
    if (ctx->pc == 0x258F98u) {
        ctx->pc = 0x258F98u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258F94u;
        // 0x258f98: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258F9Cu;
        goto label_258f9c;
    }
    ctx->pc = 0x258F94u;
    SET_GPR_U32(ctx, 31, 0x258F9Cu);
    ctx->pc = 0x258F98u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258F94u;
    // 0x258f98: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2591C0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2591C0u, 0x258F94u, 0x258F9Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258F9Cu;
label_258f9c:
    // 0x258f9c: 0x8e060070  lw          $a2, 0x70($s0)
    ctx->pc = 0x258f9cu;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
label_258fa0:
    // 0x258fa0: 0x10c00020  beqz        $a2, . + 4 + (0x20 << 2)
label_258fa4:
    if (ctx->pc == 0x258FA4u) {
        ctx->pc = 0x258FA4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FA0u;
        // 0x258fa4: 0x2605007c  addiu       $a1, $s0, 0x7C (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 16), 124));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258FA8u;
        goto label_258fa8;
    }
    ctx->pc = 0x258FA0u;
    {
        const bool branch_taken_0x258fa0 = (GPR_U64(ctx, 6) == GPR_U64(ctx, 0));
        ctx->pc = 0x258FA4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FA0u;
        // 0x258fa4: 0x2605007c  addiu       $a1, $s0, 0x7C (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 16), 124));
        ctx->in_delay_slot = false;
        if (branch_taken_0x258fa0) {
            ctx->pc = 0x259024u;
            goto label_259024;
        }
    }
    ctx->pc = 0x258FA8u;
label_258fa8:
    // 0x258fa8: 0x8cc30000  lw          $v1, 0x0($a2)
    ctx->pc = 0x258fa8u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 0)));
label_258fac:
    // 0x258fac: 0x84640068  lh          $a0, 0x68($v1)
    ctx->pc = 0x258facu;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 3), 104)));
label_258fb0:
    // 0x258fb0: 0x8c62006c  lw          $v0, 0x6C($v1)
    ctx->pc = 0x258fb0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 108)));
label_258fb4:
    // 0x258fb4: 0x40f809  jalr        $v0
label_258fb8:
    if (ctx->pc == 0x258FB8u) {
        ctx->pc = 0x258FB8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FB4u;
        // 0x258fb8: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258FBCu;
        goto label_258fbc;
    }
    ctx->pc = 0x258FB4u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 2);
        SET_GPR_U32(ctx, 31, 0x258FBCu);
        ctx->pc = 0x258FB8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FB4u;
        // 0x258fb8: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x258FB4u, 0x258FBCu, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x258FBCu;
label_258fbc:
    // 0x258fbc: 0x5040001a  beql        $v0, $zero, . + 4 + (0x1A << 2)
label_258fc0:
    if (ctx->pc == 0x258FC0u) {
        ctx->pc = 0x258FC0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FBCu;
        // 0x258fc0: 0x8e02002c  lw          $v0, 0x2C($s0) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 44)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258FC4u;
        goto label_258fc4;
    }
    ctx->pc = 0x258FBCu;
    {
        const bool branch_taken_0x258fbc = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x258fbc) {
            ctx->pc = 0x258FC0u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x258FBCu;
            // 0x258fc0: 0x8e02002c  lw          $v0, 0x2C($s0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 44)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x259028u;
            goto label_259028;
        }
    }
    ctx->pc = 0x258FC4u;
label_258fc4:
    // 0x258fc4: 0x8e03007c  lw          $v1, 0x7C($s0)
    ctx->pc = 0x258fc4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 124)));
label_258fc8:
    // 0x258fc8: 0x8e020078  lw          $v0, 0x78($s0)
    ctx->pc = 0x258fc8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 120)));
label_258fcc:
    // 0x258fcc: 0x1062000c  beq         $v1, $v0, . + 4 + (0xC << 2)
label_258fd0:
    if (ctx->pc == 0x258FD0u) {
        ctx->pc = 0x258FD0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FCCu;
        // 0x258fd0: 0x3c020044  lui         $v0, 0x44 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258FD4u;
        goto label_258fd4;
    }
    ctx->pc = 0x258FCCu;
    {
        const bool branch_taken_0x258fcc = (GPR_U64(ctx, 3) == GPR_U64(ctx, 2));
        ctx->pc = 0x258FD0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FCCu;
        // 0x258fd0: 0x3c020044  lui         $v0, 0x44 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x258fcc) {
            ctx->pc = 0x259000u;
            goto label_259000;
        }
    }
    ctx->pc = 0x258FD4u;
label_258fd4:
    // 0x258fd4: 0x3c05004b  lui         $a1, 0x4B
    ctx->pc = 0x258fd4u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)75 << 16));
label_258fd8:
    // 0x258fd8: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x258fd8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20416)));
label_258fdc:
    // 0x258fdc: 0x3c07004b  lui         $a3, 0x4B
    ctx->pc = 0x258fdcu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)75 << 16));
label_258fe0:
    // 0x258fe0: 0x24a5b860  addiu       $a1, $a1, -0x47A0
    ctx->pc = 0x258fe0u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294948960));
label_258fe4:
    // 0x258fe4: 0x24e7b8c0  addiu       $a3, $a3, -0x4740
    ctx->pc = 0x258fe4u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294949056));
label_258fe8:
    // 0x258fe8: 0xc0da03a  jal         func_3680E8
label_258fec:
    if (ctx->pc == 0x258FECu) {
        ctx->pc = 0x258FECu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FE8u;
        // 0x258fec: 0x240600b2  addiu       $a2, $zero, 0xB2 (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 178));
        ctx->in_delay_slot = false;
        ctx->pc = 0x258FF0u;
        goto label_258ff0;
    }
    ctx->pc = 0x258FE8u;
    SET_GPR_U32(ctx, 31, 0x258FF0u);
    ctx->pc = 0x258FECu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258FE8u;
    // 0x258fec: 0x240600b2  addiu       $a2, $zero, 0xB2 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 178));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x258FE8u, 0x258FF0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x258FF0u;
label_258ff0:
    // 0x258ff0: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x258ff0u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
label_258ff4:
    // 0x258ff4: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x258ff4u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_258ff8:
    // 0x258ff8: 0xc0baf6a  jal         func_2EBDA8
label_258ffc:
    if (ctx->pc == 0x258FFCu) {
        ctx->pc = 0x258FFCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x258FF8u;
        // 0x258ffc: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259000u;
        goto label_259000;
    }
    ctx->pc = 0x258FF8u;
    SET_GPR_U32(ctx, 31, 0x259000u);
    ctx->pc = 0x258FFCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x258FF8u;
    // 0x258ffc: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x258FF8u, 0x259000u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x259000u;
label_259000:
    // 0x259000: 0x8e060070  lw          $a2, 0x70($s0)
    ctx->pc = 0x259000u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 112)));
label_259004:
    // 0x259004: 0x10c00006  beqz        $a2, . + 4 + (0x6 << 2)
label_259008:
    if (ctx->pc == 0x259008u) {
        ctx->pc = 0x259008u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259004u;
        // 0x259008: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
        ctx->in_delay_slot = false;
        ctx->pc = 0x25900Cu;
        goto label_25900c;
    }
    ctx->pc = 0x259004u;
    {
        const bool branch_taken_0x259004 = (GPR_U64(ctx, 6) == GPR_U64(ctx, 0));
        ctx->pc = 0x259008u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259004u;
        // 0x259008: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
        ctx->in_delay_slot = false;
        if (branch_taken_0x259004) {
            ctx->pc = 0x259020u;
            goto label_259020;
        }
    }
    ctx->pc = 0x25900Cu;
label_25900c:
    // 0x25900c: 0x8cc20000  lw          $v0, 0x0($a2)
    ctx->pc = 0x25900cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 0)));
label_259010:
    // 0x259010: 0x84440008  lh          $a0, 0x8($v0)
    ctx->pc = 0x259010u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 2), 8)));
label_259014:
    // 0x259014: 0x8c43000c  lw          $v1, 0xC($v0)
    ctx->pc = 0x259014u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 12)));
label_259018:
    // 0x259018: 0x60f809  jalr        $v1
label_25901c:
    if (ctx->pc == 0x25901Cu) {
        ctx->pc = 0x25901Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259018u;
        // 0x25901c: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259020u;
        goto label_259020;
    }
    ctx->pc = 0x259018u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 3);
        SET_GPR_U32(ctx, 31, 0x259020u);
        ctx->pc = 0x25901Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259018u;
        // 0x25901c: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x259018u, 0x259020u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x259020u;
label_259020:
    // 0x259020: 0xae000070  sw          $zero, 0x70($s0)
    ctx->pc = 0x259020u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 112), GPR_U32(ctx, 0));
label_259024:
    // 0x259024: 0x8e02002c  lw          $v0, 0x2C($s0)
    ctx->pc = 0x259024u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 44)));
label_259028:
    // 0x259028: 0x5040005b  beql        $v0, $zero, . + 4 + (0x5B << 2)
label_25902c:
    if (ctx->pc == 0x25902Cu) {
        ctx->pc = 0x25902Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259028u;
        // 0x25902c: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259030u;
        goto label_259030;
    }
    ctx->pc = 0x259028u;
    {
        const bool branch_taken_0x259028 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x259028) {
            ctx->pc = 0x25902Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x259028u;
            // 0x25902c: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
            SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x259198u;
            goto label_259198;
        }
    }
    ctx->pc = 0x259030u;
label_259030:
    // 0x259030: 0x8e020038  lw          $v0, 0x38($s0)
    ctx->pc = 0x259030u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 56)));
label_259034:
    // 0x259034: 0x50400025  beql        $v0, $zero, . + 4 + (0x25 << 2)
label_259038:
    if (ctx->pc == 0x259038u) {
        ctx->pc = 0x259038u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259034u;
        // 0x259038: 0x8e020030  lw          $v0, 0x30($s0) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 48)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x25903Cu;
        goto label_25903c;
    }
    ctx->pc = 0x259034u;
    {
        const bool branch_taken_0x259034 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x259034) {
            ctx->pc = 0x259038u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x259034u;
            // 0x259038: 0x8e020030  lw          $v0, 0x30($s0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 48)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x2590CCu;
            goto label_2590cc;
        }
    }
    ctx->pc = 0x25903Cu;
label_25903c:
    // 0x25903c: 0x8e050028  lw          $a1, 0x28($s0)
    ctx->pc = 0x25903cu;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
label_259040:
    // 0x259040: 0x8ca30004  lw          $v1, 0x4($a1)
    ctx->pc = 0x259040u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 4)));
label_259044:
    // 0x259044: 0x84640010  lh          $a0, 0x10($v1)
    ctx->pc = 0x259044u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 3), 16)));
label_259048:
    // 0x259048: 0x8c620014  lw          $v0, 0x14($v1)
    ctx->pc = 0x259048u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 20)));
label_25904c:
    // 0x25904c: 0x40f809  jalr        $v0
label_259050:
    if (ctx->pc == 0x259050u) {
        ctx->pc = 0x259050u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25904Cu;
        // 0x259050: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259054u;
        goto label_259054;
    }
    ctx->pc = 0x25904Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 2);
        SET_GPR_U32(ctx, 31, 0x259054u);
        ctx->pc = 0x259050u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25904Cu;
        // 0x259050: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x25904Cu, 0x259054u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x259054u;
label_259054:
    // 0x259054: 0x5040001d  beql        $v0, $zero, . + 4 + (0x1D << 2)
label_259058:
    if (ctx->pc == 0x259058u) {
        ctx->pc = 0x259058u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259054u;
        // 0x259058: 0x8e020030  lw          $v0, 0x30($s0) (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 48)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x25905Cu;
        goto label_25905c;
    }
    ctx->pc = 0x259054u;
    {
        const bool branch_taken_0x259054 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x259054) {
            ctx->pc = 0x259058u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x259054u;
            // 0x259058: 0x8e020030  lw          $v0, 0x30($s0) (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 48)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x2590CCu;
            goto label_2590cc;
        }
    }
    ctx->pc = 0x25905Cu;
label_25905c:
    // 0x25905c: 0x3c01c2c0  lui         $at, 0xC2C0
    ctx->pc = 0x25905cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)49856 << 16));
label_259060:
    // 0x259060: 0x4481b000  mtc1        $at, $f22
    ctx->pc = 0x259060u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[22], &bits, sizeof(bits)); }
label_259064:
    // 0x259064: 0x8e040028  lw          $a0, 0x28($s0)
    ctx->pc = 0x259064u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
label_259068:
    // 0x259068: 0x3c01447a  lui         $at, 0x447A
    ctx->pc = 0x259068u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17530 << 16));
label_25906c:
    // 0x25906c: 0x4481a800  mtc1        $at, $f21
    ctx->pc = 0x25906cu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[21], &bits, sizeof(bits)); }
label_259070:
    // 0x259070: 0xc097fe2  jal         func_25FF88
label_259074:
    if (ctx->pc == 0x259074u) {
        ctx->pc = 0x259074u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259070u;
        // 0x259074: 0x4600b306  mov.s       $f12, $f22 (Delay Slot)
        ctx->f[12] = FPU_MOV_S(ctx->f[22]);
        ctx->in_delay_slot = false;
        ctx->pc = 0x259078u;
        goto label_259078;
    }
    ctx->pc = 0x259070u;
    SET_GPR_U32(ctx, 31, 0x259078u);
    ctx->pc = 0x259074u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x259070u;
    // 0x259074: 0x4600b306  mov.s       $f12, $f22 (Delay Slot)
    ctx->f[12] = FPU_MOV_S(ctx->f[22]);
    ctx->in_delay_slot = false;
    ctx->pc = 0x25FF88u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x25FF88u, 0x259070u, 0x259078u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x259078u;
label_259078:
    // 0x259078: 0x8e050028  lw          $a1, 0x28($s0)
    ctx->pc = 0x259078u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
label_25907c:
    // 0x25907c: 0x8ca20004  lw          $v0, 0x4($a1)
    ctx->pc = 0x25907cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 4)));
label_259080:
    // 0x259080: 0x84440020  lh          $a0, 0x20($v0)
    ctx->pc = 0x259080u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 2), 32)));
label_259084:
    // 0x259084: 0x8c430024  lw          $v1, 0x24($v0)
    ctx->pc = 0x259084u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 36)));
label_259088:
    // 0x259088: 0x60f809  jalr        $v1
label_25908c:
    if (ctx->pc == 0x25908Cu) {
        ctx->pc = 0x25908Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259088u;
        // 0x25908c: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259090u;
        goto label_259090;
    }
    ctx->pc = 0x259088u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 3);
        SET_GPR_U32(ctx, 31, 0x259090u);
        ctx->pc = 0x25908Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259088u;
        // 0x25908c: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x259088u, 0x259090u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x259090u;
label_259090:
    // 0x259090: 0xc6140044  lwc1        $f20, 0x44($s0)
    ctx->pc = 0x259090u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 68)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[20] = f; }
label_259094:
    // 0x259094: 0x3c04005a  lui         $a0, 0x5A
    ctx->pc = 0x259094u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)90 << 16));
label_259098:
    // 0x259098: 0x4614ad03  div.s       $f20, $f21, $f20
    ctx->pc = 0x259098u;
    if (ctx->f[20] == 0.0f) { ctx->fcr31 |= 0x100000; /* DZ flag */ ctx->f[20] = copysignf(INFINITY, ctx->f[21] * 0.0f); } else ctx->f[20] = ctx->f[21] / ctx->f[20];
label_25909c:
    // 0x25909c: 0xc0c5912  jal         func_316448
label_2590a0:
    if (ctx->pc == 0x2590A0u) {
        ctx->pc = 0x2590A0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25909Cu;
        // 0x2590a0: 0x24845870  addiu       $a0, $a0, 0x5870 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22640));
        ctx->in_delay_slot = false;
        ctx->pc = 0x2590A4u;
        goto label_2590a4;
    }
    ctx->pc = 0x25909Cu;
    SET_GPR_U32(ctx, 31, 0x2590A4u);
    ctx->pc = 0x2590A0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x25909Cu;
    // 0x2590a0: 0x24845870  addiu       $a0, $a0, 0x5870 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22640));
    ctx->in_delay_slot = false;
    ctx->pc = 0x316448u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x316448u, 0x25909Cu, 0x2590A4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x2590A4u;
label_2590a4:
    // 0x2590a4: 0x46150382  mul.s       $f14, $f0, $f21
    ctx->pc = 0x2590a4u;
    ctx->f[14] = FPU_MUL_S(ctx->f[0], ctx->f[21]);
label_2590a8:
    // 0x2590a8: 0x24020001  addiu       $v0, $zero, 0x1
    ctx->pc = 0x2590a8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
label_2590ac:
    // 0x2590ac: 0xc60d0048  lwc1        $f13, 0x48($s0)
    ctx->pc = 0x2590acu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 72)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[13] = f; }
label_2590b0:
    // 0x2590b0: 0x4600b306  mov.s       $f12, $f22
    ctx->pc = 0x2590b0u;
    ctx->f[12] = FPU_MOV_S(ctx->f[22]);
label_2590b4:
    // 0x2590b4: 0xae020030  sw          $v0, 0x30($s0)
    ctx->pc = 0x2590b4u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 2));
label_2590b8:
    // 0x2590b8: 0x26040080  addiu       $a0, $s0, 0x80
    ctx->pc = 0x2590b8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 128));
label_2590bc:
    // 0x2590bc: 0xc0cb504  jal         func_32D410
label_2590c0:
    if (ctx->pc == 0x2590C0u) {
        ctx->pc = 0x2590C0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2590BCu;
        // 0x2590c0: 0x461473c0  add.s       $f15, $f14, $f20 (Delay Slot)
        ctx->f[15] = FPU_ADD_S(ctx->f[14], ctx->f[20]);
        ctx->in_delay_slot = false;
        ctx->pc = 0x2590C4u;
        goto label_2590c4;
    }
    ctx->pc = 0x2590BCu;
    SET_GPR_U32(ctx, 31, 0x2590C4u);
    ctx->pc = 0x2590C0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x2590BCu;
    // 0x2590c0: 0x461473c0  add.s       $f15, $f14, $f20 (Delay Slot)
    ctx->f[15] = FPU_ADD_S(ctx->f[14], ctx->f[20]);
    ctx->in_delay_slot = false;
    ctx->pc = 0x32D410u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32D410u, 0x2590BCu, 0x2590C4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x2590C4u;
label_2590c4:
    // 0x2590c4: 0xae000038  sw          $zero, 0x38($s0)
    ctx->pc = 0x2590c4u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 56), GPR_U32(ctx, 0));
label_2590c8:
    // 0x2590c8: 0x8e020030  lw          $v0, 0x30($s0)
    ctx->pc = 0x2590c8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 48)));
label_2590cc:
    // 0x2590cc: 0x14400004  bnez        $v0, . + 4 + (0x4 << 2)
label_2590d0:
    if (ctx->pc == 0x2590D0u) {
        ctx->pc = 0x2590D0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2590CCu;
        // 0x2590d0: 0x3c04005a  lui         $a0, 0x5A (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)90 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x2590D4u;
        goto label_2590d4;
    }
    ctx->pc = 0x2590CCu;
    {
        const bool branch_taken_0x2590cc = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x2590D0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2590CCu;
        // 0x2590d0: 0x3c04005a  lui         $a0, 0x5A (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)90 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x2590cc) {
            ctx->pc = 0x2590E0u;
            goto label_2590e0;
        }
    }
    ctx->pc = 0x2590D4u;
label_2590d4:
    // 0x2590d4: 0x8e020034  lw          $v0, 0x34($s0)
    ctx->pc = 0x2590d4u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 52)));
label_2590d8:
    // 0x2590d8: 0x5040002f  beql        $v0, $zero, . + 4 + (0x2F << 2)
label_2590dc:
    if (ctx->pc == 0x2590DCu) {
        ctx->pc = 0x2590DCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2590D8u;
        // 0x2590dc: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x2590E0u;
        goto label_2590e0;
    }
    ctx->pc = 0x2590D8u;
    {
        const bool branch_taken_0x2590d8 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x2590d8) {
            ctx->pc = 0x2590DCu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x2590D8u;
            // 0x2590dc: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
            SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x259198u;
            goto label_259198;
        }
    }
    ctx->pc = 0x2590E0u;
label_2590e0:
    // 0x2590e0: 0xc0c5912  jal         func_316448
label_2590e4:
    if (ctx->pc == 0x2590E4u) {
        ctx->pc = 0x2590E4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2590E0u;
        // 0x2590e4: 0x24845870  addiu       $a0, $a0, 0x5870 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22640));
        ctx->in_delay_slot = false;
        ctx->pc = 0x2590E8u;
        goto label_2590e8;
    }
    ctx->pc = 0x2590E0u;
    SET_GPR_U32(ctx, 31, 0x2590E8u);
    ctx->pc = 0x2590E4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x2590E0u;
    // 0x2590e4: 0x24845870  addiu       $a0, $a0, 0x5870 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22640));
    ctx->in_delay_slot = false;
    ctx->pc = 0x316448u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x316448u, 0x2590E0u, 0x2590E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x2590E8u;
label_2590e8:
    // 0x2590e8: 0x3c01447a  lui         $at, 0x447A
    ctx->pc = 0x2590e8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)17530 << 16));
label_2590ec:
    // 0x2590ec: 0x44811000  mtc1        $at, $f2
    ctx->pc = 0x2590ecu;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }
label_2590f0:
    // 0x2590f0: 0x26020080  addiu       $v0, $s0, 0x80
    ctx->pc = 0x2590f0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 16), 128));
label_2590f4:
    // 0x2590f4: 0xc4410014  lwc1        $f1, 0x14($v0)
    ctx->pc = 0x2590f4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 20)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
label_2590f8:
    // 0x2590f8: 0x46020502  mul.s       $f20, $f0, $f2
    ctx->pc = 0x2590f8u;
    ctx->f[20] = FPU_MUL_S(ctx->f[0], ctx->f[2]);
label_2590fc:
    // 0x2590fc: 0xc6030048  lwc1        $f3, 0x48($s0)
    ctx->pc = 0x2590fcu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 72)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
label_259100:
    // 0x259100: 0xc4400018  lwc1        $f0, 0x18($v0)
    ctx->pc = 0x259100u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 2), 24)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
label_259104:
    // 0x259104: 0x3c01c2c0  lui         $at, 0xC2C0
    ctx->pc = 0x259104u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)49856 << 16));
label_259108:
    // 0x259108: 0x44812000  mtc1        $at, $f4
    ctx->pc = 0x259108u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
label_25910c:
    // 0x25910c: 0x46140842  mul.s       $f1, $f1, $f20
    ctx->pc = 0x25910cu;
    ctx->f[1] = FPU_MUL_S(ctx->f[1], ctx->f[20]);
label_259110:
    // 0x259110: 0x46000b00  add.s       $f12, $f1, $f0
    ctx->pc = 0x259110u;
    ctx->f[12] = FPU_ADD_S(ctx->f[1], ctx->f[0]);
label_259114:
    // 0x259114: 0x460c1834  c.lt.s      $f3, $f12
    ctx->pc = 0x259114u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[3], ctx->f[12])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
label_259118:
    // 0x259118: 0x0  nop
    ctx->pc = 0x259118u;
    // NOP
label_25911c:
    // 0x25911c: 0x45000003  bc1f        . + 4 + (0x3 << 2)
label_259120:
    if (ctx->pc == 0x259120u) {
        ctx->pc = 0x259120u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25911Cu;
        // 0x259120: 0x40882d  daddu       $s1, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259124u;
        goto label_259124;
    }
    ctx->pc = 0x25911Cu;
    {
        const bool branch_taken_0x25911c = (!(ctx->fcr31 & 0x800000));
        ctx->pc = 0x259120u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25911Cu;
        // 0x259120: 0x40882d  daddu       $s1, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x25911c) {
            ctx->pc = 0x25912Cu;
            goto label_25912c;
        }
    }
    ctx->pc = 0x259124u;
label_259124:
    // 0x259124: 0x10000005  b           . + 4 + (0x5 << 2)
label_259128:
    if (ctx->pc == 0x259128u) {
        ctx->pc = 0x259128u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259124u;
        // 0x259128: 0x46001b06  mov.s       $f12, $f3 (Delay Slot)
        ctx->f[12] = FPU_MOV_S(ctx->f[3]);
        ctx->in_delay_slot = false;
        ctx->pc = 0x25912Cu;
        goto label_25912c;
    }
    ctx->pc = 0x259124u;
    {
        const bool branch_taken_0x259124 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x259128u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259124u;
        // 0x259128: 0x46001b06  mov.s       $f12, $f3 (Delay Slot)
        ctx->f[12] = FPU_MOV_S(ctx->f[3]);
        ctx->in_delay_slot = false;
        if (branch_taken_0x259124) {
            ctx->pc = 0x25913Cu;
            goto label_25913c;
        }
    }
    ctx->pc = 0x25912Cu;
label_25912c:
    // 0x25912c: 0x46046034  c.lt.s      $f12, $f4
    ctx->pc = 0x25912cu;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[12], ctx->f[4])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
label_259130:
    // 0x259130: 0x0  nop
    ctx->pc = 0x259130u;
    // NOP
label_259134:
    // 0x259134: 0x45030001  bc1tl       . + 4 + (0x1 << 2)
label_259138:
    if (ctx->pc == 0x259138u) {
        ctx->pc = 0x259138u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259134u;
        // 0x259138: 0x46002306  mov.s       $f12, $f4 (Delay Slot)
        ctx->f[12] = FPU_MOV_S(ctx->f[4]);
        ctx->in_delay_slot = false;
        ctx->pc = 0x25913Cu;
        goto label_25913c;
    }
    ctx->pc = 0x259134u;
    {
        const bool branch_taken_0x259134 = ((ctx->fcr31 & 0x800000));
        if (branch_taken_0x259134) {
            ctx->pc = 0x259138u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x259134u;
            // 0x259138: 0x46002306  mov.s       $f12, $f4 (Delay Slot)
            ctx->f[12] = FPU_MOV_S(ctx->f[4]);
            ctx->in_delay_slot = false;
            ctx->pc = 0x25913Cu;
            goto label_25913c;
        }
    }
    ctx->pc = 0x25913Cu;
label_25913c:
    // 0x25913c: 0xc097fe2  jal         func_25FF88
label_259140:
    if (ctx->pc == 0x259140u) {
        ctx->pc = 0x259140u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25913Cu;
        // 0x259140: 0x8e040028  lw          $a0, 0x28($s0) (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259144u;
        goto label_259144;
    }
    ctx->pc = 0x25913Cu;
    SET_GPR_U32(ctx, 31, 0x259144u);
    ctx->pc = 0x259140u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x25913Cu;
    // 0x259140: 0x8e040028  lw          $a0, 0x28($s0) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x25FF88u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x25FF88u, 0x25913Cu, 0x259144u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x259144u;
label_259144:
    // 0x259144: 0xc620000c  lwc1        $f0, 0xC($s1)
    ctx->pc = 0x259144u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 17), 12)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
label_259148:
    // 0x259148: 0x46140034  c.lt.s      $f0, $f20
    ctx->pc = 0x259148u;
    ctx->fcr31 = (FPU_C_OLT_S(ctx->f[0], ctx->f[20])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);
label_25914c:
    // 0x25914c: 0x0  nop
    ctx->pc = 0x25914cu;
    // NOP
label_259150:
    // 0x259150: 0x45020011  bc1fl       . + 4 + (0x11 << 2)
label_259154:
    if (ctx->pc == 0x259154u) {
        ctx->pc = 0x259154u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259150u;
        // 0x259154: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259158u;
        goto label_259158;
    }
    ctx->pc = 0x259150u;
    {
        const bool branch_taken_0x259150 = (!(ctx->fcr31 & 0x800000));
        if (branch_taken_0x259150) {
            ctx->pc = 0x259154u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x259150u;
            // 0x259154: 0x7bb00050  lq          $s0, 0x50($sp) (Delay Slot)
            SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x259198u;
            goto label_259198;
        }
    }
    ctx->pc = 0x259158u;
label_259158:
    // 0x259158: 0x8e020034  lw          $v0, 0x34($s0)
    ctx->pc = 0x259158u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 52)));
label_25915c:
    // 0x25915c: 0x5040000c  beql        $v0, $zero, . + 4 + (0xC << 2)
label_259160:
    if (ctx->pc == 0x259160u) {
        ctx->pc = 0x259160u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25915Cu;
        // 0x259160: 0xae000034  sw          $zero, 0x34($s0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 16), 52), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259164u;
        goto label_259164;
    }
    ctx->pc = 0x25915Cu;
    {
        const bool branch_taken_0x25915c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x25915c) {
            ctx->pc = 0x259160u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x25915Cu;
            // 0x259160: 0xae000034  sw          $zero, 0x34($s0) (Delay Slot)
            WRITE32(ADD32(GPR_U32(ctx, 16), 52), GPR_U32(ctx, 0));
            ctx->in_delay_slot = false;
            ctx->pc = 0x259190u;
            goto label_259190;
        }
    }
    ctx->pc = 0x259164u;
label_259164:
    // 0x259164: 0x8e060028  lw          $a2, 0x28($s0)
    ctx->pc = 0x259164u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 40)));
label_259168:
    // 0x259168: 0x10c00006  beqz        $a2, . + 4 + (0x6 << 2)
label_25916c:
    if (ctx->pc == 0x25916Cu) {
        ctx->pc = 0x25916Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259168u;
        // 0x25916c: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259170u;
        goto label_259170;
    }
    ctx->pc = 0x259168u;
    {
        const bool branch_taken_0x259168 = (GPR_U64(ctx, 6) == GPR_U64(ctx, 0));
        ctx->pc = 0x25916Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x259168u;
        // 0x25916c: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
        ctx->in_delay_slot = false;
        if (branch_taken_0x259168) {
            ctx->pc = 0x259184u;
            goto label_259184;
        }
    }
    ctx->pc = 0x259170u;
label_259170:
    // 0x259170: 0x8cc20004  lw          $v0, 0x4($a2)
    ctx->pc = 0x259170u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 4)));
label_259174:
    // 0x259174: 0x84440008  lh          $a0, 0x8($v0)
    ctx->pc = 0x259174u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 2), 8)));
label_259178:
    // 0x259178: 0x8c43000c  lw          $v1, 0xC($v0)
    ctx->pc = 0x259178u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 12)));
label_25917c:
    // 0x25917c: 0x60f809  jalr        $v1
label_259180:
    if (ctx->pc == 0x259180u) {
        ctx->pc = 0x259180u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25917Cu;
        // 0x259180: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x259184u;
        goto label_259184;
    }
    ctx->pc = 0x25917Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 3);
        SET_GPR_U32(ctx, 31, 0x259184u);
        ctx->pc = 0x259180u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x25917Cu;
        // 0x259180: 0xc42021  addu        $a0, $a2, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x25917Cu, 0x259184u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x259184u;
label_259184:
    // 0x259184: 0xae000028  sw          $zero, 0x28($s0)
    ctx->pc = 0x259184u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 40), GPR_U32(ctx, 0));
label_259188:
    // 0x259188: 0xae00002c  sw          $zero, 0x2C($s0)
    ctx->pc = 0x259188u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 44), GPR_U32(ctx, 0));
label_25918c:
    // 0x25918c: 0xae000034  sw          $zero, 0x34($s0)
    ctx->pc = 0x25918cu;
    WRITE32(ADD32(GPR_U32(ctx, 16), 52), GPR_U32(ctx, 0));
label_259190:
    // 0x259190: 0xae000030  sw          $zero, 0x30($s0)
    ctx->pc = 0x259190u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 0));
label_259194:
    // 0x259194: 0x7bb00050  lq          $s0, 0x50($sp)
    ctx->pc = 0x259194u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 80)));
label_259198:
    // 0x259198: 0x7bb10040  lq          $s1, 0x40($sp)
    ctx->pc = 0x259198u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 64)));
label_25919c:
    // 0x25919c: 0x7bb20030  lq          $s2, 0x30($sp)
    ctx->pc = 0x25919cu;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 48)));
label_2591a0:
    // 0x2591a0: 0x7bb30020  lq          $s3, 0x20($sp)
    ctx->pc = 0x2591a0u;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 32)));
label_2591a4:
    // 0x2591a4: 0xdfbf0010  ld          $ra, 0x10($sp)
    ctx->pc = 0x2591a4u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
label_2591a8:
    // 0x2591a8: 0xc7b60070  lwc1        $f22, 0x70($sp)
    ctx->pc = 0x2591a8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 112)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[22] = f; }
label_2591ac:
    // 0x2591ac: 0xc7b50068  lwc1        $f21, 0x68($sp)
    ctx->pc = 0x2591acu;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 104)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[21] = f; }
label_2591b0:
    // 0x2591b0: 0xc7b40060  lwc1        $f20, 0x60($sp)
    ctx->pc = 0x2591b0u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 29), 96)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[20] = f; }
label_2591b4:
    // 0x2591b4: 0x3e00008  jr          $ra
label_2591b8:
    if (ctx->pc == 0x2591B8u) {
        ctx->pc = 0x2591B8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2591B4u;
        // 0x2591b8: 0x27bd0080  addiu       $sp, $sp, 0x80 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 128));
        ctx->in_delay_slot = false;
        ctx->pc = 0x2591BCu;
        goto label_fallthrough_0x2591b4;
    }
    ctx->pc = 0x2591B4u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x2591B8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2591B4u;
        // 0x2591b8: 0x27bd0080  addiu       $sp, $sp, 0x80 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 128));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x2591B4u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
label_fallthrough_0x2591b4:
    ctx->pc = 0x2591BCu;
}
