/* The mods' characters' forms in the running game: the facts a seat's rules are evaluated over (its deck, the game's
 * dual-wield flag, its HP in the field, the plugins' conditions), and the form each seat has -- chosen where the camp
 * makes the seat's character word, after the game's own choice (docs/RUNTIME_MAP.md section 32), and again in the
 * field where a rule asks what only the field knows. */
#ifndef KHDAYS_CHARACTERS_FORMS_H
#define KHDAYS_CHARACTERS_FORMS_H

#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The camp made seat `seat`'s character word `word` (Ov008_RefreshSaveSlotWidget's join, after
 * Ov008_GetEntryStatusByte): a mod's character's seat gets the base of the form its rules choose (its own base when
 * none holds, also when the game chose its dual-wield Roxas for it); in a room, of its first form on the base the
 * game chose (`word`, the same on every copy), else its own. Any other seat keeps `word`. */
int khdays_characters_seat_form(int seat, int word);

/* The form seat `seat` has (its character's own for its default form); NULL for a seat without a mod's character. */
const KhdaysCharacter *khdays_characters_seat_form_now(int seat);

/* Seat `seat` was given form `form` by the room (the copy that chose it); NULL forgets it. */
void khdays_characters_set_seat_form(int seat, const KhdaysCharacter *form);

/* Every seat's form forgotten (the title). */
void khdays_characters_forms_clear(void);

/* Each frame in the field: the rules the field answers (hp, plugins) asked again for the seats that have them; a
 * result on the same base is drawn at once, another kept for the next camp (told). */
void khdays_characters_forms_frame(void);

/* A native plugin's condition (`when = plugin <name>`), asked with the seat and its character's number; 0 when the
 * name is taken. Taken out with its plugin's `owner`. */
typedef int (*KhdaysFormCondition)(int seat, int number, void *user);
int khdays_characters_add_condition(const char *name, KhdaysFormCondition test, void *user, const void *owner);
void khdays_characters_remove_conditions(const void *owner);

/* A plugin's form for seat `seat` whatever its rules say, until NULL (`form` one of the seat's character's). */
void khdays_characters_force_form(int seat, const KhdaysCharacter *form);

/* Called as a seat's form changes (the plugins' `form` listeners). */
typedef void (*KhdaysFormChanged)(int seat, const KhdaysCharacter *form);
void khdays_characters_on_form(KhdaysFormChanged changed);

#ifdef __cplusplus
}
#endif

#endif
