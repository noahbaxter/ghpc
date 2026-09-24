// Override of work/output/SyncVideoOptions__7Options_0x10f5c0.cpp
// (Options::SyncVideoOptions, 0x10f5c0 - 0x10f600).
//
// Why: widescreen on by default. The body passes mWideScreen (+0x38) to
// Options::SetWideScreen, which sets Rnd's aspect and squeezes every camera
// for 16:9. This runs whenever options are applied, including from a save,
// so a profile saved with it off still starts widescreen. ghpc.ini
// [display] widescreen = 0 leaves the game's own value alone.
#include <stdexcept>
#include "ghpc_config.h"
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: SyncVideoOptions__7Options
// Address: 0x10f5c0 - 0x10f600
void SyncVideoOptions__7Options_0x10f5c0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("SyncVideoOptions__7Options_0x10f5c0");
#endif

    switch (ctx->pc) {
        case 0x10f5d8u: goto label_10f5d8;
        case 0x10f5e4u: goto label_10f5e4;
        default: break;
    }

    // Fresh entry only. $a0 is `this`.
    if (ghpc::settings().getBool("display", "widescreen", true)) {
        WRITE32(GPR_U32(ctx, 4) + 0x38u, 1u);
    }

    ctx->pc = 0x10f5c0u;

    // 0x10f5c0: 0x27bdffe0  addiu       $sp, $sp, -0x20
    ctx->pc = 0x10f5c0u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967264));
    // 0x10f5c4: 0x7fb00010  sq          $s0, 0x10($sp)
    ctx->pc = 0x10f5c4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 16), GPR_VEC(ctx, 16));
    // 0x10f5c8: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x10f5c8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x10f5cc: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x10f5ccu;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x10f5d0: 0xc043e28  jal         func_10F8A0
    ctx->pc = 0x10F5D0u;
    SET_GPR_U32(ctx, 31, 0x10F5D8u);
    ctx->pc = 0x10F5D4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x10F5D0u;
    // 0x10f5d4: 0x8e050038  lw          $a1, 0x38($s0) (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 56)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x10F8A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x10F8A0u, 0x10F5D0u, 0x10F5D8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x10F5D8u;
label_10f5d8:
    // 0x10f5d8: 0xc60c003c  lwc1        $f12, 0x3C($s0)
    ctx->pc = 0x10f5d8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 60)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[12] = f; }
    // 0x10f5dc: 0xc043e48  jal         func_10F920
    ctx->pc = 0x10F5DCu;
    SET_GPR_U32(ctx, 31, 0x10F5E4u);
    ctx->pc = 0x10F5E0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x10F5DCu;
    // 0x10f5e0: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x10F920u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x10F920u, 0x10F5DCu, 0x10F5E4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x10F5E4u;
label_10f5e4:
    // 0x10f5e4: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x10f5e4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x10f5e8: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x10f5e8u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x10f5ec: 0x8c432c8c  lw          $v1, 0x2C8C($v0)
    ctx->pc = 0x10f5ecu;
    SET_GPR_S32(ctx, 3, (int32_t)FAST_READ32(0x4F2C8Cu));
    // 0x10f5f0: 0xae030034  sw          $v1, 0x34($s0)
    ctx->pc = 0x10f5f0u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 52), GPR_U32(ctx, 3));
    // 0x10f5f4: 0x7bb00010  lq          $s0, 0x10($sp)
    ctx->pc = 0x10f5f4u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x10f5f8: 0x3e00008  jr          $ra
    ctx->pc = 0x10F5F8u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x10F5FCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10F5F8u;
        // 0x10f5fc: 0x27bd0020  addiu       $sp, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x10F5F8u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x10F600u;
}
