#pragma once

// JoySnapshot from SDL. Only for files that already include SDL: the Vulkan
// backend's input and ghpcbind.

#include "ghpc_input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace ghpc::input
{
    static_assert(kGpSouth == int(SDL_GAMEPAD_BUTTON_SOUTH) && kGpDpadRight == int(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) &&
                  kGpLeftShoulder == int(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && kGpStart == int(SDL_GAMEPAD_BUTTON_START));
    static_assert(kGpLeftX == int(SDL_GAMEPAD_AXIS_LEFTX) && kGpRightTrigger == int(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    static_assert(kHatUp == SDL_HAT_UP && kHatRight == SDL_HAT_RIGHT && kHatDown == SDL_HAT_DOWN &&
                  kHatLeft == SDL_HAT_LEFT);

    inline float axisFloat(Sint16 v)
    {
        return v < 0 ? v / 32768.0f : v / 32767.0f;
    }

    // `pad` is null for a joystick SDL has no gamepad mapping for.
    inline void snapshot(SDL_Joystick *joy, SDL_Gamepad *pad, JoySnapshot &s)
    {
        s = JoySnapshot{};
        if (!joy || !SDL_JoystickConnected(joy))
            return;
        s.connected = true;
        std::snprintf(s.name, sizeof(s.name), "%s", SDL_GetJoystickName(joy));
        s.numButtons = std::min(SDL_GetNumJoystickButtons(joy), 64);
        s.numAxes = std::min(SDL_GetNumJoystickAxes(joy), kMaxAxes);
        s.numHats = std::min(SDL_GetNumJoystickHats(joy), kMaxHats);
        for (int i = 0; i < s.numButtons; ++i)
            if (SDL_GetJoystickButton(joy, i))
                s.buttons |= 1ull << i;
        for (int i = 0; i < s.numAxes; ++i)
            s.axes[i] = axisFloat(SDL_GetJoystickAxis(joy, i));
        for (int i = 0; i < s.numHats; ++i)
            s.hats[i] = SDL_GetJoystickHat(joy, i);

        if (pad)
        {
            s.isGamepad = true;
            for (int b = 0; b <= kGpDpadRight; ++b)
                if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)b))
                    s.gpButtons |= 1u << b;
            for (int a = 0; a < kGpAxisCount; ++a)
                s.gpAxes[a] = axisFloat(SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a));
        }
    }
}
