#include "camp_look.h"

#include "../app/places.h"
#include "ui_text.h"
#include "../../runtime/core/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void khdays_camp_colour(const char *line, const char *key, Uint8 out[3])
{
    char format[64];
    int r, g, b;
    snprintf(format, sizeof(format), "%s %%d %%d %%d", key);
    if (SDL_sscanf(line, format, &r, &g, &b) == 3) {
        out[0] = (Uint8)r, out[1] = (Uint8)g, out[2] = (Uint8)b;
    }
}

/* Calls `line` with each of the file's lines (changed in place). */
int khdays_camp_each_line(const char *path, void (*line)(char *text, const char *folder, void *ctx),
                          const char *folder, void *ctx)
{
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(path, &size), *next;
    if (text == NULL) {
        return 0;
    }
    for (char *l = text; l != NULL && *l != '\0'; l = next) {
        next = strchr(l, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        line(l, folder, ctx);
    }
    SDL_free(text);
    return 1;
}

int khdays_camp_open_file(int language, char *path, int size, char *folder, int folder_size)
{
    char relative[96], *slash;
    if (language < 1 || language >= LANGUAGES) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "layouts/camp/%s/camp.txt", CODES[language]);
    if (!khdays_mods_find(relative, path, size)) {
        return 0;
    }
    snprintf(folder, (size_t)folder_size, "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    return 1;
}

void khdays_camp_value_text(const KhdaysCampView *v, int field, char *out, size_t size)
{
    const KhdaysCampField *f = &v->field[field];
    out[0] = '\0';
    if (!f->has) {
        return;
    }
    if (field == 0xf && (int)f->value[0] < 0) {
        snprintf(out, size, "--"); /* the game's for no next level (data_ov008_02090814) */
    } else if (field == 0x11) {
        snprintf(out, size, "%u/%u", f->value[0], f->value[1]);
    } else {
        snprintf(out, size, "%u", f->value[0]);
    }
}

/* A line's words (UTF-8) kept as UTF-16. */
void khdays_camp_words(unsigned short **slot, const char *text)
{
    const size_t room = strlen(text) + 1;
    SDL_free(*slot);
    *slot = (unsigned short *)SDL_malloc(room * sizeof(unsigned short));
    if (*slot != NULL) {
        khdays_layout_utf8(text, *slot, (int)room);
    }
}

/* A style's capitals' height. */
float khdays_camp_capitals(KhdaysCampLook *s, const char *style)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s->layout, style);
    return st != NULL ? (s->layout.ink_bottom - s->layout.ink_top) * st->size : 0;
}

/* The story's day on its tag at the model's box's top right (the game's "DAY %d", its day). */
void khdays_camp_draw_day(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const unsigned short *format = khdays_layout_string(&s->info, INFO_DAY);
    const float *tag = khdays_camp_place(s, "day_tag");
    unsigned short line[64];
    char digits[16];
    int n = 0;
    float w;
    if (format == NULL || tag[2] <= 0) {
        return;
    }
    snprintf(digits, sizeof(digits), "%d", v->day);
    for (int i = 0; format[i] != 0 && n < 62; ++i) {
        if (format[i] == '%' && format[i + 1] == 'd') {
            for (const char *d = digits; *d != 0 && n < 62; ++d) {
                line[n++] = (unsigned short)*d;
            }
            ++i;
        } else {
            line[n++] = format[i];
        }
    }
    line[n] = 0;
    w = khdays_camp_width_in(s, "day", line) + 2 * tag[3];
    khdays_layout_fill_rounded(s->layout.renderer, fr, tag[0] - w, tag[1], w, tag[2], tag[2] / 2, s->day_colour, 1.0f);
    khdays_camp_text(s, fr, "day", line, tag[0] - tag[3], tag[1] + tag[2] / 2 - khdays_camp_capitals(s, "day") / 2);
}

void khdays_camp_draw_members(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    const float *plate = khdays_camp_place(s, "member"), *ring = khdays_camp_place(s, "member_circle"),
        *small = khdays_camp_place(s, "member_face");
    for (int i = 0; i < KHDAYS_CAMP_PLACES; ++i) {
        const KhdaysCampMember *m = &v->member[i];
        const int ready = m->page == KHDAYS_CAMP_READY;
        const Uint8 *own = KHDAYS_PLACE_COLOURS[i];
        /* each place where the DS has it: its own (a place gone leaves its room) */
        const float x = plate[0] + (float)i * s->member_pitch, y = plate[1];
        char label[16];
        unsigned short wide[64];
        if (!m->present) {
            continue;
        }
        khdays_layout_fill_rounded(s->layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2, BLACK, PLATE_ALPHA);
        khdays_layout_ring_rounded(s->layout.renderer, fr, x, y, plate[2], plate[3], plate[3] / 2,
                                   ready ? PLATE_READY : PLATE_EDGE, ready ? own : EDGE_GREY, 1.0f);
        khdays_layout_fill_oval(s->layout.renderer, fr, x + ring[0], y + ring[1], ring[2] + RING, ring[2] + RING, WHITE,
                                1.0f);
        khdays_layout_fill_oval(s->layout.renderer, fr, x + ring[0], y + ring[1], ring[2], ring[2], own, 1.0f);
        snprintf(label, sizeof(label), khdays_text(TXT_ROOM_PLACE), i + 1);
        khdays_layout_utf8(label, wide, 16);
        khdays_camp_text(s, fr, "member_tag", wide, x + ring[0], y + ring[1]);
        if (m->character >= 0 && !khdays_mod_face_draw(&s->faces, &s->layout, fr, m->character, x + small[0],
                                                       y + small[1], small[2], small[3], 255)) {
            char name[16];
            snprintf(name, sizeof(name), "face%d", m->character);
            khdays_layout_draw_piece(&s->layout, fr, name, x + small[0], y + small[1], small[2], small[3], 1.0f);
        }
        {
            unsigned short mod_name[64];
            const unsigned short *named = khdays_mod_name(m->character, 0, v->language, mod_name, 64);
            khdays_camp_text(s, fr, "member_name",
                 m->name[0] != 0 ? (const unsigned short *)m->name
                 : named != NULL ? named
                                 : khdays_layout_string(&s->status, NAMES + (m->character >= 0 ? m->character : 0)),
                 x, y);
        }
        if (ready) {
            khdays_camp_prompt_part(s, wide, 64, 1);
            khdays_camp_text(s, fr, "member_ready", wide, x, y);
        } else if (m->page >= 1 && m->page <= KHDAYS_CAMP_OPTIONS - 1) {
            khdays_camp_text(s, fr, "member_place", s->option[m->page - 1], x, y);
        } else if (m->page == KHDAYS_CAMP_IN_MENU) {
            khdays_camp_text(s, fr, "member_place", s->menu_place, x, y);
        }
    }
}

/* Mission Mode's line over the menu, where the DS has it (its top band's right): the host choosing a
 * mission, START to join it or to begin, ... (camp.h `notice`). */
void khdays_camp_draw_notice(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    if (v->notice >= 0) {
        khdays_camp_text(s, fr, "notice", khdays_layout_string(&s->root, v->notice), 0, 0);
    }
}

/* The trials' label, symbols and counts in one row, centred in the trials' bar both ways; the gaps the
 * layout's (place trial, trials_value). */
void khdays_camp_draw_trials(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const KhdaysCampView *v)
{
    static const char *const symbols[2] = {"sigil", "crown"};
    const float *sym = khdays_camp_place(s, "trial");
    const KhdaysLayoutText *count_style = khdays_layout_text(&s->layout, "trials_value");
    const unsigned short *label = khdays_layout_string(&s->status, LABEL_TRIALS);
    const float h = sym[3], mid = s->trials[1] + s->trials[3] / 2, gap = count_style != NULL ? count_style->x : 0;
    unsigned short counts[2][16];
    float sym_w[2], total = khdays_camp_width_in(s, "trials_label", label), x;
    for (int i = 0; i < 2; ++i) {
        const KhdaysLayoutPiece *p = khdays_layout_piece(&s->layout, symbols[i]);
        char value[16];
        sym_w[i] = p != NULL ? p->w * h / p->h : 0;
        snprintf(value, sizeof(value), "%u", v->field[0x12].has ? v->field[0x12].value[i] : 0u);
        khdays_layout_utf8(v->field[0x12].has ? value : "", counts[i], 16);
        total += sym[2] + sym_w[i] + gap + khdays_camp_width_in(s, "trials_value", counts[i]);
    }
    x = s->trials[0] + (s->trials[2] - total) / 2;
    khdays_camp_text_at(s, fr, "trials_label", label, x, mid);
    x += khdays_camp_width_in(s, "trials_label", label);
    for (int i = 0; i < 2; ++i) {
        x += sym[2];
        khdays_layout_draw_piece(&s->layout, fr, symbols[i], x, mid - h / 2, sym_w[i], h, 1.0f);
        x += sym_w[i] + gap;
        khdays_camp_text_at(s, fr, "trials_value", counts[i], x, mid);
        x += khdays_camp_width_in(s, "trials_value", counts[i]);
    }
}

/* The notice's style (Mission Mode's line over the menu): the file's, or for a file made before it
 * tools/hd/ddd_camp.py's -- the prompt's size, right-aligned in the top band before the settings
 * key. */
void khdays_camp_notice_style(KhdaysCampLook *s)
{
    const KhdaysLayoutText *prompt = khdays_layout_text(&s->layout, "prompt");
    char line[128];
    if (khdays_layout_text(&s->layout, "notice") != NULL || prompt == NULL) {
        return;
    }
    snprintf(line, sizeof(line), "text notice 1700 %.1f %.4f right 255", prompt->y - (1016.0f - 46.0f), prompt->size);
    khdays_layout_line(&s->layout, line, "");
}

void khdays_camp_number(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const char *digits,
                        float dx, float dy)
{
    unsigned short wide[48];
    khdays_layout_utf8(digits, wide, 48);
    khdays_camp_text(s, fr, style, wide, dx, dy);
}

const float *khdays_camp_place(KhdaysCampLook *s, const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&s->layout, name);
    return p != NULL ? &p->x : none;
}

/* The game's A prompt (mlt_&.s record 63: A's icon and its word, then B's): up to B's icon, the
 * spaces round it left out; with word_alone without A's icon (the word for accepting). */
void khdays_camp_prompt_part(KhdaysCampLook *s, unsigned short *out, int room, int word_alone)
{
    const unsigned short *t = khdays_layout_string(&s->mlt, PROMPT);
    int n = 0, at = 0;
    out[0] = 0;
    if (t == NULL) {
        return;
    }
    if (word_alone) {
        while (t[at] != 0 && (t[at] == GLYPH_A || t[at] == ' ')) {
            ++at;
        }
    }
    while (t[at] != 0 && t[at] != GLYPH_B && n < room - 1) {
        out[n++] = t[at++];
    }
    while (n > 0 && out[n - 1] == ' ') {
        --n;
    }
    out[n] = 0;
}

/* A setting's words: its cfg_&.s record, or those of a value the port adds (camp.h KHDAYS_CAMP_WORD_*) in the
 * game's language, made in `own`. */
const unsigned short *khdays_camp_setting_words(KhdaysCampLook *s, int record, int language, unsigned short *own,
                                                int room)
{
    const int id = record == KHDAYS_CAMP_WORD_FREE        ? TXT_CHASE_FREE
                   : record == KHDAYS_CAMP_WORD_FREE_HELP ? TXT_CHASE_FREE_HELP
                                                          : -1;
    if (id >= 0) {
        const char *t = khdays_text_in(id, language);
        khdays_layout_utf8(t != NULL ? t : khdays_text(id), own, room);
        return own;
    }
    return khdays_layout_string(&s->cfg, record);
}

void khdays_camp_text(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t,
                      float dx, float dy)
{
    khdays_layout_draw_text(&s->layout, fr, style, t, dx, dy, 1.0f);
}

/* A line in a style with its left at x and its capitals' middle at y (the style's own place left
 * aside). */
void khdays_camp_text_at(KhdaysCampLook *s, const KhdaysLayoutFrame *fr, const char *style, const unsigned short *t,
                         float x, float y)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s->layout, style);
    if (st != NULL) {
        khdays_camp_text(s, fr, style, t, x - st->x, y - khdays_camp_capitals(s, style) / 2 - st->y);
    }
}

/* A line's width in a text style. */
float khdays_camp_width_in(KhdaysCampLook *s, const char *style, const unsigned short *t)
{
    const KhdaysLayoutText *st = khdays_layout_text(&s->layout, style);
    return st != NULL && t != NULL ? khdays_layout_line_width(&s->layout, t, st->size) : 0;
}

void khdays_camp_screen_line(char *line, const char *folder, void *ctx)
{
    float a, b, c, d, e;
    int n, at;
    KhdaysCampLook *s = (KhdaysCampLook *)ctx;
    if (khdays_layout_line(&s->layout, line, folder)) {
        return;
    }
    if (SDL_sscanf(line, "bars %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        s->bars[0] = a, s->bars[1] = b, s->bars[2] = c, s->bars[3] = d, s->bars[4] = e;
    } else if (SDL_sscanf(line, "panel %f %f %f %f", &a, &b, &c, &d) == 4) {
        s->panel[0] = a, s->panel[1] = b, s->panel[2] = c, s->panel[3] = d;
    } else if (SDL_sscanf(line, "trials %f %f %f %f", &a, &b, &c, &d) == 4) {
        s->trials[0] = a, s->trials[1] = b, s->trials[2] = c, s->trials[3] = d;
    } else if (strncmp(line, "menu_place ", 11) == 0) {
        khdays_camp_words(&s->menu_place, line + 11);
    } else if (strncmp(line, "mode_label ", 11) == 0) {
        khdays_camp_words(&s->mode_label, line + 11);
    } else if (strncmp(line, "mission_mode ", 13) == 0) {
        khdays_camp_words(&s->mission_mode, line + 13);
    } else if (SDL_sscanf(line, "difficulty %d %n", &n, &at) == 1 && n >= 0 && n < 3) {
        khdays_camp_words(&s->difficulty[n], line + at);
    } else if (SDL_sscanf(line, "description_pitch %f", &a) == 1) {
        s->description_pitch = a;
    } else if (SDL_sscanf(line, "detail_pitch %f", &a) == 1) {
        s->detail_pitch = a;
    } else if (SDL_sscanf(line, "page_tabs %f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        s->page_tabs[0] = a, s->page_tabs[1] = b, s->page_tabs[2] = c, s->page_tabs[3] = d, s->page_tabs[4] = e;
    } else if (SDL_sscanf(line, "page_rows %f %f %f %f %f %d", &a, &b, &c, &d, &e, &n) == 6 && n > 0) {
        s->page_rows[0] = a, s->page_rows[1] = b, s->page_rows[2] = c, s->page_rows[3] = d, s->page_rows[4] = e;
        s->page_rows[5] = (float)n;
    } else if (SDL_sscanf(line, "digit_overlap %f", &a) == 1) {
        s->overlap = a;
    } else if (SDL_sscanf(line, "members %f", &a) == 1) {
        s->member_pitch = a;
    } else if ((SDL_sscanf(line, "option %d %n", &n, &at) == 1 ||
                SDL_sscanf(line, "story_option %d %n", &n, &at) == 1 ||
                SDL_sscanf(line, "story_sub %d %n", &n, &at) == 1) &&
               n >= 0 && n < (strncmp(line, "story_sub", 9) == 0 ? 4 : KHDAYS_CAMP_OPTIONS)) {
        unsigned short **slot = strncmp(line, "story_sub", 9) == 0 ? &s->story_sub[n]
                                : line[0] == 's'                   ? &s->story_option[n]
                                                                   : &s->option[n];
        const size_t room = strlen(line + at) + 1;
        SDL_free(*slot);
        *slot = (unsigned short *)SDL_malloc(room * sizeof(unsigned short));
        if (*slot != NULL) {
            khdays_layout_utf8(line + at, *slot, (int)room);
        }
    } else {
        khdays_camp_colour(line, "munny_colour", s->munny_colour);
        khdays_camp_colour(line, "hearts_colour", s->hearts_colour);
        khdays_camp_colour(line, "day_colour", s->day_colour);
    }
}
