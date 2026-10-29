// The KH Mod SDK's Limit example: a character's Limit programmed in the plugin, the game's own never used.
// The mod carries a made-up character (characters/spinner/: three boxes on a skeleton of its own, with its own clips,
// on Roxas's code). Its Limit starts as the game's would -- the button held with the HP in danger -- but:
//  - kh.hooks: as the game's code enters the Limit's state for its actor (Ov022_ActorSetState), the plugin skips it
//    and starts its own (any other actor's Limit is the game's);
//  - kh.input: while it lasts the buttons and directions are kept from the game, so the actor stands and does nothing
//    else;
//  - kh.characters: it plays the character's own clips (limit_start, limit_spin again and again, limit_end);
//  - kh.combat: each spin hits every enemy within its reach, its damage the game's formula for the character's
//    strength, dealt through the game's own hit routine (the enemies flinch, are defeated and reward as they would).
//  - kh.parameters: its spins, their damage, reach and status are its mod's parameters (parameters.ini), which KH
//    Studio shows and saves without loading the plugin.
// On a guest's copy in a room the game's state is the host's: there the plugin leaves the game's Limit alone.
#include <kh/kh_actors.h>
#include <kh/kh_characters.h>
#include <kh/kh_combat.h>
#include <kh/kh_hooks.h>
#include <kh/kh_input.h>
#include <kh/kh_parameters.h>

#include <cstdint>
#include <cstdio>

namespace {

// The game's numbers for the Limit's states on Roxas's code (every character on it), which Ov022_ActorSetState is
// given: 23 as it starts (Ov022_SelectForcedState: its b_ro_L000), then 34 and 24 at each of its hits (its
// b_ro_L001; between them the jump's 2 and 22). Seen with the KH Mod SDK's own hooks.
constexpr std::int32_t kLimitStates[] = {23, 34, 24};

constexpr int kStartFrames = 30; // limit_start's half second
constexpr int kSpinFrames = 15;  // a spin (limit_spin is a quarter of a second, played again for each)
constexpr int kEndFrames = 30;   // limit_end

// Its parameters (parameters.ini), read as it starts: these when the mod has none.
int spins = 8;
float spin_damage = 1.25F; // each spin's damage: the character's strength times this
float reach = 2.5F;        // the spins' reach, in the field's units
std::int32_t status = KH_STATUS_NONE, chance = 0;

const KhHost *host = nullptr;
const KhActors *actors = nullptr;
const KhCharacters *characters = nullptr;
const KhCombat *combat = nullptr;
const KhInput *input = nullptr;
std::int32_t spinner = -1;

// The Limit under way: its actor and its frame (-1: none).
KhActor limit_actor = 0;
int limit_frame = -1;
int limit_hits = 0;

void say(const char *text)
{
    host->log(host->plugin, KH_LOG_INFO, text);
}

bool ours(KhActor actor)
{
    return actor != 0 && actors->character(actor) == spinner && actors->seat(actor) == actors->local_seat() &&
           actors->local_seat() == 0;
}

void begin(KhActor actor)
{
    limit_actor = actor;
    limit_frame = 0;
    limit_hits = 0;
    input->hide(host->plugin, ~0u); // every button and direction: the actor stands while it spins
    characters->play(host->plugin, actor, "limit_start", 0);
    say("Spinner's Limit starts (the game's skipped)");
}

void finish()
{
    char text[100];
    std::snprintf(text, sizeof(text), "Spinner's Limit ends: %d hits taken", limit_hits);
    say(text);
    characters->stop_playing(host->plugin, limit_actor);
    input->hide(host->plugin, 0);
    limit_actor = 0;
    limit_frame = -1;
}

// before Ov022_ActorSetState(actor, state): its Limit's state skipped, the plugin's started instead
void state_entered(KhHookCall *call, void *)
{
    const auto state = static_cast<std::int32_t>(call->args[1]);
    if (state != kLimitStates[0] && state != kLimitStates[1] && state != kLimitStates[2])
        return;
    const KhActor actor = actors->of(static_cast<KhRef>(call->args[0]));
    if (!ours(actor))
        return;
    call->skip = 1; // the game's never runs for it, also while the plugin's goes on
    call->result = 0;
    if (limit_frame < 0)
        begin(actor);
}

void frame(KhActor actor, void *)
{
    if (actor != limit_actor || limit_frame < 0)
        return;
    const int f = limit_frame++;
    const int spun = f - kStartFrames;
    if (spun >= 0 && spun < spins * kSpinFrames && spun % kSpinFrames == 0) {
        characters->play(host->plugin, actor, "limit_spin", 0);
        float at[3];
        if (actors->position(actor, at) == KH_OK) {
            const KhHit hit = {
                sizeof(KhHit), combat->damage(actor, spin_damage), {at[0], at[1], at[2]}, status, chance, 0};
            const std::int32_t took = combat->hit_around(actor, at, reach, &hit);
            limit_hits += took;
            char text[100];
            std::snprintf(text, sizeof(text), "spin %d: %d damage, %d enemies took it", spun / kSpinFrames + 1,
                          hit.damage, took);
            say(text);
        }
    } else if (spun == spins * kSpinFrames) {
        characters->play(host->plugin, actor, "limit_end", 0);
    } else if (spun >= spins * kSpinFrames + kEndFrames) {
        finish();
    }
}

void left(KhActor actor, void *)
{
    if (actor == limit_actor && limit_frame >= 0)
        finish();
}

const KhCharacterCalls calls = {sizeof(KhCharacterCalls), nullptr, frame, nullptr, left};

KhResult start(const KhHost *given)
{
    host = given;
    actors = static_cast<const KhActors *>(host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION));
    characters = static_cast<const KhCharacters *>(host->find(host->plugin, KH_CHARACTERS, KH_CHARACTERS_VERSION));
    combat = static_cast<const KhCombat *>(host->find(host->plugin, KH_COMBAT, KH_COMBAT_VERSION));
    input = static_cast<const KhInput *>(host->find(host->plugin, KH_INPUT, KH_INPUT_VERSION));
    const auto *hooks = static_cast<const KhHooks *>(host->find(host->plugin, KH_HOOKS, KH_HOOKS_VERSION));
    if (actors == nullptr || characters == nullptr || combat == nullptr || input == nullptr || hooks == nullptr)
        return KH_UNSUPPORTED;
    if (const auto *p =
            static_cast<const KhParameters *>(host->find(host->plugin, KH_PARAMETERS, KH_PARAMETERS_VERSION))) {
        spins = p->integer(host->plugin, "spins", spins);
        spin_damage = static_cast<float>(p->number(host->plugin, "damage", spin_damage));
        reach = static_cast<float>(p->number(host->plugin, "reach", reach));
        status = p->integer(host->plugin, "status", status); // a choice: its index (none, burn, freeze, jolt, toss)
        chance = p->integer(host->plugin, "chance", chance);
        char text[120];
        std::snprintf(text, sizeof(text), "its Limit: %d spins of %.2f times its strength within %.1f", spins,
                      static_cast<double>(spin_damage), static_cast<double>(reach));
        say(text);
    }
    spinner = characters->find(host->plugin, "spinner");
    if (spinner < 0) {
        say("no character spinner in this mod (its files missing?): nothing to do");
        return KH_ERROR;
    }
    if (characters->listen(host->plugin, spinner, &calls, nullptr) == 0 ||
        hooks->add(host->plugin, "Ov022_ActorSetState", KH_HOOK_BEFORE, 0, KH_HOOK_REPLACES, state_entered, nullptr) ==
            0)
        return KH_ERROR;
    return KH_OK;
}

void stop() {}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Custom Limit", start, stop);

} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
