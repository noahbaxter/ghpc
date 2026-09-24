#if defined(GHPC_ENABLE_VULKAN) && defined(GHPC_ENABLE_VULKAN_MESH)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
        void VulkanRenderer::drawScene(VkCommandBuffer cmd, uint32_t imageIndex, VkRect2D viewportRect)
        {
            if (!m_meshReady || imageIndex >= m_meshFramebuffers.size())
            {
                return;
            }

            scene::Camera camera{};
            bool cameraValid = false;
            const scene::DrawView *draws = nullptr;
            uint32_t drawCount = 0u;
            uint64_t serial = 0ull;
            if (!scene::acquireFrame(camera, cameraValid, &draws, drawCount, serial))
            {
                return;
            }

            ++m_meshFrames;
            m_meshSerial = serial;

            // GHPC_PRESENTTRACE=path: which guest frame each present shows.
            // The present loop and the guest's vblank run on separate clocks,
            // so a serial shown twice or skipped is judder the game clock
            // itself never shows.
            {
                static FILE *trace = []() -> FILE * {
                    const char *e = std::getenv("GHPC_PRESENTTRACE");
                    FILE *f = (e && *e) ? std::fopen(e, "w") : nullptr;
                    if (f) std::fprintf(f, "wall,present,serial,draws,camera\n");
                    return f;
                }();
                if (trace)
                {
                    static const auto t0 = std::chrono::steady_clock::now();
                    std::fprintf(trace, "%.6f,%llu,%llu,%u,%d\n",
                                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                                 (unsigned long long)m_frame, (unsigned long long)serial, drawCount,
                                 cameraValid ? 1 : 0);
                    if ((m_frame & 63u) == 0u) std::fflush(trace);
                }
            }

            // A frame with no camera cannot be transformed at all, so it is
            // counted and skipped rather than drawn through a stale one.
            if (!cameraValid)
            {
                ++m_meshNoCamera;
                return;
            }

            // present() has waited this frame index's fence, so whatever it
            // last retired is no longer read by the GPU.
            const uint32_t frameIndex = m_frame % kFramesInFlight;
            collectGarbage(frameIndex);

            if (!m_whiteReady)
            {
                const uint8_t white[4] = {0x80u, 0x80u, 0x80u, 0x80u};
                m_whiteReady = uploadTexture(cmd, frameIndex, 1u, 1u, white, 0u, 0ull, m_whiteTexture);
                if (!m_whiteReady)
                {
                    return;
                }
            }

            // Textures are resolved, and uploaded if new, before the render
            // pass begins, since a copy cannot be recorded inside one. The
            // set is copied out rather than pointed to, because a later
            // upload can rehash the cache.
            m_drawSets.clear();
            m_drawSets.reserve(drawCount);

            // On screen means the draw's scissor is the displayed frame. The
            // FRAME base pointer is not the test, since a double buffered
            // frame alternates it; the extent does not move.
            auto onScreen = [&](uint32_t i) {
                const scene::Target &t = draws[i].target;
                return t.scissor[1] + 1u == m_sourceWidth && t.scissor[3] + 1u == m_sourceHeight;
            };

            // A target drawn, sampled, then drawn again within one frame is
            // two images: the crowd renders each character type into the
            // same sheet in turn and draws that type's billboards between.
            // A draw into an FBP that was sampled since its last draw opens
            // a new generation, and a sampler reads the one current at its
            // own place in draw order.
            std::unordered_map<uint32_t, TargetState> targetState;
            m_drawTargetKey.assign(drawCount, 0u);

            m_drawStaging.clear();
            m_drawStaging.reserve(drawCount);
            for (uint32_t i = 0u; i < drawCount; ++i)
            {
                const scene::DrawView &d = draws[i];
                GpuDrawData entry{};
                const scene::Material &mat = d.material;
                const GpuTexture *tex = nullptr;
                if (m_rtEnabled && !onScreen(i))
                {
                    TargetState &s = targetState[d.target.fbp];
                    if (s.sampled)
                    {
                        ++s.generation;
                        s.sampled = false;
                    }
                    m_drawTargetKey[i] = d.target.fbp | (s.generation << 16);
                }
                if (m_textures && mat.texKey != 0u)
                {
                    tex = textureFor(cmd, frameIndex, mat.texKey, &targetState);
                    ++m_texDraws;
                    m_texMissingDraws += (tex == nullptr) ? 1u : 0u;
                }
                entry.uvScale[0] = tex ? tex->uvScale[0] : 1.0f;
                entry.uvScale[1] = tex ? tex->uvScale[1] : 1.0f;
                // Material program 0x17c transforms the UVs before T&L.
                if (mat.uvProgram == 0x17Cu)
                {
                    const float *x = mat.texXfm;
                    entry.uvMode = 1u;
                    entry.uvRows[0] = x[0];
                    entry.uvRows[1] = x[1];
                    entry.uvRows[2] = x[4];
                    entry.uvRows[3] = x[5];
                    entry.uvOffset[0] = x[12];
                    entry.uvOffset[1] = x[13];
                }
                m_drawSets.push_back(tex ? tex->set : m_whiteTexture.set);
                if (m_textures)
                {
                    // Bit layout in ghpc_mesh.frag. The alpha test runs on
                    // untextured draws too, against the vertex alpha, since
                    // that is what the GS tests when TME is off.
                    entry.mode = (tex ? 1u : 0u) |
                                 ((mat.tfx & 3u) << 1) |
                                 ((mat.alphaTest ? 1u : 0u) << 3) |
                                 ((mat.atst & 7u) << 4) |
                                 ((mat.aref & 0xFFu) << 7) |
                                 ((mat.afail & 3u) << 15) |
                                 ((mat.wrapS == 1u ? 1u : 0u) << 17) |
                                 ((mat.wrapT == 1u ? 1u : 0u) << 18) |
                                 ((mat.blend ? 1u : 0u) << 19) |
                                 ((tex && tex->renderTarget ? 1u : 0u) << 22);
                    // PSMCT16 and CT16S keep one alpha bit.
                    if (tex && tex->renderTarget && (tex->psm == 0x02u || tex->psm == 0x0Au))
                    {
                        entry.texa = scene::gsTexa() | (1u << 24);
                    }
                }
                const uint32_t count = std::min<uint32_t>(d.matrixCount, 4u);
                for (uint32_t m = 0u; m < count; ++m)
                {
                    std::memcpy(entry.m[m], d.matrices + (size_t)m * 16u, 16u * sizeof(float));
                }
                entry.matrixCount = count;
                if (m_tint)
                {
                    // A cheap integer hash spread over the three channels,
                    // biased away from black so nothing tints to invisible.
                    uint32_t h = d.meshKey * 2654435761u;
                    h ^= h >> 15;
                    entry.color[0] = 0.25f + 0.75f * (float)((h >> 0) & 0xFFu) / 255.0f;
                    entry.color[1] = 0.25f + 0.75f * (float)((h >> 8) & 0xFFu) / 255.0f;
                    entry.color[2] = 0.25f + 0.75f * (float)((h >> 16) & 0xFFu) / 255.0f;
                    entry.color[3] = 1.0f;
                }
                else
                {
                    std::memcpy(entry.color, d.material.color, sizeof(entry.color));
                }
                // VU1's lighting inputs. Off under tint and ID, which need
                // flat colours to mean anything.
                if (m_lighting && !m_tint && !m_idMode)
                {
                    const scene::Material &lm = d.material;
                    uint32_t lightMode = 2u; // ambient
                    switch (lm.lightProgram)
                    {
                    case 0x614u: lightMode = 1u; break;
                    case 0x6ECu: lightMode = d.lights ? 3u : 2u; break;
                    case 0x436u: lightMode = d.lights ? 4u : 2u; break;
                    case 0x5CCu: lightMode = 5u; break;
                    default: break;
                    }
                    entry.lightMode = lightMode;
                    entry.lightFlags = (lm.prelit ? 1u : 0u) | ((lm.useEnviron && d.lights) ? 2u : 0u);
                    std::memcpy(entry.lightMat, d.lightMatrix, sizeof(entry.lightMat));
                    if (d.lights)
                    {
                        std::memcpy(entry.lights, d.lights->qw, sizeof(entry.lights));
                    }
                    std::memcpy(entry.rgbaScale, lm.rgbaScale, sizeof(entry.rgbaScale));
                    std::memcpy(entry.vuColor, lm.vuColor, sizeof(entry.vuColor));
                    entry.mode |= 1u << 21;
                }
                if (m_idMode)
                {
                    const uint32_t id = i + 1u;
                    entry.color[0] = (float)(id & 0xFFu) / 255.0f;
                    entry.color[1] = (float)((id >> 8) & 0xFFu) / 255.0f;
                    entry.color[2] = (float)0x40u / 255.0f;
                    entry.color[3] = 1.0f;
                    entry.mode |= 1u << 20;
                    DrawRecord r{};
                    r.index = i;
                    r.meshKey = d.meshKey;
                    r.matrixCount = d.matrixCount;
                    r.vertCount = d.vertCount;
                    r.indexCount = d.indexCount;
                    r.material = d.material;
                    // Row vectors, so the translation is the fourth row.
                    std::memcpy(r.translation, d.matrices + 12, 3u * sizeof(float));
                    r.camera = d.camera ? *d.camera : camera;
                    r.target = d.target;
                    if (i == 0u)
                    {
                        m_drawRecords.clear();
                    }
                    m_drawRecords.push_back(r);
                }
                m_drawStaging.push_back(entry);
            }

            // Dynamic geometry, packed into this frame's buffers in draw order.
            {
                VkDeviceSize vertexBytes = 0u;
                VkDeviceSize indexBytes = 0u;
                for (uint32_t i = 0u; i < drawCount; ++i)
                {
                    if (draws[i].dynamicVerts != nullptr)
                    {
                        vertexBytes += (VkDeviceSize)draws[i].vertCount * sizeof(scene::Vertex);
                        indexBytes += (VkDeviceSize)draws[i].indexCount * sizeof(uint16_t);
                    }
                }
                m_dynVertexOffsets.assign(drawCount, 0u);
                m_dynIndexOffsets.assign(drawCount, 0u);
                if (vertexBytes != 0u &&
                    ensureDynamicBuffer(m_dynVertex[frameIndex], vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
                    ensureDynamicBuffer(m_dynIndex[frameIndex], indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
                {
                    VkDeviceSize vo = 0u;
                    VkDeviceSize io = 0u;
                    uint8_t *vdst = static_cast<uint8_t *>(m_dynVertex[frameIndex].mapped);
                    uint8_t *idst = static_cast<uint8_t *>(m_dynIndex[frameIndex].mapped);
                    for (uint32_t i = 0u; i < drawCount; ++i)
                    {
                        const scene::DrawView &d = draws[i];
                        if (d.dynamicVerts == nullptr)
                        {
                            continue;
                        }
                        const size_t vb = (size_t)d.vertCount * sizeof(scene::Vertex);
                        const size_t ib = (size_t)d.indexCount * sizeof(uint16_t);
                        std::memcpy(vdst + vo, d.dynamicVerts, vb);
                        std::memcpy(idst + io, d.dynamicIndices, ib);
                        m_dynVertexOffsets[i] = vo;
                        m_dynIndexOffsets[i] = io;
                        vo += vb;
                        io += ib;
                    }
                }
            }

            if (m_drawStaging.empty() ||
                !ensureDrawBuffer(frameIndex, m_drawStaging.size() * sizeof(GpuDrawData)))
            {
                return;
            }
            std::memcpy(m_drawMapped[frameIndex], m_drawStaging.data(),
                        m_drawStaging.size() * sizeof(GpuDrawData));

            // Zero for GREATER, because the shader keeps the GS's sense of
            // depth: bigger is nearer, so the far plane is zero. LESS needs
            // the opposite clear or nothing passes at all, which would look
            // exactly like a broken transform.
            VkClearValue depthClear{};
            depthClear.depthStencil.depth = (m_depthMode == 2) ? 1.0f : 0.0f;

            // Attachment 0 is LOAD in both passes, so only the depth
            // attachment needs a clear value; it still has to be the second.
            VkClearValue clears[2]{};
            clears[1] = depthClear;

            // Bound state outlives a render pass, so set 0 is bound once.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 0u, 1u,
                                    &m_meshDescriptorSets[frameIndex], 0u, nullptr);

            MeshPush push{};
            std::memcpy(push.cam, camera.m, sizeof(push.cam));
            std::memcpy(push.scale, camera.scale, 3u * sizeof(float));
            std::memcpy(push.offset, camera.offset, 3u * sizeof(float));
            float ofx = 0.0f;
            float ofy = 0.0f;
            scene::gsOffset(ofx, ofy);
            // Half the source picture, not half the window: the transform
            // lands in the guest's framebuffer and the viewport scales that
            // to the letterbox.
            push.viewport[0] = (float)m_sourceWidth * 0.5f;
            push.viewport[1] = (float)m_sourceHeight * 0.5f;
            push.viewport[2] = ofx;
            push.viewport[3] = ofy;

            uint64_t tris = 0ull;
            uint64_t drawn = 0ull;
            const uint32_t firstDraw = std::min(m_drawFirst, drawCount);
            const uint32_t lastDraw = (m_drawLimit == 0u)
                                          ? drawCount
                                          : std::min(firstDraw + m_drawLimit, drawCount);
            // Bound lazily and only when it changes. Draws arrive grouped by
            // material because PsMat::Select binds once for a run of them, so
            // a frame with 534 draws rebinds a handful of times.
            VkPipeline bound = VK_NULL_HANDLE;
            VkDescriptorSet boundSet = VK_NULL_HANDLE;
            float boundFix = -1.0f;

            // One draw into whichever pass is open. push.viewport's first half
            // is the pass's, set by the caller.
            auto drawOne = [&](uint32_t i) {
                // A dynamic draw's geometry is in this frame's buffers, and
                // looking its key up in the mesh cache would cache it forever.
                const bool dynamic = draws[i].dynamicVerts != nullptr &&
                                     m_dynVertex[frameIndex].buffer != VK_NULL_HANDLE;
                const MeshBuffers *buffers =
                    dynamic ? nullptr : meshBuffers(draws[i].meshKey, draws[i].generation, frameIndex);
                if (!dynamic && buffers == nullptr)
                {
                    return;
                }
                const scene::Target &target = draws[i].target;
                if (!m_hiddenMeshes.empty() &&
                    std::find(m_hiddenMeshes.begin(), m_hiddenMeshes.end(), draws[i].meshKey) !=
                        m_hiddenMeshes.end())
                {
                    return;
                }
                const scene::Material &mat = draws[i].material;
                if (m_skipAlphaTested && mat.alphaTest)
                {
                    ++m_meshSkippedAte;
                    return;
                }
                PipelineKey key{};
                key.ztst = mat.ztst & 3u;
                key.depthTest = mat.depthTest ? 1u : 0u;
                key.depthWrite = mat.depthWrite ? 1u : 0u;
                // An ID is only readable unblended: an additive draw would sum
                // its ID with whatever was behind it.
                const bool blend = mat.blend && !m_idMode;
                key.blend = blend ? 1u : 0u;
                key.alphaEq = blend ? (mat.alphaEq & 0xFFu) : 0u;
                // FIX / 128 is the GS's 1.0 at 0x80. Set whenever it moves;
                // every pipeline declares the constant dynamic.
                const float fix = std::min(1.0f, (float)mat.alphaFix / 128.0f);
                if (fix != boundFix)
                {
                    const float constants[4] = {fix, fix, fix, fix};
                    vkCmdSetBlendConstants(cmd, constants);
                    boundFix = fix;
                }
                VkPipeline pipeline = pipelineFor(key);
                if (pipeline == VK_NULL_HANDLE)
                {
                    return;
                }
                if (pipeline != bound)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    bound = pipeline;
                    ++m_pipelineBinds;
                }
                if (m_drawSets[i] != boundSet)
                {
                    boundSet = m_drawSets[i];
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 1u, 1u,
                                            &boundSet, 0u, nullptr);
                }

                // The draw's own camera. Falling back to the frame's last one
                // only covers a draw submitted before any camera, which the
                // collector does not produce once a frame has one.
                const scene::Camera &drawCam = draws[i].camera ? *draws[i].camera : camera;
                std::memcpy(push.cam, drawCam.m, sizeof(push.cam));
                std::memcpy(push.scale, drawCam.scale, 3u * sizeof(float));
                std::memcpy(push.offset, drawCam.offset, 3u * sizeof(float));
                // The draw's own XYOFFSET, from the register shadow at submit.
                push.viewport[2] = target.ofx;
                push.viewport[3] = target.ofy;
                push.drawIndex = i;
                vkCmdPushConstants(cmd, m_meshLayout, VK_SHADER_STAGE_VERTEX_BIT, 0u, sizeof(push), &push);
                if (dynamic)
                {
                    const VkDeviceSize offset = m_dynVertexOffsets[i];
                    vkCmdBindVertexBuffers(cmd, 0u, 1u, &m_dynVertex[frameIndex].buffer, &offset);
                    vkCmdBindIndexBuffer(cmd, m_dynIndex[frameIndex].buffer, m_dynIndexOffsets[i],
                                         VK_INDEX_TYPE_UINT16);
                    vkCmdDrawIndexed(cmd, draws[i].indexCount, 1u, 0u, 0, 0u);
                    tris += draws[i].indexCount / 3u;
                    ++m_dynDraws;
                }
                else
                {
                    const VkDeviceSize offset = 0u;
                    vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffers->vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(cmd, buffers->indexBuffer, 0u, VK_INDEX_TYPE_UINT16);
                    vkCmdDrawIndexed(cmd, buffers->indexCount, 1u, 0u, 0, 0u);
                    tris += buffers->indexCount / 3u;
                }
                ++drawn;
            };

            // Offscreen targets first, so a draw that samples one this frame
            // reads what this frame drew, as it would from GS memory. Grouped
            // by FBP and generation in order of first appearance, draw order
            // kept within.
            std::vector<std::pair<uint32_t, std::vector<uint32_t>>> offscreen;
            for (uint32_t i = firstDraw; i < lastDraw; ++i)
            {
                if (onScreen(i))
                {
                    continue;
                }
                if (!m_rtEnabled)
                {
                    ++m_meshOffscreen;
                    continue;
                }
                const uint32_t key = m_drawTargetKey[i];
                auto it = std::find_if(offscreen.begin(), offscreen.end(),
                                       [key](const auto &g) { return g.first == key; });
                if (it == offscreen.end())
                {
                    offscreen.push_back({key, {}});
                    it = offscreen.end() - 1;
                }
                it->second.push_back(i);
            }
            for (const auto &group : offscreen)
            {
                // Sized by the texture that samples it when one has been
                // bound, which textureFor did above; otherwise by the scissor.
                const scene::Target &first = draws[group.second.front()].target;
                RenderTarget *rt = renderTargetFor(cmd, group.first, first.scissor[1] + 1u, first.scissor[3] + 1u);
                if (rt == nullptr)
                {
                    m_meshOffscreen += group.second.size();
                    continue;
                }
                VkRenderPassBeginInfo rtBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rtBegin.renderPass = m_rtPass;
                rtBegin.framebuffer = rt->framebuffer;
                rtBegin.renderArea.extent = {rt->width, rt->height};
                rtBegin.clearValueCount = 2u;
                rtBegin.pClearValues = clears;
                vkCmdBeginRenderPass(cmd, &rtBegin, VK_SUBPASS_CONTENTS_INLINE);
                // GS pixels one to one: the target is the guest's buffer.
                const VkViewport rtViewport{0.0f, 0.0f, (float)rt->width, (float)rt->height, 0.0f, 1.0f};
                vkCmdSetViewport(cmd, 0u, 1u, &rtViewport);
                push.viewport[0] = (float)rt->width * 0.5f;
                push.viewport[1] = (float)rt->height * 0.5f;
                bound = VK_NULL_HANDLE;
                boundSet = VK_NULL_HANDLE;
                boundFix = -1.0f;
                for (uint32_t i : group.second)
                {
                    // The GS clips to SCISSOR_1, which in a target can be a
                    // sub-rect of it.
                    const scene::Target &t = draws[i].target;
                    const uint32_t x0 = std::min(t.scissor[0], rt->width);
                    const uint32_t y0 = std::min(t.scissor[2], rt->height);
                    const uint32_t x1 = std::min(t.scissor[1] + 1u, rt->width);
                    const uint32_t y1 = std::min(t.scissor[3] + 1u, rt->height);
                    const VkRect2D clip{{(int32_t)x0, (int32_t)y0}, {x1 > x0 ? x1 - x0 : 0u, y1 > y0 ? y1 - y0 : 0u}};
                    vkCmdSetScissor(cmd, 0u, 1u, &clip);
                    drawOne(i);
                    ++m_rtDraws;
                }
                vkCmdEndRenderPass(cmd);

                if (m_rtDump && &group == &offscreen.front())
                {
                    const VkDeviceSize bytes = (VkDeviceSize)rt->width * rt->height * 4u;
                    if (m_rtDumpBuffer == VK_NULL_HANDLE)
                    {
                        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                        info.size = 4096u * 4096u * 4u;
                        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                        VkMemoryRequirements req{};
                        uint32_t type = 0u;
                        if (vkCreateBuffer(m_device, &info, nullptr, &m_rtDumpBuffer) == VK_SUCCESS &&
                            (vkGetBufferMemoryRequirements(m_device, m_rtDumpBuffer, &req), true) &&
                            findMemoryType(req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           type))
                        {
                            VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                            alloc.allocationSize = req.size;
                            alloc.memoryTypeIndex = type;
                            vkAllocateMemory(m_device, &alloc, nullptr, &m_rtDumpMemory);
                            vkBindBufferMemory(m_device, m_rtDumpBuffer, m_rtDumpMemory, 0u);
                            vkMapMemory(m_device, m_rtDumpMemory, 0u, req.size, 0u, &m_rtDumpMapped);
                        }
                    }
                    if (m_rtDumpMapped != nullptr && bytes <= 4096u * 4096u * 4u)
                    {
                        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        b.image = rt->image;
                        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             0u, 0u, nullptr, 0u, nullptr, 1u, &b);
                        VkBufferImageCopy region{};
                        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
                        region.imageExtent = {rt->width, rt->height, 1u};
                        vkCmdCopyImageToBuffer(cmd, rt->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_rtDumpBuffer, 1u,
                                               &region);
                        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                             0u, 0u, nullptr, 0u, nullptr, 1u, &b);
                        m_rtDumpWidth = rt->width;
                        m_rtDumpHeight = rt->height;
                    }
                }
            }

            VkRenderPassBeginInfo passBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            passBegin.renderPass = m_meshPass;
            passBegin.framebuffer = m_meshFramebuffers[imageIndex];
            passBegin.renderArea.extent = m_swapchainExtent;
            passBegin.clearValueCount = 2u;
            passBegin.pClearValues = clears;
            if (m_sceneActive)
            {
                // Offscreen at the scene target's own size and sample count,
                // filling it: present() scales it into the letterbox.
                clears[0].color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
                passBegin.renderPass = scenePassFor(m_scene.samples);
                passBegin.framebuffer = m_scene.framebuffer;
                passBegin.renderArea.extent = {m_scene.width, m_scene.height};
                viewportRect = VkRect2D{{0, 0}, {m_scene.width, m_scene.height}};
                m_drawSamples = m_scene.samples;
                m_sceneDrawn = true;
            }
            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);

            // The same letterboxed rect the software picture is blitted into,
            // so the two land on top of each other and can be compared by
            // looking at them.
            VkViewport viewport{};
            viewport.x = (float)viewportRect.offset.x;
            viewport.y = (float)viewportRect.offset.y;
            viewport.width = (float)viewportRect.extent.width;
            viewport.height = (float)viewportRect.extent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0u, 1u, &viewport);
            vkCmdSetScissor(cmd, 0u, 1u, &viewportRect);
            // Half the source picture, not half the window: the transform
            // lands in the guest's framebuffer and the viewport scales that
            // to the letterbox.
            push.viewport[0] = (float)m_sourceWidth * 0.5f;
            push.viewport[1] = (float)m_sourceHeight * 0.5f;
            bound = VK_NULL_HANDLE;
            boundSet = VK_NULL_HANDLE;
            boundFix = -1.0f;
            for (uint32_t i = firstDraw; i < lastDraw; ++i)
            {
                if (onScreen(i))
                {
                    drawOne(i);
                }
            }

            vkCmdEndRenderPass(cmd);
            m_drawSamples = VK_SAMPLE_COUNT_1_BIT;

            m_meshDraws += drawn;
            m_meshTris += tris;

            if (m_meshLogEvery > 0 && (m_meshFrames % (uint64_t)m_meshLogEvery) == 0ull)
            {
                std::fprintf(stderr,
                             "[ghpc/vk/mesh] frames=%llu serial=%llu submitted=%u drawn=%llu tris=%llu "
                             "cached=%zu hits=%llu misses=%llu stale=%llu noGeom=%llu noCam=%llu "
                             "pipelines=%zu binds=%llu ofx=%.1f ofy=%.1f src=%ux%u "
                             "tex=%d texDraws=%llu texMissing=%llu textures=%zu uploads=%llu "
                             "reuploads=%llu texFailed=%llu offscreen=%llu dynamic=%llu\n",
                             (unsigned long long)m_meshFrames, (unsigned long long)serial,
                             drawCount, (unsigned long long)drawn, (unsigned long long)tris,
                             m_meshCache.size(), (unsigned long long)m_meshCacheHits,
                             (unsigned long long)m_meshCacheMisses, (unsigned long long)m_meshCacheStale,
                             (unsigned long long)m_meshNoGeometry, (unsigned long long)m_meshNoCamera,
                             m_pipelines.size(), (unsigned long long)m_pipelineBinds,
                             ofx, ofy, m_sourceWidth, m_sourceHeight,
                             m_textures ? 1 : 0, (unsigned long long)m_texDraws,
                             (unsigned long long)m_texMissingDraws, m_texCache.size(),
                             (unsigned long long)m_texUploads, (unsigned long long)m_texReuploads,
                             (unsigned long long)m_texFailed, (unsigned long long)m_meshOffscreen,
                             (unsigned long long)m_dynDraws);
            }
        }

        void VulkanRenderer::reportMesh()
        {
            if (m_meshFrames == 0ull)
            {
                return;
            }
            std::cout << "[ghpc/vk/mesh] frames=" << m_meshFrames
                      << " draws=" << m_meshDraws
                      << " tris=" << m_meshTris
                      << " meshes=" << m_meshCache.size()
                      << " hits=" << m_meshCacheHits
                      << " misses=" << m_meshCacheMisses
                      << " stale=" << m_meshCacheStale
                      << " noGeometry=" << m_meshNoGeometry
                      << " noCamera=" << m_meshNoCamera
                      << " pipelines=" << m_pipelines.size()
                      << " pipelineBinds=" << m_pipelineBinds
                      << " skippedAte=" << m_meshSkippedAte << std::endl;
            m_meshFrames = 0ull;
        }

    }
}

#endif
