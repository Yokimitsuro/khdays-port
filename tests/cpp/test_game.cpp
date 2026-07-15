#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "khdays/game/game.h"
#include "khdays/game/object.h"
#include "khdays/game/scene.h"

namespace {

using namespace khdays::game;

struct FlowLog {
    std::vector<SceneId> entered;
    int logo_frames = 0;
    int title_frames = 0;
};

// The boot/logo scene: after 3 frames it hands off to the continue scene.
class LogoScene final : public Scene {
public:
    explicit LogoScene(FlowLog* log) : log_(log) {}
    void update(SceneManager& manager) override {
        if (++log_->logo_frames == 3) {
            manager.change_scene(kSceneContinue);
        }
    }

private:
    FlowLog* log_;
};

class TitleScene final : public Scene {
public:
    explicit TitleScene(FlowLog* log) : log_(log) {}
    void update(SceneManager&) override { ++log_->title_frames; }

private:
    FlowLog* log_;
};

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        // --- scene flow ---
        FlowLog log;
        Game game;
        game.scenes().on_scene_entered(
            [&](SceneId id, int) { log.entered.push_back(id); });
        game.scenes().register_scene(
            kSceneBootLogo, [&] { return std::make_unique<LogoScene>(&log); });
        game.scenes().register_scene(
            kSceneContinue, [&] { return std::make_unique<TitleScene>(&log); });

        game.boot(0);  // fresh boot → the logo scene
        expect(game.scenes().current_id() == kSceneBootLogo, "boot enters logo");
        expect(log.entered.size() == 1U && log.entered[0] == kSceneBootLogo,
               "logo entered once");

        game.step();  // logo frame 1
        game.step();  // logo frame 2
        game.step();  // logo frame 3 -> requests continue, transitions
        expect(game.scenes().current_id() == kSceneContinue,
               "transitioned to continue");
        expect(log.entered.size() == 2U && log.entered[1] == kSceneContinue,
               "continue entered");
        expect(log.logo_frames == 3, "logo ran three frames");

        game.step();  // title frame
        expect(log.title_frames == 1, "title updates after transition");

        // A "continue" boot state skips the logo.
        Game cont;
        cont.scenes().register_scene(
            kSceneBootLogo, [&] { return std::make_unique<LogoScene>(&log); });
        cont.scenes().register_scene(
            kSceneContinue, [&] { return std::make_unique<TitleScene>(&log); });
        cont.boot(5);
        expect(cont.scenes().current_id() == kSceneContinue,
               "non-fresh boot skips to continue");

        // --- object state machine (the func_02023adc model) ---
        ObjectList objects;
        int a_runs = 0;
        int b_runs = 0;
        // State A runs once then hands off to state B; B runs once then ends.
        Object::State state_b = [&](Object& self) {
            ++b_runs;
            self.finish();
        };
        objects.spawn([&, state_b](Object& self) {
            ++a_runs;
            self.go_to(state_b);
        });
        objects.update();  // A runs -> switches to B
        expect(a_runs == 1 && b_runs == 0 && objects.size() == 1U, "state A");
        objects.update();  // B runs -> finishes
        expect(b_runs == 1, "state B");
        objects.update();  // B was removed
        expect(objects.empty(), "finished object removed");

        // An object spawned during update first runs next frame.
        int child_runs = 0;
        bool spawned_child = false;
        objects.spawn([&](Object& self) {
            if (!spawned_child) {
                spawned_child = true;
                objects.spawn([&](Object& c) { ++child_runs; c.finish(); });
            }
            self.finish();
        });
        objects.update();
        expect(child_runs == 0 && objects.size() == 1U, "child staged");
        objects.update();
        expect(child_runs == 1 && objects.empty(), "child ran next frame");

        std::cout << "Game-flow test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game-flow test failed: " << error.what() << '\n';
        return 1;
    }
}
