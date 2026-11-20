/* What both camp screens (camp_screen.c, Mission Mode's; overhaul_camp_screen.c, the Menu Overhaul's) keep and
 * draw alike: the state read from a camp layout (layouts/camp/<lang>/camp.txt), its constants, and the functions that
 * read and draw it the same in both. */
#ifndef KHDAYS_HOST_CAMP_LOOK_H
#define KHDAYS_HOST_CAMP_LOOK_H

#include "layout_screen.h"
#include "mod_faces.h"
#include "../../runtime/menus/camp.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

#define NAMES 21 /* status_&.s: the characters' names, in the game's order */
#define LABEL_TRIALS 13
#define PROMPT 63  /* mlt_&.s: A's prompt, then B's (the menu page has no B: cut there) */
#define INFO_DAY 0 /* UI/btl/&/info.s: "DAY %d" */
#define GLYPH_A 0x3349
#define GLYPH_B 0x3314
#define LANGUAGES 6
#define RING 3.0f /* the white ring around the place's circle */
#define PLATE_ALPHA 0.6f /* a member's plate (the character select's) */
#define PLATE_EDGE 2.0f
#define PLATE_READY 4.0f
static const Uint8 BLACK[3] = {0, 0, 0}, EDGE_GREY[3] = {110, 110, 110};
static const Uint8 WHITE[3] = {255, 255, 255};
static const char *const CODES[LANGUAGES] = {"", "en", "fr", "de", "it", "es"};

typedef struct KhdaysCampLook {
    int tried, ok;
    KhdaysLayout layout;
    KhdaysLayoutStrings status, root, mlt, info, sav, cfg;
    SDL_Texture *backdrop; /* the plan's, drawn around the model's window */
    float window[4];
    unsigned short *option[KHDAYS_CAMP_OPTIONS], *story_option[KHDAYS_CAMP_OPTIONS], *story_sub[4];
    unsigned short *menu_place, *mode_label, *mission_mode, *difficulty[3];
    float bars[5], panel[4], trials[4];
    float description_pitch, overlap, member_pitch;
    float page_tabs[5], page_rows[6], detail_pitch;
    int page_top; /* the first of the section's settings shown */
    Uint8 munny_colour[3], hearts_colour[3], day_colour[3];
    KhdaysModFaces faces; /* the mods' characters' */
} KhdaysCampLook;

void khdays_camp_colour(const char *line, const char *key, Uint8 out[3]);
int khdays_camp_each_line(const char *path, void (*line)(char *text, const char *folder, void *ctx),
                          const char *folder, void *ctx);
int khdays_camp_open_file(int language, char *path, int size, char *folder, int folder_size);
void khdays_camp_value_text(const KhdaysCampView *v, int field, char *out, size_t size);
void khdays_camp_words(unsigned short **slot, const char *text);
float khdays_camp_capitals(KhdaysCampLook *s, const char *style);
void khdays_camp_draw_day(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v);
void khdays_camp_draw_members(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v);
void khdays_camp_draw_notice(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v);
void khdays_camp_draw_trials(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v);
void khdays_camp_notice_style(KhdaysCampLook *s);
void khdays_camp_number(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const char *digits,
                        float dx, float dy);
const float *khdays_camp_place(KhdaysCampLook *s, const char *name);
void khdays_camp_prompt_part(KhdaysCampLook *s, unsigned short *out, int room, int word_alone);
const unsigned short *khdays_camp_setting_words(KhdaysCampLook *s, int record, int language, unsigned short *own,
                                                int room);
void khdays_camp_text(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t,
                      float dx, float dy);
void khdays_camp_text_at(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t,
                         float x, float y);
float khdays_camp_width_in(KhdaysCampLook *s, const char *style, const unsigned short *t);
void khdays_camp_screen_line(char *line, const char *folder, void *ctx);

#endif
