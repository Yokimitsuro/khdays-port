#include "khdays/game/settings.h"

#include <array>

namespace khdays::game {

namespace {
ScreenLayout g_screen_layout = ScreenLayout::Vertical;
Language g_language = Language::English;

struct LanguageInfo {
    Language value;
    std::string_view code;
    std::string_view name;  // written in that language
};

// The five languages the European release ships. (`db_ja.p2` also exists but is
// a leftover — no other Japanese asset does, so it is not offered.)
constexpr std::array<LanguageInfo, 5> kLanguages{{
    {Language::German, "de", "Deutsch"},
    {Language::English, "en", "English"},
    {Language::Spanish, "es", "Espanol"},
    {Language::French, "fr", "Francais"},
    {Language::Italian, "it", "Italiano"},
}};
}  // namespace

ScreenLayout screen_layout() { return g_screen_layout; }

void set_screen_layout(const ScreenLayout layout) { g_screen_layout = layout; }

void toggle_screen_layout() {
    g_screen_layout = g_screen_layout == ScreenLayout::Vertical
                          ? ScreenLayout::Horizontal
                          : ScreenLayout::Vertical;
}

Language language() { return g_language; }

void set_language(const Language value) { g_language = value; }

std::string_view language_code(const Language value) {
    for (const auto& info : kLanguages) {
        if (info.value == value) {
            return info.code;
        }
    }
    return "en";
}

Language language_from_code(const std::string_view code) {
    for (const auto& info : kLanguages) {
        if (info.code == code) {
            return info.value;
        }
    }
    return Language::English;
}

std::string_view language_name(const Language value) {
    for (const auto& info : kLanguages) {
        if (info.value == value) {
            return info.name;
        }
    }
    return "English";
}

std::string localized_path(const std::string_view pattern) {
    std::string out;
    out.reserve(pattern.size() + 1U);
    const auto code = language_code(g_language);
    for (const char c : pattern) {
        if (c == '&') {
            out.append(code);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace khdays::game
