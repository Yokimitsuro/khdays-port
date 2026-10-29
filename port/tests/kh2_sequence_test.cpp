#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
#include "../ui/kh2_hp_gauge.h"
#include "../ui/kh2_hp_config.h"
#include "khdays/assets/kh2_bar.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace kh2 = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace gauge = khdays::ui::kh2;

void expect(bool ok, const char *what)
{
    if (!ok)
        throw std::runtime_error(what);
}
bool near(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}
template <typename F> void rejects(F f, const char *what)
{
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(what);
}

struct Fixture final {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(296);
    void word(std::size_t p, std::uint32_t v)
    {
        for (unsigned i = 0; i < 4; ++i)
            bytes.at(p + i) = std::uint8_t(v >> (8 * i));
    }
    void number(std::size_t p, float f)
    {
        word(p, std::bit_cast<std::uint32_t>(f));
    }
    Fixture()
    {
        word(0, 0x44514553);
        word(4, 0x160);
        constexpr std::size_t offsets[]{48, 92, 112, 116, 260};
        for (std::size_t t = 0; t < 5; ++t) {
            word(8 + 8 * t, 1);
            word(12 + 8 * t, std::uint32_t(offsets[t]));
        }
        word(60, 4);
        word(64, 4);
        for (std::size_t i = 0; i < 4; ++i)
            word(76 + 4 * i, 0x80808080);
        word(100, 4);
        word(104, 4);
        word(112, 0x10000);
        word(116, 1u | 2u | (1u << 4) | (1u << 5) | (1u << 14));
        word(128, 10);
        word(136, 11);
        for (std::size_t o = 188; o < 212; o += 4)
            number(o, 1);
        number(220, 1);
        number(224, 1);
        word(252, 0x80808080);
        word(256, 0x40404040);
        word(260, 0x10000);
    }
    kh2::SequenceData read() const
    {
        return kh2::read_sequence(bytes.data(), bytes.size());
    }
};

void parser_and_player()
{
    Fixture f;
    auto data = f.read();
    expect(data.parts.size() == 1 && data.clips.size() == 1, "table counts");
    f.word(4, 0x150);
    expect(f.read().animations.size() == data.animations.size(), "0x150 face tables");
    f.word(4, 0x160);
    expect(sequence_frame_count(data, 0) == 11, "inclusive end");
    auto frame = evaluate_sequence(data, 0, 5.9f);
    expect(frame.quads.size() == 1 && near(frame.quads[0].vertices[0].x, 5.5f), "integer timeline and position");
    expect(frame.active && frame.active->x == 5, "active attachment");
    expect(frame.quads[0].vertices[0].color == 0x60606060, "128-neutral color interpolation");
    data.animations[0].flags |= 1u << 15;
    expect(sequence_frame_count(data, 0) == 10 && evaluate_sequence(data, 0, 10).quads.empty(), "last-cut end");
    data.animations[0].flags &= ~(1u << 15);
    data.clips[0].restart = 100;
    kh2::SequencePlayer player;
    player.select(data, 0);
    player.advance(data, 11.5f);
    expect(player.playing() && near(player.frame(), 100), "restart outside visible interval discards overshoot");
    expect(evaluate_sequence(data, 0, player.frame()).quads.empty(), "blank restart interval");
    data.clips[0].loop = 1;
    player.select(data, 0, true);
    player.advance(data, 11);
    expect(!player.playing() && near(player.frame(), 10), "finite loop holds last frame");
    rejects([&] { player.advance(data, -1); }, "negative delta accepted");
    f.word(8, 0xffffffff);
    rejects([&] { f.read(); }, "overflowed table accepted");
    f = Fixture{};
    f.word(108, 5);
    rejects([&] { f.read(); }, "invalid part accepted");
    f = Fixture{};
    f.word(12, 0);
    rejects([&] { f.read(); }, "header overlap accepted");
    f = Fixture{};
    f.number(188, std::numeric_limits<float>::quiet_NaN());
    rejects([&] { f.read(); }, "NaN accepted");
    f = Fixture{};
    f.word(4, 0x100);
    rejects([&] { f.read(); }, "unknown version accepted");
    f = Fixture{};
    f.bytes.pop_back();
    rejects([&] { f.read(); }, "truncated resource accepted");
    data = Fixture{}.read();
    data.animations[0].flags &= ~(1u << 5);
    data.animations[0].rotation_z = {1.570796327f, 1.570796327f};
    auto rotated = evaluate_sequence(data, 0, 0);
    expect(near(rotated.quads[0].vertices[1].x, 0.5f) && near(rotated.quads[0].vertices[1].y, -3.5f),
           "original rotation orientation");
    data = Fixture{}.read();
    data.animations[0].flags &= ~1u;
    data.animations[0].curve = {0, 0, 1, 0};
    expect(near(evaluate_sequence(data, 0, 5).quads[0].vertices[0].x, 1.5f), "cubic uses curve ordinates");
    data = Fixture{}.read();
    data.animations[0].flags &= ~(1u << 4);
    data.animations[0].x = {0, 0};
    data.animations[0].sway_x = {10, 10};
    data.animations[0].sway_count[0] = 1;
    expect(evaluate_sequence(data, 0, 2).active->x == 5, "sway count uses half-turns");
    expect(evaluate_sequence(data, 0, 5).active->x == 10 &&
               near(evaluate_sequence(data, 0, 5).quads[0].vertices[0].x, 10.5f),
           "sway midpoint differs between sprite and active anchor");
    expect(evaluate_sequence(data, 0, 8).active->x == 5, "sway added a second oscillation");
    data.animations[0].flags |= 1u << 3;
    expect(evaluate_sequence(data, 0, 0).active->x == 10 && evaluate_sequence(data, 0, 10).active->x == -10,
           "cosine sway count uses half-turns");
    data = Fixture{}.read();
    data.groups[0].rect = {4, 4, 0, 0};
    data.parts[0].uv = {2, 0, 2, 4};
    expect(evaluate_sequence(data, 0, 0).quads.size() == 1, "mirrored geometry/constant UV rejected");
    data.parts[0].scroll[0] = 1;
    rejects([&] { evaluate_sequence(data, 0, 0); }, "unsupported scroll silently rendered");
    data = Fixture{}.read();
    data.animations[0].x = {std::numeric_limits<int>::max() - 127, std::numeric_limits<int>::max() - 127};
    data.animations[0].flags &= ~(1u << 14);
    data.animations[0].offset_x = {256, 256};
    rejects([&] { evaluate_sequence(data, 0, 0); }, "overflowing active attachment accepted");
    data = Fixture{}.read();
    data.groups.resize(65535, data.groups[0]);
    data.sprites[0] = {0, 65535};
    data.animations.push_back(data.animations[0]);
    data.clips[0].animations.count = 2;
    rejects([&] { evaluate_sequence(data, 0, 0); }, "unbounded sprite-range expansion accepted");
    std::vector<std::uint8_t> bar(16);
    bar[0] = 'B';
    bar[1] = 'A';
    bar[2] = 'R';
    bar[3] = 1;
    bar[4] = 255;
    rejects([&] { kh2::bar_entries(bar.data(), bar.size()); }, "truncated BAR table accepted");
}

kh2::SequenceQuad quad()
{
    kh2::SequenceQuad q;
    q.vertices = {{{0, 0, 0, 0}, {2, 0, 2, 0}, {0, 2, 0, 2}, {2, 2, 2, 2}}};
    q.linear_filter = false;
    return q;
}

void rendering()
{
    expect(render::source_over(0, 0) == 0 && render::source_over(0xff123456, 0) == 0xff123456 &&
               render::source_over(0xff123456, 0xff654321) == 0xff654321 &&
               render::source_over(0, 0x80123456) == 0x80123456 &&
               render::source_over(0xff000000, 0x80ffffff) == 0xff808080,
           "reference face composition changed straight-alpha blending");

    khdays::assets::DecodedTexture atlas;
    atlas.width = 4;
    atlas.height = 4;
    atlas.rgba.resize(64);
    for (std::size_t i = 0; i < atlas.rgba.size(); i += 4) {
        atlas.rgba[i] = i < 32 ? 200 : 20;
        atlas.rgba[i + 3] = 255;
    }
    auto q = quad();
    KhdaysSequenceImage program{};
    q.vertices[0].x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { render::encode_sequence_quads(program, std::span(&q, 1)); }, "NaN GPU geometry accepted");
    q = quad();
    std::vector<std::uint32_t> surface(4);
    render::draw_sequence(std::span(&q, 1), atlas, 2, 2, surface, 2, 2);
    expect((surface[0] & 255) == 200 && (surface[2] & 255) == 20, "replacement resolution mapping");
    atlas.width = 1;
    atlas.height = 1;
    atlas.rgba = {100, 0, 0, 128};
    surface.assign(4, 0xff000032);
    q.blend = 1;
    render::draw_sequence(std::span(&q, 1), atlas, 1, 1, surface, 2, 2);
    expect((surface[0] & 255) == 100, "additive destination blending");
    q.blend = 2;
    render::draw_sequence(std::span(&q, 1), atlas, 1, 1, surface, 2, 2);
    expect((surface[0] & 255) == 50, "subtractive destination blending");
    atlas.rgba = {100, 0, 0, 255};
    auto mask = quad();
    mask.vertices[1].x = mask.vertices[3].x = 1;
    mask.color_write = false;
    mask.depth_write = true;
    mask.depth = 200;
    q.blend = 0;
    q.depth_test = true;
    q.depth = 100;
    const std::array commands{mask, q};
    surface.assign(4, 0);
    render::draw_sequence(commands, atlas, 1, 1, surface, 2, 2);
    expect(surface[0] == 0 && (surface[1] & 255) == 100, "depth-only mask and depth comparison");
}

void hp_mapping()
{
    const auto circle = gauge::hp_gauge_extent(186), bar = gauge::hp_gauge_extent(372);
    expect(near(circle.hp, 60) && !circle.circle_mask && near(circle.bar_length, 0), "186 Days HP capacity");
    expect(near(bar.hp, 120) && near(bar.bar_length, 180) && near(bar.bar_scale, 1.8f), "372 Days HP extension");
    expect(gauge::hp_gauge_extent(65535).bar_length > 63000, "extension was capped");
    expect(gauge::hp_gauge_extent(185).circle_mask, "circle threshold");
    const auto small = gauge::hp_gauge_extent(372, 23.0f / 45);
    expect(near(small.angle, bar.angle) && near(small.bar_length, bar.bar_length * 23 / 45) &&
               near(small.bar_scale, bar.bar_scale * 23 / 45),
           "party art/masks/extensions must scale together");
    expect(!gauge::hp_gauge_extent(186, 23.0f / 45).circle_mask, "party circular HP capacity changed");
    rejects([] { gauge::hp_gauge_extent(-1); }, "negative HP accepted");
}

void party_mask_edges()
{
    auto data = Fixture{}.read();
    data.clips.resize(139, data.clips[0]);
    data.animations[0].x = {};
    data.animations[0].colors = {0x80808080, 0x80808080};
    gauge::HpGaugeStyle style;
    style.source_origin_x = style.source_origin_y = 0;
    style.geometry_scale = 23.0f / 45;
    khdays::assets::DecodedTexture atlas;
    atlas.width = atlas.height = 1;
    atlas.rgba = {0, 255, 0, 255};
    for (float hp : {62.0f, 124.0f}) {
        gauge::HpGaugeSample sample;
        sample.hp = sample.max_hp = sample.previous_hp = hp;
        auto frame = gauge::evaluate_hp_gauge(data, sample, style);
        std::erase_if(frame.quads, [](const auto &q) { return q.color_write; });
        auto forbidden = quad();
        forbidden.depth = 1000;
        forbidden.depth_test = true;
        for (std::size_t i = 0; i < 4; ++i) {
            forbidden.vertices[i].x = ((i & 1) ? 23.0f : 0.0f) + 0.5f;
            forbidden.vertices[i].y = (hp == 62 ? ((i & 2) ? 0.0f : -23.0f) : ((i & 2) ? 23.0f : 0.0f)) + 0.5f;
        }
        frame.quads.push_back(forbidden);
        for (float scale : {1.0f, 3.0f, 5.2f}) {
            std::vector<std::uint32_t> pixels(256 * 256);
            render::draw_sequence(frame.quads, atlas, 1, 1, pixels, 256, 256, {128, 128, scale});
            expect(std::none_of(pixels.begin(), pixels.end(), [](auto p) { return p != 0; }),
                   "party quarter-turn mask left an exposed pixel edge");
        }
    }
}

void hp_pixels(const render::SequenceAsset &asset)
{
    for (const float hp : {62.0f, 124.0f}) {
        gauge::HpGaugeSample cutoff;
        cutoff.hp = cutoff.max_hp = cutoff.previous_hp = hp;
        const auto evaluated = gauge::evaluate_hp_gauge(asset.sequence, cutoff);
        std::vector<std::uint32_t> pixels(832 * 512);
        render::draw_sequence(evaluated.quads, asset.texture, 512, 256, pixels, 832, 512, {560, 256, 5.2f});
        for (int y = 0; y < 512; ++y)
            for (int x = 0; x < 832; ++x) {
                const auto c = pixels[std::size_t(y * 832 + x)];
                const bool green = ((c >> 8) & 255) > (c & 255) + 20 && ((c >> 8) & 255) > ((c >> 16) & 255) + 20;
                expect(!green || (hp == 62 ? x < 664 : y < 360), "quarter-turn mask leaked a green strip");
            }
    }
    auto picture = [&](const gauge::HpGaugeSample &sample) {
        const auto evaluated = gauge::evaluate_hp_gauge(asset.sequence, sample);
        std::vector<std::uint32_t> pixels(320 * 128);
        render::draw_sequence(evaluated.quads, asset.texture, asset.logical_size[0], asset.logical_size[1], pixels, 320,
                              128, {250, 64, 1});
        return pixels;
    };
    auto coverage = [&](const gauge::HpGaugeSample &sample, bool red) {
        const auto pixels = picture(sample);
        return std::count_if(pixels.begin(), pixels.end(), [red](std::uint32_t c) {
            if ((c >> 24) == 0)
                return false;
            const auto r = c & 255u, g = (c >> 8) & 255u, b = (c >> 16) & 255u;
            return red ? r > g + 20 && r > b + 20 : g > r + 20 && g > b + 20;
        });
    };
    gauge::HpGaugeSample sample;
    sample.hp = 0;
    {
        const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample);
        std::vector<std::uint32_t> pixels(128 * 128);
        render::draw_sequence(frame.quads, asset.texture, 512, 256, pixels, 128, 128, {64, 64, 1});
        const auto center = pixels[65 * 128 + 65];
        expect((center >> 24) > 0 && (center & 0xffffffu) == 0, "normal gauge is missing its opaque black center");
    }
    auto previous = coverage(sample, false);
    expect(previous == 0, "zero HP left green pixels");
    for (const float hp : {31.0f, 62.0f, 93.0f, 124.0f, 155.0f, 186.0f}) {
        sample.hp = hp;
        const auto current = coverage(sample, false);
        expect(current > previous, "circular HP mask did not increase fill");
        previous = current;
    }
    sample.hp = 248;
    sample.max_hp = sample.previous_hp = 372;
    sample.damage_frame = 30;
    expect(coverage(sample, true) > 0, "missing previous-HP damage segment");
    auto baseline = sample;
    baseline.previous_hp = baseline.hp;
    expect(picture(sample) != picture(baseline), "previous-HP damage did not change the rendered image");
    sample.damage_frame = 100;
    baseline.damage_frame = 100;
    expect(picture(sample) == picture(baseline), "damage segment remained at the blank restart frame");
    expect(kh2::evaluate_sequence(asset.sequence, 126, 100).quads.empty() &&
               kh2::evaluate_sequence(asset.sequence, 127, 100).quads.empty(),
           "damage clips have visible sprites at the blank restart frame");
}

void limit_pixels(const render::SequenceAsset &asset, const gauge::HpGaugeStyle &style)
{
    expect(style.limit_circle >= 0 && style.limit_bar >= 0, "missing mod limit clips");
    for (auto clip : {style.limit_circle, style.limit_bar}) {
        const auto frame = kh2::evaluate_sequence(asset.sequence, std::size_t(clip), 0);
        expect(!frame.quads.empty(), "empty yellow limit clip");
        for (const auto &q : frame.quads)
            for (const auto &v : q.vertices)
                expect(v.color == 0x8000e6ffu, "yellow RGBA FFE60080 was not stored in the sprite");
    }
    auto draw = [&](float hp, float threshold, float maximum = 372) {
        gauge::HpGaugeSample sample;
        sample.hp = hp;
        sample.max_hp = sample.previous_hp = maximum;
        sample.limit_hp = threshold;
        const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        std::vector<std::uint32_t> pixels(512 * 192);
        render::draw_sequence(frame.quads, asset.texture, 512, 256, pixels, 512, 192, {400, 96, 1.5f});
        return pixels;
    };
    auto yellow = [](std::uint32_t c) {
        return (c & 255) > 80 && ((c >> 8) & 255) > 60 && (c & 255) >= ((c >> 8) & 255) && ((c >> 16) & 255) < 20 &&
               (c >> 24) > 128;
    };
    auto green = [](std::uint32_t c) {
        return ((c >> 8) & 255) > (c & 255) + 30 && ((c >> 8) & 255) > ((c >> 16) & 255) + 30 && (c >> 24) > 240;
    };
    auto count = [&](float hp, float threshold) {
        const auto p = draw(hp, threshold);
        return std::count_if(p.begin(), p.end(), yellow);
    };
    expect(count(0, 248) == 0 && count(372, 0) == 0, "limit overlay remained with no HP or disabled threshold");
    for (float threshold : {31.0f, 62.0f, 93.0f, 124.0f, 186.0f, 248.0f}) {
        const auto at = count(threshold, threshold), above = count(372, threshold);
        expect(at > 0 && at == above, "limit indicator changed beyond its HP threshold");
        expect(count(threshold / 2, threshold) < at, "limit indicator did not shrink with HP below the threshold");
    }
    const auto base = draw(93, 0), all_yellow = draw(93, 124), below = draw(93, 31);
    int covered = 0, total = 0, remaining_green = 0;
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (green(base[i])) {
            ++total;
            covered += yellow(all_yellow[i]);
        }
        remaining_green += green(below[i]);
    }
    expect(total > 0 && covered > total * 9 / 10, "HP below the limit threshold was not covered in yellow");
    expect(remaining_green > 0, "limit overlay hid HP above its threshold");
    expect(draw(372, 1000) == draw(372, 372), "limit threshold was not clamped to max HP");
    std::cout << "limit sprites and HP/threshold intersection checks passed\n";
}

void marker_pixels(const render::SequenceAsset &asset, const gauge::HpGaugeStyle &style)
{
    const auto white = kh2::evaluate_sequence(asset.sequence, std::size_t(style.limit_marker), 0);
    expect(white.quads.size() == 1, "divider is not a single sprite quad");
    for (const auto &v : white.quads[0].vertices)
        expect(v.color == 0x80ffffffu, "white RGBA FFFFFF80 was not stored in the sprite");
    gauge::HpGaugeSample sample;
    sample.hp = 31;
    sample.max_hp = sample.previous_hp = 372;
    sample.limit_hp = 124;
    const auto hidden = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
    for (float position : {0.0f, 31.0f, 62.0f, 124.0f, 185.9f, 186.0f, 186.1f, 248.0f, 372.0f}) {
        sample.limit_marker_hp = position;
        const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        expect(frame.quads.size() == hidden.quads.size() + 1, "divider changed the underlying HP layers");
        const auto &q = frame.quads.back();
        expect(!q.depth_test && q.depth == 0, "divider was clipped by HP/limit masks");
        std::vector<std::uint32_t> pixels(512 * 192);
        render::draw_sequence(std::span(&q, 1), asset.texture, 512, 256, pixels, 512, 192, {400, 96, 1.5f});
        expect(std::any_of(pixels.begin(), pixels.end(),
                           [](std::uint32_t p) {
                               return (p & 255) > 240 && ((p >> 8) & 255) > 240 && ((p >> 16) & 255) > 240 &&
                                      (p >> 24) > 128;
                           }),
               "gray-ring slice did not produce a visible white divider");
    }
    sample.limit_marker_hp = 186;
    const auto junction = gauge::evaluate_hp_gauge(asset.sequence, sample, style).quads.back();
    sample.limit_marker_hp = 186.01f;
    const auto extension = gauge::evaluate_hp_gauge(asset.sequence, sample, style).quads.back();
    for (std::size_t i = 0; i < 4; ++i)
        expect(std::abs(junction.vertices[i].x - extension.vertices[i].x) < 0.1f &&
                   std::abs(junction.vertices[i].y - extension.vertices[i].y) < 0.1f,
               "divider jumped between the circle and extension");
    for (float position :
         {31.0f, 47.0f, 62.0f, 63.0f, 93.0f, 124.0f, 125.0f, 155.0f, 185.0f, 186.0f, 187.0f, 248.0f, 371.0f, 372.0f}) {
        sample.hp = sample.limit_hp = sample.limit_marker_hp = position;
        const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        const auto extent = gauge::hp_gauge_extent(position);
        const float c = std::cos(extent.angle), s = std::sin(extent.angle);
        const float parent_x = float(frame.active->x) - style.source_origin_x;
        const float parent_y = float(frame.active->y) - style.source_origin_y;
        const auto &offset = extent.bar ? style.bar_offset : style.circle_offset;
        const float x = parent_x + offset[0] + 0.5f - (extent.bar ? extent.bar_length : 37 * c);
        const float y = parent_y + offset[1] + 0.5f + (extent.bar ? 37 : -37 * s);
        const float tx = extent.bar ? -1 : s, ty = extent.bar ? 0 : -c;
        constexpr int width = 1200, height = 512;
        const render::SequenceView view{1000, 256, 4};
        std::vector<std::uint32_t> pixels(width * height);
        const auto &q = frame.quads.back();
        render::draw_sequence(std::span(&q, 1), asset.texture, 512, 256, pixels, width, height, view);
        double displacement = 0;
        int count = 0;
        for (int py = 0; py < height; ++py)
            for (int px = 0; px < width; ++px) {
                const auto p = pixels[std::size_t(py * width + px)];
                if ((p >> 24) > 240 && (p & 0xffffffu) == 0xffffffu) {
                    displacement += ((float(px) + 0.5f - view.x) / view.scale - x) * tx +
                                    ((float(py) + 0.5f - view.y) / view.scale - y) * ty;
                    ++count;
                }
            }
        expect(count > 0 && std::abs(displacement / count) < 0.2, "divider's visible strip is off its rotation ray");
        std::fill(pixels.begin(), pixels.end(), 0);
        render::draw_sequence(std::span(frame.quads).first(frame.quads.size() - 1), asset.texture, 512, 256, pixels,
                              width, height, view);
        const auto probe = [&](float side) {
            const int px = int((x + side * tx) * view.scale + view.x);
            const int py = int((y + side * ty) * view.scale + view.y);
            return pixels[std::size_t(py * width + px)];
        };
        const auto yellow = [](std::uint32_t p) {
            return (p >> 24) > 240 && (p & 255) > 240 && ((p >> 8) & 255) > 180 && ((p >> 16) & 255) < 20;
        };
        expect(yellow(probe(-2)) && !yellow(probe(2)), "divider is not centered on the actual yellow HP boundary");
    }
    auto disabled_style = style;
    disabled_style.limit_marker = -1;
    expect(gauge::evaluate_hp_gauge(asset.sequence, sample, disabled_style).quads.size() + 1 ==
               gauge::evaluate_hp_gauge(asset.sequence, sample, style).quads.size(),
           "disabled divider clip remained visible");
    std::cout << "white divider rotation, exact yellow boundary, extension, color and visibility checks passed\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        parser_and_player();
        rendering();
        hp_mapping();
        party_mask_edges();
        if (argc >= 3) {
            const auto asset = render::load_sequence_asset(argv[1], "gaug", argv[2]);
            expect(asset.logical_size == std::array{512, 256}, "private asset logical dimensions");
            expect(asset.sequence.parts.size() == 172 && asset.sequence.animations.size() == 1178 &&
                       asset.sequence.clips.size() == 157,
                   "private asset counts");
            std::size_t quads = 0;
            for (std::size_t clip = 0; clip < asset.sequence.clips.size(); ++clip) {
                const auto end = kh2::sequence_frame_count(asset.sequence, clip);
                for (const float frame : {0.0f, float(end / 2), float(end - 1)})
                    quads += kh2::evaluate_sequence(asset.sequence, clip, frame).quads.size();
            }
            expect(quads > 1000, "private asset evaluation");
            hp_pixels(asset);
            for (const float maximum : {62.0f, 124.0f, 186.0f, 372.0f, 744.0f, 65535.0f}) {
                for (const float hp : {0.0f, maximum / 2, maximum}) {
                    gauge::HpGaugeSample sample;
                    sample.hp = hp;
                    sample.max_hp = maximum;
                    sample.previous_hp = maximum;
                    sample.damage_frame = 30;
                    expect(!gauge::evaluate_hp_gauge(asset.sequence, sample).quads.empty(), "private HP composition");
                }
            }
            std::cout << "gaug: all 157 clips evaluated at start/middle/end (" << quads << " quads)\n";
            if (argc == 4) {
                const auto config = gauge::load_hp_gauge_config(argv[3]);
                const auto folder = std::filesystem::path(argv[3]).parent_path();
                const auto centered =
                    render::load_sequence_asset(folder / config.archive, config.entry, folder / config.texture);
                for (int clip = 132; clip <= 138; ++clip)
                    for (float at : {0.0f, 1.0f, 5.0f, 15.0f, 30.0f, 45.0f, 60.0f}) {
                        gauge::HpGaugeSample sample;
                        sample.frame = at;
                        gauge::HpGaugeStyle original_style;
                        auto centered_style = config.style;
                        original_style.center_override = centered_style.center_override = clip;
                        const auto original_frame = gauge::evaluate_hp_gauge(asset.sequence, sample, original_style);
                        const auto centered_frame = gauge::evaluate_hp_gauge(centered.sequence, sample, centered_style);
                        expect(original_frame.quads.size() == centered_frame.quads.size(),
                               "recentered gauge lost visible tracks");
                        for (std::size_t q = 0; q < original_frame.quads.size(); ++q)
                            for (std::size_t v = 0; v < 4; ++v) {
                                const auto &a = original_frame.quads[q].vertices[v],
                                           &b = centered_frame.quads[q].vertices[v];
                                expect(near(a.x, b.x) && near(a.y, b.y) && a.color == b.color,
                                       "center normalization changed animation motion/color");
                            }
                    }
                const auto black = kh2::evaluate_sequence(centered.sequence, 137, 0);
                expect(!black.quads.empty(), "normal center has no visible quads");
                const auto gauge_frame = gauge::evaluate_hp_gauge(centered.sequence, {}, config.style);
                expect(near(gauge_frame.quads[0].vertices[0].x, black.quads[0].vertices[0].x),
                       "center sprite was not added to scene");
                std::cout << "mod config and all seven centered parent animations preserve original relative motion\n";
                if (config.style.limit_circle >= 0)
                    limit_pixels(centered, config.style);
                if (config.style.limit_marker >= 0)
                    marker_pixels(centered, config.style);
            }
        }
        std::cout << "KH2 sequence tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
