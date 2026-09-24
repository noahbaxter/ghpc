#include "ghpc_host_renderer.h"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <cstring>
#include <iostream>
#include <string>

namespace ghpc
{
    bool vulkanRendererRequested()
    {
        const char *name = std::getenv("GHPC_RENDERER");
        return name != nullptr && std::strcmp(name, "vulkan") == 0;
    }

    namespace
    {
        std::mutex s_noticeMutex;
        std::string s_notice;
        std::chrono::steady_clock::time_point s_noticeUntil;
    }

    void hostNotice(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(s_noticeMutex);
        s_notice = text;
        s_noticeUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }

    std::string hostTitle(const std::string &base)
    {
        std::lock_guard<std::mutex> lock(s_noticeMutex);
        if (s_notice.empty() || std::chrono::steady_clock::now() >= s_noticeUntil)
            return base;
        return base + "   >>> " + s_notice + " <<<";
    }
}

#if !defined(GHPC_ENABLE_VULKAN)

namespace ghpc
{
    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &)
    {
        std::cerr << "[ghpc/vk] GHPC_RENDERER=vulkan but this build has no Vulkan support" << std::endl;
        return nullptr;
    }

    bool hostKeyDown(int)
    {
        return false;
    }
}

#else

#include "vk_renderer.h"

namespace ghpc
{
    // Set once SDL has pumped events, read by hostKeyDown on the pad's thread.
    static std::atomic<bool> s_keysLive{false};
}

namespace ghpc
{
    namespace vk
    {
        bool VulkanRenderer::init(const Config &config)
        {
            if (const char *parity = std::getenv("GHPC_VK_PARITY"); parity != nullptr)
            {
                m_parityEvery = std::atoi(parity);
            }

            if (const char *mesh = std::getenv("GHPC_VK_MESH"); mesh != nullptr)
            {
                m_meshMode = std::atoi(mesh);
            }
            m_meshLogEvery = 60;
            if (const char *meshLog = std::getenv("GHPC_VK_MESH_LOG"); meshLog != nullptr)
            {
                m_meshLogEvery = std::atoi(meshLog);
            }

            if (const char *shot = std::getenv("GHPC_VK_SHOT"); shot != nullptr)
            {
                m_shotEvery = std::atoi(shot);
            }
            if (const char *shotLimit = std::getenv("GHPC_VK_SHOT_LIMIT"); shotLimit != nullptr)
            {
                m_shotLimit = std::atoi(shotLimit);
            }
            if (const char *shotScreen = std::getenv("GHPC_VK_SHOT_SCREEN"); shotScreen != nullptr)
            {
                m_shotScreen = shotScreen;
            }
            if (const char *shotBeat = std::getenv("GHPC_VK_SHOT_BEAT"); shotBeat != nullptr)
            {
                m_shotBeatEvery = std::atof(shotBeat);
            }
            if (const char *shotUi = std::getenv("GHPC_VK_SHOT_UI"); shotUi != nullptr)
            {
                m_shotUiEvery = std::atof(shotUi);
            }
            m_skipAlphaTested = std::getenv("GHPC_VK_MESH_SKIP_ATE") != nullptr;
            m_tint = std::getenv("GHPC_VK_MESH_TINT") != nullptr;
            m_idMode = std::getenv("GHPC_VK_MESH_ID") != nullptr;
            if (const char *hide = std::getenv("GHPC_VK_MESH_HIDE"); hide != nullptr)
            {
                for (const char *p = hide; *p != '\0';)
                {
                    char *end = nullptr;
                    const unsigned long key = std::strtoul(p, &end, 0);
                    if (end == p)
                    {
                        break;
                    }
                    m_hiddenMeshes.push_back((uint32_t)key);
                    // '+' as well as ',', since vkarms.sh splits env pairs
                    // on commas.
                    p = (*end == ',' || *end == '+') ? end + 1 : end;
                }
            }
            if (const char *tex = std::getenv("GHPC_VK_TEX"); tex != nullptr)
            {
                m_textures = std::atoi(tex) != 0;
            }
            if (const char *rt = std::getenv("GHPC_VK_RT"); rt != nullptr)
            {
                m_rtEnabled = std::atoi(rt) != 0;
            }
            m_rtDump = std::getenv("GHPC_VK_RT_DUMP") != nullptr;
            m_shotOverlay = std::getenv("GHPC_VK_SHOT_OVERLAY") != nullptr;
            if (const char *light = std::getenv("GHPC_VK_LIGHT"); light != nullptr)
            {
                m_lighting = std::atoi(light) != 0;
            }
            if (const char *depth = std::getenv("GHPC_VK_MESH_DEPTH"); depth != nullptr)
            {
                m_depthMode = std::atoi(depth);
            }
            if (const char *rangeSpec = std::getenv("GHPC_VK_MESH_RANGE"); rangeSpec != nullptr)
            {
                int first = 0;
                int count = 0;
                if (std::sscanf(rangeSpec, "%d:%d", &first, &count) == 2)
                {
                    m_drawFirst = (uint32_t)std::max(first, 0);
                    m_drawLimit = (uint32_t)std::max(count, 0);
                }
            }

            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            {
                std::cerr << "[ghpc/vk] SDL_InitSubSystem(VIDEO): " << SDL_GetError() << std::endl;
                return false;
            }
            m_ownsSdlVideo = true;

            SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (config.hidden)
            {
                windowFlags |= SDL_WINDOW_HIDDEN;
            }
            if (config.unfocused)
            {
                windowFlags |= SDL_WINDOW_NOT_FOCUSABLE;
            }
            if (config.fullscreen)
            {
                windowFlags |= SDL_WINDOW_FULLSCREEN;
            }

            m_window = SDL_CreateWindow(config.title, config.width, config.height, windowFlags);
            if (m_window == nullptr)
            {
                std::cerr << "[ghpc/vk] SDL_CreateWindow: " << SDL_GetError() << std::endl;
                return false;
            }
            hostInputOpen();

            if (!createInstance())
            {
                return false;
            }

            if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface))
            {
                std::cerr << "[ghpc/vk] SDL_Vulkan_CreateSurface: " << SDL_GetError() << std::endl;
                return false;
            }

            if (!createDevice() || !createSyncObjects() || !createSwapchain())
            {
                return false;
            }
            // The format is the swapchain's and does not change with a resize.
            m_overlay.init(m_instance, m_physicalDevice, m_device, m_queueFamily, m_queue, m_swapchainFormat,
                           static_cast<uint32_t>(m_swapchainImages.size()));

            // R2. Asked for after the swapchain because the render pass needs
            // its format, and before any present so the first frame already
            // has the pipeline.
            //
            // A failed bring-up turns the mesh path off and leaves the rest
            // working, which is the point of having a mode at all: R1's blit
            // is still a correct picture without it.
            if (m_meshMode != 0)
            {
                scene::requestDraws();
                m_meshReady = createMeshPipeline() && createMeshTargets();
                if (!m_meshReady)
                {
                    std::cerr << "[ghpc/vk/mesh] pipeline bring-up failed; mesh path off" << std::endl;
                    destroyMeshPipeline();
                    m_meshMode = 0;
                }
                else
                {
                    std::cout << "[ghpc/vk/mesh] pipeline up, mode=" << m_meshMode
                              << (m_meshMode >= 2 ? " (native only, no software blit)" : " (over the software blit)")
                              << std::endl;
                }
            }

            // Name the path in the title bar, from the mode that actually came
            // up rather than the one asked for, so a fallback shows.
            {
                const char *vu1 = std::getenv("GHPC_VU1_OFF");
                const bool vu1Off = vu1 != nullptr && vu1[0] != '0';
                const char *path = m_meshMode >= 2 ? "native" : m_meshMode == 1 ? "native over software GS" : "software GS";
                m_baseTitle = std::string(config.title ? config.title : "") + "  [Vulkan: " + path +
                              (vu1Off ? ", VU1 off" : "") + "]";
                m_shownTitle = m_baseTitle;
                SDL_SetWindowTitle(m_window, m_baseTitle.c_str());
                std::cout << "[ghpc/vk] title: " << SDL_GetWindowTitle(m_window) << std::endl;
            }

            return true;
        }

        void VulkanRenderer::shutdown()
        {
            if (m_frame != 0u)
            {
                std::cout << "[ghpc/vk] presented " << m_frame << " frames" << std::endl;
                m_frame = 0u;
            }

            if (m_parityChecked != 0ull)
            {
                std::cout << "[ghpc/vk/parity] checked=" << m_parityChecked
                          << " mismatched=" << m_parityMismatched
                          << " worstPixels=" << m_parityWorstPixels
                          << (m_parityMismatched == 0ull ? "  PARITY HELD" : "  PARITY BROKEN") << std::endl;
                m_parityChecked = 0ull;
            }

            reportMesh();

            if (m_device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_device);
                m_overlay.shutdown();
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
                destroyMeshPipeline();
                destroyFrameResources();
                destroySwapchain();

                for (uint32_t i = 0u; i < kFramesInFlight; ++i)
                {
                    if (m_acquireSemaphores[i] != VK_NULL_HANDLE)
                    {
                        vkDestroySemaphore(m_device, m_acquireSemaphores[i], nullptr);
                        m_acquireSemaphores[i] = VK_NULL_HANDLE;
                    }
                    if (m_inFlightFences[i] != VK_NULL_HANDLE)
                    {
                        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
                        m_inFlightFences[i] = VK_NULL_HANDLE;
                    }
                }

                if (m_commandPool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                    m_commandPool = VK_NULL_HANDLE;
                }

                vkDestroyDevice(m_device, nullptr);
                m_device = VK_NULL_HANDLE;
            }

            if (m_surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
                m_surface = VK_NULL_HANDLE;
            }
            if (m_instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
            }
            if (m_window != nullptr)
            {
                SDL_DestroyWindow(m_window);
                m_window = nullptr;
            }
            hostInputClose();
            if (m_ownsSdlVideo)
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                m_ownsSdlVideo = false;
            }
        }

        void VulkanRenderer::uploadFrame(const Frame &frame)
        {
            if (frame.pixels == nullptr || frame.bufferWidth == 0u || frame.bufferHeight == 0u)
            {
                return;
            }

            if (frame.bufferWidth != m_bufferWidth || frame.bufferHeight != m_bufferHeight)
            {
                if (!createFrameResources(frame.bufferWidth, frame.bufferHeight))
                {
                    return;
                }
            }

            const size_t size = static_cast<size_t>(frame.bufferWidth) * frame.bufferHeight * 4u;
            std::memcpy(m_stagingMapped, frame.pixels, size);

            // The sub-rect is clamped rather than trusted: a source larger than
            // the buffer would read past the staging image in the blit.
            m_sourceWidth = std::min(frame.sourceWidth, frame.bufferWidth);
            m_sourceHeight = std::min(frame.sourceHeight, frame.bufferHeight);
            m_displayAspect = m_parityEvery > 0 ? 0.0f : frame.displayAspect;
            m_hasFrame = m_sourceWidth != 0u && m_sourceHeight != 0u;
        }

        void VulkanRenderer::pumpEvents()
        {
            // SDL's key array is only safe to read once SDL is up, and this is
            // the first point it is known to be.
            s_keysLive.store(true, std::memory_order_release);

            // GHPC_OVERLAY_KEYS=esc,down,enter: presses those keys, one every
            // 30 presents from present 240, through the same event path a
            // real key takes. For testing the menu without a keyboard.
            {
                static const std::vector<SDL_Keycode> s_keys = []() {
                    std::vector<SDL_Keycode> keys;
                    const char *e = std::getenv("GHPC_OVERLAY_KEYS");
                    std::string spec = e ? e : "";
                    size_t at = 0;
                    while (at < spec.size())
                    {
                        const size_t comma = spec.find(',', at);
                        const std::string k = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                        if (k == "esc") keys.push_back(SDLK_ESCAPE);
                        else if (k == "up") keys.push_back(SDLK_UP);
                        else if (k == "down") keys.push_back(SDLK_DOWN);
                        else if (k == "left") keys.push_back(SDLK_LEFT);
                        else if (k == "right") keys.push_back(SDLK_RIGHT);
                        else if (k == "enter") keys.push_back(SDLK_RETURN);
                        if (comma == std::string::npos) break;
                        at = comma + 1;
                    }
                    return keys;
                }();
                static size_t s_next = 0;
                if (s_next < s_keys.size() && m_frame >= 240u + 30u * s_next)
                {
                    SDL_Event key{};
                    key.type = SDL_EVENT_KEY_DOWN;
                    key.key.key = s_keys[s_next++];
                    key.key.down = true;
                    SDL_PushEvent(&key);
                }
            }

            SDL_Event event;
            bool devicesChanged = false;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    m_shouldClose = true;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    m_swapchainDirty = true;
                    break;
                case SDL_EVENT_JOYSTICK_ADDED:
                case SDL_EVENT_JOYSTICK_REMOVED:
                    devicesChanged = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                {
                    // The menu first: Esc opens it, and while it is open it
                    // takes every key.
                    Overlay::Action action = Overlay::Action::None;
                    if (m_overlay.onKey(event.key.key, event.key.repeat, action))
                    {
                        if (action == Overlay::Action::ToggleFullscreen)
                        {
                            const bool full = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
                            SDL_SetWindowFullscreen(m_window, !full);
                        }
                        else if (action == Overlay::Action::Quit)
                        {
                            m_shouldClose = true;
                        }
                        break;
                    }
                }
                    // F11, cmd+F, or ctrl+cmd+F as on any Mac app. Not
                    // alt/cmd+enter: enter is the pad's start button.
                    // Fullscreen is borderless at the desktop's size, and the
                    // swapchain follows it.
                    if (!event.key.repeat &&
                        (event.key.key == SDLK_F11 || (event.key.key == SDLK_F && (event.key.mod & SDL_KMOD_GUI))))
                    {
                        const bool full = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(m_window, !full);
                    }
                    break;
                default:
                    break;
                }
            }
            hostInputPoll(devicesChanged);
        }

        void VulkanRenderer::present()
        {
            pumpEvents();

            if (std::string title = hostTitle(m_baseTitle); title != m_shownTitle)
            {
                m_shownTitle = title;
                SDL_SetWindowTitle(m_window, title.c_str());
            }

            if (m_swapchainDirty || m_swapchain == VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_device);
                destroyMeshTargets();
                destroySwapchain();
                m_swapchainDirty = false;
                if (!createSwapchain())
                {
                    // Zero-extent window. Nothing to present until it comes back.
                    return;
                }
                // Views, depth and framebuffers are all swapchain-sized. The
                // render pass and pipeline are not, so they survive a resize.
                if (m_meshMode != 0 && !createMeshTargets())
                {
                    std::cerr << "[ghpc/vk/mesh] targets lost on resize; mesh path off" << std::endl;
                    m_meshReady = false;
                    m_meshMode = 0;
                }
            }

            const uint32_t frame = m_frame % kFramesInFlight;
            vkWaitForFences(m_device, 1u, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);

            uint32_t imageIndex = 0u;
            VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                      m_acquireSemaphores[frame], VK_NULL_HANDLE, &imageIndex);
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
            {
                m_swapchainDirty = true;
                return;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
            {
                check(acquired, "vkAcquireNextImageKHR");
                return;
            }

            vkResetFences(m_device, 1u, &m_inFlightFences[frame]);

            VkCommandBuffer cmd = m_commandBuffers[frame];
            vkResetCommandBuffer(cmd, 0u);

            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);

            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount = 1u;
            range.layerCount = 1u;

            VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toTransfer.srcAccessMask = 0u;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = m_swapchainImages[imageIndex];
            toTransfer.subresourceRange = range;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &toTransfer);

            // Black behind the picture, matching ClearBackground(BLACK) on the
            // raylib path. Magenta when no frame has arrived yet, matching the
            // software GS sentinel for "nothing was drawn here": black would be
            // indistinguishable from a window that never presented at all.
            //
            // Mode 2 used to clear to dark grey, so "drew nothing" differed
            // from "drew black". It is the --native picture now, whose
            // letterbox the PS2 path shows black, and GHPC_VK_MESH_ID and
            // GHPC_VK_MESH_TINT answer that question better.
            const VkClearColorValue clearColor = m_hasFrame
                                                     ? VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}}
                                                     : VkClearColorValue{{1.0f, 0.0f, 1.0f, 1.0f}};
            vkCmdClearColorImage(cmd, m_swapchainImages[imageIndex],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1u, &range);

            VkRect2D dstRect{};
            if (m_hasFrame)
            {
                // Fit the picture into the window at its display aspect,
                // centred. The native path rasterises at the size of this rect
                // and the blit stretches into it, so both de-squeeze together.
                const float srcHeight = static_cast<float>(m_sourceHeight);
                const float srcWidth = m_displayAspect > 0.0f ? srcHeight * m_displayAspect
                                                              : static_cast<float>(m_sourceWidth);
                const float scale = std::min(static_cast<float>(m_swapchainExtent.width) / srcWidth,
                                             static_cast<float>(m_swapchainExtent.height) / srcHeight);
                const int32_t dstWidth = static_cast<int32_t>(srcWidth * scale);
                const int32_t dstHeight = static_cast<int32_t>(srcHeight * scale);
                dstRect.offset.x = (static_cast<int32_t>(m_swapchainExtent.width) - dstWidth) / 2;
                dstRect.offset.y = (static_cast<int32_t>(m_swapchainExtent.height) - dstHeight) / 2;
                dstRect.extent.width = static_cast<uint32_t>(dstWidth);
                dstRect.extent.height = static_cast<uint32_t>(dstHeight);
            }

            // Mode 2 drops the software picture entirely and leaves the
            // letterbox rect it would have gone into, because that is the only
            // arm where the mesh pipeline can be seen to fail: drawn over the
            // blit, a pipeline that renders nothing looks the same as one
            // rendering behind a correct picture.
            if (m_hasFrame && m_meshMode < 2)
            {
                // The clear and the blit both write this image and overlap, so
                // they need ordering even though the layout does not change.
                VkImageMemoryBarrier clearToBlit{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                clearToBlit.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                clearToBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                clearToBlit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                clearToBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                clearToBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearToBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearToBlit.image = m_swapchainImages[imageIndex];
                clearToBlit.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &clearToBlit);

                VkImageMemoryBarrier frameToDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                frameToDst.srcAccessMask = 0u;
                frameToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                frameToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                frameToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                frameToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                frameToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                frameToDst.image = m_frameImage;
                frameToDst.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &frameToDst);

                VkBufferImageCopy copy{};
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1u;
                copy.imageExtent = VkExtent3D{m_bufferWidth, m_bufferHeight, 1u};
                vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_frameImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);

                VkImageMemoryBarrier frameToSrc = frameToDst;
                frameToSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                frameToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                frameToSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                frameToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &frameToSrc);

                // NEAREST, because raylib textures default to point filtering
                // and parity is the whole point of this phase.
                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.layerCount = 1u;
                blit.srcOffsets[1] = VkOffset3D{static_cast<int32_t>(m_sourceWidth),
                                                static_cast<int32_t>(m_sourceHeight), 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.layerCount = 1u;
                blit.dstOffsets[0] = VkOffset3D{dstRect.offset.x, dstRect.offset.y, 0};
                blit.dstOffsets[1] = VkOffset3D{dstRect.offset.x + static_cast<int32_t>(dstRect.extent.width),
                                                dstRect.offset.y + static_cast<int32_t>(dstRect.extent.height), 1};
                vkCmdBlitImage(cmd, m_frameImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1u, &blit, VK_FILTER_NEAREST);
            }

            // R2. After the blit, so native geometry lands on top of the
            // software picture in mode 1, and into the same letterbox rect so
            // the two are directly comparable.
            //
            // Gated on m_hasFrame only because the rect and the source size
            // come from the uploaded frame; the geometry itself comes from the
            // seam and does not depend on the software path at all.
            if (m_hasFrame && m_meshMode != 0)
            {
                // Offscreen when anti-aliasing or a render scale asks for it,
                // then scaled into the letterbox.
                uint32_t sceneW = 0u, sceneH = 0u;
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
                m_sceneActive = sceneWanted(dstRect, sceneW, sceneH, samples) &&
                                ensureSceneTarget(sceneW, sceneH, samples);
                m_sceneDrawn = false;
                drawScene(cmd, imageIndex, dstRect);
                if (m_sceneActive && m_sceneDrawn)
                {
                    // The clear and the blit both write the letterbox.
                    VkImageMemoryBarrier clearToBlit{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    clearToBlit.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    clearToBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    clearToBlit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    clearToBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    clearToBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    clearToBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    clearToBlit.image = m_swapchainImages[imageIndex];
                    clearToBlit.subresourceRange = range;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u,
                                         nullptr, 0u, nullptr, 1u, &clearToBlit);
                    blitScene(cmd, m_swapchainImages[imageIndex], dstRect);
                }
                m_sceneActive = false;
            }

            // The mesh path writes over the blit, so R1's gate would report a
            // mismatch that is the whole point of R2. The two are mutually
            // exclusive on purpose rather than by accident.
            m_parityThisFrame = m_parityEvery > 0 && m_meshMode == 0 && m_hasFrame &&
                                m_readbackBuffer != VK_NULL_HANDLE &&
                                (m_frame % static_cast<uint32_t>(m_parityEvery)) == 0u &&
                                dstRect.extent.width == m_sourceWidth &&
                                dstRect.extent.height == m_sourceHeight;
            if (m_parityThisFrame)
            {
                VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.image = m_swapchainImages[imageIndex];
                toRead.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &toRead);

                VkBufferImageCopy readback{};
                readback.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                readback.imageSubresource.layerCount = 1u;
                readback.imageOffset = VkOffset3D{dstRect.offset.x, dstRect.offset.y, 0};
                readback.imageExtent = VkExtent3D{dstRect.extent.width, dstRect.extent.height, 1u};
                vkCmdCopyImageToBuffer(cmd, m_swapchainImages[imageIndex],
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer, 1u, &readback);
            }

            // The whole presented image, for evidence. Recorded after the mesh
            // pass on purpose: what a shot has to show is what went to the
            // screen, not an intermediate.
            // Beat mode wins when it is set, because a present-frame interval
            // cannot produce comparable shots across runs.
            bool shotDue = false;
            long long candidateBucket = m_shotBeatBucket;
            {
                const ghpc_drive::GameClocks clocks = ghpc_drive::gameClocks();
                m_shotBeat = (double)clocks.beat;
                m_shotGame = (double)clocks.game;
                if (m_shotBeatEvery > 0.0)
                {
                    if (clocks.valid && clocks.beat > 0.0f)
                    {
                        candidateBucket =
                            (long long)std::floor((double)clocks.beat / m_shotBeatEvery);
                        shotDue = candidateBucket != m_shotBeatBucket;
                    }
                }
                else if (m_shotUiEvery > 0.0)
                {
                    if (clocks.valid && clocks.ui > 0.0f)
                    {
                        candidateBucket = (long long)std::floor((double)clocks.ui / m_shotUiEvery);
                        shotDue = candidateBucket != m_shotBeatBucket;
                    }
                }
                else if (m_shotEvery > 0)
                {
                    shotDue = (m_frame % static_cast<uint32_t>(m_shotEvery)) == 0u;
                }
            }

            m_shotThisFrame = shotDue && m_shots < m_shotLimit && m_hasFrame &&
                              m_swapchainReadable &&
                              (m_shotScreen.empty() || ghpc_drive::taggedScreen() == m_shotScreen) &&
                              ensureShotBuffer();
            // Only now, so a bucket rejected by the screen gate or the cap is
            // still available when the run reaches the screen that wants it.
            if (m_shotThisFrame)
            {
                m_shotBeatBucket = candidateBucket;
            }
            // The menu goes on after every capture, so no shot or parity check
            // sees it, except under GHPC_VK_SHOT_OVERLAY, which shoots it to
            // test it. Its pass leaves the image ready to present.
            VkImageLayout layout = m_parityThisFrame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            m_overlay.setFullscreen((SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0);
            const bool overlay = m_overlay.visible();
            if (overlay && m_shotOverlay)
            {
                m_overlay.record(cmd, m_swapchainImages[imageIndex], layout, m_swapchainExtent);
                layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            if (m_shotThisFrame)
            {
                const bool afterOverlay = layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toRead.srcAccessMask = afterOverlay ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
                toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                toRead.oldLayout = layout;
                toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.image = m_swapchainImages[imageIndex];
                toRead.subresourceRange = range;
                vkCmdPipelineBarrier(cmd,
                                     afterOverlay ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                                  : VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &toRead);

                VkBufferImageCopy shot{};
                shot.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                shot.imageSubresource.layerCount = 1u;
                shot.imageExtent = VkExtent3D{m_swapchainExtent.width, m_swapchainExtent.height, 1u};
                vkCmdCopyImageToBuffer(cmd, m_swapchainImages[imageIndex],
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_shotBuffer, 1u, &shot);
                layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            if (overlay && !m_shotOverlay)
            {
                m_overlay.record(cmd, m_swapchainImages[imageIndex], layout, m_swapchainExtent);
                layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            if (layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            {
                VkImageMemoryBarrier toPresent = toTransfer;
                toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toPresent.dstAccessMask = 0u;
                toPresent.oldLayout = layout;
                toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &toPresent);
            }

            vkEndCommandBuffer(cmd);

            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = 1u;
            submit.pWaitSemaphores = &m_acquireSemaphores[frame];
            submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1u;
            submit.pCommandBuffers = &cmd;
            submit.signalSemaphoreCount = 1u;
            submit.pSignalSemaphores = &m_renderSemaphores[imageIndex];
            if (!check(vkQueueSubmit(m_queue, 1u, &submit, m_inFlightFences[frame]), "vkQueueSubmit"))
            {
                return;
            }

            VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            presentInfo.waitSemaphoreCount = 1u;
            presentInfo.pWaitSemaphores = &m_renderSemaphores[imageIndex];
            presentInfo.swapchainCount = 1u;
            presentInfo.pSwapchains = &m_swapchain;
            presentInfo.pImageIndices = &imageIndex;

            const VkResult presented = vkQueuePresentKHR(m_queue, &presentInfo);
            if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
            {
                m_swapchainDirty = true;
            }
            else if (presented != VK_SUCCESS)
            {
                check(presented, "vkQueuePresentKHR");
            }

            if (m_parityThisFrame || m_shotThisFrame)
            {
                // The readback was recorded into this frame's command buffer,
                // so its fence is what says the pixels are there.
                vkWaitForFences(m_device, 1u, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);
                if (m_parityThisFrame)
                {
                    checkParity(dstRect);
                }
                if (m_shotThisFrame)
                {
                    writeShot();
                }
            }

            ++m_frame;
        }

    }

    using vk::VulkanRenderer;

    bool hostKeyDown(int raylibKey)
    {
        if (!s_keysLive.load(std::memory_order_acquire)) return false;
        // The open menu has the keyboard; the game sees every key up.
        if (ghpc::overlayOwnsKeyboard()) return false;
        // raylib's codes are GLFW's: ASCII for letters and digits, 256 and up
        // for the rest. Only the keys the pad reads are mapped.
        SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
        if (raylibKey >= 'A' && raylibKey <= 'Z')
            sc = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (raylibKey - 'A'));
        else if (raylibKey >= '1' && raylibKey <= '9')
            sc = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (raylibKey - '1'));
        else if (raylibKey == '0')
            sc = SDL_SCANCODE_0;
        else
        {
            switch (raylibKey)
            {
            case 257: sc = SDL_SCANCODE_RETURN; break;
            case 262: sc = SDL_SCANCODE_RIGHT; break;
            case 263: sc = SDL_SCANCODE_LEFT; break;
            case 264: sc = SDL_SCANCODE_DOWN; break;
            case 265: sc = SDL_SCANCODE_UP; break;
            case 294: sc = SDL_SCANCODE_F5; break;
            case 341: sc = SDL_SCANCODE_LCTRL; break;
            case 344: sc = SDL_SCANCODE_RSHIFT; break;
            case 345: sc = SDL_SCANCODE_RCTRL; break;
            default: return false;
            }
        }
        int count = 0;
        const bool *keys = SDL_GetKeyboardState(&count);
        return keys != nullptr && static_cast<int>(sc) < count && keys[sc];
    }

    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &config)
    {
        auto renderer = std::make_unique<VulkanRenderer>();
        if (!renderer->init(config))
        {
            std::cerr << "[ghpc/vk] bring-up failed" << std::endl;
            return nullptr;
        }
        return renderer;
    }
}

#endif
