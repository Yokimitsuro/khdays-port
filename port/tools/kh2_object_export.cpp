// khdays-native --kh2-object <mdlx> <out.gltf> [--hd <folder>] [--picture <n> <file>]... [--pose <mset> <motion>]
// [--turn <degrees>]: a KH2 object of the player's 1.5+2.5 copy (obj/<name>.mdlx) made into a glTF model, its
// pictures beside it as PNG, for the KH2 prizes mod (tools/mods/kh2_prizes.py): in the first frame of one of its
// motions (a chest closed) or at rest, turned about the up axis. Headless: nothing of the game runs. On stdout
// one line of JSON: its bones, its size as made (the models' axes, KH2's units) and what was left out.
#include "khdays/assets/kh2_room.h"
extern "C" {
#include "../host/render/png_io.h"
}

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace assets = khdays::assets;

using Matrix = std::array<float, 16>; // column-major

Matrix trs(const assets::SceneNode &n)
{
    const float x = n.rotation[0], y = n.rotation[1], z = n.rotation[2], w = n.rotation[3];
    const std::array<float, 9> r{1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                                 2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                                 2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    Matrix m{};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            m[std::size_t(c * 4 + row)] = r[std::size_t(c * 3 + row)] * n.scale[std::size_t(c)];
    m[12] = n.translation[0];
    m[13] = n.translation[1];
    m[14] = n.translation[2];
    m[15] = 1;
    return m;
}

Matrix multiply(const Matrix &a, const Matrix &b)
{
    Matrix r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[std::size_t(c * 4 + row)] += a[std::size_t(k * 4 + row)] * b[std::size_t(c * 4 + k)];
    return r;
}

std::vector<std::uint8_t> read_file(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty())
        throw std::runtime_error("cannot read " + path.string());
    return data;
}

// Motion `name` of a motion set (obj/<name>.mset: a BAR of motions, each a BAR holding the motion itself).
assets::kh2::Motion motion_of(const fs::path &mset, const std::string &name)
{
    const auto data = read_file(mset);
    for (const auto &entry : assets::kh2::bar_entries(data.data(), data.size(), 0)) {
        if (entry.name != name || entry.size == 0 || !assets::kh2::is_bar(data.data(), data.size(), entry.offset))
            continue;
        auto motions = assets::kh2::read_motions(data.data(), data.size(), entry.offset);
        if (!motions.empty())
            return motions.front();
    }
    throw std::runtime_error(mset.string() + ": no motion " + name);
}

std::string json(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}
} // namespace

extern "C" int khdays_export_kh2_object(int argc, char **argv)
{
    try {
        if (argc < 2)
            throw std::runtime_error("<mdlx> <out.gltf> [--hd <folder>] [--picture <n> <file>]... "
                                     "[--pose <mset> <motion>] [--turn <degrees>]");
        const fs::path in = argv[0], out = argv[1];
        fs::path hd;
        assets::kh2::ObjectOptions options;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--hd") == 0 && i + 1 < argc) {
                hd = argv[++i];
            } else if (std::strcmp(argv[i], "--picture") == 0 && i + 2 < argc) {
                options.pictures[std::stoi(argv[i + 1])] = argv[i + 2];
                i += 2;
            } else if (std::strcmp(argv[i], "--pose") == 0 && i + 2 < argc) {
                options.pose = motion_of(argv[i + 1], argv[i + 2]);
                i += 2;
            } else if (std::strcmp(argv[i], "--turn") == 0 && i + 1 < argc) {
                options.turn = std::stof(argv[++i]);
            } else {
                throw std::runtime_error(std::string("not an option: ") + argv[i]);
            }
        }
        const auto data = read_file(in);
        std::string failed;
        const auto folder = out.parent_path();
        if (!folder.empty())
            fs::create_directories(folder);
        const auto made = assets::kh2::object_model(
            data.data(), data.size(), hd, in.stem().string(),
            [&](const std::string &name, const assets::DecodedTexture &picture) {
                const char *e = khdays_png_write((folder / name).string().c_str(), picture.rgba.data(), picture.width,
                                                 picture.height);
                if (e != nullptr && failed.empty())
                    failed = name + ": " + e;
            },
            options);
        if (!failed.empty())
            throw std::runtime_error("a picture cannot be written: " + failed);
        std::ofstream gltf(out, std::ios::binary);
        gltf << assets::scene_to_gltf(made.scene);
        if (!gltf)
            throw std::runtime_error("cannot write " + out.string());

        // its size at rest: each vertex where its joint's node puts it
        const auto &nodes = made.scene.nodes;
        std::vector<Matrix> world(nodes.size());
        for (std::size_t n = 0; n < nodes.size(); ++n) {
            const int parent = nodes[n].parent;
            world[n] = parent >= 0 && std::size_t(parent) < n ? multiply(world[std::size_t(parent)], trs(nodes[n]))
                                                              : trs(nodes[n]);
        }
        std::array<float, 3> lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
        for (const auto &mesh : made.scene.meshes)
            for (const auto &prim : mesh.primitives)
                for (std::size_t v = 0; v < prim.positions.size(); ++v) {
                    const int joint = v < prim.joint_of.size() ? mesh.joints[std::size_t(prim.joint_of[v])] : mesh.node;
                    const auto &m = world[std::size_t(joint)];
                    const auto &p = prim.positions[v];
                    for (int k = 0; k < 3; ++k) {
                        const float at = m[std::size_t(k)] * p[0] + m[std::size_t(4 + k)] * p[1] +
                                         m[std::size_t(8 + k)] * p[2] + m[std::size_t(12 + k)];
                        lo[std::size_t(k)] = std::fmin(lo[std::size_t(k)], at);
                        hi[std::size_t(k)] = std::fmax(hi[std::size_t(k)], at);
                    }
                }
        std::string report;
        for (const auto &line : made.report)
            report += (report.empty() ? "" : ",") + json(line);
        std::printf("{\"bones\":%zu,\"min\":[%g,%g,%g],\"max\":[%g,%g,%g],\"report\":[%s]}\n", nodes.size() - 1,
                    double(lo[0]), double(lo[1]), double(lo[2]), double(hi[0]), double(hi[1]), double(hi[2]),
                    report.c_str());
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "khdays-native --kh2-object: %s\n", e.what());
        return 1;
    }
}
