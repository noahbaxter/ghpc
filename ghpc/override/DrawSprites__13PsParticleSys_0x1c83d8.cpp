// Override of work/output/DrawSprites__13PsParticleSys_0x1c83d8.cpp
// (PsParticleSys::DrawSprites, 0x1c83d8 - 0x1c85d4).
//
// Why: particles are expanded into quads by VU1 microprogram 0x5CC, so with
// VU1 off nothing drew them. The body is verbatim; on fresh entry the live
// particles are built into camera-facing quads here and submitted as one
// dynamic draw under the material DrawShowing just selected.
//
// What the body sends per particle, in batches of up to 127 (0x1c84bc..):
//   qw0  particle +0x00, the colour, alpha in w (1.0 = the GS's 0x80)
//   qw1  (+0x20, +0x24, +0x28, +0x60 * 0.5): the world position and half
//        the size; a gameplay sample reads (-383, 13, 15) and 17
// The positions are the system's own. DrawShowing (0x1c8620) has
// func_1fb270 fill this+0x1f0, then uploads four quadwords of it
// (0x1c8664..0x1c86a4) to VU1 qw676: a row-vector transform whose fourth row,
// +0x220, is the translation that puts each emitter where it stands. The w
// column is not read by 0x5CC.
// walking the list from this+0x110 by +0x6c (0x1c8514). A system with a
// prebuilt packet at +0x2d0 REFs that instead and is not handled here.
//
// What 0x5CC does with it (VU 0x2e60..0x30a0): each particle is one GS
// SPRITE (DrawShowing asks PsMat::Select for PRIM 6, REGS 0x412412) whose
// corners are the clip-space position -/+ qw699 * qw1.w. PsCam::Select
// writes qw699 as the projection's x and up scales over the guard band,
// which qw696 multiplies back, so the half extent is qw1.w view units along
// the camera's right and up: the quad built here. UVs run 0..1 over the
// sprite. The colour is FTOI0(qw0 * qw695), times the ambient qw681 when
// qw688.w is set, with no lighting program; the draw says so with 0x5CC.
#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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

namespace {

float readF(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    const uint32_t bits = READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// The current camera, *0x444c60, is what 0x5CC faces. Its world transform is
// RndTransformable +0x60 (rows) and +0x90 (position); with x right, y forward
// and z up, row 0 is the camera's right and row 2 its up.
void ghpcParticleSubmit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    static const bool s_on = []() {
        const char* e = std::getenv("GHPC_SCENE_PARTICLES");
        return e == nullptr || std::atoi(e) != 0;
    }();
    if (!s_on || !ghpc::scene::active()) return;
    static const bool s_log = std::getenv("GHPC_PARTICLE_LOG") != nullptr;
    static uint64_t s_calls = 0;
    ++s_calls;
    if (READ32(self + 0x2d0u) != 0u) {
        if (s_log && s_calls <= 16u)
            std::fprintf(stderr, "[ghpc/particle] sys=0x%08x prebuilt packet, not drawn\n", self);
        return;
    }
    const uint32_t cam = READ32(0x444c60u);
    if (cam == 0u) return;
    float right[3], up[3];
    for (int k = 0; k < 3; ++k) {
        right[k] = readF(rdram, ctx, runtime, cam + 0x60u + k * 4u);
        up[k] = readF(rdram, ctx, runtime, cam + 0x80u + k * 4u);
    }

    float xf[4][3];
    for (uint32_t r = 0; r < 4u; ++r)
        for (uint32_t c = 0; c < 3u; ++c)
            xf[r][c] = readF(rdram, ctx, runtime, self + 0x1f0u + r * 0x10u + c * 4u);

    static std::vector<ghpc::scene::Vertex> s_verts;
    static std::vector<uint16_t> s_indices;
    s_verts.clear();
    s_indices.clear();
    uint32_t p = READ32(self + 0x110u);
    uint32_t count = 0u;
    while (p != 0u && count < 16000u) {
        float local[3], pos[3], rgba[4];
        for (int k = 0; k < 4; ++k) rgba[k] = readF(rdram, ctx, runtime, p + k * 4u);
        for (int k = 0; k < 3; ++k) local[k] = readF(rdram, ctx, runtime, p + 0x20u + k * 4u);
        for (int k = 0; k < 3; ++k)
            pos[k] = local[0] * xf[0][k] + local[1] * xf[1][k] + local[2] * xf[2][k] + xf[3][k];
        const float half = readF(rdram, ctx, runtime, p + 0x60u) * 0.5f;
        if (s_log && s_calls <= 4u && count < 2u)
            std::fprintf(stderr, "[ghpc/particle]   p=0x%08x rgba=(%.3f %.3f %.3f %.3f) pos=(%.2f %.2f %.2f) half=%.3f\n",
                         p, rgba[0], rgba[1], rgba[2], rgba[3], pos[0], pos[1], pos[2], half);
        const uint16_t base = (uint16_t)s_verts.size();
        static const float kCorner[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
        for (int c = 0; c < 4; ++c) {
            ghpc::scene::Vertex v{};
            for (int k = 0; k < 3; ++k)
                v.pos[k] = pos[k] + (kCorner[c][0] * right[k] + kCorner[c][1] * up[k]) * half;
            v.norm[1] = -1.0f;
            std::memcpy(v.weights, rgba, sizeof(rgba));
            v.uv[0] = kCorner[c][0] * 0.5f + 0.5f;
            v.uv[1] = 0.5f - kCorner[c][1] * 0.5f;
            s_verts.push_back(v);
        }
        const uint16_t quad[6] = {0, 1, 2, 1, 3, 2};
        for (uint16_t q : quad) s_indices.push_back((uint16_t)(base + q));
        ++count;
        p = READ32(p + 0x6cu);
    }
    if (s_log && count != 0u) {
        // Once per system, and again if it moves.
        static std::vector<std::pair<uint32_t, float>> s_seen;
        auto it = std::find_if(s_seen.begin(), s_seen.end(), [self](const auto& e) { return e.first == self; });
        const float key = xf[3][0] + xf[3][1] * 3.0f + xf[3][2] * 7.0f;
        if (it == s_seen.end() || it->second != key) {
            if (it == s_seen.end()) s_seen.push_back({self, key}); else it->second = key;
            std::fprintf(stderr, "[ghpc/particle] sys=0x%08x particles=%u at (%.1f %.1f %.1f) first=(%.1f %.1f %.1f)\n",
                         self, count, xf[3][0], xf[3][1], xf[3][2], s_verts[0].pos[0], s_verts[0].pos[1],
                         s_verts[0].pos[2]);
        }
    }
    if (count == 0u) return;
    // Built in world space, so the draw's own matrix is the identity.
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ghpc::scene::setWorldMatrix(kIdentity);
    ghpc::scene::submitDynamicMesh(self, s_verts.data(), (uint32_t)s_verts.size(), s_indices.data(),
                                   (uint32_t)s_indices.size(), 0x5CCu);
}

} // namespace

// Function: DrawSprites__13PsParticleSys
// Address: 0x1c83d8 - 0x1c85d4
void DrawSprites__13PsParticleSys_0x1c83d8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("DrawSprites__13PsParticleSys_0x1c83d8");
#endif

    switch (ctx->pc) {
        case 0x1c8410u: goto label_1c8410;
        case 0x1c8428u: goto label_1c8428;
        case 0x1c8430u: goto label_1c8430;
        case 0x1c8444u: goto label_1c8444;
        case 0x1c844cu: goto label_1c844c;
        case 0x1c8454u: goto label_1c8454;
        case 0x1c8478u: goto label_1c8478;
        case 0x1c84c0u: goto label_1c84c0;
        case 0x1c85a8u: goto label_1c85a8;
        default: break;
    }

    // Fresh entry only; a resume above jumps past this. $a0 is `this`.
    ghpcParticleSubmit(rdram, ctx, runtime, GPR_U32(ctx, 4));

    ctx->pc = 0x1c83d8u;

    // 0x1c83d8: 0x27bdff90  addiu       $sp, $sp, -0x70
    ctx->pc = 0x1c83d8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967184));
    // 0x1c83dc: 0x7fb10050  sq          $s1, 0x50($sp)
    ctx->pc = 0x1c83dcu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 80), GPR_VEC(ctx, 17));
    // 0x1c83e0: 0x7fb00060  sq          $s0, 0x60($sp)
    ctx->pc = 0x1c83e0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 96), GPR_VEC(ctx, 16));
    // 0x1c83e4: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x1c83e4u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c83e8: 0x7fb20040  sq          $s2, 0x40($sp)
    ctx->pc = 0x1c83e8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 64), GPR_VEC(ctx, 18));
    // 0x1c83ec: 0x7fb30030  sq          $s3, 0x30($sp)
    ctx->pc = 0x1c83ecu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 48), GPR_VEC(ctx, 19));
    // 0x1c83f0: 0x7fb40020  sq          $s4, 0x20($sp)
    ctx->pc = 0x1c83f0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 32), GPR_VEC(ctx, 20));
    // 0x1c83f4: 0x7fb50010  sq          $s5, 0x10($sp)
    ctx->pc = 0x1c83f4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 16), GPR_VEC(ctx, 21));
    // 0x1c83f8: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x1c83f8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x1c83fc: 0x8e2402d0  lw          $a0, 0x2D0($s1)
    ctx->pc = 0x1c83fcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 720)));
    // 0x1c8400: 0x50800016  beql        $a0, $zero, . + 4 + (0x16 << 2)
    ctx->pc = 0x1C8400u;
    {
        const bool branch_taken_0x1c8400 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8400) {
            ctx->pc = 0x1C8404u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C8400u;
            // 0x1c8404: 0x8e300110  lw          $s0, 0x110($s1) (Delay Slot)
            SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 272)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C845Cu;
            goto label_1c845c;
        }
    }
    ctx->pc = 0x1C8408u;
    // 0x1c8408: 0xc0c8454  jal         func_321150
    ctx->pc = 0x1C8408u;
    SET_GPR_U32(ctx, 31, 0x1C8410u);
    ctx->pc = 0x321150u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321150u, 0x1C8408u, 0x1C8410u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8410u;
label_1c8410:
    // 0x1c8410: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x1c8410u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8414: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c8414u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c8418: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x1c8418u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c841c: 0x382d  daddu       $a3, $zero, $zero
    ctx->pc = 0x1c841cu;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8420: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x1C8420u;
    SET_GPR_U32(ctx, 31, 0x1C8428u);
    ctx->pc = 0x1C8424u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8420u;
    // 0x1c8424: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x1C8420u, 0x1C8428u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8428u;
label_1c8428:
    // 0x1c8428: 0xc10f9d2  jal         func_43E748
    ctx->pc = 0x1C8428u;
    SET_GPR_U32(ctx, 31, 0x1C8430u);
    ctx->pc = 0x1C842Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8428u;
    // 0x1c842c: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x1C8428u, 0x1C8430u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8430u;
label_1c8430:
    // 0x1c8430: 0x8e2702d4  lw          $a3, 0x2D4($s1)
    ctx->pc = 0x1c8430u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 724)));
    // 0x1c8434: 0x200302d  daddu       $a2, $s0, $zero
    ctx->pc = 0x1c8434u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8438: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c8438u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x1c843c: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x1C843Cu;
    SET_GPR_U32(ctx, 31, 0x1C8444u);
    ctx->pc = 0x1C8440u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C843Cu;
    // 0x1c8440: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x1C843Cu, 0x1C8444u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8444u;
label_1c8444:
    // 0x1c8444: 0xc10f9d2  jal         func_43E748
    ctx->pc = 0x1C8444u;
    SET_GPR_U32(ctx, 31, 0x1C844Cu);
    ctx->pc = 0x1C8448u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8444u;
    // 0x1c8448: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x1C8444u, 0x1C844Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C844Cu;
label_1c844c:
    // 0x1c844c: 0xc0c845c  jal         func_321170
    ctx->pc = 0x1C844Cu;
    SET_GPR_U32(ctx, 31, 0x1C8454u);
    ctx->pc = 0x1C8450u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C844Cu;
    // 0x1c8450: 0x8e2402d0  lw          $a0, 0x2D0($s1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 720)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321170u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321170u, 0x1C844Cu, 0x1C8454u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8454u;
label_1c8454:
    // 0x1c8454: 0x10000057  b           . + 4 + (0x57 << 2)
    ctx->pc = 0x1C8454u;
    {
        const bool branch_taken_0x1c8454 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8458u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8454u;
        // 0x1c8458: 0x7bb00060  lq          $s0, 0x60($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 96)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8454) {
            ctx->pc = 0x1C85B4u;
            goto label_1c85b4;
        }
    }
    ctx->pc = 0x1C845Cu;
label_1c845c:
    // 0x1c845c: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x1c845cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x1c8460: 0x3c111400  lui         $s1, 0x1400
    ctx->pc = 0x1c8460u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)5120 << 16));
    // 0x1c8464: 0x24532cb8  addiu       $s3, $v0, 0x2CB8
    ctx->pc = 0x1c8464u;
    SET_GPR_S32(ctx, 19, (int32_t)ADD32(GPR_U32(ctx, 2), 11448));
    // 0x1c8468: 0x24120001  addiu       $s2, $zero, 0x1
    ctx->pc = 0x1c8468u;
    SET_GPR_S32(ctx, 18, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1c846c: 0x3c15004f  lui         $s5, 0x4F
    ctx->pc = 0x1c846cu;
    SET_GPR_S32(ctx, 21, (int32_t)((uint32_t)79 << 16));
    // 0x1c8470: 0x363105cc  ori         $s1, $s1, 0x5CC
    ctx->pc = 0x1c8470u;
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 17) | (uint64_t)(uint16_t)1484);
    // 0x1c8474: 0x3c141700  lui         $s4, 0x1700
    ctx->pc = 0x1c8474u;
    SET_GPR_S32(ctx, 20, (int32_t)((uint32_t)5888 << 16));
label_1c8478:
    // 0x1c8478: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8478u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c847c: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c847cu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c8480: 0x70001ca9  por         $v1, $zero, $zero
    ctx->pc = 0x1c8480u;
    SET_GPR_VEC(ctx, 3, PS2_POR(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
    // 0x1c8484: 0x282d  daddu       $a1, $zero, $zero
    ctx->pc = 0x1c8484u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8488: 0x7c430000  sq          $v1, 0x0($v0)
    ctx->pc = 0x1c8488u;
    WRITE128(ADD32(GPR_U32(ctx, 2), 0), GPR_VEC(ctx, 3));
    // 0x1c848c: 0x24440020  addiu       $a0, $v0, 0x20
    ctx->pc = 0x1c848cu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 32));
    // 0x1c8490: 0x24430010  addiu       $v1, $v0, 0x10
    ctx->pc = 0x1c8490u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c8494: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8494u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c8498: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c8498u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x1c849c: 0x70001ca9  por         $v1, $zero, $zero
    ctx->pc = 0x1c849cu;
    SET_GPR_VEC(ctx, 3, PS2_POR(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
    // 0x1c84a0: 0x7c430010  sq          $v1, 0x10($v0)
    ctx->pc = 0x1c84a0u;
    WRITE128(ADD32(GPR_U32(ctx, 2), 16), GPR_VEC(ctx, 3));
    // 0x1c84a4: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c84a4u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c84a8: 0xac240008  sw          $a0, 0x8($at)
    ctx->pc = 0x1c84a8u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 4)); // MMIO: 0x70000008
    // 0x1c84ac: 0x1200001c  beqz        $s0, . + 4 + (0x1C << 2)
    ctx->pc = 0x1C84ACu;
    {
        const bool branch_taken_0x1c84ac = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C84B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C84ACu;
        // 0x1c84b0: 0x2446000c  addiu       $a2, $v0, 0xC (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 2), 12));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c84ac) {
            ctx->pc = 0x1C8520u;
            goto label_1c8520;
        }
    }
    ctx->pc = 0x1C84B4u;
    // 0x1c84b4: 0x3c013f00  lui         $at, 0x3F00
    ctx->pc = 0x1c84b4u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)16128 << 16));
    // 0x1c84b8: 0x44812000  mtc1        $at, $f4
    ctx->pc = 0x1c84b8u;
    { uint32_t bits = GPR_U32(ctx, 1); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
    // 0x1c84bc: 0x7a030000  lq          $v1, 0x0($s0)
    ctx->pc = 0x1c84bcu;
    SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 16), 0)));
label_1c84c0:
    // 0x1c84c0: 0x24a50001  addiu       $a1, $a1, 0x1
    ctx->pc = 0x1c84c0u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 1));
    // 0x1c84c4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c84c4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c84c8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c84c8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c84cc: 0x28a4007f  slti        $a0, $a1, 0x7F
    ctx->pc = 0x1c84ccu;
    SET_GPR_U64(ctx, 4, ((int64_t)GPR_S64(ctx, 5) < (int64_t)(int32_t)127) ? 1 : 0);
    // 0x1c84d0: 0x7c430000  sq          $v1, 0x0($v0)
    ctx->pc = 0x1c84d0u;
    WRITE128(ADD32(GPR_U32(ctx, 2), 0), GPR_VEC(ctx, 3));
    // 0x1c84d4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c84d4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c84d8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c84d8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c84dc: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c84dcu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c84e0: 0x24430010  addiu       $v1, $v0, 0x10
    ctx->pc = 0x1c84e0u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c84e4: 0xc6000060  lwc1        $f0, 0x60($s0)
    ctx->pc = 0x1c84e4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 96)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[0] = f; }
    // 0x1c84e8: 0xc6030020  lwc1        $f3, 0x20($s0)
    ctx->pc = 0x1c84e8u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 32)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[3] = f; }
    // 0x1c84ec: 0x46040002  mul.s       $f0, $f0, $f4
    ctx->pc = 0x1c84ecu;
    ctx->f[0] = FPU_MUL_S(ctx->f[0], ctx->f[4]);
    // 0x1c84f0: 0xc6010024  lwc1        $f1, 0x24($s0)
    ctx->pc = 0x1c84f0u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 36)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[1] = f; }
    // 0x1c84f4: 0xc6020028  lwc1        $f2, 0x28($s0)
    ctx->pc = 0x1c84f4u;
    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 40)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[2] = f; }
    // 0x1c84f8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c84f8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c84fc: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c84fcu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
    // 0x1c8500: 0xe440000c  swc1        $f0, 0xC($v0)
    ctx->pc = 0x1c8500u;
    { float f = ctx->f[0]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 2), 12), bits); }
    // 0x1c8504: 0xe4430000  swc1        $f3, 0x0($v0)
    ctx->pc = 0x1c8504u;
    { float f = ctx->f[3]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 2), 0), bits); }
    // 0x1c8508: 0xe4410004  swc1        $f1, 0x4($v0)
    ctx->pc = 0x1c8508u;
    { float f = ctx->f[1]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 2), 4), bits); }
    // 0x1c850c: 0xe4420008  swc1        $f2, 0x8($v0)
    ctx->pc = 0x1c850cu;
    { float f = ctx->f[2]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 2), 8), bits); }
    // 0x1c8510: 0x10800003  beqz        $a0, . + 4 + (0x3 << 2)
    ctx->pc = 0x1C8510u;
    {
        const bool branch_taken_0x1c8510 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8514u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8510u;
        // 0x1c8514: 0x8e10006c  lw          $s0, 0x6C($s0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 108)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8510) {
            ctx->pc = 0x1C8520u;
            goto label_1c8520;
        }
    }
    ctx->pc = 0x1C8518u;
    // 0x1c8518: 0x5600ffe9  bnel        $s0, $zero, . + 4 + (-0x17 << 2)
    ctx->pc = 0x1C8518u;
    {
        const bool branch_taken_0x1c8518 = (GPR_U64(ctx, 16) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c8518) {
            ctx->pc = 0x1C851Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C8518u;
            // 0x1c851c: 0x7a030000  lq          $v1, 0x0($s0) (Delay Slot)
            SET_GPR_VEC(ctx, 3, READ128(ADD32(GPR_U32(ctx, 16), 0)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C84C0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c84c0;
        }
    }
    ctx->pc = 0x1C8520u;
label_1c8520:
    // 0x1c8520: 0x51040  sll         $v0, $a1, 1
    ctx->pc = 0x1c8520u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 5), 1));
    // 0x1c8524: 0x3c036c00  lui         $v1, 0x6C00
    ctx->pc = 0x1c8524u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)27648 << 16));
    // 0x1c8528: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x1c8528u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
    // 0x1c852c: 0x34638000  ori         $v1, $v1, 0x8000
    ctx->pc = 0x1c852cu;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)32768);
    // 0x1c8530: 0x21400  sll         $v0, $v0, 16
    ctx->pc = 0x1c8530u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 16));
    // 0x1c8534: 0xacc50004  sw          $a1, 0x4($a2)
    ctx->pc = 0x1c8534u;
    WRITE32(ADD32(GPR_U32(ctx, 6), 4), GPR_U32(ctx, 5));
    // 0x1c8538: 0x431025  or          $v0, $v0, $v1
    ctx->pc = 0x1c8538u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | GPR_U64(ctx, 3));
    // 0x1c853c: 0xacc20000  sw          $v0, 0x0($a2)
    ctx->pc = 0x1c853cu;
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x1c8540: 0x8e630008  lw          $v1, 0x8($s3)
    ctx->pc = 0x1c8540u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 19), 8)));
    // 0x1c8544: 0x651821  addu        $v1, $v1, $a1
    ctx->pc = 0x1c8544u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 5)));
    // 0x1c8548: 0x1240000b  beqz        $s2, . + 4 + (0xB << 2)
    ctx->pc = 0x1C8548u;
    {
        const bool branch_taken_0x1c8548 = (GPR_U64(ctx, 18) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C854Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8548u;
        // 0x1c854c: 0xae630008  sw          $v1, 0x8($s3) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 19), 8), GPR_U32(ctx, 3));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8548) {
            ctx->pc = 0x1C8578u;
            goto label_1c8578;
        }
    }
    ctx->pc = 0x1C8550u;
    // 0x1c8550: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8550u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c8554: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c8554u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c8558: 0x902d  daddu       $s2, $zero, $zero
    ctx->pc = 0x1c8558u;
    SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c855c: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c855cu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8560: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c8560u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c8564: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8564u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c8568: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c8568u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c856c: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c856cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c8570: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x1C8570u;
    {
        const bool branch_taken_0x1c8570 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8574u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8570u;
        // 0x1c8574: 0xac710000  sw          $s1, 0x0($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 17));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8570) {
            ctx->pc = 0x1C8598u;
            goto label_1c8598;
        }
    }
    ctx->pc = 0x1C8578u;
label_1c8578:
    // 0x1c8578: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8578u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
    // 0x1c857c: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c857cu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
    // 0x1c8580: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c8580u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1c8584: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c8584u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
    // 0x1c8588: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8588u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x1c858c: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c858cu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
    // 0x1c8590: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c8590u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
    // 0x1c8594: 0xac740000  sw          $s4, 0x0($v1)
    ctx->pc = 0x1c8594u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 20));
label_1c8598:
    // 0x1c8598: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c8598u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
    // 0x1c859c: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c859cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
    // 0x1c85a0: 0xc10f900  jal         func_43E400
    ctx->pc = 0x1C85A0u;
    SET_GPR_U32(ctx, 31, 0x1C85A8u);
    ctx->pc = 0x1C85A4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C85A0u;
    // 0x1c85a4: 0x26a42780  addiu       $a0, $s5, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 21), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E400u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E400u, 0x1C85A0u, 0x1C85A8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C85A8u;
label_1c85a8:
    // 0x1c85a8: 0x1600ffb3  bnez        $s0, . + 4 + (-0x4D << 2)
    ctx->pc = 0x1C85A8u;
    {
        const bool branch_taken_0x1c85a8 = (GPR_U64(ctx, 16) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c85a8) {
            ctx->pc = 0x1C8478u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c8478;
        }
    }
    ctx->pc = 0x1C85B0u;
    // 0x1c85b0: 0x7bb00060  lq          $s0, 0x60($sp)
    ctx->pc = 0x1c85b0u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 96)));
label_1c85b4:
    // 0x1c85b4: 0x7bb10050  lq          $s1, 0x50($sp)
    ctx->pc = 0x1c85b4u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x1c85b8: 0x7bb20040  lq          $s2, 0x40($sp)
    ctx->pc = 0x1c85b8u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x1c85bc: 0x7bb30030  lq          $s3, 0x30($sp)
    ctx->pc = 0x1c85bcu;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x1c85c0: 0x7bb40020  lq          $s4, 0x20($sp)
    ctx->pc = 0x1c85c0u;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x1c85c4: 0x7bb50010  lq          $s5, 0x10($sp)
    ctx->pc = 0x1c85c4u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x1c85c8: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x1c85c8u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x1c85cc: 0x3e00008  jr          $ra
    ctx->pc = 0x1C85CCu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C85D0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C85CCu;
        // 0x1c85d0: 0x27bd0070  addiu       $sp, $sp, 0x70 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 112));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C85CCu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C85D4u;
}
