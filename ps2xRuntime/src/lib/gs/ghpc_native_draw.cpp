#include "runtime/gs/ghpc_native_draw.h"

#include "runtime/gs/ghpc_fixture.h"
#include "runtime/gs/ghpc_transform.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_types.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace
{

// A queue, not a single slot. PsMesh::DrawFaces does not kick: on the common
// path (+0x140 & 0x1f == 0) it appends a REF DMAtag and returns, and
// PsMesh::DrawShowing flushes once after its per-material loop. So several
// draws are armed before any MSCAL fires, and a single slot would feed the
// first MSCAL and leave the rest to VU1. VIF1 parses the chain in order, so
// MSCAL order matches DrawFaces order and a FIFO pairs them exactly.
std::deque<GhpcNativeDrawArgs> g_queue;
constexpr size_t kQueueCap = 256;

unsigned long long g_drew = 0;
unsigned long long g_tris = 0;
unsigned long long g_clipped = 0;
unsigned long long g_offscreen = 0;   // trivially rejected, wholly outside the scissor
unsigned long long g_unencodable = 0; // overlap the screen but will not fit 12.4
unsigned long long g_passed = 0;  // MSCALs that ran VU1 because the queue was empty
unsigned long long g_dropped = 0; // draws evicted by the queue cap, never consumed

// The transform itself lives in runtime/gs/ghpc_transform.h so the replay
// tool under ghpc/tools/ runs this exact code rather than a copy of it.

inline uint64_t xyz2(const GhpcScreen &s)
{
    // XYZ2 is 12.4 fixed point in x and y over the GS 4096 coordinate space.
    // Nothing is clamped here: a clamp drags an off-screen vertex onto the
    // screen edge, and the rasteriser then walks the triangle's bounding box
    // intersected with the scissor (gs_cpu_backend.cpp:2382-2403), so one
    // clamped triangle costs a full-screen edge-test scan. Callers reject
    // instead, which is what VU1 does.
    const float fx = s.x * 16.0f;
    const float fy = s.y * 16.0f;
    double dz = s.z;
    if (dz < 0.0)
        dz = 0.0;
    if (dz > 4294967295.0)
        dz = 4294967295.0;
    return (uint64_t)(uint32_t)fx | ((uint64_t)(uint32_t)fy << 16) |
           ((uint64_t)(uint32_t)dz << 32);
}

inline uint64_t rgbaq(float q)
{
    uint32_t qb;
    std::memcpy(&qb, &q, sizeof(qb));
    // 0x80 is 1.0 to the GS, so a MODULATE texture comes through unchanged.
    // The Vert's colour floats are bone weights here, not a colour.
    return 0x80u | (0x80u << 8) | (0x80u << 16) | ((uint64_t)0x80u << 24) |
           ((uint64_t)qb << 32);
}

inline uint64_t st(float s, float t)
{
    uint32_t sb, tb;
    std::memcpy(&sb, &s, sizeof(sb));
    std::memcpy(&tb, &t, sizeof(tb));
    return (uint64_t)sb | ((uint64_t)tb << 32);
}

} // namespace

int ghpcNativeDrawMode()
{
    static const int s_mode = []() -> int {
        const char *e = std::getenv("GHPC_NATIVE_DRAW");
        return (e != nullptr) ? (int)std::strtol(e, nullptr, 0) : 0;
    }();
    return s_mode;
}

void ghpcNativeDrawArm(const GhpcNativeDrawArgs &args)
{
    // A cap, so a frame that arms draws the MSCALs never consume (a flush that
    // never comes, a path this does not model) leaks bounded memory instead of
    // growing without end. Dropping the oldest keeps the queue in step with the
    // MSCALs that are still to come.
    if (g_queue.size() >= kQueueCap)
    {
        g_queue.pop_front();
        ++g_dropped;
    }
    g_queue.push_back(args);
}

void ghpcNativeDrawDisarm()
{
    g_queue.clear();
}

bool ghpcNativeDrawMscal(GS &gs)
{
    const int mode = ghpcNativeDrawMode();
    if (mode == 0)
        return false;
    if (g_queue.empty())
    {
        // An MSCAL this seam did not arm: a UI draw, or a mesh the cache
        // missed. VU1 still owns it.
        ++g_passed;
        return false;
    }
    const GhpcNativeDrawArgs a = g_queue.front();
    g_queue.pop_front();

    const bool submit = (mode != 2);

    // The visible window, in the same 2048-centred space the transform lands
    // in. The scissor is in screen pixels and the backend subtracts
    // XYOFFSET>>4 before testing it (gs_frontend.cpp:2131), so adding the
    // offset back puts both in one space. Read from the GS rather than
    // assumed: the gameplay sample is ofx=31744 ofy=30720, not 0.
    const GSDebugSnapshot snap = gs.getDebugSnapshot();
    const GSContext &sctx = snap.ctx[0];
    const float ofx = (float)sctx.xyoffset.ofx / 16.0f;
    const float ofy = (float)sctx.xyoffset.ofy / 16.0f;
    const float visX0 = ofx + (float)sctx.scissor.x0;
    const float visX1 = ofx + (float)sctx.scissor.x1 + 1.0f;
    const float visY0 = ofy + (float)sctx.scissor.y0;
    const float visY1 = ofy + (float)sctx.scissor.y1 + 1.0f;

    // Capture this draw's inputs. In mode 2 VU1 still runs, so the primitives
    // it kicks land in the same record and become the oracle the replay tool
    // diffs against.
    ghpcFixtureBeginDraw(a, visX0, visX1, visY0, visY1);

    if (submit)
    {
        // PRIM 3, a triangle list, because the cache holds Face[] rather than
        // the strips the PS2 path consumes. iip=1 tme=1 fst=0 abe=0 are the
        // flags every measured gameplay submit carried
        // (2026-09-11-vertex-transform-calibration.txt). Everything else the
        // draw needs is already latched: the material and texture registers
        // went out as GIF A+D ahead of this MSCAL.
        const uint64_t prim = (uint64_t)GS_PRIM_TRIANGLE | (1ull << 3) /*iip*/ | (1ull << 4) /*tme*/;
        gs.writeRegister(GS_REG_PRIM, prim);
    }

    unsigned long long tris = 0, clipped = 0, offscreen = 0, unencodable = 0;
    for (uint32_t i = 0; i < a.faceCount; ++i)
    {
        uint16_t idx[3];
        std::memcpy(idx, a.faces + (size_t)i * 6u, sizeof(idx));
        if (idx[0] >= a.vertCount || idx[1] >= a.vertCount || idx[2] >= a.vertCount)
        {
            ++clipped;
            continue;
        }
        GhpcScreen s[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k)
        {
            s[k] = ghpcTransformVert(a.verts + (size_t)idx[k] * 64u, a);
            ok = ok && s[k].valid;
        }
        if (!ok)
        {
            ++clipped;
            continue;
        }
        // Trivial reject. The rasteriser walks the triangle's bounding box
        // intersected with the scissor, so an off-screen triangle that is
        // submitted anyway costs a full-screen edge-test scan rather than
        // nothing. VU1 rejects these; so must this.
        const float minX = std::min({s[0].x, s[1].x, s[2].x});
        const float maxX = std::max({s[0].x, s[1].x, s[2].x});
        const float minY = std::min({s[0].y, s[1].y, s[2].y});
        const float maxY = std::max({s[0].y, s[1].y, s[2].y});
        if (maxX < visX0 || minX > visX1 || maxY < visY0 || minY > visY1)
        {
            ++offscreen;
            continue;
        }
        // What survives overlaps the screen, so its bounding box is bounded by
        // the scissor anyway. Anything that still will not encode is dropped
        // rather than clamped, because a clamp moves the vertex and distorts
        // the triangle. Proper near-plane clipping, which VU1 does by
        // generating vertices, is not implemented yet.
        if (!ghpcXyFits(s[0]) || !ghpcXyFits(s[1]) || !ghpcXyFits(s[2]))
        {
            ++unencodable;
            continue;
        }
        ++tris;
        if (!submit)
            continue;
        for (int k = 0; k < 3; ++k)
        {
            gs.writeRegister(GS_REG_RGBAQ, rgbaq(s[k].q));
            gs.writeRegister(GS_REG_ST, st(s[k].s, s[k].t));
            gs.writeRegister(GS_REG_XYZ2, xyz2(s[k]));
        }
    }

    ++g_drew;
    g_tris += tris;
    g_clipped += clipped;
    g_offscreen += offscreen;
    g_unencodable += unencodable;

    static const bool s_log = std::getenv("GHPC_MESH_LOG") != nullptr;
    if (s_log ? (g_drew <= 40 || (g_drew % 500) == 0) : ((g_drew & (g_drew - 1)) == 0))
    {
        std::fprintf(stderr,
                     "[ghpc/native] drew=%llu tris=%llu clipped=%llu offscreen=%llu"
                     " unenc=%llu vu1=%llu dropped=%llu queued=%zu mode=%d"
                     " mesh=0x%08x verts=%u faces=%u skinned=%d\n",
                     g_drew, g_tris, g_clipped, g_offscreen, g_unencodable,
                     g_passed, g_dropped, g_queue.size(), mode,
                     a.mesh, a.vertCount, a.faceCount, a.skinned ? 1 : 0);
    }

    // Mode 2 measures the transform without touching the picture, so VU1
    // still has to run.
    return submit;
}
