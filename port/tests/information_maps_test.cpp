// Exercise the generated native snapshot code, including nested snapshots and live HUD updates.
#include "../ui/hud_options.h"
#include "../host/app/hud_settings.h"
#include "../ui/kh2_hp_config.h"
extern "C" {
#include "../runtime/core/window.h"
void Ov002_PushMapSnapshot(int);
void Ov002_PopMapSnapshot(void);
void Ov002_SetPanelMode(int);
void Ov002_SetBgOfs_main001c(unsigned, unsigned);
void Ov002_SetBgOfs_main0014(unsigned, unsigned);
void Ov002_UpdatePanelScroll(void);
int Ov002_Hud_IsPanelOpen(void);
}
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>
namespace {
bool keep;
bool configured = true;
int *tween_output;
unsigned notify_ref, notify_calls;
unsigned next_address = 0x02200000;
std::vector<unsigned> stack;
std::set<unsigned> allocated;
constexpr unsigned context = 0x02100000, maps = 0x02110000;
unsigned &word(unsigned at)
{
    return *static_cast<unsigned *>(KHDAYS_W(at));
}
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
extern "C" {
unsigned char *khdays_window;
unsigned khdays_reference(const void *host)
{
    tween_output = const_cast<int *>(static_cast<const int *>(host));
    return 0x023f0000;
}
void Tween_Sample(unsigned, unsigned)
{
    *tween_output = 24 << 12;
}
int Ov002_UploadCaptionChars(int)
{
    return 1;
}
void Ov002_ReleaseSceneSurface1(int) {}
void Ov002_RetuneAmbientEmitter(void) {}
using NativeFunction = void (*)(void);
NativeFunction khdays_function(unsigned reference)
{
    require(reference != 0 && reference == notify_ref, "invalid/null scroll animation callback");
    ++notify_calls;
    const auto callback = &Ov002_SetBgOfs_main001c;
    NativeFunction result;
    static_assert(sizeof(result) == sizeof(callback));
    std::memcpy(&result, &callback, sizeof(result));
    return result;
}
unsigned NNSi_FndAllocFromDefaultExpHeap(unsigned size)
{
    const unsigned at = next_address;
    next_address += (size + 3) & ~3u;
    allocated.insert(at);
    return at;
}
unsigned NNS_FndAllocFromDefaultExpHeapEx(unsigned size, int)
{
    return NNSi_FndAllocFromDefaultExpHeap(size);
}
void NNSi_FndFreeFromDefaultHeap(unsigned at)
{
    require(allocated.erase(at) == 1, "snapshot double free");
}
void MI_CpuFill8(unsigned at, int value, unsigned size)
{
    std::memset(KHDAYS_W(at), value, size);
}
void MIi_CpuCopy16(unsigned src, unsigned dst, unsigned size)
{
    std::memcpy(KHDAYS_W(dst), KHDAYS_W(src), size);
}
void MIi_CpuClear16(int value, unsigned dst, unsigned size)
{
    std::memset(KHDAYS_W(dst), value, size);
}
void NNS_FndPrependListObject(unsigned, unsigned node)
{
    stack.push_back(node);
}
unsigned NNS_FndGetNextListObject(unsigned, unsigned)
{
    return stack.empty() ? 0 : stack.back();
}
void NNS_FndRemoveListObject(unsigned, unsigned node)
{
    require(stack.back() == node, "wrong snapshot popped");
    stack.pop_back();
}
int khdays_mods_find(const char *, char *path, int)
{
    std::strcpy(path, "synthetic.json");
    return 1;
}
int khdays_settings_game_language(void)
{
    return 1;
}
const char *khdays_language_code(int)
{
    return "en";
}
int khdays_textures_hd(void)
{
    return 1;
}
int khdays_ui_information_configured(void)
{
    return configured;
}
void khdays_log(const char *, ...) {}
void khdays_ui_changed(void) {}
void khdays_ui_text_settings_changed(void) {}
}
namespace khdays::ui::kh2 {
HudPresentationConfig load_hud_presentation_config(const std::filesystem::path &)
{
    HudPresentationConfig config;
    config.keep_hud_during_information = keep;
    config.align_hud_to_edges = keep;
    return config;
}
} // namespace khdays::ui::kh2
void information_input_test(bool keep, unsigned panel);
int main(int argc, char **)
{
    try {
        keep = argc == 1;
        khdays_window = static_cast<unsigned char *>(std::calloc(0x0b200000, 1));
        require(khdays_window != nullptr, "test RAM allocation");
        word(0x0207f60c) = context;
        for (unsigned i = 0; i < 3; ++i) {
            word(context + 0x58 + i * 4) = maps + i * 0x800;
            word(context + 0x70 + i * 4) = 1;
            std::memset(KHDAYS_W(maps + i * 0x800), int(i + 1), 0x800);
        }
        khdays_ui_information_push_maps();
        require(!khdays_ui_information_preserving_maps(), "snapshot scope leaked");
        require(*static_cast<unsigned char *>(KHDAYS_W(maps)) == (keep ? 1 : 0), "HUD text map preservation failed");
        require(*static_cast<unsigned char *>(KHDAYS_W(maps + 0x1000)) == (keep ? 3 : 0),
                "HUD map preservation did not follow option");
        require((word(stack.back() + 12) == 0) == keep, "preserved map must remain live rather than cached");
        *static_cast<unsigned char *>(KHDAYS_W(maps + 0x1000)) = 42;
        *static_cast<unsigned char *>(KHDAYS_W(maps)) = 40;
        *static_cast<unsigned char *>(KHDAYS_W(maps + 0x700)) = 99;
        Ov002_PushMapSnapshot(7);
        require(*static_cast<unsigned char *>(KHDAYS_W(maps + 0x1000)) == 0, "non-INFORMATION snapshot was affected");
        Ov002_PopMapSnapshot();
        khdays_ui_information_pop_maps();
        require(*static_cast<unsigned char *>(KHDAYS_W(maps + 0x1000)) == (keep ? 42 : 3),
                "snapshot pop overwrote live HUD updates");
        require(*static_cast<unsigned char *>(KHDAYS_W(maps)) == (keep ? 40 : 1), "HUD text update was lost on exit");
        require(*static_cast<unsigned char *>(KHDAYS_W(maps + 0x700)) == 1, "caption scratch rows were not restored");
        require(stack.empty() && allocated.empty(), "snapshot allocation/list imbalance");
        constexpr unsigned panel = context + 0x2000, bg3_scroll = 0x0b10001c;
        word(0x0207f614) = panel;
        for (unsigned state = 0; state <= 11; ++state) {
            word(panel + 0x18c) = state;
            const bool open = state >= 1 && state <= 8;
            require(bool(Ov002_Hud_IsPanelOpen()) == open, "native panel scheduler gate changed");
            require(bool(khdays_ui_command_panel_open()) == (open && !keep), "command input panel gate");
            configured = false;
            require(bool(khdays_ui_command_panel_open()) == open, "missing replacement changed command input");
            configured = true;
        }
        word(panel + 0x18c) = 0;
        information_input_test(keep, panel);
        word(bg3_scroll) = 77;
        constexpr unsigned bg1_scroll = 0x0b100014;
        word(bg1_scroll) = 0;
        Ov002_SetBgOfs_main0014(5, 216);
        int x = -1, y = -1;
        require(bool(khdays_ui_information_text_scroll(&x, &y)) == keep, "caption virtual scroll availability");
        require(!keep || (x == 5 && y == 216), "caption did not retain its independent scroll");
        require(word(bg1_scroll) == (keep ? 0u : 5u | (216u << 16)), "HUD text layer moved with caption");
        Ov002_SetPanelMode(1);
        notify_ref = word(panel + 0x234);
        require(notify_ref != 0, "INFORMATION must retain its mandatory scroll callback");
        word(panel + 0x1cc) = panel + 0x1c4;
        Ov002_UpdatePanelScroll();
        require(notify_calls == 1, "scroll animation did not invoke its callback");
        require(word(bg3_scroll) == (keep ? 77u : 0x180000u), "HUD scroll write did not follow option");
        Ov002_SetPanelMode(0);
        require(word(bg3_scroll) == (keep ? 77u : 0x180000u), "INFORMATION exit displaced HUD");
        require(word(bg1_scroll) == (keep ? 0u : 0x180000u), "INFORMATION exit displaced HUD text");
        int preferences[KHDAYS_OPT_COUNT];
        khdays_hud_settings_default(preferences);
        word(panel + 0x18c) = 0;
        require(khdays_ui_information_policy() == 3, "initial INFORMATION preferences");
        word(panel + 0x18c) = 4;
        require(khdays_ui_information_policy() == 3, "INFORMATION preference latch");
        preferences[KHDAYS_OPT_INFORMATION] = preferences[KHDAYS_OPT_INFORMATION_HUD] = 0;
        khdays_hud_settings_publish(preferences);
        require(khdays_ui_information_policy() == 3, "live preference interrupted native snapshot lifetime");
        word(panel + 0x18c) = 0;
        require(khdays_ui_information_policy() == 0, "INFORMATION preferences not applied after close");
        preferences[KHDAYS_OPT_INFORMATION] = preferences[KHDAYS_OPT_INFORMATION_HUD] = 1;
        khdays_hud_settings_publish(preferences);
        require(khdays_ui_information_policy() == 3, "INFORMATION could not be re-enabled live");
        khdays_hud_settings_default(preferences);
        require(preferences[KHDAYS_OPT_UI_PADDING] == 0, "UI padding default must be off");
        khdays_hud_settings_publish(preferences);
        require(khdays_ui_hud_options() & KHDAYS_HUD_ALIGN_EDGES, "padding off must align screen edges live");
        preferences[KHDAYS_OPT_UI_PADDING] = 1;
        khdays_hud_settings_publish(preferences);
        require(!(khdays_ui_hud_options() & KHDAYS_HUD_ALIGN_EDGES), "padding on must restore the original inset live");
        preferences[KHDAYS_OPT_UI_PADDING] = 0;
        khdays_hud_settings_publish(preferences);
        require(khdays_ui_hud_options() & KHDAYS_HUD_ALIGN_EDGES, "padding must toggle repeatedly without reloading");
        std::free(khdays_window);
        std::cout << "native INFORMATION snapshots passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
