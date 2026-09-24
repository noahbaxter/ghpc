#pragma once

// The Vulkan host renderer's declaration and the helpers its parts share.
// Private to src/lib/vk/; the rest of the runtime sees ghpc_host_renderer.h.
#if defined(GHPC_ENABLE_VULKAN)

#include "ghpc_host_renderer.h"
#include "ghpc_input.h"
#include "ghpc_overlay.h"
#include "ghpc_scene.h"
// For the screen name a shot is tagged with. Every speed reporter in this
// project carries one, because a number or a picture off the loading screen
// read as a gameplay one has cost this port whole sessions.
#include "../Kernel/Stubs/ghpc_drive.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <atomic>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <cstring>
#include <iostream>
#include <string>

namespace ghpc
{
    namespace vk
    {
        constexpr uint32_t kFramesInFlight = 2u;

        inline const char *resultName(VkResult r)
        {
            switch (r)
            {
            case VK_SUCCESS:
                return "VK_SUCCESS";
            case VK_SUBOPTIMAL_KHR:
                return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR:
                return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_INITIALIZATION_FAILED:
                return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_LAYER_NOT_PRESENT:
                return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_DEVICE_LOST:
                return "VK_ERROR_DEVICE_LOST";
            default:
                return "VkResult";
            }
        }

        inline bool check(VkResult r, const char *what)
        {
            if (r == VK_SUCCESS)
            {
                return true;
            }
            std::cerr << "[ghpc/vk] " << what << " failed: " << resultName(r) << " (" << r << ")" << std::endl;
            return false;
        }

        inline bool hasExtension(const std::vector<VkExtensionProperties> &available, const char *name)
        {
            for (const VkExtensionProperties &e : available)
            {
                if (std::strcmp(e.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // Per-mesh GPU geometry, keyed by the guest PsMesh address the seam
        // hands over. Uploaded on first sight and reused for the life of the
        // process, which is what the host mesh cache's 99% hit rate makes
        // worth doing: a gameplay frame submits about 500 draws over a few
        // hundred distinct meshes, so per-frame uploads would be the cost the
        // whole phase exists to remove.
        struct MeshBuffers
        {
            VkBuffer vertexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
            VkBuffer indexBuffer = VK_NULL_HANDLE;
            VkDeviceMemory indexMemory = VK_NULL_HANDLE;
            uint32_t indexCount = 0u;
            uint32_t generation = 0u; // the scene geometry these were built from
        };

        // One entry per draw in the storage buffer the vertex shader indexes.
        // Matches DrawData in ghpc_mesh.vert: std430 puts the four mat4s at 0,
        // the colour at 256, the count at 272, the mode at 276, the UV scale
        // at 280, then VU1's lighting inputs and the texture transform, for a
        // stride of 544.
        struct GpuDrawData
        {
            float m[4][16];
            float color[4];
            uint32_t matrixCount;
            uint32_t mode; // bit layout in ghpc_mesh.frag
            float uvScale[2];
            float lightMat[16]; // qw676..679
            float lights[7][4]; // qw681..687
            float rgbaScale[4]; // qw695
            float vuColor[4];   // qw690
            uint32_t lightMode; // see ghpc_mesh.vert
            uint32_t lightFlags;
            uint32_t uvMode;    // 1: material program 0x17c's texture transform
            uint32_t texa;      // 16-bit target: ta0 | ta1 << 8 | aem << 16 | 1 << 24
            float uvRows[4];    // xy row 0, zw row 1 of +0x170
            float uvOffset[4];  // xy row 3
        };
        static_assert(sizeof(GpuDrawData) == 544u, "DrawData stride must match the shader's std430 layout");
        static_assert(offsetof(GpuDrawData, uvScale) == 280u, "a vec2 in std430 aligns to 8");
        static_assert(offsetof(GpuDrawData, lightMat) == 288u, "a mat4 in std430 aligns to 16");
        static_assert(offsetof(GpuDrawData, lightMode) == 496u, "lighting tail offset");

        // A decoded PsTex on the GPU, keyed by the same texKey the seam
        // carries, and uploaded again only when the collector's version for
        // that key moves.
        struct GpuTexture
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkDescriptorSet set = VK_NULL_HANDLE;
            uint64_t version = 0ull;
            float uvScale[2] = {1.0f, 1.0f};
            bool renderTarget = false; // an offscreen target, alpha 1.0 = GS 0x80
            uint32_t psm = 0u;         // a target's PSM as its texture reads it
        };

        // What a frame in flight may still be reading when it is replaced.
        // Freed when that frame's fence has been waited, which is the next
        // time the same frame index records.
        struct Garbage
        {
            std::vector<VkBuffer> buffers;
            std::vector<VkDeviceMemory> memories;
            std::vector<VkImage> images;
            std::vector<VkImageView> views;
            std::vector<VkDescriptorSet> sets;
        };

        // The material state a pipeline has to bake in, packed small so it can
        // key a cache. Everything else about a material is a uniform or a
        // texture; these four are pipeline creation parameters and nothing
        // else can carry them on a device without extended dynamic state,
        // which MoltenVK reports as a 1.1 driver and cannot be assumed.
        struct PipelineKey
        {
            uint32_t ztst : 2;      // GS TEST_1 ZTST
            uint32_t depthTest : 1; // GS TEST_1 ZTE
            uint32_t depthWrite : 1;// GS ZBUF_1 ZMSK, inverted
            uint32_t blend : 1;
            uint32_t alphaEq : 8; // ALPHA_1 A, B, C, D; zero when not blended

            uint32_t word() const
            {
                return ztst | (depthTest << 2) | (depthWrite << 3) | (blend << 4) | (alphaEq << 5);
            }
        };

        // Matches the push_constant block in ghpc_mesh.vert. 116 bytes, which
        // fits the 128 every Vulkan implementation guarantees, so the camera
        // needs no descriptor of its own.
        struct MeshPush
        {
            float cam[16];
            float scale[4];
            float offset[4];
            float viewport[4]; // xy half framebuffer, zw GS XYOFFSET
            uint32_t drawIndex;
        };

        class VulkanRenderer final : public HostRenderer
        {
        public:
            ~VulkanRenderer() override { shutdown(); }

            bool init(const Config &config) override;
            void shutdown() override;
            void uploadFrame(const Frame &frame) override;
            void present() override;
            bool shouldClose() const override { return m_shouldClose; }

        private:
            bool createInstance();
            bool createDevice();
            bool createSwapchain();
            void destroySwapchain();
            bool createSyncObjects();
            void pumpEvents();

            bool findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props, uint32_t &outIndex) const;
            bool createFrameResources(uint32_t width, uint32_t height);
            void destroyFrameResources();
            void checkParity(VkRect2D dstRect);
            bool ensureShotBuffer();
            void writeShot();

            // R2, the Rnd seam. All no-ops when the mesh pipeline is off or
            // was not compiled in.
            bool createMeshPipeline();
            void destroyMeshPipeline();
            bool createMeshTargets();
            void destroyMeshTargets();
            bool ensureDrawBuffer(uint32_t frameIndex, VkDeviceSize size);
            VkPipeline pipelineFor(PipelineKey key);
            const MeshBuffers *meshBuffers(uint32_t meshKey, uint32_t generation, uint32_t frameIndex);
            bool uploadTexture(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t width, uint32_t height,
                               const uint8_t *rgba, uint32_t texKey, uint64_t version, GpuTexture &out);
            struct TargetState;
            const GpuTexture *textureFor(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t texKey,
                                         std::unordered_map<uint32_t, TargetState> *targets);
            struct RenderTarget;
            RenderTarget *renderTargetFor(VkCommandBuffer cmd, uint32_t fbp, uint32_t width, uint32_t height);
            void destroyRenderTargets();
            void releaseTexture(uint32_t frameIndex, GpuTexture &t);
            void collectGarbage(uint32_t frameIndex);
            void drawScene(VkCommandBuffer cmd, uint32_t imageIndex, VkRect2D viewportRect);
            // The offscreen scene target, vk_scene_target.cpp.
            VkRenderPass scenePassFor(VkSampleCountFlagBits samples);
            bool ensureSceneTarget(uint32_t width, uint32_t height, VkSampleCountFlagBits samples);
            void destroySceneTarget();
            void destroyScenePasses();
            bool sceneWanted(VkRect2D dstRect, uint32_t &width, uint32_t &height, VkSampleCountFlagBits &samples);
            void blitScene(VkCommandBuffer cmd, VkImage swapchainImage, VkRect2D dstRect);
            void reportMesh();

            SDL_Window *m_window = nullptr;
            Overlay m_overlay;
            std::string m_baseTitle;
            std::string m_shownTitle;
            VkInstance m_instance = VK_NULL_HANDLE;
            VkSurfaceKHR m_surface = VK_NULL_HANDLE;
            VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
            VkDevice m_device = VK_NULL_HANDLE;
            uint32_t m_queueFamily = 0u;
            VkQueue m_queue = VK_NULL_HANDLE;

            VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
            VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D m_swapchainExtent{};
            std::vector<VkImage> m_swapchainImages;

            VkCommandPool m_commandPool = VK_NULL_HANDLE;
            std::array<VkCommandBuffer, kFramesInFlight> m_commandBuffers{};
            std::array<VkSemaphore, kFramesInFlight> m_acquireSemaphores{};
            std::array<VkFence, kFramesInFlight> m_inFlightFences{};
            // Present waits on a semaphore signalled by the submit that drew
            // into a specific image, so these are per-image, not per-frame:
            // with two frames in flight and three swapchain images, a
            // per-frame semaphore can still be pending on an older present.
            std::vector<VkSemaphore> m_renderSemaphores;

            // The software GS framebuffer, staged host-side and blitted to the
            // swapchain. R8G8B8A8 into a B8G8R8A8 swapchain: vkCmdBlitImage
            // converts, which is why this is a blit and not a copy.
            VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
            void *m_stagingMapped = nullptr;
            VkImage m_frameImage = VK_NULL_HANDLE;
            VkDeviceMemory m_frameMemory = VK_NULL_HANDLE;
            uint32_t m_bufferWidth = 0u;
            uint32_t m_bufferHeight = 0u;
            uint32_t m_sourceWidth = 0u;
            uint32_t m_sourceHeight = 0u;
            float m_displayAspect = 0.0f; // Frame::displayAspect

            bool m_hasFrame = false;

            // Parity gate, GHPC_VK_PARITY=N. Reads the presented swapchain back
            // every Nth frame and compares it to the bytes that went in.
            int m_parityEvery = 0;
            VkBuffer m_readbackBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_readbackMemory = VK_NULL_HANDLE;
            void *m_readbackMapped = nullptr;
            bool m_swapchainReadable = false;
            bool m_parityThisFrame = false;
            uint64_t m_parityChecked = 0ull;
            uint64_t m_parityMismatched = 0ull;
            uint64_t m_parityWorstPixels = 0ull;

            // R2, the Rnd seam. GHPC_VK_MESH=1 draws native geometry over the
            // software picture, which is what makes the two comparable.
            // GHPC_VK_MESH=2 drops the blit so only the native path is on
            // screen, which is the arm that can actually fail: over the blit,
            // a pipeline that draws nothing is indistinguishable from one that
            // draws everything wrong behind the software picture.
            // GHPC_VK_SHOT=N writes the presented swapchain to a PPM every Nth
            // frame, capped by GHPC_VK_SHOT_LIMIT. This is the whole reason
            // the mesh path can be judged from an unattended run: the picture
            // is the evidence and a log line cannot be one.
            //
            // Separate from the parity readback, which is letterbox-sized and
            // only exists when the parity gate is on. This one is the full
            // swapchain, so it shows the letterbox itself.
            int m_shotEvery = 0;
            int m_shotLimit = 8;
            // GHPC_VK_SHOT_BEAT=N captures once per N beats of the chart
            // instead of every N present frames, and it is the only mode whose
            // output two runs can be compared on.
            //
            // A present-frame interval cannot work: about 40 presents pass per
            // guest frame on game_screen, so the interval steps the chart by a
            // fraction of a frame and any phase difference between runs lands
            // somewhere else. Beat buckets are the chart's own clock, so run A
            // and run B both capture at beat 16 whatever the host was doing,
            // and the files even come out with matching names.
            double m_shotBeatEvery = 0.0;
            long long m_shotBeatBucket = -1;
            // GHPC_VK_SHOT_UI=N, the same on TaskMgr's ui clock, every N
            // seconds, for menus: they have no chart, and the emulated arm runs
            // at 0.43 of realtime against the native arm's 1.0, so a present
            // frame is a different moment of every menu animation in each.
            // The bucket shares m_shotBeatBucket, since only one mode runs.
            double m_shotUiEvery = 0.0;
            // What the last shot was taken at, for the log line and the name.
            double m_shotBeat = 0.0;
            double m_shotGame = 0.0;
            // GHPC_VK_SHOT_SCREEN=<name> holds the shots until the guest is on
            // that screen. Without it a run that takes 600 frames to reach
            // gameplay spends its whole shot budget on the loading screen,
            // which is what the first R2 arm did.
            std::string m_shotScreen;
            int m_shots = 0;
            bool m_shotThisFrame = false;
            // GHPC_VK_SHOT_OVERLAY=1: shots include the menu, to test it.
            bool m_shotOverlay = false;
            VkBuffer m_shotBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_shotMemory = VK_NULL_HANDLE;
            void *m_shotMapped = nullptr;
            VkDeviceSize m_shotCapacity = 0u;
            uint32_t m_shotWidth = 0u;
            uint32_t m_shotHeight = 0u;

            int m_meshMode = 0;
            // GHPC_VK_MESH_DEPTH: 0 no depth at all, 1 force GREATER, 2 force
            // LESS, 3 take it from the material, which is the default.
            //
            // The default is the material's own TEST_1 and ZBUF_1 because that
            // is what the hardware does. It is NOT a measured choice: whether
            // a forced state produces a better picture is open, and the
            // coverage metric cannot answer it. See notes/gotchas.md, "Shots
            // pair on serial"; two arms of one batch sat at guest serials 653
            // and 857 at the same present frame, and coverage moves 0.2 iou
            // over 240 present frames, so the pairing error swamps the effect.
            //
            // Do not set a default here from a coverage number until the
            // shots being compared carry the same serial.
            int m_depthMode = 3;
            // GHPC_VK_MESH_RANGE=first:count draws only a slice of the frame's
            // draws. A frame with 534 of them is one silhouette; a slice is a
            // mesh you can identify.
            uint32_t m_drawFirst = 0u;
            uint32_t m_drawLimit = 0u; // 0 means all

            // GHPC_VK_MESH_SKIP_ATE=1 drops the alpha-tested draws.
            //
            // Not a rendering choice, a stand-in. 189 of 528 gameplay draws
            // enable the alpha test, and every one of them is ATST GREATER
            // with AREF 0, which is a punch-out: pass only where the texel has
            // alpha. Without texture there is no alpha to test, so those draws
            // render as their full solid quads and cover the frame. Skipping
            // them shows what is behind, which is the only way to judge the
            // rest of the geometry before texture lands. It is wrong on
            // purpose and both arms are worth looking at.
            bool m_skipAlphaTested = false;

            // GHPC_VK_MESH_TINT=1 colours each draw from a hash of its mesh
            // key instead of the material colour.
            //
            // Flat material colour plus a one-directional shade merges
            // everything that faces the camera into one white mass, which
            // reads as broken rendering whether or not it is. A per-mesh tint
            // makes the structure legible: distinct objects get distinct
            // colours, so what is actually on screen can be counted.
            bool m_tint = false;
            // GHPC_VK_MESH_ID=1 paints each draw its own index as a flat
            // colour, R low byte and G high byte of index + 1, B 0x40, and
            // writes the frame's draw table next to every shot. Counting a
            // shot's pixels by colour then names the draws that own the
            // picture, which a hashed tint can only suggest.
            bool m_idMode = false;
            // GHPC_VK_MESH_HIDE=0xKEY+0xKEY skips those meshes, to see what
            // they cover. Keys come from a GHPC_VK_MESH_ID draw table; a draw
            // index moves between frames and a mesh key does not.
            std::vector<uint32_t> m_hiddenMeshes;
            struct DrawRecord
            {
                uint32_t index, meshKey, matrixCount, vertCount, indexCount;
                scene::Material material;
                float translation[3];
                scene::Camera camera;
                scene::Target target;
            };
            std::vector<DrawRecord> m_drawRecords;
            bool m_meshReady = false;
            VkRenderPass m_meshPass = VK_NULL_HANDLE;

            // Offscreen GS targets, keyed by FRAME's FBP: the crowd's
            // impostors are drawn into one and sampled by its billboards.
            // Each pass clears it, and it sits in SHADER_READ_ONLY between
            // passes. m_rtPass matches m_meshPass's formats, so the same
            // pipelines draw into both.
            struct RenderTarget
            {
                uint32_t width = 0u;
                uint32_t height = 0u;
                VkImage image = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                VkImageView view = VK_NULL_HANDLE;
                VkImage depth = VK_NULL_HANDLE;
                VkDeviceMemory depthMemory = VK_NULL_HANDLE;
                VkImageView depthView = VK_NULL_HANDLE;
                VkFramebuffer framebuffer = VK_NULL_HANDLE;
                GpuTexture texture;
            };
            // Per FBP within one frame; see drawScene.
            struct TargetState
            {
                uint32_t generation = 0u;
                bool sampled = false;
            };
            VkRenderPass m_rtPass = VK_NULL_HANDLE;
            // Keyed by FBP | generation << 16.
            std::unordered_map<uint32_t, RenderTarget> m_renderTargets;
            std::vector<uint32_t> m_drawTargetKey;
            uint64_t m_rtDraws = 0ull;
            // GHPC_VK_RT=0 skips offscreen draws and samples no target: the
            // control arm, and the behaviour before targets existed.
            bool m_rtEnabled = true;
            // GHPC_VK_RT_DUMP=1: every shot also writes the first target drawn
            // that frame, colour and alpha side by side, as <shot>_rt.ppm.
            bool m_rtDump = false;
            VkBuffer m_rtDumpBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_rtDumpMemory = VK_NULL_HANDLE;
            void *m_rtDumpMapped = nullptr;
            uint32_t m_rtDumpWidth = 0u;
            uint32_t m_rtDumpHeight = 0u;
            VkPipelineLayout m_meshLayout = VK_NULL_HANDLE;
            // One pipeline per distinct material depth and blend state. The
            // key is five bits, so the cache is bounded at 32 whatever the
            // frame does, and gameplay uses a handful.
            std::unordered_map<uint32_t, VkPipeline> m_pipelines;
            VkShaderModule m_meshVertModule = VK_NULL_HANDLE;
            VkShaderModule m_meshFragModule = VK_NULL_HANDLE;
            VkDescriptorSetLayout m_meshSetLayout = VK_NULL_HANDLE;
            VkDescriptorPool m_meshDescriptorPool = VK_NULL_HANDLE;
            std::array<VkDescriptorSet, kFramesInFlight> m_meshDescriptorSets{};

            // Sized to the swapchain, so they are rebuilt with it.
            std::vector<VkImageView> m_swapchainViews;
            std::vector<VkFramebuffer> m_meshFramebuffers;
            VkImage m_depthImage = VK_NULL_HANDLE;
            VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
            VkImageView m_depthView = VK_NULL_HANDLE;

            // The scene drawn offscreen instead of straight into the window,
            // for anti-aliasing and a render resolution of its own: the
            // overlay's [display] msaa and render_scale. Drawn at `samples`,
            // resolved if multisampled, and blitted into the letterbox. Off,
            // at window size and one sample, drawScene writes the swapchain
            // image directly as it always has.
            struct SceneTarget
            {
                uint32_t width = 0u;
                uint32_t height = 0u;
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
                VkImage color = VK_NULL_HANDLE;   // `samples` samples
                VkDeviceMemory colorMemory = VK_NULL_HANDLE;
                VkImageView colorView = VK_NULL_HANDLE;
                VkImage resolve = VK_NULL_HANDLE; // one sample; multisampled only
                VkDeviceMemory resolveMemory = VK_NULL_HANDLE;
                VkImageView resolveView = VK_NULL_HANDLE;
                VkImage depth = VK_NULL_HANDLE;
                VkDeviceMemory depthMemory = VK_NULL_HANDLE;
                VkImageView depthView = VK_NULL_HANDLE;
                VkFramebuffer framebuffer = VK_NULL_HANDLE;
            };
            SceneTarget m_scene;
            bool m_sceneActive = false; // this present draws into m_scene
            bool m_sceneDrawn = false;  // and drawScene got as far as its pass
            std::unordered_map<uint32_t, VkRenderPass> m_scenePasses; // by sample count
            // What pipelineFor builds for: the pass being recorded. Offscreen
            // GS targets are always single-sampled.
            VkSampleCountFlagBits m_drawSamples = VK_SAMPLE_COUNT_1_BIT;

            // Per-draw storage, one buffer per frame in flight so a frame
            // being recorded does not overwrite one the GPU is still reading.
            std::array<VkBuffer, kFramesInFlight> m_drawBuffers{};
            std::array<VkDeviceMemory, kFramesInFlight> m_drawMemories{};
            std::array<void *, kFramesInFlight> m_drawMapped{};
            std::array<VkDeviceSize, kFramesInFlight> m_drawCapacities{};

            std::unordered_map<uint32_t, MeshBuffers> m_meshCache;
            std::vector<GpuDrawData> m_drawStaging;
            std::vector<VkDescriptorSet> m_drawSets;

            // Per-frame geometry (scene::submitDynamicMesh), written each
            // frame into buffers owned by the frame in flight, so a frame
            // being recorded never overwrites one the GPU still reads.
            struct DynamicBuffer
            {
                VkBuffer buffer = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                void *mapped = nullptr;
                VkDeviceSize capacity = 0u;
            };
            std::array<DynamicBuffer, kFramesInFlight> m_dynVertex{};
            std::array<DynamicBuffer, kFramesInFlight> m_dynIndex{};
            std::vector<VkDeviceSize> m_dynVertexOffsets; // per draw, bytes
            std::vector<VkDeviceSize> m_dynIndexOffsets;
            uint64_t m_dynDraws = 0ull;
            bool ensureDynamicBuffer(DynamicBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage);
            void destroyDynamicBuffer(DynamicBuffer &b);

            // Textures, set 1 of the mesh layout. One combined image sampler
            // per texture, all through one repeat sampler: clamp is done in
            // the shader, so the wrap mode, which is per material rather than
            // per texture, needs no sampler of its own.
            //
            // GHPC_VK_TEX=0 draws untextured with no alpha test, which is the
            // picture before texture landed and the control arm for it.
            bool m_textures = true;
            // GHPC_VK_LIGHT=0 skips VU1's lighting and colours by mColor, the
            // picture before lighting landed and its control arm.
            bool m_lighting = true;
            VkDescriptorSetLayout m_texSetLayout = VK_NULL_HANDLE;
            VkDescriptorPool m_texPool = VK_NULL_HANDLE;
            VkSampler m_sampler = VK_NULL_HANDLE;
            // Bound for untextured draws, since the shader declares the
            // sampler and a layout's sets have to be valid when bound.
            GpuTexture m_whiteTexture;
            bool m_whiteReady = false;
            std::unordered_map<uint32_t, GpuTexture> m_texCache;
            std::array<Garbage, kFramesInFlight> m_garbage{};
            uint64_t m_texUploads = 0ull;
            uint64_t m_texReuploads = 0ull;
            uint64_t m_texFailed = 0ull;
            uint64_t m_texMissingDraws = 0ull;
            uint64_t m_texDraws = 0ull;

            int m_meshLogEvery = 0;
            uint64_t m_meshFrames = 0ull;
            uint64_t m_meshSerial = 0ull;
            uint64_t m_meshDraws = 0ull;
            uint64_t m_meshTris = 0ull;
            uint64_t m_meshCacheHits = 0ull;
            uint64_t m_meshCacheMisses = 0ull;
            uint64_t m_meshCacheStale = 0ull; // rebuilt because the key's generation moved
            uint64_t m_meshNoGeometry = 0ull;
            uint64_t m_meshNoCamera = 0ull;
            uint64_t m_pipelineBinds = 0ull;
            uint64_t m_meshSkippedAte = 0ull;
            // Draws whose render target is not the displayed frame. They go
            // to an offscreen GS buffer (the venue's second camera renders
            // 87 draws into a 128x256 one), and drawing them onto the screen
            // put them 192 by 96 pixels out, over the real picture.
            uint64_t m_meshOffscreen = 0ull;

            uint32_t m_frame = 0u;
            bool m_swapchainDirty = false;
            bool m_shouldClose = false;
            bool m_ownsSdlVideo = false;
        };
    }
}

#endif
