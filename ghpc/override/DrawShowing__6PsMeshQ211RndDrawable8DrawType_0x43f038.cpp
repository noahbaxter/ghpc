// Override of work/output/DrawShowing__6PsMeshQ211RndDrawable8DrawType_0x43f038.cpp
// (PsMesh::DrawShowing, 0x43f038 - 0x43f490).
//
// Why: this is where the per-draw world transform comes from, and DrawFaces
// cannot see it. At 0x43f2d0 the body takes `$s2 + 0x40`, the RndTransformable
// of the INSTANCE being drawn, calls WorldXfm (0x43f768) on it, and at
// 0x43f308..0x43f32c copies the four quadwords it returns into the DMA cursor
// behind a `0x6C0402A4` VIF header: V4-32, NUM 4, VU1 address 676. So the
// object matrix VU1 uses is WorldXfm(instance), not the owner's cached
// `this+0xa0` that DrawFaces can reach. Reading the owner's is wrong twice
// over: `this` in DrawFaces is the owner (+0x138), and the owner's cached
// matrix is stale, its dirty word at +0xe0 reading 1 on every gameplay draw.
//
// The body is the generated translation verbatim plus one capture at
// label_43f2f4, the instruction after WorldXfm returns, where $v0 is the
// Transform it returned and $s2 is the instance. Nothing is written back.
#include <stdexcept>
#include <cstdlib>
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


// The world transform of the instance currently drawing, for the Rnd seam.
// Rows 0..2 are the Matrix3, row 3 the translation, matching the quadword
// order this function uploads to VU1 qw676..679.
float g_ghpcInstWorld[4][4] = {};
uint32_t g_ghpcInstThis = 0u;
uint32_t g_ghpcInstOwner = 0u;
uint64_t g_ghpcInstDraws = 0ull;

namespace {
void ghpcCaptureInstWorld(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                          uint32_t xfm, uint32_t inst) {
    // Off unless a calibration run asks for it: this runs on every draw and
    // the capture turned out to name a different mesh chain than the
    // DrawFaces it precedes, so it is a probe, not a seam input.
    static const bool s_on = std::getenv("GHPC_TL_CAL") != nullptr;
    if (!s_on || xfm == 0u) return;
    for (uint32_t r = 0; r < 4u; ++r) {
        for (uint32_t c = 0; c < 4u; ++c) {
            const uint32_t bits = READ32(xfm + r * 0x10u + c * 4u);
            std::memcpy(&g_ghpcInstWorld[r][c], &bits, sizeof(float));
        }
    }
    g_ghpcInstThis = inst;
    g_ghpcInstOwner = READ32(inst + 0x138u);
    ++g_ghpcInstDraws;
}
} // namespace

// Function: DrawShowing__6PsMeshQ211RndDrawable8DrawType
// Address: 0x43f038 - 0x43f490
void DrawShowing__6PsMeshQ211RndDrawable8DrawType_0x43f038(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("DrawShowing__6PsMeshQ211RndDrawable8DrawType_0x43f038");
#endif

    switch (ctx->pc) {
        case 0x43f098u: goto label_43f098;
        case 0x43f118u: goto label_43f118;
        case 0x43f128u: goto label_43f128;
        case 0x43f130u: goto label_43f130;
        case 0x43f140u: goto label_43f140;
        case 0x43f158u: goto label_43f158;
        case 0x43f168u: goto label_43f168;
        case 0x43f1a4u: goto label_43f1a4;
        case 0x43f1b4u: goto label_43f1b4;
        case 0x43f1f0u: goto label_43f1f0;
        case 0x43f200u: goto label_43f200;
        case 0x43f2f4u: goto label_43f2f4;
        case 0x43f308u: goto label_43f308;
        case 0x43f370u: goto label_43f370;
        case 0x43f37cu: goto label_43f37c;
        case 0x43f43cu: goto label_43f43c;
        case 0x43f460u: goto label_43f460;
        default: break;
    }

    ctx->pc = 0x43f038u;

    // 0x43f038: 0x27bdff50  addiu       $sp, $sp, -0xB0
    ctx->pc = 0x43f038u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967120));
    // 0x43f03c: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x43f03cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x43f040: 0x7fb20080  sq          $s2, 0x80($sp)
    ctx->pc = 0x43f040u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 128), GPR_VEC(ctx, 18));
    // 0x43f044: 0x24422cb8  addiu       $v0, $v0, 0x2CB8
    ctx->pc = 0x43f044u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 11448));
    // 0x43f048: 0x7fb000a0  sq          $s0, 0xA0($sp)
    ctx->pc = 0x43f048u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 16));
    // 0x43f04c: 0x80902d  daddu       $s2, $a0, $zero
    ctx->pc = 0x43f04cu;
    SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f050: 0x7fb10090  sq          $s1, 0x90($sp)
    ctx->pc = 0x43f050u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 144), GPR_VEC(ctx, 17));
    // 0x43f054: 0x7fb30070  sq          $s3, 0x70($sp)
    ctx->pc = 0x43f054u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 112), GPR_VEC(ctx, 19));
    // 0x43f058: 0x7fb40060  sq          $s4, 0x60($sp)
    ctx->pc = 0x43f058u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 96), GPR_VEC(ctx, 20));
    // 0x43f05c: 0x7fb50050  sq          $s5, 0x50($sp)
    ctx->pc = 0x43f05cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 80), GPR_VEC(ctx, 21));
    // 0x43f060: 0x7fb60040  sq          $s6, 0x40($sp)
    ctx->pc = 0x43f060u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 64), GPR_VEC(ctx, 22));
    // 0x43f064: 0x7fb70030  sq          $s7, 0x30($sp)
    ctx->pc = 0x43f064u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 48), GPR_VEC(ctx, 23));
    // 0x43f068: 0x7fbe0020  sq          $fp, 0x20($sp)
    ctx->pc = 0x43f068u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 32), GPR_VEC(ctx, 30));
    // 0x43f06c: 0xffbf0010  sd          $ra, 0x10($sp)
    ctx->pc = 0x43f06cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 31));
    // 0x43f070: 0x8c43000c  lw          $v1, 0xC($v0)
    ctx->pc = 0x43f070u;
    SET_GPR_S32(ctx, 3, (int32_t)FAST_READ32(0x4F2CC4u));
    // 0x43f074: 0x24630001  addiu       $v1, $v1, 0x1
    ctx->pc = 0x43f074u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 1));
    // 0x43f078: 0xac43000c  sw          $v1, 0xC($v0)
    ctx->pc = 0x43f078u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 3)); ps2TraceGuestWrite(rdram, 0x4F2CC4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x4F2CC4u, _value); } while (0);
    // 0x43f07c: 0x8e550138  lw          $s5, 0x138($s2)
    ctx->pc = 0x43f07cu;
    SET_GPR_S32(ctx, 21, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 312)));
    // 0x43f080: 0x8ea20150  lw          $v0, 0x150($s5)
    ctx->pc = 0x43f080u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 21), 336)));
    // 0x43f084: 0x104000f6  beqz        $v0, . + 4 + (0xF6 << 2)
    ctx->pc = 0x43F084u;
    {
        const bool branch_taken_0x43f084 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F088u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F084u;
        // 0x43f088: 0x24050001  addiu       $a1, $zero, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f084) {
            ctx->pc = 0x43F460u;
            goto label_43f460;
        }
    }
    ctx->pc = 0x43F08Cu;
    // 0x43f08c: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43f08cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43f090: 0xc10fa18  jal         func_43E860
    ctx->pc = 0x43F090u;
    SET_GPR_U32(ctx, 31, 0x43F098u);
    ctx->pc = 0x43F094u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F090u;
    // 0x43f094: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E860u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E860u, 0x43F090u, 0x43F098u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F098u;
label_43f098:
    // 0x43f098: 0x8e42013c  lw          $v0, 0x13C($s2)
    ctx->pc = 0x43f098u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
    // 0x43f09c: 0x10400086  beqz        $v0, . + 4 + (0x86 << 2)
    ctx->pc = 0x43F09Cu;
    {
        const bool branch_taken_0x43f09c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F0A0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F09Cu;
        // 0x43f0a0: 0x3c051000  lui         $a1, 0x1000 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)4096 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f09c) {
            ctx->pc = 0x43F2B8u;
            goto label_43f2b8;
        }
    }
    ctx->pc = 0x43F0A4u;
    // 0x43f0a4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43f0a4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43f0a8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43f0a8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43f0ac: 0x3c046c14  lui         $a0, 0x6C14
    ctx->pc = 0x43f0acu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)27668 << 16));
    // 0x43f0b0: 0x34840294  ori         $a0, $a0, 0x294
    ctx->pc = 0x43f0b0u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)660);
    // 0x43f0b4: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43f0b4u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f0b8: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43f0b8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43f0bc: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f0bcu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f0c0: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f0c0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f0c4: 0xac64000c  sw          $a0, 0xC($v1)
    ctx->pc = 0x43f0c4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 4));
    // 0x43f0c8: 0xac650008  sw          $a1, 0x8($v1)
    ctx->pc = 0x43f0c8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 5));
    // 0x43f0cc: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x43f0ccu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x43f0d0: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x43f0d0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x43f0d4: 0x3c117000  lui         $s1, 0x7000
    ctx->pc = 0x43f0d4u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)28672 << 16));
    // 0x43f0d8: 0x8e310008  lw          $s1, 0x8($s1)
    ctx->pc = 0x43f0d8u;
    SET_GPR_S32(ctx, 17, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 17), 8))); // MMIO: 0x70000008
    // 0x43f0dc: 0x26220140  addiu       $v0, $s1, 0x140
    ctx->pc = 0x43f0dcu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 17), 320));
    // 0x43f0e0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f0e0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f0e4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f0e4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f0e8: 0x8e50013c  lw          $s0, 0x13C($s2)
    ctx->pc = 0x43f0e8u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
    // 0x43f0ec: 0x8e020008  lw          $v0, 0x8($s0)
    ctx->pc = 0x43f0ecu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 8)));
    // 0x43f0f0: 0x1440000d  bnez        $v0, . + 4 + (0xD << 2)
    ctx->pc = 0x43F0F0u;
    {
        const bool branch_taken_0x43f0f0 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x43F0F4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F0F0u;
        // 0x43f0f4: 0x26130030  addiu       $s3, $s0, 0x30 (Delay Slot)
        SET_GPR_S32(ctx, 19, (int32_t)ADD32(GPR_U32(ctx, 16), 48));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f0f0) {
            ctx->pc = 0x43F128u;
            goto label_43f128;
        }
    }
    ctx->pc = 0x43F0F8u;
    // 0x43f0f8: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x43f0f8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x43f0fc: 0x3c050049  lui         $a1, 0x49
    ctx->pc = 0x43f0fcu;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)73 << 16));
    // 0x43f100: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x43f100u;
    SET_GPR_S32(ctx, 4, (int32_t)FAST_READ32(0x444FC0u));
    // 0x43f104: 0x3c070049  lui         $a3, 0x49
    ctx->pc = 0x43f104u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)73 << 16));
    // 0x43f108: 0x24a5ca30  addiu       $a1, $a1, -0x35D0
    ctx->pc = 0x43f108u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294953520));
    // 0x43f10c: 0x24e7ca58  addiu       $a3, $a3, -0x35A8
    ctx->pc = 0x43f10cu;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294953560));
    // 0x43f110: 0xc0da03a  jal         func_3680E8
    ctx->pc = 0x43F110u;
    SET_GPR_U32(ctx, 31, 0x43F118u);
    ctx->pc = 0x43F114u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F110u;
    // 0x43f114: 0x24060063  addiu       $a2, $zero, 0x63 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x43F110u, 0x43F118u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F118u;
label_43f118:
    // 0x43f118: 0x3c040052  lui         $a0, 0x52
    ctx->pc = 0x43f118u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)82 << 16));
    // 0x43f11c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f11cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f120: 0xc0baf6a  jal         func_2EBDA8
    ctx->pc = 0x43F120u;
    SET_GPR_U32(ctx, 31, 0x43F128u);
    ctx->pc = 0x43F124u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F120u;
    // 0x43f124: 0x2484d0e0  addiu       $a0, $a0, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x43F120u, 0x43F128u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F128u;
label_43f128:
    // 0x43f128: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x43F128u;
    SET_GPR_U32(ctx, 31, 0x43F130u);
    ctx->pc = 0x43F12Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F128u;
    // 0x43f12c: 0x8e040008  lw          $a0, 0x8($s0) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 8)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x43F128u, 0x43F130u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F130u;
label_43f130:
    // 0x43f130: 0x260202d  daddu       $a0, $s3, $zero
    ctx->pc = 0x43f130u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f134: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f134u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f138: 0xc0cbbb2  jal         func_32EEC8
    ctx->pc = 0x43F138u;
    SET_GPR_U32(ctx, 31, 0x43F140u);
    ctx->pc = 0x43F13Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F138u;
    // 0x43f13c: 0x220302d  daddu       $a2, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x32EEC8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32EEC8u, 0x43F138u, 0x43F140u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F140u;
label_43f140:
    // 0x43f140: 0x8e50013c  lw          $s0, 0x13C($s2)
    ctx->pc = 0x43f140u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
    // 0x43f144: 0x8e040014  lw          $a0, 0x14($s0)
    ctx->pc = 0x43f144u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 20)));
    // 0x43f148: 0x50800009  beql        $a0, $zero, . + 4 + (0x9 << 2)
    ctx->pc = 0x43F148u;
    {
        const bool branch_taken_0x43f148 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x43f148) {
            ctx->pc = 0x43F14Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43F148u;
            // 0x43f14c: 0x7a220000  lq          $v0, 0x0($s1) (Delay Slot)
            SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43F170u;
            goto label_43f170;
        }
    }
    ctx->pc = 0x43F150u;
    // 0x43f150: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x43F150u;
    SET_GPR_U32(ctx, 31, 0x43F158u);
    ctx->pc = 0x43F154u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F150u;
    // 0x43f154: 0x26100070  addiu       $s0, $s0, 0x70 (Delay Slot)
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x43F150u, 0x43F158u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F158u;
label_43f158:
    // 0x43f158: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f158u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f15c: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f15cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f160: 0xc0cbbb2  jal         func_32EEC8
    ctx->pc = 0x43F160u;
    SET_GPR_U32(ctx, 31, 0x43F168u);
    ctx->pc = 0x43F164u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F160u;
    // 0x43f164: 0x26260040  addiu       $a2, $s1, 0x40 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 17), 64));
    ctx->in_delay_slot = false;
    ctx->pc = 0x32EEC8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32EEC8u, 0x43F160u, 0x43F168u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F168u;
label_43f168:
    // 0x43f168: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x43F168u;
    {
        const bool branch_taken_0x43f168 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F16Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F168u;
        // 0x43f16c: 0x8e50013c  lw          $s0, 0x13C($s2) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f168) {
            ctx->pc = 0x43F190u;
            goto label_43f190;
        }
    }
    ctx->pc = 0x43F170u;
label_43f170:
    // 0x43f170: 0x7a230010  lq          $v1, 0x10($s1)
    ctx->pc = 0x43f170u;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 17), 16)));
    // 0x43f174: 0x7a240020  lq          $a0, 0x20($s1)
    ctx->pc = 0x43f174u;
    SET_GPR_VEC(ctx, 4, READ128(ADD32(GPR_U32(ctx, 17), 32)));
    // 0x43f178: 0x7a250030  lq          $a1, 0x30($s1)
    ctx->pc = 0x43f178u;
    SET_GPR_VEC(ctx, 5, READ128(ADD32(GPR_U32(ctx, 17), 48)));
    // 0x43f17c: 0x7e220040  sq          $v0, 0x40($s1)
    ctx->pc = 0x43f17cu;
    WRITE128(ADD32(GPR_U32(ctx, 17), 64), GPR_VEC(ctx, 2));
    // 0x43f180: 0x7e230050  sq          $v1, 0x50($s1)
    ctx->pc = 0x43f180u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 80), GPR_VEC(ctx, 3));
    // 0x43f184: 0x7e240060  sq          $a0, 0x60($s1)
    ctx->pc = 0x43f184u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 96), GPR_VEC(ctx, 4));
    // 0x43f188: 0x7e250070  sq          $a1, 0x70($s1)
    ctx->pc = 0x43f188u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 112), GPR_VEC(ctx, 5));
    // 0x43f18c: 0x8e50013c  lw          $s0, 0x13C($s2)
    ctx->pc = 0x43f18cu;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
label_43f190:
    // 0x43f190: 0x8e040020  lw          $a0, 0x20($s0)
    ctx->pc = 0x43f190u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 32)));
    // 0x43f194: 0x50800009  beql        $a0, $zero, . + 4 + (0x9 << 2)
    ctx->pc = 0x43F194u;
    {
        const bool branch_taken_0x43f194 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x43f194) {
            ctx->pc = 0x43F198u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43F194u;
            // 0x43f198: 0x7a220000  lq          $v0, 0x0($s1) (Delay Slot)
            SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43F1BCu;
            goto label_43f1bc;
        }
    }
    ctx->pc = 0x43F19Cu;
    // 0x43f19c: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x43F19Cu;
    SET_GPR_U32(ctx, 31, 0x43F1A4u);
    ctx->pc = 0x43F1A0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F19Cu;
    // 0x43f1a0: 0x261000b0  addiu       $s0, $s0, 0xB0 (Delay Slot)
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 176));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x43F19Cu, 0x43F1A4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F1A4u;
label_43f1a4:
    // 0x43f1a4: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f1a4u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f1a8: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f1a8u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f1ac: 0xc0cbbb2  jal         func_32EEC8
    ctx->pc = 0x43F1ACu;
    SET_GPR_U32(ctx, 31, 0x43F1B4u);
    ctx->pc = 0x43F1B0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F1ACu;
    // 0x43f1b0: 0x26260080  addiu       $a2, $s1, 0x80 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 17), 128));
    ctx->in_delay_slot = false;
    ctx->pc = 0x32EEC8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32EEC8u, 0x43F1ACu, 0x43F1B4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F1B4u;
label_43f1b4:
    // 0x43f1b4: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x43F1B4u;
    {
        const bool branch_taken_0x43f1b4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F1B8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F1B4u;
        // 0x43f1b8: 0x8e50013c  lw          $s0, 0x13C($s2) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f1b4) {
            ctx->pc = 0x43F1DCu;
            goto label_43f1dc;
        }
    }
    ctx->pc = 0x43F1BCu;
label_43f1bc:
    // 0x43f1bc: 0x7a230010  lq          $v1, 0x10($s1)
    ctx->pc = 0x43f1bcu;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 17), 16)));
    // 0x43f1c0: 0x7a240020  lq          $a0, 0x20($s1)
    ctx->pc = 0x43f1c0u;
    SET_GPR_VEC(ctx, 4, READ128(ADD32(GPR_U32(ctx, 17), 32)));
    // 0x43f1c4: 0x7a250030  lq          $a1, 0x30($s1)
    ctx->pc = 0x43f1c4u;
    SET_GPR_VEC(ctx, 5, READ128(ADD32(GPR_U32(ctx, 17), 48)));
    // 0x43f1c8: 0x7e220080  sq          $v0, 0x80($s1)
    ctx->pc = 0x43f1c8u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 128), GPR_VEC(ctx, 2));
    // 0x43f1cc: 0x7e230090  sq          $v1, 0x90($s1)
    ctx->pc = 0x43f1ccu;
    WRITE128(ADD32(GPR_U32(ctx, 17), 144), GPR_VEC(ctx, 3));
    // 0x43f1d0: 0x7e2400a0  sq          $a0, 0xA0($s1)
    ctx->pc = 0x43f1d0u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 160), GPR_VEC(ctx, 4));
    // 0x43f1d4: 0x7e2500b0  sq          $a1, 0xB0($s1)
    ctx->pc = 0x43f1d4u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 176), GPR_VEC(ctx, 5));
    // 0x43f1d8: 0x8e50013c  lw          $s0, 0x13C($s2)
    ctx->pc = 0x43f1d8u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
label_43f1dc:
    // 0x43f1dc: 0x8e04002c  lw          $a0, 0x2C($s0)
    ctx->pc = 0x43f1dcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 44)));
    // 0x43f1e0: 0x50800009  beql        $a0, $zero, . + 4 + (0x9 << 2)
    ctx->pc = 0x43F1E0u;
    {
        const bool branch_taken_0x43f1e0 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x43f1e0) {
            ctx->pc = 0x43F1E4u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43F1E0u;
            // 0x43f1e4: 0x7a220000  lq          $v0, 0x0($s1) (Delay Slot)
            SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43F208u;
            goto label_43f208;
        }
    }
    ctx->pc = 0x43F1E8u;
    // 0x43f1e8: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x43F1E8u;
    SET_GPR_U32(ctx, 31, 0x43F1F0u);
    ctx->pc = 0x43F1ECu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F1E8u;
    // 0x43f1ec: 0x261000f0  addiu       $s0, $s0, 0xF0 (Delay Slot)
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 240));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x43F1E8u, 0x43F1F0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F1F0u;
label_43f1f0:
    // 0x43f1f0: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f1f0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f1f4: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f1f4u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f1f8: 0xc0cbbb2  jal         func_32EEC8
    ctx->pc = 0x43F1F8u;
    SET_GPR_U32(ctx, 31, 0x43F200u);
    ctx->pc = 0x43F1FCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F1F8u;
    // 0x43f1fc: 0x262600c0  addiu       $a2, $s1, 0xC0 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 17), 192));
    ctx->in_delay_slot = false;
    ctx->pc = 0x32EEC8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x32EEC8u, 0x43F1F8u, 0x43F200u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F200u;
label_43f200:
    // 0x43f200: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x43F200u;
    {
        const bool branch_taken_0x43f200 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F204u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F200u;
        // 0x43f204: 0x8e43013c  lw          $v1, 0x13C($s2) (Delay Slot)
        SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f200) {
            ctx->pc = 0x43F228u;
            goto label_43f228;
        }
    }
    ctx->pc = 0x43F208u;
label_43f208:
    // 0x43f208: 0x7a230010  lq          $v1, 0x10($s1)
    ctx->pc = 0x43f208u;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 17), 16)));
    // 0x43f20c: 0x7a240020  lq          $a0, 0x20($s1)
    ctx->pc = 0x43f20cu;
    SET_GPR_VEC(ctx, 4, READ128(ADD32(GPR_U32(ctx, 17), 32)));
    // 0x43f210: 0x7a250030  lq          $a1, 0x30($s1)
    ctx->pc = 0x43f210u;
    SET_GPR_VEC(ctx, 5, READ128(ADD32(GPR_U32(ctx, 17), 48)));
    // 0x43f214: 0x7e2200c0  sq          $v0, 0xC0($s1)
    ctx->pc = 0x43f214u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 192), GPR_VEC(ctx, 2));
    // 0x43f218: 0x7e2300d0  sq          $v1, 0xD0($s1)
    ctx->pc = 0x43f218u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 208), GPR_VEC(ctx, 3));
    // 0x43f21c: 0x7e2400e0  sq          $a0, 0xE0($s1)
    ctx->pc = 0x43f21cu;
    WRITE128(ADD32(GPR_U32(ctx, 17), 224), GPR_VEC(ctx, 4));
    // 0x43f220: 0x7e2500f0  sq          $a1, 0xF0($s1)
    ctx->pc = 0x43f220u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 240), GPR_VEC(ctx, 5));
    // 0x43f224: 0x8e43013c  lw          $v1, 0x13C($s2)
    ctx->pc = 0x43f224u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 18), 316)));
label_43f228:
    // 0x43f228: 0x8c620014  lw          $v0, 0x14($v1)
    ctx->pc = 0x43f228u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 20)));
    // 0x43f22c: 0x14400007  bnez        $v0, . + 4 + (0x7 << 2)
    ctx->pc = 0x43F22Cu;
    {
        const bool branch_taken_0x43f22c = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x43f22c) {
            ctx->pc = 0x43F24Cu;
            goto label_43f24c;
        }
    }
    ctx->pc = 0x43F234u;
    // 0x43f234: 0x8c620020  lw          $v0, 0x20($v1)
    ctx->pc = 0x43f234u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 32)));
    // 0x43f238: 0x14400004  bnez        $v0, . + 4 + (0x4 << 2)
    ctx->pc = 0x43F238u;
    {
        const bool branch_taken_0x43f238 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x43f238) {
            ctx->pc = 0x43F24Cu;
            goto label_43f24c;
        }
    }
    ctx->pc = 0x43F240u;
    // 0x43f240: 0x8c62002c  lw          $v0, 0x2C($v1)
    ctx->pc = 0x43f240u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 44)));
    // 0x43f244: 0x50400013  beql        $v0, $zero, . + 4 + (0x13 << 2)
    ctx->pc = 0x43F244u;
    {
        const bool branch_taken_0x43f244 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x43f244) {
            ctx->pc = 0x43F248u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43F244u;
            // 0x43f248: 0x7a230000  lq          $v1, 0x0($s1) (Delay Slot)
            SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 17), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43F294u;
            goto label_43f294;
        }
    }
    ctx->pc = 0x43F24Cu;
label_43f24c:
    // 0x43f24c: 0x3c013f80  lui         $at, 0x3F80
    ctx->pc = 0x43f24cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16256 << 16));
    // 0x43f250: 0x44810000  mtc1        $at, $f0
    ctx->pc = 0x43f250u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x43f254: 0x26220100  addiu       $v0, $s1, 0x100
    ctx->pc = 0x43f254u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 17), 256));
    // 0x43f258: 0x26230110  addiu       $v1, $s1, 0x110
    ctx->pc = 0x43f258u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 17), 272));
    // 0x43f25c: 0x26240120  addiu       $a0, $s1, 0x120
    ctx->pc = 0x43f25cu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 17), 288));
    // 0x43f260: 0xe6200100  swc1        $f0, 0x100($s1)
    ctx->pc = 0x43f260u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 17), 256), bits); }
    // 0x43f264: 0x26250130  addiu       $a1, $s1, 0x130
    ctx->pc = 0x43f264u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 17), 304));
    // 0x43f268: 0xac400008  sw          $zero, 0x8($v0)
    ctx->pc = 0x43f268u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 8), GPR_U32(ctx, 0));
    // 0x43f26c: 0xac400004  sw          $zero, 0x4($v0)
    ctx->pc = 0x43f26cu;
    WRITE32(ADD32(GPR_U32(ctx, 2), 4), GPR_U32(ctx, 0));
    // 0x43f270: 0xae200110  sw          $zero, 0x110($s1)
    ctx->pc = 0x43f270u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 272), GPR_U32(ctx, 0));
    // 0x43f274: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x43f274u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x43f278: 0xe4600004  swc1        $f0, 0x4($v1)
    ctx->pc = 0x43f278u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4), bits); }
    // 0x43f27c: 0xae200120  sw          $zero, 0x120($s1)
    ctx->pc = 0x43f27cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 288), GPR_U32(ctx, 0));
    // 0x43f280: 0xe4800008  swc1        $f0, 0x8($a0)
    ctx->pc = 0x43f280u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 4), 8), bits); }
    // 0x43f284: 0xac800004  sw          $zero, 0x4($a0)
    ctx->pc = 0x43f284u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 4), GPR_U32(ctx, 0));
    // 0x43f288: 0xf8a00000  sqc2        $vf0, 0x0($a1)
    ctx->pc = 0x43f288u;
    WRITE128(ADD32(GPR_U32(ctx, 5), 0), _mm_castps_si128(ctx->vu0_vf[0]));
    // 0x43f28c: 0x10000028  b           . + 4 + (0x28 << 2)
    ctx->pc = 0x43F28Cu;
    {
        const bool branch_taken_0x43f28c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F290u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F28Cu;
        // 0x43f290: 0x26460118  addiu       $a2, $s2, 0x118 (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 18), 280));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f28c) {
            ctx->pc = 0x43F330u;
            goto label_43f330;
        }
    }
    ctx->pc = 0x43F294u;
label_43f294:
    // 0x43f294: 0x26460118  addiu       $a2, $s2, 0x118
    ctx->pc = 0x43f294u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 18), 280));
    // 0x43f298: 0x7a240010  lq          $a0, 0x10($s1)
    ctx->pc = 0x43f298u;
    SET_GPR_VEC(ctx, 4, READ128(ADD32(GPR_U32(ctx, 17), 16)));
    // 0x43f29c: 0x7a250020  lq          $a1, 0x20($s1)
    ctx->pc = 0x43f29cu;
    SET_GPR_VEC(ctx, 5, READ128(ADD32(GPR_U32(ctx, 17), 32)));
    // 0x43f2a0: 0x7a220030  lq          $v0, 0x30($s1)
    ctx->pc = 0x43f2a0u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 48)));
    // 0x43f2a4: 0x7e230100  sq          $v1, 0x100($s1)
    ctx->pc = 0x43f2a4u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 256), GPR_VEC(ctx, 3));
    // 0x43f2a8: 0x7e220130  sq          $v0, 0x130($s1)
    ctx->pc = 0x43f2a8u;
    WRITE128(ADD32(GPR_U32(ctx, 17), 304), GPR_VEC(ctx, 2));
    // 0x43f2ac: 0x7e240110  sq          $a0, 0x110($s1)
    ctx->pc = 0x43f2acu;
    WRITE128(ADD32(GPR_U32(ctx, 17), 272), GPR_VEC(ctx, 4));
    // 0x43f2b0: 0x1000001f  b           . + 4 + (0x1F << 2)
    ctx->pc = 0x43F2B0u;
    {
        const bool branch_taken_0x43f2b0 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43F2B4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F2B0u;
        // 0x43f2b4: 0x7e250120  sq          $a1, 0x120($s1) (Delay Slot)
        WRITE128(ADD32(GPR_U32(ctx, 17), 288), GPR_VEC(ctx, 5));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f2b0) {
            ctx->pc = 0x43F330u;
            goto label_43f330;
        }
    }
    ctx->pc = 0x43F2B8u;
label_43f2b8:
    // 0x43f2b8: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43f2b8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43f2bc: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43f2bcu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43f2c0: 0x3c056c04  lui         $a1, 0x6C04
    ctx->pc = 0x43f2c0u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)27652 << 16));
    // 0x43f2c4: 0x34a502a4  ori         $a1, $a1, 0x2A4
    ctx->pc = 0x43f2c4u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)676);
    // 0x43f2c8: 0x3c061000  lui         $a2, 0x1000
    ctx->pc = 0x43f2c8u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)4096 << 16));
    // 0x43f2cc: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43f2ccu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f2d0: 0x26440040  addiu       $a0, $s2, 0x40
    ctx->pc = 0x43f2d0u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 18), 64));
    // 0x43f2d4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43f2d4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43f2d8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f2d8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f2dc: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f2dcu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f2e0: 0xac65000c  sw          $a1, 0xC($v1)
    ctx->pc = 0x43f2e0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 5));
    // 0x43f2e4: 0xac660008  sw          $a2, 0x8($v1)
    ctx->pc = 0x43f2e4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 6));
    // 0x43f2e8: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x43f2e8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x43f2ec: 0xc10fdda  jal         func_43F768
    ctx->pc = 0x43F2ECu;
    SET_GPR_U32(ctx, 31, 0x43F2F4u);
    ctx->pc = 0x43F2F0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F2ECu;
    // 0x43f2f0: 0xac600004  sw          $zero, 0x4($v1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x43F2ECu, 0x43F2F4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F2F4u;
label_43f2f4:
    ghpcCaptureInstWorld(rdram, ctx, runtime, GPR_U32(ctx, 2), GPR_U32(ctx, 18));
    // 0x43f2f4: 0x2407ffff  addiu       $a3, $zero, -0x1
    ctx->pc = 0x43f2f4u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43f2f8: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x43f2f8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f2fc: 0x26460118  addiu       $a2, $s2, 0x118
    ctx->pc = 0x43f2fcu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 18), 280));
    // 0x43f300: 0x24050003  addiu       $a1, $zero, 0x3
    ctx->pc = 0x43f300u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x43f304: 0x0  nop
    ctx->pc = 0x43f304u;
    // NOP
label_43f308:
    // 0x43f308: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43f308u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43f30c: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43f30cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43f310: 0x24a5ffff  addiu       $a1, $a1, -0x1
    ctx->pc = 0x43f310u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294967295));
    // 0x43f314: 0x78820000  lq          $v0, 0x0($a0)
    ctx->pc = 0x43f314u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x43f318: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43f318u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43f31c: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43f31cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43f320: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f320u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f324: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43f324u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43f328: 0x14a7fff7  bne         $a1, $a3, . + 4 + (-0x9 << 2)
    ctx->pc = 0x43F328u;
    {
        const bool branch_taken_0x43f328 = (GPR_U64(ctx, 5) != GPR_U64(ctx, 7));
        ctx->pc = 0x43F32Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F328u;
        // 0x43f32c: 0x24840010  addiu       $a0, $a0, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f328) {
            ctx->pc = 0x43F308u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43f308;
        }
    }
    ctx->pc = 0x43F330u;
label_43f330:
    // 0x43f330: 0x8cd00008  lw          $s0, 0x8($a2)
    ctx->pc = 0x43f330u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 6), 8)));
    // 0x43f334: 0x16000003  bnez        $s0, . + 4 + (0x3 << 2)
    ctx->pc = 0x43F334u;
    {
        const bool branch_taken_0x43f334 = (GPR_U64(ctx, 16) != GPR_U64(ctx, 0));
        ctx->pc = 0x43F338u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F334u;
        // 0x43f338: 0x3c126c01  lui         $s2, 0x6C01 (Delay Slot)
        SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)27649 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f334) {
            ctx->pc = 0x43F344u;
            goto label_43f344;
        }
    }
    ctx->pc = 0x43F33Cu;
    // 0x43f33c: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x43f33cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x43f340: 0x8c500e08  lw          $s0, 0xE08($v0)
    ctx->pc = 0x43f340u;
    SET_GPR_S32(ctx, 16, (int32_t)FAST_READ32(0x440E08u));
label_43f344:
    // 0x43f344: 0x3c116c01  lui         $s1, 0x6C01
    ctx->pc = 0x43f344u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)27649 << 16));
    // 0x43f348: 0x241e0004  addiu       $fp, $zero, 0x4
    ctx->pc = 0x43f348u;
    SET_GPR_S32(ctx, 30, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
    // 0x43f34c: 0x365202a8  ori         $s2, $s2, 0x2A8
    ctx->pc = 0x43f34cu;
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 18) | (uint64_t)(uint16_t)680);
    // 0x43f350: 0x3414c001  ori         $s4, $zero, 0xC001
    ctx->pc = 0x43f350u;
    SET_GPR_U64(ctx, 20, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)49153);
    // 0x43f354: 0x14a3bc  dsll32      $s4, $s4, 14
    ctx->pc = 0x43f354u;
    SET_GPR_U64(ctx, 20, GPR_U64(ctx, 20) << (32 + 14));
    // 0x43f358: 0x36948000  ori         $s4, $s4, 0x8000
    ctx->pc = 0x43f358u;
    SET_GPR_U64(ctx, 20, GPR_U64(ctx, 20) | (uint64_t)(uint16_t)32768);
    // 0x43f35c: 0x24130412  addiu       $s3, $zero, 0x412
    ctx->pc = 0x43f35cu;
    SET_GPR_S32(ctx, 19, (int32_t)ADD32(GPR_U32(ctx, 0), 1042));
    // 0x43f360: 0x363103e2  ori         $s1, $s1, 0x3E2
    ctx->pc = 0x43f360u;
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 17) | (uint64_t)(uint16_t)994);
    // 0x43f364: 0x2417fff8  addiu       $s7, $zero, -0x8
    ctx->pc = 0x43f364u;
    SET_GPR_S32(ctx, 23, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967288));
    // 0x43f368: 0x24160005  addiu       $s6, $zero, 0x5
    ctx->pc = 0x43f368u;
    SET_GPR_S32(ctx, 22, (int32_t)ADD32(GPR_U32(ctx, 0), 5));
    // 0x43f36c: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43f36cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
label_43f370:
    // 0x43f370: 0x3a0282d  daddu       $a1, $sp, $zero
    ctx->pc = 0x43f370u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f374: 0xc10fa9c  jal         func_43EA70
    ctx->pc = 0x43F374u;
    SET_GPR_U32(ctx, 31, 0x43F37Cu);
    ctx->pc = 0x43F378u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F374u;
    // 0x43f378: 0xffbe0000  sd          $fp, 0x0($sp) (Delay Slot)
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 30));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43EA70u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43EA70u, 0x43F374u, 0x43F37Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F37Cu;
label_43f37c:
    // 0x43f37c: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43f37cu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43f380: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43f380u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43f384: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x43f384u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f388: 0x2a0202d  daddu       $a0, $s5, $zero
    ctx->pc = 0x43f388u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f38c: 0x60282d  daddu       $a1, $v1, $zero
    ctx->pc = 0x43f38cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f390: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43f390u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43f394: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f394u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f398: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43f398u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43f39c: 0xacb2000c  sw          $s2, 0xC($a1)
    ctx->pc = 0x43f39cu;
    WRITE32(ADD32(GPR_U32(ctx, 5), 12), GPR_U32(ctx, 18));
    // 0x43f3a0: 0xaca00000  sw          $zero, 0x0($a1)
    ctx->pc = 0x43f3a0u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 0), GPR_U32(ctx, 0));
    // 0x43f3a4: 0xaca00004  sw          $zero, 0x4($a1)
    ctx->pc = 0x43f3a4u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 4), GPR_U32(ctx, 0));
    // 0x43f3a8: 0xaca00008  sw          $zero, 0x8($a1)
    ctx->pc = 0x43f3a8u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 8), GPR_U32(ctx, 0));
    // 0x43f3ac: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43f3acu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43f3b0: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43f3b0u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43f3b4: 0xdfa30000  ld          $v1, 0x0($sp)
    ctx->pc = 0x43f3b4u;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43f3b8: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f3b8u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f3bc: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43f3bcu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43f3c0: 0x31bfc  dsll32      $v1, $v1, 15
    ctx->pc = 0x43f3c0u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << (32 + 15));
    // 0x43f3c4: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f3c4u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f3c8: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f3c8u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f3cc: 0x741825  or          $v1, $v1, $s4
    ctx->pc = 0x43f3ccu;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | GPR_U64(ctx, 20));
    // 0x43f3d0: 0xfca30000  sd          $v1, 0x0($a1)
    ctx->pc = 0x43f3d0u;
    WRITE64(ADD32(GPR_U32(ctx, 5), 0), GPR_U64(ctx, 3));
    // 0x43f3d4: 0xfcb30008  sd          $s3, 0x8($a1)
    ctx->pc = 0x43f3d4u;
    WRITE64(ADD32(GPR_U32(ctx, 5), 8), GPR_U64(ctx, 19));
    // 0x43f3d8: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43f3d8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43f3dc: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43f3dcu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43f3e0: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f3e0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f3e4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43f3e4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43f3e8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f3e8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f3ec: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f3ecu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f3f0: 0xacb1000c  sw          $s1, 0xC($a1)
    ctx->pc = 0x43f3f0u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 12), GPR_U32(ctx, 17));
    // 0x43f3f4: 0xaca00000  sw          $zero, 0x0($a1)
    ctx->pc = 0x43f3f4u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 0), GPR_U32(ctx, 0));
    // 0x43f3f8: 0xaca00004  sw          $zero, 0x4($a1)
    ctx->pc = 0x43f3f8u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 4), GPR_U32(ctx, 0));
    // 0x43f3fc: 0xaca00008  sw          $zero, 0x8($a1)
    ctx->pc = 0x43f3fcu;
    WRITE32(ADD32(GPR_U32(ctx, 5), 8), GPR_U32(ctx, 0));
    // 0x43f400: 0xdfa30000  ld          $v1, 0x0($sp)
    ctx->pc = 0x43f400u;
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43f404: 0x771824  and         $v1, $v1, $s7
    ctx->pc = 0x43f404u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) & GPR_U64(ctx, 23));
    // 0x43f408: 0x761825  or          $v1, $v1, $s6
    ctx->pc = 0x43f408u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | GPR_U64(ctx, 22));
    // 0x43f40c: 0xffa30000  sd          $v1, 0x0($sp)
    ctx->pc = 0x43f40cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 3));
    // 0x43f410: 0x31bfc  dsll32      $v1, $v1, 15
    ctx->pc = 0x43f410u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << (32 + 15));
    // 0x43f414: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43f414u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43f418: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43f418u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43f41c: 0x741825  or          $v1, $v1, $s4
    ctx->pc = 0x43f41cu;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | GPR_U64(ctx, 20));
    // 0x43f420: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x43f420u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43f424: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43f424u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43f428: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43f428u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43f42c: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43f42cu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43f430: 0xfca30000  sd          $v1, 0x0($a1)
    ctx->pc = 0x43f430u;
    WRITE64(ADD32(GPR_U32(ctx, 5), 0), GPR_U64(ctx, 3));
    // 0x43f434: 0xc10fba8  jal         func_43EEA0
    ctx->pc = 0x43F434u;
    SET_GPR_U32(ctx, 31, 0x43F43Cu);
    ctx->pc = 0x43F438u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F434u;
    // 0x43f438: 0xfcb30008  sd          $s3, 0x8($a1) (Delay Slot)
    WRITE64(ADD32(GPR_U32(ctx, 5), 8), GPR_U64(ctx, 19));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43EEA0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43EEA0u, 0x43F434u, 0x43F43Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F43Cu;
label_43f43c:
    // 0x43f43c: 0x1600ffcc  bnez        $s0, . + 4 + (-0x34 << 2)
    ctx->pc = 0x43F43Cu;
    {
        const bool branch_taken_0x43f43c = (GPR_U64(ctx, 16) != GPR_U64(ctx, 0));
        ctx->pc = 0x43F440u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F43Cu;
        // 0x43f440: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f43c) {
            ctx->pc = 0x43F370u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43f370;
        }
    }
    ctx->pc = 0x43F444u;
    // 0x43f444: 0x8ea20140  lw          $v0, 0x140($s5)
    ctx->pc = 0x43f444u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 21), 320)));
    // 0x43f448: 0x3042001f  andi        $v0, $v0, 0x1F
    ctx->pc = 0x43f448u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)31);
    // 0x43f44c: 0x14400005  bnez        $v0, . + 4 + (0x5 << 2)
    ctx->pc = 0x43F44Cu;
    {
        const bool branch_taken_0x43f44c = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x43F450u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F44Cu;
        // 0x43f450: 0x7bb000a0  lq          $s0, 0xA0($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 160)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43f44c) {
            ctx->pc = 0x43F464u;
            goto label_43f464;
        }
    }
    ctx->pc = 0x43F454u;
    // 0x43f454: 0x3c04004f  lui         $a0, 0x4F
    ctx->pc = 0x43f454u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)79 << 16));
    // 0x43f458: 0xc10f900  jal         func_43E400
    ctx->pc = 0x43F458u;
    SET_GPR_U32(ctx, 31, 0x43F460u);
    ctx->pc = 0x43F45Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F458u;
    // 0x43f45c: 0x24842780  addiu       $a0, $a0, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E400u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E400u, 0x43F458u, 0x43F460u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F460u;
label_43f460:
    // 0x43f460: 0x7bb000a0  lq          $s0, 0xA0($sp)
    ctx->pc = 0x43f460u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 160)));
label_43f464:
    // 0x43f464: 0x7bb10090  lq          $s1, 0x90($sp)
    ctx->pc = 0x43f464u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x43f468: 0x7bb20080  lq          $s2, 0x80($sp)
    ctx->pc = 0x43f468u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x43f46c: 0x7bb30070  lq          $s3, 0x70($sp)
    ctx->pc = 0x43f46cu;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x43f470: 0x7bb40060  lq          $s4, 0x60($sp)
    ctx->pc = 0x43f470u;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x43f474: 0x7bb50050  lq          $s5, 0x50($sp)
    ctx->pc = 0x43f474u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x43f478: 0x7bb60040  lq          $s6, 0x40($sp)
    ctx->pc = 0x43f478u;
    SET_GPR_VEC(ctx, 22, READ128(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x43f47c: 0x7bb70030  lq          $s7, 0x30($sp)
    ctx->pc = 0x43f47cu;
    SET_GPR_VEC(ctx, 23, READ128(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x43f480: 0x7bbe0020  lq          $fp, 0x20($sp)
    ctx->pc = 0x43f480u;
    SET_GPR_VEC(ctx, 30, READ128(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x43f484: 0xdfbf0010  ld          $ra, 0x10($sp)
    ctx->pc = 0x43f484u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x43f488: 0x3e00008  jr          $ra
    ctx->pc = 0x43F488u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43F48Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F488u;
        // 0x43f48c: 0x27bd00b0  addiu       $sp, $sp, 0xB0 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 176));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43F488u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43F490u;
}
