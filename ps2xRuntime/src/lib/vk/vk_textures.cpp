#if defined(GHPC_ENABLE_VULKAN) && defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        void VulkanRenderer::releaseTexture(uint32_t frameIndex, GpuTexture &t)
        {
            Garbage &g = m_garbage[frameIndex % kFramesInFlight];
            if (t.set != VK_NULL_HANDLE)
            {
                g.sets.push_back(t.set);
            }
            if (t.view != VK_NULL_HANDLE)
            {
                g.views.push_back(t.view);
            }
            if (t.image != VK_NULL_HANDLE)
            {
                g.images.push_back(t.image);
            }
            if (t.memory != VK_NULL_HANDLE)
            {
                g.memories.push_back(t.memory);
            }
            t = GpuTexture{};
        }

        void VulkanRenderer::collectGarbage(uint32_t frameIndex)
        {
            Garbage &g = m_garbage[frameIndex % kFramesInFlight];
            if (!g.sets.empty() && m_texPool != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(m_device, m_texPool, (uint32_t)g.sets.size(), g.sets.data());
            }
            for (VkImageView v : g.views)
            {
                vkDestroyImageView(m_device, v, nullptr);
            }
            for (VkImage i : g.images)
            {
                vkDestroyImage(m_device, i, nullptr);
            }
            for (VkBuffer b : g.buffers)
            {
                vkDestroyBuffer(m_device, b, nullptr);
            }
            for (VkDeviceMemory m : g.memories)
            {
                vkFreeMemory(m_device, m, nullptr);
            }
            g = Garbage{};
        }

        // Records the copy into `cmd`, ahead of the render pass that samples
        // it. The staging buffer is released to this frame's garbage list,
        // which is freed once this frame's fence has been waited.
        bool VulkanRenderer::uploadTexture(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t width,
                                           uint32_t height, const uint8_t *rgba, uint32_t texKey,
                                           uint64_t version, GpuTexture &out)
        {
            Garbage &g = m_garbage[frameIndex % kFramesInFlight];
            const VkDeviceSize bytes = (VkDeviceSize)width * height * 4u;

            VkBuffer staging = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = bytes;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &staging), "vkCreateBuffer(tex staging)"))
            {
                return false;
            }
            g.buffers.push_back(staging);
            VkMemoryRequirements stagingReq{};
            vkGetBufferMemoryRequirements(m_device, staging, &stagingReq);
            uint32_t stagingType = 0u;
            if (!findMemoryType(stagingReq.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                stagingType))
            {
                return false;
            }
            VkMemoryAllocateInfo stagingAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            stagingAlloc.allocationSize = stagingReq.size;
            stagingAlloc.memoryTypeIndex = stagingType;
            if (!check(vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory),
                       "vkAllocateMemory(tex staging)"))
            {
                return false;
            }
            g.memories.push_back(stagingMemory);
            vkBindBufferMemory(m_device, staging, stagingMemory, 0u);
            void *mapped = nullptr;
            if (!check(vkMapMemory(m_device, stagingMemory, 0u, bytes, 0u, &mapped), "vkMapMemory(tex staging)"))
            {
                return false;
            }
            // Either the caller's bytes, or straight out of the collector,
            // which refuses when the version has moved on since it was read.
            const bool filled = (rgba != nullptr)
                                    ? (std::memcpy(mapped, rgba, (size_t)bytes), true)
                                    : scene::copyTexture(texKey, version, mapped, bytes);
            vkUnmapMemory(m_device, stagingMemory);
            if (!filled)
            {
                return false;
            }

            GpuTexture t;
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = {width, height, 1u};
            imageInfo.mipLevels = 1u;
            imageInfo.arrayLayers = 1u;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (!check(vkCreateImage(m_device, &imageInfo, nullptr, &t.image), "vkCreateImage(tex)"))
            {
                return false;
            }
            VkMemoryRequirements imageReq{};
            vkGetImageMemoryRequirements(m_device, t.image, &imageReq);
            uint32_t imageType = 0u;
            VkMemoryAllocateInfo imageAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            imageAlloc.allocationSize = imageReq.size;
            if (!findMemoryType(imageReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageType) ||
                (imageAlloc.memoryTypeIndex = imageType,
                 !check(vkAllocateMemory(m_device, &imageAlloc, nullptr, &t.memory), "vkAllocateMemory(tex)")))
            {
                releaseTexture(frameIndex, t);
                return false;
            }
            vkBindImageMemory(m_device, t.image, t.memory, 0u);

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = t.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1u;
            viewInfo.subresourceRange.layerCount = 1u;
            if (!check(vkCreateImageView(m_device, &viewInfo, nullptr, &t.view), "vkCreateImageView(tex)"))
            {
                releaseTexture(frameIndex, t);
                return false;
            }

            VkDescriptorSetAllocateInfo setAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            setAlloc.descriptorPool = m_texPool;
            setAlloc.descriptorSetCount = 1u;
            setAlloc.pSetLayouts = &m_texSetLayout;
            if (!check(vkAllocateDescriptorSets(m_device, &setAlloc, &t.set), "vkAllocateDescriptorSets(tex)"))
            {
                t.set = VK_NULL_HANDLE;
                releaseTexture(frameIndex, t);
                return false;
            }
            VkDescriptorImageInfo imageDesc{};
            imageDesc.sampler = m_sampler;
            imageDesc.imageView = t.view;
            imageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = t.set;
            write.dstBinding = 0u;
            write.descriptorCount = 1u;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageDesc;
            vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);

            VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toDst.srcAccessMask = 0u;
            toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDst.image = t.image;
            toDst.subresourceRange = viewInfo.subresourceRange;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &toDst);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1u;
            region.imageExtent = {width, height, 1u};
            vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);

            VkImageMemoryBarrier toRead = toDst;
            toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &toRead);

            t.version = version;
            out = t;
            return true;
        }

        // The GPU texture for a texKey, uploading it on first sight or when
        // the collector's version has moved. Null when the collector has no
        // decoded image, which the caller draws untextured and counts.
        const GpuTexture *VulkanRenderer::textureFor(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t texKey,
                                                     std::unordered_map<uint32_t, TargetState> *targets)
        {
            uint32_t width = 0u;
            uint32_t height = 0u;
            uint64_t version = 0ull;
            float uvScale[2] = {1.0f, 1.0f};
            uint32_t fbp = 0u;
            uint32_t psm = 0u;
            if (m_rtEnabled && scene::renderTargetTexture(texKey, fbp, width, height, uvScale, psm))
            {
                uint32_t generation = 0u;
                if (targets != nullptr)
                {
                    TargetState &s = (*targets)[fbp];
                    s.sampled = true;
                    generation = s.generation;
                }
                RenderTarget *rt = renderTargetFor(cmd, fbp | (generation << 16), width, height);
                static uint32_t s_logged = 0u;
                if (s_logged < 8u && ++s_logged)
                {
                    std::fprintf(stderr, "[ghpc/vk/rt] tex 0x%08x samples fbp=0x%x (%ux%u) psm=0x%x texa=0x%05x%s\n",
                                 texKey, fbp, width, height, psm, scene::gsTexa(), rt ? "" : " unavailable");
                }
                if (rt == nullptr)
                {
                    return nullptr;
                }
                // The target's own size, which the texture's may not match.
                rt->texture.uvScale[0] = uvScale[0] * (float)width / (float)rt->width;
                rt->texture.uvScale[1] = uvScale[1] * (float)height / (float)rt->height;
                rt->texture.psm = psm;
                return &rt->texture;
            }
            if (!scene::textureInfo(texKey, width, height, version, uvScale))
            {
                return nullptr;
            }

            auto it = m_texCache.find(texKey);
            if (it != m_texCache.end() && it->second.version == version)
            {
                return it->second.image != VK_NULL_HANDLE ? &it->second : nullptr;
            }

            GpuTexture fresh;
            const bool ok = uploadTexture(cmd, frameIndex, width, height, nullptr, texKey, version, fresh);
            if (it != m_texCache.end())
            {
                releaseTexture(frameIndex, it->second);
                ++m_texReuploads;
            }
            if (!ok)
            {
                // Remembered at this version, so a failure costs one attempt
                // and not one a frame.
                ++m_texFailed;
                GpuTexture failed;
                failed.version = version;
                m_texCache[texKey] = failed;
                return nullptr;
            }
            ++m_texUploads;
            fresh.uvScale[0] = uvScale[0];
            fresh.uvScale[1] = uvScale[1];
            GpuTexture &slot = m_texCache[texKey];
            slot = fresh;
            return &slot;
        }

    }
}

#endif
