#pragma once

#include <cstdint>
#include <memory>
#include <string>

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
            bool fullscreen = false;
        };

        virtual ~HostRenderer() = default;

        // The software GS framebuffer. `pixels` is the whole 640x512 buffer, of
        // which only the top-left sourceWidth x sourceHeight is the live
        // picture; the rest is the other context's frame and must not reach the
        // screen. The raylib path has always worked this way, uploading the
        // whole texture and drawing a sub-rect of it.
        struct Frame
        {
            const uint8_t *pixels = nullptr; // RGBA8, bufferWidth * bufferHeight
            uint32_t bufferWidth = 0;
            uint32_t bufferHeight = 0;
            uint32_t sourceWidth = 0;
            uint32_t sourceHeight = 0;
            // Width over height the picture is shown at. PS2 pixels are not
            // square: 512x448 fills a 4:3 TV, or a 16:9 one when the game's
            // widescreen option squeezes its projection. 0 keeps the pixels
            // square, which the 1:1 parity check needs.
            float displayAspect = 0.0f;
        };

        virtual bool init(const Config &config) = 0;
        virtual void shutdown() = 0;

        virtual void uploadFrame(const Frame &frame) = 0;

        // Clear, blit whatever uploadFrame last took, present.
        virtual void present() = 0;

        virtual bool shouldClose() const = 0;
    };

    // Returns null when Vulkan support was not compiled in, or when bring-up
    // failed. The caller falls back to raylib and says so.
    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &config);

    // True when GHPC_RENDERER names the Vulkan backend.
    bool vulkanRendererRequested();

    // Keyboard state of the Vulkan backend's window, for the pad when there
    // is no raylib window to ask. Takes raylib key codes, which the pad
    // already speaks. False for every key when that window does not exist.
    bool hostKeyDown(int raylibKey);

    // A few seconds of status in the window title, e.g. "SAVED menu_x".
    // Callable from any thread; the window's own loop applies it, since
    // neither raylib nor SDL takes a title change off the main thread.
    void hostNotice(const std::string &text);
    // `base` plus any notice still live, for that loop to set.
    std::string hostTitle(const std::string &base);
}
