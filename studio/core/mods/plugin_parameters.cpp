// A mod's native plugin's parameters (plugin_parameters.h).
#include "core/mods/plugin_parameters.h"

#include "core/files/file_write.h"
#include "core/files/paths.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

namespace fs = std::filesystem;

namespace studio {

namespace {

bool read_text(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// mod.ini's `parameters`, empty when it names none.
std::string named_in_mod_ini(const fs::path& mod_folder) {
    std::string text;
    if (!read_text(mod_folder / "mod.ini", text))
        return {};
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        auto trim = [](std::string s) {
            const auto first = s.find_first_not_of(" \t\r");
            const auto last = s.find_last_not_of(" \t\r");
            return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
        };
        if (trim(line.substr(0, equals)) == "parameters") {
            std::string value = trim(line.substr(equals + 1));
            const auto comment = value.find(" #");
            return trim(comment == std::string::npos ? value : value.substr(0, comment));
        }
    }
    return {};
}

struct Reading {
    std::vector<std::string>* problems;
    std::string file;
};

void noted(int line, const char* why, void* context) {
    auto* r = static_cast<Reading*>(context);
    r->problems->push_back(r->file + (line > 0 ? ", line " + std::to_string(line) : std::string()) + ": " + why);
}

}  // namespace

bool load_plugin_parameters(const fs::path& mod_folder, PluginParameters& out) {
    khdays_parameters_free(&out.list);
    out.problems.clear();
    out.changed = false;
    const std::string named = named_in_mod_ini(mod_folder);
    out.descriptor = mod_folder / path_u8(named.empty() ? "parameters.ini" : named);
    char values[1024];
    khdays_parameters_values_name(u8(out.descriptor.filename()).c_str(), values, sizeof(values));
    out.values = out.descriptor.parent_path() / path_u8(values);
    std::string text;
    if (!read_text(out.descriptor, text))
        return false;
    Reading r{&out.problems, u8(out.descriptor.filename())};
    khdays_parameters_read(&out.list, text.data(), text.size(), noted, &r);
    if (read_text(out.values, text)) {
        r.file = u8(out.values.filename());
        khdays_parameters_values(&out.list, text.data(), text.size(), noted, &r);
    }
    return true;
}

bool set_plugin_parameter(PluginParameters& p, const std::string& name, const std::string& value, std::string& why) {
    char reason[160];
    if (khdays_parameters_set(&p.list, name.c_str(), value.c_str(), reason, sizeof(reason)) != nullptr) {
        why = reason;
        return false;
    }
    p.changed = true;
    return true;
}

bool save_plugin_parameters(PluginParameters& p, std::string& why) {
    const std::unique_ptr<char, decltype(&std::free)> text(khdays_parameters_write_values(&p.list), &std::free);
    if (!text) {
        why = "no memory";
        return false;
    }
    const std::string content = text.get();
    const WriteResult w = write_into_mod(
        p.descriptor.parent_path(), p.values,
        [&](const fs::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary);
            out << content;
            return out ? std::string() : "cannot be written";
        },
        [&](const fs::path& temporary) -> std::string {
            // read back as the port reads it: every value taken
            std::string back;
            if (!read_text(temporary, back) || back != content)
                return "not as written";
            return {};
        });
    if (!w.ok) {
        why = w.why;
        return false;
    }
    p.changed = false;
    return true;
}

}  // namespace studio
