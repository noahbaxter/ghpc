#include "ghpc_scene.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
            // arena reallocates while it fills.
            struct PendingDraw
            {
                uint32_t meshKey = 0u;
                uint32_t matrixOffset = 0u; // floats into Frame::matrices
                uint32_t matrixCount = 0u;
                uint32_t vertCount = 0u;
                uint32_t indexCount = 0u;
                Material material{};
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
                return m;
            }

            struct Frame
            {
                std::vector<PendingDraw> draws;
                std::vector<float> matrices;
                std::vector<DrawView> views; // built at publish
                Camera camera{};
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
                    cameraSeen = false;
                    boneUploads = 0u;
                    worldUploads = 0u;
                    materialBinds = 0u;
                }
            };

            struct MeshGeometry
            {
                std::vector<Vertex> verts;
                std::vector<uint16_t> indices;
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

                // Published, under `publishMutex`.
                Frame published;
                bool hasPublished = false;
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

                std::atomic<float> ofx{0.0f};
                std::atomic<float> ofy{0.0f};
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

        void setCamera(const Camera &camera)
        {
            if (!active())
            {
                return;
            }
            state().building.camera = camera;
            state().building.cameraSeen = true;
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

        void submitMesh(uint32_t meshKey,
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
                if (it == s.geometry.end())
                {
                    MeshGeometry g;
                    g.verts.assign(verts, verts + vertCount);
                    if (indices != nullptr && indexCount != 0u)
                    {
                        g.indices.assign(indices, indices + indexCount);
                    }
                    s.geometry.emplace(meshKey, std::move(g));
                }
            }

            PendingDraw d;
            d.meshKey = meshKey;
            d.matrixCount = s.pendingMatrixCount;
            d.matrixOffset = (uint32_t)s.building.matrices.size();
            d.material = s.pendingMaterial;
            d.vertCount = vertCount;
            d.indexCount = (indices != nullptr) ? indexCount : 0u;
            s.building.matrices.insert(s.building.matrices.end(),
                                       s.pendingMatrices,
                                       s.pendingMatrices + (size_t)d.matrixCount * 16u);
            s.building.draws.push_back(d);
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
                v.matrices = f.matrices.data() + d.matrixOffset;
                v.matrixCount = d.matrixCount;
                v.vertCount = d.vertCount;
                v.indexCount = d.indexCount;
                v.material = d.material;
            }
            {
                std::lock_guard<std::mutex> lock(s.publishMutex);
                std::swap(s.published, f);
                s.hasPublished = true;
                // Redundant while this is a swap, and kept deliberately.
                // std::swap exchanges the vectors' heap buffers, so a view
                // built above still points into the arena that travelled with
                // it. Turn either swap in this file into an assignment and
                // that stops being true silently, with no crash until an
                // arena reallocates under a live pointer. Repointing from the
                // offsets costs one pass over a few hundred draws and holds
                // either way.
                for (size_t i = 0; i < s.published.draws.size(); ++i)
                {
                    s.published.views[i].matrices =
                        s.published.matrices.data() + s.published.draws[i].matrixOffset;
                }
            }

            const int every = logEvery();
            if (every <= 0 || (frames % (uint64_t)every) != 0ull)
            {
                return;
            }

            // Reported off the published copy, since `f` is now the recycled
            // frame the swap handed back.
            std::lock_guard<std::mutex> lock(s.publishMutex);
            const Frame &p = s.published;
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
            for (const PendingDraw &d : p.draws)
            {
                if (d.material.alphaTest)
                {
                    ++alphaTested;
                    atst[d.material.atst & 7u] += 1u;
                    afailNonKeep += (d.material.afail != 0u) ? 1u : 0u;
                    arefMin = std::min(arefMin, d.material.aref);
                    arefMax = std::max(arefMax, d.material.aref);
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
                         "skinned=%u noIndices=%u cam=%d bones=%u world=%u meshes=%zu "
                         "mats=%u ztst=%u/%u/%u/%u noZWrite=%u noZTest=%u blend=%u tex=%u "
                         "ate=%u atst=%u/%u/%u/%u/%u/%u/%u/%u aref=%u..%u afail=%u\n",
                         (unsigned long long)frames, p.draws.size(),
                         (unsigned long long)verts, (unsigned long long)tris,
                         skinned, noIndices, p.cameraSeen ? 1 : 0,
                         p.boneUploads, p.worldUploads, s.geometry.size(),
                         p.materialBinds, ztst[0], ztst[1], ztst[2], ztst[3],
                         noZWrite, noZTest, blended, textured,
                         alphaTested, atst[0], atst[1], atst[2], atst[3],
                         atst[4], atst[5], atst[6], atst[7],
                         (arefMin == 0xFFFFFFFFu) ? 0u : arefMin, arefMax, afailNonKeep);
        }

        bool acquireFrame(Camera &camera, bool &cameraValid,
                          const DrawView **draws, uint32_t &drawCount,
                          uint64_t &serial)
        {
            State &s = state();
            {
                std::lock_guard<std::mutex> lock(s.publishMutex);
                if (s.hasPublished)
                {
                    std::swap(s.consuming, s.published);
                    s.hasPublished = false;
                    s.hasConsuming = true;
                    // Same reason as the repoint in endFrame.
                    for (size_t i = 0; i < s.consuming.draws.size(); ++i)
                    {
                        s.consuming.views[i].matrices =
                            s.consuming.matrices.data() + s.consuming.draws[i].matrixOffset;
                    }
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

        bool geometry(uint32_t meshKey,
                      const Vertex **verts, uint32_t &vertCount,
                      const uint16_t **indices, uint32_t &indexCount)
        {
            State &s = state();
            std::lock_guard<std::mutex> lock(s.geometryMutex);
            auto it = s.geometry.find(meshKey);
            if (it == s.geometry.end())
            {
                return false;
            }
            *verts = it->second.verts.data();
            vertCount = (uint32_t)it->second.verts.size();
            *indices = it->second.indices.data();
            indexCount = (uint32_t)it->second.indices.size();
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
    }
}
