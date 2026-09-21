// Screen driven pad driver.
//
// Mashing a button on a timer cannot answer "how far does the build get",
// because a press that lands mid transition is swallowed and a press that
// lands twice on one screen overshoots. This reads the live UI state out of
// guest RAM instead and presses once per settled screen, so a run walks the
// menus deliberately and reports the screen it could not leave.
//
// Guest addresses, all derived in ghpc/notes/ui-draw-hang.md:
//   TheUI              0x4f9be8   the UIManager instance, not a pointer
//   TheUI+0x28         transition state, 0 when settled
//   TheUI+0x40         current UIScreen*      (UIManager::GotoScreen compares
//   TheUI+0x44         transition target       its argument against 0x40)
//   UIScreen+0x14      name Symbol, a char*   (UIScreen::Print streams it
//                                              straight after "{UIScreen ")
#ifndef GHPC_DRIVE_H
#define GHPC_DRIVE_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghpc_drive
{
    constexpr uint32_t kTheUI = 0x4f9be8u;
    constexpr uint32_t kOffTransition = 0x28u;
    constexpr uint32_t kOffCurrent = 0x40u;
    constexpr uint32_t kOffScreenName = 0x14u;

    // Active low, matching the PS2 pad word.
    constexpr uint16_t kSelect = 1u << 0;
    constexpr uint16_t kStart = 1u << 3;
    constexpr uint16_t kUp = 1u << 4;
    constexpr uint16_t kRight = 1u << 5;
    constexpr uint16_t kDown = 1u << 6;
    constexpr uint16_t kLeft = 1u << 7;
    constexpr uint16_t kL2 = 1u << 8;
    constexpr uint16_t kR2 = 1u << 9;
    constexpr uint16_t kL1 = 1u << 10;
    constexpr uint16_t kR1 = 1u << 11;
    constexpr uint16_t kTriangle = 1u << 12;
    constexpr uint16_t kCircle = 1u << 13;
    constexpr uint16_t kCross = 1u << 14;
    constexpr uint16_t kSquare = 1u << 15;
    // Sentinel, never a button mask. parseRules keeps a rule only when the
    // name resolves to something nonzero, so "press nothing" cannot be spelled
    // as 0. ruleFor turns this back into 0.
    constexpr uint16_t kNoPress = 0xFFFFu;

    inline uint16_t buttonByName(const std::string &n)
    {
        // Fret names come from config/gen/beatmatch_controller.dtb, whose
        // "slots" list the five frets in order green, red, yellow, blue, orange:
        //   guitar        R2      Circle  Tri     X       Square   real PS2 guitar
        //   guitar_xbox   X       Tri     Square  Circle  L1       the 360 pad
        //   joypad        L2      L1      R1      R2      X        pad as guitar
        // So green is R2 on a PS2 guitar, NOT cross. Cross is blue. The common
        // "green is X" is the Xbox row. Fret names below follow the PS2 guitar.
        if (n == "green") return kR2;
        if (n == "red") return kCircle;
        if (n == "yellow") return kTriangle;
        if (n == "blue") return kCross;
        if (n == "orange") return kSquare;
        // "none" is how a rule says press nothing here. It has to be a nonzero
        // sentinel because parseRules drops a zero as an unknown name, and
        // ruleFor maps it back to zero. Without it there is no way to drive the
        // menus and then stop: an unlisted screen falls through to the
        // fallback, so a run that reaches gameplay keeps strumming through the
        // song. "*=none" makes the fallback silent and the rules explicit.
        if (n == "none" || n == "nothing") return kNoPress;
        if (n == "cross" || n == "x") return kCross;
        if (n == "circle" || n == "o") return kCircle;
        if (n == "triangle") return kTriangle;
        if (n == "square") return kSquare;
        if (n == "l2") return kL2;
        if (n == "start") return kStart;
        if (n == "select") return kSelect;
        if (n == "up" || n == "strumup") return kUp;
        if (n == "down" || n == "strumdown") return kDown;
        if (n == "left") return kLeft;
        if (n == "right") return kRight;
        if (n == "l1") return kL1;
        if (n == "r1") return kR1;
        if (n == "r2") return kR2;
        return 0u;
    }

    inline uint32_t guestWord(const uint8_t *rdram, uint32_t addr)
    {
        if (!rdram || addr >= 0x02000000u) return 0u;
        uint32_t v = 0u;
        std::memcpy(&v, rdram + (addr & 0x01FFFFFFu), sizeof(v));
        return v;
    }

    inline std::string guestString(const uint8_t *rdram, uint32_t addr)
    {
        std::string out;
        if (!rdram || addr < 0x1000u || addr >= 0x02000000u) return out;
        for (uint32_t i = 0; i < 48u; ++i)
        {
            const char c = static_cast<char>(rdram[(addr + i) & 0x01FFFFFFu]);
            if (c == '\0') break;
            if (c < 0x20 || c >= 0x7F) return std::string();
            out.push_back(c);
        }
        return out;
    }

    // The screen the game is on, empty until the UI is up. settled is false
    // while a transition is running, which is when a press would be swallowed.
    inline std::string currentScreen(const uint8_t *rdram, bool *settled)
    {
        const uint32_t screen = guestWord(rdram, kTheUI + kOffCurrent);
        if (settled) *settled = guestWord(rdram, kTheUI + kOffTransition) == 0u;
        if (screen == 0u) return std::string();
        return guestString(rdram, guestWord(rdram, screen + kOffScreenName));
    }

    // Where the speed reporters get the screen to tag their lines with.
    //
    // A frame rate is meaningless without the screen it was measured on, and
    // progress.py has already been burned by exactly that: on 2026-09-10 it
    // handed back the loading screen's 20.9% as the gameplay figure, because
    // "last value seen" cannot tell the two apart. Tagging the line at the
    // source is what makes the filter exact instead of a guess about timing.
    //
    // Written once per vblank off the scheduler thread, read by whichever
    // thread is printing. Contention is two accesses per frame, so a plain
    // mutex is cheaper than being clever.
    inline std::mutex &screenMutex()
    {
        static std::mutex m;
        return m;
    }

    inline std::string &screenSlot()
    {
        static std::string s;
        return s;
    }

    inline void setCurrentScreen(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(screenMutex());
        screenSlot() = name;
    }

    // TaskMgr's own clocks, which are the only chart position anything host
    // side can pair on.
    //
    // TheTaskMgr is 0x5a5870 with mClocks a pointer at +0x28. The array has
    // stride 0x14 with mCurrent at +0x0c, both pinned by noteGameCall's
    // UISeconds address: UISeconds is index 2 and sits at +0x34, and
    // 2*0x14+0x0c is 0x34. Index 0 is Seconds, 1 is Beat.
    //
    // Why this is published rather than read where it is needed: a host frame
    // counter is not a chart position. About 40 presents pass per guest frame
    // on game_screen, so anything sampled on the present loop drifts against
    // the chart, and two runs at the same present frame sit at different
    // beats. Screenshots and any cross-run comparison pair on `beat`.
    struct GameClocks
    {
        float game = 0.0f;
        float ui = 0.0f;
        float beat = 0.0f;
        uint32_t clocksPtr = 0u;
        bool valid = false;
    };

    inline GameClocks readGameClocks(const uint8_t *rdram)
    {
        constexpr uint32_t kTheTaskMgr = 0x005A5870u;
        GameClocks out;
        const uint32_t clocks = guestWord(rdram, kTheTaskMgr + 0x28u);
        // Before the UI is up mClocks is null and the read would be a number
        // rather than a time.
        if (clocks < 0x00100000u || clocks >= 0x02000000u)
        {
            return out;
        }
        auto at = [&](uint32_t index) {
            const uint32_t raw = guestWord(rdram, clocks + index * 0x14u + 0x0Cu);
            float v = 0.0f;
            std::memcpy(&v, &raw, sizeof(v));
            return v;
        };
        out.game = at(0u);
        out.beat = at(1u);
        out.ui = at(2u);
        out.clocksPtr = clocks;
        out.valid = true;
        return out;
    }

    inline std::mutex &clockMutex()
    {
        static std::mutex m;
        return m;
    }

    inline GameClocks &clockSlot()
    {
        static GameClocks c;
        return c;
    }

    inline void setGameClocks(const GameClocks &c)
    {
        std::lock_guard<std::mutex> lock(clockMutex());
        clockSlot() = c;
    }

    inline GameClocks gameClocks()
    {
        std::lock_guard<std::mutex> lock(clockMutex());
        return clockSlot();
    }

    // "none" rather than empty, so a tag is always a word and a log line never
    // ends in a bare "screen=".
    inline std::string taggedScreen()
    {
        std::lock_guard<std::mutex> lock(screenMutex());
        return screenSlot().empty() ? std::string("none") : screenSlot();
    }

    class Driver
    {
    public:
        // GHPC_PAD_DRIVE is a rule list, "screen=button" separated by commas,
        // with "*" as the fallback. Empty or unset means the driver is off.
        //   GHPC_PAD_DRIVE="*=cross,cut_scene_screen=start"
        static Driver &instance()
        {
            static Driver d;
            return d;
        }

        bool enabled() const { return m_enabled; }

        // Returns the buttons to hold this read, 0 for none.
        uint16_t poll(const uint8_t *rdram)
        {
            if (!m_enabled) return 0u;
            const double now = seconds();
            if (now < m_bootDelay) return 0u;

            bool settled = false;
            const std::string screen = currentScreen(rdram, &settled);
            if (screen.empty()) return 0u;

            if (screen != m_screen)
            {
                if (!m_screen.empty())
                    std::fprintf(stderr,
                                 "[drive] %s -> %s after %d press%s (t=%.1f)\n",
                                 m_screen.c_str(), screen.c_str(), m_presses,
                                 m_presses == 1 ? "" : "es", now);
                else
                    std::fprintf(stderr, "[drive] enter %s (t=%.1f)\n", screen.c_str(), now);
                m_screen = screen;
                m_presses = 0;
                m_enteredAt = now;
                m_warned = false;
                return 0u;
            }

            // A press during a transition is swallowed, so wait it out. This is
            // the whole point of driving off state rather than a timer.
            if (!settled) return 0u;

            const double since = now - m_enteredAt;
            if (since < m_settleDelay) return 0u;

            const double span = since - m_settleDelay;
            const int cycle = static_cast<int>(span / m_period);
            const double phase = span - cycle * m_period;

            if (cycle != m_lastCycle)
            {
                m_lastCycle = cycle;
                ++m_presses;
            }
            // A load screen ignoring input is correct, not stuck, so do not cry
            // wolf on one. bootup_load and loading_screen both sit for tens of
            // seconds by design.
            if (!m_warned && m_presses >= m_stuckAfter && !isLoadScreen(m_screen))
            {
                m_warned = true;
                std::fprintf(stderr, "[drive] STUCK on %s, %d presses had no effect (t=%.1f)\n",
                             m_screen.c_str(), m_presses, now);
            }
            return (phase < m_hold) ? ruleFor(m_screen) : 0u;
        }

    private:
        Driver()
        {
            const char *env = std::getenv("GHPC_PAD_DRIVE");
            if (!env || !*env) return;
            m_enabled = true;
            parseRules(env);
            if (const char *d = std::getenv("GHPC_PAD_DRIVE_DELAY")) m_bootDelay = std::atof(d);
            // Seconds to sit on each screen before the first press. Raise it
            // to let GHPC_STATE_SAVE_SCREENS catch every screen on the way.
            if (const char *s = std::getenv("GHPC_PAD_DRIVE_SETTLE")) m_settleDelay = std::atof(s);
            std::fprintf(stderr, "[drive] enabled, %zu rule(s), fallback=0x%04x\n",
                         m_rules.size(), m_fallback);
        }

        void parseRules(const char *spec)
        {
            std::string s(spec), tok;
            for (size_t i = 0; i <= s.size(); ++i)
            {
                if (i < s.size() && s[i] != ',' && s[i] != ';') { tok.push_back(s[i]); continue; }
                const size_t eq = tok.find('=');
                if (eq != std::string::npos)
                {
                    std::string key = trim(tok.substr(0, eq));
                    // "down+down+cross" is a sequence: one press each, then
                    // the last repeats. It is how a menu item other than the
                    // highlighted one gets picked.
                    std::vector<uint16_t> seq;
                    const std::string val = tok.substr(eq + 1);
                    for (size_t a = 0, b; a <= val.size(); a = b + 1)
                    {
                        b = val.find('+', a);
                        if (b == std::string::npos) b = val.size();
                        const uint16_t btn = buttonByName(trim(val.substr(a, b - a)));
                        if (!btn) { seq.clear(); break; }
                        seq.push_back(btn);
                    }
                    if (!seq.empty())
                    {
                        if (key == "*") m_fallback = seq.back();
                        else m_rules[key] = seq;
                    }
                }
                else if (!trim(tok).empty())
                {
                    // A bare button name is the fallback, so GHPC_PAD_DRIVE=cross works.
                    if (const uint16_t btn = buttonByName(trim(tok))) m_fallback = btn;
                }
                tok.clear();
            }
        }

        static std::string trim(std::string v)
        {
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            return v;
        }

        static bool isLoadScreen(const std::string &s)
        {
            return s.find("load") != std::string::npos;
        }

        uint16_t ruleFor(const std::string &screen) const
        {
            const auto it = m_rules.find(screen);
            uint16_t btn = m_fallback;
            if (it != m_rules.end())
            {
                const size_t i = m_presses > 0 ? static_cast<size_t>(m_presses - 1) : 0u;
                btn = it->second[std::min(i, it->second.size() - 1u)];
            }
            return btn == kNoPress ? 0u : btn;
        }

        static double seconds()
        {
            static const auto t0 = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }

        bool m_enabled = false;
        std::unordered_map<std::string, std::vector<uint16_t>> m_rules;
        uint16_t m_fallback = kCross;
        std::string m_screen;
        double m_enteredAt = 0.0;
        double m_bootDelay = 6.0;   // let boot settle before touching anything
        double m_settleDelay = 0.6; // let a new screen finish appearing
        double m_period = 0.7;      // one press attempt per this long
        double m_hold = 0.12;       // press edge, then release
        int m_lastCycle = -1;
        int m_presses = 0;
        int m_stuckAfter = 12;
        bool m_warned = false;
    };

} // namespace ghpc_drive

#endif // GHPC_DRIVE_H
