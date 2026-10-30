// The mod's prizes and chests (docs/KH2_SEQUENCES.md, "Field prizes"): each piece beside Days's own -- an effect
// playing as the port draws it, a prize model turning, a chest beside the game's -- a prize model's size and turn
// and a chest's scale changed and saved, and the KH2 prizes mod made into the mod from the player's 1.5+2.5 copy as
// the installer makes it.
#include "../app.h"
#include "../preview3d.h"

#include "core/files/paths.h"
#include "core/models/bones.h"
#include "core/models/game_model.h"
#include "core/mods/prizes.h"
#include "core/rooms/kh2_source.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace studio {

namespace {

namespace assets = khdays::assets;
namespace fs = std::filesystem;

// A model read for its preview: its triangles at rest and its pictures.
struct Shape {
    assets::ImportedModel model;
    std::map<std::string, std::size_t> texture;  // a mesh's texture name -> textures[]
    std::array<float, 3> low{}, high{};
    std::string error;
};

struct Making {
    std::thread thread;
    std::atomic<bool> done{false};
    bool ok = false;
    std::mutex mutex;
    std::vector<std::string> lines;
    std::string why;
    ~Making() {
        if (thread.joinable())
            thread.join();
    }
};

struct View {
    std::string mod;
    std::vector<PrizePiece> pieces;
    bool stale = true;
    int chosen = 0;
    PrizePiece edited;  // the chosen piece as changed, not saved
    std::map<std::string, std::optional<PrizeEffect>> effects;
    std::map<std::string, std::string> effect_errors;
    std::map<std::string, std::unique_ptr<Shape>> shapes;  // by file
    std::unique_ptr<Preview> preview;
    std::unique_ptr<Making> making;
};
View v;

// The model at `file` (the mod's, or the graphics dump's), read once.
const Shape& shape(const fs::path& file) {
    auto& slot = v.shapes[u8(file)];
    if (slot)
        return *slot;
    slot = std::make_unique<Shape>();
    Shape& s = *slot;
    try {
        s.model = file.extension() == ".fbx" ? assets::import_fbx(file) : assets::import_gltf(file);
    } catch (const std::exception& e) {
        s.error = e.what();
        return s;
    }
    for (std::size_t i = 0; i < s.model.textures.size(); ++i)
        s.texture[s.model.textures[i].first] = i;
    s.low = {INFINITY, INFINITY, INFINITY};
    s.high = {-INFINITY, -INFINITY, -INFINITY};
    for (const auto& mesh : s.model.model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = mat_point(vertex.joints[0] < s.model.model.palette.size()
                                         ? s.model.model.palette[vertex.joints[0]]
                                         : Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                     vertex.position);
            for (std::size_t c = 0; c < 3; ++c) {
                s.low[c] = std::fmin(s.low[c], p[c]);
                s.high[c] = std::fmax(s.high[c], p[c]);
            }
        }
    }
    return s;
}

// A model's triangles into the preview, at rest: its middle over (at_x, 0, 0), its foot at 0, turned `yaw`
// degrees about its up axis and scaled by `k`.
void add_shape(Preview& p, const Shape& s, float at_x, float yaw, float k) {
    const float mx = (s.low[0] + s.high[0]) / 2, mz = (s.low[2] + s.high[2]) / 2;
    const float a = yaw * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a);
    for (const auto& mesh : s.model.model.meshes) {
        const auto ti = s.texture.find(mesh.texture_name);
        PreviewImage image;
        float tw = 1.0f, th = 1.0f;
        if (ti != s.texture.end()) {
            const auto& tex = s.model.textures[ti->second].second;
            image = {tex.rgba.data(), tex.width, tex.height};
            tw = static_cast<float>(tex.width);
            th = static_cast<float>(tex.height);
        }
        auto vertex = [&](const assets::NeutralVertex& vx) {
            std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
            float total = 0.0f;
            for (int j = 0; j < 4; ++j) {
                const float w = vx.weights[static_cast<std::size_t>(j)];
                const std::uint32_t jt = vx.joints[static_cast<std::size_t>(j)];
                if (w <= 0.0f || jt >= s.model.model.palette.size())
                    continue;
                const auto q = mat_point(s.model.model.palette[jt], vx.position);
                const auto n = mat_vec(s.model.model.palette[jt], vx.normal);
                for (std::size_t i = 0; i < 3; ++i) {
                    pos[i] += w * q[i];
                    nrm[i] += w * n[i];
                }
                total += w;
            }
            if (total <= 0.0f)
                pos = vx.position;
            const float x = (pos[0] - mx) * k, y = (pos[1] - s.low[1]) * k, z = (pos[2] - mz) * k;
            PreviewVertex o;
            o.position = {at_x + c * x + sn * z, y, -sn * x + c * z};
            o.normal = {c * nrm[0] + sn * nrm[2], nrm[1], -sn * nrm[0] + c * nrm[2]};
            o.uv = {vx.texcoord[0] / tw, vx.texcoord[1] / th};
            o.colour = {vx.color[0] / 255.0f, vx.color[1] / 255.0f, vx.color[2] / 255.0f, vx.color[3] / 255.0f};
            return o;
        };
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                       vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
        }
    }
}

// Days's own picture of a prize, from the graphics dump; null when the dump has none.
const GpuImage* days_picture(App& app, const std::string& name) {
    const fs::path png = resolve(app.settings, app.settings.graphics_dump) / "mo" / "prize.pak.z" / "tex" /
                         path_u8(name + "@" + name + "_pl.png");
    std::error_code ec;
    if (!fs::is_regular_file(png, ec))
        return nullptr;
    const GpuImage& image = app.images->get(png);
    return image.texture != nullptr ? &image : nullptr;
}

void image(const GpuImage& img, float side) {
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(img.texture))), ImVec2(side, side));
}

void read_pieces(const Mod& m) {
    v.mod = m.name;
    v.pieces = prize_pieces(m.dir);
    v.stale = false;
    v.effects.clear();
    v.effect_errors.clear();
    v.shapes.clear();
    v.chosen = std::min(v.chosen, std::max(0, static_cast<int>(v.pieces.size()) - 1));
    if (!v.pieces.empty())
        v.edited = v.pieces[static_cast<std::size_t>(v.chosen)];
}

bool unsaved() {
    if (v.pieces.empty())
        return false;
    const PrizePiece& saved = v.pieces[static_cast<std::size_t>(v.chosen)];
    return saved.size != v.edited.size || saved.turn != v.edited.turn || saved.scale != v.edited.scale;
}

const char* kind_word(Language lang, PrizePiece::Kind kind) {
    return tr(lang, kind == PrizePiece::Kind::effect  ? S::prizes_effect
                    : kind == PrizePiece::Kind::model ? S::prizes_model
                                                      : S::prizes_chest);
}

// "Make from my copy": the tool run on a thread of its own, told when done.
void draw_make(App& app, const Mod& m) {
    const auto lang = app.language;
    const bool busy = v.making && !v.making->done;
    const fs::path collection = kh2_collection(app.settings);
    ImGui::BeginDisabled(busy || collection.empty());
    if (ImGui::Button(tr(lang, S::prizes_make))) {
        if (v.making && v.making->thread.joinable())
            v.making->thread.join();
        v.making = std::make_unique<Making>();
        Making* job = v.making.get();
        const Settings settings = app.settings;
        const fs::path dir = m.dir, exe = port_exe(app.settings);
        job->thread = std::thread([job, settings, dir, exe] {
            std::vector<std::string> lines;
            std::string why;
            const bool ok = make_kh2_prizes(settings, dir, exe, &lines, &why);
            std::lock_guard<std::mutex> lock(job->mutex);
            job->ok = ok;
            job->lines = std::move(lines);
            job->why = why;
            job->done = true;
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (collection.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::prizes_no_collection));
    else if (busy)
        ImGui::TextUnformatted(tr(lang, S::prizes_making));
    else
        ImGui::TextDisabled("%s", u8(collection).c_str());
    if (v.making && v.making->done && v.making->thread.joinable()) {
        v.making->thread.join();
        std::lock_guard<std::mutex> lock(v.making->mutex);
        if (v.making->ok)
            app.tell(fmt(lang, S::prizes_made, {m.name}), true);
        else
            app.tell(fmt(lang, S::prizes_make_failed, {v.making->why}), false);
        v.stale = true;
    }
    if (v.making && v.making->done) {
        // what the tool said, in a few lines that scroll
        std::lock_guard<std::mutex> lock(v.making->mutex);
        const float lines = static_cast<float>(std::min<std::size_t>(v.making->lines.size(), 6));
        ImGui::BeginChild("##made", ImVec2(0, lines * ImGui::GetTextLineHeightWithSpacing()));
        for (const auto& line : v.making->lines)
            ImGui::TextDisabled("%s", line.c_str());
        ImGui::EndChild();
    }
}

void draw_list(App& app) {
    const auto lang = app.language;
    for (int i = 0; i < static_cast<int>(v.pieces.size()); ++i) {
        const PrizePiece& p = v.pieces[static_cast<std::size_t>(i)];
        const std::string label = p.name + "  (" + kind_word(lang, p.kind) + ")##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), i == v.chosen) && i != v.chosen) {
            v.chosen = i;
            v.edited = p;
        }
    }
}

// The chosen piece: Days's beside the mod's, and what can be changed.
void draw_piece(App& app) {
    const auto lang = app.language;
    const float em = ImGui::GetFontSize();
    const float side = 16 * em;
    PrizePiece& p = v.edited;
    ImGui::Text("%s  %s  (%s)", p.name.c_str(), kind_word(lang, p.kind), p.where.c_str());
    ImGui::TextDisabled("%s", u8(p.file).c_str());
    const double seconds = static_cast<double>(SDL_GetTicks()) / 1000.0;

    if (p.kind == PrizePiece::Kind::chest) {
        // the game's chest (the graphics dump's) and the mod's, side by side, turning slowly
        const fs::path days =
            resolve(app.settings, app.settings.graphics_dump) / path_u8(p.where) / "mdl" / path_u8(p.name + ".gltf");
        std::error_code ec;
        const Shape* game = fs::is_regular_file(days, ec) ? &shape(days) : nullptr;
        const Shape& mod = shape(p.file);
        if (!mod.error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1), "%s", mod.error.c_str());
        } else {
            if (!v.preview)
                v.preview = std::make_unique<Preview>(app.device);
            Preview& pv = *v.preview;
            pv.collect();
            const int w = static_cast<int>(2.2f * side), h = static_cast<int>(side);
            const float width = (mod.high[0] - mod.low[0]) * p.scale;
            // a camera standing still, the two chests turning on their own places
            const float yaw = static_cast<float>(std::fmod(seconds * 20.0, 360.0));
            const Mat vp = orbit_view_projection({0, 0.35f * width, 0}, 0.0f, -20.0f, 4.0f * width,
                                                 static_cast<float>(w) / static_cast<float>(h));
            pv.begin(w, h, vp, orbit_eye_direction(0.0f, -20.0f));
            if (game != nullptr && game->error.empty())
                add_shape(pv, *game, -0.75f * width, yaw, 1.0f);
            add_shape(pv, mod, game != nullptr ? 0.75f * width : 0.0f, yaw, p.scale);
            if (SDL_GPUTexture* t = pv.finish())
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t))),
                             ImVec2(static_cast<float>(w), static_cast<float>(h)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", tr(lang, game != nullptr ? S::prizes_side_by_side : S::prizes_no_dump));
            ImGui::PopStyleColor();
        }
        ImGui::SetNextItemWidth(12 * em);
        ImGui::DragFloat(tr(lang, S::prizes_scale), &p.scale, 0.0001f, 0.0001f, 100.0f, "%.5f");
    } else {
        // Days's picture, then the mod's effect or model
        if (const GpuImage* days = days_picture(app, p.name))
            image(*days, side);
        else
            ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        if (p.kind == PrizePiece::Kind::effect) {
            auto found = v.effects.find(p.name);
            if (found == v.effects.end()) {
                std::string why;
                found = v.effects.emplace(p.name, load_prize_effect(p.file, &why)).first;
                v.effect_errors[p.name] = why;
            }
            if (found->second) {
                const int frame = static_cast<int>(seconds * 30.0);  // the field's game frames
                const int pixels = static_cast<int>(side);
                const auto rgba = prize_effect_frame(*found->second, frame, pixels, 0xff2a2626u);
                const GpuImage& img = app.images->pixels("prize effect " + p.name, static_cast<std::uint64_t>(frame),
                                                         rgba, pixels, pixels);
                if (img.texture != nullptr)
                    image(img, side);
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1), "%s", v.effect_errors[p.name].c_str());
            }
        } else {
            const Shape& mod = shape(p.file);
            if (!mod.error.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1), "%s", mod.error.c_str());
            } else {
                if (!v.preview)
                    v.preview = std::make_unique<Preview>(app.device);
                Preview& pv = *v.preview;
                pv.collect();
                const int s = static_cast<int>(side);
                const float height = mod.high[1] - mod.low[1];
                const Mat vp = orbit_view_projection({0, 0.5f * height, 0}, 0.0f, -15.0f, 2.4f * height, 1.0f);
                pv.begin(s, s, vp, orbit_eye_direction(0.0f, -15.0f));
                add_shape(pv, mod, 0.0f, static_cast<float>(std::fmod(seconds * p.turn, 360.0)), 1.0f);
                if (SDL_GPUTexture* t = pv.finish())
                    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t))),
                                 ImVec2(side, side));
            }
            ImGui::SetNextItemWidth(12 * em);
            ImGui::DragFloat(tr(lang, S::prizes_size), &p.size, 0.01f, 0.05f, 20.0f, "%.2f");
            ImGui::SetNextItemWidth(12 * em);
            ImGui::DragFloat(tr(lang, S::prizes_turn), &p.turn, 1.0f, -1440.0f, 1440.0f, "%.0f");
        }
    }
    if (p.kind != PrizePiece::Kind::effect) {
        ImGui::BeginDisabled(!unsaved());
        if (ImGui::Button(tr(lang, S::prizes_save))) {
            std::string why;
            if (save_prize_piece(p, &why)) {
                v.pieces[static_cast<std::size_t>(v.chosen)] = p;
                app.tell(fmt(lang, S::prizes_saved, {p.name}), true);
            } else {
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
        ImGui::EndDisabled();
    }
}

}  // namespace

void draw_prizes(App& app, bool* open) {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(64 * em, 40 * em), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(app.language, S::prizes_title), open)) {
        ImGui::End();
        return;
    }
    const auto lang = app.language;
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::choose_mod_first));
        ImGui::End();
        return;
    }
    if (v.stale || v.mod != m->name)
        read_pieces(*m);
    draw_make(app, *m);
    ImGui::Separator();
    if (v.pieces.empty()) {
        ImGui::TextWrapped("%s", tr(lang, S::prizes_none));
        ImGui::End();
        return;
    }
    ImGui::BeginChild("##prizes", ImVec2(16 * em, 0), ImGuiChildFlags_Borders);
    draw_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##prize", ImVec2(0, 0));
    draw_piece(app);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace studio
