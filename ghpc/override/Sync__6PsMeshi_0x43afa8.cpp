// Override of work/output/Sync__6PsMeshi_0x43afa8.cpp (PsMesh::Sync(int),
// 0x43afa8 - 0x43b148).
//
// Why: DrawFaces sees verts=0 faces=0 on every mesh because this function is
// where the EE-side copies die. Sync is the last thing RndMesh::Load does
// (virtual call at 0x43cb5c, $a1 = 0x3f or 0xbf), and after UpdateFacePacket
// 0x43a810 has turned mVerts/mFaces into the VIF packet it discards both:
//
//   0x43afc4  bne  $v0(+0x138 owner), $s1(this)   -> return; not our data
//   0x43afd4  andi $s0(flags), 0x20 == 0          -> skip the face free
//   0x43b00c  +0x140 & 0x20 != 0                  -> skip the face free
//   0x43b014  sw $zero, 0($sp); 0x43b01c sw $zero, 4($sp); 0x43b024 sw $zero, 0xc($sp)
//             an empty vector<Face> on the stack
//   0x43b040  sw $v1, 0x108($s1)   mFaces._M_start  <- 0
//   0x43b048  sw $a0, 0x10c($s1)   mFaces._M_finish <- 0
//   0x43b068  sdl/sdr 0x110..0x117 <- 0 (end_of_storage), i.e. mFaces.swap(empty)
//   0x43b0e0  jal MemOrPoolFree 0x321760 on the old face buffer
//   0x43b0e8  jal List::clear 0x3afaf8 on this+0x124
//   0x43b0f0  $s2 = flags & 0x1f == 0              -> skip the vert resize
//   0x43b100  +0x140 & 0x1f != 0                   -> SyncDCache the verts and keep them
//   0x43b128  jal VertVector::resize 0x1f1d30 (this+0x100, 0)   mVerts.mSize <- 0
//
// UpdateFacePacket itself only reads +0x100/+0x104/+0x108/+0x10c (0x43a850,
// 0x43a854, 0x43a860, 0x43ac34); its two frees (0x43ab10 MemFree of a stack
// temp, 0x43ab34 MemOrPoolFree of a 0x18-byte node from the global list at
// 0x4F7DD0) are not the vectors. RndMesh::Load has no store to those offsets
// and its four MemOrPoolFree calls (0x43bf38, 0x43bf74, 0x43ca80, 0x43cab4)
// free striper/stack buffers with strides 4, 2, 2 and 16, never the 6-byte
// faces. So an entry hook here sees the vectors intact on the Load call.
//
// This override keeps the generated body verbatim and, when GHPC_MESH_LOG is
// set, prints the same RndMesh block DrawFaces prints (offsets pinned in
// DrawFaces__6PsMesh_0x43eea0.cpp) at entry, plus the Sync flags in $a1.
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

// Guest float through the same masked path the generated code uses.
inline float ghpcMeshF32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    const uint32_t bits = READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Same block as DrawFaces' ghpcMeshLog. overlay.sh only maps .cpp files onto
// generated ones, so there is no shared header to put it in.
void ghpcMeshLog(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self, uint32_t syncFlags, uint64_t n) {
    const uint32_t vertPtr  = READ32(self + 0x100u);
    const int32_t  vertCnt  = (int32_t)READ32(self + 0x104u);
    const uint32_t faceBeg  = READ32(self + 0x108u);
    const uint32_t faceEnd  = READ32(self + 0x10cu);
    const uint32_t mat      = READ32(self + 0x120u);
    const uint32_t owner    = READ32(self + 0x138u);
    const uint32_t flags140 = READ32(self + 0x140u);
    const uint32_t packet   = READ32(self + 0x150u);
    const uint32_t packetQw = READ16(self + 0x154u);
    const uint32_t faceCnt  = (faceEnd >= faceBeg) ? (faceEnd - faceBeg) / 6u : 0u;

    std::fprintf(stderr,
        "[ghpc/mesh/sync-in] #%llu this=0x%08x sync=0x%02x verts=%d faces=%u mat=0x%08x packetQw=%u packet=0x%08x owner=0x%08x flags140=0x%08x\n",
        (unsigned long long)n, self, syncFlags, vertCnt, faceCnt, mat, packetQw, packet, owner, flags140);

    const int nv = vertCnt < 4 ? (vertCnt < 0 ? 0 : vertCnt) : 4;
    for (int i = 0; i < nv && vertPtr != 0; ++i) {
        const uint32_t v = vertPtr + (uint32_t)i * 64u;
        std::fprintf(stderr,
            "[ghpc/mesh/sync-in]   v%d pos=(%g %g %g) norm=(%g %g %g) uv=(%g %g) color=(%g %g %g %g)\n", i,
            ghpcMeshF32(rdram, ctx, runtime, v + 0x00u), ghpcMeshF32(rdram, ctx, runtime, v + 0x04u), ghpcMeshF32(rdram, ctx, runtime, v + 0x08u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x10u), ghpcMeshF32(rdram, ctx, runtime, v + 0x14u), ghpcMeshF32(rdram, ctx, runtime, v + 0x18u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x30u), ghpcMeshF32(rdram, ctx, runtime, v + 0x34u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x20u), ghpcMeshF32(rdram, ctx, runtime, v + 0x24u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x28u), ghpcMeshF32(rdram, ctx, runtime, v + 0x2cu));
    }

    const uint32_t nf = faceCnt < 4u ? faceCnt : 4u;
    for (uint32_t i = 0; i < nf && faceBeg != 0; ++i) {
        const uint32_t f = faceBeg + i * 6u;
        std::fprintf(stderr, "[ghpc/mesh/sync-in]   f%u %u %u %u\n", i,
            (unsigned)READ16(f + 0u), (unsigned)READ16(f + 2u), (unsigned)READ16(f + 4u));
    }

    // Cached RndTransformable::mWorldXfm at this+0x40+0x60. dirty is the word
    // WorldXfm tests before recomputing; nonzero means this matrix is stale.
    const uint32_t w = self + 0xa0u;
    std::fprintf(stderr,
        "[ghpc/mesh/sync-in]   world dirty=%u x=(%g %g %g) y=(%g %g %g) z=(%g %g %g) v=(%g %g %g)\n",
        READ32(self + 0xe0u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x00u), ghpcMeshF32(rdram, ctx, runtime, w + 0x04u), ghpcMeshF32(rdram, ctx, runtime, w + 0x08u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x10u), ghpcMeshF32(rdram, ctx, runtime, w + 0x14u), ghpcMeshF32(rdram, ctx, runtime, w + 0x18u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x20u), ghpcMeshF32(rdram, ctx, runtime, w + 0x24u), ghpcMeshF32(rdram, ctx, runtime, w + 0x28u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x30u), ghpcMeshF32(rdram, ctx, runtime, w + 0x34u), ghpcMeshF32(rdram, ctx, runtime, w + 0x38u));
}

} // namespace

// Function: Sync__6PsMeshi
// Address: 0x43afa8 - 0x43b148
void Sync__6PsMeshi_0x43afa8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Sync__6PsMeshi_0x43afa8");
#endif

    switch (ctx->pc) {
        case 0x43afd4u: goto label_43afd4;
        case 0x43afe8u: goto label_43afe8;
        case 0x43b0a0u: goto label_43b0a0;
        case 0x43b0e8u: goto label_43b0e8;
        case 0x43b0f0u: goto label_43b0f0;
        case 0x43b120u: goto label_43b120;
        case 0x43b130u: goto label_43b130;
        default: break;
    }

    // Fresh entry only (a resume above jumps past this). $a0 is `this`, $a1
    // the Sync flags. The vectors are still intact here: the first store that
    // zeroes them is 0x43b040, after UpdateFacePacket returns.
    {
        static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
        if (s_log) {
            static uint64_t s_calls = 0;
            const uint64_t n = ++s_calls;
            if (n <= 40 || (n % 200) == 0) {
                ghpcMeshLog(rdram, ctx, runtime, GPR_U32(ctx, 4), GPR_U32(ctx, 5), n);
            }
        }
    }

    ctx->pc = 0x43afa8u;

    // 0x43afa8: 0x27bdff30  addiu       $sp, $sp, -0xD0
    ctx->pc = 0x43afa8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967088));
    // 0x43afac: 0x7fb000c0  sq          $s0, 0xC0($sp)
    ctx->pc = 0x43afacu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 192), GPR_VEC(ctx, 16));
    // 0x43afb0: 0x7fb100b0  sq          $s1, 0xB0($sp)
    ctx->pc = 0x43afb0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 176), GPR_VEC(ctx, 17));
    // 0x43afb4: 0x7fb200a0  sq          $s2, 0xA0($sp)
    ctx->pc = 0x43afb4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 18));
    // 0x43afb8: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x43afb8u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43afbc: 0xffbf0090  sd          $ra, 0x90($sp)
    ctx->pc = 0x43afbcu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 144), GPR_U64(ctx, 31));
    // 0x43afc0: 0x8e220138  lw          $v0, 0x138($s1)
    ctx->pc = 0x43afc0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 312)));
    // 0x43afc4: 0x1451005a  bne         $v0, $s1, . + 4 + (0x5A << 2)
    ctx->pc = 0x43AFC4u;
    {
        const bool branch_taken_0x43afc4 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 17));
        ctx->pc = 0x43AFC8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43AFC4u;
        // 0x43afc8: 0xa0802d  daddu       $s0, $a1, $zero (Delay Slot)
        SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43afc4) {
            ctx->pc = 0x43B130u;
            goto label_43b130;
        }
    }
    ctx->pc = 0x43AFCCu;
    // 0x43afcc: 0xc10f2e6  jal         func_43CB98
    ctx->pc = 0x43AFCCu;
    SET_GPR_U32(ctx, 31, 0x43AFD4u);
    ctx->pc = 0x43AFD0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43AFCCu;
    // 0x43afd0: 0x3212001f  andi        $s2, $s0, 0x1F (Delay Slot)
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 16) & (uint64_t)(uint16_t)31);
    ctx->in_delay_slot = false;
    ctx->pc = 0x43CB98u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43CB98u, 0x43AFCCu, 0x43AFD4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43AFD4u;
label_43afd4:
    // 0x43afd4: 0x32020020  andi        $v0, $s0, 0x20
    ctx->pc = 0x43afd4u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 16) & (uint64_t)(uint16_t)32);
    // 0x43afd8: 0x10400045  beqz        $v0, . + 4 + (0x45 << 2)
    ctx->pc = 0x43AFD8u;
    {
        const bool branch_taken_0x43afd8 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x43afd8) {
            ctx->pc = 0x43B0F0u;
            goto label_43b0f0;
        }
    }
    ctx->pc = 0x43AFE0u;
    // 0x43afe0: 0xc10ea04  jal         func_43A810
    ctx->pc = 0x43AFE0u;
    SET_GPR_U32(ctx, 31, 0x43AFE8u);
    ctx->pc = 0x43AFE4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43AFE0u;
    // 0x43afe4: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43A810u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43A810u, 0x43AFE0u, 0x43AFE8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43AFE8u;
label_43afe8:
    // 0x43afe8: 0x8e260108  lw          $a2, 0x108($s1)
    ctx->pc = 0x43afe8u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 264)));
    // 0x43afec: 0x3c03aaaa  lui         $v1, 0xAAAA
    ctx->pc = 0x43afecu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)43690 << 16));
    // 0x43aff0: 0x8e22010c  lw          $v0, 0x10C($s1)
    ctx->pc = 0x43aff0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 268)));
    // 0x43aff4: 0x3463aaab  ori         $v1, $v1, 0xAAAB
    ctx->pc = 0x43aff4u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)43691);
    // 0x43aff8: 0x8e240140  lw          $a0, 0x140($s1)
    ctx->pc = 0x43aff8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 320)));
    // 0x43affc: 0x461023  subu        $v0, $v0, $a2
    ctx->pc = 0x43affcu;
    SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 2), GPR_U32(ctx, 6)));
    // 0x43b000: 0x431018  mult        $v0, $v0, $v1
    ctx->pc = 0x43b000u;
    { int64_t result = (int64_t)GPR_S32(ctx, 2) * (int64_t)GPR_S32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 2, (int32_t)result); }
    // 0x43b004: 0x30840020  andi        $a0, $a0, 0x20
    ctx->pc = 0x43b004u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) & (uint64_t)(uint16_t)32);
    // 0x43b008: 0x21043  sra         $v0, $v0, 1
    ctx->pc = 0x43b008u;
    SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 2), 1));
    // 0x43b00c: 0x14800038  bnez        $a0, . + 4 + (0x38 << 2)
    ctx->pc = 0x43B00Cu;
    {
        const bool branch_taken_0x43b00c = (GPR_U64(ctx, 4) != GPR_U64(ctx, 0));
        ctx->pc = 0x43B010u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B00Cu;
        // 0x43b010: 0xa6220158  sh          $v0, 0x158($s1) (Delay Slot)
        WRITE16(ADD32(GPR_U32(ctx, 17), 344), (uint16_t)GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b00c) {
            ctx->pc = 0x43B0F0u;
            goto label_43b0f0;
        }
    }
    ctx->pc = 0x43B014u;
    // 0x43b014: 0xafa00000  sw          $zero, 0x0($sp)
    ctx->pc = 0x43b014u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 0));
    // 0x43b018: 0x27a50008  addiu       $a1, $sp, 0x8
    ctx->pc = 0x43b018u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 29), 8));
    // 0x43b01c: 0xafa00004  sw          $zero, 0x4($sp)
    ctx->pc = 0x43b01cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 0));
    // 0x43b020: 0x3a0382d  daddu       $a3, $sp, $zero
    ctx->pc = 0x43b020u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43b024: 0xaca00004  sw          $zero, 0x4($a1)
    ctx->pc = 0x43b024u;
    WRITE32(ADD32(GPR_U32(ctx, 5), 4), GPR_U32(ctx, 0));
    // 0x43b028: 0x26300124  addiu       $s0, $s1, 0x124
    ctx->pc = 0x43b028u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 17), 292));
    // 0x43b02c: 0x8e22010c  lw          $v0, 0x10C($s1)
    ctx->pc = 0x43b02cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 268)));
    // 0x43b030: 0x8fa30000  lw          $v1, 0x0($sp)
    ctx->pc = 0x43b030u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43b034: 0x8fa40004  lw          $a0, 0x4($sp)
    ctx->pc = 0x43b034u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 4)));
    // 0x43b038: 0xafa60000  sw          $a2, 0x0($sp)
    ctx->pc = 0x43b038u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 6));
    // 0x43b03c: 0xafa20004  sw          $v0, 0x4($sp)
    ctx->pc = 0x43b03cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 2));
    // 0x43b040: 0xae230108  sw          $v1, 0x108($s1)
    ctx->pc = 0x43b040u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 264), GPR_U32(ctx, 3));
    // 0x43b044: 0x8ca20004  lw          $v0, 0x4($a1)
    ctx->pc = 0x43b044u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 5), 4)));
    // 0x43b048: 0xae24010c  sw          $a0, 0x10C($s1)
    ctx->pc = 0x43b048u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 268), GPR_U32(ctx, 4));
    // 0x43b04c: 0xafa20024  sw          $v0, 0x24($sp)
    ctx->pc = 0x43b04cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 36), GPR_U32(ctx, 2));
    // 0x43b050: 0x6a220117  ldl         $v0, 0x117($s1)
    ctx->pc = 0x43b050u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 17), 279); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = (7u - offset) << 3; uint64_t keepMask = (shift == 0) ? 0ull : ((1ull << shift) - 1ull); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem << shift)); }
    // 0x43b054: 0x6e220110  ldr         $v0, 0x110($s1)
    ctx->pc = 0x43b054u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 17), 272); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = offset << 3; uint64_t keepMask = (offset == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset) << 3)); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem >> shift)); }
    // 0x43b058: 0xb3a2000f  sdl         $v0, 0xF($sp)
    ctx->pc = 0x43b058u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 29), 15); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = (7u - offset) << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull >> shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val >> shift) & mask); WRITE64(aligned_addr, new_data); }
    // 0x43b05c: 0xb7a20008  sdr         $v0, 0x8($sp)
    ctx->pc = 0x43b05cu;
    { uint32_t addr = ADD32(GPR_U32(ctx, 29), 8); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = offset << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull << shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val << shift) & mask); WRITE64(aligned_addr, new_data); }
    // 0x43b060: 0x6ba20027  ldl         $v0, 0x27($sp)
    ctx->pc = 0x43b060u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 29), 39); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = (7u - offset) << 3; uint64_t keepMask = (shift == 0) ? 0ull : ((1ull << shift) - 1ull); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem << shift)); }
    // 0x43b064: 0x6fa20020  ldr         $v0, 0x20($sp)
    ctx->pc = 0x43b064u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 29), 32); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = offset << 3; uint64_t keepMask = (offset == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset) << 3)); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem >> shift)); }
    // 0x43b068: 0xb2220117  sdl         $v0, 0x117($s1)
    ctx->pc = 0x43b068u;
    { uint32_t addr = ADD32(GPR_U32(ctx, 17), 279); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = (7u - offset) << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull >> shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val >> shift) & mask); WRITE64(aligned_addr, new_data); }
    // 0x43b06c: 0xb6220110  sdr         $v0, 0x110($s1)
    ctx->pc = 0x43b06cu;
    { uint32_t addr = ADD32(GPR_U32(ctx, 17), 272); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = offset << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull << shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val << shift) & mask); WRITE64(aligned_addr, new_data); }
    // 0x43b070: 0x8fa30004  lw          $v1, 0x4($sp)
    ctx->pc = 0x43b070u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 4)));
    // 0x43b074: 0x8fa20000  lw          $v0, 0x0($sp)
    ctx->pc = 0x43b074u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43b078: 0xafa30034  sw          $v1, 0x34($sp)
    ctx->pc = 0x43b078u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 52), GPR_U32(ctx, 3));
    // 0x43b07c: 0xafa20044  sw          $v0, 0x44($sp)
    ctx->pc = 0x43b07cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 68), GPR_U32(ctx, 2));
    // 0x43b080: 0xafa30054  sw          $v1, 0x54($sp)
    ctx->pc = 0x43b080u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 84), GPR_U32(ctx, 3));
    // 0x43b084: 0xafa20064  sw          $v0, 0x64($sp)
    ctx->pc = 0x43b084u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 100), GPR_U32(ctx, 2));
    // 0x43b088: 0xafa30074  sw          $v1, 0x74($sp)
    ctx->pc = 0x43b088u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 116), GPR_U32(ctx, 3));
    // 0x43b08c: 0x1062000b  beq         $v1, $v0, . + 4 + (0xB << 2)
    ctx->pc = 0x43B08Cu;
    {
        const bool branch_taken_0x43b08c = (GPR_U64(ctx, 3) == GPR_U64(ctx, 2));
        ctx->pc = 0x43B090u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B08Cu;
        // 0x43b090: 0xafa20084  sw          $v0, 0x84($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 132), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b08c) {
            ctx->pc = 0x43B0BCu;
            goto label_43b0bc;
        }
    }
    ctx->pc = 0x43B094u;
    // 0x43b094: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x43b094u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43b098: 0x60102d  daddu       $v0, $v1, $zero
    ctx->pc = 0x43b098u;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43b09c: 0x2442fffa  addiu       $v0, $v0, -0x6
    ctx->pc = 0x43b09cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967290));
label_43b0a0:
    // 0x43b0a0: 0x0  nop
    ctx->pc = 0x43b0a0u;
    // NOP
    // 0x43b0a4: 0x0  nop
    ctx->pc = 0x43b0a4u;
    // NOP
    // 0x43b0a8: 0x0  nop
    ctx->pc = 0x43b0a8u;
    // NOP
    // 0x43b0ac: 0x0  nop
    ctx->pc = 0x43b0acu;
    // NOP
    // 0x43b0b0: 0x5444fffb  bnel        $v0, $a0, . + 4 + (-0x5 << 2)
    ctx->pc = 0x43B0B0u;
    {
        const bool branch_taken_0x43b0b0 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 4));
        if (branch_taken_0x43b0b0) {
            ctx->pc = 0x43B0B4u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43B0B0u;
            // 0x43b0b4: 0x2442fffa  addiu       $v0, $v0, -0x6 (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967290));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43B0A0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43b0a0;
        }
    }
    ctx->pc = 0x43B0B8u;
    // 0x43b0b8: 0xafa20074  sw          $v0, 0x74($sp)
    ctx->pc = 0x43b0b8u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 116), GPR_U32(ctx, 2));
label_43b0bc:
    // 0x43b0bc: 0x8ce50000  lw          $a1, 0x0($a3)
    ctx->pc = 0x43b0bcu;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x43b0c0: 0x10a00009  beqz        $a1, . + 4 + (0x9 << 2)
    ctx->pc = 0x43B0C0u;
    {
        const bool branch_taken_0x43b0c0 = (GPR_U64(ctx, 5) == GPR_U64(ctx, 0));
        ctx->pc = 0x43B0C4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B0C0u;
        // 0x43b0c4: 0x24030006  addiu       $v1, $zero, 0x6 (Delay Slot)
        SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 6));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b0c0) {
            ctx->pc = 0x43B0E8u;
            goto label_43b0e8;
        }
    }
    ctx->pc = 0x43B0C8u;
    // 0x43b0c8: 0x8ce4000c  lw          $a0, 0xC($a3)
    ctx->pc = 0x43b0c8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 7), 12)));
    // 0x43b0cc: 0x3c02aaaa  lui         $v0, 0xAAAA
    ctx->pc = 0x43b0ccu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)43690 << 16));
    // 0x43b0d0: 0x3442aaab  ori         $v0, $v0, 0xAAAB
    ctx->pc = 0x43b0d0u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)43691);
    // 0x43b0d4: 0x852023  subu        $a0, $a0, $a1
    ctx->pc = 0x43b0d4u;
    SET_GPR_S32(ctx, 4, (int32_t)SUB32(GPR_U32(ctx, 4), GPR_U32(ctx, 5)));
    // 0x43b0d8: 0x822018  mult        $a0, $a0, $v0
    ctx->pc = 0x43b0d8u;
    { int64_t result = (int64_t)GPR_S32(ctx, 4) * (int64_t)GPR_S32(ctx, 2); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 4, (int32_t)result); }
    // 0x43b0dc: 0x42043  sra         $a0, $a0, 1
    ctx->pc = 0x43b0dcu;
    SET_GPR_S32(ctx, 4, SRA32(GPR_S32(ctx, 4), 1));
    // 0x43b0e0: 0xc0c85d8  jal         func_321760
    ctx->pc = 0x43B0E0u;
    SET_GPR_U32(ctx, 31, 0x43B0E8u);
    ctx->pc = 0x43B0E4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43B0E0u;
    // 0x43b0e4: 0x832018  mult        $a0, $a0, $v1 (Delay Slot)
    { int64_t result = (int64_t)GPR_S32(ctx, 4) * (int64_t)GPR_S32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 4, (int32_t)result); }
    ctx->in_delay_slot = false;
    ctx->pc = 0x321760u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321760u, 0x43B0E0u, 0x43B0E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43B0E8u;
label_43b0e8:
    // 0x43b0e8: 0xc0ebebe  jal         func_3AFAF8
    ctx->pc = 0x43B0E8u;
    SET_GPR_U32(ctx, 31, 0x43B0F0u);
    ctx->pc = 0x43B0ECu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43B0E8u;
    // 0x43b0ec: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3AFAF8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3AFAF8u, 0x43B0E8u, 0x43B0F0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43B0F0u;
label_43b0f0:
    // 0x43b0f0: 0x12400010  beqz        $s2, . + 4 + (0x10 << 2)
    ctx->pc = 0x43B0F0u;
    {
        const bool branch_taken_0x43b0f0 = (GPR_U64(ctx, 18) == GPR_U64(ctx, 0));
        ctx->pc = 0x43B0F4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B0F0u;
        // 0x43b0f4: 0x7bb000c0  lq          $s0, 0xC0($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 192)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b0f0) {
            ctx->pc = 0x43B134u;
            goto label_43b134;
        }
    }
    ctx->pc = 0x43B0F8u;
    // 0x43b0f8: 0x8e220140  lw          $v0, 0x140($s1)
    ctx->pc = 0x43b0f8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 320)));
    // 0x43b0fc: 0x3042001f  andi        $v0, $v0, 0x1F
    ctx->pc = 0x43b0fcu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)31);
    // 0x43b100: 0x10400009  beqz        $v0, . + 4 + (0x9 << 2)
    ctx->pc = 0x43B100u;
    {
        const bool branch_taken_0x43b100 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43B104u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B100u;
        // 0x43b104: 0x26240100  addiu       $a0, $s1, 0x100 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 17), 256));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b100) {
            ctx->pc = 0x43B128u;
            goto label_43b128;
        }
    }
    ctx->pc = 0x43B108u;
    // 0x43b108: 0x8e250104  lw          $a1, 0x104($s1)
    ctx->pc = 0x43b108u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 260)));
    // 0x43b10c: 0x8e240100  lw          $a0, 0x100($s1)
    ctx->pc = 0x43b10cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 256)));
    // 0x43b110: 0x52980  sll         $a1, $a1, 6
    ctx->pc = 0x43b110u;
    SET_GPR_S32(ctx, 5, (int32_t)SLL32(GPR_U32(ctx, 5), 6));
    // 0x43b114: 0x852821  addu        $a1, $a0, $a1
    ctx->pc = 0x43b114u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 4), GPR_U32(ctx, 5)));
    // 0x43b118: 0xc0d2ec8  jal         func_34BB20
    ctx->pc = 0x43B118u;
    SET_GPR_U32(ctx, 31, 0x43B120u);
    ctx->pc = 0x43B11Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43B118u;
    // 0x43b11c: 0x24a5ffff  addiu       $a1, $a1, -0x1 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294967295));
    ctx->in_delay_slot = false;
    ctx->pc = 0x34BB20u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x34BB20u, 0x43B118u, 0x43B120u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43B120u;
label_43b120:
    // 0x43b120: 0x10000004  b           . + 4 + (0x4 << 2)
    ctx->pc = 0x43B120u;
    {
        const bool branch_taken_0x43b120 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43B124u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B120u;
        // 0x43b124: 0x7bb000c0  lq          $s0, 0xC0($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 192)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43b120) {
            ctx->pc = 0x43B134u;
            goto label_43b134;
        }
    }
    ctx->pc = 0x43B128u;
label_43b128:
    // 0x43b128: 0xc07c74c  jal         func_1F1D30
    ctx->pc = 0x43B128u;
    SET_GPR_U32(ctx, 31, 0x43B130u);
    ctx->pc = 0x43B12Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43B128u;
    // 0x43b12c: 0x282d  daddu       $a1, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1F1D30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1F1D30u, 0x43B128u, 0x43B130u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43B130u;
label_43b130:
    // 0x43b130: 0x7bb000c0  lq          $s0, 0xC0($sp)
    ctx->pc = 0x43b130u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 192)));
label_43b134:
    // 0x43b134: 0x7bb100b0  lq          $s1, 0xB0($sp)
    ctx->pc = 0x43b134u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 176)));
    // 0x43b138: 0x7bb200a0  lq          $s2, 0xA0($sp)
    ctx->pc = 0x43b138u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x43b13c: 0xdfbf0090  ld          $ra, 0x90($sp)
    ctx->pc = 0x43b13cu;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x43b140: 0x3e00008  jr          $ra
    ctx->pc = 0x43B140u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43B144u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43B140u;
        // 0x43b144: 0x27bd00d0  addiu       $sp, $sp, 0xD0 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 208));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43B140u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43B148u;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&Sync__6PsMeshi_0x43afa8), PS2Runtime::RecompiledFunction>::value,
              "Sync__6PsMeshi_0x43afa8 override no longer matches the recompiled signature");
