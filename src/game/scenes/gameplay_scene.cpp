#include "khdays/game/scenes/gameplay_scene.h"

#include <cmath>

#include "khdays/game/draw.h"
#include "khdays/resource/ui_content.h"

namespace khdays::game::scenes {

namespace {
constexpr char kFont[] = "text/font_eu_10all.nftr";
constexpr int kFadeIn = 30;

// PORT CHOICE, not the game's. Which room a mission shows is decided by the
// mission script -- room loading is a script VM opcode
// (FUN_arm9_ov002__02074d0c reads its two operands and loads the room), and
// neither the script format nor the mission data that selects a world is
// decoded. Traverse Town's District 3 stands in because it is the smallest
// shipped world, so it loads fast and is easy to recognise.
constexpr char kWorldCode[] = "tw";
constexpr std::size_t kRoomIndex = 0;

// Camera orbit speed, radians per frame, while a direction is held.
constexpr float kOrbitStep = 0.03F;
constexpr float kPitchLimit = 1.4F;

// How much of the bounding-sphere distance to keep; < 1 fills more screen.
constexpr float kFrameFill = 0.55F;
}  // namespace

void GameplayScene::on_enter(SceneManager&) {
    room_ = khdays::resource::load_world_room(kWorldCode, kRoomIndex);
    if (!room_.has_value()) {
        // No game data, or the room could not be read: keep the marker so the
        // scene still shows the state it reached rather than a black screen.
        marker_ = khdays::resource::render_ui_text(kFont, u"GAMEPLAY (ov002)");
    }
}

void GameplayScene::update(SceneManager& manager) {
    ++frame_;
    const auto& in = manager.input();
    // A free orbit, so the loaded room can be inspected. The DS camera is a
    // different thing entirely -- its distance selector table and occlusion
    // probing are recorded in docs/GAMEPLAY_RUNTIME.md and are not used here,
    // because what picks a selector is not decoded.
    if (in.held(Button::Left)) {
        yaw_ -= kOrbitStep;
    }
    if (in.held(Button::Right)) {
        yaw_ += kOrbitStep;
    }
    if (in.held(Button::Up)) {
        pitch_ = std::min(kPitchLimit, pitch_ + kOrbitStep);
    }
    if (in.held(Button::Down)) {
        pitch_ = std::max(-kPitchLimit, pitch_ - kOrbitStep);
    }
    if (in.just_pressed(Button::B)) {
        manager.change_scene(kSceneTitle);
    }
}

void GameplayScene::render(SceneManager&, Renderer& r) {
    r.clear(Color{0, 0, 0, 255});
    const auto layout = dual_screen_layout(r);

    if (room_.has_value() && !room_->models.empty()) {
        std::vector<khdays::assets::ModelInstance> instances;
        instances.reserve(room_->models.size());
        for (const auto& piece : room_->models) {
            instances.push_back(
                khdays::assets::ModelInstance{&piece.model, &piece.textures});
        }
        // The room's sub-files share one world space, so a single camera and
        // depth buffer reassemble it.
        // Framing to the bounding sphere is conservative -- a room's roof
        // planes spread far past its floor -- so pull in to fill the screen.
        const auto camera =
            khdays::assets::frame_scene(instances, yaw_, pitch_, kFrameFill);
        view_ = khdays::assets::render_scene(
            instances, camera, DualScreenLayout::kScreenW, DualScreenLayout::kScreenH);
        // Per-frame pixels, so the dynamic path (the SDL texture cache is
        // pointer-keyed and would otherwise hold the first frame).
        draw_screen_dynamic(r, layout, view_, /*bottom=*/false, 255);
    } else if (marker_) {
        draw_centered(r, *marker_, 2);
    }

    if (frame_ < kFadeIn) {
        const int a = 255 * (kFadeIn - frame_) / kFadeIn;
        r.fill_overlay(Color{0, 0, 0, static_cast<std::uint8_t>(a)});
    }
}

}  // namespace khdays::game::scenes
