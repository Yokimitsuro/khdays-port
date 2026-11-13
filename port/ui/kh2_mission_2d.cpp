// KINGDOM HEARTS II's mission pieces (docs/KH2_ENEMIES.md, "KH2's missions and signals"): the timer and counters of
// the KH2 mission a KH2 boss's battle runs (runtime/kh2_enemies: its file the KH2 enemies' mod's kh2/msn/<name>.bar),
// drawn from the mission's own sequences (each IMGD's next entry, a SEQD: Demyx's "ti_e" and "ct_h") and its HD
// pictures (kh2/msn/<name>/<LANGUAGE>_<name>_bar<N>.png, the Nth IMGD's: KH2 PC's, laid out as its sequences have
// them; without one the IMGD's own, laid out otherwise) as KH2's dk::TIMER and dk::COUNTER build them (the PS3
// build's, read): sprites of the sequence's clips -- the base (3
// coming, then 4; 5 leaving), its second piece (9, then 10; 11 leaving), the digits (12-21 by digit; 22 a counter's dim
// leading zero; 23 and 24 the timer's separators, a counter's slash; 24-33 and 34 its most's digits) -- each child
// placed at its base's active point plus its offset and faded with the base's active alpha. The timer shows minutes,
// seconds and hundredths (dk::Clock::vCountToHMSMS) and counts its last three seconds in large figures (clips 31, 30,
// 29, then 28 at 0, in the middle of the screen). The pieces stand in the columns the mission names as KH2's INFO_BASE
// stacks them (each below the open ones before it by its base's height), half a DS dot a KH2 dot, under Days's own
// pieces at the top of that side; drawn on the CPU into HUD images.
#include "kh2_mission_2d.h"

#include "../gfx/kh2_sequence.h"
#include "../runtime/kh2_enemies/shell.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../runtime/core/session_log.h"
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
constexpr int kTimerColumns[3] = {26, 27, 25}, kCounterColumns[3] = {36, 37, 35};
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
    int layer = 0; // its image: 0 the piece's box, 1 the timer's large countdown
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
    std::vector<int> digits, most_digits; // a timer's: minutes, seconds, hundredths (tens first); a counter's ones first
    int shown = std::numeric_limits<int>::min(), shown_most = std::numeric_limits<int>::min();
    unsigned countdown = 0; // the timer's large figures made (dk::TIMER's flag bits: 3, 2, 1, 0)
    bool closing = false;
    float x = 0, y = 0; // its base's place in its column
    float top = 0;      // its highest dot below its place standing (its base's clip 4): its column's top in DS pixels
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

void open(Piece &p)
{
    // its top standing: its base's (4), its second's (10) and a digit's (12) at the base's active point
    const seq::SequenceData &d = p.pictures->sequence;
    const auto base = seq::evaluate_sequence(d, 4, 0);
    const seq::SequenceActive a = base.active.value_or(seq::SequenceActive{});
    seq::SequencePose at;
    at.x = float(a.x);
    at.y = float(a.y);
    p.top = std::numeric_limits<float>::max();
    for (const auto &f : {base, seq::evaluate_sequence(d, 10, 0, at), seq::evaluate_sequence(d, 12, 0, at)})
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
    else
        open_counter(p);
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
        if ((k.kind == 4 || k.kind == 5) && k.entry >= 0 && std::size_t(k.entry) + 1 < entries.size()) {
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
                // the clips its code plays (a timer's to its large 3, 31; a counter's to its columns', 37)
                const int last = k.kind == 4 ? 31 : 37;
                if (found->sequence.clips.size() <= std::size_t(last))
                    throw std::runtime_error("entry " + std::to_string(k.entry + 1) + "'s sequence has " +
                                             std::to_string(found->sequence.clips.size()) + " clips, " +
                                             std::to_string(last + 1) + " wanted");
                p.pictures = found;
            } catch (const std::exception &e) {
                khdays_log("kh2 mission pieces: %s's %s: %s; not shown", file.c_str(),
                           k.kind == 4 ? "timer" : "counter", e.what());
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

// A layer's quads (KH2's dots) as a HUD image: DS pixels a dot's kDot, `dy` further down; `slot` its element's
void emit_layer(Piece &p, int layer, const std::vector<seq::SequenceQuad> &quads, float dy, std::uint32_t slot)
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
    float left = std::floor(x0) - 1, top = std::floor(y0) - 1;
    float right = std::ceil(x1) + 1, bottom = std::ceil(y1) + 1;
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
    if (const std::uint64_t h = hash_of(quads, c); h != c.drawn) {
        const Pictures &pc = *p.pictures;
        c.pixels.assign(std::size_t(c.width * r) * std::size_t(c.height * r), 0);
        render::draw_sequence(quads, pc.texture, pc.logical[0], pc.logical[1], c.pixels, c.width * r, c.height * r,
                              {-c.left * float(r), (dy - c.top) * float(r), kDot * float(r)});
        std::uint32_t *out = khdays_ui_image_begin(c.image);
        if (out == nullptr)
            return;
        std::copy(c.pixels.begin(), c.pixels.end(), out);
        khdays_ui_image_end(c.image);
        c.drawn = h;
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
        const auto f = seq::evaluate_sequence(pc.sequence, std::size_t(s.clip), s.player.frame(), pose);
        auto &layer = quads[std::size_t(s.layer)];
        layer.insert(layer.end(), f.quads.begin(), f.quads.end());
    }
    // its box under Days's pieces at the top of its column's side, placed on one screen with the one holding its middle
    // (its image grows to all it drew: its bottom corner, the usual anchor, may pass that piece's edge); the large
    // figures in the middle of the screen, placed with the countdown above them
    constexpr std::uint32_t kImage = KES_DIRECT | KES_PICTURE | KES_FINE | KES_HUD | 3u;
    emit_layer(p, 0, quads[0], kColumnTops[column_of(p.kh2)] - p.top * kDot, kImage | KES_BY_MIDDLE);
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
                else
                    counter_update(p);
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
