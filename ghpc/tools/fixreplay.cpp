// Replay a captured fixture through the host transform and diff it against
// the primitives VU1 produced for the same draws.
//
//     GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=/tmp/gh2.fix GHPC_COUNTIN=0.5 ./run
//     ghpc/scripts/fixreplay.sh /tmp/gh2.fix
//
// This exists because a 300s game run is a terrible way to find out that a
// formula is off by a sign. The fixture carries both sides, so the loop here
// is a compile and a diff.
//
// Pairs are matched on uv recovered as st/q, because the submit is fst=0 and
// index order does not survive the strip conversion. A pair counts only when
// it is much closer than its runner-up, so uv aliasing is excluded rather
// than averaged in. That is the same rule the original calibration used
// (ghpc/notes/evidence/2026-09-12-bone-palette.txt).

#include "runtime/gs/ghpc_fixture.h"
#include "runtime/gs/ghpc_native_draw.h"
#include "runtime/gs/ghpc_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct Draw
{
    FixDrawHead head;
    std::vector<uint8_t> verts;
    std::vector<uint8_t> faces;
    std::vector<FixRefVert> refs;
};

bool readFixture(const char *path, std::vector<Draw> &out)
{
    std::FILE *f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    FixFileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1 ||
        std::memcmp(h.magic, kGhpcFixMagic, sizeof(h.magic)) != 0)
    {
        std::fprintf(stderr, "%s is not a ghpc fixture\n", path);
        std::fclose(f);
        return false;
    }
    for (uint32_t i = 0; i < h.drawCount; ++i)
    {
        Draw d{};
        if (std::fread(&d.head, sizeof(d.head), 1, f) != 1)
            break;
        d.verts.resize((size_t)d.head.vertCount * 64u);
        d.faces.resize((size_t)d.head.faceCount * 6u);
        d.refs.resize(d.head.refCount);
        if (!d.verts.empty())
            std::fread(d.verts.data(), 1, d.verts.size(), f);
        if (!d.faces.empty())
            std::fread(d.faces.data(), 1, d.faces.size(), f);
        if (!d.refs.empty())
            std::fread(d.refs.data(), sizeof(FixRefVert), d.refs.size(), f);
        out.push_back(std::move(d));
    }
    std::fclose(f);
    return true;
}

double median(std::vector<double> &v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: fixreplay <fixture>\n");
        return 2;
    }
    std::vector<Draw> draws;
    if (!readFixture(argv[1], draws))
        return 2;

    std::printf("%zu draws\n\n", draws.size());

    unsigned long long totalPairs = 0, totalTris = 0;
    unsigned long long behind = 0, offscreen = 0, unenc = 0;
    std::vector<double> allDx, allDy, allDq;

    for (size_t di = 0; di < draws.size(); ++di)
    {
        const Draw &d = draws[di];

        // Rebuild the args the runtime would have had.
        GhpcNativeDrawArgs a;
        a.verts = d.verts.data();
        a.vertCount = d.head.vertCount;
        a.faces = d.faces.data();
        a.faceCount = d.head.faceCount;
        a.skinned = d.head.skinned != 0;
        a.mesh = d.head.mesh;
        std::memcpy(a.bone, d.head.bone, sizeof(a.bone));
        std::memcpy(a.world, d.head.world, sizeof(a.world));
        std::memcpy(a.cam, d.head.cam, sizeof(a.cam));

        std::vector<GhpcScreen> host(a.vertCount);
        for (uint32_t i = 0; i < a.vertCount; ++i)
            host[i] = ghpcTransformVert(a.verts + (size_t)i * 64u, a);

        // Triangle-level accounting, the same rules the runtime applies.
        unsigned long long dBehind = 0, dOff = 0, dUnenc = 0, dTris = 0;
        double dBoxArea = 0.0, dTriArea = 0.0, dWorstBox = 0.0;
        for (uint32_t i = 0; i < a.faceCount; ++i)
        {
            uint16_t idx[3];
            std::memcpy(idx, a.faces + (size_t)i * 6u, sizeof(idx));
            if (idx[0] >= a.vertCount || idx[1] >= a.vertCount || idx[2] >= a.vertCount)
            {
                ++dBehind;
                continue;
            }
            const GhpcScreen &s0 = host[idx[0]], &s1 = host[idx[1]], &s2 = host[idx[2]];
            if (!s0.valid || !s1.valid || !s2.valid)
            {
                ++dBehind;
                continue;
            }
            const float minX = std::min({s0.x, s1.x, s2.x});
            const float maxX = std::max({s0.x, s1.x, s2.x});
            const float minY = std::min({s0.y, s1.y, s2.y});
            const float maxY = std::max({s0.y, s1.y, s2.y});
            if (maxX < d.head.visX0 || minX > d.head.visX1 ||
                maxY < d.head.visY0 || minY > d.head.visY1)
            {
                ++dOff;
                continue;
            }
            if (!ghpcXyFits(s0) || !ghpcXyFits(s1) || !ghpcXyFits(s2))
            {
                ++dUnenc;
                continue;
            }
            ++dTris;

            // What the rasteriser will actually cost. It walks the triangle's
            // bounding box intersected with the scissor and runs an edge test
            // per pixel (gs_cpu_backend.cpp:2382-2403), so the bill is the box
            // area, not the triangle area. A triangle that straddles the edge
            // and reaches far off screen bills the whole scissor while
            // covering almost none of it.
            const double bx0 = std::max((double)minX, (double)d.head.visX0);
            const double bx1 = std::min((double)maxX, (double)d.head.visX1);
            const double by0 = std::max((double)minY, (double)d.head.visY0);
            const double by1 = std::min((double)maxY, (double)d.head.visY1);
            const double boxArea = std::max(0.0, bx1 - bx0) * std::max(0.0, by1 - by0);
            const double triArea =
                std::fabs((double)(s1.x - s0.x) * (s2.y - s0.y) -
                          (double)(s2.x - s0.x) * (s1.y - s0.y)) * 0.5;
            dBoxArea += boxArea;
            dTriArea += triArea;
            if (boxArea > dWorstBox)
                dWorstBox = boxArea;
        }

        // Match host verts to reference verts on uv = st/q.
        std::vector<double> dx, dy, dq;
        for (uint32_t i = 0; i < a.vertCount; ++i)
        {
            if (!host[i].valid || host[i].q == 0.0f)
                continue;
            const double hu = host[i].s / host[i].q;
            const double hv = host[i].t / host[i].q;

            double best = 1e30, second = 1e30;
            size_t bestIdx = (size_t)-1;
            for (size_t r = 0; r < d.refs.size(); ++r)
            {
                const FixRefVert &rv = d.refs[r];
                if (rv.q == 0.0f)
                    continue;
                const double ru = rv.s / rv.q;
                const double rvv = rv.t / rv.q;
                const double e = (ru - hu) * (ru - hu) + (rvv - hv) * (rvv - hv);
                if (e < best)
                {
                    second = best;
                    best = e;
                    bestIdx = r;
                }
                else if (e < second)
                {
                    second = e;
                }
            }
            // Unambiguous only: ten times closer than the runner-up.
            if (bestIdx == (size_t)-1 || !(second > best * 100.0))
                continue;
            const FixRefVert &rv = d.refs[bestIdx];
            dx.push_back(host[i].x - rv.x);
            dy.push_back(host[i].y - rv.y);
            if (rv.q != 0.0f)
                dq.push_back((host[i].q - rv.q) / rv.q * 100.0);
        }

        std::vector<double> adx = dx, ady = dy, adq = dq;
        const double mdx = median(adx), mdy = median(ady), mdq = median(adq);
        double rx = 0.0, ry = 0.0;
        for (double v : dx)
            rx = std::max(rx, std::fabs(v));
        for (double v : dy)
            ry = std::max(ry, std::fabs(v));

        std::printf("draw %zu mesh=0x%08x %s verts=%u faces=%u refs=%u\n",
                    di, d.head.mesh, d.head.skinned ? "skinned" : "rigid",
                    d.head.vertCount, d.head.faceCount, d.head.refCount);
        std::printf("   tris drawn=%llu behind=%llu offscreen=%llu unencodable=%llu\n",
                    dTris, dBehind, dOff, dUnenc);
        if (dTris > 0)
        {
            const double scissorArea =
                (double)(d.head.visX1 - d.head.visX0) * (d.head.visY1 - d.head.visY0);
            std::printf("   raster cost: box px/tri=%.0f  tri px/tri=%.0f  waste=%.1fx"
                        "  worst box=%.0f (%.0f%% of scissor)\n",
                        dBoxArea / (double)dTris, dTriArea / (double)dTris,
                        dTriArea > 0.0 ? dBoxArea / dTriArea : 0.0,
                        dWorstBox, scissorArea > 0.0 ? dWorstBox / scissorArea * 100.0 : 0.0);
        }
        if (dx.empty())
        {
            std::printf("   NO MATCHED PAIRS\n\n");
        }
        else
        {
            std::printf("   pairs=%zu  dx med=%+.3f max=%.3f  dy med=%+.3f max=%.3f  q med=%+.4f%%\n\n",
                        dx.size(), mdx, rx, mdy, ry, mdq);
        }

        totalPairs += dx.size();
        totalTris += dTris;
        behind += dBehind;
        offscreen += dOff;
        unenc += dUnenc;
        allDx.insert(allDx.end(), dx.begin(), dx.end());
        allDy.insert(allDy.end(), dy.begin(), dy.end());
        allDq.insert(allDq.end(), dq.begin(), dq.end());
    }

    std::printf("TOTAL pairs=%llu  tris drawn=%llu behind=%llu offscreen=%llu unencodable=%llu\n",
                totalPairs, totalTris, behind, offscreen, unenc);
    if (!allDx.empty())
    {
        std::printf("      dx med=%+.3f  dy med=%+.3f  q med=%+.4f%%\n",
                    median(allDx), median(allDy), median(allDq));
        // A sixteenth of a pixel is 0.0625, so anything at or under that is
        // GS quantisation and nothing else.
        const double worst = std::max(std::fabs(median(allDx)), std::fabs(median(allDy)));
        std::printf("      verdict: %s\n",
                    worst <= 0.0625 ? "SUBPIXEL, transform agrees with VU1"
                                    : "MISMATCH, transform disagrees with VU1");
        return worst <= 0.0625 ? 0 : 1;
    }
    std::printf("      verdict: NO PAIRS, fixture carries no usable oracle\n");
    return 2;
}
