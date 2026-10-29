// The KH Mod SDK's advanced example: a plugin that reaches the engine itself.
//  - kh.hooks: before every HP change of a player (Ov022_ActorSetHp) it reads the actor's HP from the game's memory
//    and logs the change; the damage is halved (the routine's argument changed). After every hit an enemy takes
//    (Ov107_AiState_ApplyHit) it logs the enemy's HP, through kh.actors. After every sound effect (PlaySound: the
//    menus') and every positioned sound (Slot_Spawn: the field's) it counts them.
//  - kh.events: each scene change is logged, with the sounds counted in the scene before.
//  - kh.engine: at start it calls a game routine by its decompilation name (GetLanguage) and reads a game datum by
//    name (gSceneCtl: the scene controller, its current scene at +8).
// What it relies on (Ov022_ActorSetHp's entity keeping its HP at +0x12, gSceneCtl's layout) is the game's own, as the
// decompilation names it: a later port may move it (kh_engine.h).
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_events.h>
#include <kh/kh_hooks.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

const KhHost *host = nullptr;
const KhEngine *engine = nullptr;
const KhActors *actors = nullptr;
unsigned sounds = 0, field_sounds = 0, enemy_hits = 0;

void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

// Ov022_ActorSetHp(entity, value): the entity's HP is the u16 at +0x12, its most at +0x16.
void hp_change(KhHookCall *call, void *)
{
    const auto *entity = static_cast<const unsigned char *>(engine->pointer(static_cast<KhRef>(call->args[0])));
    if (entity == nullptr)
        return;
    std::uint16_t hp = 0, most = 0;
    std::memcpy(&hp, entity + 0x12, sizeof(hp));
    std::memcpy(&most, entity + 0x16, sizeof(most));
    const auto wanted = static_cast<std::int32_t>(call->args[1]);
    if (wanted < hp) {
        // damage: halved (at least 1)
        const std::int32_t damage = hp - wanted, halved = damage / 2 > 0 ? damage / 2 : 1;
        call->args[1] = static_cast<std::uint64_t>(static_cast<std::int64_t>(hp - halved));
        char text[160];
        std::snprintf(text, sizeof(text), "actor %08x: %u/%u, %d damage halved to %d",
                      static_cast<unsigned>(call->args[0]), hp, most, damage, halved);
        say(text);
    }
}

void sound_played(KhHookCall *, void *)
{
    ++sounds;
}

void field_sound_played(KhHookCall *, void *)
{
    ++field_sounds;
}

// after Ov107_AiState_ApplyHit(enemy, source, hit): the enemy as kh.actors has it (none once the hit defeated it)
void enemy_hit(KhHookCall *call, void *)
{
    const KhActor enemy = actors->of(static_cast<KhRef>(call->args[0]));
    std::int32_t hp = 0, most = 0;
    if (enemy == 0 || actors->hp(enemy, &hp, &most) != KH_OK || enemy_hits++ >= 20)
        return;
    char text[160];
    std::snprintf(text, sizeof(text), "enemy of kind %d hit: HP %d/%d", actors->character(enemy), hp, most);
    say(text);
}

void scene_changed(const KhEvent *event, void *)
{
    char text[160];
    std::snprintf(text, sizeof(text), "scene %lld -> %lld, %u sound effects and %u field sounds in the scene before",
                  static_cast<long long>(event->a), static_cast<long long>(event->b), sounds, field_sounds);
    say(text);
    sounds = field_sounds = 0;
}

KhResult start(const KhHost *given)
{
    host = given;
    engine = static_cast<const KhEngine *>(host->find(host->plugin, KH_ENGINE, KH_ENGINE_VERSION));
    const auto *events = static_cast<const KhEvents *>(host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION));
    const auto *hooks = static_cast<const KhHooks *>(host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION));
    actors = static_cast<const KhActors *>(host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION));
    if (engine == nullptr || events == nullptr || hooks == nullptr || actors == nullptr)
        return KH_UNSUPPORTED;

    // the game's routine by name: int GetLanguage(void)
    char text[200];
    auto get_language = reinterpret_cast<int (*)()>(engine->routine("GetLanguage", nullptr));
    const KhRef scene_ctl = engine->datum("gSceneCtl");
    std::uint32_t scene = 0;
    if (const void *ctl = engine->pointer(scene_ctl))
        std::memcpy(&scene, static_cast<const unsigned char *>(ctl) + 8, sizeof(scene));
    std::snprintf(text, sizeof(text), "the game's language %d, scene %u (gSceneCtl at %08x); %u hook points",
                  get_language != nullptr ? get_language() : -1, scene, scene_ctl, hooks->point_count());
    say(text);

    for (std::uint32_t i = 0; i < hooks->point_count(); ++i) {
        std::snprintf(text, sizeof(text), "hook point: %s", hooks->point_signature(i));
        say(text);
    }
    hooks->add(host->plugin, "Ov022_ActorSetHp", KH_HOOK_BEFORE, 0, 0, hp_change, nullptr);
    hooks->add(host->plugin, "PlaySound", KH_HOOK_AFTER, 0, 0, sound_played, nullptr);
    hooks->add(host->plugin, "Slot_Spawn", KH_HOOK_AFTER, 0, 0, field_sound_played, nullptr);
    hooks->add(host->plugin, "Ov107_AiState_ApplyHit", KH_HOOK_AFTER, 0, 0, enemy_hit, nullptr);
    events->subscribe(host->plugin, "scene", scene_changed, nullptr);
    return KH_OK;
}

void stop()
{
    say("stopped");
}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Advanced native mod", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
