#if defined(GHPC_ENABLE_VULKAN) && defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"
#include "ghpc_config.h"

namespace ghpc
{
    namespace vk
    {
        // One pass per sample count. Colour clears to opaque black (the
        // letterbox is black too); a multisampled colour resolves into a
        // single-sample image, and whichever holds the picture ends in
        // TRANSFER_SRC for the blit into the window.
        VkRenderPass VulkanRenderer::scenePassFor(VkSampleCountFlagBits samples)
        {
            auto it = m_scenePasses.find(static_cast<uint32_t>(samples));
            if (it != m_scenePasses.end())
            {
                return it->second;
            }
            const bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
            std::array<VkAttachmentDescription, 3> attachments{};
            attachments[0].format = m_swapchainFormat;
            attachments[0].samples = samples;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout =
                msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            attachments[1].format = VK_FORMAT_D32_SFLOAT;
            attachments[1].samples = samples;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[2].format = m_swapchainFormat;
            attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            const VkAttachmentReference colorRef{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            const VkAttachmentReference depthRef{1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            const VkAttachmentReference resolveRef{2u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1u;
            subpass.pColorAttachments = &colorRef;
            subpass.pResolveAttachments = msaa ? &resolveRef : nullptr;
            subpass.pDepthStencilAttachment = &depthRef;

            std::array<VkSubpassDependency, 2> deps{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0u;
            // The previous frame's blit read and depth writes, since both
            // frames in flight share the one target.
            deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
            deps[1].srcSubpass = 0u;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            info.attachmentCount = msaa ? 3u : 2u;
            info.pAttachments = attachments.data();
            info.subpassCount = 1u;
            info.pSubpasses = &subpass;
            info.dependencyCount = static_cast<uint32_t>(deps.size());
            info.pDependencies = deps.data();
            VkRenderPass pass = VK_NULL_HANDLE;
            if (!check(vkCreateRenderPass(m_device, &info, nullptr, &pass), "vkCreateRenderPass(scene)"))
            {
                return VK_NULL_HANDLE;
            }
            m_scenePasses[static_cast<uint32_t>(samples)] = pass;
            return pass;
        }

        void VulkanRenderer::destroyScenePasses()
        {
            for (auto &entry : m_scenePasses)
            {
                vkDestroyRenderPass(m_device, entry.second, nullptr);
            }
            m_scenePasses.clear();
        }

        // Whether this present draws offscreen, and at what size and sample
        // count. Only the native-only picture (mode 2) can: mode 1 draws over
        // the software blit, which a cleared offscreen target would hide.
        bool VulkanRenderer::sceneWanted(VkRect2D dstRect, uint32_t &width, uint32_t &height,
                                         VkSampleCountFlagBits &samples)
        {
            LiveSettings &l = live();
            // The device's ceiling, for the overlay to stop at.
            static const int s_max = [this]() {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
                const VkSampleCountFlags ok =
                    props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
                int best = 1;
                for (int s : {2, 4, 8})
                {
                    if (ok & static_cast<VkSampleCountFlags>(s))
                    {
                        best = s;
                    }
                }
                return best;
            }();
            l.msaaMax = s_max;
            const int msaa = std::min(std::max(1, l.msaa.load(std::memory_order_relaxed)), s_max);
            const int scale = l.renderScale.load(std::memory_order_relaxed);
            if (m_meshMode != 2 || (msaa <= 1 && scale <= 0))
            {
                return false;
            }
            samples = static_cast<VkSampleCountFlagBits>(msaa);
            if (scale <= 0)
            {
                width = dstRect.extent.width;
                height = dstRect.extent.height;
            }
            else
            {
                // The guest's picture height times the scale, at the shape
                // it is shown: 448 lines is 1x, whatever the window.
                height = m_sourceHeight * static_cast<uint32_t>(scale);
                width = static_cast<uint32_t>(static_cast<float>(height) * static_cast<float>(dstRect.extent.width) /
                                                  static_cast<float>(std::max(1u, dstRect.extent.height)) +
                                              0.5f);
            }
            width = std::min(std::max(width, 1u), 8192u);
            height = std::min(std::max(height, 1u), 8192u);
            return true;
        }

        bool VulkanRenderer::ensureSceneTarget(uint32_t width, uint32_t height, VkSampleCountFlagBits samples)
        {
            if (m_scene.framebuffer != VK_NULL_HANDLE && m_scene.width == width && m_scene.height == height &&
                m_scene.samples == samples)
            {
                return true;
            }
            // A resize or a new setting: rare, so waiting out the frames that
            // still use the old images is simpler than retiring them.
            vkDeviceWaitIdle(m_device);
            destroySceneTarget();
            const VkRenderPass pass = scenePassFor(samples);
            if (pass == VK_NULL_HANDLE)
            {
                return false;
            }
            const bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;

            auto makeImage = [&](VkFormat format, VkSampleCountFlagBits count, VkImageUsageFlags usage,
                                 VkImageAspectFlags aspect, VkImage &image, VkDeviceMemory &memory,
                                 VkImageView &view) -> bool {
                VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                info.imageType = VK_IMAGE_TYPE_2D;
                info.format = format;
                info.extent = {width, height, 1u};
                info.mipLevels = 1u;
                info.arrayLayers = 1u;
                info.samples = count;
                info.tiling = VK_IMAGE_TILING_OPTIMAL;
                info.usage = usage;
                info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (!check(vkCreateImage(m_device, &info, nullptr, &image), "vkCreateImage(scene)"))
                {
                    return false;
                }
                VkMemoryRequirements req{};
                vkGetImageMemoryRequirements(m_device, image, &req);
                uint32_t type = 0u;
                if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type))
                {
                    return false;
                }
                VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                alloc.allocationSize = req.size;
                alloc.memoryTypeIndex = type;
                if (!check(vkAllocateMemory(m_device, &alloc, nullptr, &memory), "vkAllocateMemory(scene)"))
                {
                    return false;
                }
                vkBindImageMemory(m_device, image, memory, 0u);
                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = format;
                viewInfo.subresourceRange = {aspect, 0u, 1u, 0u, 1u};
                return check(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "vkCreateImageView(scene)");
            };

            const VkImageUsageFlags colorUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (msaa ? 0u : VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            bool ok = makeImage(m_swapchainFormat, samples, colorUsage, VK_IMAGE_ASPECT_COLOR_BIT, m_scene.color,
                                m_scene.colorMemory, m_scene.colorView) &&
                      makeImage(VK_FORMAT_D32_SFLOAT, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT, m_scene.depth, m_scene.depthMemory, m_scene.depthView);
            if (ok && msaa)
            {
                ok = makeImage(m_swapchainFormat, VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, m_scene.resolve, m_scene.resolveMemory, m_scene.resolveView);
            }
            if (ok)
            {
                const std::array<VkImageView, 3> views{m_scene.colorView, m_scene.depthView, m_scene.resolveView};
                VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                fbInfo.renderPass = pass;
                fbInfo.attachmentCount = msaa ? 3u : 2u;
                fbInfo.pAttachments = views.data();
                fbInfo.width = width;
                fbInfo.height = height;
                fbInfo.layers = 1u;
                ok = check(vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_scene.framebuffer),
                           "vkCreateFramebuffer(scene)");
            }
            if (!ok)
            {
                destroySceneTarget();
                return false;
            }
            m_scene.width = width;
            m_scene.height = height;
            m_scene.samples = samples;
            std::fprintf(stderr, "[ghpc/vk/scene] target %ux%u msaa=%d\n", width, height, (int)samples);
            return true;
        }

        void VulkanRenderer::destroySceneTarget()
        {
            if (m_scene.framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(m_device, m_scene.framebuffer, nullptr);
            }
            for (VkImageView v : {m_scene.colorView, m_scene.resolveView, m_scene.depthView})
            {
                if (v != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device, v, nullptr);
                }
            }
            for (VkImage i : {m_scene.color, m_scene.resolve, m_scene.depth})
            {
                if (i != VK_NULL_HANDLE)
                {
                    vkDestroyImage(m_device, i, nullptr);
                }
            }
            for (VkDeviceMemory m : {m_scene.colorMemory, m_scene.resolveMemory, m_scene.depthMemory})
            {
                if (m != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device, m, nullptr);
                }
            }
            m_scene = SceneTarget{};
        }

        // The finished picture into the window's letterbox, which is in
        // TRANSFER_DST. Linear when it scales, exact when it does not.
        void VulkanRenderer::blitScene(VkCommandBuffer cmd, VkImage swapchainImage, VkRect2D dstRect)
        {
            const VkImage src = m_scene.resolve != VK_NULL_HANDLE ? m_scene.resolve : m_scene.color;
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
            blit.srcOffsets[1] = VkOffset3D{static_cast<int32_t>(m_scene.width), static_cast<int32_t>(m_scene.height), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
            blit.dstOffsets[0] = VkOffset3D{dstRect.offset.x, dstRect.offset.y, 0};
            blit.dstOffsets[1] = VkOffset3D{dstRect.offset.x + static_cast<int32_t>(dstRect.extent.width),
                                            dstRect.offset.y + static_cast<int32_t>(dstRect.extent.height), 1};
            const bool same = m_scene.width == dstRect.extent.width && m_scene.height == dstRect.extent.height;
            vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit, same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
        }
    }
}

#endif
