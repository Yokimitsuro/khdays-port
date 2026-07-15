#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "khdays/game/game.h"
#include "khdays/game/scene.h"
#include "khdays/game/task.h"

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

        // --- task queue ---
        TaskQueue queue;
        int runs = 0;
        queue.add([&] { return ++runs < 3; });  // finishes on the third run
        queue.update();
        queue.update();
        queue.update();
        expect(runs == 3 && queue.empty(), "task removed when finished");

        // A task added during update runs on the next frame, not this one.
        int child = 0;
        bool spawned = false;
        queue.add([&] {
            if (!spawned) {
                spawned = true;
                queue.add([&] { ++child; return false; });
            }
            return false;
        });
        queue.update();
        expect(child == 0 && queue.size() == 1U, "child staged for next frame");
        queue.update();
        expect(child == 1 && queue.empty(), "child ran next frame");

        std::cout << "Game-flow test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game-flow test failed: " << error.what() << '\n';
        return 1;
    }
}
