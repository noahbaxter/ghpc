#pragma once

#include <cstdint>

// The Rnd seam's submission contract.
//
// The override bodies under ghpc/override/ are the only code that can see the
// guest's draw state, and they run inside generated translation units that
// know nothing about the renderer. This is the narrow interface between them:
// the overrides push what they observe, a backend consumes it, and neither
// includes the other's headers.
//
// Everything here is measured, not assumed. Sources:
//   notes/rnd-seam.md                       the four inputs and where they live
//   evidence/2026-09-12-bone-palette.txt    the vertex transform, to GS subpixel
//   evidence/2026-09-19-vu1-tl-semantics.md what VU1 actually does with them
//
// The geometry a backend gets does NOT come from the guest at draw time.
// PsMesh::Sync frees the engine-level vectors, so every mesh reports
// verts=0 faces=0 by the time DrawFaces runs. It comes from the host mesh
// cache in the Sync override, which copies them out before they die.
namespace ghpc
{
    namespace scene
    {
        // RndMesh::Vert is 64 bytes. Only these fields are read.
        //
        // `weights` is the Vert's Hmx::Color at +0x20, and on a skinned mesh it
        // is not a colour: the four floats are bone weights summing to 1.0,
        // indexing the palette in order. That misreading is worth guarding
        // against, which is why the field is not called `color`.
        struct Vertex
        {
            float pos[3];     // +0x00
            float norm[3];    // +0x10
            float weights[4]; // +0x20
            float uv[2];      // +0x30
        };

        // The projection PsCam::Select uploads, the only guest writer of
        // VU1 qw696-703.
        //
        //   clip  = world * m            m is qw700..703 as rows, row vectors
        //   q     = 1 / clip.w
        //   x,y,z = clip.xyz * q * scale + offset
        //   s,t   = uv * q
        //
        // qw698 is not subtracted anywhere: m already carries the view
        // transform.
        struct Camera
        {
            float m[16];     // qw700..703, row major
            float scale[3];  // qw696
            float offset[3]; // qw697
        };

        // Whether a backend is installed and wants draws. The overrides check
        // this first, so with no backend the cost is one relaxed load and the
        // guest path is untouched.
        bool active();

        void beginFrame();
        void setCamera(const Camera &camera);

        // DrawShowing picks one of these per mesh, on mBones at +0x13c.
        //
        // Skinned is the common case on gameplay geometry: a 20-quadword
        // upload at VU1 660, of which qw660..675 are four bone matrices that
        // Vertex::weights blends. Unskinned writes one matrix at qw676..679
        // and the vertex is transformed by it directly.
        void setBonePalette(const float *matrices, uint32_t matrixCount);
        void setWorldMatrix(const float m[16]);

        // Keyed by the owning PsMesh, because instances draw through their
        // owner's packet: `this` in DrawFaces is mOwner at +0x138. The backend
        // uses the key to cache GPU buffers across frames.
        //
        // `verts` is vertCount Vertex structs. `indices` is indexCount uint16,
        // a triangle list: RndMesh::Face is three unsigned shorts, stride 6.
        // A triangle list is a correct substitute for the PS2 strips because
        // there is no backface culling anywhere in VU1's 13,872 bytes.
        void submitMesh(uint32_t meshKey,
                        const Vertex *verts, uint32_t vertCount,
                        const uint16_t *indices, uint32_t indexCount);

        void endFrame();
    }
}
