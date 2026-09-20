#ifndef GHPC_TRANSFORM_H
#define GHPC_TRANSFORM_H

// The host-side vertex transform, on its own and with no runtime dependency,
// so the replay tool under ghpc/tools/ runs exactly the code the game runs
// rather than a copy of it that drifts.
//
// Measured to GS subpixel over six meshes and 171 matched pairs
// (ghpc/notes/evidence/2026-09-12-bone-palette.txt):
//
//     world = sum over b of weight[b] * (pos * Bone[b])   skinned
//     world = pos * World                                 unskinned, qw676..679
//     clip  = world * M              M = qw700..703 as rows, row vectors
//     q     = 1 / clip.w
//     x,y,z = clip.{x,y,z} * q * qw696.{x,y,z} + qw697.{x,y,z}
//     s,t   = uv * q

#include "runtime/gs/ghpc_native_draw.h"

#include <cstdint>
#include <cstring>

struct GhpcVec4
{
    float x, y, z, w;
};

struct GhpcScreen
{
    float x, y;
    double z;
    float q, s, t;
    bool valid;
};

// out = in * M with in.w implied 1, so row 3 of M is the translation. Rows
// 0..2 carry stale packet content in their w lane, so only xyz is read.
inline GhpcVec4 ghpcRowMul(const float p[3], const float m[4][4])
{
    return GhpcVec4{
        p[0] * m[0][0] + p[1] * m[1][0] + p[2] * m[2][0] + m[3][0],
        p[0] * m[0][1] + p[1] * m[1][1] + p[2] * m[2][1] + m[3][1],
        p[0] * m[0][2] + p[1] * m[1][2] + p[2] * m[2][2] + m[3][2],
        0.0f};
}

// clip = world * M, M = qw700..703. Here the w lane matters: it is what q
// divides by.
inline GhpcVec4 ghpcProject(const GhpcVec4 &w, const float cam[8][4])
{
    return GhpcVec4{
        w.x * cam[4][0] + w.y * cam[5][0] + w.z * cam[6][0] + cam[7][0],
        w.x * cam[4][1] + w.y * cam[5][1] + w.z * cam[6][1] + cam[7][1],
        w.x * cam[4][2] + w.y * cam[5][2] + w.z * cam[6][2] + cam[7][2],
        w.x * cam[4][3] + w.y * cam[5][3] + w.z * cam[6][3] + cam[7][3]};
}

// v is one RndMesh::Vert, 64 bytes: pos @0x00, norm @0x10, the four
// Hmx::Color floats @0x20 (bone weights when skinned), uv @0x30.
inline GhpcScreen ghpcTransformVert(const uint8_t *v, const GhpcNativeDrawArgs &a)
{
    float f[16];
    std::memcpy(f, v, sizeof(f));

    GhpcVec4 world;
    if (a.skinned)
    {
        world = GhpcVec4{0.0f, 0.0f, 0.0f, 0.0f};
        for (int b = 0; b < 4; ++b)
        {
            const float wt = f[8 + b];
            if (wt == 0.0f)
                continue;
            const GhpcVec4 p = ghpcRowMul(f, a.bone[b]);
            world.x += wt * p.x;
            world.y += wt * p.y;
            world.z += wt * p.z;
        }
    }
    else
    {
        world = ghpcRowMul(f, a.world);
    }

    const GhpcVec4 clip = ghpcProject(world, a.cam);

    GhpcScreen s{};
    // VU1 clips against the near plane and this does not, so a vertex at or
    // behind the eye has no screen position at all. 1/w explodes rather than
    // wrapping, which would smear one triangle across the frame.
    if (!(clip.w > 1.0e-4f))
    {
        s.valid = false;
        return s;
    }
    const float q = 1.0f / clip.w;
    s.x = clip.x * q * a.cam[0][0] + a.cam[1][0];
    s.y = clip.y * q * a.cam[0][1] + a.cam[1][1];
    s.z = (double)(clip.z * q * a.cam[0][2] + a.cam[1][2]);
    s.q = q;
    // The submit is fst=0, so the GS gets ST/Q rather than UV.
    s.s = f[12] * q;
    s.t = f[13] * q;
    s.valid = true;
    return s;
}

// True when the vertex fits the 12.4 XYZ2 encoding at all. Outside it the
// 16-bit field wraps and the vertex lands somewhere arbitrary on screen.
inline bool ghpcXyFits(const GhpcScreen &s)
{
    const float fx = s.x * 16.0f;
    const float fy = s.y * 16.0f;
    return fx >= 0.0f && fy >= 0.0f && fx <= 65535.0f && fy <= 65535.0f;
}

#endif
