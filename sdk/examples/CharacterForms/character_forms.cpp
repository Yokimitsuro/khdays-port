// The KH Mod SDK's forms example: a condition of the plugin's own for a mod's character's forms.
// The mod carries a made-up character (characters/twin/, docs/CHARACTERS.md "Forms") with two forms: `dual`, chosen by
// its description alone (the Zero Gear with three Ability Units), and `guard`, whose rule names this plugin's
// condition (`when = plugin example.character-forms.guard`):
//  - kh.characters' add_condition: "guard" holds while the mod's parameter `guard` is on (KH Studio's Plugin window).
//  - kh.characters' listener: each form a seat takes is logged.
// With native code off the condition is not there and the guard form is never chosen: the rest plays as described.
#include <kh/kh_characters.h>
#include <kh/kh_parameters.h>

#include <cstdint>
#include <cstdio>

namespace {

const KhHost *host = nullptr;
bool guard = false;

void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

// asked as the camp chooses a seat's form: the seat and its character's number
std::int32_t guard_holds(std::int32_t, std::int32_t, void *)
{
    return guard ? 1 : 0;
}

void form_taken(std::int32_t seat, const char *form, void *)
{
    char text[120];
    std::snprintf(text, sizeof(text), "Twin at seat %d takes its form %s", seat, form);
    say(text);
}

const KhCharacterCalls calls = {sizeof(KhCharacterCalls), nullptr, nullptr, nullptr, nullptr, form_taken};

KhResult start(const KhHost *given)
{
    host = given;
    const auto *characters =
        static_cast<const KhCharacters *>(host->find(host->plugin, KH_CHARACTERS, KH_CHARACTERS_VERSION));
    const auto *parameters =
        static_cast<const KhParameters *>(host->find(host->plugin, KH_PARAMETERS, KH_PARAMETERS_VERSION));
    if (characters == nullptr || parameters == nullptr)
        return KH_UNSUPPORTED;
    guard = parameters->integer(host->plugin, "guard", 0) != 0;
    const std::int32_t twin = characters->find(host->plugin, "twin");
    if (twin < 0) {
        say("no character twin in this mod (its files missing?): nothing to do");
        return KH_ERROR;
    }
    if (characters->add_condition(host->plugin, "guard", guard_holds, nullptr) != KH_OK ||
        characters->listen(host->plugin, twin, &calls, nullptr) == 0)
        return KH_ERROR;
    say(guard ? "the guard form's condition holds" : "the guard form's condition does not hold");
    return KH_OK;
}

void stop() {}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Character forms", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
