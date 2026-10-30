/* Kingdom Hearts II's 2D sequences played over the window (kh2_seq.h): read and evaluated by the KH2 HUD's
 * reader (assets/kh2_sequence.h), drawn here with the window's renderer. */
extern "C" {
#include "kh2_seq.h"
#include "../render/png_io.h"
#include "../../runtime/core/rom.h"
}

#include "khdays/assets/kh2_sequence.h"

#include <algorithm>
#include <exception>

namespace kh2 = khdays::assets::kh2;

namespace {

const kh2::SequenceData *data_of(const KhdaysKh2Seq *seq)
{
    return static_cast<const kh2::SequenceData *>(seq->data);
}

std::uint32_t as_is(std::uint32_t c)
{
    return c;
}

float channel(std::uint32_t c, int k)
{
    return float(c >> (8 * k) & 0xFFu) / 128.0f; /* 0x80 is 1.0 */
}

int loop_frame(int min, int max, int value)
{
    if (value < max)
        return value;
    if (max <= min)
        return min;
    return min + (value - min) % (max - min);
}

void draw_quad(const KhdaysKh2Stage *st, const KhdaysKh2Seq *seq, const kh2::SequenceQuad &q)
{
    /* both triangles turning the same way whatever the rectangle's flips (KH2 mirrors a sprite by swapping its
     * edges; some renderers draw only one turning, or draw the other wrongly) */
    static const int order[2][6] = {{0, 1, 2, 1, 3, 2}, {0, 2, 1, 1, 2, 3}};
    const auto &c = q.vertices;
    /* the reader places a texel's centre; the sheet is sampled from its texels' edges, a rectangle of no size
     * (the sheet's flat white) at its one texel's centre */
    const float du = c[0].u == c[1].u ? 0.0f : 0.5f, dv = c[0].v == c[2].v ? 0.0f : 0.5f;
    SDL_Vertex v[4];
    int bright = 0;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k)
            bright |= channel(c[i].color, k) > 1.0f;
    for (int i = 0; i < 4; ++i) {
        v[i].position.x = st->left + (c[i].x - 0.5f) * st->scale;
        v[i].position.y = st->top + (c[i].y - 0.5f) * st->scale;
        v[i].tex_coord.x = (c[i].u - du) / seq->sheet_units;
        v[i].tex_coord.y = (c[i].v - dv) / seq->sheet_units;
        /* a PS2 tint goes to twice the texel, which a vertex colour cannot: past 1.0 the doubled sheet, half
         * the tint */
        v[i].color.r = std::min(bright ? channel(c[i].color, 0) / 2.0f : channel(c[i].color, 0), 1.0f);
        v[i].color.g = std::min(bright ? channel(c[i].color, 1) / 2.0f : channel(c[i].color, 1), 1.0f);
        v[i].color.b = std::min(bright ? channel(c[i].color, 2) / 2.0f : channel(c[i].color, 2), 1.0f);
        v[i].color.a = std::min(channel(c[i].color, 3), 1.0f) * st->alpha;
    }
    SDL_Texture *sheet = bright && st->bright != nullptr ? st->bright : st->sheet;
    SDL_SetTextureBlendMode(sheet, q.blend == 1 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    const float turn = (v[1].position.x - v[0].position.x) * (v[2].position.y - v[0].position.y) -
                       (v[1].position.y - v[0].position.y) * (v[2].position.x - v[0].position.x);
    SDL_RenderGeometry(st->renderer, sheet, v, 4, order[turn < 0.0f], 6);
}

} // namespace

extern "C" int khdays_kh2_seq_load(KhdaysKh2Seq *seq, const char *relative, float sheet_units,
                                   uint32_t (*recolour)(uint32_t))
{
    char path[1024];
    size_t size = 0;
    void *bytes;
    *seq = KhdaysKh2Seq{};
    if (!khdays_mods_find(relative, path, sizeof(path)) || (bytes = SDL_LoadFile(path, &size)) == nullptr)
        return 0;
    try {
        auto *d = new kh2::SequenceData(kh2::read_sequence(static_cast<const std::uint8_t *>(bytes), size));
        auto *paint = recolour != nullptr ? recolour : as_is;
        for (auto &part : d->parts)
            for (auto &c : part.colors)
                c = paint(c);
        for (auto &a : d->animations)
            for (auto &c : a.colors)
                c = paint(c);
        seq->data = d;
        seq->agroup_count = int(d->clips.size());
        seq->sheet_units = sheet_units;
    } catch (const std::exception &e) {
        SDL_Log("%s: not a sequence the port reads (%s)", path, e.what());
    }
    SDL_free(bytes);
    return seq->data != nullptr;
}

extern "C" void khdays_kh2_seq_free(KhdaysKh2Seq *seq)
{
    delete static_cast<kh2::SequenceData *>(seq->data);
    *seq = KhdaysKh2Seq{};
}

extern "C" SDL_Texture *khdays_kh2_picture(SDL_Renderer *renderer, const char *relative, int bright)
{
    char path[1024];
    int w, h;
    const char *error;
    SDL_Texture *result = nullptr;
    if (!khdays_mods_find(relative, path, sizeof(path)))
        return nullptr;
    std::uint8_t *px = khdays_png_read(path, &w, &h, &error);
    if (px == nullptr)
        return nullptr;
    if (bright) {
        for (size_t i = 0; i < size_t(w) * size_t(h) * 4u; ++i) {
            if (i % 4 != 3)
                px[i] = std::uint8_t(px[i] > 127 ? 255 : px[i] * 2);
        }
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    if (surface != nullptr) {
        result = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (result)
            SDL_SetTextureScaleMode(result, SDL_SCALEMODE_LINEAR);
    }
    khdays_png_free(px);
    return result;
}

extern "C" void khdays_kh2_stage(KhdaysKh2Stage *st, SDL_Renderer *renderer, SDL_Texture *sheet, SDL_Texture *bright,
                                 float scale, float left, float top)
{
    st->renderer = renderer;
    st->sheet = sheet;
    st->bright = bright;
    st->scale = scale;
    st->left = left;
    st->top = top;
    st->stretch = 1.0f;
    st->widen = st->widen_from = 0.0f;
    st->alpha = 1.0f;
}

extern "C" void khdays_kh2_seq_draw(const KhdaysKh2Stage *st, const KhdaysKh2Seq *seq, int index, int frame, float x,
                                    float y)
{
    const kh2::SequenceData *d = data_of(seq);
    if (d == nullptr || index < 0 || index >= seq->agroup_count || st->sheet == nullptr || st->alpha <= 0.002f)
        return;
    const kh2::SequenceClip &clip = d->clips[size_t(index)];
    int f = frame;
    if (clip.loop == 0) { /* looped: from its restart to its end (else the last animation's end) */
        int end = int(clip.end);
        if (end == 0)
            for (std::size_t i = clip.animations.first; i < std::size_t(clip.animations.first) + clip.animations.count;
                 ++i)
                end = std::max(end, d->animations[i].time[1]);
        f = loop_frame(int(clip.restart), end, frame);
    }
    kh2::SequencePose pose;
    pose.x = x;
    pose.y = y;
    pose.widen = st->widen;
    pose.widen_from = st->widen_from;
    pose.stretch_y = st->stretch;
    try {
        for (const kh2::SequenceQuad &q : kh2::evaluate_sequence(*d, size_t(index), float(f), pose).quads)
            draw_quad(st, seq, q);
    } catch (const std::exception &) {
        /* a sequence the reader does not draw (a palette chosen, a scroll): nothing of it */
    }
}
