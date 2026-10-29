// The chosen mod's native plugin's parameters (app.h): a control for each, as its mod describes it -- a slider within
// its bounds, a box for a flag, a list for its choices, a field for a text -- and the values saved beside the
// description for the game to read as it starts. The plugin itself is never loaded.
#include "../app.h"
#include "../strings.h"

#include "core/files/paths.h"
#include "core/mods/plugin_parameters.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace studio {

namespace {

struct Shown {
    std::filesystem::path mod;  // whose parameters are read, empty: none yet
    PluginParameters parameters;
    bool described = false;
};

Shown shown;

void read_again(const std::filesystem::path& mod) {
    shown.mod = mod;
    shown.described = load_plugin_parameters(mod, shown.parameters);
}

// A parameter's words in the tool's language, else without one, else its name.
const char* label_of(const KhdaysParameter& q, Language language) {
    const int l = static_cast<int>(language);
    return q.labels[l] != nullptr ? q.labels[l] : q.label != nullptr ? q.label : q.name;
}

const char* help_of(const KhdaysParameter& q, Language language) {
    const int l = static_cast<int>(language);
    return q.helps[l] != nullptr ? q.helps[l] : q.help;
}

void set(App& app, const char* name, const std::string& value) {
    std::string why;
    if (!set_plugin_parameter(shown.parameters, name, value, why))
        app.tell(std::string(name) + ": " + why, false);
}

bool bounded(const KhdaysParameter& q) {
    return q.min <= q.max && std::isfinite(q.min) && std::isfinite(q.max);
}

void control(App& app, KhdaysParameter& q) {
    const std::string id = std::string("##") + q.name;
    char text[64];
    switch (q.kind) {
    case KHDAYS_PARAMETER_INTEGER: {
        int v = static_cast<int>(q.number);
        const bool moved = bounded(q)
                               ? ImGui::SliderInt(id.c_str(), &v, static_cast<int>(q.min), static_cast<int>(q.max))
                               : ImGui::InputInt(id.c_str(), &v);
        if (moved)
            set(app, q.name, std::to_string(v));
        break;
    }
    case KHDAYS_PARAMETER_NUMBER: {
        double v = q.number;
        const double low = q.min, high = q.max;
        const bool moved = bounded(q) ? ImGui::SliderScalar(id.c_str(), ImGuiDataType_Double, &v, &low, &high, "%.3f")
                                      : ImGui::InputDouble(id.c_str(), &v, 0.0, 0.0, "%.3f");
        if (moved) {
            std::snprintf(text, sizeof(text), "%.9g", v);
            set(app, q.name, text);
        }
        break;
    }
    case KHDAYS_PARAMETER_FLAG: {
        bool v = q.number != 0;
        if (ImGui::Checkbox(id.c_str(), &v))
            set(app, q.name, v ? "yes" : "no");
        break;
    }
    case KHDAYS_PARAMETER_CHOICE: {
        const int now = static_cast<int>(q.number);
        if (ImGui::BeginCombo(id.c_str(), now >= 0 && now < q.choice_count ? q.choices[now] : "")) {
            for (int i = 0; i < q.choice_count; ++i)
                if (ImGui::Selectable(q.choices[i], i == now))
                    set(app, q.name, q.choices[i]);
            ImGui::EndCombo();
        }
        break;
    }
    default: {
        std::string v = q.text != nullptr ? q.text : "";
        if (ImGui::InputText(id.c_str(), &v))
            set(app, q.name, v);
        break;
    }
    }
}

std::string default_text(const KhdaysParameter& q) {
    KhdaysParameter d = q;  // a view with its default as its value (nothing of it is freed here)
    d.number = q.default_number;
    d.text = q.default_text;
    char text[256];
    khdays_parameters_value_text(&d, text, sizeof(text));
    return text;
}

}  // namespace

void draw_plugin_parameters(App& app, bool* open) {
    const float k = ImGui::GetStyle().FontScaleDpi;
    const Mod* mod = app.current_mod();
    if (mod != nullptr && mod->dir != shown.mod)
        read_again(mod->dir);
    ImGui::SetNextWindowSize(ImVec2(560.0f * k, 420.0f * k), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(app.language, S::plugin_title), open)) {
        ImGui::End();
        return;
    }
    if (mod == nullptr) {
        ImGui::TextUnformatted(tr(app.language, S::plugin_no_mod));
        ImGui::End();
        return;
    }
    if (ImGui::Button(tr(app.language, S::plugin_reload)))
        read_again(mod->dir);
    if (!shown.described) {
        ImGui::TextWrapped("%s", fmt(app.language, S::plugin_none, {mod->name}).c_str());
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::plugin_save))) {
        std::string why;
        if (save_plugin_parameters(shown.parameters, why))
            app.tell(fmt(app.language, S::plugin_saved, {u8(shown.parameters.values)}), true);
        else
            app.tell(fmt(app.language, S::save_failed, {why}), false);
    }
    if (shown.parameters.changed) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr(app.language, S::plugin_unsaved));
    }
    ImGui::Separator();
    if (ImGui::BeginTable("##parameters", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < shown.parameters.list.count; ++i) {
            KhdaysParameter& q = shown.parameters.list.list[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label_of(q, app.language));
            const char* help = help_of(q, app.language);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s%s", help != nullptr ? help : "", help != nullptr ? "\n" : "",
                                  fmt(app.language, S::plugin_default, {default_text(q)}).c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            control(app, q);
        }
        ImGui::EndTable();
    }
    if (!shown.parameters.problems.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr(app.language, S::plugin_problems));
        for (const std::string& problem : shown.parameters.problems)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", problem.c_str());
    }
    ImGui::End();
}

}  // namespace studio
