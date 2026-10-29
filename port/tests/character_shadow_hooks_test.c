/* Owner resolution and native shadow texture bindings against a synthetic game memory window. */
#include "../gfx/character_shadow.h"
#include "../runtime/core/window.h"
#include <stdio.h>
#include <string.h>

static unsigned char memory[0x02010000];
unsigned char *khdays_window = memory;
static uintptr_t last_owner;
static int on = 1, depth, sprites, failed;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                 \
            ++failed;                                                                                                  \
        }                                                                                                              \
    } while (0)
KhdaysRef khdays_reference(const void *host)
{
    return (KhdaysRef)((const unsigned char *)host - memory);
}
int khdays_character_shadows_enabled(void)
{
    return on;
}
void khdays_shadow_capture_begin(uintptr_t owner, int enabled)
{
    last_owner = owner;
    ++depth;
    (void)enabled;
}
void khdays_shadow_capture_end(void)
{
    --depth;
}
void khdays_shadow_volume_begin(uintptr_t owner, const int32_t *pos, int enabled)
{
    (void)owner;
    (void)pos;
    (void)enabled;
}
void khdays_shadow_volume_end(void) {}
void khdays_shadow_sprite_begin(void)
{
    ++sprites;
}
void ShadowVolume_Draw(KhdaysRef volume)
{
    (void)volume;
}
extern void khdays_character_caster_begin(const void *);
extern void khdays_character_caster_end(void);
extern void khdays_character_bind_enemy(const void *);
extern void khdays_character_forget_enemy(const void *);
extern void khdays_character_object_begin(const void *);
extern void khdays_character_object_end(void);
extern int khdays_character_sprite_begin(const void *);
extern void khdays_character_sprite_end(void);

static void word(KhdaysRef ref, size_t offset, KhdaysRef value)
{
    memcpy((unsigned char *)KHDAYS_W(ref) + offset, &value, 4);
}
static void half(KhdaysRef ref, size_t offset, unsigned short value)
{
    memcpy((unsigned char *)KHDAYS_W(ref) + offset, &value, 2);
}

int main(void)
{
    const KhdaysRef body = 0x02001000, weapon = 0x02002000, actor = 0x02003000, child = 0x02004000;
    const KhdaysRef flat = 0x02005000, root = 0x02006000, state = 0x02007000, mat = 0x02008000;
    word(weapon, 0xbc, body);
    khdays_character_caster_begin(KHDAYS_W(weapon));
    CHECK(last_owner == body);
    khdays_character_caster_end();
    CHECK(depth == 0);
    word(actor, 0x9c, root);
    word(actor, 0x1a8, flat);
    word(child, 0, root);
    khdays_character_bind_enemy(KHDAYS_W(actor));
    word(state, 0xd8, mat);
    memory[mat + 5] = 2;
    half(mat, 6, 80);
    half(mat, 10, 8);
    half(mat, 12, 4);
    half(mat, 14, 12);
    memcpy((unsigned char *)KHDAYS_W(mat) + 24, "m_body", 7);
    memcpy((unsigned char *)KHDAYS_W(mat) + 40, "m_shadow", 9);
    half(mat, 0, 128);
    memory[mat + 129] = 1;
    half(mat, 130, 64);
    half(mat, 134, 8);
    half(mat, 136, 4);
    half(mat, 138, 8);
    half(mat, 140, 208);
    memory[mat + 142] = 1;
    memory[mat + 208] = 1;
    memory[state + 0xad] = 1;
    char *name = (char *)KHDAYS_W(mat) + 144;
    memcpy(name, "kage00", 7);
    khdays_character_object_begin(KHDAYS_W(child));
    CHECK(last_owner == root);
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    khdays_character_object_end();
    khdays_character_object_begin(KHDAYS_W(flat));
    CHECK(last_owner == root);
    CHECK(khdays_character_sprite_begin(KHDAYS_W(state)));
    CHECK(sprites == 1);
    khdays_character_sprite_end();
    khdays_character_object_end();
    CHECK(depth == 0);
    khdays_character_forget_enemy(KHDAYS_W(flat));
    khdays_character_object_begin(KHDAYS_W(flat));
    CHECK(last_owner == flat);
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    khdays_character_object_end();
    memcpy(name, "ef_htd", 7);
    khdays_character_caster_begin(KHDAYS_W(body));
    CHECK(khdays_character_sprite_begin(KHDAYS_W(state)));
    khdays_character_sprite_end();
    memory[state + 0xad] = 0;
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    memory[state + 0xad] = 1;
    half(mat, 136, 0); /* malformed texture pairing dictionary */
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    half(mat, 136, 4);
    on = 0;
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    on = 1;
    memcpy(name, "ef_other", 9);
    memcpy((unsigned char *)KHDAYS_W(mat) + 40, "ef_htd", 7);
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    memory[state + 0xad] = 255;
    CHECK(!khdays_character_sprite_begin(KHDAYS_W(state)));
    khdays_character_caster_end();
    CHECK(depth == 0 && sprites == 2);
    return failed ? 1 : 0;
}
