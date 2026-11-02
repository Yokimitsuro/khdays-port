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
static int object_depth, shadow_object[16];

static KhdaysRef reference_at(const void *p, size_t offset)
{
    KhdaysRef value;
    memcpy(&value, (const unsigned char *)p + offset, sizeof(value));
    return value;
}

static KhdaysRef scene_owner(const void *node)
{
    KhdaysRef owner = KHDAYS_REF(node);
    for (int i = 0; i < 16; ++i) {
        KhdaysRef parent = reference_at(KHDAYS_W(owner), 0xbc);
        if (!parent || parent == owner || !khdays_game_ram(parent))
            break;
        owner = parent;
    }
    return owner;
}

extern void ShadowVolume_Draw(KhdaysRef volume);
extern KhdaysRef khdays_kh2_camera_faded_node(float *fade);

/* The KH2 camera's first person fades the player out, and what the player holds: all drawn in these scopes. */
static float fade_now = 1.0f, fade_outer[16];
static int fade_depth;

static void fade_begin(KhdaysRef owner)
{
    float fade = 1.0f;
    const KhdaysRef node = khdays_kh2_camera_faded_node(&fade);
    if (fade_depth < 16) {
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
    if (fade_depth > 0 && --fade_depth < 16 && fade_outer[fade_depth] != fade_now) {
        fade_now = fade_outer[fade_depth];
        khdays_g3d_set_fade(fade_now);
    }
}

void khdays_character_shadow(const void *volume, const void *node)
{
    fade_begin(scene_owner(node));
    khdays_shadow_volume_begin(KHDAYS_REF(node), volume, khdays_character_shadows_enabled());
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
    const KhdaysRef ref = KHDAYS_REF(object);
    KhdaysRef owner = ref, parent = ref;
    int is_shadow = 0;
    for (int i = 0; i < binding_count; ++i)
        if (bindings[i].sprite == ref) {
            owner = bindings[i].owner;
            is_shadow = 1;
            break;
        }
    if (!is_shadow) {
        for (int depth = 0; depth < 16 && parent; ++depth) {
            int found = 0;
            for (int i = 0; i < binding_count; ++i)
                if (bindings[i].owner == parent) {
                    owner = parent;
                    found = 1;
                    break;
                }
            if (found)
                break;
            KhdaysRef next = reference_at(KHDAYS_W(parent), 0);
            if (next == parent || !khdays_game_ram(next))
                break;
            parent = next;
        }
    }
    if (object_depth < 16)
        shadow_object[object_depth] = is_shadow;
    ++object_depth;
    khdays_shadow_capture_begin(owner, khdays_character_shadows_enabled());
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
    const int enemy = object_depth > 0 && object_depth <= 16 && shadow_object[object_depth - 1];
    if (!shadow_texture(resource, id, enemy))
        return 0;
    khdays_shadow_sprite_begin();
    return 1;
}

void khdays_character_sprite_end(void)
{
    khdays_shadow_volume_end();
}
