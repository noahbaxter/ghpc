#if defined(GHPC_ENABLE_VULKAN) && defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        bool VulkanRenderer::ensureDrawBuffer(uint32_t frameIndex, VkDeviceSize size)
        {
            if (m_drawCapacities[frameIndex] >= size)
            {
                return true;
            }

            // Grown, not resized to fit, so a frame that gains a draw does not
            // reallocate. The draw count is stable to within a few per cent
            // across gameplay frames.
            VkDeviceSize capacity = std::max<VkDeviceSize>(size, 64u * sizeof(GpuDrawData));
            capacity += capacity / 2u;

            if (m_drawMapped[frameIndex] != nullptr)
            {
                vkUnmapMemory(m_device, m_drawMemories[frameIndex]);
                m_drawMapped[frameIndex] = nullptr;
            }
            if (m_drawBuffers[frameIndex] != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device, m_drawBuffers[frameIndex], nullptr);
                m_drawBuffers[frameIndex] = VK_NULL_HANDLE;
            }
            if (m_drawMemories[frameIndex] != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, m_drawMemories[frameIndex], nullptr);
                m_drawMemories[frameIndex] = VK_NULL_HANDLE;
            }

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = capacity;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_drawBuffers[frameIndex]),
                       "vkCreateBuffer(draws)"))
            {
                return false;
            }

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(m_device, m_drawBuffers[frameIndex], &requirements);
            uint32_t typeIndex = 0u;
            if (!findMemoryType(requirements.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                typeIndex))
            {
                return false;
            }

            VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = typeIndex;
            if (!check(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_drawMemories[frameIndex]),
                       "vkAllocateMemory(draws)"))
            {
                return false;
            }
            vkBindBufferMemory(m_device, m_drawBuffers[frameIndex], m_drawMemories[frameIndex], 0u);
            if (!check(vkMapMemory(m_device, m_drawMemories[frameIndex], 0u, capacity, 0u,
                                   &m_drawMapped[frameIndex]),
                       "vkMapMemory(draws)"))
            {
                return false;
            }
            m_drawCapacities[frameIndex] = capacity;

            VkDescriptorBufferInfo bufferDesc{};
            bufferDesc.buffer = m_drawBuffers[frameIndex];
            bufferDesc.offset = 0u;
            bufferDesc.range = capacity;

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_meshDescriptorSets[frameIndex];
            write.dstBinding = 0u;
            write.descriptorCount = 1u;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bufferDesc;
            vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
            return true;
        }

        const MeshBuffers *VulkanRenderer::meshBuffers(uint32_t meshKey, uint32_t generation, uint32_t frameIndex)
        {
            auto it = m_meshCache.find(meshKey);
            if (it != m_meshCache.end())
            {
                if (it->second.generation == generation)
                {
                    ++m_meshCacheHits;
                    return it->second.indexCount != 0u ? &it->second : nullptr;
                }
                // The key is a guest address and the game has put another mesh
                // there. Retire the old buffers behind this frame's fence
                // rather than destroying them under a frame still in flight.
                ++m_meshCacheStale;
                Garbage &g = m_garbage[frameIndex % kFramesInFlight];
                const MeshBuffers &old = it->second;
                if (old.vertexBuffer != VK_NULL_HANDLE) g.buffers.push_back(old.vertexBuffer);
                if (old.vertexMemory != VK_NULL_HANDLE) g.memories.push_back(old.vertexMemory);
                if (old.indexBuffer != VK_NULL_HANDLE) g.buffers.push_back(old.indexBuffer);
                if (old.indexMemory != VK_NULL_HANDLE) g.memories.push_back(old.indexMemory);
                m_meshCache.erase(it);
            }

            ++m_meshCacheMisses;

            std::vector<scene::Vertex> geomVerts;
            std::vector<uint16_t> geomIndices;
            uint32_t geomGeneration = 0u;
            const bool found = scene::geometry(meshKey, geomVerts, geomIndices, geomGeneration);
            const scene::Vertex *verts = geomVerts.data();
            const uint16_t *indices = geomIndices.data();
            const uint32_t vertCount = (uint32_t)geomVerts.size();
            const uint32_t indexCount = (uint32_t)geomIndices.size();
            if (!found || vertCount == 0u || indexCount == 0u)
            {
                // Cached as empty so a mesh with no usable geometry is looked
                // up once per generation rather than every frame.
                ++m_meshNoGeometry;
                MeshBuffers empty;
                empty.generation = generation;
                m_meshCache.emplace(meshKey, empty);
                return nullptr;
            }

            // Host-visible and written once. On a discrete GPU this wants a
            // staging copy into device-local memory; on the unified memory
            // this is developed against there is nothing to copy to. Upload
            // happens once per mesh either way, so it is not the hot path.
            auto makeBuffer = [&](VkBufferUsageFlags usage, const void *data, VkDeviceSize size,
                                  VkBuffer &buffer, VkDeviceMemory &memory) -> bool {
                VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bufferInfo.size = size;
                bufferInfo.usage = usage;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                if (!check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer(mesh)"))
                {
                    return false;
                }
                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(m_device, buffer, &requirements);
                uint32_t typeIndex = 0u;
                if (!findMemoryType(requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    typeIndex))
                {
                    return false;
                }
                VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocInfo.allocationSize = requirements.size;
                allocInfo.memoryTypeIndex = typeIndex;
                if (!check(vkAllocateMemory(m_device, &allocInfo, nullptr, &memory), "vkAllocateMemory(mesh)"))
                {
                    return false;
                }
                vkBindBufferMemory(m_device, buffer, memory, 0u);
                void *mapped = nullptr;
                if (!check(vkMapMemory(m_device, memory, 0u, size, 0u, &mapped), "vkMapMemory(mesh)"))
                {
                    return false;
                }
                std::memcpy(mapped, data, (size_t)size);
                vkUnmapMemory(m_device, memory);
                return true;
            };

            MeshBuffers buffers;
            buffers.generation = generation;
            const VkDeviceSize vertexBytes = (VkDeviceSize)vertCount * sizeof(scene::Vertex);
            const VkDeviceSize indexBytes = (VkDeviceSize)indexCount * sizeof(uint16_t);
            if (!makeBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts, vertexBytes,
                            buffers.vertexBuffer, buffers.vertexMemory) ||
                !makeBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices, indexBytes,
                            buffers.indexBuffer, buffers.indexMemory))
            {
                m_meshCache.emplace(meshKey, buffers);
                return nullptr;
            }
            buffers.indexCount = indexCount;

            auto inserted = m_meshCache.emplace(meshKey, buffers);
            return &inserted.first->second;
        }

        void VulkanRenderer::destroyDynamicBuffer(DynamicBuffer &b)
        {
            if (b.mapped != nullptr)
            {
                vkUnmapMemory(m_device, b.memory);
            }
            if (b.buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device, b.buffer, nullptr);
            }
            if (b.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, b.memory, nullptr);
            }
            b = DynamicBuffer{};
        }

        // Grown, never shrunk, with headroom, like the draw buffer. Only
        // called for the frame index whose fence was just waited, so the old
        // buffer is not in flight when it is replaced.
        bool VulkanRenderer::ensureDynamicBuffer(DynamicBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage)
        {
            if (b.capacity >= size)
            {
                return true;
            }
            destroyDynamicBuffer(b);
            const VkDeviceSize capacity = std::max<VkDeviceSize>(size + size / 2u, 64u * 1024u);

            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = capacity;
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!check(vkCreateBuffer(m_device, &info, nullptr, &b.buffer), "vkCreateBuffer(dynamic)"))
            {
                return false;
            }
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_device, b.buffer, &req);
            uint32_t type = 0u;
            if (!findMemoryType(req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, type))
            {
                destroyDynamicBuffer(b);
                return false;
            }
            VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            alloc.allocationSize = req.size;
            alloc.memoryTypeIndex = type;
            if (!check(vkAllocateMemory(m_device, &alloc, nullptr, &b.memory), "vkAllocateMemory(dynamic)"))
            {
                destroyDynamicBuffer(b);
                return false;
            }
            vkBindBufferMemory(m_device, b.buffer, b.memory, 0u);
            if (!check(vkMapMemory(m_device, b.memory, 0u, capacity, 0u, &b.mapped), "vkMapMemory(dynamic)"))
            {
                destroyDynamicBuffer(b);
                return false;
            }
            b.capacity = capacity;
            return true;
        }

    }
}

#endif
