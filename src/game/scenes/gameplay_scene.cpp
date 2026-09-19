#include "khdays/game/scenes/gameplay_scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "khdays/assets/message.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/scene3d.h"
#include "khdays/game/draw.h"
#include "khdays/game/settings.h"
#include "khdays/resource/ui_content.h"
#include "khdays/resource/mission.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::game::scenes {

namespace {

constexpr char kFont[] = "text/font_eu_08.nftr";
constexpr char kWorld[] = "tt";
constexpr char kWorldPath[] = "mi/wd/wd_tt";
constexpr char kRoxasWeaponProfiles[] = "ba/ch/ro/wp.b.z";
constexpr char kRoxasActionGraph[] = "ba/ch/ro/ci.b.z";
constexpr char kRoxasActionMetadata[] = "ba/ch/ro/cm.b.z";
constexpr char kRoxasAnimationBank[] = "ba/ch/ro/am.p2";
constexpr char kRoxasWeaponBank[] = "ba/ch/ro/w_.p2";
constexpr char kTwilightFieldTheme[] = "TwilightR_F";
constexpr char kTwilightBattleTheme[] = "TwilightR_B";
constexpr std::size_t kRoom = 0U;
constexpr int kFadeIn = 30;
constexpr float kFxScale = 4096.0F;
constexpr float kPi = 3.14159265358979323846F;

struct DebugBox final {
    std::array<float, 3> minimum;
    std::array<float, 3> maximum;
    std::array<std::uint8_t, 4> color;
};

constexpr std::array<DebugBox, 8> kDebugBoxes{{
    {{-12.0F, -0.25F, -12.0F}, {12.0F, 0.0F, 12.0F},
     {72U, 86U, 112U, 255U}},
    {{-12.0F, 0.0F, -12.0F}, {-11.5F, 2.5F, 12.0F},
     {98U, 116U, 151U, 255U}},
    {{11.5F, 0.0F, -12.0F}, {12.0F, 2.5F, 12.0F},
     {98U, 116U, 151U, 255U}},
    {{-12.0F, 0.0F, -12.0F}, {12.0F, 2.5F, -11.5F},
     {98U, 116U, 151U, 255U}},
    {{-12.0F, 0.0F, 11.5F}, {12.0F, 2.5F, 12.0F},
     {98U, 116U, 151U, 255U}},
    {{-2.2F, 0.0F, -1.2F}, {2.2F, 1.4F, 1.2F},
     {181U, 99U, 82U, 255U}},
    {{3.0F, 0.0F, -7.0F}, {4.4F, 2.2F, -2.0F},
     {88U, 157U, 123U, 255U}},
    {{-7.0F, 0.0F, 3.0F}, {-4.0F, 1.0F, 4.2F},
     {155U, 121U, 186U, 255U}},
}};

std::array<float, 16> actor_transform(
    const float x,
    const float y,
    const float z,
    const float yaw) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {
        c, 0.0F, -s, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        s, 0.0F, c, 0.0F,
        x, y, z, 1.0F};
}

std::array<float, 16> matrix_multiply(
    const std::array<float, 16>& a,
    const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            for (std::size_t k = 0; k < 4U; ++k) {
                out[column * 4U + row] +=
                    a[k * 4U + row] * b[column * 4U + k];
            }
        }
    }
    return out;
}

khdays::assets::NeutralModel make_goal_model() {
    khdays::assets::NeutralModel model;
    model.name = "playable_goal";
    khdays::assets::NeutralMesh mesh;
    mesh.name = "goal_marker";
    constexpr std::uint32_t segments = 24U;
    for (std::uint32_t segment = 0U; segment < segments; ++segment) {
        const float angle = 2.0F * kPi * static_cast<float>(segment)
            / static_cast<float>(segments);
        for (const float radius : {0.75F, 1.05F}) {
            khdays::assets::NeutralVertex vertex;
            vertex.position = {
                std::cos(angle) * radius, 0.04F, std::sin(angle) * radius};
            vertex.color = {255U, 205U, 48U, 220U};
            vertex.weights = {0.0F, 0.0F, 0.0F, 0.0F};
            mesh.vertices.push_back(vertex);
        }
    }
    for (std::uint32_t segment = 0U; segment < segments; ++segment) {
        const std::uint32_t next = (segment + 1U) % segments;
        const std::uint32_t inner = segment * 2U;
        const std::uint32_t outer = inner + 1U;
        const std::uint32_t next_inner = next * 2U;
        const std::uint32_t next_outer = next_inner + 1U;
        mesh.indices.insert(mesh.indices.end(), {
            inner, outer, next_outer, inner, next_outer, next_inner});
    }
    model.meshes.push_back(std::move(mesh));
    return model;
}

void add_box(
    khdays::assets::NeutralMesh& mesh,
    const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum,
    const std::array<std::uint8_t, 4>& color) {
    const std::uint32_t base = static_cast<std::uint32_t>(
        mesh.vertices.size());
    const std::array<std::array<float, 3>, 8> points{{
        {minimum[0], minimum[1], minimum[2]},
        {maximum[0], minimum[1], minimum[2]},
        {maximum[0], maximum[1], minimum[2]},
        {minimum[0], maximum[1], minimum[2]},
        {minimum[0], minimum[1], maximum[2]},
        {maximum[0], minimum[1], maximum[2]},
        {maximum[0], maximum[1], maximum[2]},
        {minimum[0], maximum[1], maximum[2]}}};
    for (const auto& point : points) {
        khdays::assets::NeutralVertex vertex;
        vertex.position = point;
        vertex.color = color;
        mesh.vertices.push_back(vertex);
    }
    constexpr std::array<std::uint32_t, 36> indices{{
        0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
        1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3}};
    for (const auto index : indices) {
        mesh.indices.push_back(base + index);
    }
}

khdays::resource::RoomModel make_debug_room() {
    khdays::resource::RoomModel room;
    room.name = "actor_debug_room";
    room.model.name = room.name;
    khdays::assets::NeutralMesh mesh;
    mesh.name = "debug_geometry";
    for (const auto& box : kDebugBoxes) {
        add_box(mesh, box.minimum, box.maximum, box.color);
    }
    room.model.meshes.push_back(std::move(mesh));
    return room;
}

std::int32_t debug_fx(const float value) {
    return static_cast<std::int32_t>(std::lround(value * kFxScale));
}

void add_debug_collision_face(
    khdays::assets::CollisionModel& collision,
    const std::array<std::array<float, 3>, 4>& vertices,
    const std::array<std::int16_t, 3>& normal) {
    khdays::assets::CollisionFace face;
    face.vertex_count = 4U;
    face.bounds = {
        debug_fx(vertices[0][0]), debug_fx(vertices[0][2]),
        debug_fx(vertices[0][0]), debug_fx(vertices[0][2])};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        face.vertices[i] = {
            debug_fx(vertices[i][0]),
            debug_fx(vertices[i][1]),
            debug_fx(vertices[i][2])};
        face.bounds[0] = std::min(face.bounds[0], face.vertices[i][0]);
        face.bounds[1] = std::min(face.bounds[1], face.vertices[i][2]);
        face.bounds[2] = std::max(face.bounds[2], face.vertices[i][0]);
        face.bounds[3] = std::max(face.bounds[3], face.vertices[i][2]);
    }
    face.plane.x = normal[0];
    face.plane.y = normal[1];
    face.plane.z = normal[2];
    face.plane.distance = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(normal[0]) * face.vertices[0][0]
         + static_cast<std::int64_t>(normal[1]) * face.vertices[0][1]
         + static_cast<std::int64_t>(normal[2]) * face.vertices[0][2]
         + 0x800)
        >> 12);
    // ground_at() uses a 2D edge-plane test. Box faces all share these four
    // inward bounds; vertical faces never enter its downward-plane branch.
    face.edges[0] = {0x1000, 0, face.bounds[0]};
    face.edges[1] = {-0x1000, 0, -face.bounds[2]};
    face.edges[2] = {0, 0x1000, face.bounds[1]};
    face.edges[3] = {0, -0x1000, -face.bounds[3]};
    collision.faces.push_back(face);
}

khdays::assets::CollisionModel make_debug_collision() {
    khdays::assets::CollisionModel collision;
    collision.valid = true;
    for (const auto& box : kDebugBoxes) {
        const auto& a = box.minimum;
        const auto& b = box.maximum;
        const std::array<std::array<float, 3>, 8> point{{
            {a[0], a[1], a[2]}, {b[0], a[1], a[2]},
            {b[0], b[1], a[2]}, {a[0], b[1], a[2]},
            {a[0], a[1], b[2]}, {b[0], a[1], b[2]},
            {b[0], b[1], b[2]}, {a[0], b[1], b[2]}}};
        add_debug_collision_face(
            collision, {point[0], point[3], point[2], point[1]},
            {0, 0, -0x1000});
        add_debug_collision_face(
            collision, {point[4], point[5], point[6], point[7]},
            {0, 0, 0x1000});
        add_debug_collision_face(
            collision, {point[0], point[1], point[5], point[4]},
            {0, -0x1000, 0});
        add_debug_collision_face(
            collision, {point[3], point[7], point[6], point[2]},
            {0, 0x1000, 0});
        add_debug_collision_face(
            collision, {point[1], point[2], point[6], point[5]},
            {0x1000, 0, 0});
        add_debug_collision_face(
            collision, {point[0], point[4], point[7], point[3]},
            {-0x1000, 0, 0});
    }
    return collision;
}

}  // namespace

void GameplayScene::on_enter(SceneManager& manager) {
    if (debug_room_) {
        command_available_ = {true, false, false};
        load_playable_harness();
        if (auto* music = manager.music()) {
            music->play_music(kTwilightFieldTheme);
        }
        return;
    }
    const auto& session = manager.mission_session();
    command_available_ = session.panel_loadout && session.panel_availability
        ? khdays::assets::ov002_command_availability(
            *session.panel_loadout, *session.panel_availability)
        : std::array<bool, 3>{true, false, false};
    story_day_ = manager.state().day();
    if (session.mission_id != 0U) {
        try {
            const auto sequence = khdays::resource::load_story_sequence(
                session.mission_id, story_day_);
            if (sequence && sequence->movie_path) {
                stored_day_after_movie_ = sequence->stored_day;
                if (sequence->request_kind == 2U
                    && sequence->request_argument == 0x191U) {
                    calendar_request_after_movie_ = static_cast<int>(
                        *sequence->request_argument);
                }
                story_movie_ = true;
                if (auto* music = manager.music()) {
                    music->stop_music();
                }
                video_player_ = manager.video();
                if (video_player_ != nullptr) {
                    video_player_->play_video(*sequence->movie_path);
                }
                return;
            }
        } catch (const std::exception&) {
            // The regular error panel below remains available when extracted
            // mission data is incomplete.
        }
    }
    load_playable_harness();
    if (auto* music = manager.music()) {
        music->play_music(kTwilightFieldTheme);
    }
}

void GameplayScene::load_playable_harness() {
    frame_ = 0;
    ready_ = false;
    battle_hud_.reset();
    command_menu_artwork_.reset();
    player_gauge_ = {};
    command_menu_ = {};
    command_index_ = 0U;
    weapon_profile_index_ = 0U;
    attack_step_ = 0U;
    attacking_ = false;
    attack_queued_ = false;
    attack_animations_.clear();
    attack_rows_.clear();
    weapon_profiles_.clear();
    player_textures_.clear();
    weapon_textures_.clear();
    weapon_.reset();
    debug_text_.reset();
    hud_hp_ = 0xffffU;
    hud_max_hp_ = 0xffffU;
    controls_text_ = khdays::resource::render_ui_text(
        kFont,
        debug_room_
            ? u"F1: CONSOLA  Z: ATACAR  A: PERFIL  SHIFT: ANIM"
            : u"FLECHAS: MOVER  Z: ATACAR  S: ORDEN  X: VOLVER");
    complete_text_ = khdays::resource::render_ui_text(
        kFont, u"DEMO COMPLETADA - ENTER PARA REINICIAR");

    try {
        if (debug_room_) {
            room_.clear();
            room_.push_back(make_debug_room());
            collision_ = make_debug_collision();
        } else {
            room_ = khdays::resource::load_world_models(kWorld, {2U});
            if (room_.empty()) {
                throw std::runtime_error("wd_tt room model is unavailable");
            }

            const auto room_data =
                khdays::resource::room_data_subfile(kWorld, kRoom);
            if (!room_data) {
                throw std::runtime_error("wd_tt collision is unavailable");
            }
            const auto world = khdays::vfs::read(kWorldPath);
            const auto blob = khdays::assets::extract_p2_subfile(
                world.data(), world.size(), *room_data);
            collision_ = khdays::assets::decode_collision_model(
                blob.data(), blob.size());
            if (!collision_.valid) {
                throw std::runtime_error("wd_tt collision did not decode");
            }
        }

        const auto model_path = khdays::vfs::resolve(
            "ba/ch/ro/def.p/slot_7/0000.nsbmd");
        if (!model_path) {
            throw std::runtime_error("Roxas model is unavailable");
        }
        player_ = khdays::resource::load_model(*model_path);
        for (const auto& [name, texture] : player_->textures) {
            player_textures_.emplace(name, texture.image);
        }
        for (const auto& mesh : player_->model.meshes) {
            if (mesh.texture_name.empty()
                || player_textures_.count(mesh.texture_name) != 0U) {
                continue;
            }
            try {
                player_textures_.emplace(
                    mesh.texture_name,
                    khdays::resource::load_texture(
                        mesh.texture_name, *model_path).image);
            } catch (const std::exception&) {
                // Missing material textures fall back to vertex colour.
            }
        }

        if (const auto animation_path = khdays::vfs::resolve(
                "ba/ch/ro/def.p/slot_0/0000.nsbca")) {
            idle_animation_ =
                khdays::resource::load_animation(*animation_path, 0U);
            walk_animation_ =
                khdays::resource::load_animation(*animation_path, 1U);
        }

        const auto profiles = khdays::assets::lz_decompress(
            khdays::vfs::read(kRoxasWeaponProfiles));
        weapon_profiles_ = khdays::assets::decode_actor_weapon_profiles(
            profiles.data(), profiles.size());
        if (weapon_profiles_.empty()) {
            throw std::runtime_error("Roxas weapon profiles are unavailable");
        }
        load_actor_profile(0U);

        goal_model_ = make_goal_model();
        const auto probe = [this](const float x, const float z) {
            return ground_height(x, z);
        };
        controller_.reset(probe);
        camera_.reset(
            {controller_.state().x, controller_.state().y,
             controller_.state().z});
        controller_.set_camera_yaw(camera_.yaw_radians());
        battle_hud_ = khdays::resource::load_battle_hud_artwork();
        command_menu_artwork_ = khdays::resource::load_ov002_command_menu(
            khdays::game::localized_path("UI/btl/&/main.p2").c_str(),
            khdays::game::localized_path("UI/btl/&/cmd.s.z").c_str(),
            "text/font_eu_08s.nftr");
        if (command_menu_artwork_) {
            update_command_menu();
        }
        update_battle_hud();
        ready_ = !player_->model.meshes.empty()
            && ground_height(
                   controller_.goal_x(), controller_.goal_z()).has_value();
        if (!ready_) {
            throw std::runtime_error("playable room has no walkable start/goal");
        }
    } catch (const std::exception& error) {
        ready_ = false;
        std::cerr << (debug_room_ ? "debug room" : "gameplay harness")
                  << ": " << error.what() << '\n';
        error_text_ = khdays::resource::render_ui_text(
            kFont, u"DEMO NO DISPONIBLE - EXTRAE LOS DATOS DEL JUEGO");
    }
}

void GameplayScene::load_actor_profile(const std::size_t profile_index) {
    if (weapon_profiles_.empty()) {
        throw std::runtime_error("actor has no weapon profiles");
    }
    const std::size_t selected = profile_index % weapon_profiles_.size();
    const auto& profile = weapon_profiles_[selected];

    // func_ov022_020b0720 installs ci/cm and then claims am.p2 rows for the
    // selected 0x20-byte action profile. Keep that exact indirection here:
    // graph action id -> CM record -> AM sub-file.
    const auto ci = khdays::assets::lz_decompress(
        khdays::vfs::read(kRoxasActionGraph));
    const auto cm = khdays::assets::lz_decompress(
        khdays::vfs::read(kRoxasActionMetadata));
    const auto action_ids = khdays::assets::decode_actor_action_ids(
        ci.data(), ci.size(), profile.combo_variant, 0U);
    const auto animation_archive = khdays::vfs::read(kRoxasAnimationBank);

    std::vector<std::size_t> rows;
    std::vector<khdays::assets::SkeletalAnimation> animations;
    for (const auto action_id : action_ids) {
        const auto row = khdays::assets::actor_action_animation_row(
            cm.data(), cm.size(), action_id);
        if (!row || std::find(rows.begin(), rows.end(), *row) != rows.end()) {
            continue;
        }
        const auto blob = khdays::assets::extract_p2_subfile(
            animation_archive.data(), animation_archive.size(), *row);
        const auto pack = khdays::assets::parse_slot_container(
            blob.data(), blob.size());
        if (!pack.valid || pack.slots.empty() || pack.slots[0].empty()) {
            continue;
        }
        const auto& bca = pack.slots[0].front();
        auto animation = khdays::assets::load_nsbca(
            bca.data, bca.size, 0U);
        if (animation.frame_count == 0U) {
            continue;
        }
        rows.push_back(*row);
        animations.push_back(std::move(animation));
    }
    if (animations.empty()) {
        throw std::runtime_error("selected action graph has no animations");
    }

    const auto weapon_archive = khdays::vfs::read(kRoxasWeaponBank);
    const auto weapon_blob = khdays::assets::extract_p2_subfile(
        weapon_archive.data(), weapon_archive.size(), profile.model_index);
    const auto weapon_pack = khdays::assets::parse_slot_container(
        weapon_blob.data(), weapon_blob.size());
    if (!weapon_pack.valid || weapon_pack.slots.size() <= 7U
        || weapon_pack.slots[7].empty()) {
        throw std::runtime_error("selected weapon model is unavailable");
    }
    const auto& bmd = weapon_pack.slots[7].front();
    khdays::resource::LoadedModel loaded_weapon;
    loaded_weapon.model = khdays::assets::decode_model_geometry(
        bmd.data, bmd.size);
    if (loaded_weapon.model.meshes.empty()) {
        throw std::runtime_error("selected weapon model has no geometry");
    }
    std::map<std::string, khdays::assets::DecodedTexture> weapon_textures;
    for (const auto& mesh : loaded_weapon.model.meshes) {
        if (mesh.texture_name.empty()
            || weapon_textures.count(mesh.texture_name) != 0U) {
            continue;
        }
        try {
            weapon_textures.emplace(
                mesh.texture_name,
                khdays::assets::load_tex0_texture(
                    bmd.data, bmd.size, mesh.texture_name));
        } catch (const std::exception&) {
            // Missing material textures fall back to vertex colour.
        }
    }

    weapon_profile_index_ = selected;
    attack_step_ = 0U;
    attack_rows_ = std::move(rows);
    attack_animations_ = std::move(animations);
    weapon_ = std::move(loaded_weapon);
    weapon_textures_ = std::move(weapon_textures);
    update_debug_text();
}

void GameplayScene::update_debug_text() {
    if (!debug_room_ || weapon_profiles_.empty()
        || weapon_profile_index_ >= weapon_profiles_.size()) {
        debug_text_.reset();
        return;
    }
    const auto& profile = weapon_profiles_[weapon_profile_index_];
    std::ostringstream line;
    line << "WP " << weapon_profile_index_
         << "  W " << static_cast<unsigned>(profile.model_index)
         << "  HIT " << static_cast<unsigned>(profile.hit_model_index) + 0xa0U
         << "  CI " << static_cast<unsigned>(profile.combo_variant)
         << "  AM ";
    if (attack_rows_.empty()) {
        line << '-';
    } else {
        line << attack_step_ + 1U << '/' << attack_rows_.size()
             << " ROW " << attack_rows_[attack_step_];
    }
    debug_text_ = khdays::resource::render_ui_text(
        kFont, khdays::assets::message_from_utf8(line.str()));
}

void GameplayScene::finish_story_movie(SceneManager& manager) {
    if (video_player_ != nullptr) {
        video_player_->stop_video();
    }
    video_player_ = nullptr;
    video_frame_ = {};
    story_movie_ = false;
    movie_exiting_ = false;
    movie_exit_fade_ = 0;

    if (stored_day_after_movie_) {
        manager.state().set_day(*stored_day_after_movie_);
    }
    if (calendar_request_after_movie_) {
        // 400.Z's `_i` issues pending request (2, 0x191); ov002 resolves it
        // through ov004, which selects day 7 and re-enters mission 10000.
        manager.change_scene(
            kSceneDayTransition, *calendar_request_after_movie_);
        return;
    }

    // 7.Z begins with 803.mods. The next reconstruction slice is its CAKP
    // command stream (room, actors, dialogue and retail HUD); until then the
    // existing room harness remains reachable after that real intro.
    load_playable_harness();
    if (auto* music = manager.music()) {
        music->play_music(kTwilightFieldTheme);
    }
}

void GameplayScene::reset_actor() {
    const auto probe = [this](const float x, const float z) {
        return ground_height(x, z);
    };
    controller_.reset(probe);
    camera_.reset(
        {controller_.state().x, controller_.state().y,
         controller_.state().z});
    controller_.set_camera_yaw(camera_.yaw_radians());
    animation_frame_ = 0.0F;
    attacking_ = false;
    attack_queued_ = false;
    attack_step_ = 0U;
    update_debug_text();
}

std::string GameplayScene::execute_debug_command(
    SceneManager& manager, const std::string_view command) {
    if (!debug_room_) {
        return "commands are restricted to the developer room";
    }

    std::istringstream input{std::string{command}};
    std::string verb;
    input >> verb;
    if (verb.empty()) {
        return {};
    }
    if (verb == "help") {
        return "help | status | reset | bgm field|battle|stop|NAME | "
               "profile N | anim N | spawn ID";
    }
    if (verb == "status") {
        const auto& actor = controller_.state();
        std::ostringstream status;
        status << "Roxas pos=(" << actor.x << ", " << actor.y << ", "
               << actor.z << ") WP=" << weapon_profile_index_
               << " AM=" << attack_step_ << " actors=1";
        return status.str();
    }
    if (verb == "reset") {
        reset_actor();
        return "Roxas reset through the gameplay controller";
    }
    if (verb == "bgm") {
        std::string track;
        input >> track;
        if (track.empty()) {
            return "usage: bgm field|battle|stop|SDAT_NAME";
        }
        auto* music = manager.music();
        if (music == nullptr) {
            return "audio backend is unavailable";
        }
        if (track == "stop") {
            music->stop_music();
            return "BGM stopped";
        }
        if (track == "field") {
            track = kTwilightFieldTheme;
        } else if (track == "battle") {
            track = kTwilightBattleTheme;
        }
        music->play_music(track);
        return "BGM requested from SDAT: " + track;
    }
    if (verb == "profile") {
        std::size_t profile = 0U;
        if (!(input >> profile)) {
            return "usage: profile N";
        }
        if (profile >= weapon_profiles_.size()) {
            return "profile is outside Roxas's decoded WP table";
        }
        try {
            load_actor_profile(profile);
            animation_frame_ = 0.0F;
            return "loaded real Roxas WP/CI/CM/AM profile "
                + std::to_string(profile);
        } catch (const std::exception& error) {
            return std::string{"profile load failed: "} + error.what();
        }
    }
    if (verb == "anim") {
        std::size_t animation = 0U;
        if (!(input >> animation)) {
            return "usage: anim N";
        }
        if (animation >= attack_animations_.size()) {
            return "animation is outside the decoded AM action rows";
        }
        attack_step_ = animation;
        attacking_ = true;
        attack_queued_ = false;
        animation_frame_ = 0.0F;
        update_debug_text();
        return "previewing decoded AM row "
            + std::to_string(attack_rows_[animation]);
    }
    if (verb == "spawn") {
        std::string actor;
        input >> actor;
        if (actor.empty()) {
            return "usage: spawn ACTOR_ID";
        }
        return "spawn rejected: enemy actor construction is not connected "
               "yet; no placeholder was created";
    }
    return "unknown command; use help";
}

std::optional<float> GameplayScene::ground_height(
    const float x,
    const float z) const {
    if (!collision_.valid) {
        return std::nullopt;
    }
    const auto to_fx = [](const float value) {
        return static_cast<std::int32_t>(std::lround(value * kFxScale));
    };
    const auto hit = khdays::assets::ground_at(
        collision_, to_fx(x), to_fx(z), to_fx(64.0F), to_fx(-64.0F));
    if (!hit.hit) {
        return std::nullopt;
    }
    return static_cast<float>(hit.y) / kFxScale;
}

void GameplayScene::update_animation() {
    if (!player_ || !idle_animation_
        || idle_animation_->frame_count == 0U
        || player_->model.skinning == nullptr) {
        return;
    }
    const bool playing_attack = attacking_
        && attack_step_ < attack_animations_.size()
        && attack_animations_[attack_step_].frame_count > 0U;
    const bool moving = !playing_attack && controller_.state().moving;
    const khdays::assets::SkeletalAnimation* animation = &*idle_animation_;
    if (playing_attack) {
        animation = &attack_animations_[attack_step_];
    } else if (moving && walk_animation_ && walk_animation_->frame_count > 0U) {
        animation = &*walk_animation_;
    }
    if (!playing_attack && moving != animation_was_moving_) {
        animation_frame_ = 0.0F;
        animation_was_moving_ = moving;
    }
    if (!playing_attack) {
        animation_frame_ += 1.0F;
        animation_frame_ = std::fmod(
            animation_frame_, static_cast<float>(animation->frame_count));
    }
    const auto objects = khdays::assets::sample_animation(
        *animation, animation_frame_,
        player_->model.object_matrices);
    player_->model.palette = khdays::assets::compute_palette(
        *player_->model.skinning, objects);

    weapon_attached_ = false;
    if (weapon_) {
        const auto target = std::find(
            player_->model.object_names.begin(),
            player_->model.object_names.end(), "ro_w_tg_R");
        if (target != player_->model.object_names.end()) {
            const auto bone_world =
                khdays::assets::compute_bone_world_matrices(
                    *player_->model.skinning, objects);
            const auto index = static_cast<std::size_t>(
                std::distance(player_->model.object_names.begin(), target));
            if (index < bone_world.size()) {
                weapon_bone_transform_ = bone_world[index];
                weapon_attached_ = true;
            }
        }
    }

    if (playing_attack) {
        animation_frame_ += 1.0F;
        if (animation_frame_
            >= static_cast<float>(animation->frame_count)) {
            animation_frame_ = 0.0F;
            if (attack_queued_
                && attack_step_ + 1U < attack_animations_.size()) {
                ++attack_step_;
                attack_queued_ = false;
                update_debug_text();
            } else {
                attacking_ = false;
                attack_queued_ = false;
                attack_step_ = 0U;
                animation_was_moving_ = false;
                update_debug_text();
            }
        }
    }
}

void GameplayScene::update_battle_hud() {
    if (!battle_hud_) {
        return;
    }
    const auto& state = controller_.state();
    if (state.hp == hud_hp_ && state.max_hp == hud_max_hp_) {
        return;
    }
    hud_hp_ = state.hp;
    hud_max_hp_ = state.max_hp;
    player_gauge_ = khdays::assets::compose_ov002_player_gauge(
        battle_hud_->player_gauge, state.hp, state.max_hp);
}

void GameplayScene::update_command_menu() {
    if (!command_menu_artwork_) {
        return;
    }
    command_menu_ = khdays::assets::compose_ov002_command_menu(
        *command_menu_artwork_, command_index_);
}

void GameplayScene::update(SceneManager& manager) {
    ++frame_;
    const Input& input = manager.input();
    if (story_movie_) {
        if (input.just_pressed(Button::Start) && !movie_exiting_) {
            movie_exiting_ = true;
            movie_exit_fade_ = 0;
        }
        if (video_player_ != nullptr) {
            video_frame_ = video_player_->video_frame();
        }
        if (movie_exiting_) {
            if (++movie_exit_fade_ >= 16) {
                finish_story_movie(manager);
            }
        } else if (video_player_ == nullptr
                   || !video_player_->video_playing()) {
            finish_story_movie(manager);
        }
        return;
    }
    if (input.just_pressed(Button::B)) {
        // Returning from the development harness must not leak story mission
        // 10000 into a later direct/menu launch of scene 2.
        manager.mission_session() = {};
        manager.change_scene(kSceneTitle);
        return;
    }
    if (!ready_) {
        return;
    }

    if (debug_room_ && !attacking_ && input.just_pressed(Button::Y)
        && weapon_profiles_.size() > 1U) {
        for (std::size_t offset = 1U; offset <= weapon_profiles_.size();
             ++offset) {
            const auto candidate =
                (weapon_profile_index_ + offset) % weapon_profiles_.size();
            try {
                load_actor_profile(candidate);
                animation_frame_ = 0.0F;
                break;
            } catch (const std::exception&) {
                // Some rows intentionally refer to actor-specific assets not
                // present in Roxas's archive; keep walking the real table.
            }
        }
    }
    if (debug_room_ && !attacking_
        && input.just_pressed(Button::Select)
        && !attack_animations_.empty()) {
        attack_step_ = (attack_step_ + 1U) % attack_animations_.size();
        attacking_ = true;
        attack_queued_ = false;
        animation_frame_ = 0.0F;
        update_debug_text();
    }

    // ov022 tests the DS X bit (0x400) before calling
    // func_ov002_02056cc8 -> func_ov002_0205d658. That routine advances the
    // primary command ring, skipping slot value 7 and wrapping at the end.
    if (!attacking_ && input.just_pressed(Button::X)
        && command_menu_artwork_) {
        const std::size_t next = khdays::assets::advance_ov002_command(
            command_index_, command_available_);
        if (next != command_index_) {
            command_index_ = next;
            update_command_menu();
        }
    }

    const auto probe = [this](const float x, const float z) {
        return ground_height(x, z);
    };
    const auto motion_probe = [this](
        const float from_x, const float from_y, const float from_z,
        const float to_x, const float to_y, const float to_z,
        const float radius) {
        return !khdays::assets::sweep_sphere(
                    collision_, {from_x, from_y, from_z},
                    {to_x, to_y, to_z}, radius)
                    .hit;
    };

    camera_.update(
        input,
        {controller_.state().x, controller_.state().y,
         controller_.state().z},
        [this](
            const GameplayCamera::Vec3& focus,
            const GameplayCamera::Vec3& wanted_eye) {
            return resolve_gameplay_camera_collision(
                collision_, focus, wanted_eye);
        });
    controller_.set_camera_yaw(camera_.yaw_radians());

    // ov022's A bit reaches func_ov002_02056d48 and
    // func_ov002_0205dae4. Primary action 9 is Attack. The motion list here
    // follows wp -> ci -> cm -> am.p2; ab.p2 is the separate effect bank.
    if (!attacking_ && !controller_.state().completed
        && input.just_pressed(Button::A)
        && khdays::assets::activate_ov002_command(
               command_index_, command_available_)
            == khdays::assets::Ov002CommandPage::Attack
        && !attack_animations_.empty()
        && attack_animations_[0].frame_count > 0U) {
        attack_step_ = 0U;
        attacking_ = true;
        attack_queued_ = false;
        animation_frame_ = 0.0F;
        update_debug_text();
    } else if (attacking_ && input.just_pressed(Button::A)) {
        // The exact cancel/window data is not applied yet; this deliberately
        // exposes the next graph row for inspection when the clip completes.
        attack_queued_ = true;
    }
    if (controller_.state().completed
        && input.just_pressed(Button::Start)) {
        reset_actor();
    } else if (attacking_) {
        // The attack state owns motion until its non-looping clip completes.
        // An empty input also clears a locomotion flag left by the prior frame.
        controller_.update(Input{}, probe, motion_probe);
    } else {
        controller_.update(input, probe, motion_probe);
    }
    update_battle_hud();
    update_animation();
}

void GameplayScene::render(SceneManager&, Renderer& renderer) {
    renderer.clear(Color{6, 8, 16, 255});
    const auto layout = dual_screen_layout(renderer);

    if (story_movie_) {
        renderer.clear(Color{0, 0, 0, 255});
        if (video_frame_.rgba != nullptr && video_frame_.width > 0
            && video_frame_.height > 0) {
            renderer.draw_image_dynamic(
                video_frame_.rgba, video_frame_.width, video_frame_.height,
                layout.top_x, layout.top_y,
                DualScreenLayout::kScreenW * layout.scale,
                160 * layout.scale);
        }
        if (movie_exiting_) {
            const int alpha = std::clamp(movie_exit_fade_, 0, 16) * 255 / 16;
            renderer.fill_overlay(Color{
                0, 0, 0, static_cast<std::uint8_t>(alpha)});
        }
        return;
    }

    if (ready_ && player_) {
        std::vector<khdays::assets::ModelInstance> instances;
        instances.reserve(room_.size() + 3U);
        for (const auto& piece : room_) {
            instances.push_back(
                khdays::assets::ModelInstance{&piece.model, &piece.textures});
        }

        const auto goal_y =
            ground_height(controller_.goal_x(), controller_.goal_z())
                .value_or(0.0F);
        instances.push_back(khdays::assets::ModelInstance{
            &goal_model_, nullptr,
            actor_transform(
                controller_.goal_x(), goal_y, controller_.goal_z(), 0.0F)});

        const auto& player = controller_.state();
        const auto player_transform = actor_transform(
            player.x, player.y, player.z, player.facing + kPi);
        instances.push_back(khdays::assets::ModelInstance{
            &player_->model, &player_textures_,
            player_transform});
        if (weapon_ && weapon_attached_) {
            instances.push_back(khdays::assets::ModelInstance{
                &weapon_->model, &weapon_textures_,
                matrix_multiply(player_transform, weapon_bone_transform_)});
        }

        scene_frame_ =
            khdays::assets::render_scene(
                instances, camera_.camera(), 256, 192);
        draw_screen_dynamic(renderer, layout, scene_frame_, false);

        // func_ov002_0205ad5c anchors the default three-entry page to the
        // lower-left: localized header at y=128, then rows at 144/160/176.
        if (command_menu_artwork_) {
            draw_overlay_dynamic(
                renderer, layout, command_menu_, 0, 128, false);
        }

        // The local-player cluster occupies ov002's original lower-right
        // 48x48 portrait slot. Its 80-pixel gauge overlaps the portrait's
        // bottom row and the HP label completes the strip at the right edge.
        if (battle_hud_) {
            draw_overlay(
                renderer, layout, battle_hud_->roxas_portrait,
                208, 144, false);
            draw_overlay(
                renderer, layout, battle_hud_->player_label,
                196, 176, false);
            draw_overlay_dynamic(
                renderer, layout, player_gauge_,
                160, 184, false);
            draw_overlay(
                renderer, layout, battle_hud_->hp_label,
                240, 184, false);
        }
    }

    const auto draw_bottom_centered =
        [&](const khdays::assets::DecodedTexture& text, const int y) {
            draw_overlay(
                renderer, layout, text, (256 - text.width) / 2, y, true);
        };
    if (!ready_) {
        if (error_text_) {
            draw_bottom_centered(*error_text_, 88);
        }
    } else {
        if (debug_text_) {
            draw_bottom_centered(*debug_text_, 12);
        }
        if (controls_text_) {
            draw_bottom_centered(*controls_text_, 172);
        }
        if (controller_.state().completed && complete_text_) {
            draw_bottom_centered(*complete_text_, 82);
        }
    }

    if (frame_ < kFadeIn) {
        const int alpha = 255 * (kFadeIn - frame_) / kFadeIn;
        renderer.fill_overlay(
            Color{0, 0, 0, static_cast<std::uint8_t>(alpha)});
    }
}

void GameplayScene::on_exit(SceneManager& manager) {
    if (video_player_ != nullptr) {
        video_player_->stop_video();
    }
    if (auto* music = manager.music()) {
        music->stop_music();
    }
    video_player_ = nullptr;
    video_frame_ = {};
}

}  // namespace khdays::game::scenes
