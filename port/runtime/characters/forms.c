/* The mods' characters' forms in the running game (forms.h). */
#include "forms.h"
#include "characters.h"

#include "../core/session_log.h"
#include "../net/net_session.h"
#include "../scripts/deck_link.h"
#include "../scripts/game_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEATS 4
#define CATEGORY_GEARS 4 /* the deck's gears and their links (docs/RUNTIME_MAP.md section 26) */
#define DUAL_FLAG 0x2010 /* the game's own dual-wield condition (docs/RUNTIME_MAP.md section 32) */

/* Each seat's form: the one its folder was loaded from (chosen at the camp, or told by the room) and the one forced
 * by a plugin; NULL is the character's own. `chosen` says whether the seat has had one. */
static const KhdaysCharacter *loaded[SEATS], *forced[SEATS];
static int chosen[SEATS], told_later[SEATS];
static int tracing = -1;

typedef struct Condition {
    char *name;
    KhdaysFormCondition test;
    void *user;
    const void *owner;
} Condition;
static Condition *conditions;
static int condition_count;
static KhdaysFormChanged changed_listener;

static int traced(void)
{
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_CHARACTERS") != NULL;
    }
    return tracing;
}

static int ask_plugin(const char *name, int seat, int number, void *user)
{
    (void)user;
    for (int i = 0; i < condition_count; ++i) {
        if (strcmp(conditions[i].name, name) == 0) {
            return conditions[i].test(seat, number, conditions[i].user) != 0;
        }
    }
    return 0;
}

int khdays_characters_add_condition(const char *name, KhdaysFormCondition test, void *user, const void *owner)
{
    if (name == NULL || test == NULL) {
        return 0;
    }
    for (int i = 0; i < condition_count; ++i) {
        if (strcmp(conditions[i].name, name) == 0) {
            return 0;
        }
    }
    Condition *grown = realloc(conditions, (size_t)(condition_count + 1) * sizeof(*grown));
    char *copy = malloc(strlen(name) + 1);
    if (grown == NULL || copy == NULL) {
        if (grown != NULL) {
            conditions = grown;
        }
        free(copy);
        return 0;
    }
    memcpy(copy, name, strlen(name) + 1);
    conditions = grown;
    conditions[condition_count].name = copy;
    conditions[condition_count].test = test;
    conditions[condition_count].user = user;
    conditions[condition_count].owner = owner;
    ++condition_count;
    return 1;
}

void khdays_characters_remove_conditions(const void *owner)
{
    for (int i = condition_count - 1; i >= 0; --i) {
        if (conditions[i].owner == owner) {
            free(conditions[i].name);
            memmove(&conditions[i], &conditions[i + 1], (size_t)(condition_count - i - 1) * sizeof(*conditions));
            --condition_count;
        }
    }
}

void khdays_characters_on_form(KhdaysFormChanged changed)
{
    changed_listener = changed;
}

/* The facts of the local player's equipment (the deck is each copy's own) and of seat `seat`'s state. */
typedef struct Facts {
    KhdaysFormFacts f;
    unsigned short panels[KHDAYS_LINK_DECK_CELLS], links[KHDAYS_LINK_DECK_CELLS];
} Facts;

static void take_facts(int seat, const KhdaysCharacter *c, int in_field, Facts *out)
{
    unsigned short cells[KHDAYS_LINK_DECK_CELLS];
    memset(out, 0, sizeof(*out));
    out->f.panels = out->panels;
    out->f.gear_links = out->links;
    out->f.level = -1;
    out->f.hp_percent = -1;
    out->f.seat = seat;
    out->f.number = c->number;
    out->f.plugin = ask_plugin;
    out->f.dual = khdays_link_flag(DUAL_FLAG);
    if (khdays_link_deck(cells)) {
        KhdaysLinkPanel gear;
        int gear_at = -1;
        for (int i = 0; i < KHDAYS_LINK_DECK_CELLS; ++i) {
            KhdaysLinkPanel p;
            if (cells[i] == 0) {
                continue;
            }
            out->panels[out->f.panel_count++] = cells[i];
            if (gear_at < 0 && khdays_link_panel(cells[i], &p) && p.category == CATEGORY_GEARS && p.shape >= 0) {
                gear = p;
                gear_at = i;
                out->f.gear = cells[i];
            }
        }
        short under[KHDAYS_LINK_DECK_CELLS];
        for (int i = 0; i < KHDAYS_LINK_DECK_CELLS; ++i) {
            under[i] = -1;
        }
        if (gear_at >= 0 && khdays_link_cover(&gear, gear_at, KHDAYS_LINK_DECK_CELLS, under)) {
            for (int i = 0; i < KHDAYS_LINK_DECK_CELLS; ++i) {
                KhdaysLinkPanel p;
                if (under[i] == gear_at && i != gear_at && cells[i] != 0 && khdays_link_panel(cells[i], &p) &&
                    p.category == CATEGORY_GEARS && p.shape < 0) {
                    out->links[out->f.gear_link_count++] = cells[i];
                }
            }
        }
    }
    if (in_field) {
        int icon, hp, most, low;
        if (khdays_link_hud_portrait(seat, &icon, &hp, &most, &low) && most > 0) {
            out->f.hp_percent = hp * 100 / most;
        }
    }
}

static void tell(int seat, const KhdaysCharacter *c, const KhdaysCharacter *form, const char *how)
{
    if (traced()) {
        const KhdaysCharacter *f = form != NULL ? form : c;
        khdays_log("characters: seat %d takes form %s (0x%02x) of %s, on %s%s", seat, khdays_characters_form_id(f),
                   f->number, c->identity, khdays_character_base_name(f->base), how);
    }
}

int khdays_characters_seat_form(int seat, int word)
{
    const KhdaysCharacter *c = seat >= 0 && seat < SEATS ? khdays_characters_seat_character(seat) : NULL;
    if (c == NULL) {
        return word;
    }
    const KhdaysCharacter *form = NULL;
    if (khdays_net_in_session()) {
        /* in a room each copy makes every seat's word from the room's records (the game's own choice): the seat
         * takes the first form on the base the game chose, the same on every copy; the decks are each player's own
         * and not shared, so their rules are not asked */
        for (int i = 0; i < c->form_count && word != c->base; ++i) {
            form = c->forms[i].base == word && c->forms[i].number != 0 ? &c->forms[i] : form;
            if (form != NULL) {
                break;
            }
        }
    } else {
        Facts facts;
        take_facts(seat, c, 0, &facts);
        form = forced[seat] != NULL ? forced[seat] : khdays_characters_choose(c, &facts.f);
    }
    if (form != NULL && form != c && form->number == 0) {
        if (traced()) {
            khdays_log("characters: seat %d: form %s of %s could not be served: its default form plays", seat,
                       khdays_characters_form_id(form), c->identity);
        }
        form = NULL;
    }
    if (form == c) {
        form = NULL;
    }
    const int was = chosen[seat] ? (loaded[seat] != NULL ? loaded[seat]->number : c->number) : -1;
    loaded[seat] = form;
    chosen[seat] = 1;
    told_later[seat] = 0;
    const KhdaysCharacter *f = form != NULL ? form : c;
    if (was != f->number) {
        tell(seat, c, form, word != c->base && word != f->base ? " (the game's choice for it replaced)" : "");
        if (changed_listener != NULL) {
            changed_listener(seat, f);
        }
    }
    return f->base;
}

const KhdaysCharacter *khdays_characters_seat_form_now(int seat)
{
    const KhdaysCharacter *c = seat >= 0 && seat < SEATS ? khdays_characters_seat_character(seat) : NULL;
    if (c == NULL) {
        return NULL;
    }
    return loaded[seat] != NULL ? loaded[seat] : c;
}

void khdays_characters_set_seat_form(int seat, const KhdaysCharacter *form)
{
    if (seat < 0 || seat >= SEATS) {
        return;
    }
    const KhdaysCharacter *c = khdays_characters_seat_character(seat);
    loaded[seat] = form == c ? NULL : form;
    chosen[seat] = form != NULL;
}

void khdays_characters_forms_clear(void)
{
    for (int seat = 0; seat < SEATS; ++seat) {
        loaded[seat] = forced[seat] = NULL;
    }
    memset(chosen, 0, sizeof(chosen));
    memset(told_later, 0, sizeof(told_later));
}

void khdays_characters_force_form(int seat, const KhdaysCharacter *form)
{
    if (seat >= 0 && seat < SEATS) {
        forced[seat] = form;
    }
}

void khdays_characters_forms_frame(void)
{
    if (khdays_net_in_session()) {
        return; /* a room's forms are the game's choice (khdays_characters_seat_form) */
    }
    for (int seat = 0; seat < SEATS; ++seat) {
        const KhdaysCharacter *c = khdays_characters_seat_character(seat), *now = khdays_characters_seat_form_now(seat);
        int asks = 0;
        for (int i = 0; c != NULL && i < c->form_count; ++i) {
            asks |= khdays_rule_in_field(c->forms[i].rule);
        }
        if (!asks || told_later[seat]) {
            continue;
        }
        Facts facts;
        take_facts(seat, c, 1, &facts);
        const KhdaysCharacter *form = forced[seat] != NULL ? forced[seat] : khdays_characters_choose(c, &facts.f);
        if (form == NULL) {
            form = c;
        }
        if (form != now) {
            /* the actor's code and data are its form's base's, made as it entered: another form waits for the
             * next camp */
            told_later[seat] = 1;
            if (traced()) {
                khdays_log("characters: seat %d's rules now choose form %s of %s: from the next mission",
                           seat, khdays_characters_form_id(form), c->identity);
            }
        }
    }
}
