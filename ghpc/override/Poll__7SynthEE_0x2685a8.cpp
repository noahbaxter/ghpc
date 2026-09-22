// Override of work/output/Poll__7SynthEE_0x2685a8.cpp (SynthEE::Poll,
// 0x2685a8 - 0x268760).
//
// Why: the IOP half of the synth has to call the EE, and on hardware it does
// that whenever it likes, through the EE's own RPC server 0x75433179 into
// CtlDispatch__7SynthEE. The port's only other route back is the result of an
// EE flush, and CtlClientPoll flushes only when its queue is non-empty, so a
// stream asking for data would wait on the EE having something to say.
//
// SynthEE::Poll runs once a frame on the main thread and goes on to call
// StreamEE::PollAll, which answers data requests, then CtlClientPoll, which
// flushes the answers. Delivering here means a request raised by the IOP is
// served in the same frame, at a point where no guest heap call is in flight.
//
// Hooked on fresh entry only, below the resume switch. The generated body is
// otherwise verbatim.
#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ghpc_synth_bridge.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: Poll__7SynthEE
// Address: 0x2685a8 - 0x268760
void Poll__7SynthEE_0x2685a8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Poll__7SynthEE_0x2685a8");
#endif

    switch (ctx->pc) {
        case 0x2685e0u: goto label_2685e0;
        case 0x2685e8u: goto label_2685e8;
        case 0x268624u: goto label_268624;
        case 0x26862cu: goto label_26862c;
        case 0x268634u: goto label_268634;
        case 0x268648u: goto label_268648;
        case 0x268650u: goto label_268650;
        case 0x268660u: goto label_268660;
        case 0x268668u: goto label_268668;
        case 0x268670u: goto label_268670;
        case 0x268688u: goto label_268688;
        case 0x2686b0u: goto label_2686b0;
        case 0x268704u: goto label_268704;
        case 0x26870cu: goto label_26870c;
        default: break;
    }

    ctx->pc = 0x2685a8u;

    ghpc::synth::deliverReplies(rdram, ctx, runtime, 0x2685a8u);

    // 0x2685a8: 0x27bdf780  addiu       $sp, $sp, -0x880
    ctx->pc = 0x2685a8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294965120));
    // 0x2685ac: 0x7fb20850  sq          $s2, 0x850($sp)
    ctx->pc = 0x2685acu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 2128), GPR_VEC(ctx, 18));
    // 0x2685b0: 0x3c120050  lui         $s2, 0x50
    ctx->pc = 0x2685b0u;
    SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)80 << 16));
    // 0x2685b4: 0x7fb00870  sq          $s0, 0x870($sp)
    ctx->pc = 0x2685b4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 2160), GPR_VEC(ctx, 16));
    // 0x2685b8: 0x7fb10860  sq          $s1, 0x860($sp)
    ctx->pc = 0x2685b8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 2144), GPR_VEC(ctx, 17));
    // 0x2685bc: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x2685bcu;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x2685c0: 0x8e42a654  lw          $v0, -0x59AC($s2)
    ctx->pc = 0x2685c0u;
    SET_GPR_S32(ctx, 2, (int32_t)FAST_READ32(0x4FA654u));
    // 0x2685c4: 0x3c110050  lui         $s1, 0x50
    ctx->pc = 0x2685c4u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)80 << 16));
    // 0x2685c8: 0x1440000a  bnez        $v0, . + 4 + (0xA << 2)
    ctx->pc = 0x2685C8u;
    {
        const bool branch_taken_0x2685c8 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x2685CCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2685C8u;
        // 0x2685cc: 0xffbf0840  sd          $ra, 0x840($sp) (Delay Slot)
        WRITE64(ADD32(GPR_U32(ctx, 29), 2112), GPR_U64(ctx, 31));
        ctx->in_delay_slot = false;
        if (branch_taken_0x2685c8) {
            ctx->pc = 0x2685F4u;
            goto label_2685f4;
        }
    }
    ctx->pc = 0x2685D0u;
    // 0x2685d0: 0x3c05004b  lui         $a1, 0x4B
    ctx->pc = 0x2685d0u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)75 << 16));
    // 0x2685d4: 0x27a40830  addiu       $a0, $sp, 0x830
    ctx->pc = 0x2685d4u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 2096));
    // 0x2685d8: 0xc0c9bc0  jal         func_326F00
    ctx->pc = 0x2685D8u;
    SET_GPR_U32(ctx, 31, 0x2685E0u);
    ctx->pc = 0x2685DCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x2685D8u;
    // 0x2685dc: 0x24a5f8e8  addiu       $a1, $a1, -0x718 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294965480));
    ctx->in_delay_slot = false;
    ctx->pc = 0x326F00u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x326F00u, 0x2685D8u, 0x2685E0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x2685E0u;
label_2685e0:
    // 0x2685e0: 0xc0bcbaa  jal         func_2F2EA8
    ctx->pc = 0x2685E0u;
    SET_GPR_U32(ctx, 31, 0x2685E8u);
    ctx->pc = 0x2685E4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x2685E0u;
    // 0x2685e4: 0x8fa40830  lw          $a0, 0x830($sp) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 2096)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2F2EA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2F2EA8u, 0x2685E0u, 0x2685E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x2685E8u;
label_2685e8:
    // 0x2685e8: 0x24030001  addiu       $v1, $zero, 0x1
    ctx->pc = 0x2685e8u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x2685ec: 0xae22a650  sw          $v0, -0x59B0($s1)
    ctx->pc = 0x2685ecu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 4294944336), GPR_U32(ctx, 2));
    // 0x2685f0: 0xae43a654  sw          $v1, -0x59AC($s2)
    ctx->pc = 0x2685f0u;
    WRITE32(ADD32(GPR_U32(ctx, 18), 4294944340), GPR_U32(ctx, 3));
label_2685f4:
    // 0x2685f4: 0x8e24a650  lw          $a0, -0x59B0($s1)
    ctx->pc = 0x2685f4u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4294944336)));
    // 0x2685f8: 0x10800008  beqz        $a0, . + 4 + (0x8 << 2)
    ctx->pc = 0x2685F8u;
    {
        const bool branch_taken_0x2685f8 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x2685FCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2685F8u;
        // 0x2685fc: 0xafa40000  sw          $a0, 0x0($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x2685f8) {
            ctx->pc = 0x26861Cu;
            goto label_26861c;
        }
    }
    ctx->pc = 0x268600u;
    // 0x268600: 0x8c820024  lw          $v0, 0x24($a0)
    ctx->pc = 0x268600u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 36)));
    // 0x268604: 0x24030001  addiu       $v1, $zero, 0x1
    ctx->pc = 0x268604u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x268608: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x268608u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
    // 0x26860c: 0x14430003  bne         $v0, $v1, . + 4 + (0x3 << 2)
    ctx->pc = 0x26860Cu;
    {
        const bool branch_taken_0x26860c = (GPR_U64(ctx, 2) != GPR_U64(ctx, 3));
        ctx->pc = 0x268610u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x26860Cu;
        // 0x268610: 0xac820024  sw          $v0, 0x24($a0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 4), 36), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x26860c) {
            ctx->pc = 0x26861Cu;
            goto label_26861c;
        }
    }
    ctx->pc = 0x268614u;
    // 0x268614: 0x40024800  mfc0        $v0, Count
    ctx->pc = 0x268614u;
    SET_GPR_S32(ctx, 2, (int32_t)ctx->cop0_count);
    // 0x268618: 0xac820000  sw          $v0, 0x0($a0)
    ctx->pc = 0x268618u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
label_26861c:
    // 0x26861c: 0xc098244  jal         func_260910
    ctx->pc = 0x26861Cu;
    SET_GPR_U32(ctx, 31, 0x268624u);
    ctx->pc = 0x268620u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x26861Cu;
    // 0x268620: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x260910u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x260910u, 0x26861Cu, 0x268624u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268624u;
label_268624:
    // 0x268624: 0xc0bae6a  jal         func_2EB9A8
    ctx->pc = 0x268624u;
    SET_GPR_U32(ctx, 31, 0x26862Cu);
    ctx->pc = 0x2EB9A8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EB9A8u, 0x268624u, 0x26862Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x26862Cu;
label_26862c:
    // 0x26862c: 0xc09b932  jal         func_26E4C8
    ctx->pc = 0x26862Cu;
    SET_GPR_U32(ctx, 31, 0x268634u);
    ctx->pc = 0x26E4C8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x26E4C8u, 0x26862Cu, 0x268634u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268634u;
label_268634:
    // 0x268634: 0x820240a0  lb          $v0, 0x40A0($s0)
    ctx->pc = 0x268634u;
    SET_GPR_S32(ctx, 2, (int8_t)READ8(ADD32(GPR_U32(ctx, 16), 16544)));
    // 0x268638: 0x10400009  beqz        $v0, . + 4 + (0x9 << 2)
    ctx->pc = 0x268638u;
    {
        const bool branch_taken_0x268638 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x26863Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x268638u;
        // 0x26863c: 0x27a40010  addiu       $a0, $sp, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x268638) {
            ctx->pc = 0x268660u;
            goto label_268660;
        }
    }
    ctx->pc = 0x268640u;
    // 0x268640: 0xc0c78fa  jal         func_31E3E8
    ctx->pc = 0x268640u;
    SET_GPR_U32(ctx, 31, 0x268648u);
    ctx->pc = 0x268644u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x268640u;
    // 0x268644: 0x260540a0  addiu       $a1, $s0, 0x40A0 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 16), 16544));
    ctx->in_delay_slot = false;
    ctx->pc = 0x31E3E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x31E3E8u, 0x268640u, 0x268648u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268648u;
label_268648:
    // 0x268648: 0xc0c7bae  jal         func_31EEB8
    ctx->pc = 0x268648u;
    SET_GPR_U32(ctx, 31, 0x268650u);
    ctx->pc = 0x26864Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x268648u;
    // 0x26864c: 0x27a40010  addiu       $a0, $sp, 0x10 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x31EEB8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x31EEB8u, 0x268648u, 0x268650u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268650u;
label_268650:
    // 0x268650: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x268650u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
    // 0x268654: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x268654u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x268658: 0xc0baf6a  jal         func_2EBDA8
    ctx->pc = 0x268658u;
    SET_GPR_U32(ctx, 31, 0x268660u);
    ctx->pc = 0x26865Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x268658u;
    // 0x26865c: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x268658u, 0x268660u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268660u;
label_268660:
    // 0x268660: 0xc09b5d4  jal         func_26D750
    ctx->pc = 0x268660u;
    SET_GPR_U32(ctx, 31, 0x268668u);
    ctx->pc = 0x26D750u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x26D750u, 0x268660u, 0x268668u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268668u;
label_268668:
    // 0x268668: 0xc09a6f4  jal         func_269BD0
    ctx->pc = 0x268668u;
    SET_GPR_U32(ctx, 31, 0x268670u);
    ctx->pc = 0x269BD0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x269BD0u, 0x268668u, 0x268670u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268670u;
label_268670:
    // 0x268670: 0x26074114  addiu       $a3, $s0, 0x4114
    ctx->pc = 0x268670u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 16), 16660));
    // 0x268674: 0x26034104  addiu       $v1, $s0, 0x4104
    ctx->pc = 0x268674u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 16), 16644));
    // 0x268678: 0x2606411c  addiu       $a2, $s0, 0x411C
    ctx->pc = 0x268678u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 16), 16668));
    // 0x26867c: 0xe0202d  daddu       $a0, $a3, $zero
    ctx->pc = 0x26867cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 7) + (uint64_t)GPR_U64(ctx, 0));
    // 0x268680: 0x60282d  daddu       $a1, $v1, $zero
    ctx->pc = 0x268680u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
    // 0x268684: 0x0  nop
    ctx->pc = 0x268684u;
    // NOP
label_268688:
    // 0x268688: 0x8ca20000  lw          $v0, 0x0($a1)
    ctx->pc = 0x268688u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x26868c: 0xac820000  sw          $v0, 0x0($a0)
    ctx->pc = 0x26868cu;
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    // 0x268690: 0x24840004  addiu       $a0, $a0, 0x4
    ctx->pc = 0x268690u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4));
    // 0x268694: 0x86102a  slt         $v0, $a0, $a2
    ctx->pc = 0x268694u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 4) < (int64_t)GPR_S64(ctx, 6)) ? 1 : 0);
    // 0x268698: 0x1440fffb  bnez        $v0, . + 4 + (-0x5 << 2)
    ctx->pc = 0x268698u;
    {
        const bool branch_taken_0x268698 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x26869Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x268698u;
        // 0x26869c: 0x24a50004  addiu       $a1, $a1, 0x4 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x268698) {
            ctx->pc = 0x268688u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_268688;
        }
    }
    ctx->pc = 0x2686A0u;
    // 0x2686a0: 0x24e60008  addiu       $a2, $a3, 0x8
    ctx->pc = 0x2686a0u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 7), 8));
    // 0x2686a4: 0x24650008  addiu       $a1, $v1, 0x8
    ctx->pc = 0x2686a4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 8));
    // 0x2686a8: 0x26044124  addiu       $a0, $s0, 0x4124
    ctx->pc = 0x2686a8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 16676));
    // 0x2686ac: 0x24070002  addiu       $a3, $zero, 0x2
    ctx->pc = 0x2686acu;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
label_2686b0:
    // 0x2686b0: 0x8c820000  lw          $v0, 0x0($a0)
    ctx->pc = 0x2686b0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x2686b4: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x2686b4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
    // 0x2686b8: 0x2843003c  slti        $v1, $v0, 0x3C
    ctx->pc = 0x2686b8u;
    SET_GPR_U64(ctx, 3, ((int64_t)GPR_S64(ctx, 2) < (int64_t)(int32_t)60) ? 1 : 0);
    // 0x2686bc: 0x10600006  beqz        $v1, . + 4 + (0x6 << 2)
    ctx->pc = 0x2686BCu;
    {
        const bool branch_taken_0x2686bc = (GPR_U64(ctx, 3) == GPR_U64(ctx, 0));
        ctx->pc = 0x2686C0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2686BCu;
        // 0x2686c0: 0xac820000  sw          $v0, 0x0($a0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x2686bc) {
            ctx->pc = 0x2686D8u;
            goto label_2686d8;
        }
    }
    ctx->pc = 0x2686C4u;
    // 0x2686c4: 0x8ca30000  lw          $v1, 0x0($a1)
    ctx->pc = 0x2686c4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x2686c8: 0x8cc20000  lw          $v0, 0x0($a2)
    ctx->pc = 0x2686c8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 0)));
    // 0x2686cc: 0x43102b  sltu        $v0, $v0, $v1
    ctx->pc = 0x2686ccu;
    SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 2) < (uint64_t)GPR_U64(ctx, 3)) ? 1 : 0);
    // 0x2686d0: 0x50400005  beql        $v0, $zero, . + 4 + (0x5 << 2)
    ctx->pc = 0x2686D0u;
    {
        const bool branch_taken_0x2686d0 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x2686d0) {
            ctx->pc = 0x2686D4u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x2686D0u;
            // 0x2686d4: 0x24e70001  addiu       $a3, $a3, 0x1 (Delay Slot)
            SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 1));
            ctx->in_delay_slot = false;
            ctx->pc = 0x2686E8u;
            goto label_2686e8;
        }
    }
    ctx->pc = 0x2686D8u;
label_2686d8:
    // 0x2686d8: 0xac800000  sw          $zero, 0x0($a0)
    ctx->pc = 0x2686d8u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 0));
    // 0x2686dc: 0x8ca20000  lw          $v0, 0x0($a1)
    ctx->pc = 0x2686dcu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x2686e0: 0xacc20000  sw          $v0, 0x0($a2)
    ctx->pc = 0x2686e0u;
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x2686e4: 0x24e70001  addiu       $a3, $a3, 0x1
    ctx->pc = 0x2686e4u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 1));
label_2686e8:
    // 0x2686e8: 0x24c60004  addiu       $a2, $a2, 0x4
    ctx->pc = 0x2686e8u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), 4));
    // 0x2686ec: 0x24a50004  addiu       $a1, $a1, 0x4
    ctx->pc = 0x2686ecu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4));
    // 0x2686f0: 0x28e20004  slti        $v0, $a3, 0x4
    ctx->pc = 0x2686f0u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 7) < (int64_t)(int32_t)4) ? 1 : 0);
    // 0x2686f4: 0x1440ffee  bnez        $v0, . + 4 + (-0x12 << 2)
    ctx->pc = 0x2686F4u;
    {
        const bool branch_taken_0x2686f4 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x2686F8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x2686F4u;
        // 0x2686f8: 0x24840004  addiu       $a0, $a0, 0x4 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x2686f4) {
            ctx->pc = 0x2686B0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_2686b0;
        }
    }
    ctx->pc = 0x2686FCu;
    // 0x2686fc: 0xc09a9a0  jal         func_26A680
    ctx->pc = 0x2686FCu;
    SET_GPR_U32(ctx, 31, 0x268704u);
    ctx->pc = 0x26A680u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x26A680u, 0x2686FCu, 0x268704u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x268704u;
label_268704:
    // 0x268704: 0xc0bae90  jal         func_2EBA40
    ctx->pc = 0x268704u;
    SET_GPR_U32(ctx, 31, 0x26870Cu);
    ctx->pc = 0x2EBA40u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBA40u, 0x268704u, 0x26870Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x26870Cu;
label_26870c:
    // 0x26870c: 0x8fa50000  lw          $a1, 0x0($sp)
    ctx->pc = 0x26870cu;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x268710: 0x10a0000e  beqz        $a1, . + 4 + (0xE << 2)
    ctx->pc = 0x268710u;
    {
        const bool branch_taken_0x268710 = (GPR_U64(ctx, 5) == GPR_U64(ctx, 0));
        ctx->pc = 0x268714u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x268710u;
        // 0x268714: 0x7bb00870  lq          $s0, 0x870($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 2160)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x268710) {
            ctx->pc = 0x26874Cu;
            goto label_26874c;
        }
    }
    ctx->pc = 0x268718u;
    // 0x268718: 0x8ca20024  lw          $v0, 0x24($a1)
    ctx->pc = 0x268718u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 36)));
    // 0x26871c: 0x2442ffff  addiu       $v0, $v0, -0x1
    ctx->pc = 0x26871cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967295));
    // 0x268720: 0x1440000a  bnez        $v0, . + 4 + (0xA << 2)
    ctx->pc = 0x268720u;
    {
        const bool branch_taken_0x268720 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x268724u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x268720u;
        // 0x268724: 0xaca20024  sw          $v0, 0x24($a1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 5), 36), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x268720) {
            ctx->pc = 0x26874Cu;
            goto label_26874c;
        }
    }
    ctx->pc = 0x268728u;
    // 0x268728: 0x40024800  mfc0        $v0, Count
    ctx->pc = 0x268728u;
    SET_GPR_S32(ctx, 2, (int32_t)ctx->cop0_count);
    // 0x26872c: 0x8ca40000  lw          $a0, 0x0($a1)
    ctx->pc = 0x26872cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x268730: 0xdca30008  ld          $v1, 0x8($a1)
    ctx->pc = 0x268730u;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 5), 8)));
    // 0x268734: 0x441023  subu        $v0, $v0, $a0
    ctx->pc = 0x268734u;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x268738: 0x2103c  dsll32      $v0, $v0, 0
    ctx->pc = 0x268738u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << (32 + 0));
    // 0x26873c: 0x2103e  dsrl32      $v0, $v0, 0
    ctx->pc = 0x26873cu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) >> (32 + 0));
    // 0x268740: 0x62182d  daddu       $v1, $v1, $v0
    ctx->pc = 0x268740u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 2));
    // 0x268744: 0xfca30008  sd          $v1, 0x8($a1)
    ctx->pc = 0x268744u;
    WRITE64(ADD32(GPR_U32(ctx, 5), 8), GPR_U64(ctx, 3));
    // 0x268748: 0x7bb00870  lq          $s0, 0x870($sp)
    ctx->pc = 0x268748u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 2160)));
label_26874c:
    // 0x26874c: 0x7bb10860  lq          $s1, 0x860($sp)
    ctx->pc = 0x26874cu;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 2144)));
    // 0x268750: 0x7bb20850  lq          $s2, 0x850($sp)
    ctx->pc = 0x268750u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 2128)));
    // 0x268754: 0xdfbf0840  ld          $ra, 0x840($sp)
    ctx->pc = 0x268754u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 2112)));
    // 0x268758: 0x3e00008  jr          $ra
    ctx->pc = 0x268758u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x26875Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x268758u;
        // 0x26875c: 0x27bd0880  addiu       $sp, $sp, 0x880 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 2176));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x268758u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x268760u;
}
