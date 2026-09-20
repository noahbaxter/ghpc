// Override of work/output/Select__5PsMatRUl_0x43ea70.cpp (PsMat::Select
// (unsigned long &), 0x43ea70 - 0x43ee9c).
//
// Why: the material bind. The body keeps the generated translation verbatim
// and, when GHPC_MESH_LOG is set, prints on entry every field the body reads
// off `this`, so a native backend can be designed from the material's data
// rather than from the GS register writes and the VU1 unpack it builds.
//
// What the body does with `this` ($s1) and the out param ($s2, a u64&):
//   0x43ea8c  lw  +0x11c   dirty flags; 0 and this == sLastMat (0x440e0c) is
//                          the "already bound" fast path (re-upload only)
//   0x43eb50  +0x11c & 2   -> jal PsMat::Update 0x1c23a8, then +0x11c <- 0
//   0x43eb6c  ld  +0x138   -> SetRegister(0x42 ALPHA_1, v, mask 0xFF000000FF)
//   0x43eb90  ld  +0x140   -> SetRegister(0x44 DIMX,    v, mask 0x7777777777777777)
//   0x43ebbc  ld  +0x148   -> SetRegister(0x47 TEST_1,  v, mask 0x6FFFF)
//   0x43ebd4  lw  +0xa4    -> SetRegister(0x4a FBA_1,   !v, mask 1)
//   0x43ebf4  ld  +0x150   -> SetRegister(0x08 CLAMP_1, v, mask 0xF)
//   0x43ec08  ld  +0x158   -> SetRegister(0x4e ZBUF_1,  v, mask 1<<32 ZMSK)
//   0x43ec20  lw  +0x134   PsTex*; nonzero -> PsTex::Select(tex, +0x130)
//   0x43ec44  VIF UNPACK 0x6C0802B0 (V4-32, NUM 8, ADDR 688), then 8 qw:
//             qw688 = +0x124 ? (+0x40 ? 0x7C5 : *0x4404e24, +0x9c, 0, +0x40)
//                            : (0x614, 1, 1, 0)
//             qw689 = (+0x12c, 0, 0, 0)
//             qw690 = lq +0x30 (mColor); w <- 0.035f when +0x130 != 2
//             qw691..694 = +0x12c == 0x347 ? 4 qw from 0x4f3210 (after
//                          UpdateSphereXfm 0x1c1f40) : 4 qw from +0x170
//             qw695 = lq +0x160
//   0x43ee14  *out |= 8 | (+0x134 ? 0x10 : 0) | (bit5 from +0x128 and
//             *0x444c70->+0x50) | (+0x2c == 1 ? 0x40 : 0)
//   0x43ee84  returns +0xb0 in $v0 (mNextPass.mPtr, the ObjPtr at +0xa8)
// SetRegister is SetRegister__5PsRndiUlUl (PsRnd*, int reg, u64 value,
// u64 mask); the register numbers are GS register addresses and the masks
// match those registers' field layouts, which is how the names above are
// pinned (high confidence).
//
// Field names, from gh2-decomp src/rndobj/Mat.h (RndMat::SyncProperty names
// every property; sizeof(RndMat) is 0x120, so +0x120.. is PsMat's own):
//   +0x2c  int   mBlend          high   +0x30  Hmx::Color mColor    high
//   +0x40  bool  mUseEnviron     high   +0x9c  bool mPrelit         high
//   +0xa4  bool  mAlphaWrite     high   +0xb0  RndMat* mNextPass    high
//   +0x11c int   dirty flags     high (unnamed there, "mUnk11C")
//   +0x124 +0x128 +0x12c +0x130 +0x134 +0x138.. +0x170: PsMat's own, not in
//   the decomp. Roles above come from this body only: +0x130 is the TFX
//   passed to PsTex::Select (medium), +0x134 the PsTex* (high), +0x12c a
//   mode word whose 0x347 value selects the sphere-map path (medium), the
//   u64s at +0x138..+0x158 are GS register images (high), +0x170 a
//   Transform-shaped 4 qw block (low), +0x124/+0x128/+0x160 unnamed.
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
inline float ghpcMatF32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    const uint32_t bits = READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void ghpcMatLog(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self, uint32_t outPtr, uint64_t n) {
    const int32_t  blend    = (int32_t)READ32(self + 0x2cu);
    const uint32_t useEnv   = READ32(self + 0x40u);
    const uint32_t prelit   = READ32(self + 0x9cu);
    const uint32_t alphaWr  = READ32(self + 0xa4u);
    const uint32_t nextPass = READ32(self + 0xb0u);
    const uint32_t dirty    = READ32(self + 0x11cu);
    const uint32_t w124     = READ32(self + 0x124u);
    const uint32_t w128     = READ32(self + 0x128u);
    const uint32_t mode     = READ32(self + 0x12cu);
    const uint32_t tfx      = READ32(self + 0x130u);
    const uint32_t tex      = READ32(self + 0x134u);
    const uint64_t alpha    = READ64(self + 0x138u);
    const uint64_t dimx     = READ64(self + 0x140u);
    const uint64_t test     = READ64(self + 0x148u);
    const uint64_t clamp    = READ64(self + 0x150u);
    const uint64_t zbuf     = READ64(self + 0x158u);
    const uint64_t out      = outPtr ? READ64(outPtr) : 0ull;

    std::fprintf(stderr,
        "[ghpc/mat] #%llu this=0x%08x blend2c=%d useEnviron40=%u prelit9c=%u alphaWrite_a4=%u nextPass_b0=0x%08x dirty11c=0x%08x w124=0x%08x w128=0x%08x mode12c=0x%08x tfx130=%u tex134=0x%08x out=0x%016llx\n",
        (unsigned long long)n, self, blend, useEnv, prelit, alphaWr, nextPass, dirty, w124, w128, mode, tfx, tex,
        (unsigned long long)out);
    std::fprintf(stderr,
        "[ghpc/mat]   color30=(%g %g %g %g) alpha138=0x%016llx dimx140=0x%016llx test148=0x%016llx clamp150=0x%016llx zbuf158=0x%016llx qw160=(0x%08x 0x%08x 0x%08x 0x%08x)\n",
        ghpcMatF32(rdram, ctx, runtime, self + 0x30u), ghpcMatF32(rdram, ctx, runtime, self + 0x34u),
        ghpcMatF32(rdram, ctx, runtime, self + 0x38u), ghpcMatF32(rdram, ctx, runtime, self + 0x3cu),
        (unsigned long long)alpha, (unsigned long long)dimx, (unsigned long long)test,
        (unsigned long long)clamp, (unsigned long long)zbuf,
        READ32(self + 0x160u), READ32(self + 0x164u), READ32(self + 0x168u), READ32(self + 0x16cu));
    // The 4 qw at +0x170 go to VU1 qw691..694 unless mode12c == 0x347.
    const uint32_t x = self + 0x170u;
    std::fprintf(stderr,
        "[ghpc/mat]   xfm170 r0=(%g %g %g %g) r1=(%g %g %g %g) r2=(%g %g %g %g) r3=(%g %g %g %g)\n",
        ghpcMatF32(rdram, ctx, runtime, x + 0x00u), ghpcMatF32(rdram, ctx, runtime, x + 0x04u), ghpcMatF32(rdram, ctx, runtime, x + 0x08u), ghpcMatF32(rdram, ctx, runtime, x + 0x0cu),
        ghpcMatF32(rdram, ctx, runtime, x + 0x10u), ghpcMatF32(rdram, ctx, runtime, x + 0x14u), ghpcMatF32(rdram, ctx, runtime, x + 0x18u), ghpcMatF32(rdram, ctx, runtime, x + 0x1cu),
        ghpcMatF32(rdram, ctx, runtime, x + 0x20u), ghpcMatF32(rdram, ctx, runtime, x + 0x24u), ghpcMatF32(rdram, ctx, runtime, x + 0x28u), ghpcMatF32(rdram, ctx, runtime, x + 0x2cu),
        ghpcMatF32(rdram, ctx, runtime, x + 0x30u), ghpcMatF32(rdram, ctx, runtime, x + 0x34u), ghpcMatF32(rdram, ctx, runtime, x + 0x38u), ghpcMatF32(rdram, ctx, runtime, x + 0x3cu));
}

} // namespace

// Function: Select__5PsMatRUl
// Address: 0x43ea70 - 0x43ee9c
void Select__5PsMatRUl_0x43ea70(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("Select__5PsMatRUl_0x43ea70");
#endif

    switch (ctx->pc) {
        case 0x43eab4u: goto label_43eab4;
        case 0x43eb08u: goto label_43eb08;
        case 0x43eb68u: goto label_43eb68;
        case 0x43eb90u: goto label_43eb90;
        case 0x43ebbcu: goto label_43ebbc;
        case 0x43ebd4u: goto label_43ebd4;
        case 0x43ebf4u: goto label_43ebf4;
        case 0x43ec08u: goto label_43ec08;
        case 0x43ec20u: goto label_43ec20;
        case 0x43ec34u: goto label_43ec34;
        case 0x43ec44u: goto label_43ec44;
        case 0x43ed7cu: goto label_43ed7c;
        case 0x43ed90u: goto label_43ed90;
        case 0x43edd0u: goto label_43edd0;
        default: break;
    }

    // Fresh entry only (a resume above jumps past this). $a0 is `this`, $a1
    // the u64& the body ORs its result flags into.
    {
        static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
        if (s_log) {
            static uint64_t s_calls = 0;
            const uint64_t n = ++s_calls;
            if (n <= 40 || (n % 500) == 0) {
                ghpcMatLog(rdram, ctx, runtime, GPR_U32(ctx, 4), GPR_U32(ctx, 5), n);
            }
        }
    }

    ctx->pc = 0x43ea70u;

    // 0x43ea70: 0x27bdffc0  addiu       $sp, $sp, -0x40
    ctx->pc = 0x43ea70u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967232));
    // 0x43ea74: 0x3c030044  lui         $v1, 0x44
    ctx->pc = 0x43ea74u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)68 << 16));
    // 0x43ea78: 0x7fb10020  sq          $s1, 0x20($sp)
    ctx->pc = 0x43ea78u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 32), GPR_VEC(ctx, 17));
    // 0x43ea7c: 0x7fb20010  sq          $s2, 0x10($sp)
    ctx->pc = 0x43ea7cu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 16), GPR_VEC(ctx, 18));
    // 0x43ea80: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x43ea80u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ea84: 0x7fb00030  sq          $s0, 0x30($sp)
    ctx->pc = 0x43ea84u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 48), GPR_VEC(ctx, 16));
    // 0x43ea88: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x43ea88u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x43ea8c: 0x8e22011c  lw          $v0, 0x11C($s1)
    ctx->pc = 0x43ea8cu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 284)));
    // 0x43ea90: 0x14400029  bnez        $v0, . + 4 + (0x29 << 2)
    ctx->pc = 0x43EA90u;
    {
        const bool branch_taken_0x43ea90 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x43EA94u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EA90u;
        // 0x43ea94: 0xa0902d  daddu       $s2, $a1, $zero (Delay Slot)
        SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ea90) {
            ctx->pc = 0x43EB38u;
            goto label_43eb38;
        }
    }
    ctx->pc = 0x43EA98u;
    // 0x43ea98: 0x8c620e0c  lw          $v0, 0xE0C($v1)
    ctx->pc = 0x43ea98u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 3596)));
    // 0x43ea9c: 0x14510027  bne         $v0, $s1, . + 4 + (0x27 << 2)
    ctx->pc = 0x43EA9Cu;
    {
        const bool branch_taken_0x43ea9c = (GPR_U64(ctx, 2) != GPR_U64(ctx, 17));
        ctx->pc = 0x43EAA0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EA9Cu;
        // 0x43eaa0: 0x3c02004f  lui         $v0, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ea9c) {
            ctx->pc = 0x43EB3Cu;
            goto label_43eb3c;
        }
    }
    ctx->pc = 0x43EAA4u;
    // 0x43eaa4: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43eaa4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43eaa8: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43eaa8u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43eaac: 0xc10fa18  jal         func_43E860
    ctx->pc = 0x43EAACu;
    SET_GPR_U32(ctx, 31, 0x43EAB4u);
    ctx->pc = 0x43EAB0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EAACu;
    // 0x43eab0: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E860u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E860u, 0x43EAACu, 0x43EAB4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EAB4u;
label_43eab4:
    // 0x43eab4: 0x8e23012c  lw          $v1, 0x12C($s1)
    ctx->pc = 0x43eab4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 300)));
    // 0x43eab8: 0x24020347  addiu       $v0, $zero, 0x347
    ctx->pc = 0x43eab8u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 839));
    // 0x43eabc: 0x146200d5  bne         $v1, $v0, . + 4 + (0xD5 << 2)
    ctx->pc = 0x43EABCu;
    {
        const bool branch_taken_0x43eabc = (GPR_U64(ctx, 3) != GPR_U64(ctx, 2));
        ctx->pc = 0x43EAC0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EABCu;
        // 0x43eac0: 0x262800a8  addiu       $t0, $s1, 0xA8 (Delay Slot)
        SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 17), 168));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43eabc) {
            ctx->pc = 0x43EE14u;
            goto label_43ee14;
        }
    }
    ctx->pc = 0x43EAC4u;
    // 0x43eac4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43eac4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43eac8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43eac8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43eacc: 0x3c056c04  lui         $a1, 0x6C04
    ctx->pc = 0x43eaccu;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)27652 << 16));
    // 0x43ead0: 0x34a502b3  ori         $a1, $a1, 0x2B3
    ctx->pc = 0x43ead0u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)691);
    // 0x43ead4: 0x3c061000  lui         $a2, 0x1000
    ctx->pc = 0x43ead4u;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)4096 << 16));
    // 0x43ead8: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43ead8u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43eadc: 0x3c04004f  lui         $a0, 0x4F
    ctx->pc = 0x43eadcu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)79 << 16));
    // 0x43eae0: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43eae0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43eae4: 0x24843210  addiu       $a0, $a0, 0x3210
    ctx->pc = 0x43eae4u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 12816));
    // 0x43eae8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43eae8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43eaec: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43eaecu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43eaf0: 0x24070003  addiu       $a3, $zero, 0x3
    ctx->pc = 0x43eaf0u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x43eaf4: 0xac65000c  sw          $a1, 0xC($v1)
    ctx->pc = 0x43eaf4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 5));
    // 0x43eaf8: 0xac660008  sw          $a2, 0x8($v1)
    ctx->pc = 0x43eaf8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 6));
    // 0x43eafc: 0x2405ffff  addiu       $a1, $zero, -0x1
    ctx->pc = 0x43eafcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43eb00: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x43eb00u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x43eb04: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x43eb04u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
label_43eb08:
    // 0x43eb08: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43eb08u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43eb0c: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43eb0cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43eb10: 0x24e7ffff  addiu       $a3, $a3, -0x1
    ctx->pc = 0x43eb10u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294967295));
    // 0x43eb14: 0x78820000  lq          $v0, 0x0($a0)
    ctx->pc = 0x43eb14u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x43eb18: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43eb18u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43eb1c: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43eb1cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43eb20: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43eb20u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43eb24: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43eb24u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43eb28: 0x14e5fff7  bne         $a3, $a1, . + 4 + (-0x9 << 2)
    ctx->pc = 0x43EB28u;
    {
        const bool branch_taken_0x43eb28 = (GPR_U64(ctx, 7) != GPR_U64(ctx, 5));
        ctx->pc = 0x43EB2Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EB28u;
        // 0x43eb2c: 0x24840010  addiu       $a0, $a0, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43eb28) {
            ctx->pc = 0x43EB08u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43eb08;
        }
    }
    ctx->pc = 0x43EB30u;
    // 0x43eb30: 0x100000b9  b           . + 4 + (0xB9 << 2)
    ctx->pc = 0x43EB30u;
    {
        const bool branch_taken_0x43eb30 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EB34u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EB30u;
        // 0x43eb34: 0xde420000  ld          $v0, 0x0($s2) (Delay Slot)
        SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 18), 0)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43eb30) {
            ctx->pc = 0x43EE18u;
            goto label_43ee18;
        }
    }
    ctx->pc = 0x43EB38u;
label_43eb38:
    // 0x43eb38: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x43eb38u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
label_43eb3c:
    // 0x43eb3c: 0xac710e0c  sw          $s1, 0xE0C($v1)
    ctx->pc = 0x43eb3cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 3596), GPR_U32(ctx, 17));
    // 0x43eb40: 0x24422cb8  addiu       $v0, $v0, 0x2CB8
    ctx->pc = 0x43eb40u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 11448));
    // 0x43eb44: 0x8c430010  lw          $v1, 0x10($v0)
    ctx->pc = 0x43eb44u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 16)));
    // 0x43eb48: 0x24630001  addiu       $v1, $v1, 0x1
    ctx->pc = 0x43eb48u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 1));
    // 0x43eb4c: 0xac430010  sw          $v1, 0x10($v0)
    ctx->pc = 0x43eb4cu;
    WRITE32(ADD32(GPR_U32(ctx, 2), 16), GPR_U32(ctx, 3));
    // 0x43eb50: 0x8e24011c  lw          $a0, 0x11C($s1)
    ctx->pc = 0x43eb50u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 284)));
    // 0x43eb54: 0x30840002  andi        $a0, $a0, 0x2
    ctx->pc = 0x43eb54u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) & (uint64_t)(uint16_t)2);
    // 0x43eb58: 0x10800004  beqz        $a0, . + 4 + (0x4 << 2)
    ctx->pc = 0x43EB58u;
    {
        const bool branch_taken_0x43eb58 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EB5Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EB58u;
        // 0x43eb5c: 0x3c10004f  lui         $s0, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43eb58) {
            ctx->pc = 0x43EB6Cu;
            goto label_43eb6c;
        }
    }
    ctx->pc = 0x43EB60u;
    // 0x43eb60: 0xc0708ea  jal         func_1C23A8
    ctx->pc = 0x43EB60u;
    SET_GPR_U32(ctx, 31, 0x43EB68u);
    ctx->pc = 0x43EB64u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EB60u;
    // 0x43eb64: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C23A8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C23A8u, 0x43EB60u, 0x43EB68u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EB68u;
label_43eb68:
    // 0x43eb68: 0x3c10004f  lui         $s0, 0x4F
    ctx->pc = 0x43eb68u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)79 << 16));
label_43eb6c:
    // 0x43eb6c: 0xde260138  ld          $a2, 0x138($s1)
    ctx->pc = 0x43eb6cu;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 312)));
    // 0x43eb70: 0x26102780  addiu       $s0, $s0, 0x2780
    ctx->pc = 0x43eb70u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 10112));
    // 0x43eb74: 0xae20011c  sw          $zero, 0x11C($s1)
    ctx->pc = 0x43eb74u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 284), GPR_U32(ctx, 0));
    // 0x43eb78: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43eb78u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43eb7c: 0x3407ff00  ori         $a3, $zero, 0xFF00
    ctx->pc = 0x43eb7cu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65280);
    // 0x43eb80: 0x73e38  dsll        $a3, $a3, 24
    ctx->pc = 0x43eb80u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 24);
    // 0x43eb84: 0x34e700ff  ori         $a3, $a3, 0xFF
    ctx->pc = 0x43eb84u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)255);
    // 0x43eb88: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EB88u;
    SET_GPR_U32(ctx, 31, 0x43EB90u);
    ctx->pc = 0x43EB8Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EB88u;
    // 0x43eb8c: 0x24050042  addiu       $a1, $zero, 0x42 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 66));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EB88u, 0x43EB90u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EB90u;
label_43eb90:
    // 0x43eb90: 0xde260140  ld          $a2, 0x140($s1)
    ctx->pc = 0x43eb90u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 320)));
    // 0x43eb94: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43eb94u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43eb98: 0x3407eeee  ori         $a3, $zero, 0xEEEE
    ctx->pc = 0x43eb98u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)61166);
    // 0x43eb9c: 0x73c38  dsll        $a3, $a3, 16
    ctx->pc = 0x43eb9cu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 16);
    // 0x43eba0: 0x34e7eeee  ori         $a3, $a3, 0xEEEE
    ctx->pc = 0x43eba0u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)61166);
    // 0x43eba4: 0x73c38  dsll        $a3, $a3, 16
    ctx->pc = 0x43eba4u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 16);
    // 0x43eba8: 0x34e7eeee  ori         $a3, $a3, 0xEEEE
    ctx->pc = 0x43eba8u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)61166);
    // 0x43ebac: 0x73bf8  dsll        $a3, $a3, 15
    ctx->pc = 0x43ebacu;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 15);
    // 0x43ebb0: 0x34e77777  ori         $a3, $a3, 0x7777
    ctx->pc = 0x43ebb0u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)30583);
    // 0x43ebb4: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EBB4u;
    SET_GPR_U32(ctx, 31, 0x43EBBCu);
    ctx->pc = 0x43EBB8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EBB4u;
    // 0x43ebb8: 0x24050044  addiu       $a1, $zero, 0x44 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 68));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EBB4u, 0x43EBBCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EBBCu;
label_43ebbc:
    // 0x43ebbc: 0xde260148  ld          $a2, 0x148($s1)
    ctx->pc = 0x43ebbcu;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 328)));
    // 0x43ebc0: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43ebc0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ebc4: 0x3c070006  lui         $a3, 0x6
    ctx->pc = 0x43ebc4u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)6 << 16));
    // 0x43ebc8: 0x34e7ffff  ori         $a3, $a3, 0xFFFF
    ctx->pc = 0x43ebc8u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) | (uint64_t)(uint16_t)65535);
    // 0x43ebcc: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EBCCu;
    SET_GPR_U32(ctx, 31, 0x43EBD4u);
    ctx->pc = 0x43EBD0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EBCCu;
    // 0x43ebd0: 0x24050047  addiu       $a1, $zero, 0x47 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 71));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EBCCu, 0x43EBD4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EBD4u;
label_43ebd4:
    // 0x43ebd4: 0x8e2600a4  lw          $a2, 0xA4($s1)
    ctx->pc = 0x43ebd4u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 164)));
    // 0x43ebd8: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43ebd8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ebdc: 0x2405004a  addiu       $a1, $zero, 0x4A
    ctx->pc = 0x43ebdcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 74));
    // 0x43ebe0: 0x24070001  addiu       $a3, $zero, 0x1
    ctx->pc = 0x43ebe0u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ebe4: 0x38c60001  xori        $a2, $a2, 0x1
    ctx->pc = 0x43ebe4u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) ^ (uint64_t)(uint16_t)1);
    // 0x43ebe8: 0x6303c  dsll32      $a2, $a2, 0
    ctx->pc = 0x43ebe8u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) << (32 + 0));
    // 0x43ebec: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EBECu;
    SET_GPR_U32(ctx, 31, 0x43EBF4u);
    ctx->pc = 0x43EBF0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EBECu;
    // 0x43ebf0: 0x6303e  dsrl32      $a2, $a2, 0 (Delay Slot)
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) >> (32 + 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EBECu, 0x43EBF4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EBF4u;
label_43ebf4:
    // 0x43ebf4: 0xde260150  ld          $a2, 0x150($s1)
    ctx->pc = 0x43ebf4u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 336)));
    // 0x43ebf8: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43ebf8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ebfc: 0x24050008  addiu       $a1, $zero, 0x8
    ctx->pc = 0x43ebfcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 8));
    // 0x43ec00: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EC00u;
    SET_GPR_U32(ctx, 31, 0x43EC08u);
    ctx->pc = 0x43EC04u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EC00u;
    // 0x43ec04: 0x2407000f  addiu       $a3, $zero, 0xF (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 0), 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EC00u, 0x43EC08u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EC08u;
label_43ec08:
    // 0x43ec08: 0xde260158  ld          $a2, 0x158($s1)
    ctx->pc = 0x43ec08u;
    SET_GPR_U64(ctx, 6, READ64(ADD32(GPR_U32(ctx, 17), 344)));
    // 0x43ec0c: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x43ec0cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ec10: 0x34078000  ori         $a3, $zero, 0x8000
    ctx->pc = 0x43ec10u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)32768);
    // 0x43ec14: 0x73c78  dsll        $a3, $a3, 17
    ctx->pc = 0x43ec14u;
    SET_GPR_U64(ctx, 7, GPR_U64(ctx, 7) << 17);
    // 0x43ec18: 0xc10f8c8  jal         func_43E320
    ctx->pc = 0x43EC18u;
    SET_GPR_U32(ctx, 31, 0x43EC20u);
    ctx->pc = 0x43EC1Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EC18u;
    // 0x43ec1c: 0x2405004e  addiu       $a1, $zero, 0x4E (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 78));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E320u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E320u, 0x43EC18u, 0x43EC20u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EC20u;
label_43ec20:
    // 0x43ec20: 0x8e240134  lw          $a0, 0x134($s1)
    ctx->pc = 0x43ec20u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 308)));
    // 0x43ec24: 0x50800004  beql        $a0, $zero, . + 4 + (0x4 << 2)
    ctx->pc = 0x43EC24u;
    {
        const bool branch_taken_0x43ec24 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x43ec24) {
            ctx->pc = 0x43EC28u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43EC24u;
            // 0x43ec28: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
            SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43EC38u;
            goto label_43ec38;
        }
    }
    ctx->pc = 0x43EC2Cu;
    // 0x43ec2c: 0xc10fd24  jal         func_43F490
    ctx->pc = 0x43EC2Cu;
    SET_GPR_U32(ctx, 31, 0x43EC34u);
    ctx->pc = 0x43EC30u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EC2Cu;
    // 0x43ec30: 0x8e250130  lw          $a1, 0x130($s1) (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 304)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F490u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F490u, 0x43EC2Cu, 0x43EC34u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EC34u;
label_43ec34:
    // 0x43ec34: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43ec34u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
label_43ec38:
    // 0x43ec38: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43ec38u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ec3c: 0xc10fa18  jal         func_43E860
    ctx->pc = 0x43EC3Cu;
    SET_GPR_U32(ctx, 31, 0x43EC44u);
    ctx->pc = 0x43EC40u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EC3Cu;
    // 0x43ec40: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E860u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E860u, 0x43EC3Cu, 0x43EC44u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EC44u;
label_43ec44:
    // 0x43ec44: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43ec44u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43ec48: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43ec48u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43ec4c: 0x3c046c08  lui         $a0, 0x6C08
    ctx->pc = 0x43ec4cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)27656 << 16));
    // 0x43ec50: 0x3c051000  lui         $a1, 0x1000
    ctx->pc = 0x43ec50u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)4096 << 16));
    // 0x43ec54: 0x348402b0  ori         $a0, $a0, 0x2B0
    ctx->pc = 0x43ec54u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)688);
    // 0x43ec58: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43ec58u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ec5c: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43ec5cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43ec60: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ec60u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ec64: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43ec64u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43ec68: 0xac64000c  sw          $a0, 0xC($v1)
    ctx->pc = 0x43ec68u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 4));
    // 0x43ec6c: 0xac650008  sw          $a1, 0x8($v1)
    ctx->pc = 0x43ec6cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 5));
    // 0x43ec70: 0xac600000  sw          $zero, 0x0($v1)
    ctx->pc = 0x43ec70u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 0));
    // 0x43ec74: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x43ec74u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x43ec78: 0x8e220124  lw          $v0, 0x124($s1)
    ctx->pc = 0x43ec78u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 292)));
    // 0x43ec7c: 0x10400013  beqz        $v0, . + 4 + (0x13 << 2)
    ctx->pc = 0x43EC7Cu;
    {
        const bool branch_taken_0x43ec7c = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EC80u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EC7Cu;
        // 0x43ec80: 0x24050001  addiu       $a1, $zero, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ec7c) {
            ctx->pc = 0x43ECCCu;
            goto label_43eccc;
        }
    }
    ctx->pc = 0x43EC84u;
    // 0x43ec84: 0x8e260040  lw          $a2, 0x40($s1)
    ctx->pc = 0x43ec84u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 64)));
    // 0x43ec88: 0x10c00003  beqz        $a2, . + 4 + (0x3 << 2)
    ctx->pc = 0x43EC88u;
    {
        const bool branch_taken_0x43ec88 = (GPR_U64(ctx, 6) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EC8Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EC88u;
        // 0x43ec8c: 0x3c020044  lui         $v0, 0x44 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ec88) {
            ctx->pc = 0x43EC98u;
            goto label_43ec98;
        }
    }
    ctx->pc = 0x43EC90u;
    // 0x43ec90: 0x10000002  b           . + 4 + (0x2 << 2)
    ctx->pc = 0x43EC90u;
    {
        const bool branch_taken_0x43ec90 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EC94u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EC90u;
        // 0x43ec94: 0x8c450e24  lw          $a1, 0xE24($v0) (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 3620)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ec90) {
            ctx->pc = 0x43EC9Cu;
            goto label_43ec9c;
        }
    }
    ctx->pc = 0x43EC98u;
label_43ec98:
    // 0x43ec98: 0x240507c5  addiu       $a1, $zero, 0x7C5
    ctx->pc = 0x43ec98u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1989));
label_43ec9c:
    // 0x43ec9c: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43ec9cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43eca0: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43eca0u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43eca4: 0x8e24009c  lw          $a0, 0x9C($s1)
    ctx->pc = 0x43eca4u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 156)));
    // 0x43eca8: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43eca8u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ecac: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43ecacu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43ecb0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ecb0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ecb4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43ecb4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43ecb8: 0xac66000c  sw          $a2, 0xC($v1)
    ctx->pc = 0x43ecb8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 6));
    // 0x43ecbc: 0xac650000  sw          $a1, 0x0($v1)
    ctx->pc = 0x43ecbcu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 5));
    // 0x43ecc0: 0xac640004  sw          $a0, 0x4($v1)
    ctx->pc = 0x43ecc0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 4));
    // 0x43ecc4: 0x1000000c  b           . + 4 + (0xC << 2)
    ctx->pc = 0x43ECC4u;
    {
        const bool branch_taken_0x43ecc4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43ECC8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43ECC4u;
        // 0x43ecc8: 0xac600008  sw          $zero, 0x8($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ecc4) {
            ctx->pc = 0x43ECF8u;
            goto label_43ecf8;
        }
    }
    ctx->pc = 0x43ECCCu;
label_43eccc:
    // 0x43eccc: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43ecccu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43ecd0: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43ecd0u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43ecd4: 0x24040614  addiu       $a0, $zero, 0x614
    ctx->pc = 0x43ecd4u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 0), 1556));
    // 0x43ecd8: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43ecd8u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ecdc: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43ecdcu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43ece0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ece0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ece4: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43ece4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43ece8: 0xac640000  sw          $a0, 0x0($v1)
    ctx->pc = 0x43ece8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));
    // 0x43ecec: 0xac650008  sw          $a1, 0x8($v1)
    ctx->pc = 0x43ececu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 5));
    // 0x43ecf0: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x43ecf0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x43ecf4: 0xac650004  sw          $a1, 0x4($v1)
    ctx->pc = 0x43ecf4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 5));
label_43ecf8:
    // 0x43ecf8: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x43ecf8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x43ecfc: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x43ecfcu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x43ed00: 0x24050002  addiu       $a1, $zero, 0x2
    ctx->pc = 0x43ed00u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
    // 0x43ed04: 0x8e24012c  lw          $a0, 0x12C($s1)
    ctx->pc = 0x43ed04u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 300)));
    // 0x43ed08: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x43ed08u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ed0c: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x43ed0cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x43ed10: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ed10u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ed14: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x43ed14u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x43ed18: 0xac640000  sw          $a0, 0x0($v1)
    ctx->pc = 0x43ed18u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));
    // 0x43ed1c: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x43ed1cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x43ed20: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x43ed20u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x43ed24: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x43ed24u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x43ed28: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43ed28u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43ed2c: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43ed2cu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43ed30: 0x7a220030  lq          $v0, 0x30($s1)
    ctx->pc = 0x43ed30u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 48)));
    // 0x43ed34: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43ed34u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43ed38: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43ed38u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43ed3c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ed3cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ed40: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43ed40u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43ed44: 0x8e220130  lw          $v0, 0x130($s1)
    ctx->pc = 0x43ed44u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 304)));
    // 0x43ed48: 0x54450007  bnel        $v0, $a1, . + 4 + (0x7 << 2)
    ctx->pc = 0x43ED48u;
    {
        const bool branch_taken_0x43ed48 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 5));
        if (branch_taken_0x43ed48) {
            ctx->pc = 0x43ED4Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x43ED48u;
            // 0x43ed4c: 0x8e23012c  lw          $v1, 0x12C($s1) (Delay Slot)
            SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 300)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x43ED68u;
            goto label_43ed68;
        }
    }
    ctx->pc = 0x43ED50u;
    // 0x43ed50: 0x3c013d0f  lui         $at, 0x3D0F
    ctx->pc = 0x43ed50u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)15631 << 16));
    // 0x43ed54: 0x34215c29  ori         $at, $at, 0x5C29
    ctx->pc = 0x43ed54u;
    SET_GPR_U64(ctx, 1, GPR_U64(ctx, 1) | (uint64_t)(uint16_t)23593);
    // 0x43ed58: 0x44810000  mtc1        $at, $f0
    ctx->pc = 0x43ed58u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
    // 0x43ed5c: 0x0  nop
    ctx->pc = 0x43ed5cu;
    // NOP
    // 0x43ed60: 0xe460fffc  swc1        $f0, -0x4($v1)
    ctx->pc = 0x43ed60u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 3), 4294967292), bits); }
    // 0x43ed64: 0x8e23012c  lw          $v1, 0x12C($s1)
    ctx->pc = 0x43ed64u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 300)));
label_43ed68:
    // 0x43ed68: 0x24020347  addiu       $v0, $zero, 0x347
    ctx->pc = 0x43ed68u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 839));
    // 0x43ed6c: 0x14620014  bne         $v1, $v0, . + 4 + (0x14 << 2)
    ctx->pc = 0x43ED6Cu;
    {
        const bool branch_taken_0x43ed6c = (GPR_U64(ctx, 3) != GPR_U64(ctx, 2));
        ctx->pc = 0x43ED70u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43ED6Cu;
        // 0x43ed70: 0x26240170  addiu       $a0, $s1, 0x170 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 17), 368));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ed6c) {
            ctx->pc = 0x43EDC0u;
            goto label_43edc0;
        }
    }
    ctx->pc = 0x43ED74u;
    // 0x43ed74: 0xc0707d0  jal         func_1C1F40
    ctx->pc = 0x43ED74u;
    SET_GPR_U32(ctx, 31, 0x43ED7Cu);
    ctx->pc = 0x43ED78u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43ED74u;
    // 0x43ed78: 0x220202d  daddu       $a0, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C1F40u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C1F40u, 0x43ED74u, 0x43ED7Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43ED7Cu;
label_43ed7c:
    // 0x43ed7c: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x43ed7cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x43ed80: 0x2406ffff  addiu       $a2, $zero, -0x1
    ctx->pc = 0x43ed80u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43ed84: 0x24453210  addiu       $a1, $v0, 0x3210
    ctx->pc = 0x43ed84u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 2), 12816));
    // 0x43ed88: 0x24040003  addiu       $a0, $zero, 0x3
    ctx->pc = 0x43ed88u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x43ed8c: 0x262800a8  addiu       $t0, $s1, 0xA8
    ctx->pc = 0x43ed8cu;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 17), 168));
label_43ed90:
    // 0x43ed90: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43ed90u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43ed94: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43ed94u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43ed98: 0x2484ffff  addiu       $a0, $a0, -0x1
    ctx->pc = 0x43ed98u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294967295));
    // 0x43ed9c: 0x78a20000  lq          $v0, 0x0($a1)
    ctx->pc = 0x43ed9cu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x43eda0: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43eda0u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43eda4: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43eda4u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43eda8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43eda8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43edac: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43edacu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43edb0: 0x1486fff7  bne         $a0, $a2, . + 4 + (-0x9 << 2)
    ctx->pc = 0x43EDB0u;
    {
        const bool branch_taken_0x43edb0 = (GPR_U64(ctx, 4) != GPR_U64(ctx, 6));
        ctx->pc = 0x43EDB4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EDB0u;
        // 0x43edb4: 0x24a50010  addiu       $a1, $a1, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43edb0) {
            ctx->pc = 0x43ED90u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43ed90;
        }
    }
    ctx->pc = 0x43EDB8u;
    // 0x43edb8: 0x1000000f  b           . + 4 + (0xF << 2)
    ctx->pc = 0x43EDB8u;
    {
        const bool branch_taken_0x43edb8 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x43edb8) {
            ctx->pc = 0x43EDF8u;
            goto label_43edf8;
        }
    }
    ctx->pc = 0x43EDC0u;
label_43edc0:
    // 0x43edc0: 0x24050003  addiu       $a1, $zero, 0x3
    ctx->pc = 0x43edc0u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x43edc4: 0x262800a8  addiu       $t0, $s1, 0xA8
    ctx->pc = 0x43edc4u;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 17), 168));
    // 0x43edc8: 0x2406ffff  addiu       $a2, $zero, -0x1
    ctx->pc = 0x43edc8u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x43edcc: 0x0  nop
    ctx->pc = 0x43edccu;
    // NOP
label_43edd0:
    // 0x43edd0: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43edd0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43edd4: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43edd4u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43edd8: 0x24a5ffff  addiu       $a1, $a1, -0x1
    ctx->pc = 0x43edd8u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294967295));
    // 0x43eddc: 0x78820000  lq          $v0, 0x0($a0)
    ctx->pc = 0x43eddcu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x43ede0: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43ede0u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43ede4: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43ede4u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43ede8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ede8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43edec: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43edecu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x43edf0: 0x14a6fff7  bne         $a1, $a2, . + 4 + (-0x9 << 2)
    ctx->pc = 0x43EDF0u;
    {
        const bool branch_taken_0x43edf0 = (GPR_U64(ctx, 5) != GPR_U64(ctx, 6));
        ctx->pc = 0x43EDF4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EDF0u;
        // 0x43edf4: 0x24840010  addiu       $a0, $a0, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43edf0) {
            ctx->pc = 0x43EDD0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43edd0;
        }
    }
    ctx->pc = 0x43EDF8u;
label_43edf8:
    // 0x43edf8: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x43edf8u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
    // 0x43edfc: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x43edfcu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
    // 0x43ee00: 0x7a220160  lq          $v0, 0x160($s1)
    ctx->pc = 0x43ee00u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 352)));
    // 0x43ee04: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x43ee04u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
    // 0x43ee08: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x43ee08u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
    // 0x43ee0c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43ee0cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43ee10: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x43ee10u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_43ee14:
    // 0x43ee14: 0xde420000  ld          $v0, 0x0($s2)
    ctx->pc = 0x43ee14u;
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 18), 0)));
label_43ee18:
    // 0x43ee18: 0x24030008  addiu       $v1, $zero, 0x8
    ctx->pc = 0x43ee18u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 8));
    // 0x43ee1c: 0x9e240128  lwu         $a0, 0x128($s1)
    ctx->pc = 0x43ee1cu;
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 17), 296)));
    // 0x43ee20: 0x282d  daddu       $a1, $zero, $zero
    ctx->pc = 0x43ee20u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ee24: 0x10800006  beqz        $a0, . + 4 + (0x6 << 2)
    ctx->pc = 0x43EE24u;
    {
        const bool branch_taken_0x43ee24 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EE28u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE24u;
        // 0x43ee28: 0x433025  or          $a2, $v0, $v1 (Delay Slot)
        SET_GPR_U64(ctx, 6, GPR_U64(ctx, 2) | GPR_U64(ctx, 3));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ee24) {
            ctx->pc = 0x43EE40u;
            goto label_43ee40;
        }
    }
    ctx->pc = 0x43EE2Cu;
    // 0x43ee2c: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x43ee2cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
    // 0x43ee30: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43ee30u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ee34: 0x8c434c70  lw          $v1, 0x4C70($v0)
    ctx->pc = 0x43ee34u;
    SET_GPR_S32(ctx, 3, (int32_t)FAST_READ32(0x444C70u));
    // 0x43ee38: 0x8c640050  lw          $a0, 0x50($v1)
    ctx->pc = 0x43ee38u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 80)));
    // 0x43ee3c: 0x4280a  movz        $a1, $zero, $a0
    ctx->pc = 0x43ee3cu;
    if (GPR_U64(ctx, 4) == 0) SET_GPR_VEC(ctx, 5, GPR_VEC(ctx, 0));
label_43ee40:
    // 0x43ee40: 0x8e220134  lw          $v0, 0x134($s1)
    ctx->pc = 0x43ee40u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 308)));
    // 0x43ee44: 0x10400005  beqz        $v0, . + 4 + (0x5 << 2)
    ctx->pc = 0x43EE44u;
    {
        const bool branch_taken_0x43ee44 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EE48u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE44u;
        // 0x43ee48: 0x51978  dsll        $v1, $a1, 5 (Delay Slot)
        SET_GPR_U64(ctx, 3, GPR_U64(ctx, 5) << 5);
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ee44) {
            ctx->pc = 0x43EE5Cu;
            goto label_43ee5c;
        }
    }
    ctx->pc = 0x43EE4Cu;
    // 0x43ee4c: 0x24020010  addiu       $v0, $zero, 0x10
    ctx->pc = 0x43ee4cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 16));
    // 0x43ee50: 0x621025  or          $v0, $v1, $v0
    ctx->pc = 0x43ee50u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 3) | GPR_U64(ctx, 2));
    // 0x43ee54: 0x10000002  b           . + 4 + (0x2 << 2)
    ctx->pc = 0x43EE54u;
    {
        const bool branch_taken_0x43ee54 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EE58u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE54u;
        // 0x43ee58: 0xc22825  or          $a1, $a2, $v0 (Delay Slot)
        SET_GPR_U64(ctx, 5, GPR_U64(ctx, 6) | GPR_U64(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ee54) {
            ctx->pc = 0x43EE60u;
            goto label_43ee60;
        }
    }
    ctx->pc = 0x43EE5Cu;
label_43ee5c:
    // 0x43ee5c: 0xc32825  or          $a1, $a2, $v1
    ctx->pc = 0x43ee5cu;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 6) | GPR_U64(ctx, 3));
label_43ee60:
    // 0x43ee60: 0x8e23002c  lw          $v1, 0x2C($s1)
    ctx->pc = 0x43ee60u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 44)));
    // 0x43ee64: 0x24020001  addiu       $v0, $zero, 0x1
    ctx->pc = 0x43ee64u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ee68: 0x10620003  beq         $v1, $v0, . + 4 + (0x3 << 2)
    ctx->pc = 0x43EE68u;
    {
        const bool branch_taken_0x43ee68 = (GPR_U64(ctx, 3) == GPR_U64(ctx, 2));
        ctx->pc = 0x43EE6Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE68u;
        // 0x43ee6c: 0x24020040  addiu       $v0, $zero, 0x40 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 64));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ee68) {
            ctx->pc = 0x43EE78u;
            goto label_43ee78;
        }
    }
    ctx->pc = 0x43EE70u;
    // 0x43ee70: 0x10000002  b           . + 4 + (0x2 << 2)
    ctx->pc = 0x43EE70u;
    {
        const bool branch_taken_0x43ee70 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EE74u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE70u;
        // 0x43ee74: 0xa21025  or          $v0, $a1, $v0 (Delay Slot)
        SET_GPR_U64(ctx, 2, GPR_U64(ctx, 5) | GPR_U64(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ee70) {
            ctx->pc = 0x43EE7Cu;
            goto label_43ee7c;
        }
    }
    ctx->pc = 0x43EE78u;
label_43ee78:
    // 0x43ee78: 0xa0102d  daddu       $v0, $a1, $zero
    ctx->pc = 0x43ee78u;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
label_43ee7c:
    // 0x43ee7c: 0xfe420000  sd          $v0, 0x0($s2)
    ctx->pc = 0x43ee7cu;
    WRITE64(ADD32(GPR_U32(ctx, 18), 0), GPR_U64(ctx, 2));
    // 0x43ee80: 0x7bb00030  lq          $s0, 0x30($sp)
    ctx->pc = 0x43ee80u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x43ee84: 0x8d020008  lw          $v0, 0x8($t0)
    ctx->pc = 0x43ee84u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 8), 8)));
    // 0x43ee88: 0x7bb10020  lq          $s1, 0x20($sp)
    ctx->pc = 0x43ee88u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x43ee8c: 0x7bb20010  lq          $s2, 0x10($sp)
    ctx->pc = 0x43ee8cu;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x43ee90: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x43ee90u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x43ee94: 0x3e00008  jr          $ra
    ctx->pc = 0x43EE94u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43EE98u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EE94u;
        // 0x43ee98: 0x27bd0040  addiu       $sp, $sp, 0x40 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 64));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43EE94u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43EE9Cu;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&Select__5PsMatRUl_0x43ea70), PS2Runtime::RecompiledFunction>::value,
              "Select__5PsMatRUl_0x43ea70 override no longer matches the recompiled signature");
