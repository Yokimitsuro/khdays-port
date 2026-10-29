// Synthetic actor and controls exercise the generated command dispatcher while INFORMATION is open.
#include "../ui/hud_options.h"
extern "C" {
#include "../runtime/core/window.h"
void Ov022_UpdateCommandInput(unsigned);
}
#include <cstring>
#include <stdexcept>

namespace {
constexpr unsigned actor = 0x02120000, menu = 0x02124000, equip = 0x02125000;
int command_list, controls, blocked, pause_mode, navigation, shortcut, activated, command_panel;
unsigned &word(unsigned at)
{
    return *static_cast<unsigned *>(KHDAYS_W(at));
}
unsigned short &half(unsigned at)
{
    return *static_cast<unsigned short *>(KHDAYS_W(at));
}
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void press(unsigned short held, unsigned short pressed)
{
    half(0x0204c18c) = held;
    half(0x0204c190) = pressed;
    navigation = activated = 0;
    shortcut = -1;
    Ov022_UpdateCommandInput(actor);
}
} // namespace
extern "C" {
unsigned Ov022_GetGlobalPlusE0(void)
{
    return menu;
}
int func_ov022_020ab350(unsigned)
{
    return blocked;
}
int Ov002_RunShutdownHook(void)
{
    return 0;
}
int Session_GetLocalPlayerIndex(void)
{
    return 0;
}
int PauseMenu_GetMode(void)
{
    return pause_mode;
}
int func_ov022_0209029c(unsigned, int)
{
    return 0;
}
int Load2DArrayU8(int, int)
{
    return 0;
}
void Ov002_HudSetSlotValue(int, int) {}
int GameState_IsFlagSet(int)
{
    return 0;
}
int Ov002_GetStateWord(void)
{
    return 0;
}
int func_ov022_02095450(unsigned)
{
    return 0;
}
void Ov002_PanelSetHelpVisible(int) {}
void Ov002_UpdateVisibleColumnMask(int) {}
void Ov002_SetColumnBit(int, int) {}
void Ov002_PanelSelectSource(int, int) {}
int Ov022_GetSubKindIfState2(unsigned)
{
    return -1;
}
int Ov022_IsState9Or6WithFlag200(unsigned)
{
    return 0;
}
int Ov002_IsObjectFree(unsigned, int)
{
    return 0;
}
int func_ov022_0209fc78(unsigned, int)
{
    return 0;
}
int Ov022_IsInputAllowedForActiveSlot(void)
{
    return 0;
}
int GameState_GetField(int flag, int)
{
    return flag == 0x37c6 ? command_list : controls;
}
void Ov002_Hud_SetSecondaryFlag(int) {}
void Ov002_SelectShortcut(int value)
{
    shortcut = value;
}
int Ov002_IsMissionClearFinished(int)
{
    return 0;
}
void Ov002_Hud_ActivatePanelSlot(void)
{
    ++activated;
}
void Ov002_RequestCrawlSkip(void) {}
int Ov002_AcceptRequestAndNotify(int)
{
    return 0;
}
void Ov002_PanelCursorNext(void)
{
    ++navigation;
}
int func_ov022_02083f0c(void)
{
    return -1;
}
int Ov002_IsObjectFlag2000Set(int)
{
    return 0;
}
unsigned short Ov022_GetRepeatKeys(void)
{
    return half(0x0204c190);
}
void Ov002_PanelCursorPrev(void)
{
    --navigation;
}
void Ov002_PanelCursorStepLeft(void)
{
    navigation += 10;
}
void Ov002_PanelCursorStepRight(void)
{
    navigation -= 10;
}
unsigned short Ov002_GetPanelField01a4(void)
{
    return 8;
}
unsigned short Ov002_Panel_GetSelectedCellArg(void)
{
    return 123;
}
unsigned short Ov002_LookupRowLabel(void)
{
    return 0;
}
unsigned short Ov002_GetCachedEntryField0(void)
{
    return 0;
}
unsigned short Ov002_GetCachedEntryField2(void)
{
    return 0;
}
int Ov002_Panel_GetCursor(void)
{
    return 0;
}
int Ov002_Panel_GetState(void)
{
    return 0;
}
void khdays_trace_call(const char *) {}
void khdays_trace_call_arg(const char *, int) {}
void khdays_stop_hud(int, unsigned) {}
void khdays_controls_command_input(int, int panel, int, int, int, int, int, int)
{
    command_panel = panel;
}
}

void information_input_test(bool keep, unsigned panel)
{
    std::memset(KHDAYS_W(actor), 0, 0x4000);
    word(actor + 0x4f8) = equip;
    half(actor + 0x12) = 100;
    for (unsigned state = 0; state <= 11; ++state) {
        word(panel + 0x18c) = state;
        const bool available = keep || state == 0 || state > 8;
        command_list = 1;
        press(0x400, 0x400);
        check(navigation == int(available), "INFORMATION command cursor gate");
        check(command_panel == int(!available), "D-pad enhancement received wrong field eligibility");
        command_list = 0;
        *static_cast<unsigned char *>(KHDAYS_W(menu + 1)) = 0;
        press(0x400, 0x400);
        check(*static_cast<unsigned char *>(KHDAYS_W(menu + 1)) == (available ? 2 : 0), "directional menu entry gate");
        constexpr unsigned short directions[] = {0x40, 0x80, 0x20, 0x10};
        constexpr int moves[] = {-1, 1, 10, -10};
        for (unsigned i = 0; i < 4; ++i) {
            press(0x400 | directions[i], directions[i]);
            check(navigation == (available ? moves[i] : 0), "directional menu cursor gate");
        }
        for (controls = 0; controls <= 1; ++controls) {
            constexpr unsigned short buttons[] = {1, 0x400, 0x800, 2};
            for (int i = 0; i < 4; ++i) {
                press((controls ? 0x300 : 0x200) | buttons[i], buttons[i]);
                check(shortcut == (available ? i : -1), "INFORMATION shortcut gate");
            }
        }
        press(1, 1);
        check(activated == 1, "native command activation changed");
        check(*static_cast<unsigned char *>(KHDAYS_W(actor + 0x4d0)) == (available ? 8 : 7),
              "INFORMATION rewrote selected command kind");
        if (available)
            check(half(actor + 0x4d2) == 123, "selected command argument lost");
    }
    word(panel + 0x18c) = 4;
    blocked = 1;
    press(0x400, 0x400);
    check(navigation == 0 && activated == 0, "scripted control restriction bypassed");
    blocked = 0;
    half(actor + 0x12) = 0;
    press(0x400, 0x400);
    check(navigation == 0 && activated == 0, "dead actor input restriction bypassed");
    half(actor + 0x12) = 100;
    *static_cast<unsigned char *>(KHDAYS_W(0x0204c240)) = 4;
    pause_mode = 2;
    press(0x400, 0x400);
    check(navigation == 0 && activated == 0, "pause input restriction bypassed");
    pause_mode = 0;
    *static_cast<unsigned char *>(KHDAYS_W(0x0204c240)) = 0;
    word(panel + 0x18c) = 0;
}
