#ifndef GHPC_NATIVE_DRAW_H
#define GHPC_NATIVE_DRAW_H

#include <cstdint>

class GS;

// The Rnd seam's host-side transform. GH2 runs every mesh draw through VU1,
// and VU1 interpretation is about 80% of the busy thread
// (ghpc/notes/evidence/2026-09-11-vu1-profile.txt). The transform itself is
// solved to GS subpixel (2026-09-12-bone-palette.txt), so for a mesh whose
// geometry the host cache holds there is nothing VU1 does that this cannot.
//
// The seam is the MSCAL, not PsMesh::DrawFaces. DrawFaces also drives
// PsRnd::FlushPacket, which is what carries the material and texture GS
// registers out as GIF A+D through VIF1 DIRECT, so skipping the body would
// draw with the previous material and leave the scratchpad cursor growing
// with nothing draining it. By the time the MSCAL fires those registers are
// already in the GS and the geometry is already unpacked into VU1 memory, so
// substituting a host transform for the microprogram changes nothing else.
//
// Everything crosses by value or by a pointer the caller owns for the length
// of the draw, so this file depends on no symbol from ghpc/override/ and a
// stock runtime build still links.

struct GhpcNativeDrawArgs
{
    // RndMesh::Vert[], 64-byte stride: pos @0x00, norm @0x10, the four
    // Hmx::Color floats @0x20 (bone weights on a skinned mesh), uv @0x30.
    const uint8_t *verts = nullptr;
    uint32_t vertCount = 0;
    // RndMesh::Face[], three uint16 per face, stride 6, triangle list.
    const uint8_t *faces = nullptr;
    uint32_t faceCount = 0;

    // qw660..675, four bone matrices, rows 0..2 the Matrix3 and row 3 the
    // translation. Meaningful when skinned.
    float bone[4][4][4] = {};
    // qw676..679. The object matrix on an unskinned mesh; identity or bone 0
    // on a skinned one, where it is unused.
    float world[4][4] = {};
    bool skinned = false;

    // qw696..703 as PsCam::Select staged them: 696 viewport scale, 697
    // viewport offset, 700..703 the projection rows.
    float cam[8][4] = {};

    uint32_t mesh = 0; // owner address, for logging only
};

// Arm the next MSCAL. Copies args. Call once per PsMesh::DrawFaces entry.
void ghpcNativeDrawArm(const GhpcNativeDrawArgs &args);
void ghpcNativeDrawDisarm();

// Called from the MSCAL callback. Returns true when it drew, in which case
// the microprogram is skipped for that MSCAL. Consumes the arm, so a packet
// split across several chunks draws the mesh once.
bool ghpcNativeDrawMscal(GS &gs);

// 0 off, 1 draw natively and skip VU1, 2 transform but submit nothing and
// let VU1 run (a correctness arm that cannot change the picture).
int ghpcNativeDrawMode();

#endif
