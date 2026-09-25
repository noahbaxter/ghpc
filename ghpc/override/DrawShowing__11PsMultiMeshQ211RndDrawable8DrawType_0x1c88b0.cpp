// Override of work/output/DrawShowing__11PsMultiMeshQ211RndDrawable8DrawType_0x1c88b0.cpp
// (PsMultiMesh::DrawShowing, 0x1c88b0 - 0x1c8f74).
//
// Why: a multimesh never reaches PsMesh::DrawFaces, so the Rnd seam never saw
// one and under GHPC_VU1_OFF nothing drew it. The gems' white tops and base
// rings are one: material 0x00898c70 on the gem atlas, selected from here.
//
// What the body does with `this` (saved at sp+0x50):
//   +0x48  mMesh, the PsMesh drawn once per instance
//   +0x50  sentinel of a circular list of instance nodes, next at +0x00; a
//          node's Transform is at +0x10, rows at +0x10/+0x20/+0x30 and the
//          position at +0x40, uploaded as-is per instance (0x1c8e68..)
//   mMesh+0xE4 == 8 selects the billboard upload (0x1c8ca0 sets $s7): rows
//          from WorldXfm(*0x444c60) and only the position from the node
// For each material pass the body calls PsMat::Select (0x1c89b0) then streams
// every instance in batches, so the seam submits all instances right after
// each Select returns. The generated body is otherwise verbatim.
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ghpc_scene.h"
#include "ghpc_config.h"

#include <array>
#include <vector>

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// DrawFaces__6PsMesh_0x43eea0.cpp.
void ghpcSceneSubmitInstance(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                             uint32_t mesh, const float world[16]);

namespace {

uint32_t s_pendingBillboard = 0u;

// Walks the instance list and submits each Transform. `rows`, when set, is a
// Transform whose rows 0-2 replace every instance's own: the billboard case.
uint32_t submitInstances(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self,
                         uint32_t key, uint32_t rows) {
    const uint32_t sentinel = self + 0x50u;
    uint32_t node = READ32(sentinel);
    uint32_t instances = 0u;
    // [game] crowd = double: a crowd card list gets a second card per
    // instance, a third of the way to its nearest neighbour, so the extras
    // land inside the crowd's own footprint and a mutual pair stays apart.
    const bool doubled = rows != 0u && ghpc::live().crowd.load(std::memory_order_relaxed) == ghpc::kCrowdDouble;
    static std::vector<std::array<float, 16>> s_cards;
    s_cards.clear();
    while (node != sentinel && node != 0u && instances < 4096u) {
        float m[16];
        for (uint32_t r = 0; r < 4u; ++r) {
            const uint32_t src = (rows != 0u && r < 3u) ? rows + r * 0x10u : node + 0x10u + r * 0x10u;
            for (uint32_t c = 0; c < 3u; ++c) {
                const uint32_t bits = READ32(src + c * 4u);
                std::memcpy(&m[r * 4u + c], &bits, sizeof(float));
            }
            m[r * 4u + 3u] = (r == 3u) ? 1.0f : 0.0f;
        }
        ghpcSceneSubmitInstance(rdram, ctx, runtime, key, m);
        if (doubled) {
            s_cards.emplace_back();
            std::memcpy(s_cards.back().data(), m, sizeof(m));
        }
        ++instances;
        node = READ32(node);
    }
    for (size_t i = 0; s_cards.size() > 1u && i < s_cards.size(); ++i) {
        const float* a = s_cards[i].data();
        size_t best = i;
        float bestD = 0.0f;
        for (size_t j = 0; j < s_cards.size(); ++j) {
            if (j == i) continue;
            const float* b = s_cards[j].data();
            const float dx = b[12] - a[12], dy = b[13] - a[13], dz = b[14] - a[14];
            const float d = dx * dx + dy * dy + dz * dz;
            if (best == i || d < bestD) { best = j; bestD = d; }
        }
        float m[16];
        std::memcpy(m, a, sizeof(m));
        for (int c = 12; c < 15; ++c) m[c] += (s_cards[best][c] - a[c]) / 3.0f;
        ghpcSceneSubmitInstance(rdram, ctx, runtime, key, m);
    }
    return instances;
}

void ghpcMultiMeshSubmit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    // GHPC_SCENE_MULTIMESH=0 drops these draws again, the control arm.
    static const bool s_on = []() {
        const char* e = std::getenv("GHPC_SCENE_MULTIMESH");
        return e == nullptr || std::atoi(e) != 0;
    }();
    if (!s_on || !ghpc::scene::active()) return;
    const uint32_t mesh = READ32(self + 0x48u);
    if (mesh == 0u) return;
    // DrawFaces runs on the owner (+0x138), and the mesh cache is keyed on it.
    const uint32_t owner = READ32(mesh + 0x138u);
    const uint32_t key = owner ? owner : mesh;
    const bool billboard = READ32(mesh + 0xe4u) == 8u;

    static const bool s_log = std::getenv("GHPC_MULTIMESH_LOG") != nullptr;
    static uint64_t s_calls = 0;
    ++s_calls;
    if (billboard) {
        // Its rows come from WorldXfm, which the body calls later in this
        // pass; ghpcBillboardSubmit picks it up there.
        s_pendingBillboard = self;
        return;
    }
    const uint32_t instances = submitInstances(rdram, ctx, runtime, self, key, 0u);
    if (s_log && s_calls <= 8u)
        std::fprintf(stderr, "[ghpc/multimesh] mesh=0x%08x key=0x%08x instances=%u\n", mesh, key, instances);
}

// The billboard pass, at 0x1c8ca8, where WorldXfm(*0x444c60) has just come
// back into $s1. Each instance uploads that transform's rows 0-2 and only its
// own position (0x1c8dfc..0x1c8e3c), so every card faces the camera.
void ghpcBillboardSubmit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t xfm) {
    const uint32_t self = s_pendingBillboard;
    s_pendingBillboard = 0u;
    if (self == 0u || xfm == 0u || !ghpc::scene::active()) return;
    const uint32_t mesh = READ32(self + 0x48u);
    if (mesh == 0u) return;
    const uint32_t owner = READ32(mesh + 0x138u);
    const uint32_t instances = submitInstances(rdram, ctx, runtime, self, owner ? owner : mesh, xfm);

    static const bool s_log = std::getenv("GHPC_MULTIMESH_LOG") != nullptr;
    static uint64_t s_billboards = 0;
    if (s_log && (++s_billboards <= 4u || (s_billboards % 5000u) == 0u))
        std::fprintf(stderr, "[ghpc/multimesh] billboard mesh=0x%08x instances=%u (%llu)\n", mesh, instances,
                     (unsigned long long)s_billboards);
}

} // namespace

// Function: DrawShowing__11PsMultiMeshQ211RndDrawable8DrawType
// Address: 0x1c88b0 - 0x1c8f74
void DrawShowing__11PsMultiMeshQ211RndDrawable8DrawType_0x1c88b0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("DrawShowing__11PsMultiMeshQ211RndDrawable8DrawType_0x1c88b0");
#endif

    switch (ctx->pc) {
        case 0x1c88b0u: goto label_1c88b0;
        case 0x1c88b4u: goto label_1c88b4;
        case 0x1c88b8u: goto label_1c88b8;
        case 0x1c88bcu: goto label_1c88bc;
        case 0x1c88c0u: goto label_1c88c0;
        case 0x1c88c4u: goto label_1c88c4;
        case 0x1c88c8u: goto label_1c88c8;
        case 0x1c88ccu: goto label_1c88cc;
        case 0x1c88d0u: goto label_1c88d0;
        case 0x1c88d4u: goto label_1c88d4;
        case 0x1c88d8u: goto label_1c88d8;
        case 0x1c88dcu: goto label_1c88dc;
        case 0x1c88e0u: goto label_1c88e0;
        case 0x1c88e4u: goto label_1c88e4;
        case 0x1c88e8u: goto label_1c88e8;
        case 0x1c88ecu: goto label_1c88ec;
        case 0x1c88f0u: goto label_1c88f0;
        case 0x1c88f4u: goto label_1c88f4;
        case 0x1c88f8u: goto label_1c88f8;
        case 0x1c88fcu: goto label_1c88fc;
        case 0x1c8900u: goto label_1c8900;
        case 0x1c8904u: goto label_1c8904;
        case 0x1c8908u: goto label_1c8908;
        case 0x1c890cu: goto label_1c890c;
        case 0x1c8910u: goto label_1c8910;
        case 0x1c8914u: goto label_1c8914;
        case 0x1c8918u: goto label_1c8918;
        case 0x1c891cu: goto label_1c891c;
        case 0x1c8920u: goto label_1c8920;
        case 0x1c8924u: goto label_1c8924;
        case 0x1c8928u: goto label_1c8928;
        case 0x1c892cu: goto label_1c892c;
        case 0x1c8930u: goto label_1c8930;
        case 0x1c8934u: goto label_1c8934;
        case 0x1c8938u: goto label_1c8938;
        case 0x1c893cu: goto label_1c893c;
        case 0x1c8940u: goto label_1c8940;
        case 0x1c8944u: goto label_1c8944;
        case 0x1c8948u: goto label_1c8948;
        case 0x1c894cu: goto label_1c894c;
        case 0x1c8950u: goto label_1c8950;
        case 0x1c8954u: goto label_1c8954;
        case 0x1c8958u: goto label_1c8958;
        case 0x1c895cu: goto label_1c895c;
        case 0x1c8960u: goto label_1c8960;
        case 0x1c8964u: goto label_1c8964;
        case 0x1c8968u: goto label_1c8968;
        case 0x1c896cu: goto label_1c896c;
        case 0x1c8970u: goto label_1c8970;
        case 0x1c8974u: goto label_1c8974;
        case 0x1c8978u: goto label_1c8978;
        case 0x1c897cu: goto label_1c897c;
        case 0x1c8980u: goto label_1c8980;
        case 0x1c8984u: goto label_1c8984;
        case 0x1c8988u: goto label_1c8988;
        case 0x1c898cu: goto label_1c898c;
        case 0x1c8990u: goto label_1c8990;
        case 0x1c8994u: goto label_1c8994;
        case 0x1c8998u: goto label_1c8998;
        case 0x1c899cu: goto label_1c899c;
        case 0x1c89a0u: goto label_1c89a0;
        case 0x1c89a4u: goto label_1c89a4;
        case 0x1c89a8u: goto label_1c89a8;
        case 0x1c89acu: goto label_1c89ac;
        case 0x1c89b0u: goto label_1c89b0;
        case 0x1c89b4u: goto label_1c89b4;
        case 0x1c89b8u: goto label_1c89b8;
        case 0x1c89bcu: goto label_1c89bc;
        case 0x1c89c0u: goto label_1c89c0;
        case 0x1c89c4u: goto label_1c89c4;
        case 0x1c89c8u: goto label_1c89c8;
        case 0x1c89ccu: goto label_1c89cc;
        case 0x1c89d0u: goto label_1c89d0;
        case 0x1c89d4u: goto label_1c89d4;
        case 0x1c89d8u: goto label_1c89d8;
        case 0x1c89dcu: goto label_1c89dc;
        case 0x1c89e0u: goto label_1c89e0;
        case 0x1c89e4u: goto label_1c89e4;
        case 0x1c89e8u: goto label_1c89e8;
        case 0x1c89ecu: goto label_1c89ec;
        case 0x1c89f0u: goto label_1c89f0;
        case 0x1c89f4u: goto label_1c89f4;
        case 0x1c89f8u: goto label_1c89f8;
        case 0x1c89fcu: goto label_1c89fc;
        case 0x1c8a00u: goto label_1c8a00;
        case 0x1c8a04u: goto label_1c8a04;
        case 0x1c8a08u: goto label_1c8a08;
        case 0x1c8a0cu: goto label_1c8a0c;
        case 0x1c8a10u: goto label_1c8a10;
        case 0x1c8a14u: goto label_1c8a14;
        case 0x1c8a18u: goto label_1c8a18;
        case 0x1c8a1cu: goto label_1c8a1c;
        case 0x1c8a20u: goto label_1c8a20;
        case 0x1c8a24u: goto label_1c8a24;
        case 0x1c8a28u: goto label_1c8a28;
        case 0x1c8a2cu: goto label_1c8a2c;
        case 0x1c8a30u: goto label_1c8a30;
        case 0x1c8a34u: goto label_1c8a34;
        case 0x1c8a38u: goto label_1c8a38;
        case 0x1c8a3cu: goto label_1c8a3c;
        case 0x1c8a40u: goto label_1c8a40;
        case 0x1c8a44u: goto label_1c8a44;
        case 0x1c8a48u: goto label_1c8a48;
        case 0x1c8a4cu: goto label_1c8a4c;
        case 0x1c8a50u: goto label_1c8a50;
        case 0x1c8a54u: goto label_1c8a54;
        case 0x1c8a58u: goto label_1c8a58;
        case 0x1c8a5cu: goto label_1c8a5c;
        case 0x1c8a60u: goto label_1c8a60;
        case 0x1c8a64u: goto label_1c8a64;
        case 0x1c8a68u: goto label_1c8a68;
        case 0x1c8a6cu: goto label_1c8a6c;
        case 0x1c8a70u: goto label_1c8a70;
        case 0x1c8a74u: goto label_1c8a74;
        case 0x1c8a78u: goto label_1c8a78;
        case 0x1c8a7cu: goto label_1c8a7c;
        case 0x1c8a80u: goto label_1c8a80;
        case 0x1c8a84u: goto label_1c8a84;
        case 0x1c8a88u: goto label_1c8a88;
        case 0x1c8a8cu: goto label_1c8a8c;
        case 0x1c8a90u: goto label_1c8a90;
        case 0x1c8a94u: goto label_1c8a94;
        case 0x1c8a98u: goto label_1c8a98;
        case 0x1c8a9cu: goto label_1c8a9c;
        case 0x1c8aa0u: goto label_1c8aa0;
        case 0x1c8aa4u: goto label_1c8aa4;
        case 0x1c8aa8u: goto label_1c8aa8;
        case 0x1c8aacu: goto label_1c8aac;
        case 0x1c8ab0u: goto label_1c8ab0;
        case 0x1c8ab4u: goto label_1c8ab4;
        case 0x1c8ab8u: goto label_1c8ab8;
        case 0x1c8abcu: goto label_1c8abc;
        case 0x1c8ac0u: goto label_1c8ac0;
        case 0x1c8ac4u: goto label_1c8ac4;
        case 0x1c8ac8u: goto label_1c8ac8;
        case 0x1c8accu: goto label_1c8acc;
        case 0x1c8ad0u: goto label_1c8ad0;
        case 0x1c8ad4u: goto label_1c8ad4;
        case 0x1c8ad8u: goto label_1c8ad8;
        case 0x1c8adcu: goto label_1c8adc;
        case 0x1c8ae0u: goto label_1c8ae0;
        case 0x1c8ae4u: goto label_1c8ae4;
        case 0x1c8ae8u: goto label_1c8ae8;
        case 0x1c8aecu: goto label_1c8aec;
        case 0x1c8af0u: goto label_1c8af0;
        case 0x1c8af4u: goto label_1c8af4;
        case 0x1c8af8u: goto label_1c8af8;
        case 0x1c8afcu: goto label_1c8afc;
        case 0x1c8b00u: goto label_1c8b00;
        case 0x1c8b04u: goto label_1c8b04;
        case 0x1c8b08u: goto label_1c8b08;
        case 0x1c8b0cu: goto label_1c8b0c;
        case 0x1c8b10u: goto label_1c8b10;
        case 0x1c8b14u: goto label_1c8b14;
        case 0x1c8b18u: goto label_1c8b18;
        case 0x1c8b1cu: goto label_1c8b1c;
        case 0x1c8b20u: goto label_1c8b20;
        case 0x1c8b24u: goto label_1c8b24;
        case 0x1c8b28u: goto label_1c8b28;
        case 0x1c8b2cu: goto label_1c8b2c;
        case 0x1c8b30u: goto label_1c8b30;
        case 0x1c8b34u: goto label_1c8b34;
        case 0x1c8b38u: goto label_1c8b38;
        case 0x1c8b3cu: goto label_1c8b3c;
        case 0x1c8b40u: goto label_1c8b40;
        case 0x1c8b44u: goto label_1c8b44;
        case 0x1c8b48u: goto label_1c8b48;
        case 0x1c8b4cu: goto label_1c8b4c;
        case 0x1c8b50u: goto label_1c8b50;
        case 0x1c8b54u: goto label_1c8b54;
        case 0x1c8b58u: goto label_1c8b58;
        case 0x1c8b5cu: goto label_1c8b5c;
        case 0x1c8b60u: goto label_1c8b60;
        case 0x1c8b64u: goto label_1c8b64;
        case 0x1c8b68u: goto label_1c8b68;
        case 0x1c8b6cu: goto label_1c8b6c;
        case 0x1c8b70u: goto label_1c8b70;
        case 0x1c8b74u: goto label_1c8b74;
        case 0x1c8b78u: goto label_1c8b78;
        case 0x1c8b7cu: goto label_1c8b7c;
        case 0x1c8b80u: goto label_1c8b80;
        case 0x1c8b84u: goto label_1c8b84;
        case 0x1c8b88u: goto label_1c8b88;
        case 0x1c8b8cu: goto label_1c8b8c;
        case 0x1c8b90u: goto label_1c8b90;
        case 0x1c8b94u: goto label_1c8b94;
        case 0x1c8b98u: goto label_1c8b98;
        case 0x1c8b9cu: goto label_1c8b9c;
        case 0x1c8ba0u: goto label_1c8ba0;
        case 0x1c8ba4u: goto label_1c8ba4;
        case 0x1c8ba8u: goto label_1c8ba8;
        case 0x1c8bacu: goto label_1c8bac;
        case 0x1c8bb0u: goto label_1c8bb0;
        case 0x1c8bb4u: goto label_1c8bb4;
        case 0x1c8bb8u: goto label_1c8bb8;
        case 0x1c8bbcu: goto label_1c8bbc;
        case 0x1c8bc0u: goto label_1c8bc0;
        case 0x1c8bc4u: goto label_1c8bc4;
        case 0x1c8bc8u: goto label_1c8bc8;
        case 0x1c8bccu: goto label_1c8bcc;
        case 0x1c8bd0u: goto label_1c8bd0;
        case 0x1c8bd4u: goto label_1c8bd4;
        case 0x1c8bd8u: goto label_1c8bd8;
        case 0x1c8bdcu: goto label_1c8bdc;
        case 0x1c8be0u: goto label_1c8be0;
        case 0x1c8be4u: goto label_1c8be4;
        case 0x1c8be8u: goto label_1c8be8;
        case 0x1c8becu: goto label_1c8bec;
        case 0x1c8bf0u: goto label_1c8bf0;
        case 0x1c8bf4u: goto label_1c8bf4;
        case 0x1c8bf8u: goto label_1c8bf8;
        case 0x1c8bfcu: goto label_1c8bfc;
        case 0x1c8c00u: goto label_1c8c00;
        case 0x1c8c04u: goto label_1c8c04;
        case 0x1c8c08u: goto label_1c8c08;
        case 0x1c8c0cu: goto label_1c8c0c;
        case 0x1c8c10u: goto label_1c8c10;
        case 0x1c8c14u: goto label_1c8c14;
        case 0x1c8c18u: goto label_1c8c18;
        case 0x1c8c1cu: goto label_1c8c1c;
        case 0x1c8c20u: goto label_1c8c20;
        case 0x1c8c24u: goto label_1c8c24;
        case 0x1c8c28u: goto label_1c8c28;
        case 0x1c8c2cu: goto label_1c8c2c;
        case 0x1c8c30u: goto label_1c8c30;
        case 0x1c8c34u: goto label_1c8c34;
        case 0x1c8c38u: goto label_1c8c38;
        case 0x1c8c3cu: goto label_1c8c3c;
        case 0x1c8c40u: goto label_1c8c40;
        case 0x1c8c44u: goto label_1c8c44;
        case 0x1c8c48u: goto label_1c8c48;
        case 0x1c8c4cu: goto label_1c8c4c;
        case 0x1c8c50u: goto label_1c8c50;
        case 0x1c8c54u: goto label_1c8c54;
        case 0x1c8c58u: goto label_1c8c58;
        case 0x1c8c5cu: goto label_1c8c5c;
        case 0x1c8c60u: goto label_1c8c60;
        case 0x1c8c64u: goto label_1c8c64;
        case 0x1c8c68u: goto label_1c8c68;
        case 0x1c8c6cu: goto label_1c8c6c;
        case 0x1c8c70u: goto label_1c8c70;
        case 0x1c8c74u: goto label_1c8c74;
        case 0x1c8c78u: goto label_1c8c78;
        case 0x1c8c7cu: goto label_1c8c7c;
        case 0x1c8c80u: goto label_1c8c80;
        case 0x1c8c84u: goto label_1c8c84;
        case 0x1c8c88u: goto label_1c8c88;
        case 0x1c8c8cu: goto label_1c8c8c;
        case 0x1c8c90u: goto label_1c8c90;
        case 0x1c8c94u: goto label_1c8c94;
        case 0x1c8c98u: goto label_1c8c98;
        case 0x1c8c9cu: goto label_1c8c9c;
        case 0x1c8ca0u: goto label_1c8ca0;
        case 0x1c8ca4u: goto label_1c8ca4;
        case 0x1c8ca8u: goto label_1c8ca8;
        case 0x1c8cacu: goto label_1c8cac;
        case 0x1c8cb0u: goto label_1c8cb0;
        case 0x1c8cb4u: goto label_1c8cb4;
        case 0x1c8cb8u: goto label_1c8cb8;
        case 0x1c8cbcu: goto label_1c8cbc;
        case 0x1c8cc0u: goto label_1c8cc0;
        case 0x1c8cc4u: goto label_1c8cc4;
        case 0x1c8cc8u: goto label_1c8cc8;
        case 0x1c8cccu: goto label_1c8ccc;
        case 0x1c8cd0u: goto label_1c8cd0;
        case 0x1c8cd4u: goto label_1c8cd4;
        case 0x1c8cd8u: goto label_1c8cd8;
        case 0x1c8cdcu: goto label_1c8cdc;
        case 0x1c8ce0u: goto label_1c8ce0;
        case 0x1c8ce4u: goto label_1c8ce4;
        case 0x1c8ce8u: goto label_1c8ce8;
        case 0x1c8cecu: goto label_1c8cec;
        case 0x1c8cf0u: goto label_1c8cf0;
        case 0x1c8cf4u: goto label_1c8cf4;
        case 0x1c8cf8u: goto label_1c8cf8;
        case 0x1c8cfcu: goto label_1c8cfc;
        case 0x1c8d00u: goto label_1c8d00;
        case 0x1c8d04u: goto label_1c8d04;
        case 0x1c8d08u: goto label_1c8d08;
        case 0x1c8d0cu: goto label_1c8d0c;
        case 0x1c8d10u: goto label_1c8d10;
        case 0x1c8d14u: goto label_1c8d14;
        case 0x1c8d18u: goto label_1c8d18;
        case 0x1c8d1cu: goto label_1c8d1c;
        case 0x1c8d20u: goto label_1c8d20;
        case 0x1c8d24u: goto label_1c8d24;
        case 0x1c8d28u: goto label_1c8d28;
        case 0x1c8d2cu: goto label_1c8d2c;
        case 0x1c8d30u: goto label_1c8d30;
        case 0x1c8d34u: goto label_1c8d34;
        case 0x1c8d38u: goto label_1c8d38;
        case 0x1c8d3cu: goto label_1c8d3c;
        case 0x1c8d40u: goto label_1c8d40;
        case 0x1c8d44u: goto label_1c8d44;
        case 0x1c8d48u: goto label_1c8d48;
        case 0x1c8d4cu: goto label_1c8d4c;
        case 0x1c8d50u: goto label_1c8d50;
        case 0x1c8d54u: goto label_1c8d54;
        case 0x1c8d58u: goto label_1c8d58;
        case 0x1c8d5cu: goto label_1c8d5c;
        case 0x1c8d60u: goto label_1c8d60;
        case 0x1c8d64u: goto label_1c8d64;
        case 0x1c8d68u: goto label_1c8d68;
        case 0x1c8d6cu: goto label_1c8d6c;
        case 0x1c8d70u: goto label_1c8d70;
        case 0x1c8d74u: goto label_1c8d74;
        case 0x1c8d78u: goto label_1c8d78;
        case 0x1c8d7cu: goto label_1c8d7c;
        case 0x1c8d80u: goto label_1c8d80;
        case 0x1c8d84u: goto label_1c8d84;
        case 0x1c8d88u: goto label_1c8d88;
        case 0x1c8d8cu: goto label_1c8d8c;
        case 0x1c8d90u: goto label_1c8d90;
        case 0x1c8d94u: goto label_1c8d94;
        case 0x1c8d98u: goto label_1c8d98;
        case 0x1c8d9cu: goto label_1c8d9c;
        case 0x1c8da0u: goto label_1c8da0;
        case 0x1c8da4u: goto label_1c8da4;
        case 0x1c8da8u: goto label_1c8da8;
        case 0x1c8dacu: goto label_1c8dac;
        case 0x1c8db0u: goto label_1c8db0;
        case 0x1c8db4u: goto label_1c8db4;
        case 0x1c8db8u: goto label_1c8db8;
        case 0x1c8dbcu: goto label_1c8dbc;
        case 0x1c8dc0u: goto label_1c8dc0;
        case 0x1c8dc4u: goto label_1c8dc4;
        case 0x1c8dc8u: goto label_1c8dc8;
        case 0x1c8dccu: goto label_1c8dcc;
        case 0x1c8dd0u: goto label_1c8dd0;
        case 0x1c8dd4u: goto label_1c8dd4;
        case 0x1c8dd8u: goto label_1c8dd8;
        case 0x1c8ddcu: goto label_1c8ddc;
        case 0x1c8de0u: goto label_1c8de0;
        case 0x1c8de4u: goto label_1c8de4;
        case 0x1c8de8u: goto label_1c8de8;
        case 0x1c8decu: goto label_1c8dec;
        case 0x1c8df0u: goto label_1c8df0;
        case 0x1c8df4u: goto label_1c8df4;
        case 0x1c8df8u: goto label_1c8df8;
        case 0x1c8dfcu: goto label_1c8dfc;
        case 0x1c8e00u: goto label_1c8e00;
        case 0x1c8e04u: goto label_1c8e04;
        case 0x1c8e08u: goto label_1c8e08;
        case 0x1c8e0cu: goto label_1c8e0c;
        case 0x1c8e10u: goto label_1c8e10;
        case 0x1c8e14u: goto label_1c8e14;
        case 0x1c8e18u: goto label_1c8e18;
        case 0x1c8e1cu: goto label_1c8e1c;
        case 0x1c8e20u: goto label_1c8e20;
        case 0x1c8e24u: goto label_1c8e24;
        case 0x1c8e28u: goto label_1c8e28;
        case 0x1c8e2cu: goto label_1c8e2c;
        case 0x1c8e30u: goto label_1c8e30;
        case 0x1c8e34u: goto label_1c8e34;
        case 0x1c8e38u: goto label_1c8e38;
        case 0x1c8e3cu: goto label_1c8e3c;
        case 0x1c8e40u: goto label_1c8e40;
        case 0x1c8e44u: goto label_1c8e44;
        case 0x1c8e48u: goto label_1c8e48;
        case 0x1c8e4cu: goto label_1c8e4c;
        case 0x1c8e50u: goto label_1c8e50;
        case 0x1c8e54u: goto label_1c8e54;
        case 0x1c8e58u: goto label_1c8e58;
        case 0x1c8e5cu: goto label_1c8e5c;
        case 0x1c8e60u: goto label_1c8e60;
        case 0x1c8e64u: goto label_1c8e64;
        case 0x1c8e68u: goto label_1c8e68;
        case 0x1c8e6cu: goto label_1c8e6c;
        case 0x1c8e70u: goto label_1c8e70;
        case 0x1c8e74u: goto label_1c8e74;
        case 0x1c8e78u: goto label_1c8e78;
        case 0x1c8e7cu: goto label_1c8e7c;
        case 0x1c8e80u: goto label_1c8e80;
        case 0x1c8e84u: goto label_1c8e84;
        case 0x1c8e88u: goto label_1c8e88;
        case 0x1c8e8cu: goto label_1c8e8c;
        case 0x1c8e90u: goto label_1c8e90;
        case 0x1c8e94u: goto label_1c8e94;
        case 0x1c8e98u: goto label_1c8e98;
        case 0x1c8e9cu: goto label_1c8e9c;
        case 0x1c8ea0u: goto label_1c8ea0;
        case 0x1c8ea4u: goto label_1c8ea4;
        case 0x1c8ea8u: goto label_1c8ea8;
        case 0x1c8eacu: goto label_1c8eac;
        case 0x1c8eb0u: goto label_1c8eb0;
        case 0x1c8eb4u: goto label_1c8eb4;
        case 0x1c8eb8u: goto label_1c8eb8;
        case 0x1c8ebcu: goto label_1c8ebc;
        case 0x1c8ec0u: goto label_1c8ec0;
        case 0x1c8ec4u: goto label_1c8ec4;
        case 0x1c8ec8u: goto label_1c8ec8;
        case 0x1c8eccu: goto label_1c8ecc;
        case 0x1c8ed0u: goto label_1c8ed0;
        case 0x1c8ed4u: goto label_1c8ed4;
        case 0x1c8ed8u: goto label_1c8ed8;
        case 0x1c8edcu: goto label_1c8edc;
        case 0x1c8ee0u: goto label_1c8ee0;
        case 0x1c8ee4u: goto label_1c8ee4;
        case 0x1c8ee8u: goto label_1c8ee8;
        case 0x1c8eecu: goto label_1c8eec;
        case 0x1c8ef0u: goto label_1c8ef0;
        case 0x1c8ef4u: goto label_1c8ef4;
        case 0x1c8ef8u: goto label_1c8ef8;
        case 0x1c8efcu: goto label_1c8efc;
        case 0x1c8f00u: goto label_1c8f00;
        case 0x1c8f04u: goto label_1c8f04;
        case 0x1c8f08u: goto label_1c8f08;
        case 0x1c8f0cu: goto label_1c8f0c;
        case 0x1c8f10u: goto label_1c8f10;
        case 0x1c8f14u: goto label_1c8f14;
        case 0x1c8f18u: goto label_1c8f18;
        case 0x1c8f1cu: goto label_1c8f1c;
        case 0x1c8f20u: goto label_1c8f20;
        case 0x1c8f24u: goto label_1c8f24;
        case 0x1c8f28u: goto label_1c8f28;
        case 0x1c8f2cu: goto label_1c8f2c;
        case 0x1c8f30u: goto label_1c8f30;
        case 0x1c8f34u: goto label_1c8f34;
        case 0x1c8f38u: goto label_1c8f38;
        case 0x1c8f3cu: goto label_1c8f3c;
        case 0x1c8f40u: goto label_1c8f40;
        case 0x1c8f44u: goto label_1c8f44;
        case 0x1c8f48u: goto label_1c8f48;
        case 0x1c8f4cu: goto label_1c8f4c;
        case 0x1c8f50u: goto label_1c8f50;
        case 0x1c8f54u: goto label_1c8f54;
        case 0x1c8f58u: goto label_1c8f58;
        case 0x1c8f5cu: goto label_1c8f5c;
        case 0x1c8f60u: goto label_1c8f60;
        case 0x1c8f64u: goto label_1c8f64;
        case 0x1c8f68u: goto label_1c8f68;
        case 0x1c8f6cu: goto label_1c8f6c;
        case 0x1c8f70u: goto label_1c8f70;
        default: break;
    }

    ctx->pc = 0x1c88b0u;

label_1c88b0:
    // 0x1c88b0: 0x27bdfed0  addiu       $sp, $sp, -0x130
    ctx->pc = 0x1c88b0u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294966992));
label_1c88b4:
    // 0x1c88b4: 0xafa40050  sw          $a0, 0x50($sp)
    ctx->pc = 0x1c88b4u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 80), GPR_U32(ctx, 4));
label_1c88b8:
    // 0x1c88b8: 0x7fb10110  sq          $s1, 0x110($sp)
    ctx->pc = 0x1c88b8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 272), GPR_VEC(ctx, 17));
label_1c88bc:
    // 0x1c88bc: 0x7fb20100  sq          $s2, 0x100($sp)
    ctx->pc = 0x1c88bcu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 256), GPR_VEC(ctx, 18));
label_1c88c0:
    // 0x1c88c0: 0x2491004c  addiu       $s1, $a0, 0x4C
    ctx->pc = 0x1c88c0u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 4), 76));
label_1c88c4:
    // 0x1c88c4: 0x7fb00120  sq          $s0, 0x120($sp)
    ctx->pc = 0x1c88c4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 288), GPR_VEC(ctx, 16));
label_1c88c8:
    // 0x1c88c8: 0x24920050  addiu       $s2, $a0, 0x50
    ctx->pc = 0x1c88c8u;
    SET_GPR_S32(ctx, 18, (int32_t)ADD32(GPR_U32(ctx, 4), 80));
label_1c88cc:
    // 0x1c88cc: 0x7fb300f0  sq          $s3, 0xF0($sp)
    ctx->pc = 0x1c88ccu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 240), GPR_VEC(ctx, 19));
label_1c88d0:
    // 0x1c88d0: 0x7fb400e0  sq          $s4, 0xE0($sp)
    ctx->pc = 0x1c88d0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 224), GPR_VEC(ctx, 20));
label_1c88d4:
    // 0x1c88d4: 0x7fb500d0  sq          $s5, 0xD0($sp)
    ctx->pc = 0x1c88d4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 208), GPR_VEC(ctx, 21));
label_1c88d8:
    // 0x1c88d8: 0x7fb600c0  sq          $s6, 0xC0($sp)
    ctx->pc = 0x1c88d8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 192), GPR_VEC(ctx, 22));
label_1c88dc:
    // 0x1c88dc: 0x7fb700b0  sq          $s7, 0xB0($sp)
    ctx->pc = 0x1c88dcu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 176), GPR_VEC(ctx, 23));
label_1c88e0:
    // 0x1c88e0: 0x7fbe00a0  sq          $fp, 0xA0($sp)
    ctx->pc = 0x1c88e0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 30));
label_1c88e4:
    // 0x1c88e4: 0xffbf0090  sd          $ra, 0x90($sp)
    ctx->pc = 0x1c88e4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 144), GPR_U64(ctx, 31));
label_1c88e8:
    // 0x1c88e8: 0x8e220004  lw          $v0, 0x4($s1)
    ctx->pc = 0x1c88e8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 4)));
label_1c88ec:
    // 0x1c88ec: 0x10520195  beq         $v0, $s2, . + 4 + (0x195 << 2)
label_1c88f0:
    if (ctx->pc == 0x1C88F0u) {
        ctx->pc = 0x1C88F0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C88ECu;
        // 0x1c88f0: 0x24820040  addiu       $v0, $a0, 0x40 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 4), 64));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C88F4u;
        goto label_1c88f4;
    }
    ctx->pc = 0x1C88ECu;
    {
        const bool branch_taken_0x1c88ec = (GPR_U64(ctx, 2) == GPR_U64(ctx, 18));
        ctx->pc = 0x1C88F0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C88ECu;
        // 0x1c88f0: 0x24820040  addiu       $v0, $a0, 0x40 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 4), 64));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c88ec) {
            ctx->pc = 0x1C8F44u;
            goto label_1c8f44;
        }
    }
    ctx->pc = 0x1C88F4u;
label_1c88f4:
    // 0x1c88f4: 0xafa20060  sw          $v0, 0x60($sp)
    ctx->pc = 0x1c88f4u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 96), GPR_U32(ctx, 2));
label_1c88f8:
    // 0x1c88f8: 0x8c500008  lw          $s0, 0x8($v0)
    ctx->pc = 0x1c88f8u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 8)));
label_1c88fc:
    // 0x1c88fc: 0x12000191  beqz        $s0, . + 4 + (0x191 << 2)
label_1c8900:
    if (ctx->pc == 0x1C8900u) {
        ctx->pc = 0x1C8900u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C88FCu;
        // 0x1c8900: 0x27a50020  addiu       $a1, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8904u;
        goto label_1c8904;
    }
    ctx->pc = 0x1C88FCu;
    {
        const bool branch_taken_0x1c88fc = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8900u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C88FCu;
        // 0x1c8900: 0x27a50020  addiu       $a1, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c88fc) {
            ctx->pc = 0x1C8F44u;
            goto label_1c8f44;
        }
    }
    ctx->pc = 0x1C8904u;
label_1c8904:
    // 0x1c8904: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x1c8904u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
label_1c8908:
    // 0x1c8908: 0x27a60024  addiu       $a2, $sp, 0x24
    ctx->pc = 0x1c8908u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 29), 36));
label_1c890c:
    // 0x1c890c: 0xc070d9e  jal         func_1C3678
label_1c8910:
    if (ctx->pc == 0x1C8910u) {
        ctx->pc = 0x1C8910u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C890Cu;
        // 0x1c8910: 0x27a70028  addiu       $a3, $sp, 0x28 (Delay Slot)
        SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 29), 40));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8914u;
        goto label_1c8914;
    }
    ctx->pc = 0x1C890Cu;
    SET_GPR_U32(ctx, 31, 0x1C8914u);
    ctx->pc = 0x1C8910u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C890Cu;
    // 0x1c8910: 0x27a70028  addiu       $a3, $sp, 0x28 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 29), 40));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C3678u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C3678u, 0x1C890Cu, 0x1C8914u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8914u;
label_1c8914:
    // 0x1c8914: 0x8fa20020  lw          $v0, 0x20($sp)
    ctx->pc = 0x1c8914u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
label_1c8918:
    // 0x1c8918: 0x5040018b  beql        $v0, $zero, . + 4 + (0x18B << 2)
label_1c891c:
    if (ctx->pc == 0x1C891Cu) {
        ctx->pc = 0x1C891Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8918u;
        // 0x1c891c: 0x7bb00120  lq          $s0, 0x120($sp) (Delay Slot)
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 288)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8920u;
        goto label_1c8920;
    }
    ctx->pc = 0x1C8918u;
    {
        const bool branch_taken_0x1c8918 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8918) {
            ctx->pc = 0x1C891Cu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C8918u;
            // 0x1c891c: 0x7bb00120  lq          $s0, 0x120($sp) (Delay Slot)
            SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 288)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C8F48u;
            goto label_1c8f48;
        }
    }
    ctx->pc = 0x1C8920u;
label_1c8920:
    // 0x1c8920: 0x8e030038  lw          $v1, 0x38($s0)
    ctx->pc = 0x1c8920u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 56)));
label_1c8924:
    // 0x1c8924: 0x84640018  lh          $a0, 0x18($v1)
    ctx->pc = 0x1c8924u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 3), 24)));
label_1c8928:
    // 0x1c8928: 0x8c62001c  lw          $v0, 0x1C($v1)
    ctx->pc = 0x1c8928u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 28)));
label_1c892c:
    // 0x1c892c: 0x40f809  jalr        $v0
label_1c8930:
    if (ctx->pc == 0x1C8930u) {
        ctx->pc = 0x1C8930u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C892Cu;
        // 0x1c8930: 0x2042021  addu        $a0, $s0, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8934u;
        goto label_1c8934;
    }
    ctx->pc = 0x1C892Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 2);
        SET_GPR_U32(ctx, 31, 0x1C8934u);
        ctx->pc = 0x1C8930u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C892Cu;
        // 0x1c8930: 0x2042021  addu        $a0, $s0, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C892Cu, 0x1C8934u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x1C8934u;
label_1c8934:
    // 0x1c8934: 0x40a82d  daddu       $s5, $v0, $zero
    ctx->pc = 0x1c8934u;
    SET_GPR_U64(ctx, 21, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8938:
    // 0x1c8938: 0x16a00003  bnez        $s5, . + 4 + (0x3 << 2)
label_1c893c:
    if (ctx->pc == 0x1C893Cu) {
        ctx->pc = 0x1C893Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8938u;
        // 0x1c893c: 0x27a30030  addiu       $v1, $sp, 0x30 (Delay Slot)
        SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8940u;
        goto label_1c8940;
    }
    ctx->pc = 0x1C8938u;
    {
        const bool branch_taken_0x1c8938 = (GPR_U64(ctx, 21) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C893Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8938u;
        // 0x1c893c: 0x27a30030  addiu       $v1, $sp, 0x30 (Delay Slot)
        SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8938) {
            ctx->pc = 0x1C8948u;
            goto label_1c8948;
        }
    }
    ctx->pc = 0x1C8940u;
label_1c8940:
    // 0x1c8940: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c8940u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
label_1c8944:
    // 0x1c8944: 0x8c550e08  lw          $s5, 0xE08($v0)
    ctx->pc = 0x1c8944u;
    SET_GPR_S32(ctx, 21, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 3592)));
label_1c8948:
    // 0x1c8948: 0x3c026c01  lui         $v0, 0x6C01
    ctx->pc = 0x1c8948u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)27649 << 16));
label_1c894c:
    // 0x1c894c: 0xafa30070  sw          $v1, 0x70($sp)
    ctx->pc = 0x1c894cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 112), GPR_U32(ctx, 3));
label_1c8950:
    // 0x1c8950: 0x27a40038  addiu       $a0, $sp, 0x38
    ctx->pc = 0x1c8950u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 56));
label_1c8954:
    // 0x1c8954: 0xafa20054  sw          $v0, 0x54($sp)
    ctx->pc = 0x1c8954u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 84), GPR_U32(ctx, 2));
label_1c8958:
    // 0x1c8958: 0x27a30044  addiu       $v1, $sp, 0x44
    ctx->pc = 0x1c8958u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 29), 68));
label_1c895c:
    // 0x1c895c: 0xafa40074  sw          $a0, 0x74($sp)
    ctx->pc = 0x1c895cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 116), GPR_U32(ctx, 4));
label_1c8960:
    // 0x1c8960: 0x27a20040  addiu       $v0, $sp, 0x40
    ctx->pc = 0x1c8960u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 29), 64));
label_1c8964:
    // 0x1c8964: 0xafa3007c  sw          $v1, 0x7C($sp)
    ctx->pc = 0x1c8964u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 124), GPR_U32(ctx, 3));
label_1c8968:
    // 0x1c8968: 0x27a40048  addiu       $a0, $sp, 0x48
    ctx->pc = 0x1c8968u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 72));
label_1c896c:
    // 0x1c896c: 0x8fa30054  lw          $v1, 0x54($sp)
    ctx->pc = 0x1c896cu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 84)));
label_1c8970:
    // 0x1c8970: 0x26100130  addiu       $s0, $s0, 0x130
    ctx->pc = 0x1c8970u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 304));
label_1c8974:
    // 0x1c8974: 0xafa20078  sw          $v0, 0x78($sp)
    ctx->pc = 0x1c8974u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 120), GPR_U32(ctx, 2));
label_1c8978:
    // 0x1c8978: 0xafa40080  sw          $a0, 0x80($sp)
    ctx->pc = 0x1c8978u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 128), GPR_U32(ctx, 4));
label_1c897c:
    // 0x1c897c: 0x27a2004c  addiu       $v0, $sp, 0x4C
    ctx->pc = 0x1c897cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 29), 76));
label_1c8980:
    // 0x1c8980: 0x346302a8  ori         $v1, $v1, 0x2A8
    ctx->pc = 0x1c8980u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) | (uint64_t)(uint16_t)680);
label_1c8984:
    // 0x1c8984: 0x27a4003c  addiu       $a0, $sp, 0x3C
    ctx->pc = 0x1c8984u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 29), 60));
label_1c8988:
    // 0x1c8988: 0xafb0006c  sw          $s0, 0x6C($sp)
    ctx->pc = 0x1c8988u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 108), GPR_U32(ctx, 16));
label_1c898c:
    // 0x1c898c: 0xafb00058  sw          $s0, 0x58($sp)
    ctx->pc = 0x1c898cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 88), GPR_U32(ctx, 16));
label_1c8990:
    // 0x1c8990: 0xafb10064  sw          $s1, 0x64($sp)
    ctx->pc = 0x1c8990u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 100), GPR_U32(ctx, 17));
label_1c8994:
    // 0x1c8994: 0xafb20068  sw          $s2, 0x68($sp)
    ctx->pc = 0x1c8994u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 104), GPR_U32(ctx, 18));
label_1c8998:
    // 0x1c8998: 0xafa20084  sw          $v0, 0x84($sp)
    ctx->pc = 0x1c8998u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 132), GPR_U32(ctx, 2));
label_1c899c:
    // 0x1c899c: 0xafa30054  sw          $v1, 0x54($sp)
    ctx->pc = 0x1c899cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 84), GPR_U32(ctx, 3));
label_1c89a0:
    // 0x1c89a0: 0xafa4005c  sw          $a0, 0x5C($sp)
    ctx->pc = 0x1c89a0u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 92), GPR_U32(ctx, 4));
label_1c89a4:
    // 0x1c89a4: 0x24020004  addiu       $v0, $zero, 0x4
    ctx->pc = 0x1c89a4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
label_1c89a8:
    // 0x1c89a8: 0x8fa50070  lw          $a1, 0x70($sp)
    ctx->pc = 0x1c89a8u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 112)));
label_1c89ac:
    // 0x1c89ac: 0x2a0202d  daddu       $a0, $s5, $zero
    ctx->pc = 0x1c89acu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
label_1c89b0:
    // 0x1c89b0: 0xc10fa9c  jal         func_43EA70
label_1c89b4:
    if (ctx->pc == 0x1C89B4u) {
        ctx->pc = 0x1C89B4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C89B0u;
        // 0x1c89b4: 0xffa20030  sd          $v0, 0x30($sp) (Delay Slot)
        WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 2));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C89B8u;
        goto label_1c89b8;
    }
    ctx->pc = 0x1C89B0u;
    SET_GPR_U32(ctx, 31, 0x1C89B8u);
    ctx->pc = 0x1C89B4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C89B0u;
    // 0x1c89b4: 0xffa20030  sd          $v0, 0x30($sp) (Delay Slot)
    WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43EA70u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43EA70u, 0x1C89B0u, 0x1C89B8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C89B8u;
label_1c89b8:
    // R2: PsMat::Select has latched this pass's material; submit every
    // instance under it. Reached once per return, fresh or resumed.
    ghpcMultiMeshSubmit(rdram, ctx, runtime, READ32(ADD32(GPR_U32(ctx, 29), 0x50u)));
    // 0x1c89b8: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c89b8u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
label_1c89bc:
    // 0x1c89bc: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c89bcu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
label_1c89c0:
    // 0x1c89c0: 0x40a82d  daddu       $s5, $v0, $zero
    ctx->pc = 0x1c89c0u;
    SET_GPR_U64(ctx, 21, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c89c4:
    // 0x1c89c4: 0x3c041000  lui         $a0, 0x1000
    ctx->pc = 0x1c89c4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)4096 << 16));
label_1c89c8:
    // 0x1c89c8: 0x3405c001  ori         $a1, $zero, 0xC001
    ctx->pc = 0x1c89c8u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)49153);
label_1c89cc:
    // 0x1c89cc: 0x52bbc  dsll32      $a1, $a1, 14
    ctx->pc = 0x1c89ccu;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) << (32 + 14));
label_1c89d0:
    // 0x1c89d0: 0x34a58000  ori         $a1, $a1, 0x8000
    ctx->pc = 0x1c89d0u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)32768);
label_1c89d4:
    // 0x1c89d4: 0x60102d  daddu       $v0, $v1, $zero
    ctx->pc = 0x1c89d4u;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
label_1c89d8:
    // 0x1c89d8: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c89d8u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c89dc:
    // 0x1c89dc: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c89dcu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c89e0:
    // 0x1c89e0: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c89e0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_1c89e4:
    // 0x1c89e4: 0x8fa30054  lw          $v1, 0x54($sp)
    ctx->pc = 0x1c89e4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 84)));
label_1c89e8:
    // 0x1c89e8: 0xac440008  sw          $a0, 0x8($v0)
    ctx->pc = 0x1c89e8u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 8), GPR_U32(ctx, 4));
label_1c89ec:
    // 0x1c89ec: 0xac43000c  sw          $v1, 0xC($v0)
    ctx->pc = 0x1c89ecu;
    WRITE32(ADD32(GPR_U32(ctx, 2), 12), GPR_U32(ctx, 3));
label_1c89f0:
    // 0x1c89f0: 0xac400000  sw          $zero, 0x0($v0)
    ctx->pc = 0x1c89f0u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 0));
label_1c89f4:
    // 0x1c89f4: 0xac400004  sw          $zero, 0x4($v0)
    ctx->pc = 0x1c89f4u;
    WRITE32(ADD32(GPR_U32(ctx, 2), 4), GPR_U32(ctx, 0));
label_1c89f8:
    // 0x1c89f8: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c89f8u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
label_1c89fc:
    // 0x1c89fc: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c89fcu;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
label_1c8a00:
    // 0x1c8a00: 0xdfa20030  ld          $v0, 0x30($sp)
    ctx->pc = 0x1c8a00u;
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 29), 48)));
label_1c8a04:
    // 0x1c8a04: 0x60202d  daddu       $a0, $v1, $zero
    ctx->pc = 0x1c8a04u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
label_1c8a08:
    // 0x1c8a08: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c8a08u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8a0c:
    // 0x1c8a0c: 0x213fc  dsll32      $v0, $v0, 15
    ctx->pc = 0x1c8a0cu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << (32 + 15));
label_1c8a10:
    // 0x1c8a10: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8a10u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8a14:
    // 0x1c8a14: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c8a14u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_1c8a18:
    // 0x1c8a18: 0x451025  or          $v0, $v0, $a1
    ctx->pc = 0x1c8a18u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | GPR_U64(ctx, 5));
label_1c8a1c:
    // 0x1c8a1c: 0xfc820000  sd          $v0, 0x0($a0)
    ctx->pc = 0x1c8a1cu;
    WRITE64(ADD32(GPR_U32(ctx, 4), 0), GPR_U64(ctx, 2));
label_1c8a20:
    // 0x1c8a20: 0x24020412  addiu       $v0, $zero, 0x412
    ctx->pc = 0x1c8a20u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1042));
label_1c8a24:
    // 0x1c8a24: 0xfc820008  sd          $v0, 0x8($a0)
    ctx->pc = 0x1c8a24u;
    WRITE64(ADD32(GPR_U32(ctx, 4), 8), GPR_U64(ctx, 2));
label_1c8a28:
    // 0x1c8a28: 0x8fa40064  lw          $a0, 0x64($sp)
    ctx->pc = 0x1c8a28u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 100)));
label_1c8a2c:
    // 0x1c8a2c: 0x8fa30068  lw          $v1, 0x68($sp)
    ctx->pc = 0x1c8a2cu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 104)));
label_1c8a30:
    // 0x1c8a30: 0xafa30038  sw          $v1, 0x38($sp)
    ctx->pc = 0x1c8a30u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 56), GPR_U32(ctx, 3));
label_1c8a34:
    // 0x1c8a34: 0x8c820004  lw          $v0, 0x4($a0)
    ctx->pc = 0x1c8a34u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 4)));
label_1c8a38:
    // 0x1c8a38: 0x10430009  beq         $v0, $v1, . + 4 + (0x9 << 2)
label_1c8a3c:
    if (ctx->pc == 0x1C8A3Cu) {
        ctx->pc = 0x1C8A3Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8A38u;
        // 0x1c8a3c: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8A40u;
        goto label_1c8a40;
    }
    ctx->pc = 0x1C8A38u;
    {
        const bool branch_taken_0x1c8a38 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 3));
        ctx->pc = 0x1C8A3Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8A38u;
        // 0x1c8a3c: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8a38) {
            ctx->pc = 0x1C8A60u;
            goto label_1c8a60;
        }
    }
    ctx->pc = 0x1C8A40u;
label_1c8a40:
    // 0x1c8a40: 0x8fa40074  lw          $a0, 0x74($sp)
    ctx->pc = 0x1c8a40u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 116)));
label_1c8a44:
    // 0x1c8a44: 0x8c830000  lw          $v1, 0x0($a0)
    ctx->pc = 0x1c8a44u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 0)));
label_1c8a48:
    // 0x1c8a48: 0x8c420000  lw          $v0, 0x0($v0)
    ctx->pc = 0x1c8a48u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 0)));
label_1c8a4c:
    // 0x1c8a4c: 0x0  nop
    ctx->pc = 0x1c8a4cu;
    // NOP
label_1c8a50:
    // 0x1c8a50: 0x0  nop
    ctx->pc = 0x1c8a50u;
    // NOP
label_1c8a54:
    // 0x1c8a54: 0x0  nop
    ctx->pc = 0x1c8a54u;
    // NOP
label_1c8a58:
    // 0x1c8a58: 0x1443fffb  bne         $v0, $v1, . + 4 + (-0x5 << 2)
label_1c8a5c:
    if (ctx->pc == 0x1C8A5Cu) {
        ctx->pc = 0x1C8A5Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8A58u;
        // 0x1c8a5c: 0x24e70001  addiu       $a3, $a3, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 1));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8A60u;
        goto label_1c8a60;
    }
    ctx->pc = 0x1C8A58u;
    {
        const bool branch_taken_0x1c8a58 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 3));
        ctx->pc = 0x1C8A5Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8A58u;
        // 0x1c8a5c: 0x24e70001  addiu       $a3, $a3, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 1));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8a58) {
            ctx->pc = 0x1C8A48u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c8a48;
        }
    }
    ctx->pc = 0x1C8A60u;
label_1c8a60:
    // 0x1c8a60: 0x3c03004f  lui         $v1, 0x4F
    ctx->pc = 0x1c8a60u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)79 << 16));
label_1c8a64:
    // 0x1c8a64: 0x3c04004f  lui         $a0, 0x4F
    ctx->pc = 0x1c8a64u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)79 << 16));
label_1c8a68:
    // 0x1c8a68: 0x24632cb8  addiu       $v1, $v1, 0x2CB8
    ctx->pc = 0x1c8a68u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 11448));
label_1c8a6c:
    // 0x1c8a6c: 0x8c852cb8  lw          $a1, 0x2CB8($a0)
    ctx->pc = 0x1c8a6cu;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 11448)));
label_1c8a70:
    // 0x1c8a70: 0x8c62001c  lw          $v0, 0x1C($v1)
    ctx->pc = 0x1c8a70u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 28)));
label_1c8a74:
    // 0x1c8a74: 0xafa70044  sw          $a3, 0x44($sp)
    ctx->pc = 0x1c8a74u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 68), GPR_U32(ctx, 7));
label_1c8a78:
    // 0x1c8a78: 0x471021  addu        $v0, $v0, $a3
    ctx->pc = 0x1c8a78u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 7)));
label_1c8a7c:
    // 0x1c8a7c: 0xac62001c  sw          $v0, 0x1C($v1)
    ctx->pc = 0x1c8a7cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 28), GPR_U32(ctx, 2));
label_1c8a80:
    // 0x1c8a80: 0x8fa20058  lw          $v0, 0x58($sp)
    ctx->pc = 0x1c8a80u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 88)));
label_1c8a84:
    // 0x1c8a84: 0x8c660004  lw          $a2, 0x4($v1)
    ctx->pc = 0x1c8a84u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 4)));
label_1c8a88:
    // 0x1c8a88: 0x8c430008  lw          $v1, 0x8($v0)
    ctx->pc = 0x1c8a88u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 8)));
label_1c8a8c:
    // 0x1c8a8c: 0x8fa40020  lw          $a0, 0x20($sp)
    ctx->pc = 0x1c8a8cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
label_1c8a90:
    // 0x1c8a90: 0x94620158  lhu         $v0, 0x158($v1)
    ctx->pc = 0x1c8a90u;
    SET_GPR_U32(ctx, 2, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 344)));
label_1c8a94:
    // 0x1c8a94: 0x3c03004f  lui         $v1, 0x4F
    ctx->pc = 0x1c8a94u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)79 << 16));
label_1c8a98:
    // 0x1c8a98: 0xe21018  mult        $v0, $a3, $v0
    ctx->pc = 0x1c8a98u;
    { int64_t result = (int64_t)GPR_S32(ctx, 7) * (int64_t)GPR_S32(ctx, 2); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 2, (int32_t)result); }
label_1c8a9c:
    // 0x1c8a9c: 0xa22821  addu        $a1, $a1, $v0
    ctx->pc = 0x1c8a9cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 2)));
label_1c8aa0:
    // 0x1c8aa0: 0xac652cb8  sw          $a1, 0x2CB8($v1)
    ctx->pc = 0x1c8aa0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 11448), GPR_U32(ctx, 5));
label_1c8aa4:
    // 0x1c8aa4: 0x8fa20058  lw          $v0, 0x58($sp)
    ctx->pc = 0x1c8aa4u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 88)));
label_1c8aa8:
    // 0x1c8aa8: 0x8c430008  lw          $v1, 0x8($v0)
    ctx->pc = 0x1c8aa8u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 8)));
label_1c8aac:
    // 0x1c8aac: 0x9462015a  lhu         $v0, 0x15A($v1)
    ctx->pc = 0x1c8aacu;
    SET_GPR_U32(ctx, 2, (uint16_t)READ16(ADD32(GPR_U32(ctx, 3), 346)));
label_1c8ab0:
    // 0x1c8ab0: 0x3c03004f  lui         $v1, 0x4F
    ctx->pc = 0x1c8ab0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)79 << 16));
label_1c8ab4:
    // 0x1c8ab4: 0xe21018  mult        $v0, $a3, $v0
    ctx->pc = 0x1c8ab4u;
    { int64_t result = (int64_t)GPR_S32(ctx, 7) * (int64_t)GPR_S32(ctx, 2); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 2, (int32_t)result); }
label_1c8ab8:
    // 0x1c8ab8: 0x24632cb8  addiu       $v1, $v1, 0x2CB8
    ctx->pc = 0x1c8ab8u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 11448));
label_1c8abc:
    // 0x1c8abc: 0xc23021  addu        $a2, $a2, $v0
    ctx->pc = 0x1c8abcu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), GPR_U32(ctx, 2)));
label_1c8ac0:
    // 0x1c8ac0: 0xc0c8454  jal         func_321150
label_1c8ac4:
    if (ctx->pc == 0x1C8AC4u) {
        ctx->pc = 0x1C8AC4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8AC0u;
        // 0x1c8ac4: 0xac660004  sw          $a2, 0x4($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 6));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8AC8u;
        goto label_1c8ac8;
    }
    ctx->pc = 0x1C8AC0u;
    SET_GPR_U32(ctx, 31, 0x1C8AC8u);
    ctx->pc = 0x1C8AC4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8AC0u;
    // 0x1c8ac4: 0xac660004  sw          $a2, 0x4($v1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 6));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321150u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321150u, 0x1C8AC0u, 0x1C8AC8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8AC8u;
label_1c8ac8:
    // 0x1c8ac8: 0x8fa40058  lw          $a0, 0x58($sp)
    ctx->pc = 0x1c8ac8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 88)));
label_1c8acc:
    // 0x1c8acc: 0x40902d  daddu       $s2, $v0, $zero
    ctx->pc = 0x1c8accu;
    SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8ad0:
    // 0x1c8ad0: 0x8c820008  lw          $v0, 0x8($a0)
    ctx->pc = 0x1c8ad0u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));
label_1c8ad4:
    // 0x1c8ad4: 0x5440000e  bnel        $v0, $zero, . + 4 + (0xE << 2)
label_1c8ad8:
    if (ctx->pc == 0x1C8AD8u) {
        ctx->pc = 0x1C8AD8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8AD4u;
        // 0x1c8ad8: 0x8fa4006c  lw          $a0, 0x6C($sp) (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 108)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8ADCu;
        goto label_1c8adc;
    }
    ctx->pc = 0x1C8AD4u;
    {
        const bool branch_taken_0x1c8ad4 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c8ad4) {
            ctx->pc = 0x1C8AD8u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C8AD4u;
            // 0x1c8ad8: 0x8fa4006c  lw          $a0, 0x6C($sp) (Delay Slot)
            SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 108)));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C8B10u;
            goto label_1c8b10;
        }
    }
    ctx->pc = 0x1C8ADCu;
label_1c8adc:
    // 0x1c8adc: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c8adcu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
label_1c8ae0:
    // 0x1c8ae0: 0x3c030049  lui         $v1, 0x49
    ctx->pc = 0x1c8ae0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)73 << 16));
label_1c8ae4:
    // 0x1c8ae4: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c8ae4u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20416)));
label_1c8ae8:
    // 0x1c8ae8: 0x2465e140  addiu       $a1, $v1, -0x1EC0
    ctx->pc = 0x1c8ae8u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 4294959424));
label_1c8aec:
    // 0x1c8aec: 0x3c020049  lui         $v0, 0x49
    ctx->pc = 0x1c8aecu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)73 << 16));
label_1c8af0:
    // 0x1c8af0: 0x24060063  addiu       $a2, $zero, 0x63
    ctx->pc = 0x1c8af0u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
label_1c8af4:
    // 0x1c8af4: 0xc0da03a  jal         func_3680E8
label_1c8af8:
    if (ctx->pc == 0x1C8AF8u) {
        ctx->pc = 0x1C8AF8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8AF4u;
        // 0x1c8af8: 0x2447e168  addiu       $a3, $v0, -0x1E98 (Delay Slot)
        SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959464));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8AFCu;
        goto label_1c8afc;
    }
    ctx->pc = 0x1C8AF4u;
    SET_GPR_U32(ctx, 31, 0x1C8AFCu);
    ctx->pc = 0x1C8AF8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8AF4u;
    // 0x1c8af8: 0x2447e168  addiu       $a3, $v0, -0x1E98 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959464));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C8AF4u, 0x1C8AFCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8AFCu;
label_1c8afc:
    // 0x1c8afc: 0x3c030052  lui         $v1, 0x52
    ctx->pc = 0x1c8afcu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)82 << 16));
label_1c8b00:
    // 0x1c8b00: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c8b00u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b04:
    // 0x1c8b04: 0xc0baf6a  jal         func_2EBDA8
label_1c8b08:
    if (ctx->pc == 0x1C8B08u) {
        ctx->pc = 0x1C8B08u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B04u;
        // 0x1c8b08: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B0Cu;
        goto label_1c8b0c;
    }
    ctx->pc = 0x1C8B04u;
    SET_GPR_U32(ctx, 31, 0x1C8B0Cu);
    ctx->pc = 0x1C8B08u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B04u;
    // 0x1c8b08: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C8B04u, 0x1C8B0Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B0Cu;
label_1c8b0c:
    // 0x1c8b0c: 0x8fa4006c  lw          $a0, 0x6C($sp)
    ctx->pc = 0x1c8b0cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 108)));
label_1c8b10:
    // 0x1c8b10: 0x8c830008  lw          $v1, 0x8($a0)
    ctx->pc = 0x1c8b10u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));
label_1c8b14:
    // 0x1c8b14: 0x8c620140  lw          $v0, 0x140($v1)
    ctx->pc = 0x1c8b14u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 320)));
label_1c8b18:
    // 0x1c8b18: 0x3042001f  andi        $v0, $v0, 0x1F
    ctx->pc = 0x1c8b18u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)31);
label_1c8b1c:
    // 0x1c8b1c: 0x14400011  bnez        $v0, . + 4 + (0x11 << 2)
label_1c8b20:
    if (ctx->pc == 0x1C8B20u) {
        ctx->pc = 0x1C8B20u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B1Cu;
        // 0x1c8b20: 0x3c02004f  lui         $v0, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B24u;
        goto label_1c8b24;
    }
    ctx->pc = 0x1C8B1Cu;
    {
        const bool branch_taken_0x1c8b1c = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8B20u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B1Cu;
        // 0x1c8b20: 0x3c02004f  lui         $v0, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8b1c) {
            ctx->pc = 0x1C8B64u;
            goto label_1c8b64;
        }
    }
    ctx->pc = 0x1C8B24u;
label_1c8b24:
    // 0x1c8b24: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c8b24u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
label_1c8b28:
    // 0x1c8b28: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x1c8b28u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b2c:
    // 0x1c8b2c: 0x382d  daddu       $a3, $zero, $zero
    ctx->pc = 0x1c8b2cu;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b30:
    // 0x1c8b30: 0xc10f9e2  jal         func_43E788
label_1c8b34:
    if (ctx->pc == 0x1C8B34u) {
        ctx->pc = 0x1C8B34u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B30u;
        // 0x1c8b34: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B38u;
        goto label_1c8b38;
    }
    ctx->pc = 0x1C8B30u;
    SET_GPR_U32(ctx, 31, 0x1C8B38u);
    ctx->pc = 0x1C8B34u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B30u;
    // 0x1c8b34: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x1C8B30u, 0x1C8B38u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B38u;
label_1c8b38:
    // 0x1c8b38: 0xc10f9d2  jal         func_43E748
label_1c8b3c:
    if (ctx->pc == 0x1C8B3Cu) {
        ctx->pc = 0x1C8B3Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B38u;
        // 0x1c8b3c: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B40u;
        goto label_1c8b40;
    }
    ctx->pc = 0x1C8B38u;
    SET_GPR_U32(ctx, 31, 0x1C8B40u);
    ctx->pc = 0x1C8B3Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B38u;
    // 0x1c8b3c: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x1C8B38u, 0x1C8B40u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B40u;
label_1c8b40:
    // 0x1c8b40: 0x8fa70024  lw          $a3, 0x24($sp)
    ctx->pc = 0x1c8b40u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 36)));
label_1c8b44:
    // 0x1c8b44: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c8b44u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
label_1c8b48:
    // 0x1c8b48: 0x240302d  daddu       $a2, $s2, $zero
    ctx->pc = 0x1c8b48u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b4c:
    // 0x1c8b4c: 0xc10f9e2  jal         func_43E788
label_1c8b50:
    if (ctx->pc == 0x1C8B50u) {
        ctx->pc = 0x1C8B50u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B4Cu;
        // 0x1c8b50: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B54u;
        goto label_1c8b54;
    }
    ctx->pc = 0x1C8B4Cu;
    SET_GPR_U32(ctx, 31, 0x1C8B54u);
    ctx->pc = 0x1C8B50u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B4Cu;
    // 0x1c8b50: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x1C8B4Cu, 0x1C8B54u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B54u;
label_1c8b54:
    // 0x1c8b54: 0xc10f9d2  jal         func_43E748
label_1c8b58:
    if (ctx->pc == 0x1C8B58u) {
        ctx->pc = 0x1C8B58u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B54u;
        // 0x1c8b58: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B5Cu;
        goto label_1c8b5c;
    }
    ctx->pc = 0x1C8B54u;
    SET_GPR_U32(ctx, 31, 0x1C8B5Cu);
    ctx->pc = 0x1C8B58u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B54u;
    // 0x1c8b58: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x1C8B54u, 0x1C8B5Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B5Cu;
label_1c8b5c:
    // 0x1c8b5c: 0x10000034  b           . + 4 + (0x34 << 2)
label_1c8b60:
    if (ctx->pc == 0x1C8B60u) {
        ctx->pc = 0x1C8B60u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B5Cu;
        // 0x1c8b60: 0x8fa40020  lw          $a0, 0x20($sp) (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B64u;
        goto label_1c8b64;
    }
    ctx->pc = 0x1C8B5Cu;
    {
        const bool branch_taken_0x1c8b5c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8B60u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B5Cu;
        // 0x1c8b60: 0x8fa40020  lw          $a0, 0x20($sp) (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8b5c) {
            ctx->pc = 0x1C8C30u;
            goto label_1c8c30;
        }
    }
    ctx->pc = 0x1C8B64u;
label_1c8b64:
    // 0x1c8b64: 0x24502780  addiu       $s0, $v0, 0x2780
    ctx->pc = 0x1c8b64u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 2), 10112));
label_1c8b68:
    // 0x1c8b68: 0xc06fc08  jal         func_1BF020
label_1c8b6c:
    if (ctx->pc == 0x1C8B6Cu) {
        ctx->pc = 0x1C8B6Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B68u;
        // 0x1c8b6c: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B70u;
        goto label_1c8b70;
    }
    ctx->pc = 0x1C8B68u;
    SET_GPR_U32(ctx, 31, 0x1C8B70u);
    ctx->pc = 0x1C8B6Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B68u;
    // 0x1c8b6c: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BF020u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BF020u, 0x1C8B68u, 0x1C8B70u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B70u;
label_1c8b70:
    // 0x1c8b70: 0x8fb10024  lw          $s1, 0x24($sp)
    ctx->pc = 0x1c8b70u;
    SET_GPR_S32(ctx, 17, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 36)));
label_1c8b74:
    // 0x1c8b74: 0x222102a  slt         $v0, $s1, $v0
    ctx->pc = 0x1c8b74u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 17) < (int64_t)GPR_S64(ctx, 2)) ? 1 : 0);
label_1c8b78:
    // 0x1c8b78: 0x14400011  bnez        $v0, . + 4 + (0x11 << 2)
label_1c8b7c:
    if (ctx->pc == 0x1C8B7Cu) {
        ctx->pc = 0x1C8B7Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B78u;
        // 0x1c8b7c: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B80u;
        goto label_1c8b80;
    }
    ctx->pc = 0x1C8B78u;
    {
        const bool branch_taken_0x1c8b78 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8B7Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B78u;
        // 0x1c8b7c: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8b78) {
            ctx->pc = 0x1C8BC0u;
            goto label_1c8bc0;
        }
    }
    ctx->pc = 0x1C8B80u;
label_1c8b80:
    // 0x1c8b80: 0x8fa30050  lw          $v1, 0x50($sp)
    ctx->pc = 0x1c8b80u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 80)));
label_1c8b84:
    // 0x1c8b84: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x1c8b84u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b88:
    // 0x1c8b88: 0x8c620000  lw          $v0, 0x0($v1)
    ctx->pc = 0x1c8b88u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
label_1c8b8c:
    // 0x1c8b8c: 0xc06fc08  jal         func_1BF020
label_1c8b90:
    if (ctx->pc == 0x1C8B90u) {
        ctx->pc = 0x1C8B90u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8B8Cu;
        // 0x1c8b90: 0x8c500014  lw          $s0, 0x14($v0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8B94u;
        goto label_1c8b94;
    }
    ctx->pc = 0x1C8B8Cu;
    SET_GPR_U32(ctx, 31, 0x1C8B94u);
    ctx->pc = 0x1C8B90u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8B8Cu;
    // 0x1c8b90: 0x8c500014  lw          $s0, 0x14($v0) (Delay Slot)
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BF020u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BF020u, 0x1C8B8Cu, 0x1C8B94u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8B94u;
label_1c8b94:
    // 0x1c8b94: 0x40382d  daddu       $a3, $v0, $zero
    ctx->pc = 0x1c8b94u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b98:
    // 0x1c8b98: 0x200282d  daddu       $a1, $s0, $zero
    ctx->pc = 0x1c8b98u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
label_1c8b9c:
    // 0x1c8b9c: 0x3c020049  lui         $v0, 0x49
    ctx->pc = 0x1c8b9cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)73 << 16));
label_1c8ba0:
    // 0x1c8ba0: 0x220302d  daddu       $a2, $s1, $zero
    ctx->pc = 0x1c8ba0u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
label_1c8ba4:
    // 0x1c8ba4: 0xc0ec4be  jal         func_3B12F8
label_1c8ba8:
    if (ctx->pc == 0x1C8BA8u) {
        ctx->pc = 0x1C8BA8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8BA4u;
        // 0x1c8ba8: 0x2444e258  addiu       $a0, $v0, -0x1DA8 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959704));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8BACu;
        goto label_1c8bac;
    }
    ctx->pc = 0x1C8BA4u;
    SET_GPR_U32(ctx, 31, 0x1C8BACu);
    ctx->pc = 0x1C8BA8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8BA4u;
    // 0x1c8ba8: 0x2444e258  addiu       $a0, $v0, -0x1DA8 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959704));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3B12F8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3B12F8u, 0x1C8BA4u, 0x1C8BACu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8BACu;
label_1c8bac:
    // 0x1c8bac: 0x3c030052  lui         $v1, 0x52
    ctx->pc = 0x1c8bacu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)82 << 16));
label_1c8bb0:
    // 0x1c8bb0: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c8bb0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8bb4:
    // 0x1c8bb4: 0xc0baf6a  jal         func_2EBDA8
label_1c8bb8:
    if (ctx->pc == 0x1C8BB8u) {
        ctx->pc = 0x1C8BB8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8BB4u;
        // 0x1c8bb8: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8BBCu;
        goto label_1c8bbc;
    }
    ctx->pc = 0x1C8BB4u;
    SET_GPR_U32(ctx, 31, 0x1C8BBCu);
    ctx->pc = 0x1C8BB8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8BB4u;
    // 0x1c8bb8: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C8BB4u, 0x1C8BBCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8BBCu;
label_1c8bbc:
    // 0x1c8bbc: 0x382d  daddu       $a3, $zero, $zero
    ctx->pc = 0x1c8bbcu;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1c8bc0:
    // 0x1c8bc0: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x1c8bc0u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
label_1c8bc4:
    // 0x1c8bc4: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x1c8bc4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
label_1c8bc8:
    // 0x1c8bc8: 0xc10f9e2  jal         func_43E788
label_1c8bcc:
    if (ctx->pc == 0x1C8BCCu) {
        ctx->pc = 0x1C8BCCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8BC8u;
        // 0x1c8bcc: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8BD0u;
        goto label_1c8bd0;
    }
    ctx->pc = 0x1C8BC8u;
    SET_GPR_U32(ctx, 31, 0x1C8BD0u);
    ctx->pc = 0x1C8BCCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8BC8u;
    // 0x1c8bcc: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x1C8BC8u, 0x1C8BD0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8BD0u;
label_1c8bd0:
    // 0x1c8bd0: 0x3c107000  lui         $s0, 0x7000
    ctx->pc = 0x1c8bd0u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)28672 << 16));
label_1c8bd4:
    // 0x1c8bd4: 0x8e100008  lw          $s0, 0x8($s0)
    ctx->pc = 0x1c8bd4u;
    SET_GPR_S32(ctx, 16, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 16), 8))); // MMIO: 0x70000008
label_1c8bd8:
    // 0x1c8bd8: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x1c8bd8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
label_1c8bdc:
    // 0x1c8bdc: 0xafa00004  sw          $zero, 0x4($sp)
    ctx->pc = 0x1c8bdcu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 0));
label_1c8be0:
    // 0x1c8be0: 0x240282d  daddu       $a1, $s2, $zero
    ctx->pc = 0x1c8be0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
label_1c8be4:
    // 0x1c8be4: 0xc070434  jal         func_1C10D0
label_1c8be8:
    if (ctx->pc == 0x1C8BE8u) {
        ctx->pc = 0x1C8BE8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8BE4u;
        // 0x1c8be8: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8BECu;
        goto label_1c8bec;
    }
    ctx->pc = 0x1C8BE4u;
    SET_GPR_U32(ctx, 31, 0x1C8BECu);
    ctx->pc = 0x1C8BE8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8BE4u;
    // 0x1c8be8: 0x302d  daddu       $a2, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C10D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C10D0u, 0x1C8BE4u, 0x1C8BECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8BECu;
label_1c8bec:
    // 0x1c8bec: 0x8fa20024  lw          $v0, 0x24($sp)
    ctx->pc = 0x1c8becu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 36)));
label_1c8bf0:
    // 0x1c8bf0: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x1c8bf0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
label_1c8bf4:
    // 0x1c8bf4: 0x24050009  addiu       $a1, $zero, 0x9
    ctx->pc = 0x1c8bf4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 9));
label_1c8bf8:
    // 0x1c8bf8: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x1c8bf8u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1c8bfc:
    // 0x1c8bfc: 0x21100  sll         $v0, $v0, 4
    ctx->pc = 0x1c8bfcu;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 4));
label_1c8c00:
    // 0x1c8c00: 0x200382d  daddu       $a3, $s0, $zero
    ctx->pc = 0x1c8c00u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
label_1c8c04:
    // 0x1c8c04: 0x521021  addu        $v0, $v0, $s2
    ctx->pc = 0x1c8c04u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 18)));
label_1c8c08:
    // 0x1c8c08: 0xc10f9ae  jal         func_43E6B8
label_1c8c0c:
    if (ctx->pc == 0x1C8C0Cu) {
        ctx->pc = 0x1C8C0Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C08u;
        // 0x1c8c0c: 0xafa20008  sw          $v0, 0x8($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C10u;
        goto label_1c8c10;
    }
    ctx->pc = 0x1C8C08u;
    SET_GPR_U32(ctx, 31, 0x1C8C10u);
    ctx->pc = 0x1C8C0Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C08u;
    // 0x1c8c0c: 0xafa20008  sw          $v0, 0x8($sp) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E6B8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E6B8u, 0x1C8C08u, 0x1C8C10u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8C10u;
label_1c8c10:
    // 0x1c8c10: 0x8fa20024  lw          $v0, 0x24($sp)
    ctx->pc = 0x1c8c10u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 36)));
label_1c8c14:
    // 0x1c8c14: 0x21100  sll         $v0, $v0, 4
    ctx->pc = 0x1c8c14u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 4));
label_1c8c18:
    // 0x1c8c18: 0x501021  addu        $v0, $v0, $s0
    ctx->pc = 0x1c8c18u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 16)));
label_1c8c1c:
    // 0x1c8c1c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8c1cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8c20:
    // 0x1c8c20: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c8c20u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
label_1c8c24:
    // 0x1c8c24: 0xc10f9d2  jal         func_43E748
label_1c8c28:
    if (ctx->pc == 0x1C8C28u) {
        ctx->pc = 0x1C8C28u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C24u;
        // 0x1c8c28: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C2Cu;
        goto label_1c8c2c;
    }
    ctx->pc = 0x1C8C24u;
    SET_GPR_U32(ctx, 31, 0x1C8C2Cu);
    ctx->pc = 0x1C8C28u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C24u;
    // 0x1c8c28: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x1C8C24u, 0x1C8C2Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8C2Cu;
label_1c8c2c:
    // 0x1c8c2c: 0x8fa40020  lw          $a0, 0x20($sp)
    ctx->pc = 0x1c8c2cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
label_1c8c30:
    // 0x1c8c30: 0xc0c845c  jal         func_321170
label_1c8c34:
    if (ctx->pc == 0x1C8C34u) {
        ctx->pc = 0x1C8C34u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C30u;
        // 0x1c8c34: 0x24160001  addiu       $s6, $zero, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 22, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C38u;
        goto label_1c8c38;
    }
    ctx->pc = 0x1C8C30u;
    SET_GPR_U32(ctx, 31, 0x1C8C38u);
    ctx->pc = 0x1C8C34u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C30u;
    // 0x1c8c34: 0x24160001  addiu       $s6, $zero, 0x1 (Delay Slot)
    SET_GPR_S32(ctx, 22, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321170u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321170u, 0x1C8C30u, 0x1C8C38u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8C38u;
label_1c8c38:
    // 0x1c8c38: 0x8fa40064  lw          $a0, 0x64($sp)
    ctx->pc = 0x1c8c38u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 100)));
label_1c8c3c:
    // 0x1c8c3c: 0x8c820004  lw          $v0, 0x4($a0)
    ctx->pc = 0x1c8c3cu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 4), 4))); // MMIO: 0x70000004
label_1c8c40:
    // 0x1c8c40: 0x8fa40060  lw          $a0, 0x60($sp)
    ctx->pc = 0x1c8c40u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 96)));
label_1c8c44:
    // 0x1c8c44: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x1c8c44u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8c48:
    // 0x1c8c48: 0x8c830008  lw          $v1, 0x8($a0)
    ctx->pc = 0x1c8c48u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));
label_1c8c4c:
    // 0x1c8c4c: 0x1460000d  bnez        $v1, . + 4 + (0xD << 2)
label_1c8c50:
    if (ctx->pc == 0x1C8C50u) {
        ctx->pc = 0x1C8C50u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C4Cu;
        // 0x1c8c50: 0xafa20000  sw          $v0, 0x0($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C54u;
        goto label_1c8c54;
    }
    ctx->pc = 0x1C8C4Cu;
    {
        const bool branch_taken_0x1c8c4c = (GPR_U64(ctx, 3) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8C50u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C4Cu;
        // 0x1c8c50: 0xafa20000  sw          $v0, 0x0($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8c4c) {
            ctx->pc = 0x1C8C84u;
            goto label_1c8c84;
        }
    }
    ctx->pc = 0x1C8C54u;
label_1c8c54:
    // 0x1c8c54: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c8c54u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
label_1c8c58:
    // 0x1c8c58: 0x3c030049  lui         $v1, 0x49
    ctx->pc = 0x1c8c58u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)73 << 16));
label_1c8c5c:
    // 0x1c8c5c: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c8c5cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20416)));
label_1c8c60:
    // 0x1c8c60: 0x2465e140  addiu       $a1, $v1, -0x1EC0
    ctx->pc = 0x1c8c60u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 4294959424));
label_1c8c64:
    // 0x1c8c64: 0x3c020049  lui         $v0, 0x49
    ctx->pc = 0x1c8c64u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)73 << 16));
label_1c8c68:
    // 0x1c8c68: 0x24060063  addiu       $a2, $zero, 0x63
    ctx->pc = 0x1c8c68u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 99));
label_1c8c6c:
    // 0x1c8c6c: 0xc0da03a  jal         func_3680E8
label_1c8c70:
    if (ctx->pc == 0x1C8C70u) {
        ctx->pc = 0x1C8C70u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C6Cu;
        // 0x1c8c70: 0x2447e168  addiu       $a3, $v0, -0x1E98 (Delay Slot)
        SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959464));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C74u;
        goto label_1c8c74;
    }
    ctx->pc = 0x1C8C6Cu;
    SET_GPR_U32(ctx, 31, 0x1C8C74u);
    ctx->pc = 0x1C8C70u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C6Cu;
    // 0x1c8c70: 0x2447e168  addiu       $a3, $v0, -0x1E98 (Delay Slot)
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 2), 4294959464));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C8C6Cu, 0x1C8C74u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8C74u;
label_1c8c74:
    // 0x1c8c74: 0x3c030052  lui         $v1, 0x52
    ctx->pc = 0x1c8c74u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)82 << 16));
label_1c8c78:
    // 0x1c8c78: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c8c78u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8c7c:
    // 0x1c8c7c: 0xc0baf6a  jal         func_2EBDA8
label_1c8c80:
    if (ctx->pc == 0x1C8C80u) {
        ctx->pc = 0x1C8C80u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C7Cu;
        // 0x1c8c80: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8C84u;
        goto label_1c8c84;
    }
    ctx->pc = 0x1C8C7Cu;
    SET_GPR_U32(ctx, 31, 0x1C8C84u);
    ctx->pc = 0x1C8C80u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C7Cu;
    // 0x1c8c80: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C8C7Cu, 0x1C8C84u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8C84u;
label_1c8c84:
    // 0x1c8c84: 0x8fa40060  lw          $a0, 0x60($sp)
    ctx->pc = 0x1c8c84u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 96)));
label_1c8c88:
    // 0x1c8c88: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c8c88u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
label_1c8c8c:
    // 0x1c8c8c: 0x8c830008  lw          $v1, 0x8($a0)
    ctx->pc = 0x1c8c8cu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));
label_1c8c90:
    // 0x1c8c90: 0x8c444c60  lw          $a0, 0x4C60($v0)
    ctx->pc = 0x1c8c90u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 19552)));
label_1c8c94:
    // 0x1c8c94: 0x8c6200e4  lw          $v0, 0xE4($v1)
    ctx->pc = 0x1c8c94u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 228)));
label_1c8c98:
    // 0x1c8c98: 0x38420008  xori        $v0, $v0, 0x8
    ctx->pc = 0x1c8c98u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) ^ (uint64_t)(uint16_t)8);
label_1c8c9c:
    // 0x1c8c9c: 0xc10fdda  jal         func_43F768
label_1c8ca0:
    if (ctx->pc == 0x1C8CA0u) {
        ctx->pc = 0x1C8CA0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8C9Cu;
        // 0x1c8ca0: 0x2c570001  sltiu       $s7, $v0, 0x1 (Delay Slot)
        SET_GPR_U64(ctx, 23, ((uint64_t)GPR_U64(ctx, 2) < (uint64_t)(int64_t)(int32_t)1) ? 1 : 0);
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8CA4u;
        goto label_1c8ca4;
    }
    ctx->pc = 0x1C8C9Cu;
    SET_GPR_U32(ctx, 31, 0x1C8CA4u);
    ctx->pc = 0x1C8CA0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8C9Cu;
    // 0x1c8ca0: 0x2c570001  sltiu       $s7, $v0, 0x1 (Delay Slot)
    SET_GPR_U64(ctx, 23, ((uint64_t)GPR_U64(ctx, 2) < (uint64_t)(int64_t)(int32_t)1) ? 1 : 0);
    ctx->in_delay_slot = false;
    ctx->pc = 0x43F768u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43F768u, 0x1C8C9Cu, 0x1C8CA4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8CA4u;
label_1c8ca4:
    // 0x1c8ca4: 0x8fa40028  lw          $a0, 0x28($sp)
    ctx->pc = 0x1c8ca4u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 40)));
label_1c8ca8:
    // 0x1c8ca8: 0x40882d  daddu       $s1, $v0, $zero
    ctx->pc = 0x1c8ca8u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    ghpcBillboardSubmit(rdram, ctx, runtime, GPR_U32(ctx, 17));
label_1c8cac:
    // 0x1c8cac: 0x24030149  addiu       $v1, $zero, 0x149
    ctx->pc = 0x1c8cacu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 329));
label_1c8cb0:
    // 0x1c8cb0: 0x240200fe  addiu       $v0, $zero, 0xFE
    ctx->pc = 0x1c8cb0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 254));
label_1c8cb4:
    // 0x1c8cb4: 0x641823  subu        $v1, $v1, $a0
    ctx->pc = 0x1c8cb4u;
    SET_GPR_S32(ctx, 3, (int32_t)SUB32(GPR_U32(ctx, 3), GPR_U32(ctx, 4)));
label_1c8cb8:
    // 0x1c8cb8: 0xafa2003c  sw          $v0, 0x3C($sp)
    ctx->pc = 0x1c8cb8u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 60), GPR_U32(ctx, 2));
label_1c8cbc:
    // 0x1c8cbc: 0x286400fe  slti        $a0, $v1, 0xFE
    ctx->pc = 0x1c8cbcu;
    SET_GPR_U64(ctx, 4, ((int64_t)GPR_S64(ctx, 3) < (int64_t)(int32_t)254) ? 1 : 0);
label_1c8cc0:
    // 0x1c8cc0: 0xafa30040  sw          $v1, 0x40($sp)
    ctx->pc = 0x1c8cc0u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 64), GPR_U32(ctx, 3));
label_1c8cc4:
    // 0x1c8cc4: 0x8fa3005c  lw          $v1, 0x5C($sp)
    ctx->pc = 0x1c8cc4u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 92)));
label_1c8cc8:
    // 0x1c8cc8: 0x8fa20078  lw          $v0, 0x78($sp)
    ctx->pc = 0x1c8cc8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 120)));
label_1c8ccc:
    // 0x1c8ccc: 0x64100a  movz        $v0, $v1, $a0
    ctx->pc = 0x1c8cccu;
    if (GPR_U64(ctx, 4) == 0) SET_GPR_VEC(ctx, 2, GPR_VEC(ctx, 3));
label_1c8cd0:
    // 0x1c8cd0: 0x8c430000  lw          $v1, 0x0($v0)
    ctx->pc = 0x1c8cd0u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 0)));
label_1c8cd4:
    // 0x1c8cd4: 0x31883  sra         $v1, $v1, 2
    ctx->pc = 0x1c8cd4u;
    SET_GPR_S32(ctx, 3, SRA32(GPR_S32(ctx, 3), 2));
label_1c8cd8:
    // 0x1c8cd8: 0x28620002  slti        $v0, $v1, 0x2
    ctx->pc = 0x1c8cd8u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 3) < (int64_t)(int32_t)2) ? 1 : 0);
label_1c8cdc:
    // 0x1c8cdc: 0x1040000d  beqz        $v0, . + 4 + (0xD << 2)
label_1c8ce0:
    if (ctx->pc == 0x1C8CE0u) {
        ctx->pc = 0x1C8CE0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8CDCu;
        // 0x1c8ce0: 0xafa30048  sw          $v1, 0x48($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 72), GPR_U32(ctx, 3));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8CE4u;
        goto label_1c8ce4;
    }
    ctx->pc = 0x1C8CDCu;
    {
        const bool branch_taken_0x1c8cdc = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8CE0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8CDCu;
        // 0x1c8ce0: 0xafa30048  sw          $v1, 0x48($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 72), GPR_U32(ctx, 3));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8cdc) {
            ctx->pc = 0x1C8D14u;
            goto label_1c8d14;
        }
    }
    ctx->pc = 0x1C8CE4u;
label_1c8ce4:
    // 0x1c8ce4: 0x3c020044  lui         $v0, 0x44
    ctx->pc = 0x1c8ce4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)68 << 16));
label_1c8ce8:
    // 0x1c8ce8: 0x3c050049  lui         $a1, 0x49
    ctx->pc = 0x1c8ce8u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)73 << 16));
label_1c8cec:
    // 0x1c8cec: 0x8c444fc0  lw          $a0, 0x4FC0($v0)
    ctx->pc = 0x1c8cecu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 20416)));
label_1c8cf0:
    // 0x1c8cf0: 0x3c070049  lui         $a3, 0x49
    ctx->pc = 0x1c8cf0u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)73 << 16));
label_1c8cf4:
    // 0x1c8cf4: 0x24a5e288  addiu       $a1, $a1, -0x1D78
    ctx->pc = 0x1c8cf4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294959752));
label_1c8cf8:
    // 0x1c8cf8: 0x24e7e298  addiu       $a3, $a3, -0x1D68
    ctx->pc = 0x1c8cf8u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294959768));
label_1c8cfc:
    // 0x1c8cfc: 0xc0da03a  jal         func_3680E8
label_1c8d00:
    if (ctx->pc == 0x1C8D00u) {
        ctx->pc = 0x1C8D00u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8CFCu;
        // 0x1c8d00: 0x2406006a  addiu       $a2, $zero, 0x6A (Delay Slot)
        SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 106));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8D04u;
        goto label_1c8d04;
    }
    ctx->pc = 0x1C8CFCu;
    SET_GPR_U32(ctx, 31, 0x1C8D04u);
    ctx->pc = 0x1C8D00u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8CFCu;
    // 0x1c8d00: 0x2406006a  addiu       $a2, $zero, 0x6A (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 106));
    ctx->in_delay_slot = false;
    ctx->pc = 0x3680E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x3680E8u, 0x1C8CFCu, 0x1C8D04u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8D04u;
label_1c8d04:
    // 0x1c8d04: 0x3c030052  lui         $v1, 0x52
    ctx->pc = 0x1c8d04u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)82 << 16));
label_1c8d08:
    // 0x1c8d08: 0x40282d  daddu       $a1, $v0, $zero
    ctx->pc = 0x1c8d08u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8d0c:
    // 0x1c8d0c: 0xc0baf6a  jal         func_2EBDA8
label_1c8d10:
    if (ctx->pc == 0x1C8D10u) {
        ctx->pc = 0x1C8D10u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8D0Cu;
        // 0x1c8d10: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8D14u;
        goto label_1c8d14;
    }
    ctx->pc = 0x1C8D0Cu;
    SET_GPR_U32(ctx, 31, 0x1C8D14u);
    ctx->pc = 0x1C8D10u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8D0Cu;
    // 0x1c8d10: 0x2464d0e0  addiu       $a0, $v1, -0x2F20 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 4294955232));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2EBDA8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2EBDA8u, 0x1C8D0Cu, 0x1C8D14u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8D14u;
label_1c8d14:
    // 0x1c8d14: 0x3c121400  lui         $s2, 0x1400
    ctx->pc = 0x1c8d14u;
    SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)5120 << 16));
label_1c8d18:
    // 0x1c8d18: 0x8fb4007c  lw          $s4, 0x7C($sp)
    ctx->pc = 0x1c8d18u;
    SET_GPR_S32(ctx, 20, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 124)));
label_1c8d1c:
    // 0x1c8d1c: 0x8fb30080  lw          $s3, 0x80($sp)
    ctx->pc = 0x1c8d1cu;
    SET_GPR_S32(ctx, 19, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 128)));
label_1c8d20:
    // 0x1c8d20: 0x36520616  ori         $s2, $s2, 0x616
    ctx->pc = 0x1c8d20u;
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 18) | (uint64_t)(uint16_t)1558);
label_1c8d24:
    // 0x1c8d24: 0x3c1e1700  lui         $fp, 0x1700
    ctx->pc = 0x1c8d24u;
    SET_GPR_S32(ctx, 30, (int32_t)((uint32_t)5888 << 16));
label_1c8d28:
    // 0x1c8d28: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x1c8d28u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
label_1c8d2c:
    // 0x1c8d2c: 0x0  nop
    ctx->pc = 0x1c8d2cu;
    // NOP
label_1c8d30:
    // 0x1c8d30: 0xc06fc08  jal         func_1BF020
label_1c8d34:
    if (ctx->pc == 0x1C8D34u) {
        ctx->pc = 0x1C8D34u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8D30u;
        // 0x1c8d34: 0x24442780  addiu       $a0, $v0, 0x2780 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 10112));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8D38u;
        goto label_1c8d38;
    }
    ctx->pc = 0x1C8D30u;
    SET_GPR_U32(ctx, 31, 0x1C8D38u);
    ctx->pc = 0x1C8D34u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8D30u;
    // 0x1c8d34: 0x24442780  addiu       $a0, $v0, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 2), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BF020u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BF020u, 0x1C8D30u, 0x1C8D38u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8D38u;
label_1c8d38:
    // 0x1c8d38: 0x8fa40048  lw          $a0, 0x48($sp)
    ctx->pc = 0x1c8d38u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 72)));
label_1c8d3c:
    // 0x1c8d3c: 0x2442fffd  addiu       $v0, $v0, -0x3
    ctx->pc = 0x1c8d3cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294967293));
label_1c8d40:
    // 0x1c8d40: 0x8fa30084  lw          $v1, 0x84($sp)
    ctx->pc = 0x1c8d40u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 132)));
label_1c8d44:
    // 0x1c8d44: 0x21083  sra         $v0, $v0, 2
    ctx->pc = 0x1c8d44u;
    SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 2), 2));
label_1c8d48:
    // 0x1c8d48: 0xafa2004c  sw          $v0, 0x4C($sp)
    ctx->pc = 0x1c8d48u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 76), GPR_U32(ctx, 2));
label_1c8d4c:
    // 0x1c8d4c: 0x44202a  slt         $a0, $v0, $a0
    ctx->pc = 0x1c8d4cu;
    SET_GPR_U64(ctx, 4, ((int64_t)GPR_S64(ctx, 2) < (int64_t)GPR_S64(ctx, 4)) ? 1 : 0);
label_1c8d50:
    // 0x1c8d50: 0x264180a  movz        $v1, $s3, $a0
    ctx->pc = 0x1c8d50u;
    if (GPR_U64(ctx, 4) == 0) SET_GPR_VEC(ctx, 3, GPR_VEC(ctx, 19));
label_1c8d54:
    // 0x1c8d54: 0x8fa50044  lw          $a1, 0x44($sp)
    ctx->pc = 0x1c8d54u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 68)));
label_1c8d58:
    // 0x1c8d58: 0x8c620000  lw          $v0, 0x0($v1)
    ctx->pc = 0x1c8d58u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
label_1c8d5c:
    // 0x1c8d5c: 0x3c076c00  lui         $a3, 0x6C00
    ctx->pc = 0x1c8d5cu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)27648 << 16));
label_1c8d60:
    // 0x1c8d60: 0x8fa60028  lw          $a2, 0x28($sp)
    ctx->pc = 0x1c8d60u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 40)));
label_1c8d64:
    // 0x1c8d64: 0x45102a  slt         $v0, $v0, $a1
    ctx->pc = 0x1c8d64u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 2) < (int64_t)GPR_S64(ctx, 5)) ? 1 : 0);
label_1c8d68:
    // 0x1c8d68: 0x282180a  movz        $v1, $s4, $v0
    ctx->pc = 0x1c8d68u;
    if (GPR_U64(ctx, 2) == 0) SET_GPR_VEC(ctx, 3, GPR_VEC(ctx, 20));
label_1c8d6c:
    // 0x1c8d6c: 0x8c680000  lw          $t0, 0x0($v1)
    ctx->pc = 0x1c8d6cu;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
label_1c8d70:
    // 0x1c8d70: 0xa82823  subu        $a1, $a1, $t0
    ctx->pc = 0x1c8d70u;
    SET_GPR_S32(ctx, 5, (int32_t)SUB32(GPR_U32(ctx, 5), GPR_U32(ctx, 8)));
label_1c8d74:
    // 0x1c8d74: 0x81080  sll         $v0, $t0, 2
    ctx->pc = 0x1c8d74u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 8), 2));
label_1c8d78:
    // 0x1c8d78: 0xafa50044  sw          $a1, 0x44($sp)
    ctx->pc = 0x1c8d78u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 68), GPR_U32(ctx, 5));
label_1c8d7c:
    // 0x1c8d7c: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x1c8d7cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
label_1c8d80:
    // 0x1c8d80: 0x21400  sll         $v0, $v0, 16
    ctx->pc = 0x1c8d80u;
    SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 2), 16));
label_1c8d84:
    // 0x1c8d84: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c8d84u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
label_1c8d88:
    // 0x1c8d88: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c8d88u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
label_1c8d8c:
    // 0x1c8d8c: 0x34428000  ori         $v0, $v0, 0x8000
    ctx->pc = 0x1c8d8cu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)32768);
label_1c8d90:
    // 0x1c8d90: 0xc23025  or          $a2, $a2, $v0
    ctx->pc = 0x1c8d90u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | GPR_U64(ctx, 2));
label_1c8d94:
    // 0x1c8d94: 0x60202d  daddu       $a0, $v1, $zero
    ctx->pc = 0x1c8d94u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 3) + (uint64_t)GPR_U64(ctx, 0));
label_1c8d98:
    // 0x1c8d98: 0xc73025  or          $a2, $a2, $a3
    ctx->pc = 0x1c8d98u;
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) | GPR_U64(ctx, 7));
label_1c8d9c:
    // 0x1c8d9c: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c8d9cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8da0:
    // 0x1c8da0: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8da0u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8da4:
    // 0x1c8da4: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c8da4u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_1c8da8:
    // 0x1c8da8: 0xac86000c  sw          $a2, 0xC($a0)
    ctx->pc = 0x1c8da8u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 12), GPR_U32(ctx, 6));
label_1c8dac:
    // 0x1c8dac: 0xac800000  sw          $zero, 0x0($a0)
    ctx->pc = 0x1c8dacu;
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 0));
label_1c8db0:
    // 0x1c8db0: 0xac800004  sw          $zero, 0x4($a0)
    ctx->pc = 0x1c8db0u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 4), GPR_U32(ctx, 0));
label_1c8db4:
    // 0x1c8db4: 0xac800008  sw          $zero, 0x8($a0)
    ctx->pc = 0x1c8db4u;
    WRITE32(ADD32(GPR_U32(ctx, 4), 8), GPR_U32(ctx, 0));
label_1c8db8:
    // 0x1c8db8: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8db8u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
label_1c8dbc:
    // 0x1c8dbc: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c8dbcu;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
label_1c8dc0:
    // 0x1c8dc0: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c8dc0u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8dc4:
    // 0x1c8dc4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c8dc4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
label_1c8dc8:
    // 0x1c8dc8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8dc8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8dcc:
    // 0x1c8dcc: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c8dccu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
label_1c8dd0:
    // 0x1c8dd0: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c8dd0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
label_1c8dd4:
    // 0x1c8dd4: 0xac680000  sw          $t0, 0x0($v1)
    ctx->pc = 0x1c8dd4u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 8));
label_1c8dd8:
    // 0x1c8dd8: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c8dd8u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
label_1c8ddc:
    // 0x1c8ddc: 0x12e0001f  beqz        $s7, . + 4 + (0x1F << 2)
label_1c8de0:
    if (ctx->pc == 0x1C8DE0u) {
        ctx->pc = 0x1C8DE0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8DDCu;
        // 0x1c8de0: 0xac600008  sw          $zero, 0x8($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8DE4u;
        goto label_1c8de4;
    }
    ctx->pc = 0x1C8DDCu;
    {
        const bool branch_taken_0x1c8ddc = (GPR_U64(ctx, 23) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8DE0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8DDCu;
        // 0x1c8de0: 0xac600008  sw          $zero, 0x8($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8ddc) {
            ctx->pc = 0x1C8E5Cu;
            goto label_1c8e5c;
        }
    }
    ctx->pc = 0x1C8DE4u;
label_1c8de4:
    // 0x1c8de4: 0x11000039  beqz        $t0, . + 4 + (0x39 << 2)
label_1c8de8:
    if (ctx->pc == 0x1C8DE8u) {
        ctx->pc = 0x1C8DECu;
        goto label_1c8dec;
    }
    ctx->pc = 0x1C8DE4u;
    {
        const bool branch_taken_0x1c8de4 = (GPR_U64(ctx, 8) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8de4) {
            ctx->pc = 0x1C8ECCu;
            goto label_1c8ecc;
        }
    }
    ctx->pc = 0x1C8DECu;
label_1c8dec:
    // 0x1c8dec: 0x0  nop
    ctx->pc = 0x1c8decu;
    // NOP
label_1c8df0:
    // 0x1c8df0: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c8df0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
label_1c8df4:
    // 0x1c8df4: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c8df4u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
label_1c8df8:
    // 0x1c8df8: 0x2508ffff  addiu       $t0, $t0, -0x1
    ctx->pc = 0x1c8df8u;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 8), 4294967295));
label_1c8dfc:
    // 0x1c8dfc: 0x7a220000  lq          $v0, 0x0($s1)
    ctx->pc = 0x1c8dfcu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 0)));
label_1c8e00:
    // 0x1c8e00: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c8e00u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
label_1c8e04:
    // 0x1c8e04: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c8e04u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8e08:
    // 0x1c8e08: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e08u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8e0c:
    // 0x1c8e0c: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c8e0cu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_1c8e10:
    // 0x1c8e10: 0x24640010  addiu       $a0, $v1, 0x10
    ctx->pc = 0x1c8e10u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8e14:
    // 0x1c8e14: 0x24650020  addiu       $a1, $v1, 0x20
    ctx->pc = 0x1c8e14u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 32));
label_1c8e18:
    // 0x1c8e18: 0x24660030  addiu       $a2, $v1, 0x30
    ctx->pc = 0x1c8e18u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 3), 48));
label_1c8e1c:
    // 0x1c8e1c: 0x7a220010  lq          $v0, 0x10($s1)
    ctx->pc = 0x1c8e1cu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 16)));
label_1c8e20:
    // 0x1c8e20: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c8e20u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
label_1c8e24:
    // 0x1c8e24: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e24u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8e28:
    // 0x1c8e28: 0xac240008  sw          $a0, 0x8($at)
    ctx->pc = 0x1c8e28u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 4)); // MMIO: 0x70000008
label_1c8e2c:
    // 0x1c8e2c: 0x7a220020  lq          $v0, 0x20($s1)
    ctx->pc = 0x1c8e2cu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 17), 32)));
label_1c8e30:
    // 0x1c8e30: 0x7c620010  sq          $v0, 0x10($v1)
    ctx->pc = 0x1c8e30u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 16), GPR_VEC(ctx, 2));
label_1c8e34:
    // 0x1c8e34: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e34u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8e38:
    // 0x1c8e38: 0xac250008  sw          $a1, 0x8($at)
    ctx->pc = 0x1c8e38u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 5)); // MMIO: 0x70000008
label_1c8e3c:
    // 0x1c8e3c: 0x7a020040  lq          $v0, 0x40($s0)
    ctx->pc = 0x1c8e3cu;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 64)));
label_1c8e40:
    // 0x1c8e40: 0x7c620020  sq          $v0, 0x20($v1)
    ctx->pc = 0x1c8e40u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 32), GPR_VEC(ctx, 2));
label_1c8e44:
    // 0x1c8e44: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e44u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8e48:
    // 0x1c8e48: 0xac260008  sw          $a2, 0x8($at)
    ctx->pc = 0x1c8e48u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 6)); // MMIO: 0x70000008
label_1c8e4c:
    // 0x1c8e4c: 0x1500ffe8  bnez        $t0, . + 4 + (-0x18 << 2)
label_1c8e50:
    if (ctx->pc == 0x1C8E50u) {
        ctx->pc = 0x1C8E50u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8E4Cu;
        // 0x1c8e50: 0x8e100000  lw          $s0, 0x0($s0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8E54u;
        goto label_1c8e54;
    }
    ctx->pc = 0x1C8E4Cu;
    {
        const bool branch_taken_0x1c8e4c = (GPR_U64(ctx, 8) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8E50u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8E4Cu;
        // 0x1c8e50: 0x8e100000  lw          $s0, 0x0($s0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8e4c) {
            ctx->pc = 0x1C8DF0u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c8df0;
        }
    }
    ctx->pc = 0x1C8E54u;
label_1c8e54:
    // 0x1c8e54: 0x1000001d  b           . + 4 + (0x1D << 2)
label_1c8e58:
    if (ctx->pc == 0x1C8E58u) {
        ctx->pc = 0x1C8E5Cu;
        goto label_1c8e5c;
    }
    ctx->pc = 0x1C8E54u;
    {
        const bool branch_taken_0x1c8e54 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8e54) {
            ctx->pc = 0x1C8ECCu;
            goto label_1c8ecc;
        }
    }
    ctx->pc = 0x1C8E5Cu;
label_1c8e5c:
    // 0x1c8e5c: 0x1100001b  beqz        $t0, . + 4 + (0x1B << 2)
label_1c8e60:
    if (ctx->pc == 0x1C8E60u) {
        ctx->pc = 0x1C8E64u;
        goto label_1c8e64;
    }
    ctx->pc = 0x1C8E5Cu;
    {
        const bool branch_taken_0x1c8e5c = (GPR_U64(ctx, 8) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8e5c) {
            ctx->pc = 0x1C8ECCu;
            goto label_1c8ecc;
        }
    }
    ctx->pc = 0x1C8E64u;
label_1c8e64:
    // 0x1c8e64: 0x0  nop
    ctx->pc = 0x1c8e64u;
    // NOP
label_1c8e68:
    // 0x1c8e68: 0x7a020010  lq          $v0, 0x10($s0)
    ctx->pc = 0x1c8e68u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 16)));
label_1c8e6c:
    // 0x1c8e6c: 0x2508ffff  addiu       $t0, $t0, -0x1
    ctx->pc = 0x1c8e6cu;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 8), 4294967295));
label_1c8e70:
    // 0x1c8e70: 0x3c037000  lui         $v1, 0x7000
    ctx->pc = 0x1c8e70u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)28672 << 16));
label_1c8e74:
    // 0x1c8e74: 0x8c630008  lw          $v1, 0x8($v1)
    ctx->pc = 0x1c8e74u;
    SET_GPR_S32(ctx, 3, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 3), 8))); // MMIO: 0x70000008
label_1c8e78:
    // 0x1c8e78: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c8e78u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
label_1c8e7c:
    // 0x1c8e7c: 0x24630010  addiu       $v1, $v1, 0x10
    ctx->pc = 0x1c8e7cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8e80:
    // 0x1c8e80: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e80u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8e84:
    // 0x1c8e84: 0xac230008  sw          $v1, 0x8($at)
    ctx->pc = 0x1c8e84u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 3)); // MMIO: 0x70000008
label_1c8e88:
    // 0x1c8e88: 0x24640010  addiu       $a0, $v1, 0x10
    ctx->pc = 0x1c8e88u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 16));
label_1c8e8c:
    // 0x1c8e8c: 0x24650020  addiu       $a1, $v1, 0x20
    ctx->pc = 0x1c8e8cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 3), 32));
label_1c8e90:
    // 0x1c8e90: 0x24660030  addiu       $a2, $v1, 0x30
    ctx->pc = 0x1c8e90u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 3), 48));
label_1c8e94:
    // 0x1c8e94: 0x7a020020  lq          $v0, 0x20($s0)
    ctx->pc = 0x1c8e94u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 32)));
label_1c8e98:
    // 0x1c8e98: 0x7c620000  sq          $v0, 0x0($v1)
    ctx->pc = 0x1c8e98u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 0), GPR_VEC(ctx, 2));
label_1c8e9c:
    // 0x1c8e9c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8e9cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8ea0:
    // 0x1c8ea0: 0xac240008  sw          $a0, 0x8($at)
    ctx->pc = 0x1c8ea0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 4)); // MMIO: 0x70000008
label_1c8ea4:
    // 0x1c8ea4: 0x7a020030  lq          $v0, 0x30($s0)
    ctx->pc = 0x1c8ea4u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 48)));
label_1c8ea8:
    // 0x1c8ea8: 0x7c620010  sq          $v0, 0x10($v1)
    ctx->pc = 0x1c8ea8u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 16), GPR_VEC(ctx, 2));
label_1c8eac:
    // 0x1c8eac: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8eacu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8eb0:
    // 0x1c8eb0: 0xac250008  sw          $a1, 0x8($at)
    ctx->pc = 0x1c8eb0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 5)); // MMIO: 0x70000008
label_1c8eb4:
    // 0x1c8eb4: 0x7a020040  lq          $v0, 0x40($s0)
    ctx->pc = 0x1c8eb4u;
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 16), 64)));
label_1c8eb8:
    // 0x1c8eb8: 0x7c620020  sq          $v0, 0x20($v1)
    ctx->pc = 0x1c8eb8u;
    WRITE128(ADD32(GPR_U32(ctx, 3), 32), GPR_VEC(ctx, 2));
label_1c8ebc:
    // 0x1c8ebc: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8ebcu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8ec0:
    // 0x1c8ec0: 0xac260008  sw          $a2, 0x8($at)
    ctx->pc = 0x1c8ec0u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 6)); // MMIO: 0x70000008
label_1c8ec4:
    // 0x1c8ec4: 0x1500ffe8  bnez        $t0, . + 4 + (-0x18 << 2)
label_1c8ec8:
    if (ctx->pc == 0x1C8EC8u) {
        ctx->pc = 0x1C8EC8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8EC4u;
        // 0x1c8ec8: 0x8e100000  lw          $s0, 0x0($s0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8ECCu;
        goto label_1c8ecc;
    }
    ctx->pc = 0x1C8EC4u;
    {
        const bool branch_taken_0x1c8ec4 = (GPR_U64(ctx, 8) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8EC8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8EC4u;
        // 0x1c8ec8: 0x8e100000  lw          $s0, 0x0($s0) (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8ec4) {
            ctx->pc = 0x1C8E68u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c8e68;
        }
    }
    ctx->pc = 0x1C8ECCu;
label_1c8ecc:
    // 0x1c8ecc: 0x12c0000b  beqz        $s6, . + 4 + (0xB << 2)
label_1c8ed0:
    if (ctx->pc == 0x1C8ED0u) {
        ctx->pc = 0x1C8ED4u;
        goto label_1c8ed4;
    }
    ctx->pc = 0x1C8ECCu;
    {
        const bool branch_taken_0x1c8ecc = (GPR_U64(ctx, 22) == GPR_U64(ctx, 0));
        if (branch_taken_0x1c8ecc) {
            ctx->pc = 0x1C8EFCu;
            goto label_1c8efc;
        }
    }
    ctx->pc = 0x1C8ED4u;
label_1c8ed4:
    // 0x1c8ed4: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8ed4u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
label_1c8ed8:
    // 0x1c8ed8: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c8ed8u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
label_1c8edc:
    // 0x1c8edc: 0xb02d  daddu       $s6, $zero, $zero
    ctx->pc = 0x1c8edcu;
    SET_GPR_U64(ctx, 22, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1c8ee0:
    // 0x1c8ee0: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c8ee0u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8ee4:
    // 0x1c8ee4: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c8ee4u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
label_1c8ee8:
    // 0x1c8ee8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8ee8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8eec:
    // 0x1c8eec: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c8eecu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
label_1c8ef0:
    // 0x1c8ef0: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c8ef0u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
label_1c8ef4:
    // 0x1c8ef4: 0x10000009  b           . + 4 + (0x9 << 2)
label_1c8ef8:
    if (ctx->pc == 0x1C8EF8u) {
        ctx->pc = 0x1C8EF8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8EF4u;
        // 0x1c8ef8: 0xac720000  sw          $s2, 0x0($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 18));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8EFCu;
        goto label_1c8efc;
    }
    ctx->pc = 0x1C8EF4u;
    {
        const bool branch_taken_0x1c8ef4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1C8EF8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8EF4u;
        // 0x1c8ef8: 0xac720000  sw          $s2, 0x0($v1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 18));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8ef4) {
            ctx->pc = 0x1C8F1Cu;
            goto label_1c8f1c;
        }
    }
    ctx->pc = 0x1C8EFCu;
label_1c8efc:
    // 0x1c8efc: 0x3c027000  lui         $v0, 0x7000
    ctx->pc = 0x1c8efcu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)28672 << 16));
label_1c8f00:
    // 0x1c8f00: 0x8c420008  lw          $v0, 0x8($v0)
    ctx->pc = 0x1c8f00u;
    SET_GPR_S32(ctx, 2, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 2), 8))); // MMIO: 0x70000008
label_1c8f04:
    // 0x1c8f04: 0x40182d  daddu       $v1, $v0, $zero
    ctx->pc = 0x1c8f04u;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1c8f08:
    // 0x1c8f08: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x1c8f08u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
label_1c8f0c:
    // 0x1c8f0c: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x1c8f0cu;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
label_1c8f10:
    // 0x1c8f10: 0xac220008  sw          $v0, 0x8($at)
    ctx->pc = 0x1c8f10u;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 2)); // MMIO: 0x70000008
label_1c8f14:
    // 0x1c8f14: 0xac60000c  sw          $zero, 0xC($v1)
    ctx->pc = 0x1c8f14u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 12), GPR_U32(ctx, 0));
label_1c8f18:
    // 0x1c8f18: 0xac7e0000  sw          $fp, 0x0($v1)
    ctx->pc = 0x1c8f18u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 30));
label_1c8f1c:
    // 0x1c8f1c: 0xac600004  sw          $zero, 0x4($v1)
    ctx->pc = 0x1c8f1cu;
    WRITE32(ADD32(GPR_U32(ctx, 3), 4), GPR_U32(ctx, 0));
label_1c8f20:
    // 0x1c8f20: 0xac600008  sw          $zero, 0x8($v1)
    ctx->pc = 0x1c8f20u;
    WRITE32(ADD32(GPR_U32(ctx, 3), 8), GPR_U32(ctx, 0));
label_1c8f24:
    // 0x1c8f24: 0x3c03004f  lui         $v1, 0x4F
    ctx->pc = 0x1c8f24u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)79 << 16));
label_1c8f28:
    // 0x1c8f28: 0xc10f900  jal         func_43E400
label_1c8f2c:
    if (ctx->pc == 0x1C8F2Cu) {
        ctx->pc = 0x1C8F2Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F28u;
        // 0x1c8f2c: 0x24642780  addiu       $a0, $v1, 0x2780 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 10112));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8F30u;
        goto label_1c8f30;
    }
    ctx->pc = 0x1C8F28u;
    SET_GPR_U32(ctx, 31, 0x1C8F30u);
    ctx->pc = 0x1C8F2Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1C8F28u;
    // 0x1c8f2c: 0x24642780  addiu       $a0, $v1, 0x2780 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 3), 10112));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E400u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E400u, 0x1C8F28u, 0x1C8F30u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1C8F30u;
label_1c8f30:
    // 0x1c8f30: 0x8fa20044  lw          $v0, 0x44($sp)
    ctx->pc = 0x1c8f30u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 68)));
label_1c8f34:
    // 0x1c8f34: 0x5440ff7e  bnel        $v0, $zero, . + 4 + (-0x82 << 2)
label_1c8f38:
    if (ctx->pc == 0x1C8F38u) {
        ctx->pc = 0x1C8F38u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F34u;
        // 0x1c8f38: 0x3c02004f  lui         $v0, 0x4F (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8F3Cu;
        goto label_1c8f3c;
    }
    ctx->pc = 0x1C8F34u;
    {
        const bool branch_taken_0x1c8f34 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1c8f34) {
            ctx->pc = 0x1C8F38u;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1C8F34u;
            // 0x1c8f38: 0x3c02004f  lui         $v0, 0x4F (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1C8D30u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c8d30;
        }
    }
    ctx->pc = 0x1C8F3Cu;
label_1c8f3c:
    // 0x1c8f3c: 0x16a0fe9a  bnez        $s5, . + 4 + (-0x166 << 2)
label_1c8f40:
    if (ctx->pc == 0x1C8F40u) {
        ctx->pc = 0x1C8F40u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F3Cu;
        // 0x1c8f40: 0x24020004  addiu       $v0, $zero, 0x4 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8F44u;
        goto label_1c8f44;
    }
    ctx->pc = 0x1C8F3Cu;
    {
        const bool branch_taken_0x1c8f3c = (GPR_U64(ctx, 21) != GPR_U64(ctx, 0));
        ctx->pc = 0x1C8F40u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F3Cu;
        // 0x1c8f40: 0x24020004  addiu       $v0, $zero, 0x4 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c8f3c) {
            ctx->pc = 0x1C89A8u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_1c89a8;
        }
    }
    ctx->pc = 0x1C8F44u;
label_1c8f44:
    // 0x1c8f44: 0x7bb00120  lq          $s0, 0x120($sp)
    ctx->pc = 0x1c8f44u;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 288)));
label_1c8f48:
    // 0x1c8f48: 0x7bb10110  lq          $s1, 0x110($sp)
    ctx->pc = 0x1c8f48u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 272)));
label_1c8f4c:
    // 0x1c8f4c: 0x7bb20100  lq          $s2, 0x100($sp)
    ctx->pc = 0x1c8f4cu;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 256)));
label_1c8f50:
    // 0x1c8f50: 0x7bb300f0  lq          $s3, 0xF0($sp)
    ctx->pc = 0x1c8f50u;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 240)));
label_1c8f54:
    // 0x1c8f54: 0x7bb400e0  lq          $s4, 0xE0($sp)
    ctx->pc = 0x1c8f54u;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 224)));
label_1c8f58:
    // 0x1c8f58: 0x7bb500d0  lq          $s5, 0xD0($sp)
    ctx->pc = 0x1c8f58u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 208)));
label_1c8f5c:
    // 0x1c8f5c: 0x7bb600c0  lq          $s6, 0xC0($sp)
    ctx->pc = 0x1c8f5cu;
    SET_GPR_VEC(ctx, 22, READ128(ADD32(GPR_U32(ctx, 29), 192)));
label_1c8f60:
    // 0x1c8f60: 0x7bb700b0  lq          $s7, 0xB0($sp)
    ctx->pc = 0x1c8f60u;
    SET_GPR_VEC(ctx, 23, READ128(ADD32(GPR_U32(ctx, 29), 176)));
label_1c8f64:
    // 0x1c8f64: 0x7bbe00a0  lq          $fp, 0xA0($sp)
    ctx->pc = 0x1c8f64u;
    SET_GPR_VEC(ctx, 30, READ128(ADD32(GPR_U32(ctx, 29), 160)));
label_1c8f68:
    // 0x1c8f68: 0xdfbf0090  ld          $ra, 0x90($sp)
    ctx->pc = 0x1c8f68u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 144)));
label_1c8f6c:
    // 0x1c8f6c: 0x3e00008  jr          $ra
label_1c8f70:
    if (ctx->pc == 0x1C8F70u) {
        ctx->pc = 0x1C8F70u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F6Cu;
        // 0x1c8f70: 0x27bd0130  addiu       $sp, $sp, 0x130 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 304));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C8F74u;
        goto label_fallthrough_0x1c8f6c;
    }
    ctx->pc = 0x1C8F6Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C8F70u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C8F6Cu;
        // 0x1c8f70: 0x27bd0130  addiu       $sp, $sp, 0x130 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 304));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C8F6Cu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
label_fallthrough_0x1c8f6c:
    ctx->pc = 0x1C8F74u;
}
