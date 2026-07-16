#include "overlay_ui.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <string>

#include "khdays/game/settings.h"

using khdays::game::Language;

#ifdef KHDAYS_HAS_UI
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#endif

namespace khdays::platform {

namespace {
// The interface follows the same language the game does — one setting, not two.
// Order matches khdays::game::Language: de, en, es, fr, it.
enum class Text {
    Config, View, Volume, Controls, Language, ScreensStacked, ScreensSideBySide,
    HideBar, Fullscreen, RebindHelp, PressKey, Unset, BarHint, FullscreenHint,
    RestartNeeded,
    Count
};

const char* const kText[static_cast<int>(Text::Count)][5] = {
    /* Config      */ {"Konfiguration", "Config", "Configuración", "Configuration", "Configurazione"},
    /* View        */ {"Ansicht", "View", "Ver", "Affichage", "Vista"},
    /* Volume      */ {"Lautstärke", "Volume", "Volumen", "Volume", "Volume"},
    /* Controls    */ {"Steuerung...", "Controls...", "Controles...", "Commandes...", "Comandi..."},
    /* Language    */ {"Sprache", "Language", "Idioma", "Langue", "Lingua"},
    /* Stacked     */ {"Bildschirme: übereinander", "Screens: stacked", "Pantallas: apiladas",
                       "Écrans : empilés", "Schermi: impilati"},
    /* SideBySide  */ {"Bildschirme: nebeneinander", "Screens: side by side", "Pantallas: lado a lado",
                       "Écrans : côte à côte", "Schermi: affiancati"},
    /* HideBar     */ {"Menüleiste ausblenden", "Hide menu bar", "Ocultar barra de menú",
                       "Masquer la barre de menu", "Nascondi barra dei menu"},
    /* Fullscreen  */ {"Vollbild", "Fullscreen", "Pantalla completa", "Plein écran", "Schermo intero"},
    /* RebindHelp  */ {"Taste anklicken, dann drücken. Esc bricht ab.",
                       "Click a button, then press a key. Esc cancels.",
                       "Pulsa un botón y luego una tecla. Esc cancela.",
                       "Cliquez sur un bouton, puis appuyez sur une touche. Esc annule.",
                       "Clicca un pulsante, poi premi un tasto. Esc annulla."},
    /* PressKey    */ {"[Taste drücken]", "[press a key]", "[pulsa una tecla]",
                       "[appuyez sur une touche]", "[premi un tasto]"},
    /* Unset       */ {"(nicht belegt)", "(unset)", "(sin asignar)", "(non assigné)", "(non assegnato)"},
    /* BarHint     */ {"Maus an den oberen Rand für das Menü (F10)",
                       "Move the mouse to the top edge for the menu (F10)",
                       "Mueve el ratón al borde superior para el menú (F10)",
                       "Amenez la souris en haut pour le menu (F10)",
                       "Sposta il mouse in alto per il menu (F10)"},
    /* FullHint    */ {"F11 zum Beenden des Vollbilds", "F11 to exit fullscreen",
                       "F11 para salir de pantalla completa", "F11 pour quitter le plein écran",
                       "F11 per uscire dallo schermo intero"},
    /* Restart     */ {"Neustart erforderlich, damit die Sprache wirkt",
                       "Restart the game for the language change to take effect",
                       "Reinicia el juego para aplicar el cambio de idioma",
                       "Redémarrez le jeu pour appliquer le changement de langue",
                       "Riavvia il gioco per applicare il cambio di lingua"},
};

const char* tr(Text key) {
    return kText[static_cast<int>(key)][static_cast<int>(khdays::game::language())];
}
}  // namespace


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

// The OS's preferred language, if the game ships it; English otherwise. SDL
// hands back the user's locale list in priority order, so the first match wins.
khdays::game::Language detect_os_language() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    auto picked = khdays::game::Language::English;
    if (locales != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (locales[i] == nullptr || locales[i]->language == nullptr) {
                continue;
            }
            const std::string_view code{locales[i]->language};
            // language_from_code falls back to English, so ask whether the code
            // really is one we ship instead of trusting the fallback.
            const auto candidate = khdays::game::language_from_code(code);
            if (khdays::game::language_code(candidate) == code) {
                picked = candidate;
                break;
            }
        }
        SDL_free(locales);
    }
    return picked;
}

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
    // No saved preference yet -> follow the OS.
    khdays::game::set_language(detect_os_language());
    std::ifstream file(config_path());
    if (!file) {
        language_at_start_ = khdays::game::language();
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
            } else if (key == "language") {
                khdays::game::set_language(khdays::game::language_from_code(value));
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
    language_at_start_ = khdays::game::language();
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
        hint_ = fullscreen_ ? tr(Text::FullscreenHint) : "";
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
                hint_ = tr(Text::BarHint);
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
        if (ImGui::BeginMenu(tr(Text::Config))) {
            int vol = static_cast<int>(volume_ * 100.0F + 0.5F);
            if (ImGui::SliderInt(tr(Text::Volume), &vol, 0, 100, "%d%%")) {
                volume_ = static_cast<float>(vol) / 100.0F;
            }
            if (ImGui::MenuItem(tr(Text::Controls))) {
                show_controls_ = true;
            }
            // One language for everything: it picks the game's localized assets
            // and this interface at the same time.
            if (ImGui::BeginMenu(tr(Text::Language))) {
                for (const auto lang : {Language::German, Language::English,
                                        Language::Spanish, Language::French,
                                        Language::Italian}) {
                    const bool on = khdays::game::language() == lang;
                    const auto name = khdays::game::language_name(lang);
                    if (ImGui::MenuItem(std::string{name}.c_str(), nullptr, on)) {
                        khdays::game::set_language(lang);
                        // Scenes load their localized assets on entry, so this
                        // does not redress what is already on screen.
                        hint_ = tr(Text::RestartNeeded);
                        hint_timer_ = 5.0F;
                    }
                }
                ImGui::EndMenu();
            }
            if (khdays::game::language() != language_at_start_) {
                ImGui::TextDisabled("%s", tr(Text::RestartNeeded));
            }
            ImGui::Separator();
            const bool stacked =
                khdays::game::screen_layout() == khdays::game::ScreenLayout::Vertical;
            if (ImGui::MenuItem(tr(Text::ScreensStacked), nullptr, stacked)) {
                khdays::game::set_screen_layout(khdays::game::ScreenLayout::Vertical);
            }
            if (ImGui::MenuItem(tr(Text::ScreensSideBySide), nullptr, !stacked)) {
                khdays::game::set_screen_layout(
                    khdays::game::ScreenLayout::Horizontal);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr(Text::View))) {
            if (ImGui::MenuItem(tr(Text::HideBar), "F10")) {
                show_menu_bar_ = false;
                hint_ = tr(Text::BarHint);
                hint_timer_ = 3.0F;
            }
            ImGui::MenuItem(tr(Text::Fullscreen), "F11", &fullscreen_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Controls (key remapping) window — melonDS-style: click a button, press a key.
    if (show_controls_) {
        ImGui::Begin(tr(Text::Controls), &show_controls_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(tr(Text::RebindHelp));
        ImGui::Separator();
        for (std::size_t i = 0; i < kBindRows.size(); ++i) {
            ImGui::Text("%-7s", kBindRows[i].label);
            ImGui::SameLine(90.0F);
            const SDL_Scancode sc = bindings_.*(kBindRows[i].field);
            const char* name = SDL_GetScancodeName(sc);
            char buf[64];
            if (remapping_ == static_cast<int>(i)) {
                std::snprintf(buf, sizeof buf, "%s##%zu", tr(Text::PressKey), i);
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
