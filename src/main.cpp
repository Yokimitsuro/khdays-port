#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <SDL3/SDL_main.h>

#include "khdays/assets/animation.h"
#include "khdays/assets/audio.h"
#include "khdays/assets/cell.h"
#include "khdays/assets/font.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/mdl0.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/mods.h"
#include "khdays/assets/sdat.h"
#include "khdays/assets/sequence.h"
#include "khdays/game/game.h"
#include "khdays/game/scenes/boot_logo_scene.h"
#include "khdays/game/scenes/gameplay_scene.h"
#include "khdays/game/scenes/main_menu_scene.h"
#include "khdays/game/scenes/title_scene.h"
#include "khdays/game/settings.h"
#include "khdays/game/software_renderer.h"
#include "khdays/platform/audio.h"
#include "khdays/platform/runtime.h"
#include "khdays/port.h"
#include "khdays/resource/loader.h"
#include "khdays/vfs/filesystem.h"

#ifndef KHDAYS_PORT_VERSION
#define KHDAYS_PORT_VERSION "unknown"
#endif

namespace {

// Placeholder scenes for the --game-demo: they only log, standing in for the
// real boot/logo and title scenes until khdays-decomp names their logic. They
// exercise the same scene/task framework the game will use.
class DemoLogoScene final : public khdays::game::Scene {
public:
    void update(khdays::game::SceneManager& manager) override {
        ++frames_;
        std::cout << "  [logo] frame " << frames_ << '\n';
        if (frames_ == 3) {
            std::cout << "  [logo] done -> requesting title\n";
            manager.change_scene(khdays::game::kSceneContinue);
        }
    }

private:
    int frames_ = 0;
};

class DemoTitleScene final : public khdays::game::Scene {
public:
    void update(khdays::game::SceneManager&) override {
        ++frames_;
        std::cout << "  [title] frame " << frames_ << '\n';
    }

private:
    int frames_ = 0;
};

int run_game_demo() {
    khdays::game::Game game;
    game.scenes().on_scene_entered([](khdays::game::SceneId id, int arg) {
        std::cout << "== enter scene " << id << " (arg " << arg << ") ==\n";
    });
    game.scenes().register_scene(
        khdays::game::kSceneBootLogo,
        [] { return std::make_unique<DemoLogoScene>(); });
    game.scenes().register_scene(
        khdays::game::kSceneContinue,
        [] { return std::make_unique<DemoTitleScene>(); });

    // A background object whose state-machine ticks a few frames (the game's
    // object model) alongside the scenes.
    int ticks = 0;
    game.objects().spawn([&ticks](khdays::game::Object& self) {
        std::cout << "  (object tick " << ++ticks << ")\n";
        if (ticks >= 4) {
            self.finish();
        }
    });

    std::cout << "boot (fresh) ...\n";
    game.boot(0);
    for (int i = 0; i < 8; ++i) {
        game.step();
    }
    std::cout << "ran " << game.frame() << " frames; final scene "
              << game.scenes().current_id() << '\n';
    return EXIT_SUCCESS;
}

// --- viewer skin payload ---------------------------------------------------
// Serializers for --export-skin. The gallery's WebGL viewer eats a Wavefront
// OBJ, which to_wavefront_obj() writes in the *rest pose*; the bones are gone
// by then, so the viewer cannot animate. This adds the missing half: the raw
// vertex positions with their palette indices, plus one matrix palette per
// animation frame, baked here exactly the way gpu_renderer.cpp bakes it live
// (sample_animation -> compute_palette). The viewer then skins in its vertex
// shader like the native renderer does.

// A palette is mostly 0s and 1s, so trailing zeros are trimmed. Five decimals
// resolves finer than the DS's own 20.12 fixed point (1/4096 ~= 0.00024), so
// nothing is lost against the source data.
std::string json_number(float value) {
    std::array<char, 32> buffer{};
    std::snprintf(
        buffer.data(), buffer.size(), "%.5f", static_cast<double>(value));
    std::string text{buffer.data()};
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1U);
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::string json_string(const std::string& value) {
    std::string out{"\""};
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20U) {
            std::array<char, 8> esc{};
            std::snprintf(esc.data(), esc.size(), "\\u%04x", c);
            out += esc.data();
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// All 16 floats of each matrix, column-major, exactly as compute_palette()
// returns them.
//
// Dropping the bottom row would shrink a payload by 8% (measured), and these
// matrices do look affine -- sample_animation builds TRS with a literal
// 0 0 0 1 bottom row. They are not. A check written to justify the shortcut
// found 9 of the 533 models whose palettes carry a w that is not 1 (ba/ch/ze,
// mi/ch/03 and 7 others: 1.015625 and 0.996094, i.e. 1 +/- a DS fixed-point
// step), and the port does not agree with itself about what that w means:
// model.vert multiplies the full mat4 and lets the GPU divide by it, while
// transform_point -- which is what to_wavefront_obj's rest pose goes through
// -- computes three rows and ignores it. So for those 9, the OBJ and the
// native render already disagree by ~1.5% on the affected bones.
//
// That is a real question about the port, not about this exporter, so the
// exporter refuses to be the one that answers it by discarding the evidence.
// All 16 go out; the viewer documents which convention it picked.
void write_matrices(
    std::ostringstream& out,
    const std::vector<std::array<float, 16>>& matrices) {
    bool first = true;
    for (const auto& matrix : matrices) {
        for (const float value : matrix) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << json_number(value);
        }
    }
}

// One animation already loaded and accepted for a model.
struct SkinAnimationInput final {
    std::string name;
    khdays::assets::SkeletalAnimation animation;
};

std::string to_skin_json(
    const khdays::assets::NeutralModel& model,
    const std::vector<SkinAnimationInput>& animations) {
    std::ostringstream out;
    out << "{\"name\":" << json_string(model.name)
        << ",\"paletteSize\":" << model.palette.size()
        << ",\"boneCount\":" << model.object_matrices.size();

    // Vertices are emitted in to_wavefront_obj()'s exact order (meshes in
    // order, vertices within each mesh), so `pos`/`joints`/`weights` line up
    // index-for-index with the OBJ's `v` list. The viewer re-checks that count
    // and falls back to the static OBJ if it ever disagrees.
    out << ",\"meshes\":[";
    bool first_mesh = true;
    for (const auto& mesh : model.meshes) {
        if (!first_mesh) {
            out << ',';
        }
        first_mesh = false;
        out << "{\"name\":" << json_string(mesh.name)
            << ",\"vertices\":" << mesh.vertices.size() << '}';
    }
    out << ']';

    out << ",\"pos\":[";
    bool first_value = true;
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (const float component : vertex.position) {
                if (!first_value) {
                    out << ',';
                }
                first_value = false;
                out << json_number(component);
            }
        }
    }
    out << ']';

    out << ",\"joints\":[";
    first_value = true;
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (const std::uint32_t joint : vertex.joints) {
                if (!first_value) {
                    out << ',';
                }
                first_value = false;
                out << joint;
            }
        }
    }
    out << ']';

    out << ",\"weights\":[";
    first_value = true;
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (const float weight : vertex.weights) {
                if (!first_value) {
                    out << ',';
                }
                first_value = false;
                out << json_number(weight);
            }
        }
    }
    out << ']';

    // The rest palette, so "no animation" re-poses from the same code path the
    // animated frames use. Skinning with it must reproduce the OBJ positions.
    out << ",\"rest\":[";
    write_matrices(out, model.palette);
    out << ']';

    out << ",\"anims\":[";
    bool first_anim = true;
    for (const auto& input : animations) {
        const auto& animation = input.animation;
        if (!first_anim) {
            out << ',';
        }
        first_anim = false;
        out << "{\"name\":" << json_string(input.name)
            << ",\"internalName\":" << json_string(animation.name)
            << ",\"frames\":" << animation.frame_count
            << ",\"matrices\":[";

        // Bake one palette per frame, as gpu_renderer.cpp does per displayed
        // frame. Frames are whole numbers here: these are the animation's own
        // keys, and the viewer interpolates nothing between them.
        bool first_frame = true;
        for (std::uint16_t frame = 0U; frame < animation.frame_count; ++frame) {
            const auto objects = khdays::assets::sample_animation(
                animation, static_cast<float>(frame), model.object_matrices);
            const auto palette =
                khdays::assets::compute_palette(*model.skinning, objects);
            if (!first_frame) {
                out << ',';
            }
            first_frame = false;
            write_matrices(out, palette);
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

// Decide whether an NSBCA may drive this model, and say why not when it may
// not. The two cases are genuinely different:
//
//  - Same container (<c>/slot_0/0000.nsbca next to <c>/slot_7/0000.nsbmd):
//    ownership is structural, so the animation IS this model's. It may drive
//    fewer bones than the model has -- sample_animation() maps bone-by-index
//    and leaves the rest at their rest matrix -- and 9 of the game's 374
//    sibling pairs do exactly that (mi/ch/4C is a 27-bone model with a 3-bone
//    animation). Accept it; only more bones than the model has is incoherent.
//
//  - Any other container: ownership is inferred, and an equal bone count is
//    the only evidence there is. A character folder mixes skeletons freely
//    (ba/ch/ax holds 26-bone Axel animations beside 18-bone li_ea2 and 7-bone
//    effects), so require exact equality rather than pose a model with a
//    stranger's animation.
std::string skin_animation_rejection(
    const khdays::assets::NeutralModel& model,
    const khdays::assets::SkeletalAnimation& animation,
    const bool same_container) {
    const auto model_bones = model.object_matrices.size();
    const auto anim_bones = animation.bones.size();

    // An NSBCA whose bones carry no curves poses nothing: every frame of it is
    // the rest pose (mi/ch/3B's sibling is one -- 2 frames, 1 bone, 0 curves).
    // Baking identical palettes for it would put a name in the menu that does
    // nothing when picked.
    if (std::none_of(
            animation.bones.begin(), animation.bones.end(),
            [](const khdays::assets::BoneCurves& bone) { return bone.animated; })) {
        return "drives no bones; every frame is the rest pose";
    }

    if (same_container) {
        if (anim_bones > model_bones) {
            return "sibling animation drives " + std::to_string(anim_bones)
                + " bones but the model only has "
                + std::to_string(model_bones);
        }
        return {};
    }
    if (anim_bones != model_bones) {
        return "foreign container and " + std::to_string(anim_bones)
            + " bones against the model's " + std::to_string(model_bones)
            + "; not this skeleton";
    }
    return {};
}

// Both files live at <container>/slot_N/0000.ext, so the container is the
// grandparent of each.
bool same_container(
    const std::filesystem::path& model_path,
    const std::filesystem::path& animation_path) {
    return model_path.parent_path().parent_path()
        == animation_path.parent_path().parent_path();
}

void print_version() {
    std::cout
        << khdays::port::Version::name << ' ' << KHDAYS_PORT_VERSION << '\n'
        << "Stage: " << khdays::port::Version::stage << '\n';
}

void print_help() {
    print_version();
    std::cout
        << '\n'
        << "Usage:\n"
        << "  khdays-port [--resource FILE] [--texture NAME]\n"
        << "  khdays-port --render-model FILE [--anim FILE]\n"
        << "  khdays-port --model-info FILE\n"
        << "  khdays-port --anim-info FILE\n"
        << "  khdays-port --audio-info FILE\n"
        << "  khdays-port --vfs-resolve GAMEPATH\n"
        << "  khdays-port --game\n"
        << "  khdays-port --game-demo\n"
        << "  khdays-port --render-tiles NCGR NCLR OUT.bmp [PALETTE]\n"
        << "  khdays-port --render-bg NSCR NCLR OUT.bmp NCGR [NCGR...]\n"
        << "  khdays-port --render-text NFTR TEXT OUT.bmp\n"
        << "  khdays-port --render-cell P2 SUBFILE CELL OUT.bmp\n"
        << "  khdays-port --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav\n"
        << "  khdays-port --play-sound SDAT WAVEARCHIVE SWAV\n"
        << "  khdays-port --render-sequence SDAT SEQ OUTPUT.wav [SECONDS]\n"
        << "  khdays-port --play-sequence SDAT SEQ [SECONDS]\n"
        << "  khdays-port --mods-info FILE\n"
        << "  khdays-port --message-info FILE\n"
        << "  khdays-port --dump-messages FILE [SUBDB]\n"
        << "  khdays-port --dump-strings FILE\n"
        << "  khdays-port --export-obj FILE [OUTPUT.obj]\n"
        << "  khdays-port --export-skin FILE OUTPUT.json [ANIM.nsbca...]\n"
        << "  khdays-port --version\n"
        << "  khdays-port --help\n"
        << '\n'
        << "Options:\n"
        << "  --resource FILE     Load TEX0 data from a user-extracted NSBMD/NSBTX.\n"
        << "  --texture NAME      Select a texture by name; defaults to the first.\n"
        << "  --render-model FILE Render an MDL0 model in 3D in the native window.\n"
        << "  --anim FILE         Play this NSBCA animation instead of the auto-detected one.\n"
        << "  --model-info FILE   Inspect MDL0 models, materials, meshes, and GPU commands.\n"
        << "  --anim-info FILE    Inspect an NSBCA skeletal animation.\n"
        << "  --vfs-resolve GAMEPATH  Resolve a NitroFS game path in the extracted data.\n"
        << "  --game              Run the scene/task frame loop in a window (placeholder scenes).\n"
        << "  --game-demo         Run the scene/task frame loop headless (logs the flow).\n"
        << "  --render-tiles NCGR NCLR OUT.bmp [PALETTE]  Render an NCGR tile sheet to BMP.\n"
        << "  --render-bg NSCR NCLR OUT.bmp NCGR...  Compose an NSCR background to BMP.\n"
        << "  --render-text NFTR TEXT OUT.bmp  Render TEXT with an NFTR font to BMP.\n"
        << "  --audio-info FILE   List the contents of an SDAT sound archive.\n"
        << "  --extract-wav SDAT WAVEARCHIVE SWAV OUTPUT.wav  Decode a SWAV waveform to WAV.\n"
        << "  --play-sound SDAT WAVEARCHIVE SWAV  Decode and play a SWAV waveform.\n"
        << "  --render-sequence SDAT SEQ OUT.wav [SECONDS]  Synthesize an SSEQ to WAV.\n"
        << "  --play-sequence SDAT SEQ [SECONDS]  Synthesize and play an SSEQ.\n"
        << "  --mods-info FILE    Summarize a MobiClip MODS cutscene container (mv/*.mods).\n"
        << "  --message-info FILE Summarize a P2 message container (db_<lang>.p2).\n"
        << "  --dump-messages FILE [SUBDB]  Print decoded UTF-8 text (optionally one sub-db).\n"
        << "  --dump-strings FILE Print a UI string table (.s/.s.z) as UTF-8.\n"
        << "  --export-obj FILE   Decode the first MDL0 model to a Wavefront OBJ mesh.\n"
        << "  --export-skin FILE OUT.json [ANIM...]  Export skinning data plus a baked\n"
        << "                      matrix palette per frame of each NSBCA, for a viewer\n"
        << "                      that skins the OBJ itself. An NSBCA in the model's own\n"
        << "                      container is accepted (it may drive fewer bones); one\n"
        << "                      from elsewhere needs an exact bone count to be believed.\n"
        << "  --version           Print version information without opening a window.\n"
        << "  --help              Show this help text.\n";
}

khdays::platform::ApplicationOptions parse_runtime_options(
    int argc,
    char* argv[]) {
    khdays::platform::ApplicationOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (argument == "--resource") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--resource requires a file path");
            }
            options.resource_path = std::filesystem::path{argv[++index]};
            continue;
        }

        if (argument == "--texture") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--texture requires a texture name");
            }
            options.texture_name = std::string{argv[++index]};
            continue;
        }

        if (argument == "--render-model") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--render-model requires a file path");
            }
            options.model_path = std::filesystem::path{argv[++index]};
            continue;
        }

        if (argument == "--anim") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--anim requires a file path");
            }
            options.animation_path = std::filesystem::path{argv[++index]};
            continue;
        }

        throw std::invalid_argument(
            "unknown argument: " + std::string{argument});
    }

    if (options.texture_name.has_value() && !options.resource_path.has_value()) {
        throw std::invalid_argument(
            "--texture can only be used together with --resource");
    }

    if (options.animation_path.has_value() && !options.model_path.has_value()) {
        throw std::invalid_argument(
            "--anim can only be used together with --render-model");
    }

    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view first{argv[1]};

        if (first == "--version") {
            print_version();
            return EXIT_SUCCESS;
        }

        if (first == "--help" || first == "-h") {
            print_help();
            return EXIT_SUCCESS;
        }

        if (first == "--model-info") {
            if (argc != 3) {
                std::cerr
                    << "ERROR: --model-info requires exactly one file path\n";
                return EXIT_FAILURE;
            }

            try {
                const auto information =
                    khdays::assets::inspect_mdl0(
                        std::filesystem::path{argv[2]});
                std::cout
                    << khdays::assets::format_model_report(information);
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--audio-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --audio-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat = khdays::assets::read_sdat_inventory(
                    std::filesystem::path{argv[2]});
                const auto show = [](const char* label,
                                     const std::vector<std::string>& items) {
                    std::cout << label << ": " << items.size() << '\n';
                    for (std::size_t i = 0; i < items.size() && i < 8; ++i) {
                        if (!items[i].empty()) {
                            std::cout << "    " << items[i] << '\n';
                        }
                    }
                    if (items.size() > 8) {
                        std::cout << "    ... (" << (items.size() - 8)
                                  << " more)\n";
                    }
                };
                std::cout << "SDAT inventory:\n";
                show("  Sequences", sdat.sequences);
                show("  Sequence archives", sdat.sequence_archives);
                show("  Banks", sdat.banks);
                show("  Wave archives", sdat.wave_archives);
                show("  Stream players", sdat.stream_players);
                show("  Streams", sdat.streams);
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--mods-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --mods-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto info = khdays::assets::parse_mods_header(
                    std::filesystem::path{argv[2]});
                std::cout << "MobiClip MODS container:\n"
                          << "  video: " << info.width << 'x' << info.height << ", "
                          << info.frame_count << " frames\n";
                if (info.has_audio()) {
                    std::cout << "  audio: " << info.audio_channels << " ch @ "
                              << info.audio_rate << " Hz (coding "
                              << info.audio_coding << ")\n";
                } else {
                    std::cout << "  audio: none (video-only clip)\n";
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--game-demo") {
            return run_game_demo();
        }

        if (first == "--menu-shot") {
            // Headless snapshot of the main-menu scene (for previewing the
            // layout without opening a window). Optional 2nd arg = how many
            // times to move the cursor right before capturing.
            if (argc < 3) {
                std::cerr << "ERROR: --menu-shot requires an output BMP "
                             "[cursor-steps]\n";
                return EXIT_FAILURE;
            }
            khdays::vfs::autodetect_data_root();
            const int steps = argc > 3 ? std::stoi(argv[3]) : 0;
            khdays::game::SceneManager manager;
            manager.register_scene(khdays::game::kSceneMainMenu, [] {
                return std::make_unique<khdays::game::scenes::MainMenuScene>();
            });
            manager.start(khdays::game::kSceneMainMenu);
            for (int i = 0; i < steps; ++i) {
                khdays::game::Input in;
                in.pressed = static_cast<std::uint16_t>(khdays::game::Button::Right);
                manager.set_input(in);
                manager.step();
            }
            manager.set_input(khdays::game::Input{});
            manager.step();
            khdays::game::SoftwareRenderer sw{512, 384};
            manager.render(sw);
            const auto bmp = khdays::assets::to_bmp(sw.snapshot());
            std::ofstream out{argv[2], std::ios::binary};
            out.write(reinterpret_cast<const char*>(bmp.data()),
                      static_cast<std::streamsize>(bmp.size()));
            std::cout << "Main-menu snapshot (cursor step " << steps
                      << ") -> BMP: " << argv[2] << '\n';
            return EXIT_SUCCESS;
        }

        if (first == "--title-shot") {
            // Headless snapshot of the title/main-menu scene (two DS screens
            // stacked). Optional 2nd arg = how many times to press Down first.
            if (argc < 3) {
                std::cerr << "ERROR: --title-shot requires an output BMP "
                             "[down-steps]\n";
                return EXIT_FAILURE;
            }
            khdays::vfs::autodetect_data_root();
            // Optional 2nd arg: a key sequence driving the menu before the
            // capture — 'd'/'u' move, 'a' confirms (enters a submenu), 'b' goes
            // back; a digit N means N x Down. e.g. "da" = MODO MISION -> its
            // submenu.
            const std::string keys = argc > 3 ? std::string{argv[3]} : std::string{};
            khdays::game::SceneManager manager;
            manager.register_scene(khdays::game::kSceneTitle, [] {
                return std::make_unique<khdays::game::scenes::TitleScene>();
            });
            manager.start(khdays::game::kSceneTitle);
            for (int i = 0; i < 30; ++i) {  // settle past the fade-in
                manager.set_input(khdays::game::Input{});
                manager.step();
            }
            for (const char key : keys) {
                auto button = khdays::game::Button::Down;
                int repeat = 1;
                switch (key) {
                    case 'd': button = khdays::game::Button::Down; break;
                    case 'u': button = khdays::game::Button::Up; break;
                    case 'a': button = khdays::game::Button::A; break;
                    case 'b': button = khdays::game::Button::B; break;
                    default:
                        if (key < '0' || key > '9') {
                            continue;
                        }
                        repeat = key - '0';
                        break;
                }
                for (int i = 0; i < repeat; ++i) {
                    khdays::game::Input in;
                    in.pressed = static_cast<std::uint16_t>(button);
                    manager.set_input(in);
                    manager.step();
                    manager.set_input(khdays::game::Input{});
                    manager.step();
                }
            }
            manager.set_input(khdays::game::Input{});
            manager.step();
            // Optional last arg "h" = side-by-side layout (wider canvas).
            const bool horizontal = argc > 4 && std::string{argv[4]} == "h";
            if (horizontal) {
                khdays::game::set_screen_layout(
                    khdays::game::ScreenLayout::Horizontal);
            }
            khdays::game::SoftwareRenderer sw{horizontal ? 1056 : 544,
                                              horizontal ? 400 : 816};
            manager.render(sw);
            const auto bmp = khdays::assets::to_bmp(sw.snapshot());
            std::ofstream out{argv[2], std::ios::binary};
            out.write(reinterpret_cast<const char*>(bmp.data()),
                      static_cast<std::streamsize>(bmp.size()));
            std::cout << "Title snapshot (keys \"" << keys << "\") -> BMP: "
                      << argv[2] << '\n';
            return EXIT_SUCCESS;
        }

        if (first == "--boot-shot") {
            // Headless snapshot of the boot logo sequence at a given frame.
            if (argc < 3) {
                std::cerr << "ERROR: --boot-shot requires an output BMP [frame]\n";
                return EXIT_FAILURE;
            }
            khdays::vfs::autodetect_data_root();
            const int frames = argc > 3 ? std::stoi(argv[3]) : 0;
            khdays::game::SceneManager manager;
            manager.register_scene(khdays::game::kSceneBootLogo, [] {
                return std::make_unique<khdays::game::scenes::BootLogoScene>();
            });
            manager.start(khdays::game::kSceneBootLogo);
            for (int i = 0; i < frames; ++i) {
                manager.set_input(khdays::game::Input{});
                manager.step();
            }
            khdays::game::SoftwareRenderer sw{544, 816};
            manager.render(sw);
            const auto bmp = khdays::assets::to_bmp(sw.snapshot());
            std::ofstream out{argv[2], std::ios::binary};
            out.write(reinterpret_cast<const char*>(bmp.data()),
                      static_cast<std::streamsize>(bmp.size()));
            std::cout << "Boot snapshot (frame " << frames << ") -> BMP: "
                      << argv[2] << '\n';
            return EXIT_SUCCESS;
        }

        if (first == "--game") {
            if (!khdays::vfs::autodetect_data_root()) {
                std::cerr << "note: no extracted data under data/extracted; "
                             "scenes will show without game assets\n";
            }
            khdays::game::Game game;
            game.scenes().register_scene(khdays::game::kSceneBootLogo, [] {
                return std::make_unique<khdays::game::scenes::BootLogoScene>();
            });
            game.scenes().register_scene(khdays::game::kSceneTitle, [] {
                return std::make_unique<khdays::game::scenes::TitleScene>();
            });
            game.scenes().register_scene(khdays::game::kSceneMainMenu, [] {
                return std::make_unique<khdays::game::scenes::MainMenuScene>();
            });
            game.scenes().register_scene(khdays::game::kSceneGameplay, [] {
                return std::make_unique<khdays::game::scenes::GameplayScene>();
            });
            game.boot(0);
            std::cout << "Running the game frame loop:\n"
                         "  Z/Enter confirm, X back, arrows move (remap in "
                         "Config > Controls)\n"
                         "  Menu bar: Config (volume/controls/layout), View; "
                         "F10 hide bar, F11 fullscreen, Esc quit\n";
            return khdays::platform::run_game(game);
        }

        if (first == "--vfs-resolve") {
            if (argc != 3) {
                std::cerr << "ERROR: --vfs-resolve requires a game path\n";
                return EXIT_FAILURE;
            }
            if (!khdays::vfs::autodetect_data_root()) {
                std::cerr << "ERROR: could not find extracted data under "
                             "data/extracted (run the extractor first)\n";
                return EXIT_FAILURE;
            }
            std::cout << "data root: " << khdays::vfs::data_root().string()
                      << '\n';
            const auto resolved = khdays::vfs::resolve(argv[2]);
            if (!resolved) {
                std::cerr << "not found: " << argv[2] << '\n';
                return EXIT_FAILURE;
            }
            std::error_code ec;
            const auto size = std::filesystem::file_size(*resolved, ec);
            std::cout << argv[2] << " -> " << resolved->string() << " ("
                      << size << " bytes)\n";
            return EXIT_SUCCESS;
        }

        if (first == "--render-tiles") {
            if (argc != 5 && argc != 6) {
                std::cerr << "ERROR: --render-tiles requires NCGR, NCLR, an "
                             "output BMP, and an optional palette index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto tiles =
                    khdays::assets::decode_ncgr(std::filesystem::path{argv[2]});
                const auto palette =
                    khdays::assets::decode_nclr(std::filesystem::path{argv[3]});
                const int palette_index = argc == 6 ? std::stoi(argv[5]) : 0;
                const auto image = khdays::assets::render_tile_sheet(
                    tiles, palette, palette_index);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Decoded " << tiles.tile_count << " tiles ("
                          << tiles.bpp << "bpp), " << palette.colors.size()
                          << " palette colors -> " << image.width << 'x'
                          << image.height << " BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-bg") {
            if (argc < 6) {
                std::cerr << "ERROR: --render-bg requires NSCR, NCLR, an output "
                             "BMP, and one or more NCGR\n";
                return EXIT_FAILURE;
            }
            try {
                std::vector<std::filesystem::path> ncgr_paths;
                for (int i = 5; i < argc; ++i) {
                    ncgr_paths.emplace_back(argv[i]);
                }
                const auto image = khdays::resource::load_background(
                    std::filesystem::path{argv[2]},
                    std::filesystem::path{argv[3]},
                    ncgr_paths);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Composed " << image.width << 'x' << image.height
                          << " background -> BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-cell") {
            if (argc != 6) {
                std::cerr << "ERROR: --render-cell requires a P2 file, sub-file "
                             "index, cell index, and an output BMP\n";
                return EXIT_FAILURE;
            }
            try {
                const auto pack = khdays::assets::extract_p2_subfile(
                    std::filesystem::path{argv[2]},
                    static_cast<std::size_t>(std::stoul(argv[3])));
                const auto pal = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RLCN");
                const auto chr = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RGCN");
                const auto cer = khdays::assets::find_nitro_resource(
                    pack.data(), pack.size(), "RECN");
                if (!pal || !chr || !cer) {
                    std::cerr << "ERROR: pack has no NCLR/NCGR/NCER sprites\n";
                    return EXIT_FAILURE;
                }
                const auto palette = khdays::assets::decode_nclr(pal.data, pal.size);
                const auto tiles = khdays::assets::decode_ncgr(chr.data, chr.size);
                const auto bank = khdays::assets::decode_ncer(cer.data, cer.size);
                const auto index = static_cast<std::size_t>(std::stoul(argv[4]));
                if (index >= bank.cells.size()) {
                    std::cerr << "ERROR: cell " << index << " out of range ("
                              << bank.cells.size() << ")\n";
                    return EXIT_FAILURE;
                }
                const auto image = khdays::assets::render_cell(
                    bank.cells[index], tiles, palette, bank.tile_boundary);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[5], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << bank.cells.size() << " cells (boundary "
                          << bank.tile_boundary << "), cell " << index << " -> "
                          << image.width << 'x' << image.height << " BMP: "
                          << argv[5] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-ui") {
            // Exploration aid: parse a D2KP UI pack (a P2 sub-file) and dump
            // every non-blank screen x tile-sheet composition, to identify which
            // tilemap/tiles/palette form each background layer.
            if (argc < 5 || argc > 7) {
                std::cerr << "ERROR: --dump-ui requires a P2 file, sub-file "
                             "index, and an output directory, plus optional "
                             "caps on the compositions written and examined\n";
                return EXIT_FAILURE;
            }
            try {
                // Both caps are enforced here rather than by a caller watching
                // the output directory, because a caller can only stop this by
                // killing it, and then the output depends on where the process
                // happened to be - i.e. on machine load. Stopping at a fixed
                // count of the deterministic s/t/p walk below is reproducible.
                //
                // Two caps, because they bound different things: this pack has
                // no bound of its own (one sub-file writes 1.25 million
                // compositions), but only *non-blank* ones reach the disk, so a
                // pack can also burn minutes on combinations that never write a
                // file. max_written bounds the output; max_examined bounds the
                // work, and is the one that binds on a blank-heavy pack.
                const auto arg_cap = [&](const int i) {
                    return i < argc
                               ? static_cast<std::size_t>(std::stoul(argv[i]))
                               : std::numeric_limits<std::size_t>::max();
                };
                const std::size_t max_written = arg_cap(5);
                const std::size_t max_examined = arg_cap(6);
                const auto blob = khdays::assets::extract_p2_subfile(
                    std::filesystem::path{argv[2]},
                    static_cast<std::size_t>(std::stoul(argv[3])));
                const auto pack =
                    khdays::assets::parse_pk2d(blob.data(), blob.size());
                std::cout << "D2KP: " << pack.palettes.size() << " NCLR, "
                          << pack.tiles.size() << " NCGR, " << pack.screens.size()
                          << " NSCR, " << pack.cells.size() << " NCER, "
                          << pack.anims.size() << " NANR\n";
                const std::filesystem::path out_dir{argv[4]};
                std::filesystem::create_directories(out_dir);
                std::size_t written = 0;
                std::size_t examined = 0;
                const auto capped = [&] {
                    return written >= max_written || examined >= max_examined;
                };
                for (std::size_t s = 0; s < pack.screens.size() && !capped();
                     ++s) {
                    const auto map = khdays::assets::decode_nscr(
                        pack.screens[s].data, pack.screens[s].size);
                    for (std::size_t t = 0; t < pack.tiles.size() && !capped();
                         ++t) {
                        const auto tiles = khdays::assets::decode_ncgr(
                            pack.tiles[t].data, pack.tiles[t].size);
                        for (std::size_t p = 0;
                             p < pack.palettes.size() && !capped(); ++p) {
                            ++examined;
                            const auto palette = khdays::assets::decode_nclr(
                                pack.palettes[p].data, pack.palettes[p].size);
                            const auto image = khdays::assets::compose_background(
                                map, tiles, palette, true);
                            std::size_t opaque = 0;
                            for (std::size_t k = 3; k < image.rgba.size(); k += 4) {
                                if (image.rgba[k] != 0) {
                                    ++opaque;
                                }
                            }
                            const double frac = image.rgba.empty() ? 0.0
                                : static_cast<double>(opaque)
                                    / (image.rgba.size() / 4.0);
                            if (frac < 0.02) {
                                continue;  // essentially blank pairing
                            }
                            const auto bmp = khdays::assets::to_bmp(image);
                            const auto path = out_dir
                                / ("s" + std::to_string(s) + "_t"
                                   + std::to_string(t) + "_p" + std::to_string(p)
                                   + ".bmp");
                            std::ofstream f{path, std::ios::binary};
                            f.write(reinterpret_cast<const char*>(bmp.data()),
                                    static_cast<std::streamsize>(bmp.size()));
                            std::cout << "  s" << s << " t" << t << " p" << p
                                      << "  " << image.width << 'x' << image.height
                                      << "  opaque=" << static_cast<int>(frac * 100)
                                      << "%\n";
                            ++written;
                        }
                    }
                }
                if (capped()) {
                    std::cout << "capped: wrote " << written << ", examined "
                              << examined << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-text") {
            if (argc != 5) {
                std::cerr << "ERROR: --render-text requires NFTR, text, and an "
                             "output BMP\n";
                return EXIT_FAILURE;
            }
            try {
                const auto font =
                    khdays::resource::load_font(std::filesystem::path{argv[2]});
                const auto text = khdays::assets::message_from_utf8(argv[3]);
                const auto image = khdays::assets::render_text(font, text);
                const auto bmp = khdays::assets::to_bmp(image);
                std::ofstream out{argv[4], std::ios::binary};
                out.write(
                    reinterpret_cast<const char*>(bmp.data()),
                    static_cast<std::streamsize>(bmp.size()));
                std::cout << "Font: " << font.glyphs.size() << " glyphs, "
                          << font.char_to_glyph.size() << " mapped, "
                          << font.cell_width << 'x' << font.cell_height << ' '
                          << font.bpp << "bpp -> " << image.width << 'x'
                          << image.height << " BMP: " << argv[4] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--extract-wav") {
            if (argc != 6) {
                std::cerr << "ERROR: --extract-wav requires SDAT, wave-archive "
                             "index, SWAV index, and an output path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto wave_archive =
                    static_cast<std::size_t>(std::stoul(argv[3]));
                const auto swav =
                    static_cast<std::size_t>(std::stoul(argv[4]));
                const auto audio =
                    khdays::assets::sdat_waveform(*sdat, wave_archive, swav);
                const auto wav = khdays::assets::to_wav(audio);
                std::ofstream out{argv[5], std::ios::binary};
                if (!out) {
                    std::cerr << "ERROR: cannot write " << argv[5] << '\n';
                    return EXIT_FAILURE;
                }
                out.write(
                    reinterpret_cast<const char*>(wav.data()),
                    static_cast<std::streamsize>(wav.size()));
                std::cout << "Decoded SWAV " << wave_archive << ':' << swav
                          << " -> " << audio.samples.size() << " samples @ "
                          << audio.sample_rate << " Hz ("
                          << (audio.sample_rate > 0
                                  ? static_cast<double>(audio.samples.size())
                                      / audio.sample_rate
                                  : 0.0)
                          << " s), wrote " << argv[5] << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-textures") {
            // Headless: export every TEX0 texture in an NSBMD/NSBTX to BMP.
            if (argc != 4) {
                std::cerr << "ERROR: --dump-textures requires a model file and "
                             "an output directory\n";
                return EXIT_FAILURE;
            }
            try {
                const std::filesystem::path model{argv[2]};
                const std::filesystem::path out_dir{argv[3]};
                std::filesystem::create_directories(out_dir);
                const auto names = khdays::assets::list_tex0_textures(model);
                std::cout << names.size() << " textures\n";
                for (const auto& name : names) {
                    const auto tex = khdays::assets::load_tex0_texture(model, name);
                    const auto bmp = khdays::assets::to_bmp(tex);
                    const auto path = out_dir / (name + ".bmp");
                    std::ofstream f{path, std::ios::binary};
                    f.write(reinterpret_cast<const char*>(bmp.data()),
                            static_cast<std::streamsize>(bmp.size()));
                    std::cout << "  " << name << ' ' << tex.width << 'x'
                              << tex.height << ' ' << tex.format_name << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--extract-stream") {
            if (argc != 5) {
                std::cerr << "ERROR: --extract-stream requires SDAT, stream "
                             "index, and an output WAV\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto index = static_cast<std::size_t>(std::stoul(argv[3]));
                const auto audio = khdays::assets::decode_stream(*sdat, index);
                const auto wav = khdays::assets::to_wav(audio);
                std::ofstream out{argv[4], std::ios::binary};
                if (!out) {
                    std::cerr << "ERROR: cannot write " << argv[4] << '\n';
                    return EXIT_FAILURE;
                }
                out.write(reinterpret_cast<const char*>(wav.data()),
                          static_cast<std::streamsize>(wav.size()));
                std::cout << "Decoded stream " << index << " -> "
                          << audio.samples.size() / (audio.channels ? audio.channels : 1)
                          << " frames, " << audio.channels << " ch @ "
                          << audio.sample_rate << " Hz, loop="
                          << (audio.loops ? "yes" : "no") << " wrote " << argv[4]
                          << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--play-sound") {
            if (argc != 5) {
                std::cerr << "ERROR: --play-sound requires SDAT, wave-archive "
                             "index, and SWAV index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto audio = khdays::resource::load_sound(
                    *sdat,
                    static_cast<std::size_t>(std::stoul(argv[3])),
                    static_cast<std::size_t>(std::stoul(argv[4])));
                std::cout << "Playing SWAV " << argv[3] << ':' << argv[4]
                          << " (" << audio.samples.size() << " samples @ "
                          << audio.sample_rate << " Hz)" << std::endl;
                return khdays::platform::play_audio_blocking(audio);
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--render-sequence" || first == "--play-sequence") {
            const bool render = first == "--render-sequence";
            const int min_args = render ? 5 : 4;
            if (argc < min_args) {
                std::cerr << "ERROR: " << first << " requires an SDAT and a "
                             "sequence index"
                          << (render ? ", and an output path\n" : "\n");
                return EXIT_FAILURE;
            }
            try {
                const auto sdat =
                    khdays::assets::open_sdat(std::filesystem::path{argv[2]});
                const auto seq =
                    static_cast<std::size_t>(std::stoul(argv[3]));
                const int seconds_arg = render ? 5 : 4;
                const double seconds = argc > seconds_arg
                    ? std::stod(argv[seconds_arg])
                    : (render ? 180.0 : 30.0);
                const auto audio = khdays::resource::render_music(
                    *sdat, seq, 32768U, seconds);
                std::cout << "Synthesized sequence " << seq << ": "
                          << audio.samples.size() / 2U << " frames @ "
                          << audio.sample_rate << " Hz ("
                          << (audio.sample_rate > 0
                                  ? static_cast<double>(audio.samples.size() / 2U)
                                      / audio.sample_rate
                                  : 0.0)
                          << " s)" << std::endl;
                if (render) {
                    const auto wav = khdays::assets::to_wav(audio);
                    std::ofstream out{argv[4], std::ios::binary};
                    if (!out) {
                        std::cerr << "ERROR: cannot write " << argv[4] << '\n';
                        return EXIT_FAILURE;
                    }
                    out.write(
                        reinterpret_cast<const char*>(wav.data()),
                        static_cast<std::streamsize>(wav.size()));
                    std::cout << "Wrote " << argv[4] << '\n';
                    return EXIT_SUCCESS;
                }
                return khdays::platform::play_audio_blocking(audio);
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--message-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --message-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto archive = khdays::resource::load_message_archive(
                    std::filesystem::path{argv[2]});
                std::cout << "P2 message container: "
                          << archive.subdbs.size() << " sub-databases, "
                          << archive.string_count() << " strings\n";
                for (std::size_t d = 0; d < archive.subdbs.size(); ++d) {
                    std::cout << "  sub-db " << d << ": "
                              << archive.subdbs[d].size() << " strings";
                    if (!archive.subdbs[d].empty()) {
                        std::cout << "  e.g. \""
                                  << khdays::assets::message_to_utf8(
                                         archive.subdbs[d].front())
                                  << "\"";
                    }
                    std::cout << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-messages") {
            if (argc != 3 && argc != 4) {
                std::cerr << "ERROR: --dump-messages requires a file path and "
                             "an optional sub-db index\n";
                return EXIT_FAILURE;
            }
            try {
                const auto archive = khdays::resource::load_message_archive(
                    std::filesystem::path{argv[2]});
                const bool one = argc == 4;
                const auto only = one
                    ? static_cast<std::size_t>(std::stoul(argv[3]))
                    : 0U;
                if (one && only >= archive.subdbs.size()) {
                    std::cerr << "ERROR: sub-db " << only << " out of range ("
                              << archive.subdbs.size() << ")\n";
                    return EXIT_FAILURE;
                }
                for (std::size_t d = 0; d < archive.subdbs.size(); ++d) {
                    if (one && d != only) {
                        continue;
                    }
                    for (std::size_t s = 0; s < archive.subdbs[d].size(); ++s) {
                        std::cout << '[' << d << ':' << s << "] "
                                  << khdays::assets::message_to_utf8(
                                         archive.subdbs[d][s])
                                  << '\n';
                    }
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--dump-strings") {
            if (argc != 3) {
                std::cerr << "ERROR: --dump-strings requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto strings = khdays::resource::load_string_table(
                    std::filesystem::path{argv[2]});
                std::cout << strings.size() << " strings\n";
                for (std::size_t i = 0; i < strings.size(); ++i) {
                    std::cout << '[' << i << "] "
                              << khdays::assets::message_to_utf8(strings[i])
                              << '\n';
                }
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--anim-info") {
            if (argc != 3) {
                std::cerr << "ERROR: --anim-info requires one file path\n";
                return EXIT_FAILURE;
            }
            try {
                const auto animation =
                    khdays::assets::load_nsbca(std::filesystem::path{argv[2]});
                std::size_t animated = 0;
                std::size_t rot_samples = 0;
                std::size_t rot_const = 0;
                std::size_t trans_samples = 0;
                std::size_t total_rot_keys = 0;
                for (const auto& bone : animation.bones) {
                    if (bone.animated) {
                        ++animated;
                    }
                    using RK = khdays::assets::AnimationRotationCurve::Kind;
                    if (bone.rotation.kind == RK::Samples) {
                        ++rot_samples;
                        total_rot_keys += bone.rotation.samples.size();
                    } else if (bone.rotation.kind == RK::Constant) {
                        ++rot_const;
                    }
                    using CK = khdays::assets::AnimationCurve::Kind;
                    for (const auto& t : bone.translation) {
                        if (t.kind == CK::Samples) {
                            ++trans_samples;
                        }
                    }
                }
                std::cout
                    << "Animation: " << animation.frame_count << " frames, "
                    << animation.bones.size() << " bones ("
                    << animated << " animated)\n"
                    << "  rotation curves: " << rot_samples << " sampled ("
                    << total_rot_keys << " keys total), " << rot_const
                    << " constant\n"
                    << "  translation sampled curves: " << trans_samples << "\n";
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--export-obj") {
            if (argc != 3 && argc != 4) {
                std::cerr
                    << "ERROR: --export-obj requires an input file and an "
                       "optional output path\n";
                return EXIT_FAILURE;
            }

            const std::filesystem::path input{argv[2]};
            const std::filesystem::path output =
                argc == 4
                    ? std::filesystem::path{argv[3]}
                    : std::filesystem::path{input}.replace_extension(".obj");

            try {
                const auto model =
                    khdays::assets::decode_model_geometry(input);
                const auto obj = khdays::assets::to_wavefront_obj(model);

                std::ofstream stream{output, std::ios::binary};
                if (!stream) {
                    std::cerr
                        << "ERROR: cannot write OBJ to " << output.string()
                        << '\n';
                    return EXIT_FAILURE;
                }
                stream.write(
                    obj.data(),
                    static_cast<std::streamsize>(obj.size()));

                std::size_t total_vertices = 0U;
                std::size_t total_triangles = 0U;
                for (const auto& mesh : model.meshes) {
                    total_vertices += mesh.vertices.size();
                    total_triangles += mesh.indices.size() / 3U;
                }

                std::cout
                    << "Decoded model '" << model.name << "' with "
                    << model.meshes.size() << " meshes, "
                    << total_vertices << " vertices ("
                    << model.header_vertex_count << " expected), "
                    << total_triangles << " triangles\n"
                    << "Wrote " << output.string() << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }

        if (first == "--export-skin") {
            if (argc < 4) {
                std::cerr << "ERROR: --export-skin requires a model file, an "
                             "output path, and zero or more NSBCA files\n";
                return EXIT_FAILURE;
            }

            const std::filesystem::path input{argv[2]};
            const std::filesystem::path output{argv[3]};

            try {
                const auto model =
                    khdays::assets::decode_model_geometry(input);
                if (!model.skinning || model.object_matrices.empty()) {
                    std::cerr << "ERROR: model '" << model.name
                              << "' has no skeleton to animate\n";
                    return EXIT_FAILURE;
                }

                std::size_t total_vertices = 0U;
                for (const auto& mesh : model.meshes) {
                    total_vertices += mesh.vertices.size();
                }
                std::cout
                    << "Model '" << model.name << "': " << total_vertices
                    << " vertices, " << model.object_matrices.size()
                    << " bones, " << model.palette.size()
                    << " palette matrices\n";

                // Candidates are filtered here rather than in the serializer:
                // rejecting one is a decision worth reporting, not a silent
                // omission, so every candidate gets a line either way.
                std::vector<SkinAnimationInput> animations;
                for (int index = 4; index < argc; ++index) {
                    const std::filesystem::path path{argv[index]};
                    // Name an animation after the container that holds it
                    // (ba/ch/ax/li.p/slot_0/0000.nsbca -> "li"): the NSBCA's
                    // own name is the artist's and every file is called 0000,
                    // so neither identifies it in a menu.
                    std::string name =
                        path.parent_path().parent_path().filename().string();
                    if (name.size() > 2U
                        && name.substr(name.size() - 2U) == ".p") {
                        name.erase(name.size() - 2U);
                    }
                    if (name.empty()) {
                        name = path.stem().string();
                    }

                    auto animation = khdays::assets::load_nsbca(path);
                    const bool sibling = same_container(input, path);
                    const auto rejection =
                        skin_animation_rejection(model, animation, sibling);
                    if (!rejection.empty()) {
                        std::cout << "  SKIP '" << name << "': " << rejection
                                  << '\n';
                        continue;
                    }
                    std::cout << "  animation '" << name << "': "
                              << animation.frame_count << " frames, "
                              << animation.bones.size() << " bones";
                    if (animation.bones.size() < model.object_matrices.size()) {
                        std::cout << " (partial: the model has "
                                  << model.object_matrices.size()
                                  << ", the rest stay at rest)";
                    }
                    std::cout << '\n';
                    animations.push_back({name, std::move(animation)});
                }

                const auto json = to_skin_json(model, animations);

                std::ofstream stream{output, std::ios::binary};
                if (!stream) {
                    std::cerr << "ERROR: cannot write skin JSON to "
                              << output.string() << '\n';
                    return EXIT_FAILURE;
                }
                stream.write(
                    json.data(), static_cast<std::streamsize>(json.size()));

                std::cout << "Wrote " << output.string() << " ("
                          << json.size() / 1024U << " KB, "
                          << animations.size() << " animations)\n";
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }
    }

    try {
        const auto options = parse_runtime_options(argc, argv);
        print_version();
        std::cout << "Starting the native SDL3 platform runtime..." << '\n';
        return khdays::platform::run_application(options);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        std::cerr << "Run khdays-port --help for usage." << '\n';
        return EXIT_FAILURE;
    }
}
