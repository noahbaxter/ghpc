#pragma once

#include <cstdint>
#include <string>

// Host controller input, between SDL (which only the Vulkan backend and
// ghpcbind include) and the pad stub (which must not).
//
// A controller either has bindings in ghpc.ini's [input] section, written by
// ghpcbind, or is one SDL recognises as a gamepad and gets a fixed layout.
// Guitars are the first kind: a Wii guitar through a raphnet adapter is a
// plain HID joystick whose layout nothing documents.
//
//     green = button 0
//     strum_up = hat 0 up
//     tilt = axis 3 > 0.45
//     whammy = axis 2 -0.98 0.62     ; the axis at rest, then fully pressed
namespace ghpc
{
    class IniFile;

    namespace input
    {
        constexpr int kMaxAxes = 8;
        constexpr int kMaxHats = 4;

        // SDL_GamepadButton and SDL_GamepadAxis values, checked against SDL
        // where SDL is included.
        enum GamepadButton
        {
            kGpSouth = 0, kGpEast, kGpWest, kGpNorth,
            kGpBack, kGpGuide, kGpStart, kGpLeftStick, kGpRightStick,
            kGpLeftShoulder, kGpRightShoulder,
            kGpDpadUp, kGpDpadDown, kGpDpadLeft, kGpDpadRight,
        };
        enum GamepadAxis
        {
            kGpLeftX = 0, kGpLeftY, kGpRightX, kGpRightY, kGpLeftTrigger, kGpRightTrigger,
            kGpAxisCount,
        };

        // SDL_HAT_* bits.
        constexpr uint8_t kHatUp = 1, kHatRight = 2, kHatDown = 4, kHatLeft = 8;

        // One controller's state as SDL last reported it. Axes are -1..1.
        struct JoySnapshot
        {
            bool connected = false;
            char name[96] = {};
            int numButtons = 0;
            int numAxes = 0;
            int numHats = 0;
            uint64_t buttons = 0;
            float axes[kMaxAxes] = {};
            uint8_t hats[kMaxHats] = {};

            bool isGamepad = false;
            uint32_t gpButtons = 0; // bit n is GamepadButton n
            float gpAxes[kGpAxisCount] = {};
        };

        struct Binding
        {
            enum Kind { None, Button, Hat, AxisAbove, AxisBelow };
            Kind kind = None;
            int index = 0;
            uint8_t hatMask = 0;
            float threshold = 0.0f;
        };

        // An axis read as 0 at `rest` and 1 at `full`, either direction.
        struct AxisRange
        {
            int index = -1;
            float rest = 0.0f;
            float full = 0.0f;
        };

        enum Action
        {
            kGreen, kRed, kYellow, kBlue, kOrange,
            kStrumUp, kStrumDown, kStart, kSelect, kTilt,
            kActionCount,
        };

        // The ini key for an action: "green", "strum_up", ...
        const char *actionKey(int action);

        bool parseBinding(const std::string &text, Binding &out);
        std::string formatBinding(const Binding &b);
        bool parseAxisRange(const std::string &text, AxisRange &out);
        std::string formatAxisRange(const AxisRange &r);

        bool held(const Binding &b, const JoySnapshot &s);
        // 0..1, clamped.
        float amount(const AxisRange &r, const JoySnapshot &s);

        struct Bindings
        {
            Binding digital[kActionCount];
            AxisRange whammy;
            bool any = false; // at least one action bound
        };
        Bindings loadBindings(const IniFile &ini);
    }

    // The controller the pad reads, from the Vulkan backend's SDL. False when
    // that backend is not up or no controller is connected. Callable from
    // any thread.
    bool hostJoystick(input::JoySnapshot &out);

    // The backend's side, on the thread that pumps SDL events. Poll after
    // each pump; `devicesChanged` when a joystick came or went.
    void hostInputOpen();
    void hostInputPoll(bool devicesChanged);
    void hostInputClose();
}
