#include "app.h"

#include <SDL3/SDL.h>

#include "core/files/file_write.h"
#include "core/files/paths.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"

namespace studio {

WrappedTexts::WrappedTexts() {
    ImGui::PushTextWrapPos(0.0f);
}

WrappedTexts::~WrappedTexts() {
    ImGui::PopTextWrapPos();
}

void same_line_if_room(float width) {
    // after an item the cursor is at the next line's start: from there, what is
    // left of the line is the window's whole visible width
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
        ImGui::SameLine();
}

float button_width(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float check_width(const char* label) {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
}

void App::start_catalog() {
    if (catalog_thread.joinable()) {
        catalog_cancel = true;
        catalog_thread.join();
    }
    catalog_cancel = false;
    catalog_ready = false;
    catalog_progress = 0;
    CatalogSources sources;
    sources.graphics_dump = resolve(settings, settings.graphics_dump);
    sources.sounds_dump = resolve(settings, settings.sounds_dump);
    if (khdays::vfs::autodetect_data_root(settings.root / "data" / "extracted")) {
        sources.nitrofs = khdays::vfs::data_root() / "nitrofs";
    }
    catalog_thread = std::thread([this, sources] {
        catalog_next = build_catalog(sources, &catalog_progress, &catalog_cancel);
        catalog_ready = true;
    });
}

void App::take_catalog() {
    if (!catalog_ready || !catalog_thread.joinable())
        return;
    catalog_thread.join();
    catalog = std::move(catalog_next);
    catalog_next = Catalog{};
    by_path.clear();
    for (int i = 0; i < static_cast<int>(catalog.items.size()); ++i) {
        const Item& item = catalog.items[static_cast<std::size_t>(i)];
        for (const auto& rel : lookup_paths(item)) {
            const auto dot = rel.find_last_of('.');
            const auto slash = rel.find_last_of('/');
            // a text file's lookup path is its game path, extension and all
            const std::string key =
                item.kind == Kind::text_file || dot == std::string::npos || (slash != std::string::npos && dot < slash)
                    ? rel
                    : rel.substr(0, dot);
            by_path.emplace(key, i);
        }
    }
    selected = -1;
    results_stale = true;
    clear_candidate();
}

void App::refresh_mods() {
    const std::string keep = current_mod() != nullptr ? current_mod()->name : settings.mod;
    mods = list_mods(mods_root);
    mod = -1;
    for (int i = 0; i < static_cast<int>(mods.size()); ++i) {
        if (mods[static_cast<std::size_t>(i)].name == keep)
            mod = i;
    }
}

void App::tell(const std::string& text, bool ok) {
    status = text;
    notices.push_back({text, ok, SDL_GetTicks()});
}

void App::clear_candidate() {
    if (!candidate.staged.empty()) {
        std::error_code ec;
        fs::remove(candidate.staged, ec);
        // the staging folder goes when empty
        const fs::path staging = candidate.staged.parent_path();
        if (fs::is_empty(staging, ec) && !ec)
            fs::remove(staging, ec);
        const fs::path studio_dir = staging.parent_path();
        if (fs::is_empty(studio_dir, ec) && !ec)
            fs::remove(studio_dir, ec);
    }
    candidate = Candidate{};
    candidate_error.clear();
}

void App::offer(const fs::path& file) {
    clear_candidate();
    if (selected < 0 || selected >= static_cast<int>(catalog.items.size()))
        return;
    const Mod* m = current_mod();
    if (m == nullptr) {
        candidate_error = tr(language, S::choose_mod_first);
        return;
    }
    const Item& item = catalog.items[static_cast<std::size_t>(selected)];
    if (item.kind == Kind::sheet && sheet_offer(*this, file))
        return;  // an edited assembled picture
    if (item.kind == Kind::model) {
        model_offer(*this, item, file);
        return;
    }
    if (item.kind == Kind::room) {
        room_offer(*this, item, file);
        return;
    }
    if (item.kind == Kind::movie) {
        movie_offer(*this, item, file);
        return;
    }
    if (item.kind == Kind::text_file)
        return;  // texts are edited in their view
    candidate.item = selected;
    candidate.source = file;
    candidate.any_palette = false;

    // Staged in the mod's own folder for the tool, under a plain name, so the
    // port's readers (some open files by the C library's narrow paths) read it.
    const fs::path staging = m->dir / "studio" / "staging";
    std::error_code ec;
    fs::create_directories(staging, ec);
    std::string ext = lower(u8(file.extension()));
    const bool picture = item.kind == Kind::texture || item.kind == Kind::sheet;
    if (picture)
        ext = ".png";
    candidate.staged = staging / ("candidate" + ext);

    if (picture && !is_png(file)) {
        const std::string why = convert_to_png(file, candidate.staged);
        if (!why.empty()) {
            candidate.check.refuse(Note::not_image, {why});
            return;
        }
        candidate.check.info(Note::converted_to_png, {lower(u8(file.extension()))});
    } else if (const std::string why = copy_bytes(file, candidate.staged); !why.empty()) {
        candidate_error = why;
        return;
    }

    CheckResult r;
    if (item.kind == Kind::texture) {
        int w = 0, h = 0;
        std::string why;
        png_size(item.original, &w, &h, &why);
        r = check_texture(candidate.staged, w, h);
    } else if (item.kind == Kind::sheet) {
        int w = 0, h = 0;
        std::string why;
        png_size(item.original, &w, &h, &why);
        r = check_sheet(candidate.staged, w, h);
    } else if (item.kind == Kind::sound) {
        r = check_sound(candidate.staged);
    }
    for (auto& f : r.findings)
        candidate.check.findings.push_back(f);
    if (r.verdict == Verdict::refused)
        candidate.check.verdict = Verdict::refused;
    else if (r.verdict == Verdict::warned && candidate.check.verdict == Verdict::accepted)
        candidate.check.verdict = Verdict::warned;
}

std::string finding_text(const App& app, const Finding& f) {
    static const S words[] = {
        S::note_not_image,         S::note_not_png,          S::note_converted_to_png, S::note_texture_stretched,
        S::note_texture_scale,     S::note_sheet_size,       S::note_sheet_scale,      S::note_sheet_indexed,
        S::note_sheet_true_colour, S::note_sound_unreadable, S::note_sound_decoded,    S::note_sound_looped,
        S::note_sound_plays_once,
    };
    const auto i = static_cast<std::size_t>(f.note);
    if (i >= sizeof(words) / sizeof(words[0]))
        return {};
    auto arg = [&](std::size_t n) { return n < f.args.size() ? std::string_view(f.args[n]) : std::string_view(); };
    return fmt(app.language, words[i], {arg(0), arg(1), arg(2), arg(3)});
}

}  // namespace studio
