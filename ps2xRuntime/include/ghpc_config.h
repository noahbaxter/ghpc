#pragma once

#include <atomic>
#include <string>
#include <vector>

// ghpc.ini: the player's settings. Read from the working directory, which is
// the ELF's directory under play.sh (work/), or from $GHPC_CONFIG.
//
//     [display]
//     resolution = 1280x960
//     fullscreen = 0
//
//     [input]
//     device = raphnet      ; substring of the joystick name, else the first
//     green = button 0      ; written by ghpcbind, see ghpc_input.h
//
// Env vars stay the developer's knobs. This file is for what a player sets
// once and keeps. Keys are case-sensitive, values are trimmed, ';' and '#'
// start a comment.
namespace ghpc
{
    class IniFile
    {
    public:
        bool load(const std::string &path);
        bool save(const std::string &path) const;

        // Null when the key is absent.
        const std::string *find(const std::string &section, const std::string &key) const;
        std::string get(const std::string &section, const std::string &key, const std::string &fallback = "") const;
        int getInt(const std::string &section, const std::string &key, int fallback) const;
        bool getBool(const std::string &section, const std::string &key, bool fallback) const;

        // Replaces the key in place, keeping the line's position and the
        // rest of the file, or appends it to its section.
        void set(const std::string &section, const std::string &key, const std::string &value);
        void erase(const std::string &section, const std::string &key);

    private:
        struct Line
        {
            std::string text;    // verbatim, for everything but a key's value
            std::string section; // the section the line sits in
            std::string key;     // empty unless this is a key line
            std::string value;
            bool edited = false; // written as `key = value`, else as text
        };
        static bool isBlank(const Line &line);
        std::vector<Line> m_lines;
    };

    // $GHPC_CONFIG, else ghpc.ini in the working directory.
    std::string settingsPath();

    // Loaded once on first use. An absent file is an empty one.
    const IniFile &settings();

    // The settings the overlay changes while the game runs. Seeded from
    // ghpc.ini on first use; after that these are the truth, and whoever
    // applies one reads it here rather than from settings().
    struct LiveSettings
    {
        std::atomic<bool> widescreen{true}; // [display] widescreen
        std::atomic<bool> unlockAll{false}; // [cheats] unlock_all
        std::atomic<int> volume{100};       // [audio] volume, percent
        std::atomic<bool> showFps{false};   // [display] show_fps
        std::atomic<int> msaa{1};           // [display] msaa: 1, 2, 4, 8 or 16, past the GPU's samples by supersampling
        std::atomic<int> renderScale{0};    // [display] render_scale: 0 the window, else the guest's 448 lines times it
        std::atomic<int> msaaMax{8};        // what the GPU allows, set by the renderer
    };
    LiveSettings &live();

    // Writes one key back to the file, keeping everything else in it.
    void saveSetting(const std::string &section, const std::string &key, const std::string &value);
}
