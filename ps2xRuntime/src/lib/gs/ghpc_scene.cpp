#include "ghpc_scene.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Collector for the Rnd seam.
//
// This is deliberately not a renderer yet. Before any Vulkan pipeline is worth
// writing, the question is whether the four inputs arrive at the seam intact
// and complete, every frame, on the screen that matters. GHPC_RND_SCENE=1
// answers that and draws nothing, so it cannot regress the guest.
//
// The guest path stays untouched when it is off: `active()` is one relaxed
// load of a value read from the environment once.
namespace ghpc
{
    namespace scene
    {
        namespace
        {
            struct Draw
            {
                uint32_t meshKey = 0u;
                uint32_t vertCount = 0u;
                uint32_t indexCount = 0u;
                bool skinned = false;
            };

            struct Frame
            {
                std::vector<Draw> draws;
                Camera camera{};
                bool cameraSeen = false;
                uint32_t boneUploads = 0u;
                uint32_t worldUploads = 0u;

                void clear()
                {
                    draws.clear();
                    cameraSeen = false;
                    boneUploads = 0u;
                    worldUploads = 0u;
                }
            };

            Frame &frame()
            {
                static Frame f;
                return f;
            }

            bool &skinnedPending()
            {
                static bool v = false;
                return v;
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
            static const bool on = std::getenv("GHPC_RND_SCENE") != nullptr;
            return on;
        }

        void beginFrame()
        {
            if (!active())
            {
                return;
            }
            frame().clear();
            skinnedPending() = false;
        }

        void setCamera(const Camera &camera)
        {
            if (!active())
            {
                return;
            }
            frame().camera = camera;
            frame().cameraSeen = true;
        }

        void setBonePalette(const float *matrices, uint32_t matrixCount)
        {
            if (!active() || matrices == nullptr || matrixCount == 0u)
            {
                return;
            }
            ++frame().boneUploads;
            skinnedPending() = true;
        }

        void setWorldMatrix(const float *m)
        {
            if (!active() || m == nullptr)
            {
                return;
            }
            ++frame().worldUploads;
            skinnedPending() = false;
        }

        void submitMesh(uint32_t meshKey,
                        const Vertex *verts, uint32_t vertCount,
                        const uint16_t *indices, uint32_t indexCount)
        {
            if (!active() || verts == nullptr || vertCount == 0u)
            {
                return;
            }
            Draw d;
            d.meshKey = meshKey;
            d.vertCount = vertCount;
            d.indexCount = (indices != nullptr) ? indexCount : 0u;
            d.skinned = skinnedPending();
            frame().draws.push_back(d);
        }

        void endFrame()
        {
            if (!active())
            {
                return;
            }

            static uint64_t frames = 0ull;
            ++frames;

            const int every = logEvery();
            if (every <= 0 || (frames % (uint64_t)every) != 0ull)
            {
                return;
            }

            const Frame &f = frame();
            uint64_t verts = 0ull;
            uint64_t tris = 0ull;
            uint32_t skinned = 0u;
            uint32_t noIndices = 0u;
            for (const Draw &d : f.draws)
            {
                verts += d.vertCount;
                tris += d.indexCount / 3u;
                skinned += d.skinned ? 1u : 0u;
                noIndices += (d.indexCount == 0u) ? 1u : 0u;
            }

            // A frame with no camera is a frame a backend could not transform,
            // so it is reported rather than averaged away.
            std::fprintf(stderr,
                         "[ghpc/scene] frame=%llu draws=%zu verts=%llu tris=%llu "
                         "skinned=%u noIndices=%u cam=%d bones=%u world=%u\n",
                         (unsigned long long)frames, f.draws.size(),
                         (unsigned long long)verts, (unsigned long long)tris,
                         skinned, noIndices, f.cameraSeen ? 1 : 0,
                         f.boneUploads, f.worldUploads);
        }
    }
}
