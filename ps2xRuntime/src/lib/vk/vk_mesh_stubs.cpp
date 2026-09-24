#if defined(GHPC_ENABLE_VULKAN) && !defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        // No shader compiler at build time. Everything up to R1 works without
        // one, so this is a missing feature and not a broken build.
        bool VulkanRenderer::createMeshPipeline()
        {
            std::cerr << "[ghpc/vk/mesh] this build has no compiled shaders; "
                         "install glslang and reconfigure" << std::endl;
            return false;
        }
        void VulkanRenderer::destroyMeshPipeline() {}
        bool VulkanRenderer::createMeshTargets() { return false; }
        void VulkanRenderer::destroyMeshTargets() {}
        bool VulkanRenderer::ensureDrawBuffer(uint32_t, VkDeviceSize) { return false; }
        const MeshBuffers *VulkanRenderer::meshBuffers(uint32_t, uint32_t, uint32_t) { return nullptr; }
        bool VulkanRenderer::uploadTexture(VkCommandBuffer, uint32_t, uint32_t, uint32_t,
                                           const uint8_t *, uint32_t, uint64_t, GpuTexture &) { return false; }
        const GpuTexture *VulkanRenderer::textureFor(VkCommandBuffer, uint32_t, uint32_t,
                                                     std::unordered_map<uint32_t, TargetState> *)
        {
            return nullptr;
        }
        void VulkanRenderer::releaseTexture(uint32_t, GpuTexture &) {}
        VulkanRenderer::RenderTarget *VulkanRenderer::renderTargetFor(VkCommandBuffer, uint32_t, uint32_t,
                                                                      uint32_t) { return nullptr; }
        void VulkanRenderer::destroyRenderTargets() {}
        bool VulkanRenderer::ensureDynamicBuffer(DynamicBuffer &, VkDeviceSize, VkBufferUsageFlags) { return false; }
        void VulkanRenderer::destroyDynamicBuffer(DynamicBuffer &) {}
        void VulkanRenderer::collectGarbage(uint32_t) {}
        void VulkanRenderer::drawScene(VkCommandBuffer, uint32_t, VkRect2D) {}
        void VulkanRenderer::reportMesh() {}
        VkRenderPass VulkanRenderer::scenePassFor(VkSampleCountFlagBits) { return VK_NULL_HANDLE; }
        bool VulkanRenderer::ensureSceneTarget(uint32_t, uint32_t, VkSampleCountFlagBits) { return false; }
        void VulkanRenderer::destroySceneTarget() {}
        void VulkanRenderer::destroyScenePasses() {}
        bool VulkanRenderer::sceneWanted(VkRect2D, uint32_t &, uint32_t &, VkSampleCountFlagBits &) { return false; }
        void VulkanRenderer::blitScene(VkCommandBuffer, VkImage, VkRect2D) {}

    }
}

#endif
