#include "ghpc_input.h"

#if !defined(GHPC_ENABLE_VULKAN)

namespace ghpc
{
    bool hostJoystick(input::JoySnapshot &) { return false; }
    void hostInputOpen() {}
    void hostInputPoll(bool) {}
    void hostInputClose() {}
}

#else

#include "ghpc_config.h"
#include "ghpc_input_sdl.h"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>

namespace ghpc
{
    namespace
    {
        bool s_open = false;
        SDL_Joystick *s_joy = nullptr;
        SDL_Gamepad *s_pad = nullptr;

        std::mutex s_mutex;
        input::JoySnapshot s_snap;

        std::string lower(std::string s)
        {
            for (char &c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        void closeDevice()
        {
            if (s_pad)
                SDL_CloseGamepad(s_pad); // closes its joystick too
            else if (s_joy)
                SDL_CloseJoystick(s_joy);
            s_pad = nullptr;
            s_joy = nullptr;
        }

        // [input] device picks by name, else the first joystick SDL lists.
        void chooseDevice()
        {
            const std::string want = lower(settings().get("input", "device"));
            int count = 0;
            SDL_JoystickID *ids = SDL_GetJoysticks(&count);
            SDL_JoystickID pick = 0;
            for (int i = 0; i < count && ids; ++i)
            {
                const char *name = SDL_GetJoystickNameForID(ids[i]);
                if (pick == 0 && (want.empty() || (name && lower(name).find(want) != std::string::npos)))
                    pick = ids[i];
            }
            // SDL announces every joystick already plugged in as added, so the
            // first pump would otherwise reopen the one just opened.
            if (s_joy && pick == SDL_GetJoystickID(s_joy))
            {
                SDL_free(ids);
                return;
            }
            closeDevice();
            for (int i = 0; i < count && ids; ++i)
            {
                const char *name = SDL_GetJoystickNameForID(ids[i]);
                std::fprintf(stderr, "[ghpc/input] found \"%s\"%s\n", name ? name : "?",
                             SDL_IsGamepad(ids[i]) ? " (gamepad)" : "");
            }
            SDL_free(ids);
            if (pick == 0)
            {
                if (count > 0)
                    std::fprintf(stderr, "[ghpc/input] no joystick matches device = %s\n", want.c_str());
                return;
            }

            if (SDL_IsGamepad(pick))
            {
                s_pad = SDL_OpenGamepad(pick);
                s_joy = s_pad ? SDL_GetGamepadJoystick(s_pad) : nullptr;
            }
            else
            {
                s_joy = SDL_OpenJoystick(pick);
            }
            if (!s_joy)
            {
                std::fprintf(stderr, "[ghpc/input] open failed: %s\n", SDL_GetError());
                closeDevice();
                return;
            }
            std::fprintf(stderr, "[ghpc/input] using \"%s\": %d buttons, %d axes, %d hats%s\n",
                         SDL_GetJoystickName(s_joy), SDL_GetNumJoystickButtons(s_joy),
                         SDL_GetNumJoystickAxes(s_joy), SDL_GetNumJoystickHats(s_joy),
                         s_pad ? ", gamepad" : "");
        }
    }

    void hostInputOpen()
    {
        // The controller keeps playing when the window loses focus, e.g. to a
        // second monitor or the terminal that launched it.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        {
            std::fprintf(stderr, "[ghpc/input] SDL_InitSubSystem(GAMEPAD): %s\n", SDL_GetError());
            return;
        }
        s_open = true;
        chooseDevice();
    }

    void hostInputPoll(bool devicesChanged)
    {
        if (!s_open)
            return;
        if (devicesChanged)
            chooseDevice();
        input::JoySnapshot s;
        input::snapshot(s_joy, s_pad, s);
        std::lock_guard<std::mutex> lock(s_mutex);
        s_snap = s;
    }

    void hostInputClose()
    {
        if (!s_open)
            return;
        closeDevice();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        s_open = false;
        std::lock_guard<std::mutex> lock(s_mutex);
        s_snap = input::JoySnapshot{};
    }

    bool hostJoystick(input::JoySnapshot &out)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        out = s_snap;
        return out.connected;
    }
}

#endif
