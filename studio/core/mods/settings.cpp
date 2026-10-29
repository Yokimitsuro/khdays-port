#include "core/mods/settings.h"

#include "core/files/paths.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <fstream>
#include <sstream>
#include <vector>

namespace studio {

namespace {

#ifdef _WIN32
constexpr const char* kExe = ".exe";
constexpr char kPathSeparator = ';';
#else
constexpr const char* kExe = "";
constexpr char kPathSeparator = ':';
#endif

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// The port's executable: beside the tool in an install (the port's folder), else the one the build left most
// recently (port/build.py's folders: Release, then Debug).
std::string newest_port(const fs::path& root) {
    std::string best;
    std::error_code beside;
    if (fs::is_regular_file(root / (std::string("khdays-native") + kExe), beside))
        return std::string("khdays-native") + kExe;
    fs::file_time_type best_time{};
    for (const char* dir : {"build/port/obj-release-x64", "build/port/obj-x64"}) {
        const fs::path exe = root / dir / (std::string("khdays-native") + kExe);
        std::error_code ec;
        if (!fs::is_regular_file(exe, ec))
            continue;
        const auto t = fs::last_write_time(exe, ec);
        if (best.empty() || t > best_time) {
            best = std::string(dir) + "/khdays-native" + kExe;
            best_time = t;
        }
    }
    return best;
}

// ffmpeg on the PATH, if it is there.
std::string ffmpeg_on_path() {
    const char* path = SDL_getenv("PATH");
    if (path == nullptr)
        return {};
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, kPathSeparator)) {
        if (dir.empty())
            continue;
        const fs::path exe = path_u8(dir) / (std::string("ffmpeg") + kExe);
        std::error_code ec;
        if (fs::is_regular_file(exe, ec))
            return u8(exe);
    }
    return {};
}

}  // namespace

fs::path settings_file(const fs::path& root) {
    return root / "saves" / "studio.ini";
}

bool is_port_folder(const fs::path& folder) {
    std::error_code ec;
    return fs::is_directory(folder / "data", ec) ||
           fs::is_regular_file(folder / (std::string("khdays-native") + kExe), ec);
}

fs::path find_port_root(const fs::path& current, const fs::path& tool) {
    for (const fs::path& start : {current, tool}) {
        if (start.empty())
            continue;
        for (fs::path folder = start;; folder = folder.parent_path()) {
            if (is_port_folder(folder))
                return folder;
            if (folder == folder.parent_path() || folder.parent_path().empty())
                break;
        }
    }
    return current;
}

std::string clean_path(const std::string& typed) {
    std::string s = trim(typed);
    while (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        s = trim(s.substr(1, s.size() - 2));
    return s;
}

fs::path port_exe(const Settings& settings) {
    const fs::path named = resolve(settings, settings.port);
    std::error_code ec;
    if (fs::is_directory(named, ec))
        return named / (std::string("khdays-native") + kExe);
    return named;
}

Settings load_settings(const fs::path& root, std::string* note) {
    Settings s;
    s.root = root;
    const fs::path file = settings_file(root);
    std::ifstream in(file, std::ios::binary);
    if (in) {
        Settings read = s;
        bool good = true;
        std::string line;
        while (good && std::getline(in, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#')
                continue;
            const auto eq = t.find('=');
            if (eq == std::string::npos) {
                good = false;
                break;
            }
            const std::string key = trim(t.substr(0, eq));
            const std::string value = trim(t.substr(eq + 1));
            if (key == "port")
                read.port = clean_path(value);
            else if (key == "graphics_dump")
                read.graphics_dump = clean_path(value);
            else if (key == "sounds_dump")
                read.sounds_dump = clean_path(value);
            else if (key == "ffmpeg")
                read.ffmpeg = clean_path(value);
            else if (key == "language")
                read.language = value;
            else if (key == "mod")
                read.mod = value;
            else if (key == "browser")
                read.browser_tree = value != "list";
            else if (key == "theme")
                read.theme = value;
            else if (key == "collection_1525")
                read.collection_1525 = clean_path(value);
            else if (key == "cache_mb") {
                int mb = 0;
                const auto r = std::from_chars(value.data(), value.data() + value.size(), mb);
                if (r.ec != std::errc() || r.ptr != value.data() + value.size() || mb < 1)
                    good = false;
                else
                    read.cache_mb = mb;
            }
            // other keys: a later version's, left alone
        }
        in.close();
        if (good) {
            s = read;
        } else {
            std::error_code ec;
            fs::path bad = file;
            bad += ".bad";
            fs::rename(file, bad, ec);
            if (note != nullptr)
                *note = u8(file);
        }
    }
    std::error_code ec;
    if (s.port.empty() || !fs::is_regular_file(port_exe(s), ec))
        s.port = newest_port(root);  // none named, or the one named is gone: the one there is
    if (s.ffmpeg.empty())
        s.ffmpeg = ffmpeg_on_path();
    return s;
}

bool save_settings(const Settings& s, std::string* why) {
    const fs::path file = settings_file(s.root);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (why != nullptr)
                *why = "cannot write " + u8(tmp);
            return false;
        }
        out << "# khdays-studio's settings; paths are relative to the port's folder\n"
            << "port = " << s.port << "\n"
            << "graphics_dump = " << s.graphics_dump << "\n"
            << "sounds_dump = " << s.sounds_dump << "\n"
            << "ffmpeg = " << s.ffmpeg << "\n"
            << "language = " << s.language << "\n"
            << "mod = " << s.mod << "\n"
            << "cache_mb = " << s.cache_mb << "\n"
            << "browser = " << (s.browser_tree ? "tree" : "list") << "\n"
            << "theme = " << s.theme << "\n"
            << "collection_1525 = " << s.collection_1525 << "\n";
        if (!out) {
            if (why != nullptr)
                *why = "cannot write " + u8(tmp);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        if (why != nullptr)
            *why = "cannot replace " + u8(file) + ": " + ec.message();
        return false;
    }
    return true;
}

fs::path resolve(const Settings& settings, const std::string& path) {
    const fs::path p = path_u8(clean_path(path));
    return p.is_absolute() ? p : settings.root / p;
}

}  // namespace studio
