#include "khdays/assets/kh2_mdlx.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "khdays/assets/dds.h"
#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_map.h"

// A KH2 model's bones place its vertices: a bone's matrix is its parent's times
// T * Rz * Ry * Rx * S (as kh2_room.cpp's pieces). The rest pose is the file's;
// each vertex goes into the model's space there, the joints' palette is the
// identity at rest and moves with them (model_import.h).

namespace khdays::assets::kh2 {

namespace {

using Mat = std::array<float, 16>;  // column-major
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr float kMetres = 0.01F;  // KH2's units are centimetres; mods' models are in metres

Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k) {
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            }
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

Mat local(const Bone& bone) {
    const float cx = std::cos(bone.rotation[0]), sx = std::sin(bone.rotation[0]);
    const float cy = std::cos(bone.rotation[1]), sy = std::sin(bone.rotation[1]);
    const float cz = std::cos(bone.rotation[2]), sz = std::sin(bone.rotation[2]);
    // R = Rz * Ry * Rx, its columns scaled by S
    const std::array<float, 9> r{cz * cy,
                                 sz * cy,
                                 -sy,
                                 cz * sy * sx - sz * cx,
                                 sz * sy * sx + cz * cx,
                                 cy * sx,
                                 cz * sy * cx + sz * sx,
                                 sz * sy * cx - cz * sx,
                                 cy * cx};
    Mat m = kIdentity;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t row = 0; row < 3; ++row) {
            m[c * 4 + row] = r[c * 3 + row] * bone.scale[c];
        }
    }
    m[12] = bone.translation[0];
    m[13] = bone.translation[1];
    m[14] = bone.translation[2];
    return m;
}

// The inverse of a matrix whose last row is 0 0 0 1.
Mat inverse_affine(const Mat& m) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12F) {
        throw std::runtime_error(
            "KH2 model: a bone with no inverse (a scale of 0); the file is damaged: extract it again from your game");
    }
    const float k = 1.0F / det;
    Mat r = kIdentity;
    r[0] = (e * i - f * h) * k;
    r[4] = (c * h - b * i) * k;
    r[8] = (b * f - c * e) * k;
    r[1] = (f * g - d * i) * k;
    r[5] = (a * i - c * g) * k;
    r[9] = (c * d - a * f) * k;
    r[2] = (d * h - e * g) * k;
    r[6] = (b * g - a * h) * k;
    r[10] = (a * e - b * d) * k;
    for (std::size_t row = 0; row < 3; ++row) {
        r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    }
    return r;
}

std::array<float, 3> place(const Mat& m, const float x, const float y, const float z, const float w) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w,
            m[2] * x + m[6] * y + m[10] * z + m[14] * w};
}

// A texture's HD picture, when one took its place: its size, and its texels per one of the texture's region
struct HdPicture final {
    bool used = false;
    float width = 0.0F, height = 0.0F, scale_x = 1.0F, scale_y = 1.0F;
};

// The picture -<number>.png, or .dds, in `folder`. None there: nullopt. One that cannot be read is noted and left out.
std::optional<DecodedTexture> read_hd_picture(const std::filesystem::path& folder, const int number,
                                              std::vector<std::string>& notes) {
    for (const char* extension : {".png", ".dds"}) {
        const auto path = folder / ("-" + std::to_string(number) + extension);
        std::error_code absent;
        if (!std::filesystem::exists(path, absent)) {
            continue;
        }
        try {
            return std::string(extension) == ".png" ? read_image(path) : read_dds(path);
        } catch (const std::exception& e) {
            notes.push_back(path.filename().string() + " cannot be read (" + e.what() +
                            "), so it is left out; save it again as a PNG or a DXT5 or plain 32-bit DDS");
        }
    }
    return std::nullopt;
}

// RGBA `from` (w x h) at another size, by the nearest texels.
std::vector<std::uint8_t> resized(const std::uint8_t* from, const int w, const int h, const int to_w, const int to_h) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(to_w) * static_cast<std::size_t>(to_h) * 4U);
    for (int y = 0; y < to_h; ++y) {
        const int sy = (y * 2 + 1) * h / (to_h * 2);
        for (int x = 0; x < to_w; ++x) {
            const int sx = (x * 2 + 1) * w / (to_w * 2);
            std::copy_n(
                from + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx)) * 4U,
                4,
                out.data() +
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(to_w) + static_cast<std::size_t>(x)) * 4U);
        }
    }
    return out;
}

// An HD sheet's sprites, stacked upright as kh2_room.cpp's animate reads them, each at the block's size.
std::vector<std::vector<std::uint8_t>> sprites_of_sheet(const DecodedTexture& sheet, const int count, const int width,
                                                        const int height) {
    std::vector<std::vector<std::uint8_t>> sprites;
    const int rows = sheet.height / count;
    for (int s = 0; s < count; ++s) {
        sprites.push_back(
            resized(sheet.rgba.data() + static_cast<std::size_t>(s * rows) * static_cast<std::size_t>(sheet.width) * 4U,
                    sheet.width, rows, width, height));
    }
    return sprites;
}

// One corner as a mod's vertex: in the model's space at rest (metres), on up to
// four of its bones (the heaviest, weights summing to 1).
NeutralVertex corner(const Vertex& v, const std::vector<Mat>& world, const std::array<float, 2>& texels,
                     const GsTexture* hd_region) {
    const auto bone_world = [&world](const int bone) -> const Mat& {
        if (bone < 0 || static_cast<std::size_t>(bone) >= world.size()) {
            throw std::runtime_error("KH2 model: a vertex on bone " + std::to_string(bone) + " of " +
                                     std::to_string(world.size()));
        }
        return world[static_cast<std::size_t>(bone)];
    };
    NeutralVertex out;
    std::vector<std::pair<float, int>> shares;  // (weight, bone)
    std::array<float, 3> p{};
    if (v.influences.empty()) {
        p = place(bone_world(v.bone), v.position[0], v.position[1], v.position[2], 1.0F);
        shares.emplace_back(1.0F, v.bone);
    } else {
        for (const Influence& in : v.influences) {
            const auto q = place(bone_world(in.bone), in.position[0], in.position[1], in.position[2], in.position[3]);
            for (std::size_t i = 0; i < 3; ++i) {
                p[i] += q[i];
            }
            shares.emplace_back(in.position[3], in.bone);
        }
        std::sort(shares.begin(), shares.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        shares.resize(std::min<std::size_t>(shares.size(), 4U));
    }
    float total = 0.0F;
    for (const auto& s : shares) {
        total += s.first;
    }
    out.weights = {0.0F, 0.0F, 0.0F, 0.0F};
    for (std::size_t i = 0; i < shares.size(); ++i) {
        out.joints[i] = static_cast<std::uint32_t>(shares[i].second + 1);  // palette 0 is the identity
        out.weights[i] = total > 0.0F ? shares[i].first / total : (i == 0 ? 1.0F : 0.0F);
    }
    out.position = {p[0] * kMetres, p[1] * kMetres, p[2] * kMetres};
    // an HD picture is the texture's region alone (kh2_map.h region_uv)
    const std::array<float, 2> uv =
        hd_region != nullptr ? region_uv(*hd_region, v.uv[0], v.uv[1]) : std::array<float, 2>{v.uv[0], v.uv[1]};
    out.texcoord = {uv[0] * texels[0], uv[1] * texels[1]};
    for (std::size_t i = 0; i < 4; ++i) {
        out.color[i] = static_cast<std::uint8_t>(std::min(255, v.color[i] * 2));  // PS2's 0x80 = 1.0
    }
    return out;
}

}  // namespace

std::string mdlx_joint_name(const int bone) {
    return "kh2_" + std::to_string(bone);
}

ImportedModel import_mdlx(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    in.seekg(0, std::ios::end);  // in one go: an istreambuf_iterator reads a byte at a time
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<std::streamoff>(0, in.tellg())));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return import_mdlx(bytes.data(), bytes.size(), path.stem().string(), path.parent_path() / path.stem());
}

ImportedModel import_mdlx(const std::uint8_t* data, const std::size_t size, const std::string& name,
                          const std::filesystem::path& hd_folder) {
    const auto entries = bar_entries(data, size);
    const BarEntry* model_entry = nullptr;
    const BarEntry* texture_entry = nullptr;
    for (const BarEntry& e : entries) {
        if (e.type == kEntryModel && e.size != 0 && model_entry == nullptr) {
            model_entry = &e;
        } else if (e.type == kEntryTextures && e.size != 0 && texture_entry == nullptr) {
            texture_entry = &e;
        }
    }
    if (model_entry == nullptr) {
        throw std::runtime_error("KH2 model: the BAR has no model entry (type 4); name a KH2 obj/*.mdlx");
    }
    const Model model = read_model(data, size, model_entry->offset);
    if (model.kind != 3) {
        throw std::runtime_error("KH2 model: a map model (kind " + std::to_string(model.kind) +
                                 "), not a character's or an object's; name one from KH2's obj/ folder");
    }

    ImportedModel result;
    result.model.name = name;
    result.model.palette.push_back(kIdentity);

    // the bones at rest, parents first as the file lists them
    std::vector<Mat> world;
    world.reserve(model.bones.size());
    for (std::size_t b = 0; b < model.bones.size(); ++b) {
        const int parent = model.bones[b].parent;
        if (parent >= static_cast<int>(b)) {
            throw std::runtime_error("KH2 model: bone " + std::to_string(b) + "'s parent " + std::to_string(parent) +
                                     " comes after it");
        }
        const Mat m = local(model.bones[b]);
        world.push_back(parent >= 0 ? mul(world[static_cast<std::size_t>(parent)], m) : m);
    }
    for (std::size_t b = 0; b < world.size(); ++b) {
        // in metres: the same turn, its place scaled
        Mat w = world[b];
        w[12] *= kMetres;
        w[13] *= kMetres;
        w[14] *= kMetres;
        ImportedJoint joint;
        joint.palette_index = static_cast<std::uint32_t>(result.model.palette.size());
        joint.name = mdlx_joint_name(static_cast<int>(b));
        joint.world = w;
        joint.inverse_bind = inverse_affine(w);
        joint.parent = model.bones[b].parent;
        joint.no_envelope = model.bones[b].no_envelope;
        joint.length = model.bones[b].length * kMetres;
        result.joints.push_back(joint);
        result.model.palette.push_back(kIdentity);
    }

    // the textures: GS infos decoded, each named by its index
    TextureContainer container;
    std::vector<HdPicture> hd;
    if (texture_entry != nullptr) {
        container = read_textures(data, size, texture_entry->offset);
        TexturePictures pictures(data, size, texture_entry->offset);
        std::error_code no_folder;
        const bool has_hd = !hd_folder.empty() && std::filesystem::is_directory(hd_folder, no_folder);
        int hd_pictures = 0, hd_sheets = 0;
        hd.resize(container.textures.size());
        // the HD pictures, then the animations' sprite sheets, read and decoded on several threads at once (each
        // its own file; a model has a few dozen, a DXT5 1024 x 1024 several ms to decode), notes and failures in order
        const std::size_t count = container.textures.size();
        const std::size_t all = count + container.animations.size();
        std::vector<std::optional<DecodedTexture>> bigs(all);
        std::vector<std::vector<std::string>> big_notes(all);
        std::vector<std::exception_ptr> big_errors(all);
        if (has_hd && all > 0) {
            const unsigned threads =
                std::max(1U, std::min({std::thread::hardware_concurrency(), 8U, static_cast<unsigned>(all)}));
            std::atomic<std::size_t> next{0};
            const auto work = [&] {
                for (std::size_t t = next++; t < all; t = next++) {
                    try {
                        const int number =
                            t < count ? container.hd_pictures[t] : container.animations[t - count].hd_sheet;
                        if (t < count || number >= 0) {
                            bigs[t] = read_hd_picture(hd_folder, number, big_notes[t]);
                        }
                    } catch (...) {
                        big_errors[t] = std::current_exception();
                    }
                }
            };
            std::vector<std::thread> pool;
            for (unsigned i = 1; i < threads; ++i) {
                pool.emplace_back(work);
            }
            work();
            for (std::thread& th : pool) {
                th.join();
            }
        }
        for (std::size_t t = 0; t < container.textures.size(); ++t) {
            const std::string key = "tex" + std::to_string(t);
            result.notes.insert(result.notes.end(), big_notes[t].begin(), big_notes[t].end());
            try {
                if (big_errors[t]) {
                    std::rethrow_exception(big_errors[t]);
                }
                auto big = std::move(bigs[t]);
                if (big) {
                    const GsTexture& gs = container.textures[t];
                    hd[t] = {true, static_cast<float>(big->width), static_cast<float>(big->height),
                             static_cast<float>(big->width) / static_cast<float>(gs.region_width),
                             static_cast<float>(big->height) / static_cast<float>(gs.region_height)};
                    ++hd_pictures;
                }
                DecodedTexture picture = big ? std::move(*big) : pictures.picture(t);
                picture.name = key;
                result.textures.emplace_back(key, std::move(picture));
            } catch (const std::exception& e) {
                result.missing_textures.emplace_back(key, e.what());
            }
        }
        // the textures' animated parts (a face's eyes and mouth), their sprites decoded through the palette
        for (std::size_t n = 0; n < container.animations.size(); ++n) {
            const TextureAnimation& a = container.animations[n];
            ImportedTextureAnimation out;
            out.texture = "tex" + std::to_string(a.target);
            out.x = a.x;
            out.y = a.y;
            out.width = a.width;
            out.height = a.height;
            // over an HD picture: the block at its scale, in its region
            const bool over_hd = a.target >= 0 && static_cast<std::size_t>(a.target) < hd.size() &&
                                 hd[static_cast<std::size_t>(a.target)].used;
            if (over_hd) {
                const HdPicture& big = hd[static_cast<std::size_t>(a.target)];
                const GsTexture& gs = container.textures[static_cast<std::size_t>(a.target)];
                out.x = static_cast<int>(std::lround(static_cast<float>(a.x - gs.region_x) * big.scale_x));
                out.y = static_cast<int>(std::lround(static_cast<float>(a.y - gs.region_y) * big.scale_y));
                out.width = std::max(1, static_cast<int>(std::lround(static_cast<float>(a.width) * big.scale_x)));
                out.height = std::max(1, static_cast<int>(std::lround(static_cast<float>(a.height) * big.scale_y)));
            }
            out.default_script = a.default_animation;
            out.first_slot = a.first_slot;
            out.slot_script = a.slot_animation;
            for (const auto& frames : a.animations) {
                std::vector<ImportedTextureAnimation::Step> script;
                for (const TextureFrame& f : frames) {
                    using Kind = ImportedTextureAnimation::Step::Kind;
                    ImportedTextureAnimation::Step s;
                    s.kind = f.kind == TextureFrame::Kind::Sprite ? Kind::sprite
                             : f.kind == TextureFrame::Kind::Base ? Kind::base
                             : f.kind == TextureFrame::Kind::Jump ? Kind::jump
                                                                  : Kind::stop;
                    s.jump = f.jump;
                    s.min_frames = f.min_length;
                    s.max_frames = f.max_length;
                    s.sprite = f.sprite;
                    script.push_back(s);
                }
                out.scripts.push_back(std::move(script));
            }
            result.notes.insert(result.notes.end(), big_notes[count + n].begin(), big_notes[count + n].end());
            if (big_errors[count + n]) {
                std::rethrow_exception(big_errors[count + n]);
            }
            auto sheet = std::move(bigs[count + n]);
            if (sheet && (a.sprite_count <= 0 || sheet->height % a.sprite_count != 0)) {
                result.notes.push_back("-" + std::to_string(a.hd_sheet) + ": " + std::to_string(sheet->height) +
                                       " rows do not divide into its " + std::to_string(a.sprite_count) +
                                       " sprites stacked upright, so the model's own are used; fix the sheet's height");
                sheet.reset();
            }
            try {
                if (sheet) {
                    out.sprites = sprites_of_sheet(*sheet, a.sprite_count, out.width, out.height);
                    ++hd_sheets;
                } else {
                    out.sprites = pictures.sprites(a);
                    if (over_hd) {
                        for (auto& sprite : out.sprites) {
                            sprite = resized(sprite.data(), a.width, a.height, out.width, out.height);
                        }
                    }
                }
            } catch (const std::exception& e) {
                result.missing_textures.emplace_back(out.texture + " (its animated part)", e.what());
                continue;
            }
            result.texture_animations.push_back(std::move(out));
        }
        if (hd_pictures + hd_sheets > 0) {
            result.notes.push_back(std::to_string(hd_pictures) + " of " + std::to_string(container.textures.size()) +
                                   " pictures and " + std::to_string(hd_sheets) + " of " +
                                   std::to_string(container.animations.size()) + " sprite sheets from " +
                                   hd_folder.string());
        }
    }

    // a mesh per subpart, and per face kind: one-sided, and seen from both sides
    for (std::size_t g = 0; g < model.groups.size(); ++g) {
        const Group& group = model.groups[g];
        const bool textured = group.texture >= 0 && static_cast<std::size_t>(group.texture) < container.textures.size();
        std::array<float, 2> texels{1.0F, 1.0F};
        const GsTexture* hd_region = nullptr;
        if (textured) {
            const GsTexture& t = container.textures[static_cast<std::size_t>(group.texture)];
            const HdPicture& big = hd[static_cast<std::size_t>(group.texture)];
            texels = {big.used ? big.width : static_cast<float>(t.width),
                      big.used ? big.height : static_cast<float>(t.height)};
            hd_region = big.used ? &t : nullptr;
        }
        for (const bool two_sided : {false, true}) {
            NeutralMesh mesh;
            mesh.name = name + "_" + std::to_string(g) + (two_sided ? "_both" : "");
            mesh.texture_name = textured ? "tex" + std::to_string(group.texture) : std::string{};
            mesh.material_name = mesh.name;
            mesh.double_sided = two_sided;
            // bit 1: translucent; otherwise drawn where the texture is opaque enough
            mesh.alpha_mode =
                (group.attribute & 2U) != 0 ? NeutralMesh::AlphaMode::blend : NeutralMesh::AlphaMode::mask;
            for (const Triangle& t : group.triangles) {
                if (t.two_sided != two_sided) {
                    continue;
                }
                for (const Vertex& v : t.corners) {
                    mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(corner(v, world, texels, hd_region));
                }
            }
            if (mesh.vertices.empty()) {
                continue;
            }
            result.model.header_vertex_count += static_cast<std::uint16_t>(mesh.vertices.size());
            result.model.meshes.push_back(std::move(mesh));
        }
    }
    if (result.model.meshes.empty()) {
        throw std::runtime_error("KH2 model: no triangles; name a KH2 obj/*.mdlx that draws something");
    }
    return result;
}

}  // namespace khdays::assets::kh2
