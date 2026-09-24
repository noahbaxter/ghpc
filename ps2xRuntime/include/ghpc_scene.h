#pragma once

#include <cstdint>
#include <vector>

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
        //   +0x148  TEST_1   GS test register image. ZTST is at bits 17..18
        //                    (0 never, 1 always, 2 gequal, 3 greater). ZTE at
        //                    bit 16 is NOT in it: the mask the body passes to
        //                    SetRegister is 0x6FFFF, which leaves bit 16 out,
        //                    so depth testing is on globally and only ZTST
        //                    varies per material. Reading bit 16 anyway
        //                    reports every draw as depth-test-off.
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
            bool depthTest;    // always true today, see the ZTE note above
            bool depthWrite;   // ZBUF_1 ZMSK, inverted
            bool blend;        // mBlend != 1
            float color[4];    // mColor
            uint32_t texKey;   // the PsTex this draw binds, 0 for none

            // The alpha test, TEST_1 bits 0..13. Carried but not yet honoured
            // by any backend, and measured for a reason: the native path
            // covers about 45k pixels per frame that the software path leaves
            // empty, and geometry the GS punches out arriving solid is the
            // leading explanation. Implementing it needs the texture's alpha,
            // so it cannot land before texture does.
            //
            //   ate   bit 0, enable
            //   atst  bits 1..3: 0 never, 1 always, 2 less, 3 lequal,
            //         4 equal, 5 gequal, 6 greater, 7 notequal
            //   aref  bits 4..11, the reference value
            //   afail bits 12..13: 0 keep, 1 fb only, 2 zb only, 3 rgb only
            bool alphaTest;
            uint32_t atst;
            uint32_t aref;
            uint32_t afail;

            // +0x130, the value PsMat::Select hands PsTex::Select, which packs
            // it into TEX0 bits 35..36: 0 modulate, 1 decal, 2 highlight,
            // 3 highlight2. TCC is 1 on every select the census saw, so the
            // texture's alpha always takes part.
            uint32_t tfx;
            // +0x150 CLAMP_1 WMS bits 0..1 and WMT bits 2..3, 0 repeat and
            // 1 clamp. The mask the body passes is 0xF, so the region fields
            // are not the material's and the region modes cannot arise.
            uint32_t wrapS;
            uint32_t wrapT;

            // +0x138 ALPHA_1, the blend equation ((A - B) * C >> 7) + D:
            // A, B, D at bits 0..1, 2..3, 6..7 (0 Cs, 1 Cd, 2 zero), C at
            // bits 4..5 (0 As, 1 Ad, 2 FIX), FIX at bits 32..39. Only in
            // effect when `blend`: VU1 sets ABE for every mBlend but 1.
            uint32_t alphaEq; // bits 0..7
            uint32_t alphaFix;

            // The vertex colour, as qw688 hands it to VU1 (PsMat::Select
            // 0x43ec78..0x43ecf4):
            //   +0x124 == 0          program 0x614, a bare return: the
            //                        vertex keeps Vert+0x20 as its colour
            //   else !mUseEnviron    0x7C5: mColor, or Vert+0x20 if prelit
            //   else                 the environ's program, 0x440E24
            // then the T&L loop multiplies by qw695 (+0x160) for GS units.
            uint32_t lightProgram;  // 0x614, 0x7C5, 0x6EC or 0x436
            bool prelit;            // qw688.y, +0x9c
            bool useEnviron;        // qw688.w, +0x40
            float rgbaScale[4];     // qw695, +0x160
            // qw690 as VU1 gets it: mColor, with w replaced by 0.035 when
            // TFX is highlight. `color` above stays the raw mColor.
            float vuColor[4];

            // The material program, qw689.x, from +0x12c: what VU1 does to
            // the UVs before T&L. 0x614 leaves them; 0x17c (overlay 0x0be0)
            // is the texture transform, uv' = r3.xy + r0.xy * u + r1.xy * v
            // with the rows of `texXfm`, +0x170 (qw691..694); 0x347 is the
            // sphere map, which generates them and is not reproduced.
            uint32_t uvProgram;
            float texXfm[16];
        };

        // PsEnviron::Select's light block, qw681..687, and the lighting
        // program it chose. Latched per draw like the camera.
        //   qw[0]     ambient
        //   qw[1..3]  light colours
        //   qw[4..6]  rows of a 3x3 whose columns are the light directions
        //             (normalised, negated), or positions for program 0x436
        struct Lights
        {
            float qw[7][4];
            uint32_t program;
        };
        void setLights(const Lights &lights);

        // qw676..678, the 3x3 VU1's lighting programs take the object normal
        // through. The unskinned world matrix, and on a skinned draw the
        // four quadwords after the bone palette in the same 20-qw upload.
        void setLightMatrix(const float m[16]);

        void setMaterial(const Material &material);

        // Where a draw lands on the GS: the render target and its offset, as
        // they stand in PsRnd's register shadow when the draw is submitted.
        // SetRegister keeps every GS register it sends at ThePsRnd (0x4f2780)
        // + 0xF0 + reg * 8, and FRAME_1 and XYOFFSET_1 change per render
        // target (PsRnd::MakeDrawTarget, PsTex::MakeDrawTarget), not per
        // frame. A 128-wide offscreen target runs at XYOFFSET 1984/1920 while
        // the main frame runs at 1792/1824, so one offset for the frame puts
        // offscreen draws on screen 192 by 96 pixels out.
        struct Target
        {
            uint32_t fbp;       // FRAME_1 bits 0..8, in 2048-word pages
            uint32_t fbw;       // bits 16..21, in 64-pixel units
            uint32_t psm;       // bits 24..29
            float ofx;          // XYOFFSET_1, pixels
            float ofy;
            uint32_t scissor[4]; // SCISSOR_1 x0, x1, y0, y1
        };
        void setTarget(const Target &target);

        // The texture a material binds, as the engine holds it: PsTex's
        // RndBitmap at +0x28, which stays in guest RAM for the life of the
        // texture because PsTex::LoadVram re-uploads from it whenever the
        // VRAM allocator evicts. Linear rows, not GS-swizzled, so decoding it
        // needs no GS state and does not race the packet path.
        //
        // Measured on a driven run, 2026-09-21: PSMT8 58%, PSMT4 35%,
        // PSMCT16 6%, no PSMCT32; CPSM 0 (a CT32 palette) and CSM 0 on every
        // select; mOrder 3 on every bitmap sampled, bit 0 RGBA and bit 1 the
        // CSM1 index swizzle RndBitmap::PaletteOffset applies.
        struct TextureSource
        {
            uint32_t width;       // RndBitmap +0x00
            uint32_t height;      // +0x02
            uint32_t rowBytes;    // +0x04
            uint32_t bpp;         // +0x06: 4, 8, 16 or 32
            uint32_t order;       // +0x08
            uint32_t pixelsAddr;  // +0x0c, guest address, the cache signature
            uint32_t paletteAddr; // +0x10
            const uint8_t *pixels;  // host pointers to the same bytes
            const uint8_t *palette; // null when bpp > 8
            uint32_t tw;          // TEX0 TW and TH, log2 of the GS's texture
            uint32_t th;          // space, which the UVs are normalised to
            bool renderTarget;    // PsTex +0x48 & 8: drawn by the GS, no bitmap
        };

        // Decoded once per distinct bitmap and kept. Called from the material
        // bind; a texKey seen before with the same signature costs one lookup.
        void setTexture(uint32_t texKey, const TextureSource &source);

        // The consumer side of the texture cache. `version` changes when the
        // PsTex at texKey starts naming a different bitmap, so a backend
        // re-uploads on a changed version and never otherwise. `uvScale`
        // takes the vertex UV from the GS's 2^TW space into this image.
        bool textureInfo(uint32_t texKey, uint32_t &width, uint32_t &height,
                         uint64_t &version, float uvScale[2]);
        // RGBA8, width * height * 4 bytes, alpha in the GS's sense where 0x80
        // is 1.0. False when the version moved on since textureInfo.
        bool copyTexture(uint32_t texKey, uint64_t version, void *dst, uint64_t bytes);

        // A PsTex the GS draws into (+0x48 & 8), which has no bitmap.
        // PsTex::Select points its TBP0 at the render target's frame, (FBP &
        // 0x1ff) << 5, so `fbp` names the offscreen target a backend renders
        // that frame's draws into. width and height are PsTex +0x4c/+0x50,
        // tw and th its TEX0's, which the UVs are normalised to.
        void setRenderTargetTexture(uint32_t texKey, uint32_t fbp, uint32_t width, uint32_t height,
                                    uint32_t tw, uint32_t th, uint32_t psm);
        bool renderTargetTexture(uint32_t texKey, uint32_t &fbp, uint32_t &width, uint32_t &height,
                                 float uvScale[2], uint32_t &psm);
        // TEXA as setGsTexa last stored it: ta0 | ta1 << 8 | aem << 16.
        uint32_t gsTexa();

        // GS TEXA, which is what a 16-bit texel's alpha bit selects between.
        void setGsTexa(uint32_t ta0, uint32_t ta1, bool aem);

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
        //
        // `generation` is meshcache::generation(meshKey). The key is a guest
        // address the game reuses once a menu unloads, so geometry and GPU
        // buffers are replaced whenever the generation moves.
        void submitMesh(uint32_t meshKey, uint32_t generation,
                        const Vertex *verts, uint32_t vertCount,
                        const uint16_t *indices, uint32_t indexCount);

        // Geometry that changes every frame: a mutable mesh (+0x140 & 0x1f),
        // whose vectors Sync leaves live instead of freeing, so it is never
        // in the host mesh cache. Copied into the frame rather than cached,
        // and handed to the backend as DrawView::dynamicVerts.
        void submitDynamicMesh(uint32_t meshKey,
                               const Vertex *verts, uint32_t vertCount,
                               const uint16_t *indices, uint32_t indexCount);

        void endFrame();

        // Where a frame closes. The guest's own boundary is PsRnd::EndDrawing,
        // which calls guestFrameEnd. vblank closes a frame only when the guest
        // has not for kGuestBoundaryVblanks, so a screen that never reaches
        // EndDrawing still publishes. GHPC_SCENE_BOUNDARY=vblank closes on
        // every vblank, which splits a guest frame a vblank lands inside.
        void guestFrameEnd();
        void vblank();

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
            uint32_t generation; // GPU buffers for another generation are stale
            const float *matrices; // matrixCount * 16 floats, row major
            uint32_t matrixCount;  // 4 skinned (the bone palette), 1 not
            // The camera this draw was submitted under. Per draw, not per
            // frame: GH2 draws the venue and the track through different
            // cameras in one frame, and one frame-wide camera put the whole
            // venue through the track's, into a strip at the top of the
            // screen behind two surfaces that covered 70% of it.
            const Camera *camera;
            Target target;
            const Lights *lights;     // null before any PsEnviron::Select
            const float *lightMatrix; // 16 floats, row major, never null
            // Set for a submitDynamicMesh draw, null for a cached one; then
            // vertCount and indexCount describe these arrays.
            const Vertex *dynamicVerts;
            const uint16_t *dynamicIndices;
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

        // Geometry for a meshKey, kept because the guest frees its own copy
        // at PsMesh::Sync and there is nowhere else to get it. Copied out
        // under the lock, since submitMesh replaces it when the key's
        // generation moves. Safe to call from the consumer thread.
        bool geometry(uint32_t meshKey, std::vector<Vertex> &verts,
                      std::vector<uint16_t> &indices, uint32_t &generation);

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
