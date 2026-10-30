/* The rule of a character's form (`when`, docs/CHARACTERS.md "Forms"): conditions on what the player has equipped
 * and on the character's state, joined by and, or, not and parentheses. Parsed once when the description is read,
 * evaluated over a snapshot of facts the game side takes (forms.c). Plain C: no game, no system calls. */
#ifndef KHDAYS_CHARACTERS_RULES_H
#define KHDAYS_CHARACTERS_RULES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a rule is evaluated over: a seat's equipment and state. */
typedef struct KhdaysFormFacts {
    const unsigned short *panels; /* the deck's set panels, one item id per copy set (any order) */
    int panel_count;
    int gear;                         /* the deck's gear, 0 none */
    const unsigned short *gear_links; /* the links set on the gear's cells, one item id per copy */
    int gear_link_count;
    int dual;       /* the game's own dual-wield condition (its flag 0x2010) */
    int level;      /* the member's level, -1 unknown */
    int hp_percent; /* the actor's HP as a percent of its most, -1 outside the field */
    int seat, number;
    /* a native plugin's condition `name`: nonzero true; NULL, or a name no plugin has, is false */
    int (*plugin)(const char *name, int seat, int number, void *user);
    void *plugin_user;
} KhdaysFormFacts;

typedef struct KhdaysRule KhdaysRule;

/* Parses `text`; NULL with the reason (naming the place) in `why` when it is not a rule. */
KhdaysRule *khdays_rule_parse(const char *text, char *why, size_t why_size);

/* Whether the rule holds for `facts`. */
int khdays_rule_holds(const KhdaysRule *rule, const KhdaysFormFacts *facts);

/* Whether the rule asks what only the field knows (hp, a plugin's condition): it is evaluated again while playing. */
int khdays_rule_in_field(const KhdaysRule *rule);

/* Each item number the rule names (gear, links, panel), for the checks; `each` is called once per atom. */
void khdays_rule_items(const KhdaysRule *rule, void (*each)(int item, void *context), void *context);

/* Each plugin condition's name the rule names. */
void khdays_rule_plugins(const KhdaysRule *rule, void (*each)(const char *name, void *context), void *context);

void khdays_rule_free(KhdaysRule *rule);

#ifdef __cplusplus
}
#endif

#endif
