#include "core/rooms/room_map.h"

#include "core/files/paths.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/message.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace studio {

namespace fs = std::filesystem;

namespace {

constexpr float kFinest = 0.4F;           // the game's finest map scale (Under the Tracks'), its others a multiple
constexpr int kMargin = 2;                // pixels kept clear around the floors
constexpr int kPanelTiles[2] = {15, 10};  // the panel's 120 x 80 pixels: the least a map holds
constexpr int kPictureTiles[2] = {RoomMap::kWidth / 8, RoomMap::kHeight / 8};
constexpr int kMainPalette = 13;         // bm_lo_bg000's sub-palette the game's maps show in
constexpr std::size_t kSmallLedge = 24;  // pixels: a ledge smaller is a prop on the floor, drawn as the floor

struct Floor {
    std::array<std::array<float, 2>, 3> at;  // x, z
    float height = 0;
};

}  // namespace

RoomMap room_map(const CollisionSource& source, float ground_limit, float step) {
    std::vector<Floor> floors;
    std::array<float, 2> lo{0, 0}, hi{0, 0};  // the origin held
    std::map<long, double> area_at;           // the floors' area by height, in quarters of a unit
    for (const auto& t : source.triangles) {
        const std::array<float, 3> u{t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]};
        const std::array<float, 3> v{t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
        const std::array<float, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length <= 0 || n[1] / length <= ground_limit)
            continue;
        Floor f;
        for (std::size_t c = 0; c < 3; ++c) {
            f.at[c] = {t[c][0], t[c][2]};
            lo = {std::min(lo[0], t[c][0]), std::min(lo[1], t[c][2])};
            hi = {std::max(hi[0], t[c][0]), std::max(hi[1], t[c][2])};
        }
        f.height = (t[0][1] + t[1][1] + t[2][1]) / 3;
        area_at[std::lround(f.height * 4)] += length / 2;
        floors.push_back(f);
    }
    float main = 0;
    double most = -1;
    for (const auto& [quarter, area] : area_at) {
        if (area > most) {
            most = area;
            main = static_cast<float>(quarter) / 4;
        }
    }
    RoomMap map;
    map.pixels.assign(static_cast<std::size_t>(RoomMap::kWidth) * RoomMap::kHeight, kMapNone);
    // the finest of the game's scales the floors fit at
    for (int k = 1;; ++k) {
        map.cell = kFinest * static_cast<float>(k);
        if ((hi[0] - lo[0]) / map.cell + 2 * kMargin <= RoomMap::kWidth &&
            (hi[1] - lo[1]) / map.cell + 2 * kMargin <= RoomMap::kHeight)
            break;
    }
    const auto tiles = [&](float extent, int axis) {
        const int held = static_cast<int>(std::ceil((extent / map.cell + 2 * kMargin) / 8));
        return std::clamp(held, kPanelTiles[axis], kPictureTiles[axis]);
    };
    map.width = tiles(hi[0] - lo[0], 0);
    map.height = tiles(hi[1] - lo[1], 1);
    map.origin_x = static_cast<int>(std::lround(map.width * 4 - (lo[0] + hi[0]) / 2 / map.cell));
    map.origin_y = static_cast<int>(std::lround(map.height * 4 - (lo[1] + hi[1]) / 2 / map.cell));
    // the floors from the lowest up, each pixel whose middle a floor covers
    std::sort(floors.begin(), floors.end(), [](const Floor& a, const Floor& b) { return a.height < b.height; });
    for (const Floor& f : floors) {
        std::array<std::array<float, 2>, 3> p{};
        float x0 = 1e30F, x1 = -1e30F, y0 = 1e30F, y1 = -1e30F;
        for (std::size_t c = 0; c < 3; ++c) {
            p[c] = {f.at[c][0] / map.cell + static_cast<float>(map.origin_x),
                    f.at[c][1] / map.cell + static_cast<float>(map.origin_y)};
            x0 = std::min(x0, p[c][0]), x1 = std::max(x1, p[c][0]);
            y0 = std::min(y0, p[c][1]), y1 = std::max(y1, p[c][1]);
        }
        const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
        if (area == 0)
            continue;
        const auto colour = f.height > main + step ? kMapAbove : kMapFloor;
        for (int y = std::max(0, static_cast<int>(std::floor(y0)));
             y <= std::min(RoomMap::kHeight - 1, static_cast<int>(y1)); ++y) {
            for (int x = std::max(0, static_cast<int>(std::floor(x0)));
                 x <= std::min(RoomMap::kWidth - 1, static_cast<int>(x1)); ++x) {
                const float mx = static_cast<float>(x) + 0.5F, my = static_cast<float>(y) + 0.5F;
                bool inside = true;
                for (std::size_t e = 0; e < 3 && inside; ++e) {
                    const auto& a = p[e];
                    const auto& b = p[(e + 1) % 3];
                    const float side = (b[0] - a[0]) * (my - a[1]) - (b[1] - a[1]) * (mx - a[0]);
                    inside = area > 0 ? side >= 0 : side <= 0;
                }
                if (inside)
                    map.pixels[static_cast<std::size_t>(y) * RoomMap::kWidth + static_cast<std::size_t>(x)] = colour;
            }
        }
    }
    // what the game's maps leave out: a ledge of a few pixels (a crate, a barrel) drawn as the floor under it, a pixel
    // the floors' seams missed filled
    constexpr std::array<std::array<int, 2>, 4> kBeside{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    const auto index = [](int x, int y) {
        return static_cast<std::size_t>(y) * RoomMap::kWidth + static_cast<std::size_t>(x);
    };
    const auto inside = [](int x, int y) { return x >= 0 && y >= 0 && x < RoomMap::kWidth && y < RoomMap::kHeight; };
    std::vector<char> seen(map.pixels.size(), 0);
    for (int y = 0; y < RoomMap::kHeight; ++y) {
        for (int x = 0; x < RoomMap::kWidth; ++x) {
            if (map.pixels[index(x, y)] != kMapAbove || seen[index(x, y)] != 0)
                continue;
            std::vector<std::array<int, 2>> ledge{{x, y}};
            seen[index(x, y)] = 1;
            for (std::size_t i = 0; i < ledge.size(); ++i) {
                for (const auto& [dx, dy] : kBeside) {
                    const int nx = ledge[i][0] + dx, ny = ledge[i][1] + dy;
                    if (inside(nx, ny) && seen[index(nx, ny)] == 0 && map.pixels[index(nx, ny)] == kMapAbove) {
                        seen[index(nx, ny)] = 1;
                        ledge.push_back({nx, ny});
                    }
                }
            }
            if (ledge.size() < kSmallLedge)
                for (const auto& [lx, ly] : ledge)
                    map.pixels[index(lx, ly)] = kMapFloor;
        }
    }
    {
        const std::vector<std::uint8_t> drawn = map.pixels;
        for (int y = 0; y < RoomMap::kHeight; ++y) {
            for (int x = 0; x < RoomMap::kWidth; ++x) {
                if (drawn[index(x, y)] != kMapNone)
                    continue;
                int around = 0;
                for (const auto& [dx, dy] : kBeside)
                    around += inside(x + dx, y + dy) && drawn[index(x + dx, y + dy)] != kMapNone ? 1 : 0;
                if (around >= 3)
                    map.pixels[index(x, y)] = kMapFloor;
            }
        }
    }
    // edges: a floor beside nothing, or above beside a lower one
    const std::vector<std::uint8_t> drawn = map.pixels;
    const auto at = [&](int x, int y) {
        return x < 0 || y < 0 || x >= RoomMap::kWidth || y >= RoomMap::kHeight
                   ? kMapNone
                   : drawn[static_cast<std::size_t>(y) * RoomMap::kWidth + static_cast<std::size_t>(x)];
    };
    for (int y = 0; y < RoomMap::kHeight; ++y) {
        for (int x = 0; x < RoomMap::kWidth; ++x) {
            const auto here = at(x, y);
            if (here == kMapNone)
                continue;
            for (const auto& [dx, dy] : kBeside) {
                const auto next = at(x + dx, y + dy);
                if (next == kMapNone || (here == kMapAbove && next == kMapFloor)) {
                    map.pixels[static_cast<std::size_t>(y) * RoomMap::kWidth + static_cast<std::size_t>(x)] = kMapEdge;
                    break;
                }
            }
        }
    }
    return map;
}

bool write_room_map(const RoomMap& map, const fs::path& folder, const std::string& name,
                    const std::vector<std::uint8_t>& palette, std::string* why) {
    std::vector<std::uint8_t> colours = palette;
    if (colours.size() < 16 * 3) {  // greys where the game's palette is not at hand
        colours.assign(16 * 3, 128);
        constexpr std::array<std::array<int, 2>, 4> kGreys{
            {{kMapEdge, 0}, {kMapAbove, 220}, {kMapFloor, 245}, {kMapNone, 255}}};
        for (const auto& [index, grey] : kGreys)
            for (int c = 0; c < 3; ++c)
                colours[static_cast<std::size_t>(index) * 3 + static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(grey);
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (const char* e = khdays_png_write_indexed(u8(folder / "map.png").c_str(), map.pixels.data(), RoomMap::kWidth,
                                                 RoomMap::kHeight, colours.data(), 16, 0)) {
        if (why != nullptr)
            *why = std::string("map.png: ") + e;
        return false;
    }
    std::ofstream text(folder / "map.txt", std::ios::binary | std::ios::trunc);
    text << "# The room's map on the lower screen (map.png, in the map's colours: 15 none, 14 a floor, 13 one above,\n"
            "# 10 an edge): where the room's x = z = 0 falls in it, the part the map holds (8-pixel tiles), the game\n"
            "# units a pixel spans, its name (name.de, .en, .es, .fr, .it for one language).\n"
         << "origin " << map.origin_x << " " << map.origin_y << "\n"
         << "size " << map.width << " " << map.height << "\n"
         << "cell " << map.cell << "\n"
         << "name " << name << "\n";
    if (!text) {
        if (why != nullptr)
            *why = "map.txt could not be written";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> room_map_palette(const fs::path& nitrofs) {
    std::ifstream in(nitrofs / "UI" / "btl" / "bm_lo_bg000.NCLR.z", std::ios::binary);
    const std::vector<std::uint8_t> packed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (packed.empty())
        return {};
    try {
        const auto data = khdays::assets::lz_decompress(packed);
        const auto nclr = khdays::assets::decode_nclr(data.data(), data.size());
        const std::size_t first = static_cast<std::size_t>(kMainPalette) * 16;
        if (nclr.colors.size() < first + 16)
            return {};
        std::vector<std::uint8_t> out;
        for (std::size_t i = 0; i < 16; ++i)
            for (std::size_t c = 0; c < 3; ++c)
                out.push_back(nclr.colors[first + i][c]);
        return out;
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace studio
