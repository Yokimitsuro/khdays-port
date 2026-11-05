/* Associate existing shadow volumes with the geometry emitted by their scene nodes. */
#include "../../gfx/character_shadow.h"
#include "../../gfx/g3d.h"
#include "../../host/app/hud_settings.h"
#include "../core/window.h"
#include <stdlib.h>
#include <string.h>

typedef struct Binding {
    KhdaysRef sprite, owner;
} Binding;
static Binding *bindings;
static int binding_count, binding_capacity;
static int object_depth, *shadow_object, object_capacity; /* by depth: whether that object is a flat shadow */

/* Room for `need` entries of `size` bytes in a stack grown as the draws nest deeper; 0 out of memory. */
static int stack_room(void **stack, int *capacity, int need, size_t size)
{
    if (need <= *capacity)
        return 1;
    int n = *capacity ? *capacity : 16;
    while (n < need)
        n *= 2;
    void *grown = realloc(*stack, (size_t)n * size);
    if (!grown)
        return 0;
    *stack = grown;
    *capacity = n;
    return 1;
}

static KhdaysRef reference_at(const void *p, size_t offset)
{
    KhdaysRef value;
    memcpy(&value, (const unsigned char *)p + offset, sizeof(value));
    return value;
}

/* The last of a chain of links at +`at` from `from`: up to a link that is 0, itself or out of the game's memory, or,
 * for one that loops (garbage), where it laps a second walker going at half its pace. */
static KhdaysRef chain_end(KhdaysRef from, size_t at)
{
    KhdaysRef end = from, slow = from;
    for (unsigned walked = 1;; ++walked) {
        const KhdaysRef next = reference_at(KHDAYS_W(end), at);
        if (!next || next == end || !khdays_game_ram(next))
            break;
        end = next;
        if (walked % 2 == 0 && (slow = reference_at(KHDAYS_W(slow), at)) == end)
            break;
    }
    return end;
}

/* A scene node's parent is at +0xbc: SceneNode_AttachToModelJoint (main) sets it, SceneNode_Detach clears it. The
 * decompilation's Scene_DrawNode names that offset `displayList`, a name these two contradict. */
static KhdaysRef scene_owner(const void *node)
{
    return chain_end(KHDAYS_REF(node), 0xbc);
}

extern void ShadowVolume_Draw(KhdaysRef volume);
extern KhdaysRef khdays_kh2_camera_faded_node(float *fade);

/* The KH2 camera's first person fades the player out, and what the player holds: all drawn in these scopes. */
static float fade_now = 1.0f, *fade_outer;
static int fade_depth, fade_capacity;

static void fade_begin(KhdaysRef owner)
{
    float fade = 1.0f;
    const KhdaysRef node = khdays_kh2_camera_faded_node(&fade);
    if (stack_room((void **)&fade_outer, &fade_capacity, fade_depth + 1, sizeof(*fade_outer))) {
        fade_outer[fade_depth] = fade_now;
    }
    ++fade_depth;
    if (node != 0 && scene_owner(KHDAYS_W(node)) == owner) {
        fade_now = fade;
        khdays_g3d_set_fade(fade);
    }
}

static void fade_end(void)
{
    if (fade_depth > 0 && --fade_depth < fade_capacity && fade_outer[fade_depth] != fade_now) {
        fade_now = fade_outer[fade_depth];
        khdays_g3d_set_fade(fade_now);
    }
}

void khdays_character_shadow(const void *volume, const void *node)
{
    fade_begin(scene_owner(node));
    khdays_shadow_volume_begin(KHDAYS_REF(node), khdays_character_shadows_enabled());
    ShadowVolume_Draw(KHDAYS_REF(volume));
    khdays_shadow_volume_end();
    fade_end();
}

void khdays_character_caster_begin(const void *node)
{
    fade_begin(scene_owner(node));
    khdays_shadow_capture_begin(scene_owner(node), khdays_character_shadows_enabled());
}

void khdays_character_caster_end(void)
{
    khdays_shadow_capture_end();
    fade_end();
}

/* The shared enemy framework owns the flat shadow separately from the model-node tree. */
void khdays_character_bind_enemy(const void *actor)
{
    const KhdaysRef sprite_ref = reference_at(actor, 0x1a8), owner = reference_at(actor, 0x9c);
    if (!sprite_ref || !owner)
        return;
    for (int i = 0; i < binding_count; ++i)
        if (bindings[i].sprite == sprite_ref) {
            bindings[i].owner = owner;
            return;
        }
    if (binding_count == binding_capacity) {
        int capacity = binding_capacity ? binding_capacity * 2 : 64;
        Binding *grown = realloc(bindings, (size_t)capacity * sizeof(*grown));
        if (!grown)
            return;
        bindings = grown;
        binding_capacity = capacity;
    }
    bindings[binding_count++] = (Binding){sprite_ref, owner};
}

void khdays_character_forget_enemy(const void *sprite_ptr)
{
    const KhdaysRef ref = KHDAYS_REF(sprite_ptr);
    for (int i = 0; i < binding_count; ++i)
        if (bindings[i].sprite == ref) {
            bindings[i] = bindings[--binding_count];
            return;
        }
}

void khdays_character_object_begin(const void *object)
{
    const int on = khdays_character_shadows_enabled();
    const KhdaysRef ref = KHDAYS_REF(object);
    KhdaysRef owner = ref, parent = ref, slow = ref;
    int is_shadow = 0;
    for (int i = 0; on && i < binding_count; ++i)
        if (bindings[i].sprite == ref) {
            owner = bindings[i].owner;
            is_shadow = 1;
            break;
        }
    /* +0 is a node's owner (Node_UnlinkAndClearRefs reads it so, Node_BaseInit clears it). Assumed, not traced in the
     * game: that it leads from an enemy's attachments to its model tree (actor +0x9c). Followed up to a bound enemy's,
     * or, in a chain that loops (garbage), to where it laps a walker going at half its pace. */
    for (unsigned walked = 1; on && !is_shadow && parent; ++walked) {
        int found = 0;
        for (int i = 0; i < binding_count; ++i)
            if (bindings[i].owner == parent) {
                owner = parent;
                found = 1;
                break;
            }
        if (found)
            break;
        const KhdaysRef next = reference_at(KHDAYS_W(parent), 0);
        if (next == parent || !khdays_game_ram(next))
            break;
        parent = next;
        if (walked % 2 == 0 && (slow = reference_at(KHDAYS_W(slow), 0)) == parent)
            break;
    }
    if (stack_room((void **)&shadow_object, &object_capacity, object_depth + 1, sizeof(*shadow_object)))
        shadow_object[object_depth] = is_shadow;
    ++object_depth;
    khdays_shadow_capture_begin(owner, on);
}

void khdays_character_object_end(void)
{
    khdays_shadow_capture_end();
    if (object_depth > 0)
        --object_depth;
}

static unsigned short half_at(const void *p, size_t offset)
{
    unsigned short value;
    memcpy(&value, (const unsigned char *)p + offset, sizeof(value));
    return value;
}

/* Texture pairing entries name a texture and list the material IDs that use it. */
static int shadow_texture(const unsigned char *mat, unsigned id, int enemy)
{
    const unsigned pairing = half_at(mat, 0);
    if (pairing < 4)
        return 0;
    const unsigned char *dict = mat + pairing;
    const unsigned size = half_at(dict, 2), entries = half_at(dict, 6);
    if (size < 8 || entries > size - 4)
        return 0;
    const unsigned unit = half_at(dict, entries), names = entries + half_at(dict, entries + 2);
    const unsigned count = dict[1];
    if (unit < 4 || count * unit > size - entries - 4 || names > size || count * 16 > size - names)
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        const char *name = (const char *)dict + names + i * 16;
        if (memcmp(name, "ef_htd\0", 7) && !(enemy && memcmp(name, "kage00\0", 7) == 0))
            continue;
        const unsigned char *entry = dict + entries + 4 + i * unit;
        const unsigned char *ids = mat + half_at(entry, 0);
        for (unsigned j = 0; j < entry[2]; ++j)
            if (ids[j] == id)
                return 1;
    }
    return 0;
}

/* materialAd and pResMat are the NitroSystem SBC state's current material and resource. */
int khdays_character_sprite_begin(const void *state)
{
    if (!khdays_character_shadows_enabled())
        return 0;
    const unsigned char *rs = state;
    const KhdaysRef mat = reference_at(state, 0xd8);
    if (!khdays_game_ram(mat))
        return 0;
    const unsigned id = rs[0xad];
    const unsigned char *resource = KHDAYS_W(mat);
    if (id >= resource[5])
        return 0;
    const int enemy = object_depth > 0 && object_depth <= object_capacity && shadow_object[object_depth - 1];
    if (!shadow_texture(resource, id, enemy))
        return 0;
    khdays_shadow_sprite_begin();
    return 1;
}

void khdays_character_sprite_end(void)
{
    khdays_shadow_volume_end();
}
