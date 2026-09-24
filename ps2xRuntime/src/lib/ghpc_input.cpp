#include "ghpc_input.h"
#include "ghpc_config.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace ghpc::input
{
    namespace
    {
        const char *const kActionKeys[kActionCount] = {
            "green", "red", "yellow", "blue", "orange",
            "strum_up", "strum_down", "start", "select", "tilt",
        };

        struct HatName { const char *name; uint8_t mask; };
        const HatName kHatNames[] = {
            {"up", kHatUp}, {"right", kHatRight}, {"down", kHatDown}, {"left", kHatLeft},
        };
    }

    const char *actionKey(int action)
    {
        return (action >= 0 && action < kActionCount) ? kActionKeys[action] : "";
    }

    bool parseBinding(const std::string &text, Binding &out)
    {
        std::istringstream in(text);
        std::string kind;
        in >> kind;
        Binding b;
        if (kind == "button")
        {
            b.kind = Binding::Button;
            if (!(in >> b.index) || b.index < 0 || b.index >= 64)
                return false;
        }
        else if (kind == "hat")
        {
            std::string dir;
            if (!(in >> b.index >> dir) || b.index < 0 || b.index >= kMaxHats)
                return false;
            for (const HatName &h : kHatNames)
                if (dir == h.name)
                    b.hatMask = h.mask;
            if (b.hatMask == 0)
                return false;
            b.kind = Binding::Hat;
        }
        else if (kind == "axis")
        {
            std::string op;
            if (!(in >> b.index >> op >> b.threshold) || b.index < 0 || b.index >= kMaxAxes)
                return false;
            if (op == ">")
                b.kind = Binding::AxisAbove;
            else if (op == "<")
                b.kind = Binding::AxisBelow;
            else
                return false;
        }
        else
        {
            return false;
        }
        out = b;
        return true;
    }

    std::string formatBinding(const Binding &b)
    {
        char buf[64];
        switch (b.kind)
        {
        case Binding::Button:
            std::snprintf(buf, sizeof(buf), "button %d", b.index);
            return buf;
        case Binding::Hat:
            for (const HatName &h : kHatNames)
                if (b.hatMask == h.mask)
                {
                    std::snprintf(buf, sizeof(buf), "hat %d %s", b.index, h.name);
                    return buf;
                }
            return "";
        case Binding::AxisAbove:
        case Binding::AxisBelow:
            std::snprintf(buf, sizeof(buf), "axis %d %c %.2f", b.index,
                          b.kind == Binding::AxisAbove ? '>' : '<', b.threshold);
            return buf;
        default:
            return "";
        }
    }

    bool parseAxisRange(const std::string &text, AxisRange &out)
    {
        std::istringstream in(text);
        std::string kind;
        AxisRange r;
        if (!(in >> kind >> r.index >> r.rest >> r.full) || kind != "axis")
            return false;
        if (r.index < 0 || r.index >= kMaxAxes || r.rest == r.full)
            return false;
        out = r;
        return true;
    }

    std::string formatAxisRange(const AxisRange &r)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "axis %d %.2f %.2f", r.index, r.rest, r.full);
        return buf;
    }

    bool held(const Binding &b, const JoySnapshot &s)
    {
        switch (b.kind)
        {
        case Binding::Button:
            return b.index < s.numButtons && ((s.buttons >> b.index) & 1u) != 0;
        case Binding::Hat:
            return b.index < s.numHats && (s.hats[b.index] & b.hatMask) != 0;
        case Binding::AxisAbove:
            return b.index < s.numAxes && s.axes[b.index] > b.threshold;
        case Binding::AxisBelow:
            return b.index < s.numAxes && s.axes[b.index] < b.threshold;
        default:
            return false;
        }
    }

    float amount(const AxisRange &r, const JoySnapshot &s)
    {
        if (r.index < 0 || r.index >= s.numAxes || r.rest == r.full)
            return 0.0f;
        return std::clamp((s.axes[r.index] - r.rest) / (r.full - r.rest), 0.0f, 1.0f);
    }

    Bindings loadBindings(const IniFile &ini)
    {
        Bindings out;
        for (int a = 0; a < kActionCount; ++a)
        {
            const std::string *text = ini.find("input", kActionKeys[a]);
            if (!text || text->empty())
                continue;
            if (parseBinding(*text, out.digital[a]))
                out.any = true;
            else
                std::fprintf(stderr, "[ghpc/input] %s = %s: not a binding, ignored\n",
                             kActionKeys[a], text->c_str());
        }
        if (const std::string *text = ini.find("input", "whammy"); text && !text->empty())
        {
            if (parseAxisRange(*text, out.whammy))
                out.any = true;
            else
                std::fprintf(stderr, "[ghpc/input] whammy = %s: not an axis range, ignored\n", text->c_str());
        }
        return out;
    }
}
