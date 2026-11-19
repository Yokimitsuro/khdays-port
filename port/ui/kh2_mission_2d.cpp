// KINGDOM HEARTS II's mission pieces (docs/KH2_ENEMIES.md, "KH2's missions and signals"): the timer and counters of
// the KH2 mission a KH2 boss's battle runs (runtime/kh2_enemies: its file the KH2 enemies' mod's kh2/msn/<name>.bar),
// drawn from the mission's own sequences (each IMGD's next entry, a SEQD: Demyx's "ti_e" and "ct_h") and its HD
// pictures (kh2/msn/<name>/<LANGUAGE>_<name>_bar<N>.png, the Nth IMGD's: KH2 PC's, laid out as its sequences have
// them; without one the IMGD's own, laid out otherwise) as KH2's dk::TIMER, dk::COUNTER and dk::MISSION_GAUGE build
// them (the PS3 build's, read): sprites of the sequence's clips -- the base (3
// coming, then 4; 5 leaving), its second piece (9, then 10; 11 leaving), the digits (12-21 by digit; 22 a counter's dim
// leading zero; 23 and 24 the timer's separators, a counter's slash; 24-33 and 34 its most's digits) -- each child
// placed at its base's active point plus its offset and faded with the base's active alpha. The timer shows minutes,
// seconds and hundredths (dk::Clock::vCountToHMSMS) and counts its last three seconds in large figures (clips 31, 30,
// 29, then 28 at 0, in the middle of the screen). A gauge (Saïx's berserk) fills its bar by its level over its most:
// 12 its full part scaled across by it, 13 its empty part by what is left, 14 its tip moved that share of the bar's
// length (14's ex) along. The pieces stand in the columns the mission names as KH2's INFO_BASE
// stacks them (each below the open ones before it by its base's height), half a DS dot a KH2 dot, under Days's own
// pieces at the top of that side -- one KH2 PC's wide screen brings to the middle (Luxord's time gauges) in the
// middle at the top; drawn on the CPU into HUD images.
#include "kh2_mission_2d.h"

#include "../gfx/kh2_sequence.h"
#include "../runtime/kh2_enemies/shell.h"
#include "character_portraits.h"
#include "kh2_font.h"
#include "kh2_portrait.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../runtime/characters/characters.h"
#include "../runtime/characters/forms.h"
#include "../runtime/core/session_log.h"
#include "../runtime/scripts/game_link.h"
int khdays_settings_game_language(void);
}

#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_sequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace kh2e = khdays::kh2_enemies;

constexpr float kDot = 0.5F; // a KH2 dot in DS pixels: KH2's 512-dot screen the DS's 256
// KH2's INFO_BASE columns, ALIGN 0 left, 1 right, 2 the middle: a piece's x its column's clip's active point (a timer's
// 26, 27, 25; a counter's 36, 37, 35), its y below the open pieces before it in its column. KH2 moves its side columns
// 85 dots out on a wide screen; the port's layout moves a piece on a side out as far as its screen widens
constexpr int kTimerColumns[3] = {26, 27, 25}, kCounterColumns[3] = {36, 37, 35}, kGaugeColumns[3] = {16, 17, 15};
// A side column's move on KH2 PC's wide screen (KH2's dots: the right one's out to the right, the left one's to the
// left) and KH2's screen's middle. A piece whose column's point has that move taken off already (Luxord's two time
// gauges, EH14_MS103: both in the right column at -85, Sora's half ending at the middle and Luxord's starting there,
// "SORA [TIME GAUGE VS] LUXORD" whole) stands in the middle of the screen there: such a piece, whose base meets the
// middle once moved, is laid out by the middle of the screen (its halves kept together), not by its column's side
constexpr float kWideColumnMove = 85.0F, kKh2Middle = 256.0F;

// The player's half of a two-sided piece, which draws KH2's own player (Luxord's time gauges, EH14_MS103's gauge 0:
// Sora's face, its sequence's part 0, and "SORA" written on its bar, part 1), shows the player playing: his portrait
// by his HP gauge (character_portraits.h), turned to face the middle, in the face's place, and his character's name
// over the bar's, the bar's plain stretch beside it laid over that first -- where the name and the plain stretch lie
// in the bar's part our measures of its HD picture, in its IMGD's dots
struct PlayerSide final {
    const char *mission;
    int kind, index;
    std::size_t face_part, bar_part;
    std::array<float, 4> name, plain; // left, top, right, bottom
};
constexpr PlayerSide kPlayerSides[] = {{"EH14_MS103", 6, 0, 0, 1, {80, 46, 131, 61}, {20, 46, 71, 61}}};

// Laid over a layer's quads once drawn (a player's half's portrait and name), KH2's dots: a picture (straight RGBA)
// fitted in its box, its shape kept, at the box's bottom -- turned left for right when `mirror` -- or a text in KH2's
// font from the box's left, as tall as `font` KH2 dots a font dot, in the middle of the box's height
struct Overlay final {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const std::vector<std::uint32_t> *texels = nullptr;
    int w = 0, h = 0;
    bool mirror = false;
    float alpha = 1;
    std::uint64_t key = 0;
    std::u16string text;
    float font = 1;
};

// The name of the character the player plays, in capitals as KH2's gauge writes its player's: his mod character's
// own, else the game's character's (its portrait's name, its hood left out)
std::u16string player_name()
{
    const int seat = khdays_link_hud_seat(0);
    const KhdaysCharacter *c = seat >= 0 ? khdays_characters_seat_form_now(seat) : nullptr;
    std::string utf8;
    if (c != nullptr)
        if (const char *n = khdays_characters_name(c, khdays_settings_game_language()))
            utf8 = n;
    if (utf8.empty()) {
        int icon = -1, hp = 0, most = 0, low = 0;
        if (khdays_link_hud_portrait(0, &icon, &hp, &most, &low) && icon >= 0 &&
            std::size_t(icon) < khdays::ui::kh2::portrait_names.size()) {
            utf8 = khdays::ui::kh2::portrait_names[std::size_t(icon)];
            if (const auto hood = utf8.find("_Hood"); hood != std::string::npos)
                utf8.erase(hood);
            if (utf8 == "Saix")
                utf8 = "Sa\xc3\xafx";
        }
    }
    std::u16string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        char32_t cp = b;
        std::size_t n = 1;
        if (b >= 0xf0 && i + 3 < utf8.size())
            cp = (b & 7u) << 18 | (utf8[i + 1] & 63u) << 12 | (utf8[i + 2] & 63u) << 6 | (utf8[i + 3] & 63u), n = 4;
        else if (b >= 0xe0 && i + 2 < utf8.size())
            cp = (b & 15u) << 12 | (utf8[i + 1] & 63u) << 6 | (utf8[i + 2] & 63u), n = 3;
        else if (b >= 0xc0 && i + 1 < utf8.size())
            cp = (b & 31u) << 6 | (utf8[i + 1] & 63u), n = 2;
        i += n;
        if ((cp >= u'a' && cp <= u'z') || (cp >= 0xe0 && cp <= 0xfe && cp != 0xf7))
            cp -= 0x20;
        if (cp < 0x10000)
            out.push_back(char16_t(cp));
    }
    return out;
}
// Where a column's first piece starts, DS pixels down Days's upper screen: below Days's own pieces at its top, the rows
// above it as KH2's INFORMATION window is above its pieces -- the hearts and the clock at the left (to 37), the
// target's name and health at the right (to 24), the countdown and lock-on in the middle (to 20; ui/compositions/
// port_days.txt's regions of the field)
constexpr float kColumnTops[3] = {38.0F, 25.0F, 20.0F};

int column_of(const kh2e::MissionPiece &k)
{
    return std::clamp(k.align, 0, 2);
}

// A piece's picture and sequence
struct Pictures final {
    seq::SequenceData sequence;
    khdays::assets::DecodedTexture texture;
    std::array<int, 2> logical{};
};

// dk::Sprite: a clip of its piece's sequence playing; at its end the next (m_nextNum) or gone; a child placed at its
// base's active point plus its offset, faded with the base's active alpha (getParentSeqAlphaOnly)
struct Sprite final {
    int clip = -1, next = -1;
    seq::SequencePlayer player;
    int parent = -1;      // its base's index in its piece's sprites, -1 none
    float x = 0, y = 0;   // a base's place (setPos), KH2's dots
    float ox = 0, oy = 0; // its offset (setOffsetPos)
    bool alive = false;
    int layer = 0;        // its image: 0 the piece's box, 1 the timer's large countdown
    float scale_x = 1.0F; // its sequence's scale across (YI::SEQUENCE::SetScaleX: a gauge's parts)
    float age = 0.0F;     // KH2 ticks since it was made (YI::SPRITE's time: its parts' pictures' scroll)
};

// A layer's image: its place (DS pixels), only grown as its pictures reach further (each image of a new size is
// another of the port's textures, kept), drawn again only when its quads change (`drawn`: theirs, hashed)
struct Canvas final {
    int image = -1, width = 0, height = 0, scale = 0;
    float left = 0, top = 0;
    std::uint64_t drawn = 0;
    std::vector<std::uint32_t> pixels;
};

// FNV-1a over the quads' every vertex and blending, and the canvas they are drawn in
std::uint64_t hash_of(const std::vector<seq::SequenceQuad> &quads, const Canvas &c)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto add = [&h](const void *data, std::size_t size) {
        const auto *b = static_cast<const unsigned char *>(data);
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ b[i]) * 0x100000001b3ull;
    };
    for (const seq::SequenceQuad &q : quads) {
        for (const seq::SequenceVertex &v : q.vertices) {
            add(&v.x, sizeof v.x), add(&v.y, sizeof v.y), add(&v.u, sizeof v.u), add(&v.v, sizeof v.v);
            add(&v.color, sizeof v.color);
        }
        add(&q.blend, sizeof q.blend);
    }
    add(&c.left, sizeof c.left), add(&c.top, sizeof c.top), add(&c.width, sizeof c.width), add(&c.scale, sizeof c.scale);
    return h == 0 ? 1 : h;
}

struct Piece final {
    kh2e::MissionPiece kh2;
    std::shared_ptr<const Pictures> pictures; // none: not shown
    std::vector<Sprite> sprites;
    std::vector<int> digits, most_digits; // a timer's: minutes, seconds, hundredths (tens first); a counter's ones first;
                                          // a gauge's full part, empty part and tip
    float shown_level = -1.0F;            // the gauge's level drawn (dk::MISSION_GAUGE's + 0xaa0)
    int shown = std::numeric_limits<int>::min(), shown_most = std::numeric_limits<int>::min();
    unsigned countdown = 0; // the timer's large figures made (dk::TIMER's flag bits: 3, 2, 1, 0)
    bool closing = false;
    bool alerting = false; // its alert's clips playing (alert_update)
    float x = 0, y = 0; // its base's place in its column
    float top = 0;      // its highest dot below its place standing (its base's clip 4): its column's top in DS pixels
    bool middle = false; // laid out by the middle of the screen (kWideColumnMove): its x KH2 PC's, moved
    std::array<Canvas, 2> canvases;
};

std::string file;
unsigned serial = 0, ticks = 0;
std::vector<Piece> pieces;
bool visible = false;

int ex(const Pictures &p, int clip) // a clip's advance (its parameters' "ex": a digit's width)
{
    return p.sequence.clips.at(std::size_t(clip)).parameters[4];
}

int cr(const Pictures &p, int clip) // a clip's height (its parameters' "cr": the base's row in the column)
{
    return p.sequence.clips.at(std::size_t(clip)).parameters[3];
}

void set(Piece &p, std::size_t i, int clip) // dk::Sprite::setNum
{
    Sprite &s = p.sprites.at(i);
    s.clip = clip;
    s.player.select(p.pictures->sequence, std::size_t(clip), true);
}

std::size_t make(Piece &p, int clip, int parent, float ox, float oy, int layer = 0) // dk::Sprite::create
{
    Sprite s;
    s.parent = parent;
    s.ox = ox;
    s.oy = oy;
    s.alive = true;
    s.layer = layer;
    p.sprites.push_back(std::move(s));
    set(p, p.sprites.size() - 1, clip);
    return p.sprites.size() - 1;
}

int figures_of(int most) // YS::MATH::GetFigure: a counter's digits, its most's
{
    int n = 1;
    for (long long v = std::max(0, most); v >= 10; v /= 10)
        ++n;
    return n;
}

// dk::TIMER::createBase and init: its base (3, then 4), its second (9, then 10), its digits in a row -- minutes,
// ':' (23), seconds, "'" (24), hundredths -- each the width (ex) of the one before it further
void open_timer(Piece &p)
{
    const Pictures &pc = *p.pictures;
    p.sprites.clear();
    p.digits.clear();
    make(p, 3, -1, 0, 0);
    p.sprites[0].next = 4;
    make(p, 9, 0, 0, 0);
    p.sprites[1].next = 10;
    float x = 0;
    for (int part = 0; part < 3; ++part) {
        for (int d = 0; d < 2; ++d) {
            p.digits.push_back(int(make(p, 12, 0, x, 0)));
            x += float(ex(pc, 12));
        }
        if (part < 2) {
            make(p, 23 + part, 0, x, 0);
            x += float(ex(pc, 23 + part));
        }
    }
    p.x = float(seq::evaluate_sequence(pc.sequence, kTimerColumns[column_of(p.kh2)], 0)
                    .active.value_or(seq::SequenceActive{})
                    .x);
}

// dk::COUNTER::create: its base (3, then 4), its second (9, then 10), its digits (its most's count of them) right to
// left from the ones, each a digit's width (ex) apart; shown "count / most": the slash (23) and its most's digits (24-)
void open_counter(Piece &p)
{
    const Pictures &pc = *p.pictures;
    p.sprites.clear();
    p.digits.clear();
    p.most_digits.clear();
    make(p, 3, -1, 0, 0);
    p.sprites[0].next = 4;
    make(p, 9, 0, 0, 0);
    p.sprites[1].next = 10;
    const int figures = figures_of(p.kh2.most);
    for (int i = 0; i < figures; ++i)
        p.digits.push_back(int(make(p, 12, 0, float(ex(pc, 12) * (figures - 1 - i)), 0)));
    if (p.kh2.with_most) {
        const float slash = float(ex(pc, 12) * figures);
        make(p, 23, 0, slash, 0);
        const float after = slash + float(ex(pc, 23));
        for (int i = 0; i < figures; ++i)
            p.most_digits.push_back(int(make(p, 24, 0, float(ex(pc, 24) * (figures - 1 - i)) + after, 0)));
    }
    p.x = float(seq::evaluate_sequence(pc.sequence, kCounterColumns[column_of(p.kh2)], 0)
                    .active.value_or(seq::SequenceActive{})
                    .x);
}

// dk::MISSION_GAUGE::create: its base (3, then 4), its second (9, then 10), its full part (12), its empty part (13)
// and its tip (14), placed in its column (16, 17, 15 by its side)
void open_gauge(Piece &p)
{
    const Pictures &pc = *p.pictures;
    p.sprites.clear();
    p.digits.clear();
    make(p, 3, -1, 0, 0);
    p.sprites[0].next = 4;
    make(p, 9, 0, 0, 0);
    p.sprites[1].next = 10;
    for (const int clip : {12, 13, 14})
        p.digits.push_back(int(make(p, clip, 0, 0, 0)));
    p.shown_level = -1.0F;
    p.x = float(seq::evaluate_sequence(pc.sequence, kGaugeColumns[column_of(p.kh2)], 0)
                    .active.value_or(seq::SequenceActive{})
                    .x);
}

void open(Piece &p)
{
    p.alerting = false; // (made anew: an alert on plays again)
    // its top standing: its base's (4), its second's (10, its last frame: a gauge's opens with a flash far above it)
    // and a digit's or its bar's (12) at the base's active point
    const seq::SequenceData &d = p.pictures->sequence;
    const auto base = seq::evaluate_sequence(d, 4, 0);
    const seq::SequenceActive a = base.active.value_or(seq::SequenceActive{});
    seq::SequencePose at;
    at.x = float(a.x);
    at.y = float(a.y);
    at.scroll_time = 0; // (a gauge's bar scrolls its picture: its first frame's)
    at.image_size = {float(p.pictures->logical[0]), float(p.pictures->logical[1])};
    p.top = std::numeric_limits<float>::max();
    const float second_last = std::max(0.0F, float(seq::sequence_frame_count(d, 10)) - 1.0F);
    for (const auto &f : {base, seq::evaluate_sequence(d, 10, second_last, at), seq::evaluate_sequence(d, 12, 0, at)})
        for (const seq::SequenceQuad &q : f.quads)
            for (const seq::SequenceVertex &v : q.vertices)
                p.top = std::min(p.top, v.y);
    if (p.top == std::numeric_limits<float>::max())
        p.top = 0;
    p.shown = p.shown_most = std::numeric_limits<int>::min();
    p.countdown = 0;
    p.closing = false;
    if (p.kh2.kind == 4)
        open_timer(p);
    else if (p.kh2.kind == 6)
        open_gauge(p);
    else
        open_counter(p);
    // a side column's piece whose base, moved as KH2 PC moves its column, meets the middle of KH2's screen: laid out
    // by the middle of the screen, at KH2 PC's place (kWideColumnMove)
    p.middle = false;
    const int column = column_of(p.kh2);
    if (column != 2) {
        const float moved = p.x + (column == 1 ? kWideColumnMove : -kWideColumnMove);
        float left = std::numeric_limits<float>::max(), right = -left;
        for (const seq::SequenceQuad &q : base.quads)
            for (const seq::SequenceVertex &v : q.vertices) {
                left = std::min(left, v.x + moved);
                right = std::max(right, v.x + moved);
            }
        if (left <= kKh2Middle + 1.0F && right >= kKh2Middle - 1.0F) {
            p.middle = true;
            p.x = moved;
        }
    }
}

// dk::TIMER::close, dk::COUNTER::close: the base 5, its second 11, then gone (its children with it)
void close(Piece &p)
{
    if (p.sprites.size() < 2 || !p.sprites[0].alive)
        return;
    set(p, 0, 5);
    set(p, 1, 11);
    p.sprites[0].next = p.sprites[1].next = -1;
    p.closing = true;
}

// A counter's digits for `value` (dk::COUNTER::update: getSeqNumber's table, a digit d its clip `first` + d), its
// leading zeros past the ones its dim zero (`zero`)
void count_digits(Piece &p, const std::vector<int> &digits, int value, int first, int zero)
{
    long long v = std::max(0, value);
    std::vector<int> d(digits.size());
    for (std::size_t i = 0; i < digits.size(); ++i, v /= 10)
        d[i] = int(v % 10);
    for (std::size_t i = 0; i < digits.size(); ++i)
        set(p, std::size_t(digits[i]), first + d[i]);
    for (std::size_t i = digits.size(); i-- > 1 && d[i] == 0;)
        set(p, std::size_t(digits[i]), zero);
}

// dk::TIMER::update: its time (KH2's frames) as minutes, seconds and hundredths (dk::Clock::vCountToHMSMS; a second
// more when its second piece's clip says so, ex 1), not past an hour; a timer counting down shows its last three
// seconds in large figures, once each (31 with two left, 30, 29, then 28 at its end)
void timer_update(Piece &p)
{
    const Pictures &pc = *p.pictures;
    int time = std::max(0, p.kh2.value);
    if (time / 216000 != 0)
        return;
    if (time != p.shown) {
        p.shown = time;
        int t = time;
        if (ex(pc, p.sprites[1].clip) == 1 && t != 0)
            t += 60;
        const int minutes = t % 216000 / 3600, seconds = t % 3600 / 60;
        const int hundredths = int(float(t % 60) * (5.0F / 3.0F));
        const int parts[3] = {minutes, seconds, hundredths};
        for (int k = 0; k < 3; ++k) {
            set(p, std::size_t(p.digits[std::size_t(2 * k)]), 12 + parts[k] / 10 % 10);
            set(p, std::size_t(p.digits[std::size_t(2 * k + 1)]), 12 + parts[k] % 10);
        }
    }
    if (!p.kh2.down || time >= 3600)
        return;
    const int seconds = time / 60;
    const auto large = [&](unsigned bit, int clip) {
        p.countdown |= bit;
        make(p, clip, -1, 0, 0, 1);
    };
    if (seconds == 2 && !(p.countdown & 1u))
        large(1u, 31);
    else if (seconds == 1 && !(p.countdown & 2u))
        large(2u, 30);
    else if (seconds == 0 && !(p.countdown & 4u))
        large(4u, 29);
    else if (time == 0 && !(p.countdown & 8u))
        large(8u, 28);
}

void counter_update(Piece &p)
{
    if (p.kh2.value != p.shown) {
        p.shown = p.kh2.value;
        count_digits(p, p.digits, p.kh2.value, 12, 22);
    }
    if (p.kh2.with_most && p.kh2.most != p.shown_most) {
        p.shown_most = p.kh2.most;
        count_digits(p, p.most_digits, p.kh2.most, 24, 34);
    }
}

// dk::MISSION_GAUGE::update and updateGauge: when its level changes, its full part scaled across by its level over
// its most, its empty part by the rest, its tip moved that share of the bar's length (its clip's ex) along
void gauge_update(Piece &p)
{
    if (p.kh2.level == p.shown_level || p.digits.size() < 3)
        return;
    p.shown_level = p.kh2.level;
    const float share = p.kh2.most > 0 ? p.kh2.level / float(p.kh2.most) : 0.0F;
    p.sprites[std::size_t(p.digits[0])].scale_x = share;
    p.sprites[std::size_t(p.digits[1])].scale_x = 1.0F - share;
    p.sprites[std::size_t(p.digits[2])].ox = float(int(share * float(ex(*p.pictures, 14))));
}

// dk::COUNTER::alert and dk::MISSION_GAUGE::alert, their releaseAlert (the PS3 build's): a counter's or a gauge's
// warning on, its base plays clip 0 and its second piece 6 -- a counter's then 1 and 7 (dk::COUNTER::alertLoop) --;
// off, 4 and 10 again, nothing after
void alert_update(Piece &p)
{
    if (p.kh2.alert == p.alerting || p.sprites.size() < 2)
        return;
    p.alerting = p.kh2.alert;
    const bool counter = p.kh2.kind == 5;
    set(p, 0, p.alerting ? 0 : 4);
    set(p, 1, p.alerting ? 6 : 10);
    p.sprites[0].next = p.alerting && counter ? 1 : -1;
    p.sprites[1].next = p.alerting && counter ? 7 : -1;
}

// dk::Sprite::update, a KH2 tick: its clip a frame on; at its end the next, or gone; gone with its base
void tick(Piece &p)
{
    const seq::SequenceData &d = p.pictures->sequence;
    for (std::size_t i = 0; i < p.sprites.size(); ++i) {
        Sprite &s = p.sprites[i];
        if (!s.alive)
            continue;
        if (s.parent >= 0 && !p.sprites[std::size_t(s.parent)].alive) {
            s.alive = false;
            continue;
        }
        s.player.advance(d, 1.0F);
        s.age += 1.0F;
        if (!s.player.playing()) {
            if (s.next >= 0) {
                const int next = s.next;
                s.next = -1;
                set(p, i, next);
                p.sprites[i].player.advance(d, 1.0F);
            } else {
                s.alive = false;
            }
        }
    }
}

bool alive(const Piece &p)
{
    return std::any_of(p.sprites.begin(), p.sprites.end(), [](const Sprite &s) { return s.alive; });
}

void forget_images(Piece &p)
{
    for (Canvas &c : p.canvases) {
        khdays_ui_image_put(c.image);
        c = Canvas{};
    }
}

std::filesystem::path u8path(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::vector<std::uint8_t> read(const std::string &path)
{
    std::ifstream in(u8path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The mission's HD picture of its IMGD number `n` (kh2/msn/<name>/<LANGUAGE>_<name>_bar<n>.png), none when it has none
std::optional<std::filesystem::path> hd_picture(const std::string &mission_file, int n)
{
    const std::filesystem::path bar = u8path(mission_file);
    const std::filesystem::path folder = bar.parent_path() / bar.stem();
    const std::string tail = "_" + bar.stem().string() + "_bar" + std::to_string(n) + ".png";
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
            return entry.path();
    }
    return std::nullopt;
}

// The mission's pieces from its file: each one's picture (its entry, an IMGD) and sequence (the next, a SEQD), with
// the clips KH2's code plays
void load(const kh2e::MissionView &v)
{
    for (Piece &p : pieces)
        forget_images(p);
    pieces.clear();
    file = v.file;
    serial = v.serial;
    ticks = v.ticks;
    std::vector<std::uint8_t> bytes;
    std::vector<seq::BarEntry> entries;
    try {
        bytes = read(file);
        entries = seq::bar_entries(bytes.data(), bytes.size());
    } catch (const std::exception &e) {
        khdays_log("kh2 mission pieces: %s: %s; not shown", file.c_str(), e.what());
    }
    std::map<int, std::shared_ptr<const Pictures>> loaded;
    for (const kh2e::MissionPiece &k : v.pieces) {
        Piece p;
        p.kh2 = k;
        if ((k.kind == 4 || k.kind == 5 || k.kind == 6) && k.entry >= 0 && std::size_t(k.entry) + 1 < entries.size()) {
            auto &found = loaded[k.entry];
            try {
                if (!found) {
                    const seq::BarEntry &image = entries[std::size_t(k.entry)], &sequence = entries[std::size_t(k.entry) + 1];
                    if ((image.type & 0xffffu) != 0x18 || (sequence.type & 0xffffu) != 0x19)
                        throw std::runtime_error("entry " + std::to_string(k.entry) + " not an IMGD and its SEQD");
                    auto pc = std::make_shared<Pictures>();
                    pc->logical = seq::read_sequence_image_size(bytes.data() + image.offset, image.size);
                    // KH2 PC's picture of it, else its own
                    const int n = int(std::count_if(entries.begin(), entries.begin() + k.entry,
                                                    [](const seq::BarEntry &e) { return (e.type & 0xffffu) == 0x18; }));
                    if (const auto hd = hd_picture(file, n)) {
                        pc->texture = khdays::assets::read_image(*hd);
                    } else {
                        pc->texture = seq::read_sequence_image(bytes.data() + image.offset, image.size);
                        khdays_log("kh2 mission pieces: %s: no HD picture of its IMGD %d (the mod's kh2/msn/%s/); "
                                   "its PS2 picture drawn, laid out otherwise than its sequence's",
                                   file.c_str(), n, u8path(file).stem().string().c_str());
                    }
                    pc->sequence = seq::read_sequence(bytes.data() + sequence.offset, sequence.size);
                    found = std::move(pc);
                }
                // the clips its code plays (a timer's to its large 3, 31; a counter's to its columns', 37; a gauge's
                // to its columns', 17)
                const int last = k.kind == 4 ? 31 : k.kind == 6 ? 17 : 37;
                if (found->sequence.clips.size() <= std::size_t(last))
                    throw std::runtime_error("entry " + std::to_string(k.entry + 1) + "'s sequence has " +
                                             std::to_string(found->sequence.clips.size()) + " clips, " +
                                             std::to_string(last + 1) + " wanted");
                p.pictures = found;
            } catch (const std::exception &e) {
                khdays_log("kh2 mission pieces: %s's %s: %s; not shown", file.c_str(),
                           k.kind == 4 ? "timer" : k.kind == 6 ? "gauge" : "counter", e.what());
            }
        }
        pieces.push_back(std::move(p));
    }
}

// INFO_BASE: each open piece below the open ones before it in its column, by their bases' heights
void stack()
{
    float y[3] = {};
    for (Piece &p : pieces) {
        if (!p.pictures || !alive(p))
            continue;
        const int column = column_of(p.kh2);
        p.y = y[column];
        y[column] += float(cr(*p.pictures, 3));
    }
}

// The overlays laid over `pixels` (`w` x `h` texels, KH2's dot at x * k + ox, y * k + oy)
void draw_overlays(const std::vector<Overlay> &overlays, std::vector<std::uint32_t> &pixels, int w, int h, float k,
                   float ox, float oy)
{
    for (const Overlay &o : overlays) {
        const float bx0 = o.x0 * k + ox, by0 = o.y0 * k + oy, bx1 = o.x1 * k + ox, by1 = o.y1 * k + oy;
        std::vector<std::uint32_t> top;
        int tx0 = 0, ty0 = 0, tw = 0, th = 0;
        if (!o.text.empty()) { // its glyphs, their dark edge, from the box's left
            const float size = o.font * k; // texels a font dot
            const float pen_y = (by0 + by1) / 2 - float(khdays::ui::kh2::SysFont::kCellH) * size / 2;
            tx0 = int(std::floor(bx0)) - 4, ty0 = int(std::floor(pen_y)) - 4;
            tw = int(std::ceil(khdays::ui::kh2::sys_font().width(o.text) * size)) + 8;
            th = int(std::ceil(float(khdays::ui::kh2::SysFont::kCellH) * size)) + 8;
            top.assign(std::size_t(tw) * std::size_t(th), 0);
            khdays::ui::kh2::paint_text(o.text, top, tw, th, bx0 - float(tx0), pen_y - float(ty0), size);
        } else if (o.texels != nullptr && o.w > 0 && o.h > 0) { // its picture fitted, at the box's bottom, nearest
            const float scale = std::min((bx1 - bx0) / float(o.w), (by1 - by0) / float(o.h));
            const float dw = float(o.w) * scale, dh = float(o.h) * scale;
            const float px = bx0 + (bx1 - bx0 - dw) / 2, py = by1 - dh;
            tx0 = int(std::floor(px)), ty0 = int(std::floor(py));
            tw = int(std::ceil(px + dw)) - tx0, th = int(std::ceil(py + dh)) - ty0;
            if (tw <= 0 || th <= 0 || scale <= 0.0F)
                continue;
            top.assign(std::size_t(tw) * std::size_t(th), 0);
            for (int y = 0; y < th; ++y)
                for (int x = 0; x < tw; ++x) {
                    const int sx = int((float(tx0 + x) + 0.5F - px) / scale);
                    const int sy = int((float(ty0 + y) + 0.5F - py) / scale);
                    if (sx < 0 || sy < 0 || sx >= o.w || sy >= o.h)
                        continue;
                    top[std::size_t(y) * std::size_t(tw) + std::size_t(x)] =
                        (*o.texels)[std::size_t(sy) * std::size_t(o.w) + std::size_t(o.mirror ? o.w - 1 - sx : sx)];
                }
        }
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                const int cx = tx0 + x, cy = ty0 + y;
                std::uint32_t t = top[std::size_t(y) * std::size_t(tw) + std::size_t(x)];
                if (cx < 0 || cy < 0 || cx >= w || cy >= h || (t >> 24) == 0)
                    continue;
                const unsigned alpha = unsigned(std::lround(float(t >> 24) * std::clamp(o.alpha, 0.0F, 1.0F)));
                t = (t & 0xffffffu) | alpha << 24;
                std::uint32_t &under = pixels[std::size_t(cy) * std::size_t(w) + std::size_t(cx)];
                under = render::source_over(under, t);
            }
    }
}

// A layer's quads (KH2's dots) as a HUD image: DS pixels a dot's kDot, `dy` further down; `slot` its element's; the
// overlays laid over them
void emit_layer(Piece &p, int layer, const std::vector<seq::SequenceQuad> &quads, float dy, std::uint32_t slot,
                const std::vector<Overlay> &overlays = {})
{
    Canvas &c = p.canvases[std::size_t(layer)];
    if (quads.empty())
        return;
    float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (const seq::SequenceQuad &q : quads)
        for (const seq::SequenceVertex &v : q.vertices) {
            x0 = std::min(x0, v.x * kDot);
            y0 = std::min(y0, v.y * kDot + dy);
            x1 = std::max(x1, v.x * kDot);
            y1 = std::max(y1, v.y * kDot + dy);
        }
    for (const Overlay &o : overlays) {
        x0 = std::min(x0, o.x0 * kDot);
        y0 = std::min(y0, o.y0 * kDot + dy);
        x1 = std::max(x1, o.x1 * kDot);
        y1 = std::max(y1, o.y1 * kDot + dy);
    }
    float left = std::floor(x0) - 1, top = std::floor(y0) - 1;
    float right = std::ceil(x1) + 1, bottom = std::ceil(y1) + 1;
    if (p.middle && layer == 0) { // as wide on both sides of the screen's middle: its middle the screen's
        const float twice = 2.0F * kKh2Middle * kDot, l = left;
        left = std::min(left, twice - right);
        right = std::max(right, twice - l);
    }
    const int r = khdays_ui_scale();
    const bool inside = c.image >= 0 && c.scale == r && left >= c.left && top >= c.top &&
                        right <= c.left + float(c.width) && bottom <= c.top + float(c.height);
    if (!inside) { // a canvas round this and what it held
        if (c.image >= 0 && c.scale == r) {
            left = std::min(left, c.left), top = std::min(top, c.top);
            right = std::max(right, c.left + float(c.width)), bottom = std::max(bottom, c.top + float(c.height));
        }
        khdays_ui_image_put(c.image);
        c.left = left;
        c.top = top;
        c.width = int(right - left);
        c.height = int(bottom - top);
        c.scale = r;
        c.drawn = 0;
        c.image = khdays_ui_image_get(c.width * r, c.height * r);
    }
    if (c.image < 0)
        return;
    std::uint64_t h = hash_of(quads, c);
    for (const Overlay &o : overlays) { // (and its overlays: their places, pictures and texts)
        const auto add = [&h](const void *data, std::size_t size) {
            const auto *b = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
                h = (h ^ b[i]) * 0x100000001b3ull;
        };
        add(&o.x0, sizeof o.x0), add(&o.y0, sizeof o.y0), add(&o.x1, sizeof o.x1), add(&o.y1, sizeof o.y1);
        add(&o.key, sizeof o.key), add(&o.alpha, sizeof o.alpha), add(&o.font, sizeof o.font);
        add(o.text.data(), o.text.size() * sizeof(char16_t));
    }
    if (h != c.drawn) {
        const Pictures &pc = *p.pictures;
        c.pixels.assign(std::size_t(c.width * r) * std::size_t(c.height * r), 0);
        render::draw_sequence(quads, pc.texture, pc.logical[0], pc.logical[1], c.pixels, c.width * r, c.height * r,
                              {-c.left * float(r), (dy - c.top) * float(r), kDot * float(r)});
        draw_overlays(overlays, c.pixels, c.width * r, c.height * r, kDot * float(r), -c.left * float(r),
                      (dy - c.top) * float(r));
        std::uint32_t *out = khdays_ui_image_begin(c.image);
        if (out == nullptr)
            return;
        std::copy(c.pixels.begin(), c.pixels.end(), out);
        khdays_ui_image_end(c.image);
        c.drawn = h == 0 ? 1 : h;
    }
    KhdaysUiElement e{};
    e.id = "hud.kh2_mission";
    e.screen = 0;
    e.slot = slot;
    e.x = c.left;
    e.y = c.top;
    e.w = float(c.width);
    e.h = float(c.height);
    e.image = c.image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}

// The PlayerSide this piece is, null none
const PlayerSide *player_side_of(const Piece &p)
{
    const std::string stem = std::filesystem::path(std::u8string(file.begin(), file.end())).stem().string();
    for (const PlayerSide &side : kPlayerSides)
        if (stem == side.mission && p.kh2.kind == side.kind && p.kh2.index == side.index)
            return &side;
    return nullptr;
}

// The player's half of a two-sided piece (PlayerSide): its face's quad taken out for his portrait (where it stood,
// as clear as it was), the bar's plain stretch laid over the bar's name (a quad of the bar's after it), his name over
// that; KH2's own left where the portrait or the name is not known
void player_side(const Piece &p, const PlayerSide &side, std::vector<seq::SequenceQuad> &quads,
                 std::vector<Overlay> &out)
{
    const auto &parts = p.pictures->sequence.parts;
    if (side.face_part >= parts.size() || side.bar_part >= parts.size())
        return;
    struct Box {
        float x0, y0, x1, y1, u0, v0, u1, v1;
    };
    const auto box_of = [](const seq::SequenceQuad &q) {
        Box b{1e30F, 1e30F, -1e30F, -1e30F, 1e30F, 1e30F, -1e30F, -1e30F};
        for (const seq::SequenceVertex &v : q.vertices) {
            b.x0 = std::min(b.x0, v.x), b.y0 = std::min(b.y0, v.y), b.x1 = std::max(b.x1, v.x), b.y1 = std::max(b.y1, v.y);
            b.u0 = std::min(b.u0, v.u), b.v0 = std::min(b.v0, v.v), b.u1 = std::max(b.u1, v.u), b.v1 = std::max(b.v1, v.v);
        }
        return b;
    };
    // (a part's quad's texels are its rectangle's, half a texel in: evaluate_sequence's)
    const auto is_part = [](const Box &b, const seq::SequencePart &part) {
        return std::abs(b.u0 - float(part.uv[0]) - 0.5F) < 0.25F && std::abs(b.v0 - float(part.uv[1]) - 0.5F) < 0.25F &&
               std::abs(b.u1 - float(part.uv[2]) - 0.5F) < 0.25F && std::abs(b.v1 - float(part.uv[3]) - 0.5F) < 0.25F;
    };
    static std::vector<std::uint32_t> face;
    static int face_w = 0, face_h = 0;
    static std::uint64_t face_key = 0;
    const bool has_face = khdays::ui::local_portrait(face, face_w, face_h, face_key);
    const std::u16string name = khdays::ui::kh2::sys_font().get() ? player_name() : std::u16string();
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const Box b = box_of(quads[i]);
        if (has_face && is_part(b, parts[side.face_part])) {
            Overlay o;
            o.x0 = b.x0, o.y0 = b.y0, o.x1 = b.x1, o.y1 = b.y1;
            o.texels = &face;
            o.w = face_w, o.h = face_h;
            o.mirror = true; // (the HUD's faces look left, to the screen's middle; here to the middle on its right)
            o.alpha = float(quads[i].vertices[0].color >> 24) / 128.0F;
            o.key = face_key;
            out.push_back(o);
            quads.erase(quads.begin() + std::ptrdiff_t(i));
            --i;
        } else if (!name.empty() && is_part(b, parts[side.bar_part]) && b.u1 > b.u0 && b.v1 > b.v0) {
            const float ax = (b.x1 - b.x0) / (b.u1 - b.u0), ay = (b.y1 - b.y0) / (b.v1 - b.v0);
            const auto x_at = [&](float u) { return b.x0 + (u - b.u0) * ax; };
            const auto y_at = [&](float v) { return b.y0 + (v - b.v0) * ay; };
            seq::SequenceQuad patch = quads[i];
            for (seq::SequenceVertex &v : patch.vertices) {
                const float fx = (v.u - b.u0) / (b.u1 - b.u0), fy = (v.v - b.v0) / (b.v1 - b.v0);
                v.u = side.plain[0] + fx * (side.plain[2] - side.plain[0]);
                v.v = side.plain[1] + fy * (side.plain[3] - side.plain[1]);
                v.x = x_at(side.name[0]) + fx * (x_at(side.name[2]) - x_at(side.name[0]));
                v.y = y_at(side.name[1]) + fy * (y_at(side.name[3]) - y_at(side.name[1]));
            }
            quads.insert(quads.begin() + std::ptrdiff_t(i) + 1, patch);
            ++i;
            Overlay o;
            o.x0 = x_at(side.name[0]) + 1.0F, o.y0 = y_at(side.name[1]);
            o.x1 = x_at(side.name[2]), o.y1 = y_at(side.name[3]);
            // its capitals as tall as KH2's own name (the font's cell 1.6 times the box: seen beside "LUXORD",
            // 2026-11-15), narrower where it would pass the bar's plain end (as far again as two fifths of the
            // name's own stretch)
            const float room = (x_at(side.name[2]) - x_at(side.name[0])) * 1.4F;
            const float wide = khdays::ui::kh2::sys_font().width(name);
            o.font = (o.y1 - o.y0) * 1.6F / float(khdays::ui::kh2::SysFont::kCellH);
            if (wide > 0.0F && wide * o.font > room)
                o.font = room / wide;
            o.text = name;
            o.alpha = float(quads[i].vertices[0].color >> 24) / 128.0F;
            out.push_back(o);
        }
    }
}

void emit(Piece &p)
{
    const Pictures &pc = *p.pictures;
    std::array<std::vector<seq::SequenceQuad>, 2> quads;
    std::map<int, seq::SequenceActive> actives; // each base's active point now
    for (std::size_t i = 0; i < p.sprites.size(); ++i) {
        Sprite &s = p.sprites[i];
        if (!s.alive)
            continue;
        seq::SequencePose pose;
        if (s.parent >= 0) {
            const Sprite &base = p.sprites[std::size_t(s.parent)];
            auto found = actives.find(s.parent);
            if (found == actives.end()) {
                const auto f = seq::evaluate_sequence(pc.sequence, std::size_t(base.clip), base.player.frame());
                found = actives.emplace(s.parent, f.active.value_or(seq::SequenceActive{})).first;
            }
            const seq::SequenceActive &a = found->second;
            pose.x = base.x + float(a.x) + base.ox + s.ox;
            pose.y = base.y + float(a.y) + base.oy + s.oy;
            pose.color_rate[3] = float((a.color >> 24) & 255u) / 128.0F;
        } else {
            pose.x = s.x + s.ox;
            pose.y = s.y + s.oy;
        }
        pose.scale_x = s.scale_x;
        pose.scroll_time = s.age;
        pose.image_size = {float(pc.logical[0]), float(pc.logical[1])};
        const auto f = seq::evaluate_sequence(pc.sequence, std::size_t(s.clip), s.player.frame(), pose);
        auto &layer = quads[std::size_t(s.layer)];
        layer.insert(layer.end(), f.quads.begin(), f.quads.end());
    }
    // its box under Days's pieces at the top of its column's side, placed on one screen with the one holding its middle
    // (its image grows to all it drew: its bottom corner, the usual anchor, may pass that piece's edge); the large
    // figures in the middle of the screen, placed with the countdown above them
    constexpr std::uint32_t kImage = KES_DIRECT | KES_PICTURE | KES_FINE | KES_HUD | 3u;
    // (a piece laid out by the screen's middle: as the piece at the top in the middle is, its image as wide on both
    // sides of the middle -- the halves of one, each its own image, so placed alike)
    std::vector<Overlay> overlays;
    if (const PlayerSide *side = player_side_of(p))
        player_side(p, *side, quads[0], overlays);
    emit_layer(p, 0, quads[0], kColumnTops[column_of(p.kh2)] - p.top * kDot,
               kImage | (p.middle ? KES_UNDER_TOP : KES_BY_MIDDLE), overlays);
    emit_layer(p, 1, quads[1], 0.0F, kImage | KES_UNDER_TOP);
}
} // namespace

extern "C" void khdays_ui_kh2_mission_2d_frame(unsigned)
{
    kh2e::MissionView v;
    const bool was = visible;
    visible = false;
    if (!kh2e::mission_view(v)) {
        for (Piece &p : pieces)
            forget_images(p);
        pieces.clear();
        serial = 0;
        if (was)
            khdays_ui_changed();
        return;
    }
    if (v.serial != serial || v.file != file)
        load(v);
    const unsigned delta = v.ticks >= ticks ? v.ticks - ticks : 0;
    ticks = v.ticks;
    for (std::size_t k = 0; k < pieces.size() && k < v.pieces.size(); ++k) {
        Piece &p = pieces[k];
        if (!p.pictures)
            continue;
        p.kh2 = v.pieces[k];
        try {
            if (v.shown && (!alive(p) || p.closing))
                open(p);
            else if (!v.shown && alive(p) && !p.closing)
                close(p);
            if (alive(p) && !p.closing) {
                if (p.kh2.kind == 4)
                    timer_update(p);
                else if (p.kh2.kind == 6)
                    gauge_update(p);
                else
                    counter_update(p);
                if (p.kh2.kind != 4)
                    alert_update(p);
            }
            for (unsigned t = 0; t < delta && alive(p); ++t)
                tick(p);
        } catch (const std::exception &e) {
            khdays_log("kh2 mission pieces: %s: %s; not shown", file.c_str(), e.what());
            forget_images(p);
            p.pictures.reset();
            p.sprites.clear();
        }
    }
    stack();
    for (Piece &p : pieces) {
        if (!p.pictures || !alive(p))
            continue;
        p.sprites[0].x = p.x; // the large figures stay at 0, 0: their clips place them on KH2's screen
        p.sprites[0].y = p.y;
        visible = true;
    }
    if (was || visible)
        khdays_ui_changed();
}

extern "C" void khdays_ui_kh2_mission_2d_emit(void)
{
    for (Piece &p : pieces) {
        if (!p.pictures || !alive(p))
            continue;
        try {
            emit(p);
        } catch (const std::exception &e) {
            khdays_log("kh2 mission pieces: %s: %s; not shown", file.c_str(), e.what());
            forget_images(p);
            p.pictures.reset();
            p.sprites.clear();
        }
    }
}
