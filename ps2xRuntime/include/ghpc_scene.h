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

        // The material state a draw needs before it is a picture rather than a
        // silhouette. PsMat::Select's body is the source for every field and
        // its override file pins each offset:
        //
        //   +0x148  TEST_1   GS test register image, ZTE at bit 16 and ZTST
        //                    at bits 17..18 (0 never, 1 always, 2 gequal,
        //                    3 greater)
        //   +0x158  ZBUF_1   ZMSK at bit 32: set means do NOT write depth
        //   +0x138  ALPHA_1  the blend equation
        //   +0x2c   mBlend   the engine's own blend mode, 1 meaning opaque
        //   +0x30   mColor   RGBA
        //
        // These are not decoration. A full-screen overlay on PS2 is drawn with
        // ZTST always and ZMSK set, so it covers the frame without touching
        // the depth buffer. A backend that ignores ZMSK writes depth for it
        // instead and loses everything behind it, which is exactly what the
        // first R2 picture did.
        struct Material
        {
            uint32_t ztst;     // 0 never, 1 always, 2 gequal, 3 greater
            bool depthTest;    // TEST_1 ZTE
            bool depthWrite;   // ZBUF_1 ZMSK, inverted
            bool blend;        // mBlend != 1
            float color[4];    // mColor
            uint32_t texKey;   // the PsTex this draw binds, 0 for none
        };

        void setMaterial(const Material &material);

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

        // ------------------------------------------------------------------
        // The consumer side.
        //
        // No STL here on purpose: this header is included by generated
        // translation units, and the collector's containers are its own
        // business.
        //
        // Threading. The submit side runs on the thread executing the guest
        // and closes its frame at vblank; the consumer is the host present
        // loop, a different thread running at its own rate. So the collector
        // publishes a completed frame and the consumer takes the latest one
        // that exists. A consumer can see the same frame twice, or miss one
        // entirely, and neither is a fault: presentation is not the guest's
        // frame boundary and this seam does not pretend otherwise.

        // One draw, as the consumer sees it. The geometry is not here because
        // it does not change per frame: `meshKey` names it and `geometry()`
        // hands it over once, which is what lets a backend key GPU buffers by
        // mesh and upload each one exactly once.
        struct DrawView
        {
            uint32_t meshKey;
            const float *matrices; // matrixCount * 16 floats, row major
            uint32_t matrixCount;  // 4 skinned (the bone palette), 1 not
            uint32_t vertCount;
            uint32_t indexCount;
            Material material;
        };

        // A backend declaring itself. This is what turns the seam on when
        // GHPC_RND_SCENE is not set, so `active()` stays a single load and the
        // overrides need no second gate.
        void requestDraws();

        // The latest completed frame, or false when none exists yet. The
        // pointers stay valid until the next call from the same thread.
        //
        // `serial` counts published frames, so a consumer can tell a new frame
        // from the one it already drew.
        bool acquireFrame(Camera &camera, bool &cameraValid,
                          const DrawView **draws, uint32_t &drawCount,
                          uint64_t &serial);

        // Geometry for a meshKey, kept for the life of the process because
        // the guest frees its own copy at PsMesh::Sync and there is nowhere
        // else to get it. Safe to call from the consumer thread.
        bool geometry(uint32_t meshKey,
                      const Vertex **verts, uint32_t &vertCount,
                      const uint16_t **indices, uint32_t &indexCount);

        // GS XYOFFSET for context 0, in pixels. The transform lands in the
        // GS's 2048-centred space and the rasteriser subtracts this before it
        // tests the scissor (gs_frontend.cpp:2137), so a native backend has to
        // subtract the same thing to land in the same place. Read from the GS
        // rather than assumed: a gameplay sample is ofx=1984 ofy=1920, not
        // half the framebuffer.
        void setGsOffset(float ofx, float ofy);
        void gsOffset(float &ofx, float &ofy);
    }
}
