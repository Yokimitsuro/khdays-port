// A mod's native plugin's parameters, as its mod describes them and the values a person chose (the port's
// runtime/plugins/parameters.h reads both, here as in the game; docs/MODDING_NATIVE.md, "Its parameters"). The
// plugin itself is never loaded: what the controls show is the description's.
#pragma once

#include "plugins/parameters.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct PluginParameters {
    std::filesystem::path descriptor;  // mods/<Mod>/parameters.ini, or mod.ini's `parameters`
    std::filesystem::path values;      // beside it: <its name>.values.ini
    KhdaysParameters list{};
    std::vector<std::string> problems;  // what reading them found wrong, with its file and line
    bool changed = false;               // values set since the last save

    PluginParameters() = default;
    PluginParameters(const PluginParameters&) = delete;
    PluginParameters& operator=(const PluginParameters&) = delete;
    ~PluginParameters() {
        khdays_parameters_free(&list);
    }
};

// Reads the parameters of the mod in `mod_folder` into `out` (emptied first): false when it describes none.
bool load_plugin_parameters(const std::filesystem::path& mod_folder, PluginParameters& out);

// Parameter `name` given `value` as a person writes it; false with why not.
bool set_plugin_parameter(PluginParameters& p, const std::string& name, const std::string& value, std::string& why);

// The values chosen written beside the description; false with why not.
bool save_plugin_parameters(PluginParameters& p, std::string& why);

}  // namespace studio
