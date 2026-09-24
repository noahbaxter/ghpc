#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

// The in-window menu, trainer style: Esc opens it over the running game (it
// does not pause), Up/Down pick a row, Left/Right or Enter change it, Esc
// closes it. Settings take effect at once and are written to ghpc.ini. While
// it is open the keyboard belongs to it and the game sees no keys; a pad or
// guitar keeps playing.
//
// Drawn with Dear ImGui's Vulkan backend as the last pass of a present, after
// any screenshot copy, so shots never contain it. Built without ImGui
// (GHPC_ENABLE_OVERLAY unset) every call is a no-op.
namespace ghpc
{
    // True while the menu is open; the pad's keyboard bridge reads it.
    bool overlayOwnsKeyboard();

    class Overlay
    {
    public:
        enum class Action
        {
            None,
            ToggleFullscreen,
            Quit,
        };

        bool init(VkInstance instance, VkPhysicalDevice physical, VkDevice device, uint32_t queueFamily,
                  VkQueue queue, VkFormat format, uint32_t imageCount);
        void shutdown();

        // A key press. True when the menu took it, which is every key while
        // it is open and Esc while it is closed. `action` is what the window
        // owner has to do.
        bool onKey(SDL_Keycode key, bool repeat, Action &action);

        // The window's state, for the rows that show it.
        void setFullscreen(bool fullscreen) { m_fullscreen = fullscreen; }

        // Whether record() has anything to draw this frame.
        bool visible() const;

        // Draws into `image`, which is in `layout`, and leaves it in
        // PRESENT_SRC_KHR.
        void record(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, VkExtent2D extent);

        // Before the swapchain's images go away.
        void releaseSwapchain();

    private:
        struct Target
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            VkExtent2D extent{};
        };
        const Target *targetFor(VkImage image, VkExtent2D extent);
        void build(VkExtent2D extent);
        void change(int row, int dir, Action &action);

        bool m_ready = false;
        bool m_open = false;
        bool m_fullscreen = false;
        int m_row = 0;
        VkDevice m_device = VK_NULL_HANDLE;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VkRenderPass m_pass = VK_NULL_HANDLE;
        std::vector<Target> m_targets;
        uint64_t m_lastTicks = 0;
        uint64_t m_fpsStart = 0;
        int m_fpsFrames = 0;
        float m_fps = 0.0f;
    };
}
