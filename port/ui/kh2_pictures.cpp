// KINGDOM HEARTS II's object pictures (kh2_pictures.h): a KH2 enemy's dk::Layout read from its archive and played as
// the PS3 build's YI::LAYOUT plays it (our reading of it: YI::LAYOUT::ChangeNumber, Control, Draw, GetPlayMode;
// Tz::Layout::update; docs/KH2_ENEMIES.md, "Pictures"). The archive's first entry of type 0x2e is a BAR of a LAYD
// (type 0x1c) and an IMGZ (0x1d). The LAYD: "LAYD", its version, then counts and offsets of its screens (24 bytes
// each: the picture, the sequence unit, the sequence's clip, the frame it starts at, its x and y on KH2's 512 x 416
// screen), its layouts (20 bytes: a u16 first screen and a u16 count of them) and its sequence units (a u32 offset of
// a SEQD in the LAYD each). The IMGZ: "IMGZ", its version, a header size, a count, then an offset and a size of each
// IMGD. A layout shown starts its time at 0; each screen's sequence plays from its clip's first frame once the time
// reaches the screen's frame; the screens draw at their places moved by the layout's offset (1:315); the layout is over
// when its screens have all started and none plays or loops (YI::SEQUENCE::GetPlayMode), and the picture with it
// (Tz::Layout::update ends the object). The pictures are KH2 PC's HD pages of the archive's pictures, after its
// effects' (kh2/fx/<name>/-<n>.png or .dds); without them nothing is drawn.
#include "kh2_pictures.h"

#include "../gfx/kh2_sequence.h"

extern "C" {
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/kh2_enemies/shell.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
#include "kh2_reticles.h"
#include "heart_points.h"
#include "prize_effects.h"
}

#include "khdays/assets/dds.h"
#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_sequence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;

constexpr unsigned kProgram = KHDAYS_RETICLE_COUNT + KHDAYS_HEART_PROGRAMS + KHDAYS_PRIZE_PROGRAMS;
constexpr float kTexels = 2.0F; // the canvas's texels a KH2 dot (KH2 PC's pictures: twice the PS2's)

std::filesystem::path path_of(const char *s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

std::uint32_t u32(const std::vector<std::uint8_t> &b, std::size_t at)
{
    if (at + 4 > b.size())
        throw std::runtime_error("the layout runs past its file");
    std::uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

// A LAYD's screen (YI::LAYOUT::SCREEN_FORMAT)
struct Screen final {
    int image = 0, unit = 0, clip = 0, start = 0, x = 0, y = 0;
};

// An archive's pictures: its LAYD's screens, layouts (first screen, count) and sequences, each IMGD's size (the
// sequences' dots) and where its HD page stands in the atlas (KH2 dots down the stack)
struct Archive final {
    std::vector<Screen> screens;
    std::vector<std::pair<int, int>> layouts;
    std::vector<seq::SequenceData> sequences;
    std::vector<std::array<int, 2>> sizes;
    std::vector<khdays::assets::DecodedTexture> pages;
    std::vector<float> down; // each picture's top in the atlas, KH2 dots
};

std::shared_ptr<const Archive> read_archive(const std::string &archive, const std::string &stem, int first_page)
{
    std::ifstream file(path_of(archive.c_str()), std::ios::binary);
    if (!file)
        throw std::runtime_error("its archive cannot be opened");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::size_t at = 0, end = 0;
    for (const auto &e : seq::bar_entries(bytes.data(), bytes.size()))
        if (e.type == 0x2e) { // YS::BINARC::get_info(0x2e, 0): the first
            at = e.offset;
            end = e.offset + e.size;
            break;
        }
    if (end == 0)
        throw std::runtime_error("its archive has no pictures (no entry of type 0x2e)");
    std::vector<std::uint8_t> layd, imgz;
    for (const auto &e : seq::bar_entries(bytes.data(), end, at)) {
        if (e.type == 0x1c && layd.empty())
            layd.assign(bytes.begin() + std::ptrdiff_t(e.offset), bytes.begin() + std::ptrdiff_t(e.offset + e.size));
        if (e.type == 0x1d && imgz.empty())
            imgz.assign(bytes.begin() + std::ptrdiff_t(e.offset), bytes.begin() + std::ptrdiff_t(e.offset + e.size));
    }
    if (layd.size() < 0x20 || std::memcmp(layd.data(), "LAYD", 4) != 0 || imgz.size() < 0x10 ||
        std::memcmp(imgz.data(), "IMGZ", 4) != 0)
        throw std::runtime_error("its pictures have no LAYD and IMGZ");
    auto a = std::make_shared<Archive>();
    const std::uint32_t screens = u32(layd, 8), screen_at = u32(layd, 12), layouts = u32(layd, 16),
                        layout_at = u32(layd, 20), units = u32(layd, 24), unit_at = u32(layd, 28);
    for (std::uint32_t i = 0; i < screens; ++i) {
        const std::size_t o = screen_at + 24u * i;
        a->screens.push_back({int(u32(layd, o)), int(u32(layd, o + 4)), int(u32(layd, o + 8)), int(u32(layd, o + 12)),
                              int(u32(layd, o + 16)), int(u32(layd, o + 20))});
    }
    for (std::uint32_t i = 0; i < layouts; ++i) {
        const std::uint32_t w = u32(layd, layout_at + 20u * i);
        a->layouts.push_back({int(w & 0xffffu), int(w >> 16)});
    }
    for (std::uint32_t i = 0; i < units; ++i) {
        const std::uint32_t o = u32(layd, unit_at + 4u * i);
        if (o >= layd.size())
            throw std::runtime_error("a sequence of its pictures lies past its LAYD");
        a->sequences.push_back(seq::read_sequence(layd.data() + o, layd.size() - o));
    }
    const std::uint32_t count = u32(imgz, 12);
    float down = 0.0F;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t o = u32(imgz, 16 + 8u * i), size = u32(imgz, 20 + 8u * i);
        if (std::size_t(o) + size > imgz.size())
            throw std::runtime_error("a picture lies past its IMGZ");
        a->sizes.push_back(seq::read_sequence_image_size(imgz.data() + o, size));
        char found[2048] = {};
        khdays::assets::DecodedTexture page;
        for (const char *extension : {".png", ".dds"}) {
            const std::string name = stem + std::to_string(first_page + int(i)) + extension;
            if (khdays_mods_find(name.c_str(), found, int(sizeof(found)))) {
                page = std::strcmp(extension, ".dds") == 0 ? khdays::assets::read_dds(path_of(found))
                                                           : khdays::assets::read_image(path_of(found));
                break;
            }
        }
        if (page.width <= 0 || page.height <= 0)
            throw std::runtime_error("no HD page " + stem + std::to_string(first_page + int(i)));
        a->pages.push_back(std::move(page));
        a->down.push_back(down);
        down += float(a->sizes.back()[1]);
    }
    for (const Screen &s : a->screens)
        if (s.unit < 0 || std::size_t(s.unit) >= a->sequences.size() || s.image < 0 ||
            std::size_t(s.image) >= a->sizes.size() || s.clip < 0 ||
            std::size_t(s.clip) >= a->sequences[std::size_t(s.unit)].clips.size())
            throw std::runtime_error("a screen of its LAYD names no sequence, clip or picture of its own");
    return a;
}

// The atlas: every archive's pictures stacked down one image, KH2 dots kTexels texels each (each page drawn to that
// size); made again when an archive comes, each image made kept (the textures' store holds their texels, not a copy)
struct Atlas final {
    std::vector<std::shared_ptr<const Archive>> archives; // those in it, in their order down
    std::vector<float> top;                               // each archive's first picture's top, KH2 dots
    std::array<int, 2> logical{};                         // its size in KH2 dots
    std::deque<std::vector<std::uint32_t>> images;        // every image made, the last the one in use
    int id = -1;
};

void add_to_atlas(Atlas &atlas, const std::shared_ptr<const Archive> &a)
{
    atlas.top.push_back(float(atlas.logical[1]));
    atlas.archives.push_back(a);
    int width = atlas.logical[0], height = atlas.logical[1];
    for (const auto &size : a->sizes)
        width = std::max(width, size[0]), height += size[1];
    std::vector<std::uint32_t> pixels(std::size_t(width * kTexels) * std::size_t(height * kTexels), 0u);
    // the pictures before, kept
    const int old_w = int(float(atlas.logical[0]) * kTexels), old_h = int(float(atlas.logical[1]) * kTexels);
    for (int y = 0; y < old_h; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * std::size_t(width * kTexels),
                    atlas.images.back().data() + std::size_t(y) * std::size_t(old_w), std::size_t(old_w) * 4u);
    const int stride = int(float(width) * kTexels);
    for (std::size_t i = 0; i < a->pages.size(); ++i) {
        const auto &page = a->pages[i];
        const int w = int(float(a->sizes[i][0]) * kTexels), h = int(float(a->sizes[i][1]) * kTexels);
        const int top = int((atlas.top.back() + a->down[i]) * kTexels);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) { // the page drawn to the picture's size (nearest: KH2 PC's pages are exact)
                const int sx = std::min(page.width - 1, x * page.width / w);
                const int sy = std::min(page.height - 1, y * page.height / h);
                std::uint32_t texel;
                std::memcpy(&texel, page.rgba.data() + (std::size_t(sy) * std::size_t(page.width) + std::size_t(sx)) * 4,
                            4);
                pixels[std::size_t(top + y) * std::size_t(stride) + std::size_t(x)] = texel;
            }
    }
    atlas.logical = {width, height};
    atlas.images.push_back(std::move(pixels));
    atlas.id = khdays_textures_add_info("kh2:pictures:atlas", int(float(width) * kTexels), int(float(height) * kTexels),
                                        atlas.images.back().data(), KHDAYS_REPLACEMENT_NO_MIPS);
    if (atlas.id < 0)
        throw std::runtime_error("the pictures' atlas could not be registered");
}

// A picture shown: the layout playing (YI::LAYOUT): its number, the change it was started for, its time (KH2's
// frames), its screens' sequences (started once the time reaches theirs)
struct Shown final {
    std::shared_ptr<const Archive> archive;
    int number = -1;
    unsigned changes = ~0u;
    float time = 0.0F;
    std::vector<seq::SequencePlayer> players;
    std::vector<bool> started;
    std::vector<float> ages; // each screen's sequence's frames since it started (its parts' pictures' scroll)
    float x = 0.0F, y = 0.0F;
    bool seen = false;
};

std::map<std::string, std::shared_ptr<const Archive>> archives; // by archive path (null: it could not be read)
std::set<std::string> told;
Atlas atlas;
std::map<unsigned, Shown> shown;
unsigned last_vblank = 0;
bool started = false;

struct Canvas final {
    int token = -1;
    KhdaysSequenceImage commands{};
};
std::array<Canvas, 4> canvases; // a 3D list's build slot each (khdays_g3d_build_slot)
bool told_quads = false;

// The layout `number` of the picture started (YI::LAYOUT::SetNumber: ChangeNumber, its time 0)
void start(Shown &s, int number)
{
    s.number = number;
    s.time = 0.0F;
    s.players.clear();
    s.started.clear();
    s.ages.clear();
    if (number < 0 || std::size_t(number) >= s.archive->layouts.size())
        return;
    const int count = s.archive->layouts[std::size_t(number)].second;
    s.players.resize(std::size_t(std::max(count, 0)));
    s.started.assign(s.players.size(), false);
    s.ages.assign(s.players.size(), 0.0F);
}

const Screen *screen_of(const Shown &s, std::size_t i)
{
    const std::size_t at = std::size_t(s.archive->layouts[std::size_t(s.number)].first) + i;
    return at < s.archive->screens.size() ? &s.archive->screens[at] : nullptr;
}

// YI::LAYOUT::Control over `frames`: its time on, each screen's sequence started at its frame and played on; whether
// it still plays (GetPlayMode: a screen yet to start or one whose sequence plays or loops)
bool play(Shown &s, float frames)
{
    if (s.number < 0 || std::size_t(s.number) >= s.archive->layouts.size() || s.players.empty())
        return false;
    bool playing = false;
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const Screen *sc = screen_of(s, i);
        if (sc == nullptr)
            continue;
        const auto &data = s.archive->sequences[std::size_t(sc->unit)];
        const float to = s.time + frames;
        if (!s.started[i] && to >= float(sc->start)) {
            s.players[i].select(data, std::size_t(sc->clip), true);
            s.players[i].advance(data, std::max(0.0F, to - float(sc->start)));
            s.ages[i] = std::max(0.0F, to - float(sc->start));
            s.started[i] = true;
        } else if (s.started[i]) {
            s.players[i].advance(data, frames);
            s.ages[i] += frames;
        }
        if (!s.started[i] || s.players[i].playing())
            playing = true;
    }
    s.time += frames;
    return playing;
}

void drop(const std::string &archive, const std::exception &e)
{
    archives[archive] = nullptr;
    if (told.insert(archive).second)
        khdays_log("ui: a KH2 enemy's pictures left out (%s): %s", archive.c_str(), e.what());
}

} // namespace

extern "C" void khdays_ui_kh2_pictures_draw(void)
{
    const unsigned vblank = khdays_link_frame();
    const unsigned frames = !started || khdays_link_paused() ? 0u : std::min(vblank - last_vblank, 4u);
    started = true;
    last_vblank = vblank;
    const int slot = khdays_g3d_build_slot();
    Canvas &c = canvases[std::size_t(slot)];
    c.token = -1;
    const int count = khdays_kh2_enemies_picture_count();
    for (auto &[handle, s] : shown)
        s.seen = false;
    std::vector<seq::SequenceQuad> quads;
    std::vector<unsigned> over;
    for (int i = 0; i < count; ++i) {
        unsigned handle = 0, changes = 0;
        int number = 0, first_page = 0;
        float place[2] = {};
        char archive[2048], stem[512];
        if (!khdays_kh2_enemies_picture(i, &handle, &number, &changes, place, archive, int(sizeof(archive)), stem,
                                        int(sizeof(stem)), &first_page))
            continue;
        auto found = archives.find(archive);
        if (found == archives.end()) {
            try {
                found = archives.emplace(archive, read_archive(archive, stem, first_page)).first;
                add_to_atlas(atlas, found->second);
                khdays_log("ui: a KH2 enemy's pictures from %s (%zu layouts)", archive,
                           found->second->layouts.size());
            } catch (const std::exception &e) {
                drop(archive, e);
                continue;
            }
        }
        if (!found->second)
            continue;
        Shown &s = shown[handle];
        s.seen = true;
        s.x = place[0];
        s.y = place[1];
        if (s.archive != found->second || s.changes != changes) {
            s.archive = found->second;
            s.changes = changes;
            start(s, number);
        }
        if (!play(s, float(frames))) {
            over.push_back(handle);
            continue;
        }
        // its screens' quads at their places moved by its offset (YI::LAYOUT::Draw), its pictures' places in the atlas
        const auto at = std::find(atlas.archives.begin(), atlas.archives.end(), s.archive);
        const float archive_top = atlas.top[std::size_t(at - atlas.archives.begin())];
        for (std::size_t k = 0; k < s.players.size(); ++k) {
            const Screen *sc = screen_of(s, k);
            if (sc == nullptr || !s.started[k] || !s.players[k].playing())
                continue;
            const auto &data = s.archive->sequences[std::size_t(sc->unit)];
            seq::SequencePose pose;
            pose.x = float(sc->x) + s.x;
            pose.y = float(sc->y) + s.y;
            pose.scroll_time = s.ages[k];
            pose.image_size = {float(s.archive->sizes[std::size_t(sc->image)][0]),
                               float(s.archive->sizes[std::size_t(sc->image)][1])};
            auto frame = seq::evaluate_sequence(data, s.players[k].clip(), s.players[k].frame(), pose);
            const float down = archive_top + s.archive->down[std::size_t(sc->image)];
            for (auto &q : frame.quads) {
                for (auto &v : q.vertices)
                    v.v += down;
                q.wrap_rect[1] += down;
                q.wrap_rect[3] += down;
                quads.push_back(q);
            }
        }
    }
    for (const unsigned h : over) {
        shown.erase(h);
        khdays_kh2_enemies_picture_over(h);
    }
    for (auto it = shown.begin(); it != shown.end();)
        it = it->second.seen ? std::next(it) : shown.erase(it);
    if (quads.empty() || atlas.id < 0)
        return;
    if (quads.size() > KSI_MAX_QUADS) {
        if (!told_quads)
            khdays_log("ui: a KH2 enemy's pictures draw %zu pieces at once, the first %u shown", quads.size(),
                       unsigned(KSI_MAX_QUADS));
        told_quads = true;
        quads.resize(KSI_MAX_QUADS);
    }
    float x0 = 1e9F, y0 = 1e9F, x1 = -1e9F, y1 = -1e9F;
    for (const auto &q : quads)
        for (const auto &v : q.vertices)
            x0 = std::min(x0, v.x), y0 = std::min(y0, v.y), x1 = std::max(x1, v.x), y1 = std::max(y1, v.y);
    x0 = std::floor(x0) - 1.0F, y0 = std::floor(y0) - 1.0F, x1 = std::ceil(x1) + 1.0F, y1 = std::ceil(y1) + 1.0F;
    c.token = int(kProgram) * 4 + slot;
    auto *w = c.commands.words;
    std::memset(w, 0, sizeof(unsigned) * KSI_HEADER);
    const auto number = [w](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
    w[KSI_OUTPUT] = unsigned(c.token);
    w[KSI_ATLAS] = unsigned(atlas.id);
    w[KSI_LOGICAL_W] = unsigned(atlas.logical[0]);
    w[KSI_LOGICAL_H] = unsigned(atlas.logical[1]);
    w[KSI_WIDTH] = unsigned(std::ceil((x1 - x0) * kTexels));
    w[KSI_HEIGHT] = unsigned(std::ceil((y1 - y0) * kTexels));
    number(KSI_VIEW_X, -x0 * kTexels);
    number(KSI_VIEW_Y, -y0 * kTexels);
    number(KSI_SCALE, kTexels);
    w[KSI_FACE] = ~0u;
    w[KSI_HD_FACE] = ~0u;
    w[KSI_CLEAR] = 1;
    render::encode_sequence_quads(c.commands, quads);
    // KH2's screen over the 3D's clip space: its 512 x 416 dots the 4:3 picture's (x 256 its middle, y 208), whatever
    // the picture's width (a wider one shows what lies beyond), in front of everything
    KhdaysG3dClipVertex v[4]{};
    constexpr int corners[]{0, 1, 3, 2}; // top-left, top-right, bottom-right, bottom-left
    for (int k = 0; k < 4; ++k) {
        const int corner = corners[k];
        auto &o = v[k];
        const float x = (corner & 1) ? x1 : x0, y = (corner & 2) ? y1 : y0;
        o.x = (x - 256.0F) / 256.0F * 1024.0F;
        o.y = (208.0F - y) / 208.0F * 1024.0F;
        o.z = -0.99F * 1024.0F;
        o.w = 1024.0F;
        o.r = o.g = o.b = 63;
        o.a = 1;
        o.s = (corner & 1) ? 128.0F : 0.0F;
        o.t = (corner & 2) ? 128.0F : 0.0F;
    }
    constexpr unsigned kPolygonId = 61;
    khdays_g3d_sequence_polygon(v, 4, 0xc0u | (31u << 16) | (kPolygonId << 24), atlas.id, unsigned(c.token));
}

extern "C" void khdays_ui_kh2_pictures_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out)
{
    *out = {};
    for (const auto &c : canvases) {
        if (c.token < 0)
            continue;
        for (int k = 0; k < list->npolys; ++k)
            if ((list->polys[k].flags & KHDAYS_G3D_SEQUENCE) && list->polys[k].pltt == unsigned(c.token)) {
                *out = c.commands;
                return;
            }
    }
}
