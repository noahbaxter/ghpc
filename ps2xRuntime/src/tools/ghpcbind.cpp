// ghpcbind: record a controller's layout into ghpc.ini.
//
//     ghpcbind [ini]            walk every action, write [input]
//     ghpcbind --probe [ini]    print the controller's raw state, live
//     ghpcbind --list           list the joysticks SDL sees
//
// For controllers nothing documents, a Wii guitar through a raphnet adapter
// first among them. Each action is found by what moves away from the resting
// state captured at the start, so the controller must be left alone then.
// Leave an action untouched for 10 seconds to skip it.

#include "ghpc_config.h"
#include "ghpc_input_sdl.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace ghpc::input;

namespace
{
    SDL_Joystick *g_joy = nullptr;

    JoySnapshot read()
    {
        SDL_UpdateJoysticks();
        JoySnapshot s;
        snapshot(g_joy, nullptr, s);
        return s;
    }

    bool sameControl(const Binding &a, const Binding &b)
    {
        if (a.kind == Binding::None || b.kind == Binding::None)
            return false;
        const bool axisA = a.kind == Binding::AxisAbove || a.kind == Binding::AxisBelow;
        const bool axisB = b.kind == Binding::AxisAbove || b.kind == Binding::AxisBelow;
        if (axisA && axisB)
            return a.index == b.index && a.kind == b.kind;
        return a.kind == b.kind && a.index == b.index && a.hatMask == b.hatMask;
    }

    // The first control away from rest: a button, a hat direction, or an
    // axis moved over half its travel.
    bool moved(const JoySnapshot &rest, const JoySnapshot &now, Binding &out)
    {
        const uint64_t pressed = now.buttons & ~rest.buttons;
        for (int i = 0; i < now.numButtons; ++i)
            if ((pressed >> i) & 1u)
            {
                out = Binding{Binding::Button, i, 0, 0.0f};
                return true;
            }
        for (int i = 0; i < now.numHats; ++i)
        {
            const uint8_t bits = now.hats[i] & ~rest.hats[i];
            for (uint8_t m : {kHatUp, kHatRight, kHatDown, kHatLeft})
                if (bits & m)
                {
                    out = Binding{Binding::Hat, i, m, 0.0f};
                    return true;
                }
        }
        for (int i = 0; i < now.numAxes; ++i)
        {
            const float d = now.axes[i] - rest.axes[i];
            if (std::fabs(d) > 0.5f)
            {
                out = Binding{d > 0 ? Binding::AxisAbove : Binding::AxisBelow, i, 0, 0.0f};
                return true;
            }
        }
        return false;
    }

    bool atRest(const JoySnapshot &rest, const JoySnapshot &now)
    {
        if ((now.buttons & ~rest.buttons) != 0)
            return false;
        for (int i = 0; i < now.numHats; ++i)
            if ((now.hats[i] & ~rest.hats[i]) != 0)
                return false;
        for (int i = 0; i < now.numAxes; ++i)
            if (std::fabs(now.axes[i] - rest.axes[i]) > 0.25f)
                return false;
        return true;
    }

    void waitForRest(const JoySnapshot &rest)
    {
        while (!atRest(rest, read()))
            SDL_Delay(5);
    }

    // An axis binding's threshold sits halfway to the furthest it went in
    // the next 300 ms, so an accelerometer that only just clears half travel
    // still switches cleanly.
    void settleAxis(const JoySnapshot &rest, Binding &b)
    {
        float peak = read().axes[b.index];
        const Uint64 until = SDL_GetTicks() + 300;
        while (SDL_GetTicks() < until)
        {
            const float v = read().axes[b.index];
            if (std::fabs(v - rest.axes[b.index]) > std::fabs(peak - rest.axes[b.index]))
                peak = v;
            SDL_Delay(5);
        }
        b.threshold = (rest.axes[b.index] + peak) * 0.5f;
    }

    bool bindDigital(const JoySnapshot &rest, const char *prompt, const Binding *taken, int takenCount,
                     Binding &out)
    {
        std::printf("  %-34s", prompt);
        std::fflush(stdout);
        const Uint64 deadline = SDL_GetTicks() + 10000;
        while (SDL_GetTicks() < deadline)
        {
            Binding b;
            if (!moved(rest, read(), b))
            {
                SDL_Delay(5);
                continue;
            }
            if (b.kind == Binding::AxisAbove || b.kind == Binding::AxisBelow)
                settleAxis(rest, b);
            bool dup = false;
            for (int i = 0; i < takenCount; ++i)
                dup = dup || sameControl(taken[i], b);
            waitForRest(rest);
            if (dup)
            {
                std::printf("%s is taken, try again\n  %-34s", formatBinding(b).c_str(), prompt);
                std::fflush(stdout);
                continue;
            }
            std::printf("%s\n", formatBinding(b).c_str());
            out = b;
            return true;
        }
        std::printf("skipped\n");
        return false;
    }

    // The axis that moves furthest, from its rest to its furthest point.
    bool bindWhammy(const JoySnapshot &rest, AxisRange &out)
    {
        std::printf("  %-34s", "push the whammy all the way, release");
        std::fflush(stdout);
        const Uint64 deadline = SDL_GetTicks() + 10000;
        int axis = -1;
        while (axis < 0 && SDL_GetTicks() < deadline)
        {
            const JoySnapshot s = read();
            for (int i = 0; i < s.numAxes && axis < 0; ++i)
                if (std::fabs(s.axes[i] - rest.axes[i]) > 0.3f)
                    axis = i;
            SDL_Delay(5);
        }
        if (axis < 0)
        {
            std::printf("skipped\n");
            return false;
        }
        float peak = rest.axes[axis];
        const Uint64 until = SDL_GetTicks() + 3000;
        while (SDL_GetTicks() < until)
        {
            const float v = read().axes[axis];
            if (std::fabs(v - rest.axes[axis]) > std::fabs(peak - rest.axes[axis]))
                peak = v;
            if (std::fabs(peak - rest.axes[axis]) > 0.3f && std::fabs(v - rest.axes[axis]) < 0.1f)
                break; // back at rest
            SDL_Delay(5);
        }
        // A little dead travel at each end: the bar never quite rests at the
        // same place, and a full press should reach 1.
        const float span = peak - rest.axes[axis];
        out = AxisRange{axis, rest.axes[axis] + span * 0.05f, rest.axes[axis] + span * 0.9f};
        std::printf("%s\n", formatAxisRange(out).c_str());
        waitForRest(rest);
        return true;
    }

    // One line per change: a button or hat, or an axis moving 0.15 from
    // where the last line had it. Quiet otherwise, so it can be logged.
    void probe()
    {
        std::printf("ctrl-c to stop\n");
        JoySnapshot last;
        bool first = true;
        const Uint64 start = SDL_GetTicks();
        for (;;)
        {
            const JoySnapshot s = read();
            bool changed = first || s.buttons != last.buttons;
            for (int i = 0; i < s.numHats; ++i)
                changed = changed || s.hats[i] != last.hats[i];
            for (int i = 0; i < s.numAxes; ++i)
                changed = changed || std::fabs(s.axes[i] - last.axes[i]) > 0.15f;
            if (!changed)
            {
                SDL_Delay(2);
                continue;
            }
            first = false;
            last = s;
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "%7.3fs ", (SDL_GetTicks() - start) / 1000.0);
            std::string line = std::string(stamp) + "buttons:";
            for (int i = 0; i < s.numButtons; ++i)
                if ((s.buttons >> i) & 1u)
                    line += " " + std::to_string(i);
            line += "  axes:";
            char buf[16];
            for (int i = 0; i < s.numAxes; ++i)
            {
                std::snprintf(buf, sizeof(buf), " %+.2f", s.axes[i]);
                line += buf;
            }
            line += "  hats:";
            for (int i = 0; i < s.numHats; ++i)
                line += " " + std::to_string(s.hats[i]);
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
        }
    }
}

int main(int argc, char **argv)
{
    bool doProbe = false;
    bool doList = false;
    std::string path = ghpc::settingsPath();
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--probe") == 0)
            doProbe = true;
        else if (std::strcmp(argv[i], "--list") == 0)
            doList = true;
        else
            path = argv[i];
    }

    // No window: this runs in a terminal, which holds the focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_JOYSTICK))
    {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    ghpc::IniFile ini;
    ini.load(path);
    const std::string want = ini.get("input", "device");

    SDL_UpdateJoysticks();
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_JoystickID pick = 0;
    for (int i = 0; i < count; ++i)
    {
        const char *name = SDL_GetJoystickNameForID(ids[i]);
        std::printf("%s \"%s\"\n", doList ? "" : "found", name ? name : "?");
        if (pick == 0 && (want.empty() || (name && std::string(name).find(want) != std::string::npos)))
            pick = ids[i];
    }
    SDL_free(ids);
    if (doList)
        return 0;
    if (count == 0)
    {
        std::fprintf(stderr, "no joysticks. is the controller plugged in?\n");
        return 1;
    }
    if (pick == 0)
    {
        std::fprintf(stderr, "none matches device = %s in %s\n", want.c_str(), path.c_str());
        return 1;
    }
    g_joy = SDL_OpenJoystick(pick);
    if (!g_joy)
    {
        std::fprintf(stderr, "SDL_OpenJoystick: %s\n", SDL_GetError());
        return 1;
    }
    const std::string name = SDL_GetJoystickName(g_joy);
    std::printf("using \"%s\": %d buttons, %d axes, %d hats\n", name.c_str(), SDL_GetNumJoystickButtons(g_joy),
                SDL_GetNumJoystickAxes(g_joy), SDL_GetNumJoystickHats(g_joy));

    if (doProbe)
    {
        probe();
        return 0;
    }

    std::printf("\nhands off the controller...\n");
    SDL_Delay(1000);
    const JoySnapshot rest = read();
    std::printf("got its resting state. now, one at a time:\n\n");

    struct Step { int action; const char *prompt; };
    const Step steps[] = {
        {kGreen, "press green"},
        {kRed, "press red"},
        {kYellow, "press yellow"},
        {kBlue, "press blue"},
        {kOrange, "press orange"},
        {kStrumUp, "strum up"},
        {kStrumDown, "strum down"},
        {kStart, "press + (start)"},
        {kSelect, "press - (star power)"},
        {kTilt, "tilt the neck up, hold, level it"},
    };
    Binding taken[kActionCount];
    int takenCount = 0;
    Binding bound[kActionCount];
    bool have[kActionCount] = {};
    for (const Step &step : steps)
    {
        have[step.action] = bindDigital(rest, step.prompt, taken, takenCount, bound[step.action]);
        if (have[step.action])
            taken[takenCount++] = bound[step.action];
    }
    AxisRange whammy;
    const bool haveWhammy = bindWhammy(rest, whammy);

    ini.set("input", "device", name);
    for (int a = 0; a < kActionCount; ++a)
    {
        if (have[a])
            ini.set("input", actionKey(a), formatBinding(bound[a]));
        else
            ini.erase("input", actionKey(a));
    }
    if (haveWhammy)
        ini.set("input", "whammy", formatAxisRange(whammy));
    else
        ini.erase("input", "whammy");

    if (!ini.save(path))
    {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("\nwrote %s\n", path.c_str());
    SDL_CloseJoystick(g_joy);
    SDL_Quit();
    return 0;
}
