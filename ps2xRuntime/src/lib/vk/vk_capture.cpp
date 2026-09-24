#if defined(GHPC_ENABLE_VULKAN)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        bool VulkanRenderer::ensureShotBuffer()
        {
            const VkDeviceSize needed =
                (VkDeviceSize)m_swapchainExtent.width * m_swapchainExtent.height * 4u;
            if (m_shotBuffer != VK_NULL_HANDLE && m_shotCapacity >= needed &&
                m_shotWidth == m_swapchainExtent.width && m_shotHeight == m_swapchainExtent.height)
            {
                return true;
            }

            if (m_shotMapped != nullptr)
            {
                vkUnmapMemory(m_device, m_shotMemory);
                m_shotMapped = nullptr;
            }
            if (m_shotBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device, m_shotBuffer, nullptr);
                m_shotBuffer = VK_NULL_HANDLE;
            }
            if (m_shotMemory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, m_shotMemory, nullptr);
                m_shotMemory = VK_NULL_HANDLE;
            }
            m_shotCapacity = 0u;

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = needed;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_shotBuffer), "vkCreateBuffer(shot)"))
            {
                return false;
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(m_device, m_shotBuffer, &requirements);
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
            if (!check(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_shotMemory), "vkAllocateMemory(shot)"))
            {
                return false;
            }
            vkBindBufferMemory(m_device, m_shotBuffer, m_shotMemory, 0u);
            if (!check(vkMapMemory(m_device, m_shotMemory, 0u, needed, 0u, &m_shotMapped), "vkMapMemory(shot)"))
            {
                return false;
            }
            m_shotCapacity = needed;
            m_shotWidth = m_swapchainExtent.width;
            m_shotHeight = m_swapchainExtent.height;
            return true;
        }

        void VulkanRenderer::writeShot()
        {
            if (m_shotMapped == nullptr)
            {
                return;
            }

            const std::string screen = ghpc_drive::taggedScreen();
            char path[256];
            if (m_shotBeatEvery > 0.0)
            {
                // Named by the beat bucket, not by a counter, so run A and run
                // B write the same filenames for the same chart positions and
                // pairing two arms is matching names. A counter cannot do
                // that: an arm that starts capturing one bucket earlier
                // shifts every later index by one.
                std::snprintf(path, sizeof(path), "/tmp/ghpc_vk_b%06lld_%s.ppm",
                              m_shotBeatBucket, screen.c_str());
            }
            else if (m_shotUiEvery > 0.0)
            {
                std::snprintf(path, sizeof(path), "/tmp/ghpc_vk_u%06lld_%s.ppm",
                              m_shotBeatBucket, screen.c_str());
            }
            else
            {
                std::snprintf(path, sizeof(path), "/tmp/ghpc_vk_%d_%s.ppm", m_shots, screen.c_str());
            }
            FILE *f = std::fopen(path, "wb");
            if (f == nullptr)
            {
                return;
            }
            std::fprintf(f, "P6\n%u %u\n255\n", m_shotWidth, m_shotHeight);

            // The swapchain is B8G8R8A8 on every surface this has seen, and
            // the format is checked rather than assumed because a PPM written
            // with the channels swapped looks like a rendering bug.
            const bool bgr = (m_swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                              m_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
            const uint8_t *src = static_cast<const uint8_t *>(m_shotMapped);
            uint64_t lit = 0ull;
            for (uint32_t y = 0u; y < m_shotHeight; ++y)
            {
                for (uint32_t x = 0u; x < m_shotWidth; ++x)
                {
                    const size_t i = ((size_t)y * m_shotWidth + x) * 4u;
                    uint8_t rgb[3];
                    rgb[0] = bgr ? src[i + 2] : src[i + 0];
                    rgb[1] = src[i + 1];
                    rgb[2] = bgr ? src[i + 0] : src[i + 2];
                    // Anything above near-black, the clear on every mode.
                    if (rgb[0] > 20u || rgb[1] > 20u || rgb[2] > 32u)
                    {
                        ++lit;
                    }
                    std::fwrite(rgb, 1, 3, f);
                }
            }
            std::fclose(f);
            ++m_shots;

            if (m_rtDump && m_rtDumpMapped != nullptr && m_rtDumpWidth != 0u)
            {
                std::string rtPath(path);
                rtPath.replace(rtPath.size() - 4u, 4u, "_rt.ppm");
                if (FILE *r = std::fopen(rtPath.c_str(), "wb"))
                {
                    // Colour on the left, alpha as grey on the right.
                    std::fprintf(r, "P6\n%u %u\n255\n", m_rtDumpWidth * 2u, m_rtDumpHeight);
                    const uint8_t *s = static_cast<const uint8_t *>(m_rtDumpMapped);
                    for (uint32_t y = 0u; y < m_rtDumpHeight; ++y)
                    {
                        for (uint32_t x = 0u; x < m_rtDumpWidth * 2u; ++x)
                        {
                            const size_t i = ((size_t)y * m_rtDumpWidth + (x % m_rtDumpWidth)) * 4u;
                            uint8_t rgb[3] = {s[i + 2], s[i + 1], s[i + 0]};
                            if (x >= m_rtDumpWidth)
                                rgb[0] = rgb[1] = rgb[2] = s[i + 3];
                            std::fwrite(rgb, 1, 3, r);
                        }
                    }
                    std::fclose(r);
                }
            }

            if (m_idMode)
            {
                std::string tablePath(path);
                tablePath.replace(tablePath.size() - 4u, 4u, ".txt");
                if (FILE *t = std::fopen(tablePath.c_str(), "w"))
                {
                    std::fprintf(t, "# id index meshKey matrices verts indices ztst zwrite blend "
                                    "texKey tfx ate atst afail tx ty tz cam fbp fbw ofx ofy scissor uvp lp "
                                    "prelit env colA scaleA alphaEq fix\n");
                    // Cameras numbered by first appearance, and listed after
                    // the draws, so a draw's camera is one lookup.
                    std::vector<scene::Camera> cams;
                    for (const DrawRecord &r : m_drawRecords)
                    {
                        const scene::Material &m = r.material;
                        size_t cam = 0u;
                        while (cam < cams.size() &&
                               std::memcmp(&cams[cam], &r.camera, sizeof(scene::Camera)) != 0)
                        {
                            ++cam;
                        }
                        if (cam == cams.size())
                        {
                            cams.push_back(r.camera);
                        }
                        const scene::Target &g = r.target;
                        std::fprintf(t, "%u %u 0x%08x %u %u %u %u %d %d 0x%08x %u %d %u %u %.2f %.2f %.2f %zu "
                                        "%u %u %.0f %.0f %u..%u,%u..%u 0x%x 0x%x %d %d %.3f %.3f 0x%02x %u\n",
                                     r.index + 1u, r.index, r.meshKey, r.matrixCount, r.vertCount,
                                     r.indexCount, m.ztst, m.depthWrite ? 1 : 0, m.blend ? 1 : 0,
                                     m.texKey, m.tfx, m.alphaTest ? 1 : 0, m.atst, m.afail,
                                     r.translation[0], r.translation[1], r.translation[2], cam,
                                     g.fbp, g.fbw, g.ofx, g.ofy,
                                     g.scissor[0], g.scissor[1], g.scissor[2], g.scissor[3],
                                     m.uvProgram, m.lightProgram, m.prelit ? 1 : 0, m.useEnviron ? 1 : 0,
                                     m.color[3], m.rgbaScale[3], m.alphaEq, m.alphaFix);
                    }
                    for (size_t c = 0u; c < cams.size(); ++c)
                    {
                        const scene::Camera &k = cams[c];
                        std::fprintf(t, "# cam %zu scale %.3f %.3f %.3f offset %.3f %.3f %.3f m",
                                     c, k.scale[0], k.scale[1], k.scale[2],
                                     k.offset[0], k.offset[1], k.offset[2]);
                        for (float v : k.m)
                        {
                            std::fprintf(t, " %.4f", v);
                        }
                        std::fprintf(t, "\n");
                    }
                    std::fclose(t);
                }
            }
            // serial is the guest frame, and it is the only field here that
            // two runs can be paired on. `frame` is the host present counter
            // and does not track it: at the same present frame, two arms of
            // one batch were at guest serials 653 and 857. Pairing shots by
            // index or by present frame compares different moments of the
            // chart, and coverage moves by 0.2 iou across 240 present frames,
            // so that difference swamps anything a render change does.
            std::fprintf(stderr,
                         "[ghpc/vk/shot] wrote %s %ux%u lit=%llu of %llu beat=%.3f game=%.3f"
                         " frame=%u serial=%llu mesh=%d screen=%s\n",
                         path, m_shotWidth, m_shotHeight, (unsigned long long)lit,
                         (unsigned long long)m_shotWidth * m_shotHeight,
                         m_shotBeat, m_shotGame, m_frame,
                         (unsigned long long)m_meshSerial, m_meshMode, screen.c_str());
        }

        // Blit parity: the pixels read back off the presented swapchain against
        // the pixels handed to uploadFrame. Only runs on a 1:1 blit, so the
        // comparison is byte for byte with no resampling to excuse a
        // difference. RGBA in, BGRA out, which is the swizzle vkCmdBlitImage is
        // there to do; getting it backwards is exactly what this catches.
        void VulkanRenderer::checkParity(VkRect2D dstRect)
        {
            const auto *src = static_cast<const uint8_t *>(m_stagingMapped);
            const auto *dst = static_cast<const uint8_t *>(m_readbackMapped);
            if (src == nullptr || dst == nullptr)
            {
                return;
            }

            uint64_t mismatched = 0ull;
            for (uint32_t y = 0u; y < dstRect.extent.height; ++y)
            {
                const uint8_t *srcRow = src + static_cast<size_t>(y) * m_bufferWidth * 4u;
                const uint8_t *dstRow = dst + static_cast<size_t>(y) * dstRect.extent.width * 4u;
                for (uint32_t x = 0u; x < dstRect.extent.width; ++x)
                {
                    const uint8_t *s = srcRow + static_cast<size_t>(x) * 4u;
                    const uint8_t *d = dstRow + static_cast<size_t>(x) * 4u;
                    if (s[0] != d[2] || s[1] != d[1] || s[2] != d[0])
                    {
                        ++mismatched;
                    }
                }
            }

            ++m_parityChecked;
            if (mismatched != 0ull)
            {
                ++m_parityMismatched;
                m_parityWorstPixels = std::max(m_parityWorstPixels, mismatched);
            }

            const uint64_t total = static_cast<uint64_t>(dstRect.extent.width) * dstRect.extent.height;
            std::cout << "[ghpc/vk/parity] frame=" << m_frame
                      << " rect=" << dstRect.extent.width << "x" << dstRect.extent.height
                      << " mismatched=" << mismatched << "/" << total
                      << (mismatched == 0ull ? " OK" : " MISMATCH") << std::endl;
        }

    }
}

#endif
