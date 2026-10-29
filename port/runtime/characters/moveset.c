/* A character's [moveset] in its combo files (moveset.h). */
#include "moveset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ci.b: a word counting the keyblades, then a 0x34-byte record each -- its two graphs' three pools' places (words,
 * from the file's start), their counts (u16), each graph's four entry points into its first pool (s16) -- then the
 * pools: 8 bytes a node, its attack (a word), the node it goes on to and its branch (s16: an index in the same
 * pool, TO_ENTRY the graph's entry point, -1 none). The first pool is the chain, the second its finisher, the
 * third the branches' nodes. */
#define RECORD 0x34
#define COUNTS 0x18
#define ENTRIES 0x24
#define NODE 8
#define TO_ENTRY 0x80

/* cm.b: a word whose low byte counts the attacks, their records' places, each record 15 words (its number, the
 * kind of its hits, its animation's row, ..., its hits' count last) and then its hits, 15 words each for the
 * sweeping kind (0): the damage (fx32, Ov022_ComputeStrengthDamage's), the first and last frame, the push out and
 * up, two flags, a level, then where it is (x, y, z), its radius and spread (Ov022_RunContactMode0's slot). */
#define WORD 4
#define HEADER (15 * WORD)
#define HIT (15 * WORD)
#define SWEEPING 0
enum { HIT_DAMAGE = 0, HIT_PUSH_OUT = 3, HIT_PUSH_UP = 4, HIT_X = 8, HIT_Z = 10, HIT_RADIUS = 11 };

static unsigned u32_at(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_u16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

int khdays_moveset_wanted(const KhdaysCharacter *c)
{
    for (int g = 0; g < 2; ++g) {
        if (c->combos[g].count > 0 || c->combos[g].finisher >= 0) {
            return 1;
        }
    }
    return c->attack_count > 0;
}

/* The place of the first record of attack `id` in cm.b; 0 when it has none or the file is not as the game's. */
static size_t attack_at(const unsigned char *cm, size_t size, int id)
{
    const size_t count = size >= WORD ? cm[0] : 0;
    for (size_t i = 0; i < count && WORD + (i + 1) * WORD <= size; ++i) {
        const size_t at = u32_at(cm + WORD + i * WORD);
        if (at + HEADER <= size && at >= WORD && (int)u32_at(cm + at) == id) {
            return at;
        }
    }
    return 0;
}

/* A keyblade's attacks so far: the game keeps each once (its id), and each of their animations once. */
typedef struct Seen {
    int attacks[KHDAYS_MOVESET_ATTACKS], animations[KHDAYS_MOVESET_ANIMATIONS];
    int attack_count, animation_count;
} Seen;

static int see(Seen *s, int id, const unsigned char *cm, size_t cm_size, int keyblade, char *why, size_t why_size)
{
    const size_t at = attack_at(cm, cm_size, id);
    int animation;
    if (at == 0) {
        snprintf(why, why_size, "attack %d: its base has no such attack (keyblade %d's combos)", id, keyblade);
        return 0;
    }
    for (int i = 0; i < s->attack_count; ++i) {
        if (s->attacks[i] == id) {
            return 1;
        }
    }
    if (s->attack_count == KHDAYS_MOVESET_ATTACKS) {
        snprintf(why, why_size, "keyblade %d's combos: more than %d attacks (the game's table)", keyblade,
                 KHDAYS_MOVESET_ATTACKS);
        return 0;
    }
    s->attacks[s->attack_count++] = id;
    animation = (int)(u32_at(cm + at + 2 * WORD) & 0xff);
    for (int i = 0; i < s->animation_count; ++i) {
        if (s->animations[i] == animation) {
            return 1;
        }
    }
    if (s->animation_count == KHDAYS_MOVESET_ANIMATIONS) {
        snprintf(why, why_size, "keyblade %d's combos: attacks of more than %d animations (the game's table)", keyblade,
                 KHDAYS_MOVESET_ANIMATIONS);
        return 0;
    }
    s->animations[s->animation_count++] = animation;
    return 1;
}

/* The new file, growing. */
typedef struct Out {
    unsigned char *data;
    size_t size, room;
} Out;

static unsigned char *grow(Out *o, size_t n)
{
    if (o->size + n > o->room) {
        size_t room = o->room > 0 ? o->room : 256;
        while (room < o->size + n) {
            room *= 2;
        }
        unsigned char *bigger = realloc(o->data, room);
        if (bigger == NULL) {
            return NULL;
        }
        o->data = bigger;
        o->room = room;
    }
    unsigned char *at = o->data + o->size;
    o->size += n;
    return at;
}

static int node(Out *o, int attack, int next, int branch)
{
    unsigned char *at = grow(o, NODE);
    if (at == NULL) {
        return 0;
    }
    put_u32(at, (unsigned)attack);
    put_u16(at + 4, (unsigned)next);
    put_u16(at + 6, (unsigned)branch);
    return 1;
}

int khdays_moveset_combos(const KhdaysCharacter *c, const unsigned char *ci, size_t ci_size, const unsigned char *cm,
                          size_t cm_size, unsigned char **out, size_t *out_size, char *why, size_t why_size)
{
    const size_t keyblades = ci_size >= WORD ? u32_at(ci) : 0;
    Out o = {NULL, 0, 0};
    *out = NULL;
    *out_size = 0;
    why[0] = '\0';
    if (ci_size < WORD || keyblades > (ci_size - WORD) / RECORD) {
        snprintf(why, why_size, "its base's ci.b is not as the game's");
        return 0;
    }
    if (grow(&o, WORD + keyblades * RECORD) == NULL) {
        snprintf(why, why_size, "out of memory");
        return 0;
    }
    memcpy(o.data, ci, o.size);
    for (size_t k = 0; k < keyblades; ++k) {
        const unsigned char *record = ci + WORD + k * RECORD;
        Seen seen;
        memset(&seen, 0, sizeof(seen));
        for (int g = 0; g < 2; ++g) {
            const KhdaysCharacterCombo *combo = &c->combos[g];
            for (int p = 0; p < 3; ++p) {
                const size_t place = o.size, from = u32_at(record + (size_t)(g * 3 + p) * WORD);
                const unsigned count = record[COUNTS + g * 6 + p * 2] | (unsigned)record[COUNTS + g * 6 + p * 2 + 1]
                                                                            << 8;
                int nodes = 0, ok = 1;
                if (p == 0 && combo->count > 0) {
                    for (int i = 0; i < combo->count && ok; ++i) {
                        ok = node(&o, combo->attacks[i], i + 1 < combo->count ? i + 1 : TO_ENTRY, -1) &&
                             see(&seen, combo->attacks[i], cm, cm_size, (int)k, why, why_size);
                    }
                    nodes = combo->count;
                } else if (p == 1 && combo->finisher >= 0) {
                    ok = node(&o, combo->finisher, -1, -1) &&
                         see(&seen, combo->finisher, cm, cm_size, (int)k, why, why_size);
                    nodes = 1;
                } else if (!(p == 2 && combo->count > 0)) {
                    /* the keyblade's own pool */
                    if (from > ci_size || count > (ci_size - from) / NODE) {
                        snprintf(why, why_size, "its base's ci.b is not as the game's");
                        ok = 0;
                    }
                    for (unsigned i = 0; i < count && ok; ++i) {
                        const unsigned char *n = ci + from + (size_t)i * NODE;
                        ok = node(&o, (int)u32_at(n), n[4] | n[5] << 8, n[6] | n[7] << 8) &&
                             see(&seen, (int)u32_at(n), cm, cm_size, (int)k, why, why_size);
                    }
                    nodes = (int)count;
                }
                if (!ok) {
                    if (why[0] == '\0') {
                        snprintf(why, why_size, "out of memory");
                    }
                    free(o.data);
                    return 0;
                }
                unsigned char *at = o.data + WORD + k * RECORD;
                put_u32(at + (size_t)(g * 3 + p) * WORD, (unsigned)place);
                put_u16(at + COUNTS + g * 6 + p * 2, (unsigned)nodes);
            }
            if (combo->count > 0) {
                /* the chain's head; its last node (where the finisher follows); no branches */
                const int entries[4] = {0, combo->count - 1, -1, -1};
                for (int e = 0; e < 4; ++e) {
                    put_u16(o.data + WORD + k * RECORD + ENTRIES + g * 8 + e * 2, (unsigned)entries[e]);
                }
            }
        }
    }
    *out = o.data;
    *out_size = o.size;
    return 1;
}

/* `v` times `times`, rounded. */
static unsigned scaled(unsigned v, float times)
{
    return (unsigned)(int)floor((double)(int)v * (double)times + 0.5);
}

int khdays_moveset_attacks(const KhdaysCharacter *c, unsigned char *cm, size_t cm_size, char *why, size_t why_size)
{
    const size_t count = cm_size >= WORD ? cm[0] : 0;
    for (int a = 0; a < c->attack_count; ++a) {
        const KhdaysCharacterAttack *attack = &c->attacks[a];
        int found = 0;
        for (size_t i = 0; i < count && WORD + (i + 1) * WORD <= cm_size; ++i) {
            const size_t at = u32_at(cm + WORD + i * WORD);
            if (at < WORD || at + HEADER > cm_size || (int)u32_at(cm + at) != attack->id) {
                continue;
            }
            const size_t hits = u32_at(cm + at + 14 * WORD);
            found = 1;
            if (u32_at(cm + at + WORD) != SWEEPING) {
                snprintf(why, why_size, "attack %d: its hits are not the sweeping kind (%u): left as they are",
                         attack->id, u32_at(cm + at + WORD));
                return 0;
            }
            if (hits > (cm_size - at - HEADER) / HIT) {
                snprintf(why, why_size, "its base's cm.b is not as the game's (attack %d)", attack->id);
                return 0;
            }
            for (size_t h = 0; h < hits; ++h) {
                unsigned char *hit = cm + at + HEADER + h * HIT;
                const struct {
                    int word;
                    float times;
                } fields[] = {{HIT_DAMAGE, attack->damage}, {HIT_PUSH_OUT, attack->push}, {HIT_PUSH_UP, attack->push},
                              {HIT_X, attack->reach},       {HIT_Z, attack->reach},       {HIT_RADIUS, attack->reach}};
                for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); ++f) {
                    unsigned char *w = hit + (size_t)fields[f].word * WORD;
                    put_u32(w, scaled(u32_at(w), fields[f].times));
                }
            }
        }
        if (!found) {
            snprintf(why, why_size, "attack %d: its base has no such attack", attack->id);
            return 0;
        }
    }
    return 1;
}
