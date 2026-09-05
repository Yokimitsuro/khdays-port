#include "khdays/game/scenes/gameplay_scene.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

#include "khdays/assets/message.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/scene3d.h"
#include "khdays/game/draw.h"
#include "khdays/resource/ui_content.h"
#include "khdays/resource/mission.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::game::scenes {

namespace {

constexpr char kFont[] = "text/font_eu_08.nftr";
constexpr char kWorld[] = "tt";
constexpr char kWorldPath[] = "mi/wd/wd_tt";
constexpr std::size_t kRoom = 0U;
constexpr int kFadeIn = 30;
constexpr float kFxScale = 4096.0F;
constexpr float kPi = 3.14159265358979323846F;

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

}  // namespace

void GameplayScene::on_enter(SceneManager& manager) {
    const auto& session = manager.mission_session();
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
}

void GameplayScene::load_playable_harness() {
    frame_ = 0;
    ready_ = false;
    battle_hud_.reset();
    player_gauge_ = {};
    hud_hp_ = 0xffffU;
    hud_max_hp_ = 0xffffU;
    controls_text_ = khdays::resource::render_ui_text(
        kFont, u"FLECHAS: MOVER  Q/E: CAMARA  X: VOLVER");
    complete_text_ = khdays::resource::render_ui_text(
        kFont, u"DEMO COMPLETADA - ENTER PARA REINICIAR");

    try {
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

        // w_d00.p is an attack effect, not the held weapon. Roxas's actual
        // weapon models live as KAPH sub-files in w_.p2; sub-file 1 is the
        // default ro_w01000 Keyblade used by this playable slice.
        const auto weapon_archive = khdays::vfs::read("ba/ch/ro/w_.p2");
        const auto weapon_blob = khdays::assets::extract_p2_subfile(
            weapon_archive.data(), weapon_archive.size(), 1U);
        const auto weapon_pack = khdays::assets::parse_slot_container(
            weapon_blob.data(), weapon_blob.size());
        if (weapon_pack.valid && weapon_pack.slots.size() > 7U
            && !weapon_pack.slots[7].empty()) {
            const auto& bmd = weapon_pack.slots[7].front();
            khdays::resource::LoadedModel loaded;
            loaded.model = khdays::assets::decode_model_geometry(
                bmd.data, bmd.size);
            weapon_ = std::move(loaded);
            for (const auto& mesh : weapon_->model.meshes) {
                if (mesh.texture_name.empty()
                    || weapon_textures_.count(mesh.texture_name) != 0U) {
                    continue;
                }
                try {
                    weapon_textures_.emplace(
                        mesh.texture_name,
                        khdays::assets::load_tex0_texture(
                            bmd.data, bmd.size, mesh.texture_name));
                } catch (const std::exception&) {
                    // Missing material textures fall back to vertex colour.
                }
            }
        }

        goal_model_ = make_goal_model();
        const auto probe = [this](const float x, const float z) {
            return ground_height(x, z);
        };
        controller_.reset(probe);
        battle_hud_ = khdays::resource::load_battle_hud_artwork();
        update_battle_hud();
        ready_ = !player_->model.meshes.empty()
            && ground_height(
                   controller_.goal_x(), controller_.goal_z()).has_value();
        if (!ready_) {
            throw std::runtime_error("playable room has no walkable start/goal");
        }
    } catch (const std::exception&) {
        ready_ = false;
        error_text_ = khdays::resource::render_ui_text(
            kFont, u"DEMO NO DISPONIBLE - EXTRAE LOS DATOS DEL JUEGO");
    }
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
    const bool moving = controller_.state().moving;
    const khdays::assets::SkeletalAnimation* animation = &*idle_animation_;
    if (moving && walk_animation_ && walk_animation_->frame_count > 0U) {
        animation = &*walk_animation_;
    }
    if (moving != animation_was_moving_) {
        animation_frame_ = 0.0F;
        animation_was_moving_ = moving;
    }
    animation_frame_ += 1.0F;
    animation_frame_ = std::fmod(
        animation_frame_, static_cast<float>(animation->frame_count));
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
    if (controller_.state().completed
        && input.just_pressed(Button::Start)) {
        controller_.reset(probe);
        animation_frame_ = 0.0F;
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

        khdays::assets::Camera3D camera;
        camera.target = {player.x, player.y + 0.9F, player.z};
        camera.eye = {
            player.x + std::sin(player.camera_yaw) * 5.2F,
            player.y + 3.2F,
            player.z + std::cos(player.camera_yaw) * 5.2F};
        camera.fov_y = 0.82F;
        camera.near_z = 0.05F;
        camera.far_z = 180.0F;
        scene_frame_ =
            khdays::assets::render_scene(instances, camera, 256, 192);
        draw_screen_dynamic(renderer, layout, scene_frame_, false);

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

void GameplayScene::on_exit(SceneManager&) {
    if (video_player_ != nullptr) {
        video_player_->stop_video();
    }
    video_player_ = nullptr;
    video_frame_ = {};
}

}  // namespace khdays::game::scenes
