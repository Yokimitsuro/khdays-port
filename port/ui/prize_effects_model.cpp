// A prize picture's effect: its text read and checked, its pages stacked, its frames as sequence quads.
#include "prize_effects_model.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace khdays::ui::prizes {

namespace {

bool number(std::istringstream &in, float &out)
{
    return static_cast<bool>(in >> out) && std::isfinite(out);
}

bool word(std::istringstream &in, const char *expected)
{
    std::string w;
    return static_cast<bool>(in >> w) && w == expected;
}

std::uint8_t colour_byte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(std::lround(value * 128.0F), 0L, 255L));
}

} // namespace

std::optional<Effect> parse_effect(std::string_view text, std::string &why)
{
    Effect e;
    bool versioned = false;
    std::istringstream lines{std::string(text)};
    std::string line;
    int number_of_line = 0;
    const auto fail = [&](const std::string &reason) {
        why = "line " + std::to_string(number_of_line) + ": " + reason;
        return std::optional<Effect>{};
    };
    while (std::getline(lines, line)) {
        ++number_of_line;
        if (const auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream in(line);
        std::string what;
        if (!(in >> what))
            continue;
        if (!versioned) {
            int version = 0;
            if (what != "effect" || !(in >> version))
                return fail("the first statement is not 'effect <version>'");
            if (version != 1)
                return fail("version " + std::to_string(version) + " (this port reads 1)");
            versioned = true;
        } else if (what == "page") {
            int n = -1;
            std::string file;
            if (!(in >> n >> file) || n < 0)
                return fail("'page <n> <file>'");
            if (static_cast<std::size_t>(n) >= e.pages.size())
                e.pages.resize(static_cast<std::size_t>(n) + 1);
            e.pages[static_cast<std::size_t>(n)] = file;
        } else if (what == "body") {
            if (!(in >> e.body) || e.body < 0)
                return fail("'body <layer>'");
        } else if (what == "frame_time") {
            if (!(in >> e.frame_time) || e.frame_time < 1)
                return fail("'frame_time <frames>' of at least 1");
        } else if (what == "frame") {
            e.frames.emplace_back();
        } else if (what == "layer") {
            if (e.frames.empty())
                return fail("a layer before any frame");
            Layer l;
            std::array<float, 4> c{};
            std::string blend;
            if (!word(in, "page") || !(in >> l.page) || !word(in, "rect") || !number(in, l.u) || !number(in, l.v) ||
                !number(in, l.w) || !number(in, l.h) || !word(in, "corners"))
                return fail("'layer page <n> rect <u> <v> <w> <h> corners ...'");
            for (float &corner : l.corners)
                if (!number(in, corner))
                    return fail("eight numbers after 'corners'");
            if (!word(in, "colour") || !number(in, c[0]) || !number(in, c[1]) || !number(in, c[2]) ||
                !number(in, c[3]) || !word(in, "blend") || !(in >> blend))
                return fail("'colour <r> <g> <b> <a> blend <alpha|add>'");
            if (l.w <= 0 || l.h <= 0)
                return fail("a part of no size");
            for (float v : c)
                if (v < 0 || v > 2)
                    return fail("a colour out of 0..2");
            if (blend != "alpha" && blend != "add")
                return fail("blend '" + blend + "' (alpha or add)");
            l.blend = blend == "add" ? Blend::add : Blend::alpha;
            l.colour = std::uint32_t(colour_byte(c[0])) | std::uint32_t(colour_byte(c[1])) << 8 |
                       std::uint32_t(colour_byte(c[2])) << 16 | std::uint32_t(colour_byte(c[3])) << 24;
            e.frames.back().layers.push_back(l);
        } else {
            return fail("'" + what + "' is not a statement of the format");
        }
    }
    if (!versioned)
        return fail("no 'effect <version>'");
    if (e.frames.empty())
        return fail("no frame");
    for (const auto &f : e.frames) {
        if (f.layers.empty())
            return fail("a frame with no layer");
        for (const auto &l : f.layers)
            if (l.page < 0 || static_cast<std::size_t>(l.page) >= e.pages.size() || e.pages[l.page].empty())
                return fail("page " + std::to_string(l.page) + " is not declared");
    }
    if (static_cast<std::size_t>(e.body) >= e.frames[0].layers.size())
        return fail("body " + std::to_string(e.body) + " is not a layer of the first frame");
    return e;
}

Atlas stack_pages(const std::vector<std::pair<int, int>> &sizes)
{
    Atlas a;
    for (const auto &[w, h] : sizes) {
        a.top.push_back(a.height);
        a.page_width.push_back(w);
        a.page_height.push_back(h);
        a.width = std::max(a.width, w);
        a.height += h;
    }
    return a;
}

Reach reach_of(const Effect &e)
{
    Reach r;
    for (const auto &f : e.frames)
        for (const auto &l : f.layers)
            for (float c : l.corners)
                r.half = std::max(r.half, std::fabs(c));
    const auto &body = e.frames[0].layers[static_cast<std::size_t>(e.body)].corners;
    r.body_width = std::hypot(body[2] - body[0], body[3] - body[1]);
    return r;
}

std::vector<assets::kh2::SequenceQuad> frame_quads(const Effect &e, int f, const Reach &r, const Atlas &a)
{
    std::vector<assets::kh2::SequenceQuad> out;
    for (const auto &l : e.frames[static_cast<std::size_t>(f)].layers) {
        const auto p = static_cast<std::size_t>(l.page);
        const float pw = float(a.page_width[p]), ph = float(a.page_height[p]), top = float(a.top[p]);
        assets::kh2::SequenceQuad q;
        for (std::size_t k = 0; k < 4; ++k) {
            auto &v = q.vertices[k];
            v.x = r.half + l.corners[2 * k];
            v.y = r.half + l.corners[2 * k + 1];
            v.u = (l.u + ((k & 1) ? l.w : 0)) * pw;
            v.v = top + (l.v + ((k & 2) ? l.h : 0)) * ph;
            v.color = l.colour;
        }
        q.blend = l.blend;
        out.push_back(q);
    }
    return out;
}

std::vector<assets::kh2::SequenceQuad> sheet_quads(const Effect &e, const Reach &r, const Atlas &a, float column)
{
    std::vector<assets::kh2::SequenceQuad> out;
    for (std::size_t f = 0; f < e.frames.size(); ++f)
        for (auto q : frame_quads(e, static_cast<int>(f), r, a)) {
            for (auto &v : q.vertices)
                v.x += float(f) * column;
            out.push_back(q);
        }
    return out;
}

int frame_at(const Effect &e, unsigned frames)
{
    return static_cast<int>((frames / static_cast<unsigned>(e.frame_time)) % e.frames.size());
}

int picture_of(std::uint32_t texture, const std::vector<std::uint32_t> &words)
{
    if (texture == 0)
        return -1;
    const auto at = std::find(words.begin(), words.end(), texture);
    return at == words.end() ? -1 : static_cast<int>(at - words.begin());
}

} // namespace khdays::ui::prizes
