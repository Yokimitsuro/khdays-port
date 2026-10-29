// khdays-studio: the window, its frame loop, and the command line
// when it is given one.
#include "app.h"
#include "cli.h"
#include "views/console.h"
#include "theme.h"

#include "core/files/paths.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>

namespace studio {
namespace {

void apply_language(App& app) {
    app.language =
        app.settings.language.empty() ? system_language() : language_from_code(app.settings.language, Language::en);
}

void take_dialog_files(App& app) {
    std::vector<std::string> files, clips, exports, grids, rooms, models, sheet_folders, texture_folders,
        texture_exports;
    {
        std::lock_guard lock(app.dialog_mutex);
        files.swap(app.dialog_files);
        clips.swap(app.dialog_clips);
        exports.swap(app.dialog_exports);
        grids.swap(app.dialog_grids);
        rooms.swap(app.dialog_room_exports);
        models.swap(app.dialog_model_exports);
        sheet_folders.swap(app.dialog_sheet_folders);
        texture_folders.swap(app.dialog_texture_folders);
        texture_exports.swap(app.dialog_texture_exports);
    }
    if (!sheet_folders.empty())
        sheets_export_to(app, path_u8(sheet_folders.front()));
    if (!texture_folders.empty())
        textures_export_to(app, path_u8(texture_folders.front()));
    if (!texture_exports.empty())
        texture_export_to(app, path_u8(texture_exports.front()));
    if (!rooms.empty())
        room_export_to(app, path_u8(rooms.front()));
    if (!models.empty())
        model_export_to(app, path_u8(models.front()));
    if (!files.empty())
        app.offer(path_u8(files.front()));
    if (!clips.empty())
        clip_offer(app, path_u8(clips.front()));
    if (!exports.empty())
        sheet_export_to(app, path_u8(exports.front()));
    if (!grids.empty())
        sheet_grid_export_to(app, path_u8(grids.front()));
}

void draw_notices(App& app);  // below

// The windows over the studio's page, open or not.
struct Windows {
    bool settings = false, themes = false, console = false, plugin = false, characters = false, missions = false;
};

void draw_frame(App& app, Windows& open) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme_page());  // the page behind the panels
    ImGui::Begin("##studio", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    draw_mod_bar(app);
    // the themes and the settings at the right end of the same row
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttons =
        ImGui::CalcTextSize(tr(app.language, S::mw_button)).x +
        ImGui::CalcTextSize(tr(app.language, S::chars_button)).x +
        ImGui::CalcTextSize(tr(app.language, S::plugin_button)).x +
        ImGui::CalcTextSize(tr(app.language, S::console)).x + ImGui::CalcTextSize(tr(app.language, S::themes)).x +
        ImGui::CalcTextSize(tr(app.language, S::settings)).x + style.FramePadding.x * 12 + style.ItemSpacing.x * 5;
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x;
    if (room > buttons)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - buttons);
    if (ImGui::Button(tr(app.language, S::mw_button)))
        open.missions = true;
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::chars_button)))
        open.characters = true;
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::plugin_button)))
        open.plugin = true;
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::console)))
        open.console = true;
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::themes)))
        open.themes = true;
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::settings)))
        open.settings = true;
    ImGui::Separator();

    const float status_h = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // what is wider than a panel can be scrolled to, never hidden past its edge
    ImGui::BeginChild("##left", ImVec2(avail.x * 0.42f, avail.y - status_h), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw_browser(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, avail.y - status_h), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw_item(app);
    ImGui::EndChild();
    ImGui::TextUnformatted(app.status.c_str());
    ImGui::End();
    draw_notices(app);

    if (open.settings)
        draw_settings(app, &open.settings);
    if (open.themes)
        draw_themes(app, &open.themes);
    if (open.console)
        draw_mods_console(app, &open.console);
    if (open.plugin)
        draw_plugin_parameters(app, &open.plugin);
    if (open.characters)
        draw_characters(app, &open.characters);
    if (open.missions)
        draw_mod_missions(app, &open.missions);
}

// The results a person must see (App::tell), over the window: a success in
// green for six seconds, a failure in red until it is closed.
void draw_notices(App& app) {
    const std::uint64_t now = SDL_GetTicks();
    app.notices.erase(std::remove_if(app.notices.begin(), app.notices.end(),
                                     [&](const App::Notice& n) { return n.ok && now - n.since > 6000; }),
                      app.notices.end());
    if (app.notices.empty())
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float k = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 48.0f * k), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(vp->WorkSize.x * 0.7f, vp->WorkSize.y * 0.5f));
    ImGui::Begin("##notices", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    for (std::size_t i = 0; i < app.notices.size();) {
        const App::Notice& n = app.notices[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, n.ok ? ImVec4(0.10f, 0.42f, 0.18f, 1) : ImVec4(0.55f, 0.12f, 0.12f, 1));
        ImGui::BeginChild("##n", ImVec2(vp->WorkSize.x * 0.6f, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - 90.0f * k);
        ImGui::TextUnformatted(n.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f * k);
        const bool close = ImGui::Button(tr(app.language, S::close));
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        if (close) {
            app.notices.erase(app.notices.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    ImGui::End();
}

int run_window(App& app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "khdays-studio: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) > 0
                            ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())
                            : 1.0f;
    app.window = SDL_CreateWindow("khdays-studio", static_cast<int>(1400 * scale), static_cast<int>(860 * scale),
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (app.window == nullptr) {
        std::fprintf(stderr, "khdays-studio: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(app.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(app.window);
    // the formats the port's own device asks for (port/host/render/gpu_present.c)
    app.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                                     false, nullptr);
    if (app.device == nullptr || !SDL_ClaimWindowForGPUDevice(app.device, app.window)) {
        const std::string why = SDL_GetError();
        std::fprintf(stderr, "khdays-studio: no graphics device: %s\n", why.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "khdays-studio", why.c_str(), app.window);
        return 1;
    }
    SDL_SetGPUSwapchainParameters(app.device, app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // nothing written outside the mod and studio.ini
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    apply_theme(app.settings.theme);  // the palette chosen in the Themes window
    style.FontSizeBase = 16.0f;
    io.Fonts->AddFontDefaultVector();
    ImGui_ImplSDL3_InitForSDLGPU(app.window);
    ImGui_ImplSDLGPU3_InitInfo init{};
    init.Device = app.device;
    init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(app.device, app.window);
    init.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init);

    app.images = std::make_unique<GpuImages>(app.device, static_cast<std::size_t>(app.settings.cache_mb) << 20);
    app.refresh_mods();
    app.start_catalog();

    Windows open;
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            const bool closing = event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                                                  event.window.windowID == SDL_GetWindowID(app.window));
            if (closing && (new_missions_unsaved() || characters_unsaved()))
                app.quit_asked = true;  // asked first (draw_unsaved_quit)
            else if (closing)
                done = true;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr &&
                !(open.characters && characters_take_drop(app, path_u8(event.drop.data))))
                app.offer(path_u8(event.drop.data));
        }
        if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        app.images->collect();
        app.take_catalog();
        take_dialog_files(app);

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_frame(app, open);
        done = draw_unsaved_quit(app) || done;
        ImGui::Render();

        ImDrawData* draw = ImGui::GetDrawData();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(app.device);
        SDL_GPUTexture* swapchain = nullptr;
        SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window, &swapchain, nullptr, nullptr);
        if (swapchain != nullptr && draw->DisplaySize.x > 0 && draw->DisplaySize.y > 0) {
            ImGui_ImplSDLGPU3_PrepareDrawData(draw, cmd);
            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = SDL_FColor{0.08f, 0.08f, 0.10f, 1.0f};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(draw, cmd, pass);
            SDL_EndGPURenderPass(pass);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    stop_sounds();
    app.catalog_cancel = true;
    if (app.catalog_thread.joinable())
        app.catalog_thread.join();
    app.job.reset();
    app.bulk_export.reset();  // stopped and waited for
    app.clear_candidate();
    movie_view_leave();
    SDL_WaitForGPUIdle(app.device);
    model_view_shutdown();
    app.images.reset();
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(app.device, app.window);
    SDL_DestroyGPUDevice(app.device);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}

}  // namespace
}  // namespace studio

int main(int argc, char** argv) {
    using namespace studio;
    attach_parent_console();  // a terminal run still shows the command line's output
    // the port's folder: --port's, else found from where the tool was started or where it is (beside the game, or in
    // the repository's build/)
    const char* base = SDL_GetBasePath();
    std::filesystem::path root =
        find_port_root(std::filesystem::current_path(), base != nullptr ? path_u8(base) : std::filesystem::path());
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    if (!take_port_argument(args, &root)) {
        std::fprintf(stderr, "khdays-studio: --port needs a folder\n");
        return 2;
    }
    if (!args.empty())
        return run_command_line(root, args);

    App app;
    std::string note;
    app.settings = load_settings(root, &note);
    app.mods_root = root / "mods";
    apply_language(app);
    if (!note.empty())
        app.tell(fmt(app.language, S::settings_bad, {note}), false);
    return run_window(app);
}
