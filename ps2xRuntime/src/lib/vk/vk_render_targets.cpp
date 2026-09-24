#if defined(GHPC_ENABLE_VULKAN) && defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        // The offscreen target at `fbp`, made on first use at width x height
        // and cleared to transparent black, as unwritten GS memory reads to a
        // sampler that alpha-tests it away. Kept for the run: a target is a
        // piece of GS memory and the game draws into it again every frame.
        VulkanRenderer::RenderTarget *VulkanRenderer::renderTargetFor(VkCommandBuffer cmd, uint32_t fbp,
                                                                      uint32_t width, uint32_t height)
        {
            auto it = m_renderTargets.find(fbp);
            if (it != m_renderTargets.end())
            {
                return it->second.framebuffer != VK_NULL_HANDLE ? &it->second : nullptr;
            }
            RenderTarget &rt = m_renderTargets[fbp]; // a failure stays, so it is tried once
            if (width == 0u || height == 0u || width > 4096u || height > 4096u)
            {
                return nullptr;
            }

            auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                 VkImage &image, VkDeviceMemory &memory, VkImageView &view) -> bool {
                VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                info.imageType = VK_IMAGE_TYPE_2D;
                info.format = format;
                info.extent = {width, height, 1u};
                info.mipLevels = 1u;
                info.arrayLayers = 1u;
                info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.tiling = VK_IMAGE_TILING_OPTIMAL;
                info.usage = usage;
                info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (!check(vkCreateImage(m_device, &info, nullptr, &image), "vkCreateImage(rt)"))
                {
                    return false;
                }
                VkMemoryRequirements req{};
                vkGetImageMemoryRequirements(m_device, image, &req);
                uint32_t type = 0u;
                VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                alloc.allocationSize = req.size;
                if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type))
                {
                    return false;
                }
                alloc.memoryTypeIndex = type;
                if (!check(vkAllocateMemory(m_device, &alloc, nullptr, &memory), "vkAllocateMemory(rt)"))
                {
                    return false;
                }
                vkBindImageMemory(m_device, image, memory, 0u);
                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = format;
                viewInfo.subresourceRange.aspectMask = aspect;
                viewInfo.subresourceRange.levelCount = 1u;
                viewInfo.subresourceRange.layerCount = 1u;
                return check(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "vkCreateImageView(rt)");
            };

            if (!makeImage(m_swapchainFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, rt.image, rt.memory, rt.view) ||
                !makeImage(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT, rt.depth, rt.depthMemory, rt.depthView))
            {
                return nullptr;
            }

            const std::array<VkImageView, 2> views{rt.view, rt.depthView};
            VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbInfo.renderPass = m_rtPass;
            fbInfo.attachmentCount = (uint32_t)views.size();
            fbInfo.pAttachments = views.data();
            fbInfo.width = width;
            fbInfo.height = height;
            fbInfo.layers = 1u;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (!check(vkCreateFramebuffer(m_device, &fbInfo, nullptr, &framebuffer), "vkCreateFramebuffer(rt)"))
            {
                return nullptr;
            }

            VkDescriptorSetAllocateInfo setAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            setAlloc.descriptorPool = m_texPool;
            setAlloc.descriptorSetCount = 1u;
            setAlloc.pSetLayouts = &m_texSetLayout;
            if (!check(vkAllocateDescriptorSets(m_device, &setAlloc, &rt.texture.set), "vkAllocateDescriptorSets(rt)"))
            {
                vkDestroyFramebuffer(m_device, framebuffer, nullptr);
                return nullptr;
            }
            VkDescriptorImageInfo imageDesc{};
            imageDesc.sampler = m_sampler;
            imageDesc.imageView = rt.view;
            imageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = rt.texture.set;
            write.dstBinding = 0u;
            write.descriptorCount = 1u;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageDesc;
            vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);

            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = 0u;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = rt.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u,
                                 nullptr, 0u, nullptr, 1u, &barrier);
            const VkClearColorValue clear{{0.0f, 0.0f, 0.0f, 0.0f}};
            vkCmdClearColorImage(cmd, rt.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1u,
                                 &barrier.subresourceRange);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &barrier);

            rt.width = width;
            rt.height = height;
            rt.framebuffer = framebuffer;
            rt.texture.image = rt.image;
            rt.texture.memory = rt.memory;
            rt.texture.view = rt.view;
            rt.texture.renderTarget = true;
            std::fprintf(stderr, "[ghpc/vk/rt] target fbp=0x%x %ux%u\n", fbp, width, height);
            return &rt;
        }

        void VulkanRenderer::destroyRenderTargets()
        {
            for (auto &entry : m_renderTargets)
            {
                RenderTarget &rt = entry.second;
                if (rt.framebuffer != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
                if (rt.depthView != VK_NULL_HANDLE)
                    vkDestroyImageView(m_device, rt.depthView, nullptr);
                if (rt.depth != VK_NULL_HANDLE)
                    vkDestroyImage(m_device, rt.depth, nullptr);
                if (rt.depthMemory != VK_NULL_HANDLE)
                    vkFreeMemory(m_device, rt.depthMemory, nullptr);
                // The colour image, its view and set go the way a texture does.
                rt.texture.image = rt.image;
                rt.texture.memory = rt.memory;
                rt.texture.view = rt.view;
                releaseTexture(0u, rt.texture);
            }
            m_renderTargets.clear();
        }

    }
}

#endif
