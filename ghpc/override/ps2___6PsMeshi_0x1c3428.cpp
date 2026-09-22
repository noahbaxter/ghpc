// Override of work/output/ps2___6PsMeshi_0x1c3428.cpp (PsMesh::PsMesh(int),
// 0x1c3428 - 0x1c3674, the constructor PsMesh::NewObject calls at 0x3afc4c).
//
// Why: the host mesh cache is keyed on the PsMesh's address, and the game
// reuses addresses once a menu or a song unloads. PsMesh::Sync only stores
// for an owner that is not mutable and has verts, so the next occupant of an
// address was served the old one's geometry whenever it was anything else.
// Measured on the second song of route.sh: a smoke card drew with the EXIT
// sign quad at 0x007e7d60, and the kick drum, a mutable mesh, never reached
// its face packet decode because the lookup hit. A new object at an address
// is the moment its cache entry dies, so it is dropped here.
//
// Fresh entry only; the body is otherwise the generated translation.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ghpc_mesh_cache.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: __6PsMeshi
// Address: 0x1c3428 - 0x1c3674
void ps2___6PsMeshi_0x1c3428(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("ps2___6PsMeshi_0x1c3428");
#endif

    switch (ctx->pc) {
        case 0x1c3488u: goto label_1c3488;
        case 0x1c34acu: goto label_1c34ac;
        case 0x1c3514u: goto label_1c3514;
        case 0x1c3544u: goto label_1c3544;
        default: break;
    }

    // $a0 is `this`. GHPC_MESH_EVICT=0 keeps stale entries, the control arm.
    {
        static const bool s_evict = []() {
            const char* e = std::getenv("GHPC_MESH_EVICT");
            return e == nullptr || std::atoi(e) != 0;
        }();
        static uint64_t s_evicted = 0;
        if (s_evict && ghpc::meshcache::erase(GPR_U32(ctx, 4))) {
            if ((++s_evicted % 500u) == 1u)
                std::fprintf(stderr, "[ghpc/mesh/evict] %llu stale entries dropped at construction\n",
                             (unsigned long long)s_evicted);
        }
    }

    ctx->pc = 0x1c3428u;

    // 0x1c3428: 0x27bdfe70  addiu       $sp, $sp, -0x190
    ctx->pc = 0x1c3428u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294966896));
    // 0x1c342c: 0x7fb00180  sq          $s0, 0x180($sp)
    ctx->pc = 0x1c342cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 384), GPR_VEC(ctx, 16));
    // 0x1c3430: 0x7fb10170  sq          $s1, 0x170($sp)
    ctx->pc = 0x1c3430u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 368), GPR_VEC(ctx, 17));
    // 0x1c3434: 0xa0802d  daddu       $s0, $a1, $zero
    ctx->pc = 0x1c3434u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3438: 0x7fb20160  sq          $s2, 0x160($sp)
    ctx->pc = 0x1c3438u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 352), GPR_VEC(ctx, 18));
    // 0x1c343c: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x1c343cu;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3440: 0x7fb30150  sq          $s3, 0x150($sp)
    ctx->pc = 0x1c3440u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 336), GPR_VEC(ctx, 19));
    // 0x1c3444: 0x3c120045  lui         $s2, 0x45
    ctx->pc = 0x1c3444u;
    SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)69 << 16));
    // 0x1c3448: 0x7fb40140  sq          $s4, 0x140($sp)
    ctx->pc = 0x1c3448u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 320), GPR_VEC(ctx, 20));
    // 0x1c344c: 0x3c130045  lui         $s3, 0x45
    ctx->pc = 0x1c344cu;
    SET_GPR_S32(ctx, 19, (int32_t)((uint32_t)69 << 16));
    // 0x1c3450: 0x7fb50130  sq          $s5, 0x130($sp)
    ctx->pc = 0x1c3450u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 304), GPR_VEC(ctx, 21));
    // 0x1c3454: 0x3c140045  lui         $s4, 0x45
    ctx->pc = 0x1c3454u;
    SET_GPR_S32(ctx, 20, (int32_t)((uint32_t)69 << 16));
    // 0x1c3458: 0xffbf0120  sd          $ra, 0x120($sp)
    ctx->pc = 0x1c3458u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 288), GPR_U64(ctx, 31));
    // 0x1c345c: 0x1200002a  beqz        $s0, . + 4 + (0x2A << 2)
    ctx->pc = 0x1C345Cu;
    {
        const bool branch_taken_0x1c345c = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C3460u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C345Cu;
        // 0x1c3460: 0x3c150045  lui         $s5, 0x45 (Delay Slot)
        SET_GPR_S32(ctx, 21, (int32_t)((uint32_t)69 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c345c) {
            ctx->pc = 0x1C3508u;
            goto label_1c3508;
        }
    }
    ctx->pc = 0x1C3464u;
    // 0x1c3464: 0x26220160  addiu       $v0, $s1, 0x160
    ctx->pc = 0x1c3464u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 17), 352));
    // 0x1c3468: 0x26230188  addiu       $v1, $s1, 0x188
    ctx->pc = 0x1c3468u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 17), 392));
    // 0x1c346c: 0xae230004  sw          $v1, 0x4($s1)
    ctx->pc = 0x1c346cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 4), GPR_U32(ctx, 3));
    // 0x1c3470: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x1c3470u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3474: 0xae230044  sw          $v1, 0x44($s1)
    ctx->pc = 0x1c3474u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 68), GPR_U32(ctx, 3));
    // 0x1c3478: 0xae220040  sw          $v0, 0x40($s1)
    ctx->pc = 0x1c3478u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 64), GPR_U32(ctx, 2));
    // 0x1c347c: 0xae220188  sw          $v0, 0x188($s1)
    ctx->pc = 0x1c347cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 392), GPR_U32(ctx, 2));
    // 0x1c3480: 0xc0c3de4  jal         func_30F790
    ctx->pc = 0x1C3480u;
    SET_GPR_U32(ctx, 31, 0x1C3488u);
    ctx->pc = 0x1C3484u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C3480u;
    // 0x1c3484: 0xae220000  sw          $v0, 0x0($s1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 17), 0), GPR_U32(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x30F790u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x30F790u, 0x1C3480u, 0x1C3488u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C3488u;
label_1c3488:
    // 0x1c3488: 0x8e270044  lw          $a3, 0x44($s1)
    ctx->pc = 0x1c3488u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 68)));
    // 0x1c348c: 0x3c020045  lui         $v0, 0x45
    ctx->pc = 0x1c348cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)69 << 16));
    // 0x1c3490: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c3490u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3494: 0x3a0302d  daddu       $a2, $sp, $zero
    ctx->pc = 0x1c3494u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3498: 0x8ce30000  lw          $v1, 0x0($a3)
    ctx->pc = 0x1c3498u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x1c349c: 0x24429c00  addiu       $v0, $v0, -0x6400
    ctx->pc = 0x1c349cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294941696));
    // 0x1c34a0: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x1c34a0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c34a4: 0xac620000  sw          $v0, 0x0($v1)
    ctx->pc = 0x1c34a4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 2));
    // 0x1c34a8: 0x24820080  addiu       $v0, $a0, 0x80
    ctx->pc = 0x1c34a8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 4), 128));
label_1c34ac:
    // 0x1c34ac: 0xdc830000  ld          $v1, 0x0($a0)
    ctx->pc = 0x1c34acu;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x1c34b0: 0xdc880008  ld          $t0, 0x8($a0)
    ctx->pc = 0x1c34b0u;
    SET_GPR_U64(ctx, 8, READ64(ADD32(GPR_U32(ctx, 4), 8)));
    // 0x1c34b4: 0xdc890010  ld          $t1, 0x10($a0)
    ctx->pc = 0x1c34b4u;
    SET_GPR_U64(ctx, 9, READ64(ADD32(GPR_U32(ctx, 4), 16)));
    // 0x1c34b8: 0xdc8a0018  ld          $t2, 0x18($a0)
    ctx->pc = 0x1c34b8u;
    SET_GPR_U64(ctx, 10, READ64(ADD32(GPR_U32(ctx, 4), 24)));
    // 0x1c34bc: 0xfcc30000  sd          $v1, 0x0($a2)
    ctx->pc = 0x1c34bcu;
    WRITE64(ADD32(GPR_U32(ctx, 6), 0), GPR_U64(ctx, 3));
    // 0x1c34c0: 0xfcc80008  sd          $t0, 0x8($a2)
    ctx->pc = 0x1c34c0u;
    WRITE64(ADD32(GPR_U32(ctx, 6), 8), GPR_U64(ctx, 8));
    // 0x1c34c4: 0xfcc90010  sd          $t1, 0x10($a2)
    ctx->pc = 0x1c34c4u;
    WRITE64(ADD32(GPR_U32(ctx, 6), 16), GPR_U64(ctx, 9));
    // 0x1c34c8: 0xfcca0018  sd          $t2, 0x18($a2)
    ctx->pc = 0x1c34c8u;
    WRITE64(ADD32(GPR_U32(ctx, 6), 24), GPR_U64(ctx, 10));
    // 0x1c34cc: 0x24840020  addiu       $a0, $a0, 0x20
    ctx->pc = 0x1c34ccu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 32));
    // 0x1c34d0: 0x1482fff6  bne         $a0, $v0, . + 4 + (-0xA << 2)
    ctx->pc = 0x1C34D0u;
    {
        const bool branch_taken_0x1c34d0 = (GPR_U64(ctx, 4) != GPR_U64(ctx, 2));
        ctx->pc = 0x1C34D4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C34D0u;
        // 0x1c34d4: 0x24c60020  addiu       $a2, $a2, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), 32));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c34d0) {
            ctx->pc = 0x1C34ACu;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c34ac;
        }
    }
    ctx->pc = 0x1C34D8u;
    // 0x1c34d8: 0x8ce30000  lw          $v1, 0x0($a3)
    ctx->pc = 0x1c34d8u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x1c34dc: 0x24a59c00  addiu       $a1, $a1, -0x6400
    ctx->pc = 0x1c34dcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294941696));
    // 0x1c34e0: 0x3c040045  lui         $a0, 0x45
    ctx->pc = 0x1c34e0u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)69 << 16));
    // 0x1c34e4: 0xac7d0000  sw          $sp, 0x0($v1)
    ctx->pc = 0x1c34e4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 29));
    // 0x1c34e8: 0x24849be8  addiu       $a0, $a0, -0x6418
    ctx->pc = 0x1c34e8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294941672));
    // 0x1c34ec: 0x8ce20000  lw          $v0, 0x0($a3)
    ctx->pc = 0x1c34ecu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x1c34f0: 0x94a30008  lhu         $v1, 0x8($a1)
    ctx->pc = 0x1c34f0u;
    SET_GPR_U32(ctx, 3, (uint16_t)READ16(ADD32(GPR_U32(ctx, 5), 8)));
    // 0x1c34f4: 0x2442fff8  addiu       $v0, $v0, -0x8
    ctx->pc = 0x1c34f4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967288));
    // 0x1c34f8: 0xe21023  subu        $v0, $a3, $v0
    ctx->pc = 0x1c34f8u;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 7), GPR_U32(ctx, 2)));
    // 0x1c34fc: 0x621821  addu        $v1, $v1, $v0
    ctx->pc = 0x1c34fcu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 2)));
    // 0x1c3500: 0xa7a30008  sh          $v1, 0x8($sp)
    ctx->pc = 0x1c3500u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 8), (uint16_t)GPR_U32(ctx, 3));
    // 0x1c3504: 0xace40004  sw          $a0, 0x4($a3)
    ctx->pc = 0x1c3504u;
    WRITE32(ADD32(GPR_U32(ctx, 7), 4), GPR_U32(ctx, 4));
label_1c3508:
    // 0x1c3508: 0x220202d  daddu       $a0, $s1, $zero
    ctx->pc = 0x1c3508u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c350c: 0xc07c568  jal         func_1F15A0
    ctx->pc = 0x1C350Cu;
    SET_GPR_U32(ctx, 31, 0x1C3514u);
    ctx->pc = 0x1C3510u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C350Cu;
    // 0x1c3510: 0x282d  daddu       $a1, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1F15A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1F15A0u, 0x1C350Cu, 0x1C3514u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C3514u;
label_1c3514:
    // 0x1c3514: 0x8e220000  lw          $v0, 0x0($s1)
    ctx->pc = 0x1c3514u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 0)));
    // 0x1c3518: 0x2645f500  addiu       $a1, $s2, -0xB00
    ctx->pc = 0x1c3518u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 18), 4294964480));
    // 0x1c351c: 0x2664f580  addiu       $a0, $s3, -0xA80
    ctx->pc = 0x1c351cu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 19), 4294964608));
    // 0x1c3520: 0xac450000  sw          $a1, 0x0($v0)
    ctx->pc = 0x1c3520u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 5));
    // 0x1c3524: 0x8e230004  lw          $v1, 0x4($s1)
    ctx->pc = 0x1c3524u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4)));
    // 0x1c3528: 0x16000041  bnez        $s0, . + 4 + (0x41 << 2)
    ctx->pc = 0x1C3528u;
    {
        const bool branch_taken_0x1c3528 = (GPR_U64(ctx, 16) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C352Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C3528u;
        // 0x1c352c: 0xac640004  sw          $a0, 0x4($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c3528) {
            ctx->pc = 0x1C3630u;
            goto label_1c3630;
        }
    }
    ctx->pc = 0x1C3530u;
    // 0x1c3530: 0xa0182d  daddu       $v1, $a1, $zero
    ctx->pc = 0x1c3530u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3534: 0x27a20080  addiu       $v0, $sp, 0x80
    ctx->pc = 0x1c3534u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 29), 128));
    // 0x1c3538: 0x24640080  addiu       $a0, $v1, 0x80
    ctx->pc = 0x1c3538u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 128));
    // 0x1c353c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c353cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3540: 0x27b80100  addiu       $t8, $sp, 0x100
    ctx->pc = 0x1c3540u;
    SET_GPR_S32(ctx, 24, (int32_t)ADD32(GPR_U32(ctx, 29), 256));
label_1c3544:
    // 0x1c3544: 0xdc660000  ld          $a2, 0x0($v1)
    ctx->pc = 0x1c3544u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x1c3548: 0xdc670008  ld          $a3, 0x8($v1)
    ctx->pc = 0x1c3548u;
    SET_GPR_U64(ctx, 7, READ64(ADD32(GPR_U32(ctx, 3), 8)));
    // 0x1c354c: 0xdc680010  ld          $t0, 0x10($v1)
    ctx->pc = 0x1c354cu;
    SET_GPR_U64(ctx, 8, READ64(ADD32(GPR_U32(ctx, 3), 16)));
    // 0x1c3550: 0xdc690018  ld          $t1, 0x18($v1)
    ctx->pc = 0x1c3550u;
    SET_GPR_U64(ctx, 9, READ64(ADD32(GPR_U32(ctx, 3), 24)));
    // 0x1c3554: 0xfc460000  sd          $a2, 0x0($v0)
    ctx->pc = 0x1c3554u;
    WRITE64(ADD32(GPR_U32(ctx, 2), 0), GPR_U64(ctx, 6));
    // 0x1c3558: 0xfc470008  sd          $a3, 0x8($v0)
    ctx->pc = 0x1c3558u;
    WRITE64(ADD32(GPR_U32(ctx, 2), 8), GPR_U64(ctx, 7));
    // 0x1c355c: 0xfc480010  sd          $t0, 0x10($v0)
    ctx->pc = 0x1c355cu;
    WRITE64(ADD32(GPR_U32(ctx, 2), 16), GPR_U64(ctx, 8));
    // 0x1c3560: 0xfc490018  sd          $t1, 0x18($v0)
    ctx->pc = 0x1c3560u;
    WRITE64(ADD32(GPR_U32(ctx, 2), 24), GPR_U64(ctx, 9));
    // 0x1c3564: 0x24630020  addiu       $v1, $v1, 0x20
    ctx->pc = 0x1c3564u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 32));
    // 0x1c3568: 0x1464fff6  bne         $v1, $a0, . + 4 + (-0xA << 2)
    ctx->pc = 0x1C3568u;
    {
        const bool branch_taken_0x1c3568 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 4));
        ctx->pc = 0x1C356Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C3568u;
        // 0x1c356c: 0x24420020  addiu       $v0, $v0, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 32));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c3568) {
            ctx->pc = 0x1C3544u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c3544;
        }
    }
    ctx->pc = 0x1C3570u;
    // 0x1c3570: 0x8e220000  lw          $v0, 0x0($s1)
    ctx->pc = 0x1c3570u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 0)));
    // 0x1c3574: 0x2643f500  addiu       $v1, $s2, -0xB00
    ctx->pc = 0x1c3574u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 18), 4294964480));
    // 0x1c3578: 0x266df580  addiu       $t5, $s3, -0xA80
    ctx->pc = 0x1c3578u;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 19), 4294964608));
    // 0x1c357c: 0x946c0050  lhu         $t4, 0x50($v1)
    ctx->pc = 0x1c357cu;
    SET_GPR_U32(ctx, 12, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 80)));
    // 0x1c3580: 0xac450000  sw          $a1, 0x0($v0)
    ctx->pc = 0x1c3580u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 5));
    // 0x1c3584: 0x946b0008  lhu         $t3, 0x8($v1)
    ctx->pc = 0x1c3584u;
    SET_GPR_U32(ctx, 11, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 8)));
    // 0x1c3588: 0x8e220000  lw          $v0, 0x0($s1)
    ctx->pc = 0x1c3588u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 0)));
    // 0x1c358c: 0x946a0018  lhu         $t2, 0x18($v1)
    ctx->pc = 0x1c358cu;
    SET_GPR_U32(ctx, 10, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 24)));
    // 0x1c3590: 0x94680020  lhu         $t0, 0x20($v1)
    ctx->pc = 0x1c3590u;
    SET_GPR_U32(ctx, 8, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 32)));
    // 0x1c3594: 0x2442fea0  addiu       $v0, $v0, -0x160
    ctx->pc = 0x1c3594u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294966944));
    // 0x1c3598: 0x94690028  lhu         $t1, 0x28($v1)
    ctx->pc = 0x1c3598u;
    SET_GPR_U32(ctx, 9, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 40)));
    // 0x1c359c: 0x2221023  subu        $v0, $s1, $v0
    ctx->pc = 0x1c359cu;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 17), GPR_U32(ctx, 2)));
    // 0x1c35a0: 0x94670030  lhu         $a3, 0x30($v1)
    ctx->pc = 0x1c35a0u;
    SET_GPR_U32(ctx, 7, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 48)));
    // 0x1c35a4: 0x1826021  addu        $t4, $t4, $v0
    ctx->pc = 0x1c35a4u;
    SET_GPR_S32(ctx, 12, (int32_t)ADD32(GPR_U32(ctx, 12), GPR_U32(ctx, 2)));
    // 0x1c35a8: 0x94660038  lhu         $a2, 0x38($v1)
    ctx->pc = 0x1c35a8u;
    SET_GPR_U32(ctx, 6, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 56)));
    // 0x1c35ac: 0x1625821  addu        $t3, $t3, $v0
    ctx->pc = 0x1c35acu;
    SET_GPR_S32(ctx, 11, (int32_t)ADD32(GPR_U32(ctx, 11), GPR_U32(ctx, 2)));
    // 0x1c35b0: 0x94650040  lhu         $a1, 0x40($v1)
    ctx->pc = 0x1c35b0u;
    SET_GPR_U32(ctx, 5, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 64)));
    // 0x1c35b4: 0x1425021  addu        $t2, $t2, $v0
    ctx->pc = 0x1c35b4u;
    SET_GPR_S32(ctx, 10, (int32_t)ADD32(GPR_U32(ctx, 10), GPR_U32(ctx, 2)));
    // 0x1c35b8: 0x94640048  lhu         $a0, 0x48($v1)
    ctx->pc = 0x1c35b8u;
    SET_GPR_U32(ctx, 4, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 72)));
    // 0x1c35bc: 0x1024021  addu        $t0, $t0, $v0
    ctx->pc = 0x1c35bcu;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 8), GPR_U32(ctx, 2)));
    // 0x1c35c0: 0xdda30008  ld          $v1, 0x8($t5)
    ctx->pc = 0x1c35c0u;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 13), 8)));
    // 0x1c35c4: 0x1224821  addu        $t1, $t1, $v0
    ctx->pc = 0x1c35c4u;
    SET_GPR_S32(ctx, 9, (int32_t)ADD32(GPR_U32(ctx, 9), GPR_U32(ctx, 2)));
    // 0x1c35c8: 0xde70f580  ld          $s0, -0xA80($s3)
    ctx->pc = 0x1c35c8u;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 19), 4294964608)));
    // 0x1c35cc: 0xe23821  addu        $a3, $a3, $v0
    ctx->pc = 0x1c35ccu;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), GPR_U32(ctx, 2)));
    // 0x1c35d0: 0xddaf0010  ld          $t7, 0x10($t5)
    ctx->pc = 0x1c35d0u;
    SET_GPR_U64(ctx, 15, READ64(ADD32(GPR_U32(ctx, 13), 16)));
    // 0x1c35d4: 0xc23021  addu        $a2, $a2, $v0
    ctx->pc = 0x1c35d4u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 2)));
    // 0x1c35d8: 0x8e2e0004  lw          $t6, 0x4($s1)
    ctx->pc = 0x1c35d8u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4)));
    // 0x1c35dc: 0xa22821  addu        $a1, $a1, $v0
    ctx->pc = 0x1c35dcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 2)));
    // 0x1c35e0: 0x822021  addu        $a0, $a0, $v0
    ctx->pc = 0x1c35e0u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), GPR_U32(ctx, 2)));
    // 0x1c35e4: 0xffa30108  sd          $v1, 0x108($sp)
    ctx->pc = 0x1c35e4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 264), GPR_U64(ctx, 3));
    // 0x1c35e8: 0xa7ab0088  sh          $t3, 0x88($sp)
    ctx->pc = 0x1c35e8u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 136), (uint16_t)GPR_U32(ctx, 11));
    // 0x1c35ec: 0xa7aa0098  sh          $t2, 0x98($sp)
    ctx->pc = 0x1c35ecu;
    WRITE16(ADD32(GPR_U32(ctx, 29), 152), (uint16_t)GPR_U32(ctx, 10));
    // 0x1c35f0: 0xa7a800a0  sh          $t0, 0xA0($sp)
    ctx->pc = 0x1c35f0u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 160), (uint16_t)GPR_U32(ctx, 8));
    // 0x1c35f4: 0xa7a900a8  sh          $t1, 0xA8($sp)
    ctx->pc = 0x1c35f4u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 168), (uint16_t)GPR_U32(ctx, 9));
    // 0x1c35f8: 0xa7a700b0  sh          $a3, 0xB0($sp)
    ctx->pc = 0x1c35f8u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 176), (uint16_t)GPR_U32(ctx, 7));
    // 0x1c35fc: 0xa7a600b8  sh          $a2, 0xB8($sp)
    ctx->pc = 0x1c35fcu;
    WRITE16(ADD32(GPR_U32(ctx, 29), 184), (uint16_t)GPR_U32(ctx, 6));
    // 0x1c3600: 0xa7a500c0  sh          $a1, 0xC0($sp)
    ctx->pc = 0x1c3600u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 192), (uint16_t)GPR_U32(ctx, 5));
    // 0x1c3604: 0xa7a400c8  sh          $a0, 0xC8($sp)
    ctx->pc = 0x1c3604u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 200), (uint16_t)GPR_U32(ctx, 4));
    // 0x1c3608: 0xa7ac00d0  sh          $t4, 0xD0($sp)
    ctx->pc = 0x1c3608u;
    WRITE16(ADD32(GPR_U32(ctx, 29), 208), (uint16_t)GPR_U32(ctx, 12));
    // 0x1c360c: 0xffb00100  sd          $s0, 0x100($sp)
    ctx->pc = 0x1c360cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 256), GPR_U64(ctx, 16));
    // 0x1c3610: 0xffaf0110  sd          $t7, 0x110($sp)
    ctx->pc = 0x1c3610u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 272), GPR_U64(ctx, 15));
    // 0x1c3614: 0xadd80004  sw          $t8, 0x4($t6)
    ctx->pc = 0x1c3614u;
    WRITE32(ADD32(GPR_U32(ctx, 14), 4), GPR_U32(ctx, 24));
    // 0x1c3618: 0x95a30008  lhu         $v1, 0x8($t5)
    ctx->pc = 0x1c3618u;
    SET_GPR_U32(ctx, 3, (uint16_t)READ16(ADD32(GPR_U32(ctx, 13), 8)));
    // 0x1c361c: 0x8e220004  lw          $v0, 0x4($s1)
    ctx->pc = 0x1c361cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4)));
    // 0x1c3620: 0x2442fe78  addiu       $v0, $v0, -0x188
    ctx->pc = 0x1c3620u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294966904));
    // 0x1c3624: 0x2221023  subu        $v0, $s1, $v0
    ctx->pc = 0x1c3624u;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 17), GPR_U32(ctx, 2)));
    // 0x1c3628: 0x621821  addu        $v1, $v1, $v0
    ctx->pc = 0x1c3628u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 2)));
    // 0x1c362c: 0xa7a30108  sh          $v1, 0x108($sp)
    ctx->pc = 0x1c362cu;
    WRITE16(ADD32(GPR_U32(ctx, 29), 264), (uint16_t)GPR_U32(ctx, 3));
label_1c3630:
    // 0x1c3630: 0x2682f598  addiu       $v0, $s4, -0xA68
    ctx->pc = 0x1c3630u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 20), 4294964632));
    // 0x1c3634: 0x26a3f5b0  addiu       $v1, $s5, -0xA50
    ctx->pc = 0x1c3634u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 21), 4294964656));
    // 0x1c3638: 0xae2200f8  sw          $v0, 0xF8($s1)
    ctx->pc = 0x1c3638u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 248), GPR_U32(ctx, 2));
    // 0x1c363c: 0x220102d  daddu       $v0, $s1, $zero
    ctx->pc = 0x1c363cu;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c3640: 0xae230038  sw          $v1, 0x38($s1)
    ctx->pc = 0x1c3640u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 56), GPR_U32(ctx, 3));
    // 0x1c3644: 0xae200150  sw          $zero, 0x150($s1)
    ctx->pc = 0x1c3644u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 336), GPR_U32(ctx, 0));
    // 0x1c3648: 0xa6200158  sh          $zero, 0x158($s1)
    ctx->pc = 0x1c3648u;
    WRITE16(ADD32(GPR_U32(ctx, 17), 344), (uint16_t)GPR_U32(ctx, 0));
    // 0x1c364c: 0xa620015a  sh          $zero, 0x15A($s1)
    ctx->pc = 0x1c364cu;
    WRITE16(ADD32(GPR_U32(ctx, 17), 346), (uint16_t)GPR_U32(ctx, 0));
    // 0x1c3650: 0x7bb00180  lq          $s0, 0x180($sp)
    ctx->pc = 0x1c3650u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 384)));
    // 0x1c3654: 0x7bb10170  lq          $s1, 0x170($sp)
    ctx->pc = 0x1c3654u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 368)));
    // 0x1c3658: 0x7bb20160  lq          $s2, 0x160($sp)
    ctx->pc = 0x1c3658u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 352)));
    // 0x1c365c: 0x7bb30150  lq          $s3, 0x150($sp)
    ctx->pc = 0x1c365cu;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 336)));
    // 0x1c3660: 0x7bb40140  lq          $s4, 0x140($sp)
    ctx->pc = 0x1c3660u;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 320)));
    // 0x1c3664: 0x7bb50130  lq          $s5, 0x130($sp)
    ctx->pc = 0x1c3664u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 304)));
    // 0x1c3668: 0xdfbf0120  ld          $ra, 0x120($sp)
    ctx->pc = 0x1c3668u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 288)));
    // 0x1c366c: 0x3e00008  jr          $ra
    ctx->pc = 0x1C366Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C3670u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C366Cu;
        // 0x1c3670: 0x27bd0190  addiu       $sp, $sp, 0x190 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 400));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C366Cu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C3674u;
}
