#if defined(GHPC_ENABLE_VULKAN)

#include "ghpc_overlay.h"
#include "ghpc_config.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>

#if defined(GHPC_ENABLE_OVERLAY)
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#endif

namespace ghpc
{
    namespace
    {
        std::atomic<bool> s_ownsKeyboard{false};
    }

    bool overlayOwnsKeyboard()
    {
        return s_ownsKeyboard.load(std::memory_order_relaxed);
    }

#if defined(GHPC_ENABLE_OVERLAY)
    namespace
    {
        // The rows, top to bottom.
        enum Row
        {
            kFullscreen,
            kWidescreen,
            kResolution,
            kMsaa,
            kVolume,
            kFps,
            kUnlockAll,
            kQuit,
            kRows,
        };

        const char *onOff(bool v) { return v ? "ON" : "OFF"; }

        std::string resolutionName(int scale)
        {
            if (scale <= 0)
                return "WINDOW";
            if (scale == 1)
                return "NATIVE";
            return std::to_string(scale) + "X";
        }

        // Left/Right step, Enter goes up and wraps.
        int stepWrap(int v, int dir, int lo, int hi)
        {
            if (dir == 0)
                return v >= hi ? lo : v + 1;
            return std::max(lo, std::min(hi, v + dir));
        }

        void checkVk(VkResult r)
        {
            if (r != VK_SUCCESS)
                std::fprintf(stderr, "[ghpc/overlay] vulkan error %d\n", (int)r);
        }
    }

    bool Overlay::init(VkInstance instance, VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
                       VkQueue queue, VkFormat format, uint32_t imageCount)
    {
        m_device = device;
        m_format = format;

        // Loads onto whatever the frame left there and hands the image to
        // present, so this pass is always the last thing in the frame.
        VkAttachmentDescription color{};
        color.format = format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1u;
        subpass.pColorAttachments = &ref;
        VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        passInfo.attachmentCount = 1u;
        passInfo.pAttachments = &color;
        passInfo.subpassCount = 1u;
        passInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &passInfo, nullptr, &m_pass) != VK_SUCCESS)
        {
            std::fprintf(stderr, "[ghpc/overlay] no render pass; menu off\n");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();

        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_1;
        info.Instance = instance;
        info.PhysicalDevice = physical;
        info.Device = device;
        info.QueueFamily = queueFamily;
        info.Queue = queue;
        info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        info.MinImageCount = std::max(2u, imageCount);
        info.ImageCount = std::max(2u, imageCount);
        info.PipelineInfoMain.RenderPass = m_pass;
        info.CheckVkResultFn = checkVk;
        if (!ImGui_ImplVulkan_Init(&info))
        {
            std::fprintf(stderr, "[ghpc/overlay] ImGui Vulkan backend failed; menu off\n");
            ImGui::DestroyContext();
            vkDestroyRenderPass(device, m_pass, nullptr);
            m_pass = VK_NULL_HANDLE;
            return false;
        }
        m_ready = true;
        return true;
    }

    void Overlay::shutdown()
    {
        if (!m_ready)
            return;
        vkDeviceWaitIdle(m_device);
        releaseSwapchain();
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
        vkDestroyRenderPass(m_device, m_pass, nullptr);
        m_pass = VK_NULL_HANDLE;
        m_ready = false;
        m_open = false;
        s_ownsKeyboard = false;
    }

    bool Overlay::onKey(SDL_Keycode key, bool repeat, Action &action)
    {
        action = Action::None;
        if (!m_ready)
            return false;
        if (!m_open)
        {
            if (key != SDLK_ESCAPE || repeat)
                return false;
            m_open = true;
            s_ownsKeyboard = true;
            return true;
        }
        switch (key)
        {
        case SDLK_ESCAPE:
            if (!repeat)
            {
                m_open = false;
                s_ownsKeyboard = false;
            }
            break;
        case SDLK_UP:
            m_row = (m_row + kRows - 1) % kRows;
            break;
        case SDLK_DOWN:
            m_row = (m_row + 1) % kRows;
            break;
        case SDLK_LEFT:
            change(m_row, -1, action);
            break;
        case SDLK_RIGHT:
            change(m_row, +1, action);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!repeat)
                change(m_row, 0, action);
            break;
        default:
            break;
        }
        return true;
    }

    // dir is -1 or +1 from Left/Right, 0 from Enter. A switch flips on any of
    // them; the volume steps by 10 and Enter mutes or restores it.
    void Overlay::change(int row, int dir, Action &action)
    {
        LiveSettings &l = live();
        switch (row)
        {
        case kFullscreen:
            action = Action::ToggleFullscreen;
            saveSetting("display", "fullscreen", m_fullscreen ? "0" : "1");
            break;
        case kWidescreen:
            l.widescreen = !l.widescreen;
            saveSetting("display", "widescreen", l.widescreen ? "1" : "0");
            break;
        case kResolution:
            l.renderScale = stepWrap(l.renderScale, dir, 0, 4);
            saveSetting("display", "render_scale", std::to_string(l.renderScale.load()));
            break;
        case kMsaa:
        {
            // 1, 2, 4, 8 as steps 0..3, capped at what the GPU does.
            int step = l.msaa >= 8 ? 3 : l.msaa >= 4 ? 2 : l.msaa >= 2 ? 1 : 0;
            const int max = l.msaaMax >= 8 ? 3 : l.msaaMax >= 4 ? 2 : l.msaaMax >= 2 ? 1 : 0;
            step = stepWrap(step, dir, 0, max);
            l.msaa = 1 << step;
            saveSetting("display", "msaa", std::to_string(l.msaa.load()));
            break;
        }
        case kVolume:
        {
            static int s_beforeMute = 100;
            int v = l.volume;
            if (dir == 0)
            {
                if (v > 0)
                {
                    s_beforeMute = v;
                    v = 0;
                }
                else
                    v = s_beforeMute > 0 ? s_beforeMute : 100;
            }
            else
                v = std::max(0, std::min(100, v + dir * 10));
            l.volume = v;
            saveSetting("audio", "volume", std::to_string(v));
            break;
        }
        case kFps:
            l.showFps = !l.showFps;
            saveSetting("display", "show_fps", l.showFps ? "1" : "0");
            break;
        case kUnlockAll:
            l.unlockAll = !l.unlockAll;
            saveSetting("cheats", "unlock_all", l.unlockAll ? "1" : "0");
            break;
        case kQuit:
            if (dir == 0)
                action = Action::Quit;
            break;
        default:
            break;
        }
    }

    bool Overlay::visible() const
    {
        return m_ready && (m_open || live().showFps.load(std::memory_order_relaxed));
    }

    const Overlay::Target *Overlay::targetFor(VkImage image, VkExtent2D extent)
    {
        for (const Target &t : m_targets)
            if (t.image == image && t.extent.width == extent.width && t.extent.height == extent.height)
                return &t;
        Target t;
        t.image = image;
        t.extent = extent;
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &t.view) != VK_SUCCESS)
            return nullptr;
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = m_pass;
        fbInfo.attachmentCount = 1u;
        fbInfo.pAttachments = &t.view;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1u;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &t.framebuffer) != VK_SUCCESS)
        {
            vkDestroyImageView(m_device, t.view, nullptr);
            return nullptr;
        }
        m_targets.push_back(t);
        return &m_targets.back();
    }

    void Overlay::releaseSwapchain()
    {
        for (Target &t : m_targets)
        {
            vkDestroyFramebuffer(m_device, t.framebuffer, nullptr);
            vkDestroyImageView(m_device, t.view, nullptr);
        }
        m_targets.clear();
    }

    void Overlay::build(VkExtent2D extent)
    {
        const float h = static_cast<float>(extent.height);
        // Sized off the window's height, so it reads the same windowed and
        // fullscreen: about 1/30 of the height per line.
        const float scale = std::max(1.0f, h / 540.0f);
        ImGuiStyle &style = ImGui::GetStyle();
        style.FontScaleMain = scale;

        if (live().showFps.load(std::memory_order_relaxed))
        {
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(extent.width) - 8.0f * scale, 8.0f * scale),
                                    ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.45f);
            ImGui::Begin("##fps", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            ImGui::Text("%.0f fps", m_fps);
            ImGui::End();
        }

        if (!m_open)
            return;

        const LiveSettings &l = live();
        struct Line
        {
            const char *label;
            std::string value;
        };
        const Line lines[kRows] = {
            {"Fullscreen", onOff(m_fullscreen)},
            {"Widescreen", onOff(l.widescreen)},
            {"Resolution", resolutionName(l.renderScale)},
            {"Anti-aliasing", l.msaa <= 1 ? std::string("OFF") : std::to_string(l.msaa.load()) + "X MSAA"},
            {"Volume", l.volume == 0 ? std::string("MUTED") : std::to_string(l.volume.load()) + "%"},
            {"FPS counter", onOff(l.showFps)},
            {"Unlock all songs", onOff(l.unlockAll)},
            {"Quit game", ""},
        };

        const float width = 300.0f * scale;
        ImGui::SetNextWindowPos(ImVec2(24.0f * scale, 24.0f * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("##menu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "GHPC");
        ImGui::Separator();
        const ImU32 highlight = IM_COL32(255, 200, 64, 60);
        for (int i = 0; i < kRows; ++i)
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float lineH = ImGui::GetTextLineHeightWithSpacing();
            const float inner = ImGui::GetContentRegionAvail().x;
            if (i == m_row)
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x - 4.0f, at.y), ImVec2(at.x + inner + 4.0f, at.y + lineH),
                                                          highlight, 3.0f);
            const ImVec4 labelColor = i == m_row ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            ImGui::TextColored(labelColor, "%s", lines[i].label);
            if (!lines[i].value.empty())
            {
                const float vw = ImGui::CalcTextSize(lines[i].value.c_str()).x;
                ImGui::SameLine(inner - vw);
                const bool on = lines[i].value != "OFF" && lines[i].value != "MUTED";
                ImGui::TextColored(on ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                                   lines[i].value.c_str());
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Up/Down  Left/Right  Enter  Esc");
        ImGui::End();
    }

    void Overlay::record(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, VkExtent2D extent)
    {
        const Target *t = m_ready ? targetFor(image, extent) : nullptr;
        if (t == nullptr)
            return;

        const uint64_t now = SDL_GetTicksNS();
        ++m_fpsFrames;
        if (m_fpsStart == 0u)
            m_fpsStart = now;
        else if (now - m_fpsStart >= 500000000ull)
        {
            m_fps = static_cast<float>(m_fpsFrames) * 1e9f / static_cast<float>(now - m_fpsStart);
            m_fpsStart = now;
            m_fpsFrames = 0;
        }

        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
        io.DeltaTime = m_lastTicks != 0u ? std::max(1e-4f, static_cast<float>(now - m_lastTicks) * 1e-9f) : 1.0f / 60.0f;
        m_lastTicks = now;
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        build(extent);
        ImGui::Render();

        VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toColor.oldLayout = layout;
        toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColor.image = image;
        toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, nullptr, 0u, nullptr, 1u, &toColor);

        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = m_pass;
        begin.framebuffer = t->framebuffer;
        begin.renderArea.extent = extent;
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
    }

#else // no ImGui in this build: the menu does not exist

    bool Overlay::init(VkInstance, VkPhysicalDevice, VkDevice, uint32_t, VkQueue, VkFormat, uint32_t) { return false; }
    void Overlay::shutdown() {}
    bool Overlay::onKey(SDL_Keycode, bool, Action &action)
    {
        action = Action::None;
        return false;
    }
    bool Overlay::visible() const { return false; }
    void Overlay::record(VkCommandBuffer, VkImage, VkImageLayout, VkExtent2D) {}
    void Overlay::releaseSwapchain() {}

#endif
}

#endif
