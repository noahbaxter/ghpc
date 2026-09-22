// Override of work/output/EndDrawing__5PsRnd_0x1c0070.cpp (PsRnd::EndDrawing,
// 0x1c0070 - 0x1c00a4).
//
// Why: this is the guest's own frame boundary, and the Rnd seam closes its
// scene frame here rather than at vblank. A vblank lands wherever the guest
// happens to be, so closing there split one guest frame into two published
// halves, each presented as a frame with part of the scene missing. By the
// time EndDrawing runs every DrawFaces of the frame has submitted.
//
// Hooked on fresh entry only, below the resume switch: a call that yields
// inside UpdateStats or StopDrawing re-enters through a label and must not
// close a second frame. The generated body is otherwise verbatim.
#include <stdexcept>
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

// Function: EndDrawing__5PsRnd
// Address: 0x1c0070 - 0x1c00a4
void EndDrawing__5PsRnd_0x1c0070(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("EndDrawing__5PsRnd_0x1c0070");
#endif

    switch (ctx->pc) {
        case 0x1c0084u: goto label_1c0084;
        case 0x1c008cu: goto label_1c008c;
        case 0x1c0094u: goto label_1c0094;
        default: break;
    }

    ctx->pc = 0x1c0070u;

    ghpc::scene::guestFrameEnd();

    // 0x1c0070: 0x27bdffe0  addiu       $sp, $sp, -0x20
    ctx->pc = 0x1c0070u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967264));
    // 0x1c0074: 0x7fb00010  sq          $s0, 0x10($sp)
    ctx->pc = 0x1c0074u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 16), GPR_VEC(ctx, 16));
    // 0x1c0078: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x1c0078u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x1c007c: 0xc07002a  jal         func_1C00A8
    ctx->pc = 0x1C007Cu;
    SET_GPR_U32(ctx, 31, 0x1C0084u);
    ctx->pc = 0x1C0080u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C007Cu;
    // 0x1c0080: 0x80802d  daddu       $s0, $a0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C00A8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C00A8u, 0x1C007Cu, 0x1C0084u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C0084u;
label_1c0084:
    // 0x1c0084: 0xc0807ec  jal         func_201FB0
    ctx->pc = 0x1C0084u;
    SET_GPR_U32(ctx, 31, 0x1C008Cu);
    ctx->pc = 0x1C0088u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C0084u;
    // 0x1c0088: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x201FB0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x201FB0u, 0x1C0084u, 0x1C008Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C008Cu;
label_1c008c:
    // 0x1c008c: 0xc06f922  jal         func_1BE488
    ctx->pc = 0x1C008Cu;
    SET_GPR_U32(ctx, 31, 0x1C0094u);
    ctx->pc = 0x1C0090u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C008Cu;
    // 0x1c0090: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BE488u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BE488u, 0x1C008Cu, 0x1C0094u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C0094u;
label_1c0094:
    // 0x1c0094: 0x7bb00010  lq          $s0, 0x10($sp)
    ctx->pc = 0x1c0094u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x1c0098: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x1c0098u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x1c009c: 0x3e00008  jr          $ra
    ctx->pc = 0x1C009Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C00A0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C009Cu;
        // 0x1c00a0: 0x27bd0020  addiu       $sp, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C009Cu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C00A4u;
}
