#include "ghpc_scene.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

// Collector for the Rnd seam.
//
// Two jobs. It counts what arrives, which is what GHPC_RND_SCENE=1 was built
// for and which draws nothing so it cannot regress the guest. And it retains
// what a backend needs: the per-draw matrices, and the geometry, keyed by
// mesh.
//
// Geometry is retained separately from the frame and never freed, because the
// guest frees its own copy at PsMesh::Sync and there is nowhere else to get it
// back. A frame therefore carries only matrices and a mesh key, which is a few
// hundred bytes a draw instead of the 42k verts a gameplay frame submits.
//
// The guest path stays untouched when nothing asked for draws: `active()` is
// one relaxed load.
namespace ghpc
{
    namespace scene
    {
        namespace
        {
            // Matrices live in the frame's own arena rather than inside the
            // draw, so a draw stays small and a skinned palette does not cost
            // 256 bytes in every unskinned one. DrawView::matrices is patched
            // to point into the arena when the frame is published, since the
            // arena reallocates while it fills. Cameras the same way.
            constexpr uint32_t kNoCamera = 0xFFFFFFFFu;

            struct PendingDraw
            {
                uint32_t meshKey = 0u;
                uint32_t generation = 0u;
                uint32_t matrixOffset = 0u; // floats into Frame::matrices
                uint32_t matrixCount = 0u;
                uint32_t vertCount = 0u;
                uint32_t indexCount = 0u;
                uint32_t cameraIndex = kNoCamera; // into Frame::cameras
                uint32_t lightsIndex = kNoCamera; // into Frame::lights, same sentinel
                bool dynamic = false;             // geometry in Frame::dynVerts
                uint32_t dynVertOffset = 0u;
                uint32_t dynIndexOffset = 0u;
                Material material{};
                Target target{};
            };

            // What a draw gets when no PsMat::Select has run yet. Depth test
            // and write on with the GS's own sense, opaque, white. It is
            // stated rather than left zeroed because a zeroed Material means
            // ZTST never, which draws nothing and looks like a broken seam.
            Material defaultMaterial()
            {
                Material m{};
                m.ztst = 2u; // gequal
                m.depthTest = true;
                m.depthWrite = true;
                m.blend = false;
                m.color[0] = m.color[1] = m.color[2] = m.color[3] = 1.0f;
                m.texKey = 0u;
                // No alpha test, and atst ALWAYS to match, so a zeroed atst
                // cannot read as ATST never and silently mean "draw nothing".
                m.alphaTest = false;
                m.atst = 1u;
                m.aref = 0u;
                m.afail = 0u;
                m.tfx = 0u;
                m.wrapS = 0u;
                m.wrapT = 0u;
                // (Cs - Cd) * As + Cd, the ordinary source-over.
                m.alphaEq = 0x44u;
                m.alphaFix = 0x80u;
                // Unlit and white: the ambient program with no environment.
                m.lightProgram = 0x7C5u;
                m.prelit = false;
                m.useEnviron = false;
                for (uint32_t k = 0; k < 4u; ++k)
                {
                    m.rgbaScale[k] = 128.0f;
                    m.vuColor[k] = 1.0f;
                }
                m.uvProgram = 0x614u;
                for (uint32_t k = 0; k < 16u; ++k)
                {
                    m.texXfm[k] = (k % 5u == 0u) ? 1.0f : 0.0f;
                }
                return m;
            }

            struct Frame
            {
                std::vector<PendingDraw> draws;
                std::vector<float> matrices;
                std::vector<DrawView> views; // built at publish
                std::vector<Camera> cameras; // one per change, in draw order
                std::vector<Lights> lights;  // the same, for PsEnviron
                std::vector<Vertex> dynVerts;     // submitDynamicMesh geometry
                std::vector<uint16_t> dynIndices;
                Camera camera{};             // the last, for the frame-wide view
                bool cameraSeen = false;
                uint32_t boneUploads = 0u;
                uint32_t worldUploads = 0u;
                uint32_t materialBinds = 0u;
                uint64_t serial = 0ull;

                void clear()
                {
                    draws.clear();
                    matrices.clear();
                    views.clear();
                    cameras.clear();
                    lights.clear();
                    dynVerts.clear();
                    dynIndices.clear();
                    cameraSeen = false;
                    boneUploads = 0u;
                    worldUploads = 0u;
                    materialBinds = 0u;
                }
            };

            // A view's pointers into its frame's arenas, set from the offsets
            // whenever the frame has changed hands.
            void pointViews(Frame &f)
            {
                for (size_t i = 0; i < f.draws.size(); ++i)
                {
                    const PendingDraw &d = f.draws[i];
                    f.views[i].matrices = f.matrices.data() + d.matrixOffset;
                    f.views[i].camera = (d.cameraIndex < f.cameras.size())
                                            ? &f.cameras[d.cameraIndex]
                                            : nullptr;
                    f.views[i].lights = (d.lightsIndex < f.lights.size())
                                            ? &f.lights[d.lightsIndex]
                                            : nullptr;
                    // Stored straight after the draw's own matrices.
                    f.views[i].lightMatrix = f.matrices.data() + d.matrixOffset + (size_t)d.matrixCount * 16u;
                    f.views[i].dynamicVerts = d.dynamic ? f.dynVerts.data() + d.dynVertOffset : nullptr;
                    f.views[i].dynamicIndices = d.dynamic ? f.dynIndices.data() + d.dynIndexOffset : nullptr;
                }
            }

            struct MeshGeometry
            {
                std::vector<Vertex> verts;
                std::vector<uint16_t> indices;
                uint32_t generation = 0u;
            };

            // What makes two binds of one PsTex the same image. The guest
            // pointers rather than a content hash, because hashing a 256x256
            // bitmap on each of about 260 material binds a frame is the kind
            // of cost this phase exists to remove. The gap that leaves: a
            // bitmap rewritten in place keeps its pointers and is not seen.
            struct TextureSignature
            {
                uint32_t pixelsAddr = 0u;
                uint32_t paletteAddr = 0u;
                uint32_t width = 0u;
                uint32_t height = 0u;
                uint32_t bpp = 0u;
                uint32_t order = 0u;
                uint32_t tw = 0u;
                uint32_t th = 0u;
                bool renderTarget = false;

                bool operator==(const TextureSignature &o) const
                {
                    return pixelsAddr == o.pixelsAddr && paletteAddr == o.paletteAddr &&
                           width == o.width && height == o.height && bpp == o.bpp &&
                           order == o.order && tw == o.tw && th == o.th &&
                           renderTarget == o.renderTarget;
                }
            };

            struct TextureEntry
            {
                TextureSignature signature;
                uint64_t version = 0ull;
                // Empty when the bitmap could not be decoded: a render target,
                // a null pixel pointer, or a depth this does not handle.
                std::vector<uint8_t> rgba;
                uint32_t width = 0u;
                uint32_t height = 0u;
                float uvScale[2] = {1.0f, 1.0f};
            };

            struct State
            {
                // Submit side, guest thread only.
                Frame building;
                float pendingMatrices[64]{}; // up to four 4x4
                uint32_t pendingMatrixCount = 0u;
                // Latched, not per-draw: PsMat::Select binds a material and
                // the draws that follow inherit it until the next Select,
                // which is how the PS2 path works too.
                Material pendingMaterial = defaultMaterial();
                Target pendingTarget{};
                // Not reset per frame: an environment stays selected until
                // the next PsEnviron::Select, whichever frame that is in.
                Lights currentLights{};
                bool hasLights = false;
                float pendingLightMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

                // Published, under `publishMutex`: an ordered queue rather
                // than a latest-only slot. The guest's vblank and the display
                // are two 60 Hz clocks, and latest-only showed a frame twice
                // and dropped the next every time their phases crossed, 12%
                // of presents. Frames go through in order; the cap only
                // trims when one clock has really drifted from the other.
                std::deque<Frame> published;
                std::vector<Frame> pool; // recycled, so arenas keep capacity
                std::mutex publishMutex;

                // Consumer side, consumer thread only.
                Frame consuming;
                bool hasConsuming = false;

                // Geometry, written by the guest thread and read by the
                // consumer. Additive and never erased, so the only hazard is
                // a rehash racing a lookup; the mutex covers that and costs
                // nothing, because a lookup happens on a backend cache miss
                // and there are about a thousand of those in a whole run.
                std::unordered_map<uint32_t, MeshGeometry> geometry;
                std::mutex geometryMutex;

                // Textures, keyed by the PsTex. Unlike geometry an entry can
                // be replaced, when a PsTex starts naming another bitmap, so
                // the consumer copies out under the lock rather than holding
                // a pointer.
                std::unordered_map<uint32_t, TextureEntry> textures;
                std::mutex textureMutex;
                uint64_t textureVersions = 0ull;
                uint32_t texDecoded = 0u;
                uint32_t texRedecoded = 0u;
                uint32_t texRenderTarget = 0u;
                uint32_t texNoPixels = 0u;
                uint32_t texUnsupported = 0u;
                uint32_t texOddOrder = 0u;
                uint32_t texScaled = 0u;

                std::atomic<float> ofx{0.0f};
                std::atomic<float> ofy{0.0f};
                std::atomic<uint32_t> texa{0u}; // ta0 | ta1 << 8 | aem << 16
                std::atomic<bool> texaSeen{false};
            };

            State &state()
            {
                static State s;
                return s;
            }

            std::atomic<bool> &requested()
            {
                static std::atomic<bool> v{false};
                return v;
            }

            bool envActive()
            {
                static const bool on = std::getenv("GHPC_RND_SCENE") != nullptr;
                return on;
            }

            int logEvery()
            {
                static const int every = []() {
                    const char *e = std::getenv("GHPC_RND_SCENE_LOG");
                    return e ? std::atoi(e) : 60;
                }();
                return every;
            }

            // RndBitmap to RGBA8, alpha left in the GS's sense (0x80 is 1.0)
            // so the shader can run the GS's own arithmetic on it.
            //
            // Indexed depths look up a CT32 palette of RGBA bytes, which is
            // mOrder bit 0. Bit 1 is the CSM1 swizzle RndBitmap::PaletteOffset
            // (0x1d75e0) undoes: an 8-bit index whose bits 3..4 are 01 or 10
            // has them exchanged. 16-bit texels are the GS's 5551 with R low,
            // expanded by shifting as the GS does, and the alpha bit picks
            // TA1 or TA0 out of TEXA.
            bool decodeBitmap(const TextureSource &src, uint32_t texa, std::vector<uint8_t> &out)
            {
                const uint32_t w = src.width;
                const uint32_t h = src.height;
                if (src.pixels == nullptr || w == 0u || h == 0u)
                {
                    return false;
                }
                const bool indexed = src.bpp == 4u || src.bpp == 8u;
                if ((indexed && src.palette == nullptr) ||
                    (!indexed && src.bpp != 16u && src.bpp != 32u))
                {
                    return false;
                }

                const uint8_t ta0 = (uint8_t)(texa & 0xFFu);
                const uint8_t ta1 = (uint8_t)((texa >> 8) & 0xFFu);
                const bool aem = ((texa >> 16) & 1u) != 0u;
                const bool swizzled = (src.order & 2u) != 0u;

                out.resize((size_t)w * h * 4u);
                uint8_t *dst = out.data();
                for (uint32_t y = 0u; y < h; ++y)
                {
                    const uint8_t *row = src.pixels + (size_t)y * src.rowBytes;
                    for (uint32_t x = 0u; x < w; ++x, dst += 4)
                    {
                        if (indexed)
                        {
                            uint32_t index;
                            if (src.bpp == 4u)
                            {
                                const uint8_t pair = row[x >> 1];
                                index = (x & 1u) ? (uint32_t)(pair >> 4) : (uint32_t)(pair & 0xFu);
                            }
                            else
                            {
                                index = row[x];
                                if (swizzled)
                                {
                                    const uint32_t bits = index & 0x18u;
                                    if (bits == 0x08u)
                                    {
                                        index += 8u;
                                    }
                                    else if (bits == 0x10u)
                                    {
                                        index -= 8u;
                                    }
                                }
                            }
                            std::memcpy(dst, src.palette + (size_t)index * 4u, 4u);
                        }
                        else if (src.bpp == 16u)
                        {
                            const uint32_t v = (uint32_t)row[x * 2u] | ((uint32_t)row[x * 2u + 1u] << 8);
                            dst[0] = (uint8_t)((v & 0x1Fu) << 3);
                            dst[1] = (uint8_t)(((v >> 5) & 0x1Fu) << 3);
                            dst[2] = (uint8_t)(((v >> 10) & 0x1Fu) << 3);
                            const bool rgbZero = (v & 0x7FFFu) == 0u;
                            dst[3] = (v & 0x8000u) ? ta1 : ((aem && rgbZero) ? 0u : ta0);
                        }
                        else
                        {
                            std::memcpy(dst, row + (size_t)x * 4u, 4u);
                        }
                    }
                }
                return true;
            }

            // GHPC_TEX_DUMP=<dir> writes the first decoded textures as PPM,
            // colour composited over a checker by its alpha, so a wrong
            // palette swizzle, nibble order or alpha scale is visible by
            // looking. Capped, because a run decodes hundreds.
            void dumpTexture(uint32_t texKey, const TextureSource &src, const std::vector<uint8_t> &rgba)
            {
                static const char *dir = std::getenv("GHPC_TEX_DUMP");
                static int dumped = 0;
                if (dir == nullptr || dumped >= 64)
                {
                    return;
                }
                ++dumped;
                char path[512];
                std::snprintf(path, sizeof(path), "%s/tex_%08x_%ux%u_%ubpp.ppm",
                              dir, texKey, src.width, src.height, src.bpp);
                FILE *f = std::fopen(path, "wb");
                if (f == nullptr)
                {
                    return;
                }
                std::fprintf(f, "P6\n%u %u\n255\n", src.width, src.height);
                std::vector<uint8_t> line((size_t)src.width * 3u);
                for (uint32_t y = 0u; y < src.height; ++y)
                {
                    for (uint32_t x = 0u; x < src.width; ++x)
                    {
                        const uint8_t *p = rgba.data() + ((size_t)y * src.width + x) * 4u;
                        const float a = std::min(1.0f, (float)p[3] / 128.0f);
                        const float bg = (((x >> 3) ^ (y >> 3)) & 1u) ? 200.0f : 90.0f;
                        for (uint32_t c = 0u; c < 3u; ++c)
                        {
                            line[(size_t)x * 3u + c] = (uint8_t)((float)p[c] * a + bg * (1.0f - a));
                        }
                    }
                    std::fwrite(line.data(), 1u, line.size(), f);
                }
                std::fclose(f);
            }
        }

        bool active()
        {
            return envActive() || requested().load(std::memory_order_relaxed);
        }

        void requestDraws()
        {
            requested().store(true, std::memory_order_relaxed);
        }

        void beginFrame()
        {
            if (!active())
            {
                return;
            }
            state().building.clear();
            state().pendingMatrixCount = 0u;
            state().pendingMaterial = defaultMaterial();
        }

        void setMaterial(const Material &material)
        {
            if (!active())
            {
                return;
            }
            state().pendingMaterial = material;
            ++state().building.materialBinds;
        }

        void setLights(const Lights &lights)
        {
            if (!active())
            {
                return;
            }
            state().currentLights = lights;
            state().hasLights = true;
        }

        void setLightMatrix(const float m[16])
        {
            if (!active() || m == nullptr)
            {
                return;
            }
            std::memcpy(state().pendingLightMatrix, m, 16u * sizeof(float));
        }

        void setTarget(const Target &target)
        {
            if (!active())
            {
                return;
            }
            state().pendingTarget = target;
        }

        void setCamera(const Camera &camera)
        {
            if (!active())
            {
                return;
            }
            // Appended only when it changes, since it is re-sent before every
            // draw and changes a handful of times a frame.
            Frame &f = state().building;
            if (f.cameras.empty() || std::memcmp(&f.cameras.back(), &camera, sizeof(Camera)) != 0)
            {
                f.cameras.push_back(camera);
            }
            f.camera = camera;
            f.cameraSeen = true;
        }

        void setBonePalette(const float *matrices, uint32_t matrixCount)
        {
            if (!active() || matrices == nullptr || matrixCount == 0u)
            {
                return;
            }
            State &s = state();
            if (matrixCount > 4u)
            {
                matrixCount = 4u;
            }
            std::memcpy(s.pendingMatrices, matrices, (size_t)matrixCount * 16u * sizeof(float));
            s.pendingMatrixCount = matrixCount;
            ++s.building.boneUploads;
        }

        void setWorldMatrix(const float *m)
        {
            if (!active() || m == nullptr)
            {
                return;
            }
            State &s = state();
            std::memcpy(s.pendingMatrices, m, 16u * sizeof(float));
            s.pendingMatrixCount = 1u;
            ++s.building.worldUploads;
        }

        PendingDraw &pushDraw(uint32_t meshKey, uint32_t vertCount, uint32_t indexCount, bool dynamic);

        void submitMesh(uint32_t meshKey, uint32_t generation,
                        const Vertex *verts, uint32_t vertCount,
                        const uint16_t *indices, uint32_t indexCount)
        {
            if (!active() || verts == nullptr || vertCount == 0u)
            {
                return;
            }

            State &s = state();

            // A draw with no matrix cannot be transformed, so it is not a draw
            // a backend can take. Dropping it here keeps the count honest
            // rather than handing the backend an identity and a wrong picture.
            if (s.pendingMatrixCount == 0u)
            {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(s.geometryMutex);
                auto it = s.geometry.find(meshKey);
                if (it == s.geometry.end() || it->second.generation != generation)
                {
                    MeshGeometry g;
                    g.verts.assign(verts, verts + vertCount);
                    if (indices != nullptr && indexCount != 0u)
                    {
                        g.indices.assign(indices, indices + indexCount);
                    }
                    g.generation = generation;
                    s.geometry[meshKey] = std::move(g);
                }
            }

            pushDraw(meshKey, vertCount, (indices != nullptr) ? indexCount : 0u, false).generation = generation;
        }

        void submitDynamicMesh(uint32_t meshKey,
                               const Vertex *verts, uint32_t vertCount,
                               const uint16_t *indices, uint32_t indexCount)
        {
            if (!active() || verts == nullptr || vertCount == 0u || indices == nullptr || indexCount == 0u)
            {
                return;
            }
            State &s = state();
            if (s.pendingMatrixCount == 0u)
            {
                return;
            }
            PendingDraw &d = pushDraw(meshKey, vertCount, indexCount, true);
            d.dynVertOffset = (uint32_t)s.building.dynVerts.size();
            d.dynIndexOffset = (uint32_t)s.building.dynIndices.size();
            s.building.dynVerts.insert(s.building.dynVerts.end(), verts, verts + vertCount);
            s.building.dynIndices.insert(s.building.dynIndices.end(), indices, indices + indexCount);
        }

        // The latched state a draw takes at submit, shared by both kinds.
        PendingDraw &pushDraw(uint32_t meshKey, uint32_t vertCount, uint32_t indexCount, bool dynamic)
        {
            State &s = state();
            PendingDraw d;
            d.dynamic = dynamic;
            d.meshKey = meshKey;
            d.matrixCount = s.pendingMatrixCount;
            d.matrixOffset = (uint32_t)s.building.matrices.size();
            d.material = s.pendingMaterial;
            d.target = s.pendingTarget;
            d.cameraIndex = s.building.cameras.empty() ? kNoCamera
                                                        : (uint32_t)s.building.cameras.size() - 1u;
            d.vertCount = vertCount;
            d.indexCount = indexCount;
            s.building.matrices.insert(s.building.matrices.end(),
                                       s.pendingMatrices,
                                       s.pendingMatrices + (size_t)d.matrixCount * 16u);
            s.building.matrices.insert(s.building.matrices.end(),
                                       s.pendingLightMatrix, s.pendingLightMatrix + 16);
            if (s.hasLights)
            {
                std::vector<Lights> &arena = s.building.lights;
                if (arena.empty() || std::memcmp(&arena.back(), &s.currentLights, sizeof(Lights)) != 0)
                {
                    arena.push_back(s.currentLights);
                }
                d.lightsIndex = (uint32_t)arena.size() - 1u;
            }
            s.building.draws.push_back(d);
            return s.building.draws.back();
        }

        void endFrame()
        {
            if (!active())
            {
                return;
            }

            State &s = state();
            Frame &f = s.building;

            static uint64_t frames = 0ull;
            ++frames;
            f.serial = frames;

            // Publish. The arena is stable now, so the views can point into
            // it; they could not while it was still growing.
            f.views.resize(f.draws.size());
            for (size_t i = 0; i < f.draws.size(); ++i)
            {
                const PendingDraw &d = f.draws[i];
                DrawView &v = f.views[i];
                v.meshKey = d.meshKey;
                v.generation = d.generation;
                v.matrixCount = d.matrixCount;
                v.vertCount = d.vertCount;
                v.indexCount = d.indexCount;
                v.material = d.material;
                v.target = d.target;
            }
            {
                std::lock_guard<std::mutex> lock(s.publishMutex);
                // Swap into a recycled frame, so `f` comes back with an old
                // frame's capacity for beginFrame to clear.
                Frame slot;
                if (!s.pool.empty())
                {
                    std::swap(slot, s.pool.back());
                    s.pool.pop_back();
                }
                std::swap(slot, f);
                s.published.push_back(std::move(slot));
                // Three is about 50 ms of slack. Past it the guest is really
                // ahead of the display, and the oldest frame goes.
                // GHPC_SCENE_QUEUE=1 is the old latest-only handoff.
                static const size_t kMaxQueued = []() -> size_t {
                    const char *e = std::getenv("GHPC_SCENE_QUEUE");
                    const int n = e ? std::atoi(e) : 3;
                    return n < 1 ? 1u : (size_t)n;
                }();
                while (s.published.size() > kMaxQueued)
                {
                    s.pool.push_back(std::move(s.published.front()));
                    s.published.pop_front();
                }
                // Moves and swaps carry the vectors' heap buffers, so the
                // views built above still point into their arena. Repointed
                // anyway: turn any of these into a copy and that stops being
                // true silently, with no crash until an arena reallocates
                // under a live pointer.
                pointViews(s.published.back());
            }

            const int every = logEvery();
            if (every <= 0 || (frames % (uint64_t)every) != 0ull)
            {
                return;
            }

            // Reported off the newest queued frame, since `f` is now the
            // recycled one. Held under the lock, which is what stops the
            // consumer taking it mid-read.
            std::lock_guard<std::mutex> lock(s.publishMutex);
            if (s.published.empty())
            {
                return;
            }
            const Frame &p = s.published.back();
            uint64_t verts = 0ull;
            uint64_t tris = 0ull;
            uint32_t skinned = 0u;
            uint32_t noIndices = 0u;
            // The depth state histogram, because it is the difference between
            // a silhouette and a picture. ztst[] is indexed by the GS field:
            // 0 never, 1 always, 2 gequal, 3 greater. noZWrite counts the
            // draws whose ZMSK says leave the depth buffer alone, which a
            // backend that writes depth anyway loses the whole frame behind.
            uint32_t ztst[4] = {0u, 0u, 0u, 0u};
            uint32_t noZWrite = 0u;
            uint32_t noZTest = 0u;
            uint32_t blended = 0u;
            uint32_t textured = 0u;
            // The alpha test, counted because it is the leading explanation
            // for the native path covering pixels the software path leaves
            // empty. atst[] is indexed by the GS field.
            uint32_t alphaTested = 0u;
            uint32_t atst[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            uint32_t afailNonKeep = 0u;
            uint32_t arefMin = 0xFFFFFFFFu;
            uint32_t arefMax = 0u;
            // AFAIL decides what a failed texel still writes, 0 keep, 1 fb
            // only, 2 zb only, 3 rgb only, and an alpha-tested draw that does
            // not blend is the case where fb-only still paints a solid
            // colour. Both are counted because a discard is only exact for
            // some of them.
            uint32_t afail[4] = {0u, 0u, 0u, 0u};
            uint32_t ateOpaque = 0u;
            uint32_t tfx[4] = {0u, 0u, 0u, 0u};
            uint32_t clampWrap = 0u;
            uint32_t texMissing = 0u;
            for (const PendingDraw &d : p.draws)
            {
                if (d.material.alphaTest)
                {
                    ++alphaTested;
                    atst[d.material.atst & 7u] += 1u;
                    afailNonKeep += (d.material.afail != 0u) ? 1u : 0u;
                    afail[d.material.afail & 3u] += 1u;
                    ateOpaque += d.material.blend ? 0u : 1u;
                    arefMin = std::min(arefMin, d.material.aref);
                    arefMax = std::max(arefMax, d.material.aref);
                }
                if (d.material.texKey != 0u)
                {
                    tfx[d.material.tfx & 3u] += 1u;
                    clampWrap += (d.material.wrapS | d.material.wrapT) ? 1u : 0u;
                    std::lock_guard<std::mutex> texLock(s.textureMutex);
                    auto it = s.textures.find(d.material.texKey);
                    texMissing += (it == s.textures.end() || it->second.rgba.empty()) ? 1u : 0u;
                }
                verts += d.vertCount;
                tris += d.indexCount / 3u;
                skinned += (d.matrixCount > 1u) ? 1u : 0u;
                noIndices += (d.indexCount == 0u) ? 1u : 0u;
                ztst[d.material.ztst & 3u] += 1u;
                noZWrite += d.material.depthWrite ? 0u : 1u;
                noZTest += d.material.depthTest ? 0u : 1u;
                blended += d.material.blend ? 1u : 0u;
                textured += (d.material.texKey != 0u) ? 1u : 0u;
            }

            // A frame with no camera is a frame a backend could not transform,
            // so it is reported rather than averaged away.
            std::fprintf(stderr,
                         "[ghpc/scene] frame=%llu draws=%zu verts=%llu tris=%llu "
                         "skinned=%u noIndices=%u cam=%d cams=%zu bones=%u world=%u meshes=%zu "
                         "mats=%u ztst=%u/%u/%u/%u noZWrite=%u noZTest=%u blend=%u tex=%u "
                         "ate=%u atst=%u/%u/%u/%u/%u/%u/%u/%u aref=%u..%u afail=%u\n",
                         (unsigned long long)frames, p.draws.size(),
                         (unsigned long long)verts, (unsigned long long)tris,
                         skinned, noIndices, p.cameraSeen ? 1 : 0, p.cameras.size(),
                         p.boneUploads, p.worldUploads, s.geometry.size(),
                         p.materialBinds, ztst[0], ztst[1], ztst[2], ztst[3],
                         noZWrite, noZTest, blended, textured,
                         alphaTested, atst[0], atst[1], atst[2], atst[3],
                         atst[4], atst[5], atst[6], atst[7],
                         (arefMin == 0xFFFFFFFFu) ? 0u : arefMin, arefMax, afailNonKeep);

            // Lighting: which VU1 program each draw's colour comes from, and
            // one sample of the scale and the light block, since the formula
            // is only as right as the values it is fed.
            {
                uint32_t passthrough = 0u, ambient = 0u, directional = 0u, point = 0u, other = 0u;
                uint32_t prelitCount = 0u, noLights = 0u;
                // Material programs: the texture transform is reproduced, the
                // sphere map and anything else are not, so they are counted.
                uint32_t uvXfm = 0u, uvSphere = 0u, uvOther = 0u;
                const PendingDraw *sample = nullptr;
                for (const PendingDraw &d : p.draws)
                {
                    switch (d.material.lightProgram)
                    {
                    case 0x614u: ++passthrough; break;
                    case 0x7C5u: ++ambient; break;
                    case 0x6ECu: ++directional; sample = sample ? sample : &d; break;
                    case 0x436u: ++point; break;
                    default: ++other; break;
                    }
                    prelitCount += d.material.prelit ? 1u : 0u;
                    switch (d.material.uvProgram)
                    {
                    case 0x614u: break;
                    case 0x17Cu: ++uvXfm; break;
                    case 0x347u: ++uvSphere; break;
                    default: ++uvOther; break;
                    }
                    noLights += (d.lightsIndex == kNoCamera) ? 1u : 0u;
                }
                if (sample == nullptr && !p.draws.empty())
                {
                    sample = &p.draws.front();
                }
                const float *scale = sample ? sample->material.rgbaScale : nullptr;
                const Lights *l = (sample && sample->lightsIndex < p.lights.size())
                                      ? &p.lights[sample->lightsIndex] : nullptr;
                std::fprintf(stderr,
                             "[ghpc/scene/light] frame=%llu passthrough=%u ambient=%u directional=%u "
                             "point=%u other=%u prelit=%u noLights=%u environs=%zu "
                             "uvXfm=%u uvSphere=%u uvOther=%u "
                             "qw695=(%.3f %.3f %.3f %.3f) ambientQw=(%.3f %.3f %.3f %.3f) "
                             "col0=(%.3f %.3f %.3f) dirRow0=(%.3f %.3f %.3f)\n",
                             (unsigned long long)frames, passthrough, ambient, directional, point, other,
                             prelitCount, noLights, p.lights.size(), uvXfm, uvSphere, uvOther,
                             scale ? scale[0] : 0.0f, scale ? scale[1] : 0.0f,
                             scale ? scale[2] : 0.0f, scale ? scale[3] : 0.0f,
                             l ? l->qw[0][0] : 0.0f, l ? l->qw[0][1] : 0.0f,
                             l ? l->qw[0][2] : 0.0f, l ? l->qw[0][3] : 0.0f,
                             l ? l->qw[1][0] : 0.0f, l ? l->qw[1][1] : 0.0f, l ? l->qw[1][2] : 0.0f,
                             l ? l->qw[4][0] : 0.0f, l ? l->qw[4][1] : 0.0f, l ? l->qw[4][2] : 0.0f);
            }

            // The texture side of the same frame. texMissing is draws naming
            // a PsTex with no decoded image, which a backend draws untextured.
            const uint32_t texa = s.texa.load(std::memory_order_relaxed);
            std::fprintf(stderr,
                         "[ghpc/scene/tex] frame=%llu afail=%u/%u/%u/%u ateOpaque=%u tfx=%u/%u/%u/%u "
                         "clamp=%u texMissing=%u decoded=%u redecoded=%u renderTarget=%u "
                         "noPixels=%u unsupported=%u oddOrder=%u scaled=%u texa=%u/%u/%u\n",
                         (unsigned long long)frames, afail[0], afail[1], afail[2], afail[3],
                         ateOpaque, tfx[0], tfx[1], tfx[2], tfx[3], clampWrap, texMissing,
                         s.texDecoded, s.texRedecoded, s.texRenderTarget, s.texNoPixels,
                         s.texUnsupported, s.texOddOrder, s.texScaled,
                         texa & 0xFFu, (texa >> 8) & 0xFFu, (texa >> 16) & 1u);
        }

        bool acquireFrame(Camera &camera, bool &cameraValid,
                          const DrawView **draws, uint32_t &drawCount,
                          uint64_t &serial)
        {
            State &s = state();
            {
                std::lock_guard<std::mutex> lock(s.publishMutex);
                if (!s.published.empty())
                {
                    if (s.hasConsuming)
                    {
                        s.pool.push_back(std::move(s.consuming));
                    }
                    s.consuming = std::move(s.published.front());
                    s.published.pop_front();
                    s.hasConsuming = true;
                    // Same reason as the repoint in endFrame.
                    pointViews(s.consuming);
                }
            }

            if (!s.hasConsuming)
            {
                return false;
            }

            camera = s.consuming.camera;
            cameraValid = s.consuming.cameraSeen;
            *draws = s.consuming.views.data();
            drawCount = (uint32_t)s.consuming.views.size();
            serial = s.consuming.serial;
            return true;
        }

        bool geometry(uint32_t meshKey, std::vector<Vertex> &verts,
                      std::vector<uint16_t> &indices, uint32_t &generation)
        {
            State &s = state();
            std::lock_guard<std::mutex> lock(s.geometryMutex);
            auto it = s.geometry.find(meshKey);
            if (it == s.geometry.end())
            {
                return false;
            }
            verts = it->second.verts;
            indices = it->second.indices;
            generation = it->second.generation;
            return true;
        }

        void setGsOffset(float ofx, float ofy)
        {
            state().ofx.store(ofx, std::memory_order_relaxed);
            state().ofy.store(ofy, std::memory_order_relaxed);
        }

        void gsOffset(float &ofx, float &ofy)
        {
            ofx = state().ofx.load(std::memory_order_relaxed);
            ofy = state().ofy.load(std::memory_order_relaxed);
        }

        void setGsTexa(uint32_t ta0, uint32_t ta1, bool aem)
        {
            State &s = state();
            s.texa.store((ta0 & 0xFFu) | ((ta1 & 0xFFu) << 8) | (aem ? 0x10000u : 0u),
                         std::memory_order_relaxed);
            s.texaSeen.store(true, std::memory_order_relaxed);
        }

        void setTexture(uint32_t texKey, const TextureSource &source)
        {
            if (!active() || texKey == 0u)
            {
                return;
            }
            State &s = state();

            TextureSignature sig;
            sig.pixelsAddr = source.pixelsAddr;
            sig.paletteAddr = source.paletteAddr;
            sig.width = source.width;
            sig.height = source.height;
            sig.bpp = source.bpp;
            sig.order = source.order;
            sig.tw = source.tw;
            sig.th = source.th;
            sig.renderTarget = source.renderTarget;

            bool known = false;
            {
                std::lock_guard<std::mutex> lock(s.textureMutex);
                auto it = s.textures.find(texKey);
                if (it != s.textures.end())
                {
                    if (it->second.signature == sig)
                    {
                        return;
                    }
                    known = true;
                }
            }

            // A 16-bit texel's alpha is TEXA's, and TEXA is read off the GS
            // at present. Before the first present it is not known, so the
            // texture is left undecided and tried again on the next bind
            // rather than cached with every alpha zero.
            const bool texaSeen = s.texaSeen.load(std::memory_order_relaxed);
            if (source.bpp == 16u && !texaSeen)
            {
                return;
            }

            TextureEntry entry;
            entry.signature = sig;
            if (source.renderTarget)
            {
                ++s.texRenderTarget;
            }
            else if (source.pixels == nullptr)
            {
                ++s.texNoPixels;
            }
            else if (decodeBitmap(source, s.texa.load(std::memory_order_relaxed), entry.rgba))
            {
                entry.width = source.width;
                entry.height = source.height;
                // UVs are normalised to the GS's 2^TW x 2^TH texture, which
                // is the bitmap's own size unless the bitmap is not a power
                // of two. tw 0 is an uninitialised TEX0 and is taken as 1:1.
                if (source.tw != 0u && source.th != 0u)
                {
                    entry.uvScale[0] = (float)(1u << source.tw) / (float)source.width;
                    entry.uvScale[1] = (float)(1u << source.th) / (float)source.height;
                    if (entry.uvScale[0] != 1.0f || entry.uvScale[1] != 1.0f)
                    {
                        ++s.texScaled;
                    }
                }
                if ((source.order & 1u) == 0u)
                {
                    ++s.texOddOrder;
                }
                ++(known ? s.texRedecoded : s.texDecoded);
                dumpTexture(texKey, source, entry.rgba);
            }
            else
            {
                ++s.texUnsupported;
            }

            std::lock_guard<std::mutex> lock(s.textureMutex);
            entry.version = ++s.textureVersions;
            s.textures[texKey] = std::move(entry);
        }

        bool textureInfo(uint32_t texKey, uint32_t &width, uint32_t &height,
                         uint64_t &version, float uvScale[2])
        {
            State &s = state();
            std::lock_guard<std::mutex> lock(s.textureMutex);
            auto it = s.textures.find(texKey);
            if (it == s.textures.end() || it->second.rgba.empty())
            {
                return false;
            }
            width = it->second.width;
            height = it->second.height;
            version = it->second.version;
            uvScale[0] = it->second.uvScale[0];
            uvScale[1] = it->second.uvScale[1];
            return true;
        }

        bool copyTexture(uint32_t texKey, uint64_t version, void *dst, uint64_t bytes)
        {
            State &s = state();
            std::lock_guard<std::mutex> lock(s.textureMutex);
            auto it = s.textures.find(texKey);
            if (it == s.textures.end() || it->second.version != version ||
                it->second.rgba.size() != bytes)
            {
                return false;
            }
            std::memcpy(dst, it->second.rgba.data(), (size_t)bytes);
            return true;
        }
    }
}
