#include "khdays/assets/kh2_room.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "khdays/assets/dds.h"
#include "khdays/assets/kh2_map.h"

// KH2's map models are drawn as stored (one face, counter-clockwise from the
// front, as glTF's; a strip vertex flag 0x00 both faces), their texels cut by
// their alpha (the PS2's alpha test) unless their group is translucent (blended).
// Leaves, fences and railings are cut out in groups not marked transparent.
// Placements are in KH2's world axes, (x, -y, -z) of the models': a piece's
// place is turned into the models' axes, its bones and motion are in them already.

namespace khdays::assets::kh2 {

namespace {

// A UVSC speed read as 2^31 = a texture's width per game frame (the values are
// 2^31 / N: 13421.77 = 2^31/160000, 2236962.25 = 2^31/960); not yet compared
// with KH2 in motion.
constexpr float kScrollUnit = 2147483648.0F;
// A piece placed this far down waits for an event to be shown (y 1,000,000 seen).
constexpr float kHiddenHeight = 100000.0F;

// Turns about x, then y, then z (radians) as a quaternion x y z w.
std::array<float, 4> quaternion(const float x, const float y, const float z) {
    const float cx = std::cos(x / 2), sx = std::sin(x / 2);
    const float cy = std::cos(y / 2), sy = std::sin(y / 2);
    const float cz = std::cos(z / 2), sz = std::sin(z / 2);
    return {cz * cy * sx - sz * sy * cx, cz * sy * cx + sz * cy * sx, sz * cy * cx - cz * sy * sx,
            cz * cy * cx + sz * sy * sx};
}

// A PS2 colour (0x80 = 1.0) as glTF's (255 = 1.0); brighter than 1.0 is kept at 1.0.
std::array<std::uint8_t, 4> gltf_color(const std::array<std::uint8_t, 4>& c) {
    std::array<std::uint8_t, 4> out{};
    for (std::size_t i = 0; i < 4; ++i)
        out[i] = static_cast<std::uint8_t>(std::min(255, c[i] * 2));
    return out;
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}

struct Part final {
    MapPart info;
    Model model;
    TextureContainer textures;
};

// The part's GS info `texture`, or nullptr when its container has none of that number.
const GsTexture* gs_of(const Part& part, int texture) {
    if (texture < 0 || static_cast<std::size_t>(texture) >= part.textures.textures.size())
        return nullptr;
    return &part.textures.textures[static_cast<std::size_t>(texture)];
}

// A GS info's picture as the scene uses it.
struct Picture final {
    std::string file;  // empty: none (vertex colours only)
    bool translucent = false;
    bool cut = false;  // some texels clear (alpha under half): cut out, else drawn whole
    std::vector<std::string> frame_pictures;
    std::vector<std::array<int, 2>> frames;
    bool frames_loop = true;
    int frames_loop_from = 0;
};

class Maker final {
public:
    Maker(const std::uint8_t* data, std::size_t size, const std::filesystem::path& hd, const std::string& name,
          const PictureSink& sink)
        : data_(data), size_(size), hd_(hd), name_(name), sink_(sink) {}

    Room run() {
        const MapFile map = read_map(data_, size_);
        for (const MapPart& info : map.parts) {
            Part part;
            part.info = info;
            part.model = read_model(data_, size_, map.entries[info.model_entry].offset);
            part.textures = read_textures(data_, size_, map.entries[info.texture_entry].offset);
            parts_.push_back(std::move(part));
        }
        room_.scene.name = name_;
        room_.scene.nodes.push_back({"room"});
        std::vector<std::size_t> pieces;
        for (std::size_t p = 0; p < parts_.size(); ++p) {
            if (parts_[p].info.name == "BOB") {
                pieces.push_back(p);
                continue;
            }
            const std::string& n = parts_[p].info.name;
            SceneNode node;
            node.name = n == "MAP" ? "background" : lower(n);
            node.parent = 0;
            node.sky = n.rfind("SK", 0) == 0;
            room_.scene.nodes.push_back(node);
            add_mesh(p, static_cast<int>(room_.scene.nodes.size() - 1), {}, node.name);
        }
        std::vector<Placement> rows;
        if (map.placements)
            rows = read_placements(data_, size_, map.entries[*map.placements].offset);
        std::vector<bool> placed(pieces.size(), false);
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const Placement& row = rows[r];
            if (row.piece < 0 || static_cast<std::size_t>(row.piece) >= pieces.size()) {
                note("placement " + std::to_string(r) + ": piece " + std::to_string(row.piece) + " not in the map");
                continue;
            }
            placed[static_cast<std::size_t>(row.piece)] = true;
            if (row.motion < 0 && std::fabs(row.position[1]) >= kHiddenHeight) {
                note("piece " + std::to_string(row.piece) + " (placement " + std::to_string(r) +
                     "): hidden until an event, left out");
                continue;
            }
            add_piece(pieces[static_cast<std::size_t>(row.piece)], row, r, map);
        }
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (!placed[i])
                note("piece " + std::to_string(i) + ": placed nowhere, left out");
        }
        return std::move(room_);
    }

    // An object's .mdlx (a BAR of its model and its textures): the object at rest, a node per bone under its own.
    Room object(const ObjectOptions& options) {
        pictures_in_place_ = options.pictures;
        const auto entries = bar_entries(data_, size_, 0);
        const auto first = [&](std::uint32_t type) {
            return std::find_if(entries.begin(), entries.end(),
                                [type](const BarEntry& e) { return e.type == type && e.size != 0; });
        };
        const auto model = first(kEntryModel);
        const auto textures = first(kEntryTextures);
        if (model == entries.end() || textures == entries.end())
            throw std::runtime_error("KH2 object: no model (entry 0x04) or no textures (entry 0x07)");
        Part part;
        part.info.name = name_;
        part.model = read_model(data_, size_, model->offset);
        if (part.model.kind != 3)
            throw std::runtime_error("KH2 object: model kind " + std::to_string(part.model.kind) +
                                     " (an object's is 3)");
        part.textures = read_textures(data_, size_, textures->offset);
        parts_.push_back(std::move(part));
        room_.scene.name = name_;
        SceneNode object;
        object.name = lower(name_);
        object.rotation = quaternion(0.0F, options.turn * 3.14159265F / 180.0F, 0.0F);
        room_.scene.nodes.push_back(object);
        std::vector<int> joints;
        const auto& bones = parts_[0].model.bones;
        const Motion* pose = options.pose ? &*options.pose : nullptr;
        for (std::size_t b = 0; b < bones.size(); ++b) {
            SceneNode joint;
            joint.name = "j" + std::to_string(b);
            const int parent = bones[b].parent;
            joint.parent =
                parent >= 0 && static_cast<std::size_t>(parent) < b ? joints[static_cast<std::size_t>(parent)] : 0;
            const auto ch = channels(bones[b], pose, static_cast<int>(b), pose ? pose->start : 0.0F);
            joint.scale = {ch[0], ch[1], ch[2]};
            joint.rotation = quaternion(ch[3], ch[4], ch[5]);
            joint.translation = {ch[6], ch[7], ch[8]};
            room_.scene.nodes.push_back(joint);
            joints.push_back(static_cast<int>(room_.scene.nodes.size() - 1));
        }
        if (joints.empty())
            throw std::runtime_error("KH2 object: no bones");
        add_mesh(0, 0, joints, lower(name_));
        return std::move(room_);
    }

private:
    void note(const std::string& text) {
        if (std::find(room_.report.begin(), room_.report.end(), text) == room_.report.end())
            room_.report.push_back(text);
    }

    std::optional<UvScroll> scroll(std::size_t p, int index) const {
        // the part's own container first, then the map's others in order (whether
        // the index is the map's or the container's is not known yet)
        for (const auto& s : parts_[p].textures.scrolls) {
            if (static_cast<int>(s.index) == index)
                return s;
        }
        for (const Part& other : parts_) {
            for (const auto& s : other.textures.scrolls) {
                if (static_cast<int>(s.index) == index)
                    return s;
            }
        }
        return std::nullopt;
    }

    // The picture of part p's GS info i: its HD picture as a PNG, and its texture
    // animation's pictures and frames.
    const Picture& picture(std::size_t p, int i) {
        const auto key = std::make_pair(p, i);
        const auto found = pictures_.find(key);
        if (found != pictures_.end())
            return found->second;
        Picture& pic = pictures_[key];
        const Part& part = parts_[p];
        const std::string where = lower(part.info.name) + " texture " + std::to_string(i);
        const GsTexture* gs = gs_of(part, i);
        if (gs == nullptr) {
            note(where + ": not in its container, vertex colours only");
            return pic;
        }
        const int hd = part.info.first_hd_picture + part.textures.hd_pictures[static_cast<std::size_t>(i)];
        DecodedTexture base;
        try {
            const auto in_place = pictures_in_place_.find(hd);
            base = read_dds(in_place != pictures_in_place_.end() ? in_place->second
                                                                 : hd_ / ("-" + std::to_string(hd) + ".dds"));
        } catch (const std::exception& e) {
            note(where + ": no HD picture (-" + std::to_string(hd) + ".dds: " + e.what() + "), vertex colours only");
            return pic;
        }
        pic.file = name_ + "_" + std::to_string(hd) + ".png";
        pic.translucent = texels_translucent(base.rgba.data(), base.rgba.size() / 4);
        for (std::size_t k = 3; k < base.rgba.size() && !pic.cut; k += 4)
            pic.cut = base.rgba[k] < 128;
        sink_(pic.file, base);

        const TextureAnimation* anim = nullptr;
        for (const auto& a : part.textures.animations) {
            if (a.target != i)
                continue;
            if (anim == nullptr)
                anim = &a;
            else
                note(where + ": several texture animations, the first played");
        }
        if (anim != nullptr && !anim->animations.empty() && anim->sprite_count > 0)
            animate(pic, part, *gs, *anim, base, hd, where);
        return pic;
    }

    // A texture animation: the base picture with each of its sprites over it, a PNG each, and the
    // frames that show them in turn.
    void animate(Picture& pic, const Part& part, const GsTexture& gs, const TextureAnimation& a,
                 const DecodedTexture& base, int hd, const std::string& where) {
        DecodedTexture sheet;
        const int sheet_hd = part.info.first_hd_picture + a.hd_sheet;
        try {
            sheet = read_dds(hd_ / ("-" + std::to_string(sheet_hd) + ".dds"));
        } catch (const std::exception& e) {
            note(where + ": no HD sprites (-" + std::to_string(sheet_hd) + ".dds: " + e.what() + "), still");
            return;
        }
        // the sprites stacked upright at the picture's scale, drawn over its region
        const float sx = static_cast<float>(base.width) / static_cast<float>(std::max(1, gs.region_width));
        const float sy = static_cast<float>(base.height) / static_cast<float>(std::max(1, gs.region_height));
        const int sprite_h = sheet.height / a.sprite_count;
        const int at_x = static_cast<int>(std::lround(static_cast<float>(a.x - gs.region_x) * sx));
        const int at_y = static_cast<int>(std::lround(static_cast<float>(a.y - gs.region_y) * sy));
        pic.frame_pictures.push_back(pic.file);
        for (int s = 0; s < a.sprite_count; ++s) {
            DecodedTexture frame = base;
            for (int y = 0; y < sprite_h; ++y) {
                for (int x = 0; x < sheet.width; ++x) {
                    const int tx = at_x + x;
                    const int ty = at_y + y;
                    if (tx < 0 || ty < 0 || tx >= frame.width || ty >= frame.height)
                        continue;
                    const std::size_t from = (static_cast<std::size_t>(s * sprite_h + y) * sheet.width + x) * 4;
                    const std::size_t to = (static_cast<std::size_t>(ty) * frame.width + tx) * 4;
                    std::copy(sheet.rgba.begin() + static_cast<std::ptrdiff_t>(from),
                              sheet.rgba.begin() + static_cast<std::ptrdiff_t>(from + 4),
                              frame.rgba.begin() + static_cast<std::ptrdiff_t>(to));
                }
            }
            const std::string file = name_ + "_" + std::to_string(hd) + "_s" + std::to_string(s) + ".png";
            sink_(file, frame);
            pic.frame_pictures.push_back(file);
        }
        const auto chosen = static_cast<std::size_t>(a.default_animation) < a.animations.size()
                                ? static_cast<std::size_t>(a.default_animation)
                                : 0;
        const auto& steps = a.animations[chosen];
        pic.frames_loop = true;
        for (std::size_t k = 0; k < steps.size(); ++k) {
            const TextureFrame& f = steps[k];
            if (f.kind == TextureFrame::Kind::Jump) {
                const long to = static_cast<long>(k) + f.jump;
                const long last = static_cast<long>(pic.frames.size()) - 1;
                pic.frames_loop_from = static_cast<int>(to < 0 || last < 0 ? 0 : std::min(to, last));
                break;
            }
            if (f.kind == TextureFrame::Kind::Stop) {
                pic.frames_loop = false;
                break;
            }
            if (f.min_length != f.max_length)
                note(where + ": frames of random length drawn at their middle");
            const int length = std::max(1, (f.min_length + f.max_length + 1) / 2);
            const int shown = f.kind == TextureFrame::Kind::Sprite && f.sprite < a.sprite_count ? 1 + f.sprite : 0;
            pic.frames.push_back({shown, length});
        }
    }

    // A light group's picture (its light added over the model in KH2): each texel's alpha its brightness, so
    // that blended its black adds nothing and its colours show as light.
    const Picture& light_picture(std::size_t p, int i) {
        const auto key = std::make_pair(p, -1 - i);
        const auto found = pictures_.find(key);
        if (found != pictures_.end())
            return found->second;
        Picture& pic = pictures_[key];
        const Part& part = parts_[p];
        const GsTexture* gs = gs_of(part, i);
        if (gs == nullptr)
            return pic;
        const int hd = part.info.first_hd_picture + part.textures.hd_pictures[static_cast<std::size_t>(i)];
        DecodedTexture light;
        try {
            const auto in_place = pictures_in_place_.find(hd);
            light = read_dds(in_place != pictures_in_place_.end() ? in_place->second
                                                                  : hd_ / ("-" + std::to_string(hd) + ".dds"));
        } catch (const std::exception& e) {
            note(lower(part.info.name) + " texture " + std::to_string(i) + ": no HD picture for its light (" +
                 e.what() + "), left out");
            return pic;
        }
        for (std::size_t k = 0; k + 3 < light.rgba.size(); k += 4)
            light.rgba[k + 3] = std::max({light.rgba[k], light.rgba[k + 1], light.rgba[k + 2]});
        pic.file = name_ + "_" + std::to_string(hd) + "_light.png";
        pic.translucent = true;
        sink_(pic.file, light);
        note(lower(part.info.name) + " texture " + std::to_string(i) +
             ": its light (added over the model in KH2) drawn blended by its brightness");
        return pic;
    }

    int material(std::size_t p, const Group& g, bool two_sided, bool vertex_alpha) {
        const int scroll_index = g.scroll_index().value_or(-1);
        const auto key =
            std::make_tuple(p, g.light ? -1 - g.texture : g.texture, g.transparent, two_sided, scroll_index);
        const auto found = materials_.find(key);
        auto upgrade = [&](int m) {
            // a translucent group's cut-out surface whose vertex colours fade: blended
            SceneMaterial& mat = room_.scene.materials[static_cast<std::size_t>(m)];
            if (vertex_alpha && g.transparent && mat.alpha_mode == NeutralMesh::AlphaMode::mask)
                mat.alpha_mode = NeutralMesh::AlphaMode::blend;
            return m;
        };
        if (found != materials_.end())
            return upgrade(found->second);
        const Picture& pic = g.light ? light_picture(p, g.texture) : picture(p, g.texture);
        const Part& part = parts_[p];
        SceneMaterial mat;
        mat.name = lower(part.info.name) + "_t" + std::to_string(g.texture) + (g.transparent ? "_cut" : "") +
                   (two_sided ? "_2" : "") + (scroll_index >= 0 ? "_scroll" + std::to_string(scroll_index) : "");
        mat.texture = pic.file;
        mat.double_sided = two_sided;
        mat.alpha_mode = g.light           ? NeutralMesh::AlphaMode::blend
                         : !g.transparent  ? (pic.cut ? NeutralMesh::AlphaMode::mask : NeutralMesh::AlphaMode::opaque)
                         : pic.translucent ? NeutralMesh::AlphaMode::blend
                                           : NeutralMesh::AlphaMode::mask;
        const GsTexture* gs = gs_of(part, g.texture);
        if (gs != nullptr) {
            mat.repeat = {gs->wrap_u == 0 || gs->wrap_u == 3, gs->wrap_v == 0 || gs->wrap_v == 3};
        }
        if (scroll_index >= 0) {
            const auto s = scroll(p, scroll_index);
            if (!s) {
                note(lower(part.info.name) + ": scroll " + std::to_string(scroll_index) + " has no speed, left still");
            } else if (gs != nullptr) {
                // a whole texture's fraction per frame, as the region's (what the HD picture covers)
                mat.scroll = {s->u / kScrollUnit * static_cast<float>(gs->width) / static_cast<float>(gs->region_width),
                              s->v / kScrollUnit * static_cast<float>(gs->height) /
                                  static_cast<float>(gs->region_height)};
            }
        }
        mat.frame_pictures = pic.frame_pictures;
        mat.frames = pic.frames;
        mat.frames_loop = pic.frames_loop;
        mat.frames_loop_from = pic.frames_loop_from;
        room_.scene.materials.push_back(std::move(mat));
        const int index = static_cast<int>(room_.scene.materials.size() - 1);
        materials_[key] = index;
        return upgrade(index);
    }

    // Part p's triangles as a mesh on `node`; skinned to `joints` (a piece's bones) when given.
    void add_mesh(std::size_t p, int node, const std::vector<int>& joints, const std::string& name) {
        const Part& part = parts_[p];
        std::map<int, ScenePrimitive> primitives;
        for (const Group& g : part.model.groups) {
            if (g.triangles.empty())
                continue;
            for (const bool two_sided : {false, true}) {
                bool any = false;
                bool fading = false;
                for (const Triangle& t : g.triangles) {
                    if (t.two_sided != two_sided)
                        continue;
                    any = true;
                    for (const Vertex& v : t.corners)
                        fading = fading || v.color[3] < 0x7C;
                }
                if (!any)
                    continue;
                const int m = material(p, g, two_sided, fading);
                ScenePrimitive& prim = primitives[m];
                prim.material = m;
                const GsTexture* gs = gs_of(part, g.texture);
                for (const Triangle& t : g.triangles) {
                    if (t.two_sided != two_sided)
                        continue;
                    for (const Vertex& v : t.corners) {
                        prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
                        prim.positions.push_back(v.position);
                        prim.uvs.push_back(gs != nullptr ? region_uv(*gs, v.uv[0], v.uv[1]) : v.uv);
                        prim.colors.push_back(gltf_color(v.color));
                        if (!joints.empty())
                            prim.joint_of.push_back(
                                v.bone >= 0 && static_cast<std::size_t>(v.bone) < joints.size() ? v.bone : 0);
                    }
                }
            }
        }
        SceneMesh mesh;
        mesh.name = name;
        mesh.node = node;
        mesh.joints = joints;
        for (auto& [m, prim] : primitives)
            mesh.primitives.push_back(std::move(prim));
        room_.scene.meshes.push_back(std::move(mesh));
    }

    void add_piece(std::size_t p, const Placement& row, std::size_t r, const MapFile& map) {
        const Part& part = parts_[p];
        const std::string name = "bob" + std::to_string(r);
        SceneNode node;
        node.name = name;
        node.parent = 0;
        // KH2's world axes to the models'
        node.translation = {row.position[0], -row.position[1], -row.position[2]};
        node.rotation = quaternion(row.rotation[0], -row.rotation[1], -row.rotation[2]);
        node.scale = row.scale;
        room_.scene.nodes.push_back(node);
        const int at = static_cast<int>(room_.scene.nodes.size() - 1);

        // its motion, when the row plays one
        std::optional<Motion> motion;
        if (row.motion >= 0) {
            if (!part.info.motion_entry) {
                note(name + ": motion " + std::to_string(row.motion) + " but the piece has none, still");
            } else {
                auto motions = read_motions(data_, size_, map.entries[*part.info.motion_entry].offset);
                if (static_cast<std::size_t>(row.motion) < motions.size())
                    motion = std::move(motions[static_cast<std::size_t>(row.motion)]);
                else
                    note(name + ": motion " + std::to_string(row.motion) + " not among its motions, still");
            }
        }
        if (motion && motion->has_constraints)
            note(name + ": its motion's constraints are not played");

        // a node per bone, under its parent's
        std::vector<int> joints;
        const auto& bones = part.model.bones;
        for (std::size_t b = 0; b < bones.size(); ++b) {
            SceneNode joint;
            joint.name = name + "_j" + std::to_string(b);
            const int parent = bones[b].parent;
            joint.parent =
                parent >= 0 && static_cast<std::size_t>(parent) < b ? joints[static_cast<std::size_t>(parent)] : at;
            const auto pose =
                channels(bones[b], motion ? &*motion : nullptr, static_cast<int>(b), motion ? motion->start : 0.0F);
            joint.scale = {pose[0], pose[1], pose[2]};
            joint.rotation = quaternion(pose[3], pose[4], pose[5]);
            joint.translation = {pose[6], pose[7], pose[8]};
            room_.scene.nodes.push_back(joint);
            joints.push_back(static_cast<int>(room_.scene.nodes.size() - 1));
        }
        if (joints.empty()) {
            note(name + ": no bones, left out");
            return;
        }
        add_mesh(p, at, joints, name);
        if (motion)
            add_animation(*motion, part.model, joints, name);
    }

    // A bone's nine channels (scale, rotation, translation; x y z each) at
    // `time` of `motion`: its curves, else the motion's first pose, else its rest.
    static std::array<float, 9> channels(const Bone& bone, const Motion* motion, int joint, float time) {
        std::array<float, 9> ch{bone.scale[0],       bone.scale[1],       bone.scale[2],
                                bone.rotation[0],    bone.rotation[1],    bone.rotation[2],
                                bone.translation[0], bone.translation[1], bone.translation[2]};
        if (motion == nullptr)
            return ch;
        for (const PoseValue& v : motion->pose) {
            if (v.joint == joint && v.channel >= 0 && v.channel < 9)
                ch[static_cast<std::size_t>(v.channel)] = v.value;
        }
        for (const MotionCurve& c : motion->curves) {
            if (c.joint == joint && c.channel >= 0 && c.channel < 9)
                ch[static_cast<std::size_t>(c.channel)] = evaluate(c, time);
        }
        return ch;
    }

    void add_animation(const Motion& m, const Model& model, const std::vector<int>& joints, const std::string& name) {
        const float fps = m.fps > 0.0F ? m.fps : 30.0F;
        SceneAnimation anim;
        anim.name = name;
        anim.loop = m.loop_from < m.end - 0.5F;
        anim.loop_from = (std::max(m.loop_from, m.start) - m.start) / fps;
        for (std::size_t b = 0; b < joints.size() && b < model.bones.size(); ++b) {
            std::array<bool, 3> moves{false, false, false};  // scale, rotation, translation
            for (const MotionCurve& c : m.curves) {
                if (c.joint == static_cast<int>(b) && c.channel >= 0 && c.channel < 9)
                    moves[static_cast<std::size_t>(c.channel / 3)] = true;
            }
            if (!moves[0] && !moves[1] && !moves[2])
                continue;
            SceneTrack scale{joints[b], SceneTrack::Path::scale, {}, {}};
            SceneTrack rotation{joints[b], SceneTrack::Path::rotation, {}, {}};
            SceneTrack translation{joints[b], SceneTrack::Path::translation, {}, {}};
            // a key every motion frame: KH2's Hermite curves as glTF's linear keys
            for (float t = m.start;; t = std::min(m.end, t + 1.0F)) {
                const auto ch = channels(model.bones[b], &m, static_cast<int>(b), t);
                const float seconds = (t - m.start) / fps;
                scale.times.push_back(seconds);
                scale.values.insert(scale.values.end(), {ch[0], ch[1], ch[2]});
                rotation.times.push_back(seconds);
                const auto q = quaternion(ch[3], ch[4], ch[5]);
                rotation.values.insert(rotation.values.end(), q.begin(), q.end());
                translation.times.push_back(seconds);
                translation.values.insert(translation.values.end(), {ch[6], ch[7], ch[8]});
                if (t >= m.end)
                    break;
            }
            if (moves[0])
                anim.tracks.push_back(std::move(scale));
            if (moves[1])
                anim.tracks.push_back(std::move(rotation));
            if (moves[2])
                anim.tracks.push_back(std::move(translation));
        }
        if (!anim.tracks.empty())
            room_.scene.animations.push_back(std::move(anim));
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::filesystem::path hd_;
    std::string name_;
    const PictureSink& sink_;
    std::vector<Part> parts_;
    std::map<std::pair<std::size_t, int>, Picture> pictures_;
    std::map<std::tuple<std::size_t, int, bool, bool, int>, int> materials_;
    std::map<int, std::filesystem::path> pictures_in_place_;  // an object's HD pictures given in place of its own
    Room room_;
};

}  // namespace

Room room_model(const std::uint8_t* data, std::size_t size, const std::filesystem::path& hd_folder,
                const std::string& name, const PictureSink& write_picture) {
    return Maker(data, size, hd_folder, name, write_picture).run();
}

Room object_model(const std::uint8_t* data, std::size_t size, const std::filesystem::path& hd_folder,
                  const std::string& name, const PictureSink& write_picture, const ObjectOptions& options) {
    return Maker(data, size, hd_folder, name, write_picture).object(options);
}

Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name) {
    const MapFile map = read_map(data, size);
    if (!map.collision)
        throw std::runtime_error("KH2 map: no collision (entry 0x06 ID_)");
    const auto faces = read_collision(data, size, map.entries[*map.collision].offset);
    Scene scene;
    scene.name = name + " collision";
    scene.nodes.push_back({"room"});
    SceneMaterial mat;
    mat.name = "kh2 collision";
    mat.double_sided = true;  // a wall stops from both sides
    scene.materials.push_back(mat);
    ScenePrimitive prim;
    for (const auto& f : faces) {
        for (const auto& corner : f.corners) {
            prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
            prim.positions.push_back(corner);
            prim.uvs.push_back({0.0F, 0.0F});
            prim.colors.push_back({255, 255, 255, 255});
        }
    }
    scene.meshes.push_back({"collision", 0, {}, {std::move(prim)}});
    return scene;
}

}  // namespace khdays::assets::kh2
