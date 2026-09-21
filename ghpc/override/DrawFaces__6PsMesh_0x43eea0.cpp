// Override of work/output/DrawFaces__6PsMesh_0x43eea0.cpp (PsMesh::DrawFaces,
// 0x43eea0 - 0x43f038).
//
// Why: the generated body is the PS2 geometry submit, and it never touches the
// engine-level mesh. It reads only +0x150 (mFacePacket handle, Lock/Unlock),
// +0x154 (packet quadword count), +0x158/+0x15a (two u16 stat counters added
// into 0x4F2CB8/0x4F2CBC) and +0x140 & 0x1f. The RndMesh subobject that a
// native backend would consume (verts, faces, material, world transform) is
// invisible to it. This override keeps the generated body verbatim and, when
// GHPC_MESH_LOG is set, prints that subobject on entry so a backend can be
// designed from the engine's data rather than from the VIF packet.
//
// Offsets, each pinned by a generated function (not by the decomp alone):
//   +0x100 Vert*   mVerts.mVerts   UpdateFacePacket 0x43ac34 `lw $t1, 0x100($s3)`
//   +0x104 int     mVerts.mSize    OnNumVerts 0x1f4cc8 `lw $v1, 0x104($a1)`
//   +0x108 Face*   mFaces._M_start OnNumFaces 0x1f4ce0 loads 0x108/0x10c and
//   +0x10c Face*   mFaces._M_finish  divides the difference by 6 (0xaaaaaaab, sra 1)
//   +0x120 RndMat* mMat.ptr        RndMesh::Mat 0x3c2090 `lw $v0, 0x120($a0)`
//   +0x138 RndMesh* mOwner.ptr     PsMesh::DrawShowing 0x43f07c `lw $s5, 0x138($s2)`,
//                                  and $s5 is the $a0 handed to this function at
//                                  0x43f388/0x43f434, so `this` here is the OWNER
//   +0x140 int                     read here, masked with 0x1f, selects the path
//   +0x150 MemHandle* mFacePacket  read here, Lock 0x321150 / Unlock 0x321170
//   +0x154 u16 packet quadwords    read here
//   +0x40  RndTransformable subobject: DrawShowing 0x43f2d0 `addiu $a0, $s2, 0x40`
//          then `jal WorldXfm 0x43f768`; WorldXfm returns $s1+0x60 and keeps its
//          dirty flag at $s1+0xa0. So the cached world Transform is at this+0xa0
//          (Matrix3 rows at +0xa0/+0xb0/+0xc0, translation at +0xd0) and the
//          dirty flag at this+0xe0. Read, never recomputed: calling WorldXfm
//          would clear the flag and change behaviour.
//   Vert stride 64: VertVector::resize 0x1f1d94 `sll $a0, $s1, 6`; layout
//   pos@0 norm@0x10 color@0x20 uv@0x30 is the decomp's (RndMesh.h:279), pinned
//   by which words resize's initialiser writes.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/gs/ghpc_native_draw.h"
#include "ghpc_scene.h"

#include <cstring>
#include <vector>

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// qw696..703, defined at global scope in Select__5PsCam_0x1c1770.cpp. That
// capture is ungated and runs once per camera select, so a global is the right
// shape for it. Declared out here rather than beside its use, because the use
// sits inside this file's anonymous namespace and an extern in there names a
// different symbol that nothing defines.
extern float g_ghpcCamQw[8][4];
extern uint64_t g_ghpcCamSelects;

// Host mesh cache, defined in Sync__6PsMeshi_0x43afa8.cpp (overlay.sh maps
// only .cpp files, so there is no header). Filled at PsMesh::Sync entry, the
// last moment the guest still holds Vert[]/Face[]; probed here by `this`,
// which is the owner mesh (see +0x138 above).
void ghpcMeshCacheStore(const uint8_t* rdram, uint32_t self, uint32_t vertPtr, uint32_t vertCnt, uint32_t faceBeg, uint32_t faceEnd);
bool ghpcMeshCacheLookup(uint32_t self, uint32_t* vertCnt, uint32_t* faceCnt, const uint8_t** verts, const uint8_t** faces);

// The camera the last PsCam::Select staged, from Select__5PsCam_0x1c1770.cpp.
extern float g_ghpcCamQw[8][4];
extern uint32_t g_ghpcCamThis;
extern uint64_t g_ghpcCamSelects;

// The calibration tag the GS frontend reads (gs_frontend.cpp). Set to the
// mesh about to be drawn, so every primitive the PS2 path submits for it is
// printed next to the object-space verts printed here. Everything downstream
// of the DMA kick runs synchronously on this thread, so the pairing is exact.
extern uint32_t g_ghpcTlCalMesh;

// GamePanel::StartGame has run (ps2_runtime.cpp:409). Same gate
// GHPC_FRAME_AFTER_START uses, so the calibration lands on gameplay.
extern std::atomic<bool> g_ghpcGameStarted;

// The world transform of the instance that is drawing, captured by the
// DrawShowing override right after WorldXfm returns. This is the matrix VU1
// gets (qw676..679); the owner's cached one at this+0xa0 is stale.
extern float g_ghpcInstWorld[4][4];
extern uint32_t g_ghpcInstThis;
extern uint32_t g_ghpcInstOwner;
// 0 unskinned (WorldXfm of the instance), 1 bone 0's matrix, 2 identity.
extern int32_t g_ghpcInstPath;
extern uint32_t g_ghpcInstBones;
extern float g_ghpcBoneXfm[4][4][4];

namespace {

// Hit/miss census for the cache. Prints under GHPC_MESH_LOG on the first 40
// calls and every 500th, and unconditionally whenever the miss count reaches
// a power of two, so a plain run still shows the miss rate in a few lines.
// The first 24 misses also get a `[ghpc/mesh/miss]` line under GHPC_MESH_LOG
// naming the mesh (owner, +0x140 flags, live vector sizes, packet size) so
// the misses can be classified: mutable geometry keeps verts > 0, an
// instance has owner != this, a pre-log Sync shows verts=0 with a packet.
void ghpcMeshCacheProbe(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
    static uint64_t s_calls = 0, s_hits = 0, s_misses = 0;
    uint32_t verts = 0, faces = 0;
    const bool hit = ghpcMeshCacheLookup(self, &verts, &faces, nullptr, nullptr);
    const uint64_t n = ++s_calls;
    bool print;
    if (hit) {
        ++s_hits;
        print = s_log && (n <= 40 || (n % 500) == 0);
    } else {
        ++s_misses;
        print = (s_log && (n <= 40 || (n % 500) == 0)) || (s_misses & (s_misses - 1)) == 0;
    }
    if (print) {
        std::fprintf(stderr, "[ghpc/mesh/cache] hits=%llu misses=%llu thisHit=%d verts=%u faces=%u\n",
            (unsigned long long)s_hits, (unsigned long long)s_misses, hit ? 1 : 0, verts, faces);
    }
    if (!hit && s_log && s_misses <= 24) {
        const int32_t  vertCnt  = (int32_t)READ32(self + 0x104u);
        const uint32_t faceBeg  = READ32(self + 0x108u);
        const uint32_t faceEnd  = READ32(self + 0x10cu);
        const uint32_t faceCnt  = (faceEnd >= faceBeg) ? (faceEnd - faceBeg) / 6u : 0u;
        std::fprintf(stderr, "[ghpc/mesh/miss] this=0x%08x owner=0x%08x flags140=0x%08x verts=%d faces=%u packetQw=%u\n",
            self, READ32(self + 0x138u), READ32(self + 0x140u), vertCnt, faceCnt, (unsigned)READ16(self + 0x154u));
    }
}

// Guest float through the same masked path the generated code uses.
inline float ghpcMeshF32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    const uint32_t bits = READ32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void ghpcMeshLog(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self, uint64_t n) {
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
        "[ghpc/mesh] #%llu this=0x%08x verts=%d faces=%u mat=0x%08x packetQw=%u packet=0x%08x owner=0x%08x flags140=0x%08x\n",
        (unsigned long long)n, self, vertCnt, faceCnt, mat, packetQw, packet, owner, flags140);

    const int nv = vertCnt < 4 ? (vertCnt < 0 ? 0 : vertCnt) : 4;
    for (int i = 0; i < nv && vertPtr != 0; ++i) {
        const uint32_t v = vertPtr + (uint32_t)i * 64u;
        std::fprintf(stderr,
            "[ghpc/mesh]   v%d pos=(%g %g %g) norm=(%g %g %g) uv=(%g %g) color=(%g %g %g %g)\n", i,
            ghpcMeshF32(rdram, ctx, runtime, v + 0x00u), ghpcMeshF32(rdram, ctx, runtime, v + 0x04u), ghpcMeshF32(rdram, ctx, runtime, v + 0x08u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x10u), ghpcMeshF32(rdram, ctx, runtime, v + 0x14u), ghpcMeshF32(rdram, ctx, runtime, v + 0x18u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x30u), ghpcMeshF32(rdram, ctx, runtime, v + 0x34u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x20u), ghpcMeshF32(rdram, ctx, runtime, v + 0x24u),
            ghpcMeshF32(rdram, ctx, runtime, v + 0x28u), ghpcMeshF32(rdram, ctx, runtime, v + 0x2cu));
    }

    const uint32_t nf = faceCnt < 4u ? faceCnt : 4u;
    for (uint32_t i = 0; i < nf && faceBeg != 0; ++i) {
        const uint32_t f = faceBeg + i * 6u;
        std::fprintf(stderr, "[ghpc/mesh]   f%u %u %u %u\n", i,
            (unsigned)READ16(f + 0u), (unsigned)READ16(f + 2u), (unsigned)READ16(f + 4u));
    }

    // Cached RndTransformable::mWorldXfm at this+0x40+0x60. dirty is the word
    // WorldXfm tests before recomputing; nonzero means this matrix is stale.
    const uint32_t w = self + 0xa0u;
    std::fprintf(stderr,
        "[ghpc/mesh]   world dirty=%u x=(%g %g %g) y=(%g %g %g) z=(%g %g %g) v=(%g %g %g)\n",
        READ32(self + 0xe0u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x00u), ghpcMeshF32(rdram, ctx, runtime, w + 0x04u), ghpcMeshF32(rdram, ctx, runtime, w + 0x08u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x10u), ghpcMeshF32(rdram, ctx, runtime, w + 0x14u), ghpcMeshF32(rdram, ctx, runtime, w + 0x18u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x20u), ghpcMeshF32(rdram, ctx, runtime, w + 0x24u), ghpcMeshF32(rdram, ctx, runtime, w + 0x28u),
        ghpcMeshF32(rdram, ctx, runtime, w + 0x30u), ghpcMeshF32(rdram, ctx, runtime, w + 0x34u), ghpcMeshF32(rdram, ctx, runtime, w + 0x38u));
}

// One matched pair for the transform calibration: everything a native draw
// would read (cached object-space verts, the owner's world transform, the
// staged camera) printed just before the PS2 path runs, so the screen-space
// verts the frontend prints under the same mesh tag can be checked against a
// candidate formula. GHPC_TL_CAL=N enables it and caps the primitives the
// frontend prints; GHPC_TL_CAL_DRAWS caps the draws tagged here, default 3.
// Returns the mesh to tag, or 0.
// R2: hand the cached geometry to a native backend.
//
// Separate from ghpcMeshTlCal rather than folded into it, because that one is
// a calibration probe: it is gated on GHPC_TL_CAL, waits for the chart to
// start, and stops after a few draws. A backend needs every draw of every
// frame, so the only gate here is scene::active(), which is one relaxed load
// when no backend is installed.
//
// The PS2 is little endian and so is the host, and the cache holds raw guest
// bytes, so the float and uint16 fields copy straight across.
// Find a VIF header by scanning back from the packet cursor.
//
// The per-draw matrices are read out of the packet here rather than from
// DrawShowing's g_ghpcInstWorld, and that is not a style choice: that global
// is documented in its own file as a probe and not a seam input, because it
// names a different mesh chain than the DrawFaces it precedes. Reading the
// packet at submit time names the right draw by construction, and it is the
// path the transform was verified on to GS subpixel over 171 matched pairs.
static uint32_t ghpcFindVifHeader(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                                  uint32_t cursor, uint32_t header) {
    for (uint32_t back = 1; back <= 512u; ++back) {
        const uint32_t a = cursor - back * 0x10u;
        if (READ32(a + 0xcu) == header) return a;
    }
    return 0u;
}

static void ghpcReadQuads(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                          uint32_t base, uint32_t quadCount, float* out) {
    for (uint32_t q = 0; q < quadCount; ++q) {
        for (uint32_t k = 0; k < 4u; ++k) {
            const uint32_t bits = READ32(base + q * 0x10u + k * 4u);
            std::memcpy(out + q * 4u + k, &bits, sizeof(float));
        }
    }
}

void ghpcSceneSubmit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    if (!ghpc::scene::active()) return;

    if (g_ghpcCamSelects != 0ull) {
        ghpc::scene::Camera cam{};
        // g_ghpcCamQw[0]=qw696 scale, [1]=qw697 offset, [4..7]=qw700..703 rows.
        for (uint32_t r = 0; r < 4u; ++r)
            for (uint32_t c = 0; c < 4u; ++c)
                cam.m[r * 4u + c] = g_ghpcCamQw[4u + r][c];
        for (uint32_t k = 0; k < 3u; ++k) {
            cam.scale[k] = g_ghpcCamQw[0][k];
            cam.offset[k] = g_ghpcCamQw[1][k];
        }
        ghpc::scene::setCamera(cam);
    }

    // The render target this draw lands in, from PsRnd's register shadow:
    // SetRegister (0x43e320) stores each register at ThePsRnd + 0xF0 +
    // reg * 8 before it sends it, so the shadow is what the GS will hold when
    // this draw's packet runs. FRAME_1 0x4c, XYOFFSET_1 0x18, SCISSOR_1 0x40.
    {
        const uint32_t shadow = 0x4f2780u + 0xF0u;
        const uint64_t frame = READ64(shadow + 0x4cu * 8u);
        const uint64_t xyoffset = READ64(shadow + 0x18u * 8u);
        const uint64_t scissor = READ64(shadow + 0x40u * 8u);
        ghpc::scene::Target t{};
        t.fbp = (uint32_t)(frame & 0x1FFull);
        t.fbw = (uint32_t)((frame >> 16) & 0x3Full);
        t.psm = (uint32_t)((frame >> 24) & 0x3Full);
        t.ofx = (float)(xyoffset & 0xFFFFull) / 16.0f;
        t.ofy = (float)((xyoffset >> 32) & 0xFFFFull) / 16.0f;
        t.scissor[0] = (uint32_t)(scissor & 0x7FFull);
        t.scissor[1] = (uint32_t)((scissor >> 16) & 0x7FFull);
        t.scissor[2] = (uint32_t)((scissor >> 32) & 0x7FFull);
        t.scissor[3] = (uint32_t)((scissor >> 48) & 0x7FFull);
        ghpc::scene::setTarget(t);
    }

    // Skinned uploads a 20-quadword palette at VU1 660 behind 0x6C140294, of
    // which qw660..675 are the four bone matrices. Unskinned writes one matrix
    // at qw676..679 behind 0x6C0402A4. Both paths write qw676, so the palette
    // header is what distinguishes them.
    const uint32_t cursor = runtime->Load32(rdram, ctx, 0x70000008u);
    const uint32_t palette = ghpcFindVifHeader(rdram, ctx, runtime, cursor, 0x6C140294u);
    // Either way qw676..679 is the matrix the lighting programs take the
    // normal through: the last four quadwords of the 20-qw skinned upload, or
    // the unskinned world matrix itself.
    if (palette != 0u) {
        float bones[20 * 4];
        ghpcReadQuads(rdram, ctx, runtime, palette + 0x10u, 20u, bones);
        ghpc::scene::setBonePalette(bones, 4u);
        ghpc::scene::setLightMatrix(bones + 64);
    } else {
        const uint32_t world = ghpcFindVifHeader(rdram, ctx, runtime, cursor, 0x6C0402A4u);
        if (world != 0u) {
            float m[16];
            ghpcReadQuads(rdram, ctx, runtime, world + 0x10u, 4u, m);
            ghpc::scene::setWorldMatrix(m);
            ghpc::scene::setLightMatrix(m);
        }
    }

    uint32_t vertCnt = 0, faceCnt = 0;
    const uint8_t* verts = nullptr;
    const uint8_t* faces = nullptr;
    // Every draw this seam cannot serve, counted. A backend can only draw
    // what arrives, and the difference between the guest's draw count and the
    // seam's is invisible from the backend side: the backend sees a complete
    // frame either way, just a smaller one.
    //
    // Two reasons a draw is dropped, and they are not the same gap:
    //   cacheMiss  the mesh was never captured at PsMesh::Sync. rnd-seam.md
    //              names three meshes, one of them mutable geometry
    //              (flags140 & 0x1f), which Sync deliberately leaves alone.
    //   noFaces    captured, but Face[] is empty. Its triangles live in the
    //              converted PS2 packet, not in the engine's face vector, so
    //              a triangle list off Face[] has nothing to draw.
    static uint64_t s_dropCacheMiss = 0;
    static uint64_t s_dropNoFaces = 0;
    static uint64_t s_served = 0;
    static const long s_dropEvery = []() {
        const char* e = std::getenv("GHPC_SCENE_DROP_LOG");
        return e ? std::atol(e) : 20000L;
    }();

    const bool cached = ghpcMeshCacheLookup(self, &vertCnt, &faceCnt, &verts, &faces);
    const bool usable = cached && vertCnt != 0 && faceCnt != 0 &&
                        verts != nullptr && faces != nullptr;
    if (!cached) {
        ++s_dropCacheMiss;
        // What a miss is: the first few, with the mutable flag and the live
        // vector sizes, since Sync leaves mutable meshes' vectors alone.
        static int s_missLogged = 0;
        if (s_dropEvery > 0 && s_missLogged < 12) {
            ++s_missLogged;
            const uint32_t fb = READ32(self + 0x108u), fe = READ32(self + 0x10cu);
            const uint32_t vp = READ32(self + 0x100u);
            float c[4] = {0, 0, 0, 0};
            float uv[2] = {0, 0};
            if (vp != 0u) {
                for (uint32_t k = 0; k < 4u; ++k) { const uint32_t b = READ32(vp + 0x20u + k * 4u); std::memcpy(&c[k], &b, 4); }
                for (uint32_t k = 0; k < 2u; ++k) { const uint32_t b = READ32(vp + 0x30u + k * 4u); std::memcpy(&uv[k], &b, 4); }
            }
            std::fprintf(stderr,
                "[ghpc/scene/miss] self=0x%08x owner=0x%08x flags140=0x%x verts=%u vertPtr=0x%08x faces=%u "
                "v0col=(%g %g %g %g) v0uv=(%g %g)\n",
                self, READ32(self + 0x138u), READ32(self + 0x140u), READ32(self + 0x104u),
                vp, (fe >= fb) ? (fe - fb) / 6u : 0u, c[0], c[1], c[2], c[3], uv[0], uv[1]);
            // The converted face packet, the only place a mesh with empty
            // vectors keeps its geometry: +0x150 a MemHandle*, +0x154 its
            // size in quadwords, +0x158/+0x15a two halfwords DrawFaces reads.
            // The handle's first words and the words at its first pointer,
            // so the next run can see which of them is the buffer.
            const uint32_t handle = READ32(self + 0x150u);
            std::fprintf(stderr,
                "[ghpc/scene/packet] self=0x%08x handle=0x%08x qwc=%u h158=0x%04x h15a=0x%04x\n",
                self, handle, READ32(self + 0x154u), READ16(self + 0x158u), READ16(self + 0x15au));
            if (handle != 0u && !PS2Runtime::isSpecialAddress(handle)) {
                std::fprintf(stderr, "[ghpc/scene/packet]   handle words:");
                for (uint32_t k = 0; k < 4u; ++k) std::fprintf(stderr, " 0x%08x", READ32(handle + k * 4u));
                std::fprintf(stderr, "\n");
                const uint32_t buf = READ32(handle);
                if (buf != 0u && !PS2Runtime::isSpecialAddress(buf)) {
                    for (uint32_t q = 0; q < 12u; ++q) {
                        std::fprintf(stderr, "[ghpc/scene/packet]   qw%-2u 0x%08x 0x%08x 0x%08x 0x%08x\n", q,
                                     READ32(buf + q * 16u), READ32(buf + q * 16u + 4u),
                                     READ32(buf + q * 16u + 8u), READ32(buf + q * 16u + 12u));
                    }
                }
            }
        }
    } else if (!usable) {
        ++s_dropNoFaces;
    } else {
        ++s_served;
    }
    if (s_dropEvery > 0 && ((s_served + s_dropCacheMiss + s_dropNoFaces) % (uint64_t)s_dropEvery) == 0) {
        std::fprintf(stderr,
            "[ghpc/scene/drop] served=%llu cacheMiss=%llu noFaces=%llu\n",
            (unsigned long long)s_served, (unsigned long long)s_dropCacheMiss,
            (unsigned long long)s_dropNoFaces);
    }
    // Reused across draws; a frame submits hundreds and these would otherwise
    // be hundreds of allocations per frame.
    static std::vector<ghpc::scene::Vertex> s_verts;
    static std::vector<uint16_t> s_indices;

    auto convert = [&](const uint8_t* src, uint32_t count) {
        s_verts.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* v = src + (size_t)i * 64u;
            ghpc::scene::Vertex& o = s_verts[i];
            std::memcpy(o.pos,     v + 0x00u, sizeof(o.pos));
            std::memcpy(o.norm,    v + 0x10u, sizeof(o.norm));
            std::memcpy(o.weights, v + 0x20u, sizeof(o.weights));
            std::memcpy(o.uv,      v + 0x30u, sizeof(o.uv));
        }
    };

    // A mutable mesh (+0x140 & 0x1f) is one Sync leaves alone: its vectors
    // stay live and change, which is why it is not cached. Serve it from the
    // live vectors, as per-frame geometry. With no Face[] its triangles are
    // the strip VU1 draws over the verts in order, 0-1-2, 1-2-3, and so on.
    if (!cached && (READ32(self + 0x140u) & 0x1fu) != 0u) {
        const uint32_t liveCnt = READ32(self + 0x104u);
        const uint32_t livePtr = READ32(self + 0x100u);
        const uint32_t fb = READ32(self + 0x108u), fe = READ32(self + 0x10cu);
        const uint32_t liveFaces = (fe >= fb) ? (fe - fb) / 6u : 0u;
        if (liveCnt >= 3u && liveCnt <= 4096u && livePtr != 0u &&
            !PS2Runtime::isSpecialAddress(livePtr) &&
            (livePtr & PS2_RAM_MASK) + liveCnt * 64u <= PS2_RAM_SIZE) {
            convert(rdram + (livePtr & PS2_RAM_MASK), liveCnt);
            s_indices.clear();
            if (liveFaces != 0u && liveFaces <= 8192u &&
                (fb & PS2_RAM_MASK) + liveFaces * 6u <= PS2_RAM_SIZE) {
                s_indices.resize((size_t)liveFaces * 3u);
                std::memcpy(s_indices.data(), rdram + (fb & PS2_RAM_MASK), (size_t)liveFaces * 6u);
            } else {
                for (uint32_t i = 0; i + 2u < liveCnt; ++i) {
                    s_indices.push_back((uint16_t)i);
                    s_indices.push_back((uint16_t)(i + 1u));
                    s_indices.push_back((uint16_t)(i + 2u));
                }
            }
            ghpc::scene::submitDynamicMesh(self, s_verts.data(), liveCnt,
                                           s_indices.data(), (uint32_t)s_indices.size());
        }
        return;
    }
    if (!usable) return;

    convert(verts, vertCnt);

    s_indices.resize((size_t)faceCnt * 3u);
    std::memcpy(s_indices.data(), faces, (size_t)faceCnt * 6u);

    ghpc::scene::submitMesh(self, s_verts.data(), vertCnt,
                            s_indices.data(), (uint32_t)s_indices.size());
}

uint32_t ghpcMeshTlCal(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    static const bool s_on = std::getenv("GHPC_TL_CAL") != nullptr;
    if (!s_on) return 0u;
    static const long s_maxDraws = []() {
        const char* e = std::getenv("GHPC_TL_CAL_DRAWS");
        return (e != nullptr) ? std::strtol(e, nullptr, 0) : 3l;
    }();
    // The draws before StartGame are menu and loading-screen quads under a 2D
    // camera, which pin none of the perspective terms. Wait for gameplay, the
    // same instant GHPC_FRAME_AFTER_START waits for. GHPC_TL_CAL_ANY takes
    // whatever draws first instead, for a run that will not reach gameplay.
    static const bool s_any = std::getenv("GHPC_TL_CAL_ANY") != nullptr;
    static long s_draws = 0;
    if (s_draws >= s_maxDraws) return 0u;
    if (!s_any && !g_ghpcGameStarted.load(std::memory_order_relaxed)) return 0u;

    uint32_t vertCnt = 0, faceCnt = 0;
    const uint8_t* verts = nullptr;
    const uint8_t* faces = nullptr;
    if (!ghpcMeshCacheLookup(self, &vertCnt, &faceCnt, &verts, &faces)) return 0u;
    if (vertCnt == 0 || faceCnt == 0) return 0u;
    ++s_draws;

    std::fprintf(stderr,
        "[ghpc/tlcal] --- draw %ld mesh=0x%08x verts=%u faces=%u packetQw=%u flags140=0x%08x"
        " cam=0x%08x camSelects=%llu worldDirty=%u\n",
        s_draws, self, vertCnt, faceCnt, (unsigned)READ16(self + 0x154u),
        READ32(self + 0x140u), g_ghpcCamThis, (unsigned long long)g_ghpcCamSelects,
        READ32(self + 0xe0u));

    const uint32_t w = self + 0xa0u;
    for (int r = 0; r < 4; ++r) {
        std::fprintf(stderr, "[ghpc/tlcal]   ownerworld%d=(%g %g %g %g)\n", r,
            ghpcMeshF32(rdram, ctx, runtime, w + (uint32_t)r * 0x10u + 0x0u),
            ghpcMeshF32(rdram, ctx, runtime, w + (uint32_t)r * 0x10u + 0x4u),
            ghpcMeshF32(rdram, ctx, runtime, w + (uint32_t)r * 0x10u + 0x8u),
            ghpcMeshF32(rdram, ctx, runtime, w + (uint32_t)r * 0x10u + 0xcu));
    }
    std::fprintf(stderr, "[ghpc/tlcal]   inst=0x%08x instOwner=0x%08x selfOwner=0x%08x\n",
        g_ghpcInstThis, g_ghpcInstOwner, READ32(self + 0x138u));

    // The object matrix VU1 actually received, read out of the packet rather
    // than inferred from a call path. PsMesh::DrawShowing writes it at
    // 0x43f308..0x43f32c as four quadwords behind a 0x6C0402A4 VIF header
    // (V4-32, NUM 4, VU1 address 676), and material and texture registers are
    // appended after it, so it sits a bounded distance below the cursor.
    {
        const uint32_t cur = runtime->Load32(rdram, ctx, 0x70000008u);
        uint32_t hdr = 0u;
        for (uint32_t back = 1; back <= 512u; ++back) {
            const uint32_t a = cur - back * 0x10u;
            if (READ32(a + 0xcu) == 0x6C0402A4u) { hdr = a; break; }
        }
        std::fprintf(stderr, "[ghpc/tlcal]   qw676hdr=0x%08x cursor=0x%08x\n", hdr, cur);
        for (uint32_t r = 0; r < 4u && hdr != 0u; ++r) {
            const uint32_t q = hdr + 0x10u + r * 0x10u;
            std::fprintf(stderr, "[ghpc/tlcal]   vuworld%u=(%g %g %g %g)\n", r,
                ghpcMeshF32(rdram, ctx, runtime, q + 0x0u), ghpcMeshF32(rdram, ctx, runtime, q + 0x4u),
                ghpcMeshF32(rdram, ctx, runtime, q + 0x8u), ghpcMeshF32(rdram, ctx, runtime, q + 0xcu));
        }
    }
    std::fprintf(stderr, "[ghpc/tlcal]   objpath=%d bones=0x%08x inst=0x%08x instowner=0x%08x\n",
        g_ghpcInstPath, g_ghpcInstBones, g_ghpcInstThis, g_ghpcInstOwner);
    for (int b = 0; b < 4 && g_ghpcInstPath > 0; ++b) {
        for (int r = 0; r < 4; ++r) {
            std::fprintf(stderr, "[ghpc/tlcal]   bone%d_%d=(%g %g %g %g)\n", b, r,
                g_ghpcBoneXfm[b][r][0], g_ghpcBoneXfm[b][r][1],
                g_ghpcBoneXfm[b][r][2], g_ghpcBoneXfm[b][r][3]);
        }
    }
    for (int r = 0; r < 4; ++r) {
        std::fprintf(stderr, "[ghpc/tlcal]   world%d=(%g %g %g %g)\n", r,
            g_ghpcInstWorld[r][0], g_ghpcInstWorld[r][1],
            g_ghpcInstWorld[r][2], g_ghpcInstWorld[r][3]);
    }
    for (int i = 0; i < 8; ++i) {
        std::fprintf(stderr, "[ghpc/tlcal]   qw%d=(%g %g %g %g)\n", 696 + i,
            g_ghpcCamQw[i][0], g_ghpcCamQw[i][1], g_ghpcCamQw[i][2], g_ghpcCamQw[i][3]);
    }

    const uint32_t nv = vertCnt < 256u ? vertCnt : 256u;
    for (uint32_t i = 0; i < nv; ++i) {
        float f[16];
        std::memcpy(f, verts + (size_t)i * 64u, sizeof(f));
        std::fprintf(stderr,
            "[ghpc/tlcal]   ov%u pos=(%g %g %g) norm=(%g %g %g) color=(%g %g %g %g) uv=(%g %g)\n",
            i, f[0], f[1], f[2], f[4], f[5], f[6], f[8], f[9], f[10], f[11], f[12], f[13]);
    }
    const uint32_t nf = faceCnt < 256u ? faceCnt : 256u;
    for (uint32_t i = 0; i < nf; ++i) {
        uint16_t idx[3];
        std::memcpy(idx, faces + (size_t)i * 6u, sizeof(idx));
        std::fprintf(stderr, "[ghpc/tlcal]   of%u %u %u %u\n", i, idx[0], idx[1], idx[2]);
    }
    return self;
}

// Arm the native draw for this mesh. The transform runs at the MSCAL rather
// than here, because this body also drives PsRnd::FlushPacket 0x43e400, which
// is what carries the material and texture registers out as GIF A+D through
// VIF1 DIRECT. Skipping the body would draw with the previous material and
// leave the scratchpad cursor growing with nothing draining it. See
// runtime/gs/ghpc_native_draw.h.
//
// Armed only when the cache holds this mesh's geometry and the DrawShowing
// capture names this same owner, so a stale palette can never be projected
// against the wrong mesh. Everything else falls through to VU1.
void ghpcMeshNativeArm(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t self) {
    if (ghpcNativeDrawMode() == 0) return;
    // Nothing is disarmed here. DrawFaces only appends on the common path and
    // DrawShowing flushes once after its material loop, so earlier draws are
    // still queued and waiting for their MSCAL.
    //
    // Before StartGame the draws are menu and loading quads under a 2D
    // camera, and the perspective terms this reads are not staged yet.
    if (!g_ghpcGameStarted.load(std::memory_order_relaxed)) return;
    if (g_ghpcInstPath < 0 || g_ghpcInstOwner != self) return;

    GhpcNativeDrawArgs a;
    if (!ghpcMeshCacheLookup(self, &a.vertCount, &a.faceCount, &a.verts, &a.faces)) return;
    if (a.vertCount == 0 || a.faceCount == 0) return;

    a.skinned = (g_ghpcInstPath > 0);
    a.mesh = self;
    std::memcpy(a.bone, g_ghpcBoneXfm, sizeof(a.bone));
    std::memcpy(a.world, g_ghpcInstWorld, sizeof(a.world));
    std::memcpy(a.cam, g_ghpcCamQw, sizeof(a.cam));
    ghpcNativeDrawArm(a);
}

} // namespace

// Function: DrawFaces__6PsMesh
// Address: 0x43eea0 - 0x43f038
void DrawFaces__6PsMesh_0x43eea0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("DrawFaces__6PsMesh_0x43eea0");
#endif

    switch (ctx->pc) {
        case 0x43eefcu: goto label_43eefc;
        case 0x43ef20u: goto label_43ef20;
        case 0x43ef28u: goto label_43ef28;
        case 0x43ef3cu: goto label_43ef3c;
        case 0x43ef44u: goto label_43ef44;
        case 0x43ef70u: goto label_43ef70;
        case 0x43ef80u: goto label_43ef80;
        case 0x43ef88u: goto label_43ef88;
        case 0x43efccu: goto label_43efcc;
        case 0x43efe4u: goto label_43efe4;
        case 0x43eff8u: goto label_43eff8;
        case 0x43f00cu: goto label_43f00c;
        default: break;
    }

    // Fresh entry only (a resume above jumps past this). $a0 is `this`.
    ghpcMeshCacheProbe(rdram, ctx, runtime, GPR_U32(ctx, 4));
    ghpcMeshNativeArm(rdram, ctx, runtime, GPR_U32(ctx, 4));
    // Tag stays set through the body: the DMA kick and everything under it
    // run inline, so the frontend's submits land while this is current. It is
    // cleared by the next fresh entry rather than at the return, because the
    // body has a dozen yield points and none of them is the only exit.
    g_ghpcTlCalMesh = ghpcMeshTlCal(rdram, ctx, runtime, GPR_U32(ctx, 4));
    ghpcSceneSubmit(rdram, ctx, runtime, GPR_U32(ctx, 4));
    {
        static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
        if (s_log) {
            static uint64_t s_calls = 0;
            const uint64_t n = ++s_calls;
            if (n <= 40 || (n % 500) == 0) {
                ghpcMeshLog(rdram, ctx, runtime, GPR_U32(ctx, 4), n);
            }
        }
    }

    ctx->pc = 0x43eea0u;

    // 0x43eea0: 0x27bdff40  addiu       $sp, $sp, -0xC0
    ctx->pc = 0x43eea0u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967104));
    // 0x43eea4: 0x3c05004f  lui         $a1, 0x4F
    ctx->pc = 0x43eea4u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)79 << 16));
    // 0x43eea8: 0x7fb30080  sq          $s3, 0x80($sp)
    ctx->pc = 0x43eea8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 128), GPR_VEC(ctx, 19));
    // 0x43eeac: 0x24a62cb8  addiu       $a2, $a1, 0x2CB8
    ctx->pc = 0x43eeacu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 5), 11448));
    // 0x43eeb0: 0x7fb40070  sq          $s4, 0x70($sp)
    ctx->pc = 0x43eeb0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 112), GPR_VEC(ctx, 20));
    // 0x43eeb4: 0x7fb000b0  sq          $s0, 0xB0($sp)
    ctx->pc = 0x43eeb4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 176), GPR_VEC(ctx, 16));
    // 0x43eeb8: 0x80a02d  daddu       $s4, $a0, $zero
    ctx->pc = 0x43eeb8u;
    SET_GPR_U64(ctx, 20, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43eebc: 0x7fb100a0  sq          $s1, 0xA0($sp)
    ctx->pc = 0x43eebcu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 17));
    // 0x43eec0: 0x7fb20090  sq          $s2, 0x90($sp)
    ctx->pc = 0x43eec0u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 144), GPR_VEC(ctx, 18));
    // 0x43eec4: 0x7fb50060  sq          $s5, 0x60($sp)
    ctx->pc = 0x43eec4u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 96), GPR_VEC(ctx, 21));
    // 0x43eec8: 0x7fb60050  sq          $s6, 0x50($sp)
    ctx->pc = 0x43eec8u;
    WRITE128(ADD32(GPR_U32(ctx, 29), 80), GPR_VEC(ctx, 22));
    // 0x43eecc: 0x7fb70040  sq          $s7, 0x40($sp)
    ctx->pc = 0x43eeccu;
    WRITE128(ADD32(GPR_U32(ctx, 29), 64), GPR_VEC(ctx, 23));
    // 0x43eed0: 0xffbf0030  sd          $ra, 0x30($sp)
    ctx->pc = 0x43eed0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 31));
    // 0x43eed4: 0x8ca22cb8  lw          $v0, 0x2CB8($a1)
    ctx->pc = 0x43eed4u;
    SET_GPR_S32(ctx, 2, (int32_t)FAST_READ32(0x4F2CB8u));
    // 0x43eed8: 0x96830158  lhu         $v1, 0x158($s4)
    ctx->pc = 0x43eed8u;
    SET_GPR_U32(ctx, 3, (uint16_t)READ16(ADD32(GPR_U32(ctx, 20), 344)));
    // 0x43eedc: 0x8cc40004  lw          $a0, 0x4($a2)
    ctx->pc = 0x43eedcu;
    SET_GPR_S32(ctx, 4, (int32_t)FAST_READ32(0x4F2CBCu));
    // 0x43eee0: 0x431021  addu        $v0, $v0, $v1
    ctx->pc = 0x43eee0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
    // 0x43eee4: 0xaca22cb8  sw          $v0, 0x2CB8($a1)
    ctx->pc = 0x43eee4u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 2)); ps2TraceGuestWrite(rdram, 0x4F2CB8u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x4F2CB8u, _value); } while (0);
    // 0x43eee8: 0x9683015a  lhu         $v1, 0x15A($s4)
    ctx->pc = 0x43eee8u;
    SET_GPR_U32(ctx, 3, (uint16_t)READ16(ADD32(GPR_U32(ctx, 20), 346)));
    // 0x43eeec: 0x832021  addu        $a0, $a0, $v1
    ctx->pc = 0x43eeecu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), GPR_U32(ctx, 3)));
    // 0x43eef0: 0xacc40004  sw          $a0, 0x4($a2)
    ctx->pc = 0x43eef0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 4)); ps2TraceGuestWrite(rdram, 0x4F2CBCu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x4F2CBCu, _value); } while (0);
    // 0x43eef4: 0xc0c8454  jal         func_321150
    ctx->pc = 0x43EEF4u;
    SET_GPR_U32(ctx, 31, 0x43EEFCu);
    ctx->pc = 0x43EEF8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EEF4u;
    // 0x43eef8: 0x8e840150  lw          $a0, 0x150($s4) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 20), 336)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321150u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321150u, 0x43EEF4u, 0x43EEFCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EEFCu;
label_43eefc:
    // 0x43eefc: 0x8e830140  lw          $v1, 0x140($s4)
    ctx->pc = 0x43eefcu;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 20), 320)));
    // 0x43ef00: 0x3063001f  andi        $v1, $v1, 0x1F
    ctx->pc = 0x43ef00u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) & (uint64_t)(uint16_t)31);
    // 0x43ef04: 0x14600011  bnez        $v1, . + 4 + (0x11 << 2)
    ctx->pc = 0x43EF04u;
    {
        const bool branch_taken_0x43ef04 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 0));
        ctx->pc = 0x43EF08u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EF04u;
        // 0x43ef08: 0x40982d  daddu       $s3, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 19, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ef04) {
            ctx->pc = 0x43EF4Cu;
            goto label_43ef4c;
        }
    }
    ctx->pc = 0x43EF0Cu;
    // 0x43ef0c: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43ef0cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ef10: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43ef10u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef14: 0x382d  daddu       $a3, $zero, $zero
    ctx->pc = 0x43ef14u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef18: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x43EF18u;
    SET_GPR_U32(ctx, 31, 0x43EF20u);
    ctx->pc = 0x43EF1Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF18u;
    // 0x43ef1c: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x43EF18u, 0x43EF20u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF20u;
label_43ef20:
    // 0x43ef20: 0xc10f9d2  jal         func_43E748
    ctx->pc = 0x43EF20u;
    SET_GPR_U32(ctx, 31, 0x43EF28u);
    ctx->pc = 0x43EF24u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF20u;
    // 0x43ef24: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x43EF20u, 0x43EF28u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF28u;
label_43ef28:
    // 0x43ef28: 0x96870154  lhu         $a3, 0x154($s4)
    ctx->pc = 0x43ef28u;
    SET_GPR_U32(ctx, 7, (uint16_t)READ16(ADD32(GPR_U32(ctx, 20), 340)));
    // 0x43ef2c: 0x3c047000  lui         $a0, 0x7000
    ctx->pc = 0x43ef2cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    // 0x43ef30: 0x260302d  daddu       $a2, $s3, $zero
    ctx->pc = 0x43ef30u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef34: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x43EF34u;
    SET_GPR_U32(ctx, 31, 0x43EF3Cu);
    ctx->pc = 0x43EF38u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF34u;
    // 0x43ef38: 0x24050003  addiu       $a1, $zero, 0x3 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x43EF34u, 0x43EF3Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF3Cu;
label_43ef3c:
    // 0x43ef3c: 0xc10f9d2  jal         func_43E748
    ctx->pc = 0x43EF3Cu;
    SET_GPR_U32(ctx, 31, 0x43EF44u);
    ctx->pc = 0x43EF40u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF3Cu;
    // 0x43ef40: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E748u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E748u, 0x43EF3Cu, 0x43EF44u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF44u;
label_43ef44:
    // 0x43ef44: 0x1000002f  b           . + 4 + (0x2F << 2)
    ctx->pc = 0x43EF44u;
    {
        const bool branch_taken_0x43ef44 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x43ef44) {
            ctx->pc = 0x43F004u;
            goto label_43f004;
        }
    }
    ctx->pc = 0x43EF4Cu;
label_43ef4c:
    // 0x43ef4c: 0x96820154  lhu         $v0, 0x154($s4)
    ctx->pc = 0x43ef4cu;
    SET_GPR_U32(ctx, 2, (uint16_t)READ16(ADD32(GPR_U32(ctx, 20), 340)));
    // 0x43ef50: 0xafa00004  sw          $zero, 0x4($sp)
    ctx->pc = 0x43ef50u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 0));
    // 0x43ef54: 0x1040002b  beqz        $v0, . + 4 + (0x2B << 2)
    ctx->pc = 0x43EF54u;
    {
        const bool branch_taken_0x43ef54 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x43EF58u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EF54u;
        // 0x43ef58: 0xafa20020  sw          $v0, 0x20($sp) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 29), 32), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43ef54) {
            ctx->pc = 0x43F004u;
            goto label_43f004;
        }
    }
    ctx->pc = 0x43EF5Cu;
    // 0x43ef5c: 0x3c02004f  lui         $v0, 0x4F
    ctx->pc = 0x43ef5cu;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)79 << 16));
    // 0x43ef60: 0x27b70020  addiu       $s7, $sp, 0x20
    ctx->pc = 0x43ef60u;
    SET_GPR_S32(ctx, 23, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
    // 0x43ef64: 0x24552780  addiu       $s5, $v0, 0x2780
    ctx->pc = 0x43ef64u;
    SET_GPR_S32(ctx, 21, (int32_t)ADD32(GPR_U32(ctx, 2), 10112));
    // 0x43ef68: 0x27b60024  addiu       $s6, $sp, 0x24
    ctx->pc = 0x43ef68u;
    SET_GPR_S32(ctx, 22, (int32_t)ADD32(GPR_U32(ctx, 29), 36));
    // 0x43ef6c: 0x382d  daddu       $a3, $zero, $zero
    ctx->pc = 0x43ef6cu;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_43ef70:
    // 0x43ef70: 0x24050001  addiu       $a1, $zero, 0x1
    ctx->pc = 0x43ef70u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x43ef74: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43ef74u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef78: 0xc10f9e2  jal         func_43E788
    ctx->pc = 0x43EF78u;
    SET_GPR_U32(ctx, 31, 0x43EF80u);
    ctx->pc = 0x43EF7Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF78u;
    // 0x43ef7c: 0x3c047000  lui         $a0, 0x7000 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)28672 << 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E788u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E788u, 0x43EF78u, 0x43EF80u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF80u;
label_43ef80:
    // 0x43ef80: 0xc06fc08  jal         func_1BF020
    ctx->pc = 0x43EF80u;
    SET_GPR_U32(ctx, 31, 0x43EF88u);
    ctx->pc = 0x43EF84u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EF80u;
    // 0x43ef84: 0x2a0202d  daddu       $a0, $s5, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1BF020u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1BF020u, 0x43EF80u, 0x43EF88u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EF88u;
label_43ef88:
    // 0x43ef88: 0x8fa70020  lw          $a3, 0x20($sp)
    ctx->pc = 0x43ef88u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x43ef8c: 0x2c0182d  daddu       $v1, $s6, $zero
    ctx->pc = 0x43ef8cu;
    SET_GPR_U64(ctx, 3, (uint64_t)GPR_U64(ctx, 22) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef90: 0xafa20024  sw          $v0, 0x24($sp)
    ctx->pc = 0x43ef90u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 36), GPR_U32(ctx, 2));
    // 0x43ef94: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x43ef94u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43ef98: 0x47102a  slt         $v0, $v0, $a3
    ctx->pc = 0x43ef98u;
    SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 2) < (int64_t)GPR_S64(ctx, 7)) ? 1 : 0);
    // 0x43ef9c: 0x260282d  daddu       $a1, $s3, $zero
    ctx->pc = 0x43ef9cu;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43efa0: 0x2e2180a  movz        $v1, $s7, $v0
    ctx->pc = 0x43efa0u;
    if (GPR_U64(ctx, 2) == 0) SET_GPR_VEC(ctx, 3, GPR_VEC(ctx, 23));
    // 0x43efa4: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43efa4u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43efa8: 0x8c700000  lw          $s0, 0x0($v1)
    ctx->pc = 0x43efa8u;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x43efac: 0xf03823  subu        $a3, $a3, $s0
    ctx->pc = 0x43efacu;
    SET_GPR_S32(ctx, 7, (int32_t)SUB32(GPR_U32(ctx, 7), GPR_U32(ctx, 16)));
    // 0x43efb0: 0xafa70020  sw          $a3, 0x20($sp)
    ctx->pc = 0x43efb0u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 32), GPR_U32(ctx, 7));
    // 0x43efb4: 0x108100  sll         $s0, $s0, 4
    ctx->pc = 0x43efb4u;
    SET_GPR_S32(ctx, 16, (int32_t)SLL32(GPR_U32(ctx, 16), 4));
    // 0x43efb8: 0x2138821  addu        $s1, $s0, $s3
    ctx->pc = 0x43efb8u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 16), GPR_U32(ctx, 19)));
    // 0x43efbc: 0x3c127000  lui         $s2, 0x7000
    ctx->pc = 0x43efbcu;
    SET_GPR_S32(ctx, 18, (int32_t)((uint32_t)28672 << 16));
    // 0x43efc0: 0x8e520008  lw          $s2, 0x8($s2)
    ctx->pc = 0x43efc0u;
    SET_GPR_S32(ctx, 18, (int32_t)runtime->Load32(rdram, ctx, ADD32(GPR_U32(ctx, 18), 8))); // MMIO: 0x70000008
    // 0x43efc4: 0xc070434  jal         func_1C10D0
    ctx->pc = 0x43EFC4u;
    SET_GPR_U32(ctx, 31, 0x43EFCCu);
    ctx->pc = 0x43EFC8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EFC4u;
    // 0x43efc8: 0x220982d  daddu       $s3, $s1, $zero (Delay Slot)
    SET_GPR_U64(ctx, 19, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C10D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C10D0u, 0x43EFC4u, 0x43EFCCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EFCCu;
label_43efcc:
    // 0x43efcc: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x43efccu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43efd0: 0xafb10008  sw          $s1, 0x8($sp)
    ctx->pc = 0x43efd0u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 17));
    // 0x43efd4: 0x24050009  addiu       $a1, $zero, 0x9
    ctx->pc = 0x43efd4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 0), 9));
    // 0x43efd8: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x43efd8u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x43efdc: 0xc10f9ae  jal         func_43E6B8
    ctx->pc = 0x43EFDCu;
    SET_GPR_U32(ctx, 31, 0x43EFE4u);
    ctx->pc = 0x43EFE0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EFDCu;
    // 0x43efe0: 0x240382d  daddu       $a3, $s2, $zero (Delay Slot)
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E6B8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E6B8u, 0x43EFDCu, 0x43EFE4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EFE4u;
label_43efe4:
    // 0x43efe4: 0x2128021  addu        $s0, $s0, $s2
    ctx->pc = 0x43efe4u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), GPR_U32(ctx, 18)));
    // 0x43efe8: 0x3c017000  lui         $at, 0x7000
    ctx->pc = 0x43efe8u;
    SET_GPR_S32(ctx, 1, (int32_t)((uint32_t)28672 << 16));
    // 0x43efec: 0xac300008  sw          $s0, 0x8($at)
    ctx->pc = 0x43efecu;
    runtime->Store32(rdram, ctx, ADD32(GPR_U32(ctx, 1), 8), GPR_U32(ctx, 16)); // MMIO: 0x70000008
    // 0x43eff0: 0xc10f900  jal         func_43E400
    ctx->pc = 0x43EFF0u;
    SET_GPR_U32(ctx, 31, 0x43EFF8u);
    ctx->pc = 0x43EFF4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43EFF0u;
    // 0x43eff4: 0x2a0202d  daddu       $a0, $s5, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x43E400u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x43E400u, 0x43EFF0u, 0x43EFF8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43EFF8u;
label_43eff8:
    // 0x43eff8: 0x8fa20020  lw          $v0, 0x20($sp)
    ctx->pc = 0x43eff8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x43effc: 0x1440ffdc  bnez        $v0, . + 4 + (-0x24 << 2)
    ctx->pc = 0x43EFFCu;
    {
        const bool branch_taken_0x43effc = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x43F000u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43EFFCu;
        // 0x43f000: 0x382d  daddu       $a3, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x43effc) {
            ctx->pc = 0x43EF70u;
            if (runtime->eeCheckpointDue()) {
                return;
            }
            goto label_43ef70;
        }
    }
    ctx->pc = 0x43F004u;
label_43f004:
    // 0x43f004: 0xc0c845c  jal         func_321170
    ctx->pc = 0x43F004u;
    SET_GPR_U32(ctx, 31, 0x43F00Cu);
    ctx->pc = 0x43F008u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x43F004u;
    // 0x43f008: 0x8e840150  lw          $a0, 0x150($s4) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 20), 336)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x321170u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x321170u, 0x43F004u, 0x43F00Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x43F00Cu;
label_43f00c:
    // 0x43f00c: 0x7bb000b0  lq          $s0, 0xB0($sp)
    ctx->pc = 0x43f00cu;
    SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 176)));
    // 0x43f010: 0x7bb100a0  lq          $s1, 0xA0($sp)
    ctx->pc = 0x43f010u;
    SET_GPR_VEC(ctx, 17, READ128(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x43f014: 0x7bb20090  lq          $s2, 0x90($sp)
    ctx->pc = 0x43f014u;
    SET_GPR_VEC(ctx, 18, READ128(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x43f018: 0x7bb30080  lq          $s3, 0x80($sp)
    ctx->pc = 0x43f018u;
    SET_GPR_VEC(ctx, 19, READ128(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x43f01c: 0x7bb40070  lq          $s4, 0x70($sp)
    ctx->pc = 0x43f01cu;
    SET_GPR_VEC(ctx, 20, READ128(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x43f020: 0x7bb50060  lq          $s5, 0x60($sp)
    ctx->pc = 0x43f020u;
    SET_GPR_VEC(ctx, 21, READ128(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x43f024: 0x7bb60050  lq          $s6, 0x50($sp)
    ctx->pc = 0x43f024u;
    SET_GPR_VEC(ctx, 22, READ128(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x43f028: 0x7bb70040  lq          $s7, 0x40($sp)
    ctx->pc = 0x43f028u;
    SET_GPR_VEC(ctx, 23, READ128(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x43f02c: 0xdfbf0030  ld          $ra, 0x30($sp)
    ctx->pc = 0x43f02cu;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x43f030: 0x3e00008  jr          $ra
    ctx->pc = 0x43F030u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x43F034u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x43F030u;
        // 0x43f034: 0x27bd00c0  addiu       $sp, $sp, 0xC0 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 192));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x43F030u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x43F038u;
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&DrawFaces__6PsMesh_0x43eea0), PS2Runtime::RecompiledFunction>::value,
              "DrawFaces__6PsMesh_0x43eea0 override no longer matches the recompiled signature");
