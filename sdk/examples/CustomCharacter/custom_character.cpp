// The KH Mod SDK's character example: behaviour for a mod's own character, given by a plugin of the same mod.
// The mod carries a made-up character (characters/block/: a figure of three boxes on Roxas's code, described by its
// character.ini alone, docs/CHARACTERS.md); this plugin gives it what a description cannot:
//  - kh.characters: it listens to its character, and to no other actor: it is told when one enters the field, each
//    attack it starts (logged, and 1 HP back for each), and when it leaves.
//  - kh.hooks + kh.actors: after the game works out the damage of a player's hit (Ov022_ComputeStrengthDamage, whose
//    first argument is the attacking actor), the hits of its character's actor deal double; everyone else's stay as
//    they were.
// With native code off the character still plays, by its description alone.
#include <kh/kh_actors.h>
#include <kh/kh_characters.h>
#include <kh/kh_hooks.h>

#include <cstdint>
#include <cstdio>

namespace {

const KhHost *host = nullptr;
const KhActors *actors = nullptr;
std::int32_t block = -1; // its character's number this run
int told_hits = 0;

void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

void entered(KhActor actor, std::int32_t seat, void *)
{
    std::int32_t hp = 0, most = 0;
    actors->hp(actor, &hp, &most);
    char text[120];
    std::snprintf(text, sizeof(text), "Block entered the field at seat %d, HP %d/%d", seat, hp, most);
    say(text);
}

void attacked(KhActor actor, std::int32_t attack, void *)
{
    std::int32_t hp = 0, most = 0;
    actors->hp(actor, &hp, &most);
    // the game's state is the host's in a room (seat 0, as alone): the other copies only tell
    const bool healed = actors->local_seat() == 0 && hp > 0 && hp < most && actors->set_hp(actor, hp + 1) == KH_OK;
    char text[120];
    std::snprintf(text, sizeof(text), "Block attacks with attack %d%s", attack, healed ? ", 1 HP back" : "");
    say(text);
}

void left(KhActor, void *)
{
    say("Block left the field");
}

// after Ov022_ComputeStrengthDamage(attacker, scale): double for its character's actor
void damage_worked_out(KhHookCall *call, void *)
{
    const KhActor attacker = actors->of(static_cast<KhRef>(call->args[0]));
    if (attacker == 0 || actors->character(attacker) != block)
        return;
    const auto damage = static_cast<std::int32_t>(call->result);
    call->result = static_cast<std::uint64_t>(static_cast<std::int64_t>(damage) * 2);
    if (told_hits < 20) {
        ++told_hits;
        char text[120];
        std::snprintf(text, sizeof(text), "Block's hit: %d damage doubled to %d", damage, damage * 2);
        say(text);
    }
}

const KhCharacterCalls calls = {sizeof(KhCharacterCalls), entered, nullptr, attacked, left};

KhResult start(const KhHost *given)
{
    host = given;
    actors = static_cast<const KhActors *>(host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION));
    const auto *characters =
        static_cast<const KhCharacters *>(host->find(host->plugin, KH_CHARACTERS, KH_CHARACTERS_VERSION));
    const auto *hooks = static_cast<const KhHooks *>(host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION));
    if (actors == nullptr || characters == nullptr || hooks == nullptr)
        return KH_UNSUPPORTED;

    // the character of this plugin's own mod, by its folder's name
    block = characters->find(host->plugin, "block");
    if (block < 0) {
        say("no character block in this mod (its files missing?): nothing to do");
        return KH_ERROR;
    }
    char text[120];
    std::snprintf(text, sizeof(text), "Block is character %d this run", block);
    say(text);
    if (characters->listen(host->plugin, block, &calls, nullptr) == 0 ||
        hooks->add(host->plugin, "Ov022_ComputeStrengthDamage", KH_HOOK_AFTER, 0, 0, damage_worked_out, nullptr) == 0)
        return KH_ERROR;
    return KH_OK;
}

void stop() {}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Custom character", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
