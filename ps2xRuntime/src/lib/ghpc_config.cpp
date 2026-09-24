#include "ghpc_config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace ghpc
{
    namespace
    {
        std::string trim(const std::string &s)
        {
            const size_t b = s.find_first_not_of(" \t\r");
            if (b == std::string::npos)
                return "";
            const size_t e = s.find_last_not_of(" \t\r");
            return s.substr(b, e - b + 1);
        }
    }

    bool IniFile::isBlank(const Line &line)
    {
        return line.key.empty() && trim(line.text).empty();
    }

    bool IniFile::load(const std::string &path)
    {
        m_lines.clear();
        std::ifstream in(path);
        if (!in)
            return false;

        std::string section;
        std::string raw;
        while (std::getline(in, raw))
        {
            Line line;
            line.text = raw;
            std::string body = raw;
            if (const size_t c = body.find_first_of(";#"); c != std::string::npos)
                body = body.substr(0, c);
            body = trim(body);

            if (body.size() >= 2 && body.front() == '[' && body.back() == ']')
            {
                section = trim(body.substr(1, body.size() - 2));
            }
            else if (const size_t eq = body.find('='); eq != std::string::npos)
            {
                line.key = trim(body.substr(0, eq));
                line.value = trim(body.substr(eq + 1));
            }
            line.section = section;
            m_lines.push_back(line);
        }
        return true;
    }

    bool IniFile::save(const std::string &path) const
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            return false;
        for (const Line &line : m_lines)
        {
            if (!line.edited)
                out << line.text << '\n';
            else
                out << line.key << " = " << line.value << '\n';
        }
        return bool(out);
    }

    const std::string *IniFile::find(const std::string &section, const std::string &key) const
    {
        for (const Line &line : m_lines)
        {
            if (line.section == section && line.key == key)
                return &line.value;
        }
        return nullptr;
    }

    std::string IniFile::get(const std::string &section, const std::string &key, const std::string &fallback) const
    {
        const std::string *v = find(section, key);
        return v ? *v : fallback;
    }

    int IniFile::getInt(const std::string &section, const std::string &key, int fallback) const
    {
        const std::string *v = find(section, key);
        if (!v || v->empty())
            return fallback;
        char *end = nullptr;
        const long n = std::strtol(v->c_str(), &end, 10);
        return (end && *end == '\0') ? int(n) : fallback;
    }

    bool IniFile::getBool(const std::string &section, const std::string &key, bool fallback) const
    {
        const std::string *v = find(section, key);
        if (!v || v->empty())
            return fallback;
        return *v != "0" && *v != "false" && *v != "no" && *v != "off";
    }

    void IniFile::set(const std::string &section, const std::string &key, const std::string &value)
    {
        size_t lastInSection = std::string::npos;
        bool haveSection = false;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            Line &line = m_lines[i];
            if (line.section != section)
                continue;
            haveSection = true;
            if (line.key == key)
            {
                line.value = value;
                line.edited = true;
                return;
            }
            // Past trailing blank lines, so a key lands under its section's
            // last entry rather than above the next header.
            if (!isBlank(line))
                lastInSection = i;
        }

        Line line;
        line.section = section;
        line.key = key;
        line.value = value;
        line.edited = true;
        if (!haveSection)
        {
            if (!m_lines.empty() && !isBlank(m_lines.back()))
                m_lines.push_back(Line{"", m_lines.back().section, "", "", false});
            m_lines.push_back(Line{"[" + section + "]", section, "", "", false});
            m_lines.push_back(line);
            return;
        }
        m_lines.insert(m_lines.begin() + (lastInSection + 1), line);
    }

    void IniFile::erase(const std::string &section, const std::string &key)
    {
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].section == section && m_lines[i].key == key)
            {
                m_lines.erase(m_lines.begin() + i);
                return;
            }
        }
    }

    std::string settingsPath()
    {
        const char *env = std::getenv("GHPC_CONFIG");
        return (env && *env) ? env : "ghpc.ini";
    }

    const IniFile &settings()
    {
        static const IniFile file = []() {
            IniFile f;
            const std::string path = settingsPath();
            if (f.load(path))
                std::fprintf(stderr, "[ghpc/config] loaded %s\n", path.c_str());
            return f;
        }();
        return file;
    }

    LiveSettings &live()
    {
        static LiveSettings *s = []() {
            auto *l = new LiveSettings();
            const IniFile &f = settings();
            l->widescreen = f.getBool("display", "widescreen", true);
            l->unlockAll = f.getBool("cheats", "unlock_all", false);
            l->volume = std::max(0, std::min(100, f.getInt("audio", "volume", 100)));
            l->showFps = f.getBool("display", "show_fps", false);
            const int msaa = f.getInt("display", "msaa", 1);
            l->msaa = msaa >= 16 ? 16 : msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 1;
            l->renderScale = std::max(0, std::min(4, f.getInt("display", "render_scale", 0)));
            return l;
        }();
        return *s;
    }

    void saveSetting(const std::string &section, const std::string &key, const std::string &value)
    {
        const std::string path = settingsPath();
        IniFile f;
        f.load(path);
        f.set(section, key, value);
        if (!f.save(path))
            std::fprintf(stderr, "[ghpc/config] could not write %s\n", path.c_str());
    }
}
