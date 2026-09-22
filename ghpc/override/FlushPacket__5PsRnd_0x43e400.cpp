// Override of work/output/FlushPacket__5PsRnd_0x43e400.cpp
// (PsRnd::FlushPacket, 0x43e400 - 0x43e50c).
//
// Why: this is the one chokepoint where every built packet is kicked to DMA
// channel 8, so returning early here kills the GIF path and with it VU1, VIF1,
// the MFIFO spin and the software rasteriser, in one place.
//
// GHPC_RND_FLUSH_OFF=1 WEDGES THE GUEST. It is kept as a recorded dead end,
// not as a usable arm. Do not read a number out of it.
//
// It was meant to be the R2 ceiling measurement, the counterpart of
// GHPC_VU1_OFF: return early, no packet reaches DMA, and see what the EE holds
// once the GIF path and the software rasteriser are gone. Measured 2026-09-20,
// gameplay drive, 95s:
//
//   flush on    ui advancing, uirate 0.430 on main_screen, 0.050 in a song
//   flush off   ui frozen at 0.505 for 90s, uirate 0.000, eerate 100.0%
//
// eerate read 100.0% of realtime while the game's own clock had not moved in
// ninety seconds. The EE spins at full rate making no progress, and every
// screen after main_screen is unreachable. A metric that only watches cycles
// retiring calls that a total win. It is a corpse.
//
// The cause is this override, not the seam. The plan in notes/rnd-seam.md says
// to keep FlushPacket's close/swap/reopen bookkeeping and drop only the MFIFO
// spin and DmaPacket::Send. A blunt early return drops the bookkeeping too, so
// the packet cursor never resets and the guest waits forever for buffer space.
// A surgical version has to keep the calls at 0x43e414, 0x43e434, 0x43e4b0 and
// 0x43e4d8 and skip only the MMIO writes at label_43e43c and the spin at
// label_43e478.
//
// Doing that at the runtime's DMA layer instead is not the shortcut it looks
// like: the guest polls the MFIFO ring pointers directly, so dropping the
// transfer means faking the ring state, which is a second way to build a
// corpse.
//
// Whatever replaces this, the liveness check is not optional. `[ghpc/clock]`
// `game` and `beat` have to keep advancing on game_screen, because those are
// the chart clocks the notes are drawn from.
//
// The early return skips the prologue rather than unwinding it. The generated
// body opens with `addiu $sp, $sp, -0x10` and `sd $ra, 0($sp)`, so leaving
// both undone and jumping straight to $ra is balanced; there is nothing to pop
// because nothing was pushed.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: FlushPacket__5PsRnd
// Address: 0x43e400 - 0x43e50c
void FlushPacket__5PsRnd_0x43e400(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FlushPacket__5PsRnd_0x43e400");
#endif

    static const bool s_flushOff = std::getenv("GHPC_RND_FLUSH_OFF") != nullptr;
    if (s_flushOff) {
        // Nothing below runs, so none of the resume labels can be reached and
        // the switch above has nothing to dispatch to.
        ctx->pc = GPR_U32(ctx, 31);
        return;
    }

    switch (ctx->pc) {
        case 0x43e41cu: goto label_43e41c;
        case 0x43e43cu: goto label_43e43c;
        case 0x43e478u: goto label_43e478;
        case 0x43e4b8u: goto label_43e4b8;
        case 0x43e4e0u: goto label_43e4e0;
        case 0x43e4f8u: goto label_43e4f8;
        case 0x43e500u: goto label_43e500;
        default: break;
    }

    // GHPC_DRAWPATH_CENSUS=N: who flushes, by return address, as a table every
    // N calls. Every draw path ends here, so a caller the Rnd seam does not
    // hook is content --native drops. ghpc/scripts/drawpaths.py names them.
    {
        static const long s_every = []() {
            const char* e = std::getenv("GHPC_DRAWPATH_CENSUS");
            return e ? std::atol(e) : 0L;
        }();
        if (s_every > 0) {
            static std::map<uint32_t, uint64_t> s_byRa;
            static long s_calls = 0;
            ++s_byRa[GPR_U32(ctx, 31)];
            if (++s_calls % s_every == 0) {
                for (const auto& kv : s_byRa)
                    std::fprintf(stderr, "[ghpc/drawpath] flush ra=0x%08x n=%llu\n", kv.first,
                                 (unsigned long long)kv.second);
            }
        }
    }

    ctx->pc = 0x43e400u;

    // 0x43e400: 0x27bdfff0  addiu       $sp, $sp, -0x10
    ctx->pc = 0x43e400u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967280));
    // 0x43e404: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43e404u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43e408: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x43e408u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x43e40c: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43e40cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43e410: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43e410u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43e414: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x43E414u;
    SET_GPR_U32(ctx, 31, 0x43E41Cu);
    ctx->pc = 0x43E418u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E414u;
    // 0x43e418: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x43E414u, 0x43E41Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E41Cu;
label_43e41c:
    // 0x43e41c: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43e41cu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43e420: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43e420u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43e424: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43e424u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43e428: 0x8c420004  lw          $v0, 0x4($v0)
    ctx->pc = 0x43e428u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 4))); // MMIO: 0x70000004
    // 0x43e42c: 0x10620032  beq         $v1, $v0, . + 4 + (0x32 << 2)
    ctx->pc = 0x43E42Cu;
    {
        const bool branch_taken_0x43e42c = (GPR_U64(ctx, 3) == GPR_U64(ctx, 2));
        if (branch_taken_0x43e42c) {
            ctx->pc = 0x43E4F8u;
            goto label_43e4f8;
        }
    }
    ctx->pc = 0x43E434u;
    // 0x43e434: 0xc10f944  jal         func_43E510
    ctx->pc = 0x43E434u;
    SET_GPR_U32(ctx, 31, 0x43E43Cu);
    ctx->pc = 0x43E438u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E434u;
    // 0x43e438: 0x24040008  addiu       $a0, $zero, 0x8 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 0), 8));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E510u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E510u, 0x43E434u, 0x43E43Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E43Cu;
label_43e43c:
    // 0x43e43c: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43e43cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43e440: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43e440u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43e444: 0x3c061000  lui         $a2, 0x1000
    ctx->pc = 0x43e444u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)4096 << 16));
    // 0x43e448: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43e448u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43e44c: 0x8c630004  lw          $v1, 0x4($v1)
    ctx->pc = 0x43e44cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 4))); // MMIO: 0x70000004
    // 0x43e450: 0x3c051000  lui         $a1, 0x1000
    ctx->pc = 0x43e450u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)4096 << 16));
    // 0x43e454: 0x3c040007  lui         $a0, 0x7
    ctx->pc = 0x43e454u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)7 << 16));
    // 0x43e458: 0x3c080008  lui         $t0, 0x8
    ctx->pc = 0x43e458u;
    SET_GPR_S32(ctx, 8, (int32_t)((uint32_t)8 << 16));
    // 0x43e45c: 0x431023  subu        $v0, $v0, $v1
    ctx->pc = 0x43e45cu;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
    // 0x43e460: 0x34c69030  ori         $a2, $a2, 0x9030
    ctx->pc = 0x43e460u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | (uint64_t)(uint16_t)36912);
    // 0x43e464: 0x21103  sra         $v0, $v0, 4
    ctx->pc = 0x43e464u;
    SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 2), 4));
    // 0x43e468: 0x34a5d010  ori         $a1, $a1, 0xD010
    ctx->pc = 0x43e468u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)53264);
    // 0x43e46c: 0x23900  sll         $a3, $v0, 4
    ctx->pc = 0x43e46cu;
    SET_GPR_S32(ctx, 7, (int32_t)SLL32(GPR_U32(ctx, 2), 4));
    // 0x43e470: 0x3484fff0  ori         $a0, $a0, 0xFFF0
    ctx->pc = 0x43e470u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)65520);
    // 0x43e474: 0x0  nop
    ctx->pc = 0x43e474u;
    // NOP
label_43e478:
    // 0x43e478: 0x8cc20000  lw          $v0, 0x0($a2)
    ctx->pc = 0x43e478u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 0)));
    // 0x43e47c: 0x8ca30000  lw          $v1, 0x0($a1)
    ctx->pc = 0x43e47cu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x43e480: 0x14430003  bne         $v0, $v1, . + 4 + (0x3 << 2)
    ctx->pc = 0x43E480u;
    {
        const bool branch_taken_0x43e480 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 3));
        ctx->pc = 0x43E484u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43E480u;
        // 0x43e484: 0x481021  addu        $v0, $v0, $t0 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 8)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43e480) {
            ctx->pc = 0x43E490u;
            goto label_43e490;
        }
    }
    ctx->pc = 0x43E488u;
    // 0x43e488: 0x10000003  b           . + 4 + (0x3 << 2)
    ctx->pc = 0x43E488u;
    {
        const bool branch_taken_0x43e488 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43E48Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43E488u;
        // 0x43e48c: 0x3c020008  lui         $v0, 0x8 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)8 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43e488) {
            ctx->pc = 0x43E498u;
            goto label_43e498;
        }
    }
    ctx->pc = 0x43E490u;
label_43e490:
    // 0x43e490: 0x431023  subu        $v0, $v0, $v1
    ctx->pc = 0x43e490u;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
    // 0x43e494: 0x441024  and         $v0, $v0, $a0
    ctx->pc = 0x43e494u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & GPR_U64(ctx, 4));
label_43e498:
    // 0x43e498: 0xe2102b  sltu        $v0, $a3, $v0
    ctx->pc = 0x43e498u;
    SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 7) < (uint64_t)GPR_U64(ctx, 2)) ? 1 : 0);
    // 0x43e49c: 0x1040fff6  beqz        $v0, . + 4 + (-0xA << 2)
    ctx->pc = 0x43E49Cu;
    {
        const bool branch_taken_0x43e49c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x43e49c) {
            ctx->pc = 0x43E478u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43e478;
        }
    }
    ctx->pc = 0x43E4A4u;
    // 0x43e4a4: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43e4a4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43e4a8: 0x24050008  addiu       $a1, $zero, 0x8
    ctx->pc = 0x43e4a8u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 8));
    // 0x43e4ac: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43e4acu;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43e4b0: 0xc10f9ae  jal         func_43E6B8
    ctx->pc = 0x43E4B0u;
    SET_GPR_U32(ctx, 31, 0x43E4B8u);
    ctx->pc = 0x43E4B4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E4B0u;
    // 0x43e4b4: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E6B8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E6B8u, 0x43E4B0u, 0x43E4B8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E4B8u;
label_43e4b8:
    // 0x43e4b8: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43e4b8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43e4bc: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43e4bcu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43e4c0: 0x8c630004  lw          $v1, 0x4($v1)
    ctx->pc = 0x43e4c0u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 4))); // MMIO: 0x70000004
    // 0x43e4c4: 0x34420020  ori         $v0, $v0, 0x20
    ctx->pc = 0x43e4c4u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)32);
    // 0x43e4c8: 0x14620007  bne         $v1, $v0, . + 4 + (0x7 << 2)
    ctx->pc = 0x43E4C8u;
    {
        const bool branch_taken_0x43e4c8 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 2));
        ctx->pc = 0x43E4CCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43E4C8u;
        // 0x43e4cc: 0x3c057000  lui         $a1, 0x7000 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)28672 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43e4c8) {
            ctx->pc = 0x43E4E8u;
            goto label_43e4e8;
        }
    }
    ctx->pc = 0x43E4D0u;
    // 0x43e4d0: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43e4d0u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43e4d4: 0x34a52010  ori         $a1, $a1, 0x2010
    ctx->pc = 0x43e4d4u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)8208);
    // 0x43e4d8: 0xc070434  jal         func_1C10D0
    ctx->pc = 0x43E4D8u;
    SET_GPR_U32(ctx, 31, 0x43E4E0u);
    ctx->pc = 0x43E4DCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E4D8u;
    // 0x43e4dc: 0x24060001  addiu       $a2, $zero, 0x1 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C10D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C10D0u, 0x43E4D8u, 0x43E4E0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E4E0u;
label_43e4e0:
    // 0x43e4e0: 0x10000005  b           . + 4 + (0x5 << 2)
    ctx->pc = 0x43E4E0u;
    {
        const bool branch_taken_0x43e4e0 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x43e4e0) {
            ctx->pc = 0x43E4F8u;
            goto label_43e4f8;
        }
    }
    ctx->pc = 0x43E4E8u;
label_43e4e8:
    // 0x43e4e8: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43e4e8u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43e4ec: 0x34a50020  ori         $a1, $a1, 0x20
    ctx->pc = 0x43e4ecu;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)32);
    // 0x43e4f0: 0xc070434  jal         func_1C10D0
    ctx->pc = 0x43E4F0u;
    SET_GPR_U32(ctx, 31, 0x43E4F8u);
    ctx->pc = 0x43E4F4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E4F0u;
    // 0x43e4f4: 0x24060001  addiu       $a2, $zero, 0x1 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C10D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C10D0u, 0x43E4F0u, 0x43E4F8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E4F8u;
label_43e4f8:
    // 0x43e4f8: 0xc10f9d2  jal         func_43E748
    ctx->pc = 0x43E4F8u;
    SET_GPR_U32(ctx, 31, 0x43E500u);
    ctx->pc = 0x43E4FCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43E4F8u;
    // 0x43e4fc: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x43E4F8u, 0x43E500u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43E500u;
label_43e500:
    // 0x43e500: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x43e500u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43e504: 0x3e00008  jr          $ra
    ctx->pc = 0x43E504u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43E508u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43E504u;
        // 0x43e508: 0x27bd0010  addiu       $sp, $sp, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43E504u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43E50Cu;
}
