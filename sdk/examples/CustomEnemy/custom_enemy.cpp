// The KH Mod SDK's enemy example: an enemy kind given a behaviour of its own, on top of its game code.
// The Shadows (kind 0) of any mission, after each hit they take (Ov107_AiState_ApplyHit's hook, kh.actors):
//  - one that still stands with half its HP or less blinks away from its attacker, once (kh.actors' set_position:
//    moved as the game moves its own);
//  - one the hit defeats splits into two weaker Shadows where it stood (kh.actors' make_enemy, made as the area makes
//    its own), which do not split again.
// What a hit asks for is done at the next frame (kh.events): a hit comes while the game walks its enemies, which an
// enemy made or moved then would change under it. Enemies are the host's in a room: there the plugin does nothing (a
// mod's enemies are made alone, not yet in rooms).
#include <kh/kh_actors.h>
#include <kh/kh_events.h>
#include <kh/kh_hooks.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

namespace {

constexpr std::int32_t kShadow = 0;
constexpr float kBlink = 3.0F; // how far a Shadow blinks away

const KhHost *host = nullptr;
const KhActors *actors = nullptr;
std::set<KhActor> blinked, split_from, made_here; // handles: an ended one never names another enemy

struct Pending {
    bool split; // else a blink
    KhActor enemy;
    float to[3]; // where: the split's place, or the blink's
    std::int32_t hp, most;
};
std::vector<Pending> pending;

void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

// after Ov107_AiState_ApplyHit(enemy, source, hit)
void enemy_hit(KhHookCall *call, void *)
{
    if (actors->local_seat() != 0)
        return;
    const KhRef ref = static_cast<KhRef>(call->args[0]);
    const KhActor enemy = actors->of(ref);
    std::int32_t hp = 0, most = 0;
    float at[3], from[3];
    if (enemy == 0 || actors->character(enemy) != kShadow || actors->hp(enemy, &hp, &most) != KH_OK ||
        actors->position(enemy, at) != KH_OK)
        return;
    if (hp <= 0) {
        // defeated by this hit (it still stands until the game's defeat runs): two weaker ones where it stood
        if (made_here.count(enemy) == 0 && split_from.insert(enemy).second)
            pending.push_back({true, enemy, {at[0], at[1], at[2]}, hp, most});
        return;
    }
    if (hp * 2 > most || !blinked.insert(enemy).second)
        return;
    // away from the nearest player (its attacker, most likely)
    float dx = 0.0F, dz = 1.0F, nearest = -1.0F;
    for (std::int32_t seat = 0; seat < 4; ++seat) {
        const KhActor player = actors->player(seat);
        if (player == 0 || actors->position(player, from) != KH_OK)
            continue;
        const float x = at[0] - from[0], z = at[2] - from[2], d = std::sqrt(x * x + z * z);
        if (nearest < 0.0F || d < nearest) {
            nearest = d;
            dx = d > 0.001F ? x / d : 0.0F;
            dz = d > 0.001F ? z / d : 1.0F;
        }
    }
    pending.push_back({false, enemy, {at[0] + dx * kBlink, at[1], at[2] + dz * kBlink}, hp, most});
}

void frame(const KhEvent *, void *)
{
    for (const Pending &p : pending) {
        char text[120];
        if (p.split) {
            int made = 0;
            for (const float side : {-0.8F, 0.8F}) {
                const float there[3] = {p.to[0] + side, p.to[1], p.to[2]};
                const KhActor child = actors->make_enemy(host->plugin, kShadow, there, 0, 0.5F, 0.5F);
                if (child != 0) {
                    made_here.insert(child);
                    ++made;
                }
            }
            std::snprintf(text, sizeof(text), "a defeated Shadow splits into %d", made);
        } else {
            std::snprintf(text, sizeof(text), "a Shadow at %d/%d HP blinks away: %s", p.hp, p.most,
                          actors->set_position(p.enemy, p.to) == KH_OK ? "moved" : "gone first");
        }
        say(text);
    }
    pending.clear();
}

KhResult start(const KhHost *given)
{
    host = given;
    actors = static_cast<const KhActors *>(host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION));
    const auto *hooks = static_cast<const KhHooks *>(host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION));
    const auto *events = static_cast<const KhEvents *>(host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION));
    if (actors == nullptr || hooks == nullptr || events == nullptr)
        return KH_UNSUPPORTED;
    return hooks->add(host->plugin, "Ov107_AiState_ApplyHit", KH_HOOK_AFTER, 0, 0, enemy_hit, nullptr) != 0 &&
                   events->subscribe(host->plugin, "frame", frame, nullptr) != 0
               ? KH_OK
               : KH_ERROR;
}

void stop() {}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Custom enemy", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
