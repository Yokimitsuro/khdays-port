// A mod's native plugin's parameters as the tool reads, changes and saves them (core/mods/plugin_parameters.h).
#include "test.h"

#include "core/mods/plugin_parameters.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace studio;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

TEST(plugin_parameters) {
    const fs::path mod = studio_test::scratch("plugin_parameters");
    PluginParameters p;
    std::string why;
    CHECK(!load_plugin_parameters(mod, p));  // none described
    studio_test::write(mod / "mod.ini", "id = test.tuned\nparameters = native/tuning.ini  # its own name\n");
    studio_test::write(mod / "native" / "tuning.ini",
                       "[spins]\ntype = integer\ndefault = 8\nmin = 1\nmax = 30\nlabel = Spins\n"
                       "[sound]\ntype = choice\nchoices = none, soft, loud\ndefault = soft\n"
                       "[odd]\ntype = colour\n");
    CHECK(load_plugin_parameters(mod, p));
    CHECK(p.list.count == 2 && p.problems.size() == 1);
    CHECK(p.problems[0].find("tuning.ini, line 11: [odd]") == 0);
    CHECK(p.values == mod / "native" / "tuning.values.ini");
    CHECK(!set_plugin_parameter(p, "spins", "31", why) && why == "outside its bounds" && !p.changed);
    CHECK(!set_plugin_parameter(p, "sound", "quiet", why) && why == "not one of its choices");
    CHECK(set_plugin_parameter(p, "spins", "12", why) && set_plugin_parameter(p, "sound", "loud", why) && p.changed);
    CHECK(save_plugin_parameters(p, why) && !p.changed);
    CHECK(read(p.values) == "# the values chosen for the plugin's parameters (KH Studio writes them)\n"
                            "spins = 12\nsound = loud\n");
    PluginParameters again;  // read back as the port reads them
    CHECK(load_plugin_parameters(mod, again));
    CHECK(again.list.list[0].number == 12 && again.list.list[1].number == 2);
}
