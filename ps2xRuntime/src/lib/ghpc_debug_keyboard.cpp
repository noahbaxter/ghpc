#include "ghpc_debug_keyboard.h"

#include "Kernel/Stubs/ghpc_drive.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ghpc
{
    namespace debug_keyboard
    {
        // GH2 debug build. Keyboard_EE.cpp's read record and the function
        // that turns it into a KeyboardKeyMsg (gh2-decomp src/os/Keyboard_EE.cpp).
        constexpr uint32_t kGKBData = 0x0051ED00u; // +0x04 modifiers, +0x08 count, +0x0c first code
        constexpr uint32_t kProcessRead = 0x002F4718u;

        std::mutex g_mutex;
        std::deque<uint16_t> g_keys;
        std::atomic<bool> g_autoplay{false};

        struct Rule
        {
            std::string screen;
            std::string keys;
            bool fired = false;
        };

        std::vector<Rule> parseRules()
        {
            std::vector<Rule> rules;
            const char *e = std::getenv("GHPC_DEBUG_KEYS");
            const std::string spec = e ? e : "";
            size_t at = 0;
            while (at < spec.size())
            {
                const size_t comma = spec.find(',', at);
                const std::string item = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                const size_t eq = item.find('=');
                if (eq != std::string::npos && eq + 1 < item.size())
                {
                    rules.push_back({item.substr(0, eq), item.substr(eq + 1)});
                }
                if (comma == std::string::npos)
                {
                    break;
                }
                at = comma + 1;
            }
            return rules;
        }

        void pushScripted()
        {
            static std::vector<Rule> rules = parseRules();
            if (rules.empty())
            {
                return;
            }
            const std::string screen = ghpc_drive::taggedScreen();
            for (Rule &r : rules)
            {
                if (r.fired || r.screen != screen)
                {
                    continue;
                }
                r.fired = true;
                for (char c : r.keys)
                {
                    debugKeyPush(static_cast<uint8_t>(c));
                }
            }
        }
    }

    using namespace debug_keyboard;

    bool autoplay()
    {
        return g_autoplay.load(std::memory_order_relaxed);
    }

    void setAutoplay(bool on)
    {
        if (g_autoplay.exchange(on) != on)
        {
            debugKeyPush('p');
        }
    }

    void debugKeyPush(uint16_t code)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_keys.push_back(code);
    }

    bool debugKeyServe(uint8_t *rdram, uint32_t &targetPc, uint32_t &a0)
    {
        pushScripted();
        uint16_t code = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_keys.empty())
            {
                return false;
            }
            code = g_keys.front();
            g_keys.pop_front();
        }
        const uint32_t modifiers = 0u;
        const uint32_t count = 1u;
        std::memcpy(rdram + kGKBData + 0x04u, &modifiers, 4);
        std::memcpy(rdram + kGKBData + 0x08u, &count, 4);
        std::memcpy(rdram + kGKBData + 0x0Cu, &code, 2);
        std::fprintf(stderr, "[ghpc/keyboard] key 0x%x on %s\n", (unsigned)code,
                     ghpc_drive::taggedScreen().c_str());
        targetPc = kProcessRead;
        a0 = 1u;
        return true;
    }
}
