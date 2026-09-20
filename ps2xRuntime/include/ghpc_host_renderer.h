#pragma once

#include <cstdint>
#include <memory>

// Host presentation seam.
//
// raylib is OpenGL-only and owns the window today. Vulkan cannot share that
// window, so a second backend needs its own windowing rather than a second
// drawing path inside raylib's. Everything a host backend has to do for the
// runtime is below: open a window, take a framebuffer, put it on screen, and
// say when the user closed it.
//
// R0 only implements the clear. uploadFrame is the R1 entry point and the
// Vulkan backend ignores it until then.
namespace ghpc
{
    class HostRenderer
    {
    public:
        struct Config
        {
            int width = 640;
            int height = 448;
            const char *title = "ghpc";
            bool hidden = false;
            bool unfocused = false;
        };

        virtual ~HostRenderer() = default;

        virtual bool init(const Config &config) = 0;
        virtual void shutdown() = 0;

        // RGBA8, `width` * `height` tightly packed. Ignored in R0.
        virtual void uploadFrame(const uint8_t *pixels, uint32_t width, uint32_t height) = 0;

        // Clear, blit whatever uploadFrame last took, present.
        virtual void present() = 0;

        virtual bool shouldClose() const = 0;
    };

    // Returns null when Vulkan support was not compiled in, or when bring-up
    // failed. The caller falls back to raylib and says so.
    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &config);

    // True when GHPC_RENDERER names the Vulkan backend.
    bool vulkanRendererRequested();
}
