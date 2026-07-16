#include "overlay_ui.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <string>

#include "khdays/game/settings.h"

#ifdef KHDAYS_HAS_UI
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#endif

namespace khdays::platform {

std::uint16_t poll_buttons(const KeyBindings& b) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    using Btn = khdays::game::Button;
    std::uint16_t down = 0;
    const auto set = [&](SDL_Scancode code, Btn button) {
        if (keys[code]) {
            down |= static_cast<std::uint16_t>(button);
        }
    };
    set(b.up, Btn::Up);
    set(b.down, Btn::Down);
    set(b.left, Btn::Left);
    set(b.right, Btn::Right);
    set(b.a, Btn::A);
    set(b.b, Btn::B);
    set(b.x, Btn::X);
    set(b.y, Btn::Y);
    set(b.l, Btn::L);
    set(b.r, Btn::R);
    set(b.start, Btn::Start);
    set(b.select, Btn::Select);
    return down;
}

namespace {
// The bindable buttons, in the Controls window order.
struct BindRow {
    const char* label;
    SDL_Scancode KeyBindings::* field;
};
const std::array<BindRow, 12> kBindRows = {{
    {"Up", &KeyBindings::up},
    {"Down", &KeyBindings::down},
    {"Left", &KeyBindings::left},
    {"Right", &KeyBindings::right},
    {"A", &KeyBindings::a},
    {"B", &KeyBindings::b},
    {"X", &KeyBindings::x},
    {"Y", &KeyBindings::y},
    {"L", &KeyBindings::l},
    {"R", &KeyBindings::r},
    {"Start", &KeyBindings::start},
    {"Select", &KeyBindings::select},
}};

std::string config_path() {
    char* pref = SDL_GetPrefPath("khdays", "port");
    std::string path =
        pref != nullptr ? std::string(pref) + "settings.ini" : "khdays-settings.ini";
    if (pref != nullptr) {
        SDL_free(pref);
    }
    return path;
}
}  // namespace

OverlayUi::OverlayUi(SDL_Window* window, SDL_Renderer* renderer)
    : window_(window), renderer_(renderer) {
#ifdef KHDAYS_HAS_UI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini side file
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    ui_ = true;
#else
    (void)window_;
    (void)renderer_;
#endif
    load_config();
}

void OverlayUi::load_config() {
    std::ifstream file(config_path());
    if (!file) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "volume") {
                const float v = std::stof(value);
                volume_ = v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v);
            } else if (key == "layout") {
                khdays::game::set_screen_layout(
                    value == "sidebyside" ? khdays::game::ScreenLayout::Horizontal
                                          : khdays::game::ScreenLayout::Vertical);
            } else if (key.rfind("key.", 0) == 0) {
                const std::string button = key.substr(4);
                for (const auto& row : kBindRows) {
                    if (button == row.label) {
                        bindings_.*(row.field) =
                            static_cast<SDL_Scancode>(std::stoi(value));
                    }
                }
            }
        } catch (...) {
            // ignore malformed lines
        }
    }
}

void OverlayUi::save_config() const {
    std::ofstream file(config_path());
    if (!file) {
        return;
    }
    file << "volume=" << volume_ << '\n';
    file << "layout="
         << (khdays::game::screen_layout() == khdays::game::ScreenLayout::Horizontal
                 ? "sidebyside"
                 : "stacked")
         << '\n';
    for (const auto& row : kBindRows) {
        file << "key." << row.label << '='
             << static_cast<int>(bindings_.*(row.field)) << '\n';
    }
}

OverlayUi::~OverlayUi() {
#ifdef KHDAYS_HAS_UI
    if (ui_) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
#endif
}

void OverlayUi::apply_fullscreen() {
    if (fullscreen_ != applied_fullscreen_) {
        SDL_SetWindowFullscreen(window_, fullscreen_);
        applied_fullscreen_ = fullscreen_;
        hint_ = fullscreen_ ? "F11 to exit fullscreen" : "";
        hint_timer_ = fullscreen_ ? 2.5F : 0.0F;
    }
}

void OverlayUi::process_event(const SDL_Event& event) {
#ifdef KHDAYS_HAS_UI
    if (!ui_) {
        return;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_KEY_DOWN) {
        // Capture the next key for a pending rebind (Escape cancels).
        if (remapping_ >= 0) {
            if (event.key.scancode != SDL_SCANCODE_ESCAPE) {
                bindings_.*(kBindRows[static_cast<std::size_t>(remapping_)].field) =
                    event.key.scancode;
            }
            remapping_ = -1;
            return;
        }
        // App hotkeys.
        if (event.key.key == SDLK_F10) {
            show_menu_bar_ = !show_menu_bar_;
            if (!show_menu_bar_) {
                hint_ = "Move the mouse to the top edge for the menu (F10)";
                hint_timer_ = 3.0F;
            }
        } else if (event.key.key == SDLK_F11) {
            fullscreen_ = !fullscreen_;
        }
    }
#else
    (void)event;
#endif
}

bool OverlayUi::wants_keyboard() const {
#ifdef KHDAYS_HAS_UI
    return ui_ && (ImGui::GetIO().WantCaptureKeyboard || remapping_ >= 0);
#else
    return false;
#endif
}

void OverlayUi::render() {
#ifdef KHDAYS_HAS_UI
    if (!ui_) {
        return;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();

    // Show the bar when enabled, or transiently while the mouse is at the top.
    const bool show_bar = show_menu_bar_ || io.MousePos.y < 6.0F;
    if (show_bar && ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Config")) {
            int vol = static_cast<int>(volume_ * 100.0F + 0.5F);
            if (ImGui::SliderInt("Volume", &vol, 0, 100, "%d%%")) {
                volume_ = static_cast<float>(vol) / 100.0F;
            }
            if (ImGui::MenuItem("Controls...")) {
                show_controls_ = true;
            }
            ImGui::Separator();
            const bool stacked =
                khdays::game::screen_layout() == khdays::game::ScreenLayout::Vertical;
            if (ImGui::MenuItem("Screens: stacked", nullptr, stacked)) {
                khdays::game::set_screen_layout(khdays::game::ScreenLayout::Vertical);
            }
            if (ImGui::MenuItem("Screens: side by side", nullptr, !stacked)) {
                khdays::game::set_screen_layout(
                    khdays::game::ScreenLayout::Horizontal);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Hide menu bar", "F10")) {
                show_menu_bar_ = false;
                hint_ = "Move the mouse to the top edge for the menu (F10)";
                hint_timer_ = 3.0F;
            }
            ImGui::MenuItem("Fullscreen", "F11", &fullscreen_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Controls (key remapping) window — melonDS-style: click a button, press a key.
    if (show_controls_) {
        ImGui::Begin("Controls", &show_controls_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("Click a button, then press a key. Esc cancels.");
        ImGui::Separator();
        for (std::size_t i = 0; i < kBindRows.size(); ++i) {
            ImGui::Text("%-7s", kBindRows[i].label);
            ImGui::SameLine(90.0F);
            const SDL_Scancode sc = bindings_.*(kBindRows[i].field);
            const char* name = SDL_GetScancodeName(sc);
            char buf[64];
            if (remapping_ == static_cast<int>(i)) {
                std::snprintf(buf, sizeof buf, "[press a key]##%zu", i);
            } else {
                std::snprintf(buf, sizeof buf, "%s##%zu",
                              (name != nullptr && *name != '\0') ? name : "(unset)", i);
            }
            if (ImGui::Button(buf, ImVec2(140.0F, 0.0F))) {
                remapping_ = static_cast<int>(i);
            }
        }
        ImGui::End();
    }

    // Transient hint (hidden bar / fullscreen) fading out.
    if (hint_timer_ > 0.0F) {
        hint_timer_ -= io.DeltaTime;
        const float alpha = hint_timer_ > 1.0F ? 1.0F : hint_timer_;
        ImGui::SetNextWindowPos(ImVec2(8.0F, io.DisplaySize.y - 32.0F));
        ImGui::SetNextWindowBgAlpha(0.5F * (alpha < 0.0F ? 0.0F : alpha));
        ImGui::Begin("##hint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
                         | ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextUnformatted(hint_.c_str());
        ImGui::End();
    }

    apply_fullscreen();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
#endif
}

}  // namespace khdays::platform
