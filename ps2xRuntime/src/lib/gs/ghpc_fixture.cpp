#include "runtime/gs/ghpc_fixture.h"

#include "runtime/gs/ghpc_native_draw.h"
#include "runtime/gs/gs_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

struct Record
{
    FixDrawHead head{};
    std::vector<uint8_t> verts;
    std::vector<uint8_t> faces;
    std::vector<FixRefVert> refs;
};

const char *fixturePath()
{
    static const char *s_path = std::getenv("GHPC_FIXTURE");
    return s_path;
}

// Small on purpose. A fixture is a regression case, not a capture of the
// whole frame, and a handful of draws already covers skinned, unskinned and
// the clipping edge cases.
long fixtureCap()
{
    static const long s_cap = []() -> long {
        const char *e = std::getenv("GHPC_FIXTURE_DRAWS");
        return (e != nullptr) ? std::strtol(e, nullptr, 0) : 8l;
    }();
    return s_cap;
}

// The first draws after StartGame are close-up character meshes and are not
// representative of the frame: measured, they reject 4% of their triangles
// offscreen where a whole run rejects 72%. Skipping past them is how you
// capture the part of the frame that actually costs something.
long fixtureSkip()
{
    static const long s_skip = []() -> long {
        const char *e = std::getenv("GHPC_FIXTURE_SKIP");
        return (e != nullptr) ? std::strtol(e, nullptr, 0) : 0l;
    }();
    return s_skip;
}

// Capture one draw in every N past the skip, so a fixture spans the frame
// instead of a contiguous burst of whatever draws first.
long fixtureStride()
{
    static const long s_stride = []() -> long {
        const char *e = std::getenv("GHPC_FIXTURE_STRIDE");
        const long v = (e != nullptr) ? std::strtol(e, nullptr, 0) : 1l;
        return v > 0 ? v : 1l;
    }();
    return s_stride;
}

long g_seen = 0;

std::vector<Record> g_records;
bool g_done = false;
// Whether the draw currently in flight is one we opened a record for. Without
// this, the primitives of a skipped draw would append to the previous
// record and quietly corrupt its oracle.
bool g_capturing = false;

void writeFixture()
{
    const char *path = fixturePath();
    if (path == nullptr)
        return;
    std::FILE *f = std::fopen(path, "wb");
    if (f == nullptr)
    {
        std::fprintf(stderr, "[ghpc/fixture] cannot open %s for writing\n", path);
        return;
    }

    FixFileHeader h{};
    std::memcpy(h.magic, kGhpcFixMagic, sizeof(h.magic));
    h.version = 1;
    h.drawCount = (uint32_t)g_records.size();
    std::fwrite(&h, sizeof(h), 1, f);

    unsigned long long refTotal = 0;
    for (Record &r : g_records)
    {
        r.head.refCount = (uint32_t)r.refs.size();
        refTotal += r.refs.size();
        std::fwrite(&r.head, sizeof(r.head), 1, f);
        if (!r.verts.empty())
            std::fwrite(r.verts.data(), 1, r.verts.size(), f);
        if (!r.faces.empty())
            std::fwrite(r.faces.data(), 1, r.faces.size(), f);
        if (!r.refs.empty())
            std::fwrite(r.refs.data(), sizeof(FixRefVert), r.refs.size(), f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[ghpc/fixture] wrote %s: %zu draws, %llu reference verts\n",
                 path, g_records.size(), refTotal);
}

} // namespace

bool ghpcFixtureActive()
{
    return fixturePath() != nullptr && !g_done;
}

void ghpcFixtureBeginDraw(const GhpcNativeDrawArgs &a,
                          float visX0, float visX1, float visY0, float visY1)
{
    if (!ghpcFixtureActive())
        return;
    // The previous record closes implicitly: VU1 has finished kicking for it
    // by the time the next MSCAL arrives, because everything under the DMA
    // kick runs inline on this thread.
    if ((long)g_records.size() >= fixtureCap())
    {
        g_done = true;
        writeFixture();
        return;
    }

    const long seen = g_seen++;
    g_capturing = false;
    if (seen < fixtureSkip() || ((seen - fixtureSkip()) % fixtureStride()) != 0)
        return;
    g_capturing = true;

    Record r;
    r.head.mesh = a.mesh;
    r.head.vertCount = a.vertCount;
    r.head.faceCount = a.faceCount;
    r.head.skinned = a.skinned ? 1u : 0u;
    std::memcpy(r.head.bone, a.bone, sizeof(r.head.bone));
    std::memcpy(r.head.world, a.world, sizeof(r.head.world));
    std::memcpy(r.head.cam, a.cam, sizeof(r.head.cam));
    r.head.visX0 = visX0;
    r.head.visX1 = visX1;
    r.head.visY0 = visY0;
    r.head.visY1 = visY1;

    if (a.verts != nullptr && a.vertCount > 0)
        r.verts.assign(a.verts, a.verts + (size_t)a.vertCount * 64u);
    if (a.faces != nullptr && a.faceCount > 0)
        r.faces.assign(a.faces, a.faces + (size_t)a.faceCount * 6u);

    g_records.push_back(std::move(r));
}

void ghpcFixtureNoteSubmit(const GSPrimitiveBatch &b)
{
    if (!ghpcFixtureActive() || !g_capturing || g_records.empty())
        return;
    Record &r = g_records.back();
    for (uint8_t i = 0; i < b.vertexCount && i < 3u; ++i)
    {
        const GSVertex &v = b.vertices[i];
        FixRefVert o{};
        o.x = v.x;
        o.y = v.y;
        o.z = v.z;
        o.q = v.q;
        o.s = v.s;
        o.t = v.t;
        o.rgba = (uint32_t)v.r | ((uint32_t)v.g << 8) | ((uint32_t)v.b << 16) |
                 ((uint32_t)v.a << 24);
        o.prim = (uint32_t)b.state.prim.type;
        r.refs.push_back(o);
    }
}
