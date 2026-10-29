// The mod's characters (docs/CHARACTERS.md): each one's name, base, picture and gauges; its model, skeleton, size
// and weapon bone; its clips given to the base's animations, each seen beside the game's own; its voice, combos and
// Limit. Every change made in the description as the tool shows it, saved when the port's own reader takes it.
#include "../app.h"
#include "../preview3d.h"

#include "core/characters/character_files.h"
#include "core/characters/character_pose.h"
#include "core/files/paths.h"
#include "core/models/game_model.h"
#include "core/models/model_files.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

namespace studio {

namespace {

namespace assets = khdays::assets;

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};

enum class Tab { character, model, animations, more };

struct View {
    std::string mod;  // whose characters are listed
    std::vector<std::string> keys, names;
    bool stale = true;

    std::string key;  // the one open
    IniText ini;
    std::string saved;  // its character.ini as on disk
    std::string checked;
    std::vector<std::string> problems;
    std::string wanted;  // another one asked for while this one has changes
    bool wanted_asked = false;
    int language = -1;  // -1 every language, else 0-4 (en fr de it es)
    Tab tab = Tab::character;

    // its model, as the port reads it
    std::string model_for = "\x01";
    bool have_model = false;
    assets::ImportedModel model;
    std::string model_error;
    std::map<std::string, std::size_t> model_texture;
    std::vector<std::string> bones;
    float model_height = 0.0f, model_width = 0.0f;

    // its base, as the game has it (the graphics dump's model, its animations)
    std::string base_for = "\x01";
    bool base_ok = false;
    GameModel base;
    std::string base_error;
    std::map<std::string, Texture> base_textures;
    std::vector<std::string> base_animations;  // each name once, the game's order
    std::map<std::string, int> base_frames;
    float base_height = 0.0f, base_width = 0.0f;
    std::string animation_for = "\x01";
    bool animation_ok = false;
    assets::SkeletalAnimation animation;

    // its clips, read when shown
    std::map<std::string, CharacterClip> clips;
    std::map<std::string, std::string> clip_errors;

    // the preview
    std::string shown_game;  // one of the base's animations
    std::string shown_clip;  // one of the character's clips
    double time = 0.0;
    bool playing = true;
    float yaw = 25.0f, pitch = 8.0f, distance = 4.0f;
    float framed_for = 0.0f;  // the span the camera was last put back for
    std::array<float, 3> target{0, 0.8f, 0};
    std::unique_ptr<Preview> preview;
    bool hovered = false;  // the window, last frame (where a dropped file goes)

    // a new character
    std::string new_key, new_name;
    int new_base = 0;
};

View v;

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

const char* const kLanguages[] = {"en", "fr", "de", "it", "es"};

// The key of a text in the language shown: "name" for every language, "name es" for one.
std::string language_key(const char* key) {
    return v.language < 0 ? std::string(key) : std::string(key) + " " + kLanguages[v.language];
}

void read_list(const Mod& m) {
    v.mod = m.name;
    v.keys = character_keys(m.dir);
    v.names.clear();
    for (const auto& k : v.keys) {
        const IniText ini = IniText::parse(read_file(character_dir(m.dir, k) / "character.ini"));
        const std::string name = ini.get("character", "name");
        v.names.push_back(name.empty() ? k : name + " (" + k + ")");
    }
    v.stale = false;
}

void open_character(const Mod& m, const std::string& key) {
    v.key = key;
    v.saved = read_file(character_dir(m.dir, key) / "character.ini");
    v.ini = IniText::parse(v.saved);
    v.checked = "\x01";
    v.model_for = v.base_for = v.animation_for = "\x01";
    v.clips.clear();
    v.clip_errors.clear();
    v.shown_game.clear();
    v.shown_clip.clear();
    v.time = 0.0;
    v.framed_for = 0.0f;
    // shown at first: the first of the base's animations it gives a clip, else its clip for any other
    for (const auto& [game, value] : v.ini.entries("actions")) {
        const std::string clip = value.substr(0, value.find(' '));
        if (game == "fallback") {
            if (v.shown_clip.empty())
                v.shown_clip = clip;
            continue;
        }
        v.shown_game = game;
        v.shown_clip = clip;
        break;
    }
}

bool unsaved() {
    return !v.key.empty() && v.ini.text() != v.saved;
}

// The model the description names, read as the port reads it (again when it changes).
void take_model(const Mod& m) {
    const std::string file = v.ini.get("model", "file");
    if (file == v.model_for)
        return;
    v.model_for = file;
    v.have_model = false;
    v.model = {};
    v.model_error.clear();
    v.model_texture.clear();
    v.bones.clear();
    v.model_height = 0.0f;
    if (file.empty())
        return;
    if (!import_model(character_dir(m.dir, v.key) / path_u8(file), &v.model, &v.model_error))
        return;
    v.have_model = true;
    for (std::size_t i = 0; i < v.model.textures.size(); ++i)
        v.model_texture.emplace(v.model.textures[i].first, i);
    for (const auto& j : v.model.joints)
        v.bones.push_back(j.name);
    std::array<float, 3> lo, hi;
    character_span(v.model, &lo, &hi);
    v.model_height = hi[1] > lo[1] ? hi[1] - lo[1] : 0.0f;
    v.model_width = hi[0] > lo[0] ? hi[0] - lo[0] : 0.0f;
}

const Texture* base_texture(const std::string& name) {
    const auto found = v.base_textures.find(name);
    if (found != v.base_textures.end())
        return found->second.rgba.empty() ? nullptr : &found->second;
    Texture t;
    const auto file = v.base.textures.find(name);
    if (file != v.base.textures.end()) {
        const char* error = nullptr;
        std::uint8_t* px = khdays_png_read(u8(file->second).c_str(), &t.width, &t.height, &error);
        if (px != nullptr) {
            t.rgba.assign(px, px + static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4u);
            khdays_png_free(px);
        }
    }
    const auto it = v.base_textures.emplace(name, std::move(t)).first;
    return it->second.rgba.empty() ? nullptr : &it->second;
}

// The base's model as the graphics dump has it, and the names of its animations (again when the base changes).
void take_base(App& app) {
    const std::string base = v.ini.get("character", "base");
    if (base == v.base_for)
        return;
    v.base_for = base;
    v.base_ok = false;
    v.base = {};
    v.base_error.clear();
    v.base_textures.clear();
    v.base_animations.clear();
    v.base_frames.clear();
    v.base_height = 0.0f;
    v.animation_for = "\x01";
    const std::string code = character_base_code(base);
    if (code.empty())
        return;
    // its model in battle: ba/ch/<code>/def_hb.p.z, else def.p.z
    std::string key;
    for (const char* file : {"def_hb.p.z", "def.p.z"}) {
        const std::string prefix = "ba/ch/" + code + "/" + file + "/mdl/";
        for (const auto& it : app.catalog.items) {
            if (it.kind == Kind::model && it.key.rfind(prefix, 0) == 0) {
                key = it.key;
                break;
            }
        }
        if (!key.empty())
            break;
    }
    if (key.empty()) {
        v.base_error = fmt(app.language, S::no_graphics_dump, {app.settings.graphics_dump});
        return;
    }
    if (!load_game_model(nitrofs(), resolve(app.settings, app.settings.graphics_dump), key, &v.base, &v.base_error))
        return;
    v.base_ok = true;
    for (const auto& a : v.base.animations) {
        if (!a.fits || v.base_frames.count(a.name) != 0)
            continue;
        v.base_frames.emplace(a.name, a.frames);
        v.base_animations.push_back(a.name);
    }
    std::sort(v.base_animations.begin(), v.base_animations.end());
    // its height, its mesh at rest
    std::vector<Mat> node_world, palette;
    pose_game_model(v.base, nullptr, 0.0f, &node_world, &palette);
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto& mesh : v.base.model.meshes) {
        for (const auto& vx : mesh.vertices) {
            const std::uint32_t j = vx.joints[0];
            const auto p = j < palette.size() ? mat_point(palette[j], vx.position) : vx.position;
            for (std::size_t c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], p[c]);
                hi[c] = std::max(hi[c], p[c]);
            }
        }
    }
    v.base_height = hi[1] > lo[1] ? hi[1] - lo[1] : 0.0f;
    v.base_width = hi[0] > lo[0] ? hi[0] - lo[0] : 0.0f;
}

// The base's animation shown, decoded once.
const assets::SkeletalAnimation* shown_animation() {
    if (!v.base_ok || v.shown_game.empty())
        return nullptr;
    if (v.animation_for != v.shown_game) {
        v.animation_for = v.shown_game;
        v.animation_ok = false;
        for (const auto& a : v.base.animations) {
            if (a.name == v.shown_game && a.fits) {
                std::string why;
                v.animation_ok = load_game_animation(nitrofs(), a, &v.animation, &why);
                break;
            }
        }
    }
    return v.animation_ok ? &v.animation : nullptr;
}

// One of the character's clips, read once.
const CharacterClip* clip_of(const Mod& m, const std::string& file) {
    if (file.empty() || !v.have_model)
        return nullptr;
    const auto found = v.clips.find(file);
    if (found != v.clips.end())
        return &found->second;
    if (v.clip_errors.count(file) != 0)
        return nullptr;
    CharacterClip c;
    std::string why;
    if (!load_character_clip(character_dir(m.dir, v.key) / path_u8(file), v.model, &c, &why)) {
        v.clip_errors[file] = why;
        return nullptr;
    }
    return &v.clips.emplace(file, std::move(c)).first->second;
}

// Up to `n` numbers of a line written "a b c ..." (those missing left as they are).
template <typename T> void numbers_of(const std::string& text, T* out, std::size_t n) {
    std::istringstream in(text);
    for (std::size_t i = 0; i < n; ++i) {
        T x{};
        if (!(in >> x))
            break;
        out[i] = x;
    }
}

float model_scale() {
    const std::string s = v.ini.get("model", "scale");
    const float k = s.empty() ? 1.0f : static_cast<float>(std::atof(s.c_str()));
    return k > 0.0f ? k : 1.0f;
}

bool own_skeleton() {
    return v.ini.get("model", "skeleton") == "own";
}

// The base (the game's model, its animation shown) and the character (its clip shown, scaled as the port scales
// it) side by side, turned and zoomed by the mouse.
void draw_preview(App& app, const Mod& m, ImVec2 size) {
    if (!v.preview)
        v.preview = std::make_unique<Preview>(app.device);
    Preview& p = *v.preview;
    p.collect();
    const int w = std::max(64, static_cast<int>(size.x));
    const int h = std::max(64, static_cast<int>(size.y));
    if (v.playing)
        v.time += ImGui::GetIO().DeltaTime;
    // the two side by side, each its own width apart, the camera put back to see both when their size changes
    const float k = model_scale();
    const float height = std::max({v.base_height, v.model_height * k, 0.5f});
    const float apart = (v.base_width + v.model_width * k) * 0.3f + height * 0.15f;
    if (std::fabs(height + apart - v.framed_for) > 1e-3f) {
        v.framed_for = height + apart;
        v.target = {0.0f, height * 0.5f, 0.0f};
        v.distance = std::max(height * 2.6f, (apart * 2.0f + height * 0.8f) * 2.1f);
    }
    const Mat vp =
        orbit_view_projection(v.target, v.yaw, v.pitch, v.distance, static_cast<float>(w) / static_cast<float>(h));
    p.begin(w, h, vp, orbit_eye_direction(v.yaw, v.pitch));

    // the base, on the left (drawn both sides: the preview does not claim the DS's winding)
    if (v.base_ok) {
        const auto* anim = shown_animation();
        const float frame = anim != nullptr && anim->frame_count > 0
                                ? std::fmod(static_cast<float>(v.time) * 30.0f, static_cast<float>(anim->frame_count))
                                : 0.0f;
        std::vector<Mat> node_world, palette;
        pose_game_model(v.base, anim, frame, &node_world, &palette);
        for (const auto& mesh : v.base.model.meshes) {
            if (mesh.material_alpha <= 0.0f)
                continue;
            const Texture* t = mesh.texture_name.empty() ? nullptr : base_texture(mesh.texture_name);
            PreviewImage image;
            if (t != nullptr)
                image = {t->rgba.data(), t->width, t->height, mesh.repeat, mesh.mirror};
            const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
            const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
            auto vertex = [&](const assets::NeutralVertex& vx) {
                PreviewVertex o;
                std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                for (std::size_t k = 0; k < 4; ++k) {
                    const float wgt = vx.weights[k];
                    const std::uint32_t jt = vx.joints[k];
                    if (wgt <= 0.0f || jt >= palette.size())
                        continue;
                    const auto q = mat_point(palette[jt], vx.position);
                    const auto n = mat_vec(palette[jt], vx.normal);
                    for (std::size_t c = 0; c < 3; ++c) {
                        pos[c] += wgt * q[c];
                        nrm[c] += wgt * n[c];
                    }
                }
                pos[0] -= apart;
                o.position = pos;
                o.normal = nrm;
                o.uv = {vx.texcoord[0] / tw, vx.texcoord[1] / th};
                o.colour = {vx.color[0] / 255.0f, vx.color[1] / 255.0f, vx.color[2] / 255.0f,
                            vx.color[3] / 255.0f * mesh.material_alpha};
                return o;
            };
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                           vertex(mesh.vertices[mesh.indices[i + 2]]), image, false, {1, 1, 1});
        }
    }

    // the character, on the right: its own clip shown (own skeleton), else at rest
    if (v.have_model) {
        const CharacterClip* clip = own_skeleton() ? clip_of(m, v.shown_clip) : nullptr;
        const std::vector<Mat> palette = character_palette(v.model, character_pose(v.model, clip, v.time));
        for (const auto& mesh : v.model.model.meshes) {
            const auto ti = v.model_texture.find(mesh.texture_name);
            PreviewImage image;
            float tw = 1.0f, th = 1.0f;
            if (ti != v.model_texture.end()) {
                const auto& tex = v.model.textures[ti->second].second;
                image = {tex.rgba.data(), tex.width, tex.height};
                tw = static_cast<float>(tex.width);
                th = static_cast<float>(tex.height);
            }
            auto vertex = [&](const assets::NeutralVertex& vx) {
                PreviewVertex o;
                std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                float total = 0.0f;
                for (std::size_t j = 0; j < 4; ++j) {
                    const float wgt = vx.weights[j];
                    const std::uint32_t entry = vx.joints[j];
                    if (wgt <= 0.0f || entry >= palette.size())
                        continue;
                    const auto q = mat_point(palette[entry], vx.position);
                    const auto n = mat_vec(palette[entry], vx.normal);
                    for (std::size_t c = 0; c < 3; ++c) {
                        pos[c] += wgt * q[c];
                        nrm[c] += wgt * n[c];
                    }
                    total += wgt;
                }
                if (total <= 0.0f) {
                    pos = vx.position;
                    nrm = vx.normal;
                } else if (std::fabs(total - 1.0f) > 1e-4f) {
                    for (auto& c : pos)
                        c /= total;
                }
                for (auto& c : pos)
                    c *= k;
                pos[0] += v.base_ok ? apart : 0.0f;
                o.position = pos;
                o.normal = nrm;
                o.uv = {vx.texcoord[0] / tw, vx.texcoord[1] / th};
                return o;
            };
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                           vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
        }
    }

    SDL_GPUTexture* texture = p.finish();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (texture != nullptr)
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture))),
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));
    else
        ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
    // the mouse the picture's own (a drag on it never moves the window)
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##preview", ImVec2(static_cast<float>(w), static_cast<float>(h)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        v.yaw -= d.x * 0.4f;
        v.pitch = std::clamp(v.pitch + d.y * 0.4f, -85.0f, 85.0f);
    }
    orbit_move(v.target, v.yaw, v.pitch, v.distance, static_cast<float>(h), hovered);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        v.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
    // the names under it: the game's on the left, the character's on the right
    const std::string left = v.base_ok
                                 ? fmt(app.language, S::chars_preview_base,
                                       {character_display_name(v.base_for), v.shown_game.empty() ? "-" : v.shown_game})
                                 : v.base_error;
    const std::string right = !v.have_model
                                  ? std::string()
                                  : fmt(app.language, S::chars_preview_character,
                                        {v.ini.get("character", "name"), v.shown_clip.empty() ? "-" : v.shown_clip});
    ImGui::TextDisabled("%s", left.c_str());
    ImGui::SameLine(static_cast<float>(w) * 0.5f);
    ImGui::TextDisabled("%s", right.c_str());
    if (ImGui::SmallButton(v.playing ? tr(app.language, S::chars_pause) : tr(app.language, S::chars_play)))
        v.playing = !v.playing;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(app.language, S::chars_preview_hint));
}

// --- the file dialogs' answers, taken on the interface's thread ---------------------------------------------

enum class Wanted { model, clips, face, sound };
struct Asked {
    Wanted what;
    std::string sound_key;  // a sound's line, for its file
};
Asked asked{Wanted::model, {}};

void ask_files(App& app, Wanted what, const std::string& sound_key = {}) {
    asked = {what, sound_key};
    static const SDL_DialogFileFilter models[] = {{"FBX, glTF, glb", "fbx;gltf;glb"}};
    static const SDL_DialogFileFilter pictures[] = {{"PNG, JPEG, BMP, TGA", "png;jpg;jpeg;bmp;tga"}};
    static const SDL_DialogFileFilter sounds[] = {{"WAV, FLAC, OGG", "wav;flac;ogg"}};
    const SDL_DialogFileFilter* filters = what == Wanted::face ? pictures : what == Wanted::sound ? sounds : models;
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            if (files == nullptr)
                return;
            App* a = static_cast<App*>(user);
            std::lock_guard lock(a->dialog_mutex);
            for (int i = 0; files[i] != nullptr; ++i)
                a->dialog_characters.emplace_back(files[i]);
        },
        &app, app.window, filters, 1, nullptr, what == Wanted::clips);
}

// A file given (chosen, or dropped on the window): copied into the character's folder and named in its
// description, as the tab or the question it answers wants.
void take_file(App& app, const Mod& m, const fs::path& file, Wanted what, const std::string& sound_key) {
    const char* const folders[] = {"model", "clips", "", "voices"};
    std::string relative, why;
    if (!copy_into_character(m.dir, v.key, file, folders[static_cast<int>(what)], &relative, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    switch (what) {
    case Wanted::model:
        v.ini.set("model", "file", relative);
        if (v.ini.get("model", "skeleton").empty())
            v.ini.set("model", "skeleton", "own");
        break;
    case Wanted::clips:
        if (v.ini.get("actions", "fallback").empty())
            v.ini.set("actions", "fallback", relative);
        v.clip_errors.erase(relative);
        v.clips.erase(relative);
        v.shown_clip = relative;
        break;
    case Wanted::face:
        v.ini.set("character", "face", relative);
        app.images->forget(character_dir(m.dir, v.key) / path_u8(relative));
        break;
    case Wanted::sound:
        if (!sound_key.empty())
            v.ini.set("sounds", sound_key, relative);
        break;
    }
    app.tell(fmt(app.language, S::chars_copied, {u8(file.filename()), v.key}), true);
}

void take_dialog(App& app, const Mod& m) {
    std::vector<std::string> files;
    {
        std::lock_guard lock(app.dialog_mutex);
        files.swap(app.dialog_characters);
    }
    for (const auto& f : files)
        take_file(app, m, path_u8(f), asked.what, asked.sound_key);
}

// --- the tabs -------------------------------------------------------------------------------------------------

void draw_text_field(const char* label, const char* key) {
    const std::string k = language_key(key);
    std::string value = v.ini.get("character", k);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::InputText(label, &value)) {
        value.erase(std::remove(value.begin(), value.end(), '#'), value.end());  // a '#' starts a comment
        if (value.empty() && v.language >= 0)
            v.ini.remove("character", k);
        else
            v.ini.set("character", k, value);
    }
}

void draw_character_tab(App& app, const Mod& m) {
    const auto lang = app.language;
    // its words, for every language or one
    const char* const shown = v.language < 0 ? tr(lang, S::chars_all_languages) : kLanguages[v.language];
    ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
    if (ImGui::BeginCombo(tr(lang, S::language), shown)) {
        if (ImGui::Selectable(tr(lang, S::chars_all_languages), v.language < 0))
            v.language = -1;
        for (int l = 0; l < 5; ++l)
            if (ImGui::Selectable(kLanguages[l], v.language == l))
                v.language = l;
        ImGui::EndCombo();
    }
    draw_text_field(tr(lang, S::chars_name), "name");
    draw_text_field(tr(lang, S::chars_weapon), "weapon");
    ImGui::Separator();
    // whose moves it takes: a new character, never one of the game's replaced
    const std::string base = v.ini.get("character", "base");
    ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
    if (ImGui::BeginCombo(tr(lang, S::chars_moves_like), character_display_name(base).c_str())) {
        for (const auto& b : character_bases())
            if (ImGui::Selectable(character_display_name(b).c_str(), b == base))
                v.ini.set("character", "base", b);
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(lang, S::chars_moves_like_help));
    ImGui::PopTextWrapPos();
    const bool with_base = v.ini.get("character", "locked") == "base";
    int locked = with_base ? 1 : 0;
    const char* const lockings[] = {tr(lang, S::chars_locked_no), tr(lang, S::chars_locked_base)};
    ImGui::SetNextItemWidth(18 * ImGui::GetFontSize());
    if (ImGui::Combo(tr(lang, S::chars_locked), &locked, lockings, 2))
        v.ini.set("character", "locked", locked == 1 ? "base" : "no");
    ImGui::Separator();
    // its picture
    ImGui::TextUnformatted(tr(lang, S::chars_face));
    const std::string face = v.ini.get("character", "face");
    if (!face.empty()) {
        const GpuImage& g = app.images->get(character_dir(m.dir, v.key) / path_u8(face));
        if (g.texture != nullptr) {
            const float side = 6.0f * ImGui::GetFontSize();
            const float k = side / static_cast<float>(std::max(g.width, g.height));
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(g.texture))),
                         ImVec2(static_cast<float>(g.width) * k, static_cast<float>(g.height) * k));
            ImGui::SameLine();
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s: %s", face.c_str(), g.error.c_str());
        }
    }
    if (ImGui::Button(tr(lang, S::chars_choose_picture)))
        ask_files(app, Wanted::face);
    ImGui::Separator();
    // its gauges: its own, or its base's
    std::string gauges = v.ini.get("character", "gauges");
    bool own = !gauges.empty();
    if (ImGui::Checkbox(tr(lang, S::chars_gauges), &own)) {
        if (own)
            v.ini.set("character", "gauges", "2 2 2 2 4 2 2 2 2");
        else
            v.ini.remove("character", "gauges");
        gauges = v.ini.get("character", "gauges");
    }
    if (own) {
        int values[9] = {2, 2, 2, 2, 4, 2, 2, 2, 2};
        numbers_of(gauges, values, 9);
        // the rows the select shows (5 and 6 are not)
        const std::string names = tr(lang, S::chars_gauge_names);
        std::vector<std::string> label;
        for (std::size_t at = 0; label.size() < 9;) {
            const auto bar = names.find('|', at);
            label.push_back(names.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
            if (bar == std::string::npos)
                break;
            at = bar + 1;
        }
        label.resize(9);
        bool changed = false;
        for (int g = 0; g < 9; ++g) {
            if (g == 5 || g == 6)
                continue;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            changed |= ImGui::SliderInt(label[static_cast<std::size_t>(g)].c_str(), &values[g], 0, 10);
        }
        if (changed) {
            char text[64];
            std::snprintf(text, sizeof(text), "%d %d %d %d %d %d %d %d %d", values[0], values[1], values[2], values[3],
                          values[4], values[5], values[6], values[7], values[8]);
            v.ini.set("character", "gauges", text);
        }
    }
}

void draw_model_tab(App& app) {
    const auto lang = app.language;
    const std::string file = v.ini.get("model", "file");
    if (file.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::chars_no_model));
    else
        ImGui::TextUnformatted(fmt(lang, S::chars_model_file, {file}).c_str());
    if (!v.model_error.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", v.model_error.c_str());
    if (ImGui::Button(tr(lang, S::chars_choose_model)))
        ask_files(app, Wanted::model);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr(lang, S::chars_drop_hint));
    ImGui::Separator();
    // its skeleton
    int own = own_skeleton() ? 0 : 1;
    if (ImGui::RadioButton(tr(lang, S::chars_skeleton_own), &own, 0))
        v.ini.set("model", "skeleton", "own");
    if (ImGui::RadioButton(tr(lang, S::chars_skeleton_paired), &own, 1))
        v.ini.set("model", "skeleton", "paired");
    // its size
    float scale = model_scale();
    ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
    if (ImGui::InputFloat(tr(lang, S::chars_scale), &scale, 0.0f, 0.0f, "%.4g") && scale > 0.0f) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(scale));
        v.ini.set("model", "scale", text);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!v.base_ok || v.model_height <= 0.0f || v.base_height <= 0.0f);
    if (ImGui::Button(tr(lang, S::chars_match_height))) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(v.base_height / v.model_height));
        v.ini.set("model", "scale", text);
    }
    ImGui::EndDisabled();
    // the bone its weapon follows (own skeleton)
    if (own == 0 && v.have_model) {
        const std::string bone = v.ini.get("model", "weapon_bone");
        ImGui::SetNextItemWidth(14 * ImGui::GetFontSize());
        if (ImGui::BeginCombo(tr(lang, S::chars_weapon_bone),
                              bone.empty() ? tr(lang, S::chars_weapon_bone_none) : bone.c_str())) {
            if (ImGui::Selectable(tr(lang, S::chars_weapon_bone_none), bone.empty()))
                v.ini.remove("model", "weapon_bone");
            for (const auto& b : v.bones)
                if (ImGui::Selectable(b.c_str(), b == bone))
                    v.ini.set("model", "weapon_bone", b);
            ImGui::EndCombo();
        }
        if (!bone.empty()) {
            bool turned = !v.ini.get("model", "weapon_turn").empty();
            if (ImGui::Checkbox(tr(lang, S::chars_weapon_turn), &turned)) {
                if (turned)
                    v.ini.set("model", "weapon_turn", "0 0 0");
                else
                    v.ini.remove("model", "weapon_turn");
            }
            if (turned) {
                float t[3] = {0, 0, 0};
                numbers_of(v.ini.get("model", "weapon_turn"), t, 3);
                ImGui::SetNextItemWidth(16 * ImGui::GetFontSize());
                if (ImGui::DragFloat3("##turn", t, 1.0f, -180.0f, 180.0f, "%.0f")) {
                    char text[64];
                    std::snprintf(text, sizeof(text), "%g %g %g", static_cast<double>(t[0]), static_cast<double>(t[1]),
                                  static_cast<double>(t[2]));
                    v.ini.set("model", "weapon_turn", text);
                }
            }
        }
    }
}

// Roxas's animations as they have been seen played (docs/CHARACTERS.md); others by their names alone.
S known_animation(const std::string& name, int* number) {
    static const std::map<std::string, std::pair<S, int>> known = {
        {"b_ro_A000", {S::chars_anim_standing, 0}},  {"b_ro_A001", {S::chars_anim_running, 0}},
        {"b_ro_A003", {S::chars_anim_jump, 0}},      {"b_ro_A004", {S::chars_anim_landing, 0}},
        {"b_ro_B009", {S::chars_anim_combo, 1}},     {"b_ro_B008", {S::chars_anim_combo, 2}},
        {"b_ro_B015", {S::chars_anim_combo, 3}},     {"b_ro_L000", {S::chars_anim_limit_start, 0}},
        {"b_ro_L001", {S::chars_anim_limit_hit, 0}},
    };
    const auto found = known.find(name);
    if (found == known.end())
        return S::count;
    *number = found->second.second;
    return found->second.first;
}

// An action's line: "<clip> [blend <ms>] [root keep] [time game]".
struct Action {
    std::string clip;
    int blend = 100;
    bool keep = false, game_time = false;
};

Action parse_action(const std::string& value) {
    Action a;
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < value.size()) {
        const auto start = value.find_first_not_of(' ', at);
        if (start == std::string::npos)
            break;
        const auto end = value.find(' ', start);
        words.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? value.size() : end;
    }
    if (!words.empty())
        a.clip = words[0];
    for (std::size_t i = 1; i + 1 < words.size(); i += 2) {
        if (words[i] == "blend")
            a.blend = std::atoi(words[i + 1].c_str());
        else if (words[i] == "root")
            a.keep = words[i + 1] == "keep";
        else if (words[i] == "time")
            a.game_time = words[i + 1] == "game";
    }
    return a;
}

std::string action_text(const Action& a) {
    std::string out = a.clip;
    if (a.blend != 100)
        out += " blend " + std::to_string(a.blend);
    if (a.keep)
        out += " root keep";
    if (a.game_time)
        out += " time game";
    return out;
}

void draw_animations_tab(App& app, const Mod& m) {
    const auto lang = app.language;
    if (!own_skeleton()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(tr(lang, S::chars_paired_note));
        ImGui::PopTextWrapPos();
        return;
    }
    // its clips
    const std::vector<std::string> clips = character_files(m.dir, v.key, "clips");
    if (ImGui::Button(tr(lang, S::chars_add_clips)))
        ask_files(app, Wanted::clips);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fmt(lang, S::chars_clips_count, {std::to_string(clips.size())}).c_str());
    auto clip_combo = [&](const char* id, std::string* clip, bool in_table = false) {
        bool changed = false;
        ImGui::SetNextItemWidth(in_table ? -FLT_MIN : 12 * ImGui::GetFontSize());
        if (ImGui::BeginCombo(id, clip->empty() ? tr(lang, S::chars_no_clip) : clip->c_str())) {
            if (ImGui::Selectable(tr(lang, S::chars_no_clip), clip->empty())) {
                clip->clear();
                changed = true;
            }
            for (const auto& c : clips) {
                if (ImGui::Selectable(c.c_str(), c == *clip)) {
                    *clip = c;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    v.shown_clip = c;  // seen while choosing
            }
            ImGui::EndCombo();
        }
        return changed;
    };
    std::string fallback = v.ini.get("actions", "fallback");
    if (clip_combo(tr(lang, S::chars_fallback), &fallback)) {
        if (fallback.empty())
            v.ini.remove("actions", "fallback");
        else
            v.ini.set("actions", "fallback", fallback);
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(lang, S::chars_actions_note));
    ImGui::PopTextWrapPos();
    // the base's animations, and those the description names that the base does not have
    std::vector<std::string> rows = v.base_animations;
    for (const auto& [game, value] : v.ini.entries("actions"))
        if (game != "fallback" && std::find(rows.begin(), rows.end(), game) == rows.end())
            rows.push_back(game);
    // the table takes the height left above the chosen row's details
    const float details = 3.5f * ImGui::GetFrameHeightWithSpacing();
    const float rest = std::max(8 * ImGui::GetTextLineHeightWithSpacing(),
                                ImGui::GetContentRegionAvail().y - details -
                                    ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(v.clip_errors.size()));
    if (ImGui::BeginTable("##actions", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, rest))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr(lang, S::chars_action_game), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(tr(lang, S::chars_action_clip), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const auto& game : rows) {
            ImGui::PushID(game.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            int number = 0;
            const S label = known_animation(game, &number);
            const auto frames = v.base_frames.find(game);
            std::string text = game;
            if (label != S::count)
                text = fmt(lang, label, {std::to_string(number)}) + "  (" + game + ")";
            if (frames != v.base_frames.end())
                text += "  " + fmt(lang, S::chars_frames, {std::to_string(frames->second)});
            Action a = parse_action(v.ini.get("actions", game));
            // a row clicked: both shown, the game's and the character's
            if (ImGui::Selectable(text.c_str(), v.shown_game == game,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                v.shown_game = game;
                v.shown_clip = a.clip.empty() ? fallback : a.clip;
                v.time = 0.0;
            }
            ImGui::TableNextColumn();
            if (clip_combo("##clip", &a.clip, true)) {
                if (a.clip.empty())
                    v.ini.remove("actions", game);
                else
                    v.ini.set("actions", game, action_text(a));
                v.shown_game = game;
                v.shown_clip = a.clip.empty() ? fallback : a.clip;
                v.time = 0.0;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // the chosen row's details: how its clip blends in, steps and keeps time
    bool found = false;
    Action a = parse_action(v.ini.get("actions", v.shown_game, &found));
    if (found && !v.shown_game.empty()) {
        bool changed = false;
        ImGui::TextUnformatted(v.shown_game.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6 * ImGui::GetFontSize());
        changed |= ImGui::InputInt(tr(lang, S::chars_blend), &a.blend, 10, 100);
        changed |= ImGui::Checkbox(tr(lang, S::chars_keep_root), &a.keep);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(tr(lang, S::chars_game_time), &a.game_time);
        if (changed) {
            a.blend = std::clamp(a.blend, 0, 10000);
            v.ini.set("actions", v.shown_game, action_text(a));
        }
    } else if (!v.shown_game.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", fmt(lang, S::chars_no_clip_for, {v.shown_game}).c_str());
        ImGui::PopTextWrapPos();
    }
    for (const auto& [file, why] : v.clip_errors)
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s: %s", file.c_str(), why.c_str());
}

// A part's lines as rows of two fields, with a button to take one away and one to add one.
void draw_pairs(App& app, const char* part, const char* key_hint, const char* value_hint, bool files) {
    auto entries = v.ini.entries(part);
    bool changed = false;
    int gone = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(16 * ImGui::GetFontSize());
        changed |= ImGui::InputTextWithHint("##key", key_hint, &entries[i].first);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(16 * ImGui::GetFontSize());
        changed |= ImGui::InputTextWithHint("##value", value_hint, &entries[i].second);
        if (files) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(app.language, S::chars_choose_sound)))
                ask_files(app, Wanted::sound, entries[i].first);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            gone = static_cast<int>(i);
        ImGui::PopID();
    }
    if (gone >= 0) {
        entries.erase(entries.begin() + gone);
        changed = true;
    }
    if (ImGui::SmallButton((std::string(tr(app.language, S::chars_add_row)) + "##" + part).c_str())) {
        entries.emplace_back(key_hint, value_hint);
        changed = true;
    }
    if (changed) {
        for (auto& [k, value] : entries) {
            k.erase(std::remove(k.begin(), k.end(), '#'), k.end());
            value.erase(std::remove(value.begin(), value.end(), '#'), value.end());
        }
        v.ini.set_entries(part, entries);
    }
}

void draw_more_tab(App& app) {
    const auto lang = app.language;
    ImGui::TextUnformatted(tr(lang, S::chars_sounds));
    draw_pairs(app, "sounds", "seqarc/PLY_ROX/vo_*", "none", true);
    ImGui::Separator();
    ImGui::TextUnformatted(tr(lang, S::chars_moveset));
    draw_pairs(app, "moveset", "ground", "0 1", false);
    ImGui::Separator();
    std::string damage = v.ini.get("limit", "damage");
    float times = damage.empty() ? 1.0f : static_cast<float>(std::atof(damage.c_str()));
    ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
    if (ImGui::InputFloat(tr(lang, S::chars_limit_damage), &times, 0.1f, 0.5f, "%.2f") && times > 0.0f) {
        if (std::fabs(times - 1.0f) < 1e-4f) {
            v.ini.set_entries("limit", {});
        } else {
            char text[32];
            std::snprintf(text, sizeof(text), "%g", static_cast<double>(times));
            v.ini.set("limit", "damage", text);
        }
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(lang, S::chars_more_note));
    ImGui::PopTextWrapPos();
}

void draw_list(App& app, const Mod& m) {
    const auto lang = app.language;
    WrappedTexts wrap;
    if (v.keys.empty())
        ImGui::TextWrapped("%s", tr(lang, S::chars_none));
    for (std::size_t i = 0; i < v.keys.size(); ++i) {
        if (ImGui::Selectable(v.names[i].c_str(), v.keys[i] == v.key) && v.keys[i] != v.key) {
            if (unsaved()) {
                v.wanted = v.keys[i];
                v.wanted_asked = true;
            } else {
                open_character(m, v.keys[i]);
            }
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted(tr(lang, S::chars_new));
    ImGui::TextDisabled("%s", tr(lang, S::chars_key));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##newkey", &v.new_key);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newname", tr(lang, S::chars_name), &v.new_name);
    const auto& bases = character_bases();
    ImGui::TextDisabled("%s", tr(lang, S::chars_moves_like));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##newbase", character_display_name(bases[static_cast<std::size_t>(v.new_base)]).c_str())) {
        for (std::size_t b = 0; b < bases.size(); ++b)
            if (ImGui::Selectable(character_display_name(bases[b]).c_str(), static_cast<int>(b) == v.new_base))
                v.new_base = static_cast<int>(b);
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(v.new_key.empty() || v.new_name.empty() || unsaved());
    if (ImGui::Button(tr(lang, S::chars_create))) {
        std::string why;
        if (new_character(m.dir, v.new_key, bases[static_cast<std::size_t>(v.new_base)], v.new_name, &why)) {
            app.tell(fmt(lang, S::chars_created, {v.new_key, m.name}), true);
            read_list(m);
            open_character(m, v.new_key);
            v.tab = Tab::model;
            v.new_key.clear();
            v.new_name.clear();
        } else {
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    }
    ImGui::EndDisabled();
}

}  // namespace

bool characters_unsaved() {
    return unsaved();
}

std::string characters_unsaved_key() {
    return v.key;
}

bool characters_take_drop(App& app, const fs::path& file) {
    const Mod* m = app.current_mod();
    if (!v.hovered || m == nullptr || v.key.empty())
        return false;
    const Wanted what = v.tab == Tab::character    ? Wanted::face
                        : v.tab == Tab::animations ? Wanted::clips
                                                   : Wanted::model;
    take_file(app, *m, file, what, {});
    return true;
}

void draw_characters(App& app, bool* open) {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(72 * em, 46 * em), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(app.language, S::chars_title), open)) {
        v.hovered = false;
        ImGui::End();
        return;
    }
    v.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const auto lang = app.language;
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::choose_mod_first));
        ImGui::End();
        return;
    }
    if (v.stale || v.mod != m->name) {
        if (v.mod != m->name) {
            v.key.clear();
            v.preview.reset();
        }
        read_list(*m);
        if (v.key.empty() && !v.keys.empty())
            open_character(*m, v.keys.front());
    }
    take_dialog(app, *m);

    ImGui::BeginChild("##characters", ImVec2(12 * em, 0), ImGuiChildFlags_Borders);
    draw_list(app, *m);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##character", ImVec2(0, 0));
    if (v.key.empty()) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    take_model(*m);
    take_base(app);
    // saving: written when the port's reader takes it, read back
    const std::string text = v.ini.text();
    if (text != v.checked) {
        v.checked = text;
        v.problems = check_character(m->dir, v.key, text);
    }
    ImGui::BeginDisabled(!unsaved());
    if (ImGui::Button(tr(lang, S::chars_save))) {
        std::string why;
        if (save_character(m->dir, v.key, text, &why)) {
            v.saved = text;
            app.tell(fmt(lang, S::chars_saved, {v.key}), true);
            read_list(*m);
        } else {
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(lang, S::chars_undo)))
        open_character(*m, v.key);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (v.problems.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "%s", tr(lang, S::chars_ok));
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", tr(lang, S::chars_problems));
        for (const auto& p : v.problems)
            ImGui::BulletText("%s", p.c_str());
    }
    // the tabs on the left half, the preview on the right
    const float half = ImGui::GetContentRegionAvail().x * 0.58f;
    ImGui::BeginChild("##tabs", ImVec2(half, 0));
    if (WrappedTexts wrap; ImGui::BeginTabBar("##charactertabs")) {
        if (ImGui::BeginTabItem(tr(lang, S::chars_tab_character))) {
            v.tab = Tab::character;
            draw_character_tab(app, *m);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(lang, S::chars_tab_model))) {
            v.tab = Tab::model;
            draw_model_tab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(lang, S::chars_tab_animations))) {
            v.tab = Tab::animations;
            draw_animations_tab(app, *m);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(lang, S::chars_tab_more))) {
            v.tab = Tab::more;
            draw_more_tab(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##preview");
    const ImVec2 room = ImGui::GetContentRegionAvail();
    {
        WrappedTexts wrap;
        draw_preview(app, *m, ImVec2(room.x, room.y - 4 * ImGui::GetFrameHeightWithSpacing()));
    }
    ImGui::EndChild();
    ImGui::EndChild();

    // another character asked for with changes not saved: asked first
    if (v.wanted_asked) {
        ImGui::OpenPopup("###unsavedcharacter");
        v.wanted_asked = false;
    }
    if (ImGui::BeginPopupModal("###unsavedcharacter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(fmt(lang, S::chars_unsaved, {v.key}).c_str());
        if (ImGui::Button(tr(lang, S::new_missions_drop))) {
            open_character(*m, v.wanted);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(lang, S::new_missions_keep)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

}  // namespace studio
