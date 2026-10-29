/* Independent HUD images keep their screen shape's priority and dimming without inheriting its crop. */
#include "onescreen_images.h"
#include "../../runtime/display/frame_package.h"
#include "../../ui/hud_options.h"

#include <stdlib.h>
#include <string.h>

typedef struct Picture {
    KhdaysShape shape;
    int owner;
} Picture;

static Picture *pictures;
static KhdaysShape *placed;
static size_t picture_capacity, placed_capacity;

int khdays_onescreen_information(const KhdaysFramePackage *p, KhdaysSituation *s)
{
    s->states &= ~(1u << KHDAYS_ST_INFORMATION_MOD);
    if (!(p->information_state & KHDAYS_INFORMATION_AVAILABLE))
        return 0;
    s->states |= 1u << KHDAYS_ST_INFORMATION_MOD;
    if ((p->hud_options & KHDAYS_HUD_KEEP_INFORMATION) &&
        (s->scene == KHDAYS_SIT_FIELD || s->scene == KHDAYS_SIT_FIELD_3D_BOTH) &&
        (s->states & (1u << KHDAYS_ST_INFO_UPPER)) && !(s->states & (1u << KHDAYS_ST_DIALOG)) && !p->situation.movie &&
        !p->situation.tutorial && khdays_situation_hud_mode() != KHDAYS_HUD_NONE)
        s->states |= 1u << KHDAYS_ST_HUD;
    /* INFO_UPPER controls the native HUD's placement as well as the banner crop. Its lifetime
     * must remain the game's, even while the independent sequence is entering or leaving. */
    s->states &= ~(1u << KHDAYS_ST_INFO_NEW);
    return 1;
}

static int information_shown(const KhdaysFramePackage *p, const KhdaysSituation *s, int engine)
{
    if (!(p->information_state & KHDAYS_INFORMATION_AVAILABLE) || !(p->information_state & (1u << (8 + engine))) ||
        (s->states & (1u << KHDAYS_ST_DIALOG)))
        return 0;
    const int field = s->scene == KHDAYS_SIT_FIELD || s->scene == KHDAYS_SIT_FIELD_3D_BOTH;
    const int pause = s->scene == KHDAYS_SIT_PAUSE && !(s->states & (1u << KHDAYS_ST_EVENT));
    if (!field && !pause)
        return 0;
    if (pause)
        return engine != p->upper || !(p->information_state & (1u << (9 - p->upper)));
    /* The physical upper banner takes priority during a mission introduction. No cropped
     * pixels, lower-panel detection or native text-layer opacity determines this image's life. */
    if (engine == p->upper)
        return 1;
    return !(p->information_state & (1u << (8 + p->upper))) &&
           (pause || ((s->states & (1u << KHDAYS_ST_HUD)) && khdays_situation_hud_mode() == KHDAYS_HUD_COMPLETE));
}

static void image_shape(KhdaysShape *shape, const uint32_t *r, uint32_t lower, float x, float y, float w, float h)
{
    uint32_t *dst = shape->words;
    memset(dst, 0, KO_WORDS * sizeof(*dst));
    dst[KO_FLAGS] = KOF_IMAGE | lower;
    dst[KO_KEYS] = r[KE_IMAGE];
    const float opacity = 1;
    memcpy(dst + KO_OPACITY, &opacity, 4);
    float values[4] = {0, 0, (float)r[KE_IW], (float)r[KE_IH]};
    memcpy(dst + KO_SRC_X, values, sizeof(values));
    values[0] = x;
    values[1] = y;
    values[2] = x + w;
    values[3] = y + h;
    memcpy(dst + KO_DST_X1, values, sizeof(values));
    const float sx = w / (float)r[KE_IW], sy = h / (float)r[KE_IH];
    memcpy(dst + KO_SCALE_X, &sx, 4);
    memcpy(dst + KO_SCALE_Y, &sy, 4);
}

static int native_information_crop(const uint32_t *w)
{
    float src[4];
    memcpy(src, w + KO_SRC_X, sizeof(src));
    return !(w[KO_FLAGS] & (KOF_IMAGE | KOF_3D)) && src[0] == 0 && src[1] == 0 && src[2] >= 224 && src[3] == 24;
}

static int hud_shown(const KhdaysSituation *s)
{
    return (s->scene == KHDAYS_SIT_FIELD || s->scene == KHDAYS_SIT_FIELD_3D_BOTH || s->scene == KHDAYS_SIT_TUTORIAL) &&
           (s->states & (1u << KHDAYS_ST_HUD));
}

void khdays_onescreen_images(const KhdaysFramePackage *p, const KhdaysSituation *situation,
                             KhdaysComposition *composition)
{
    size_t maximum = 0, count = 0, out = 0;
    int radar_owner = -1, pause_layout = 0;
    for (int e = 0; e < 2; ++e)
        maximum += p->elements[e].count;
    if (!maximum)
        return;
    if (maximum > picture_capacity) {
        Picture *grown = realloc(pictures, maximum * sizeof(*pictures));
        if (!grown)
            return;
        pictures = grown;
        picture_capacity = maximum;
    }
    const size_t need = maximum + (size_t)composition->shape_count;
    if (need > placed_capacity) {
        KhdaysShape *grown = realloc(placed, need * sizeof(*placed));
        if (!grown)
            return;
        placed = grown;
        placed_capacity = need;
    }
    for (int e = 0; e < 2; ++e) {
        for (uint32_t i = 0; i < p->elements[e].count; ++i) {
            const uint32_t *r = p->elements[e].records + i * KE_WORDS;
            float x, y, w, h;
            if (!(r[KE_SLOT] & KES_PICTURE) || ((r[KE_SLOT] & KES_HUD) && !hud_shown(situation)))
                continue;
            memcpy(&x, r + KE_X, 4);
            memcpy(&y, r + KE_Y, 4);
            memcpy(&w, r + KE_W, 4);
            memcpy(&h, r + KE_H, 4);
            if (r[KE_SLOT] & KES_PAUSE_LAYOUT) {
                if (situation->scene != KHDAYS_SIT_PAUSE || (situation->states & (1u << KHDAYS_ST_EVENT)))
                    continue;
                const float factor = khdays_composition_hud_factor();
                pause_layout = 1;
                const float columns = (float)khdays_composition_columns();
                Picture *picture = &pictures[count++];
                picture->owner = -1;
                image_shape(&picture->shape, r, 0, columns * .5f + (x - 128) * factor, 96 + (y - 96) * factor,
                            w * factor, h * factor);
                continue;
            }
            float anchor_x = (x + w < 256 ? x + w : 256) - 2;
            float anchor_y = (y + h < 192 ? y + h : 192) - 2;
            const unsigned *map = p->minimap_images[0].words;
            const int minimap = map[KSI_ANCHORED] && map[KSI_OUTPUT] == r[KE_IMAGE];
            if (minimap) {
                memcpy(&anchor_x, map + KSI_ANCHOR_X, 4);
                memcpy(&anchor_y, map + KSI_ANCHOR_Y, 4);
            }
            const unsigned *native = p->enemy_image.words;
            if (native[KSI_ANCHORED] && native[KSI_OUTPUT] == r[KE_IMAGE]) {
                memcpy(&anchor_x, native + KSI_ANCHOR_X, 4);
                memcpy(&anchor_y, native + KSI_ANCHOR_Y, 4);
            }
            int information = 0;
            for (int j = 0; j < 2; ++j) {
                native = p->information_images[j].words;
                if (native[KSI_ANCHORED] && native[KSI_OUTPUT] == r[KE_IMAGE]) {
                    information = 1;
                    memcpy(&anchor_x, native + KSI_ANCHOR_X, 4);
                    memcpy(&anchor_y, native + KSI_ANCHOR_Y, 4);
                }
            }
            const unsigned *panel = p->mission_panel_image.words;
            const int mission =
                (p->mission_panel_state & 1u) && panel[KSI_ANCHORED] && panel[KSI_OUTPUT] == r[KE_IMAGE];
            if (information || mission) {
                const float factor = khdays_composition_hud_factor(), scale = 0.766667f * factor;
                if (mission && panel[KSI_PANEL_ANCHOR] >= 1 && panel[KSI_PANEL_ANCHOR] <= 9) {
                    const unsigned anchor = panel[KSI_PANEL_ANCHOR] - 1;
                    const float ax = (float)(anchor % 3) * .5f, ay = (float)(anchor / 3) * .5f;
                    Picture *picture = &pictures[count++];
                    picture->owner = -1;
                    image_shape(&picture->shape, r, e != p->upper ? KOF_LOWER : 0,
                                (float)khdays_composition_columns() * ax + (x - 256 * ax) * scale,
                                192 * ay + (y - 192 * ay) * scale, w * scale, h * scale);
                    continue;
                }
                if (mission) {
                    float native_y;
                    memcpy(&native_y, panel + KSI_ANCHOR_Y, 4);
                    y -= native_y;
                }
                if (information && !information_shown(p, situation, e))
                    continue;
                /* Match the player/party gauge's composition scale, with one root for both native engines.
                 * The element already contains gauge.json's offsets and the entire animation. */
                Picture *picture = &pictures[count++];
                picture->owner = information ? -2 : -1;
                image_shape(&picture->shape, r, e != p->upper ? KOF_LOWER : 0, x * scale,
                            ((p->hud_options & KHDAYS_HUD_ALIGN_EDGES) ? 0 : 5 * factor) + y * scale, w * scale,
                            h * scale);
                continue;
            }
            const uint32_t lower = e != p->upper ? KOF_LOWER : 0;
            const unsigned *pickup = p->obtained_image.words;
            for (int notice = 0; notice < 3; ++notice) {
                const unsigned *candidate = p->notification_images[notice].words;
                if (candidate[KSI_ANCHORED] && candidate[KSI_OUTPUT] == r[KE_IMAGE])
                    pickup = candidate;
            }
            const int obtained = pickup[KSI_ANCHORED] && pickup[KSI_OUTPUT] == r[KE_IMAGE];
            if (obtained) {
                memcpy(&anchor_x, pickup + KSI_ANCHOR_X, 4);
                memcpy(&anchor_y, pickup + KSI_ANCHOR_Y, 4);
            }
            for (int s = 0; s < composition->shape_count; ++s) {
                const uint32_t *src = composition->shapes[s].words;
                float sx, sy, sw, sh, dx, dy, scale_x, scale_y;
                if ((src[KO_FLAGS] & (KOF_LOWER | KOF_3D | KOF_IMAGE)) != lower)
                    continue;
                memcpy(&sx, src + KO_SRC_X, 4);
                memcpy(&sy, src + KO_SRC_Y, 4);
                memcpy(&sw, src + KO_SRC_W, 4);
                memcpy(&sh, src + KO_SRC_H, 4);
                if (anchor_x < sx || anchor_x >= sx + sw || anchor_y < sy || anchor_y >= sy + sh)
                    continue;
                memcpy(&dx, src + KO_DST_X1, 4);
                memcpy(&dy, src + KO_DST_Y1, 4);
                memcpy(&scale_x, src + KO_SCALE_X, 4);
                memcpy(&scale_y, src + KO_SCALE_Y, 4);
                if (obtained) {
                    dx += (anchor_x - sx) * scale_x;
                    dy += (anchor_y - sy) * scale_y;
                    scale_x = scale_y = 0.766667f * khdays_composition_hud_factor();
                    dx += (x - anchor_x) * scale_x;
                    dy += (y - anchor_y) * scale_y;
                } else {
                    dx += (x - sx) * scale_x;
                    dy += (y - sy) * scale_y;
                }
                const float dw = w * scale_x, dh = h * scale_y;
                Picture *picture = &pictures[count++];
                picture->owner = s;
                image_shape(&picture->shape, r, lower, dx, dy, dw, dh);
                uint32_t *dst = picture->shape.words;
                const unsigned *radar = p->radar_image.words;
                const int native_map = minimap && radar[KSI_RADAR_MASK] && radar[KSI_CLEAR];
                if (native_map) {
                    uint32_t record[KE_WORDS];
                    memcpy(record, r, sizeof(record));
                    record[KE_IMAGE] = radar[KSI_OUTPUT];
                    record[KE_IW] = radar[KSI_WIDTH];
                    record[KE_IH] = radar[KSI_HEIGHT];
                    float ui_scale, offset_x, offset_y, right, top;
                    memcpy(&ui_scale, radar + KSI_RADAR_SCALE, 4);
                    memcpy(&offset_x, radar + KSI_ANCHOR_X, 4);
                    memcpy(&offset_y, radar + KSI_ANCHOR_Y, 4);
                    memcpy(&right, src + KO_DST_X2, 4);
                    memcpy(&top, src + KO_DST_Y1, 4);
                    const float factor = .766667f * khdays_composition_hud_factor();
                    const float side = 96 * ui_scale * factor;
                    image_shape(&picture->shape, record, lower, right - side + offset_x * factor,
                                top + offset_y * factor, side, side);
                    radar_owner = s;
                } else if (minimap) {
                    /* Keep the map crop and edge fade, without its white key or floor recolouring. */
                    const float left = x > sx ? x : sx, top = y > sy ? y : sy;
                    const float right = x + w < sx + sw ? x + w : sx + sw;
                    const float bottom = y + h < sy + sh ? y + h : sy + sh;
                    const float source[4] = {(left - x) * r[KE_IW] / w, (top - y) * r[KE_IH] / h,
                                             (right - left) * r[KE_IW] / w, (bottom - top) * r[KE_IH] / h};
                    const float rect[4] = {dx + (left - x) * scale_x, dy + (top - y) * scale_y,
                                           dx + (right - x) * scale_x, dy + (bottom - y) * scale_y};
                    memcpy(dst + KO_SRC_X, source, sizeof(source));
                    memcpy(dst + KO_DST_X1, rect, sizeof(rect));
                    dst[KO_FLAGS] |= src[KO_FLAGS] & KOF_WEIGHT;
                    float fade[4];
                    memcpy(fade, src + KO_FADE, sizeof(fade));
                    const float inset[4] = {(left - sx) * scale_x, (top - sy) * scale_y, (sx + sw - right) * scale_x,
                                            (sy + sh - bottom) * scale_y};
                    for (int edge = 0; edge < 4; ++edge)
                        fade[edge] = fade[edge] > inset[edge] ? fade[edge] - inset[edge] : 0;
                    memcpy(dst + KO_FADE, fade, sizeof(fade));
                }
                dst[KO_FLAGS] |= src[KO_FLAGS] & KOF_DIM;
                dst[KO_OPACITY] = src[KO_OPACITY];
                if (r[KE_SLOT] & (KES_HUD | KES_NATIVE_FADE)) {
                    const unsigned layer = r[KE_SLOT] & KES_NUMBER_MASK;
                    const unsigned blend = p->desc2d[e][KD_BLEND], coef = p->desc2d[e][KD_COEF];
                    float alpha = 1;
                    /* Ov002_CommitDisplayState fades BG3 against BG0/backdrop with EVA and 16-EVA. */
                    if (((blend >> 16) & 3u) == 1u && (blend & (1u << layer)) && (blend & 0x2100u))
                        alpha = (float)((coef & 31u) > 16u ? 16u : coef & 31u) / 16.0f;
                    dst[KO_FLAGS] |= KOF_NATIVE_FADE;
                    dst[KO_IMAGE_GROUP] = 1u + (unsigned)e * 4u + layer;
                    memcpy(dst + KO_IMAGE_ALPHA, &alpha, 4);
                }
                break;
            }
        }
    }
    if (!count)
        return;
    if (p->mission_panel_state & 1u)
        for (int s = 0; s < composition->shape_count; ++s)
            if (native_information_crop(composition->shapes[s].words))
                placed[out++] = composition->shapes[s];
    for (int priority = -2; priority < 0; ++priority)
        for (size_t i = 0; i < count; ++i)
            if (pictures[i].owner == priority)
                placed[out++] = pictures[i].shape;
    for (int s = 0; s < composition->shape_count; ++s) {
        for (size_t i = 0; i < count; ++i)
            if (pictures[i].owner == s)
                placed[out++] = pictures[i].shape;
        KhdaysShape shape = composition->shapes[s];
        float source[4];
        memcpy(source, shape.words + KO_SRC_X, sizeof(source));
        if (pause_layout && !(shape.words[KO_FLAGS] & (KOF_IMAGE | KOF_3D | KOF_LOWER)) && source[0] == 72 &&
            source[1] == 64 && source[2] == 112 && source[3] == 76)
            continue;
        if (radar_owner >= 0 && (s == radar_owner || (!(shape.words[KO_FLAGS] & (KOF_IMAGE | KOF_3D)) &&
                                                      (shape.words[KO_FLAGS] & KOF_LOWER) && source[0] == 186 &&
                                                      source[1] == 156 && source[2] == 2 && source[3] == 12)))
            continue;
        if ((p->mission_panel_state & 1u) && p->mission_panel_backdrop >= 0 &&
            !(shape.words[KO_FLAGS] & (KOF_3D | KOF_IMAGE)) && (shape.words[KO_FLAGS] & KOF_LOWER) &&
            source[0] == 186 && source[1] == 156 && source[2] == 2 && source[3] == 12) {
            shape.words[KO_FLAGS] = (shape.words[KO_FLAGS] | KOF_IMAGE) & ~KOF_SEE_THROUGH;
            shape.words[KO_KEYS] = (unsigned)p->mission_panel_backdrop;
            const float rectangle[4] = {0, 0, 1, 1};
            memcpy(shape.words + KO_SRC_X, rectangle, sizeof(rectangle));
            float dx[4];
            memcpy(dx, shape.words + KO_DST_X1, sizeof(dx));
            const float sx = dx[2] - dx[0], sy = dx[3] - dx[1];
            memcpy(shape.words + KO_SCALE_X, &sx, 4);
            memcpy(shape.words + KO_SCALE_Y, &sy, 4);
        }
        if (!(p->mission_panel_state & 1u) || !native_information_crop(shape.words))
            placed[out++] = shape;
    }
    composition->shape_count = (int)out;
    composition->shapes = placed;
}
