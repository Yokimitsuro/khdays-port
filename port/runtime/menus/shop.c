/* The Moogle Shop (shop.h). The story's shop is ov026 (a sub-scene of the field, Ov026_FillSceneHooks),
 * Mission Mode's the same code in ov008 (each function 0x2eb0 lower, the same context). The shop object's
 * update is its step (no mode of its own to read): ShopTabSelectTick the menu, ShopListInput a list, and
 * so on (the decomp's ov026_shop). Its context (Ov026_ShopCreate: 0xc608 bytes):
 *   +0xc250  the list's tab: 0 Buy, 1 Sell, 2 Synthesize, 3 Redeem (copied from the cursor on entering)
 *   +0xc31c  the munny row's change shown (s32), +0xc320 the heart points'
 *   +0xc3b0  the menu's cursor (0-4)
 *   +0xc4e4  the quantity dialog's stage (s16): 0 the count, 1 OK/Cancel; +0xc4e7 (u8) 0 OK, 1 Cancel
 *   +0xc51c  the recipe panel up (Synthesize: the moogle not drawn)
 *   +0xc534  the synthesis question's stage (s16): 0 asked, 1 done, 2 only one allowed; +0xc53c 0 Yes, 1 No
 *   +0xc5f0  Synthesize offered, +0xc5f4 Redeem offered
 *   +0xbff0  the moogle's model instance (mi/ch/70.z): +0x00 its flags (0x20: its rotation made again from
 *            its angles at the next draw), +0x7c its turn about Y (u16, 0x10000 a whole turn)
 * The money: the game state's +0x8 (munny) and +0xc (heart points), as the camp reads them. */
#include "shop.h"

#include "../core/io.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../mods/installed.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);
int khdays_overlay_loaded(int id); /* overlays.c */

#define STORY_CONTEXT 0x02091368u   /* data_ov026_02091368 */
#define MISSION_CONTEXT 0x02090facu /* data_ov008_02090fac */
#define GAME_STATE 0x0204be18u
#define MUNNY 0x8u
#define HEARTS 0xcu
#define TAB 0xc250u
#define MUNNY_CHANGE 0xc31cu
#define HEARTS_CHANGE 0xc320u
#define CURSOR 0xc3b0u
#define QUANTITY_STAGE 0xc4e4u
#define QUANTITY_CHOICE 0xc4e7u
#define RECIPE 0xc51cu
#define SYNTHESIS_STAGE 0xc534u
#define SYNTHESIS_CHOICE 0xc53cu
#define SYNTHESIS_OFFERED 0xc5f0u
#define REDEEM_OFFERED 0xc5f4u
#define MODEL 0xbff0u
#define MODEL_FLAGS 0x00u
#define MODEL_TURN 0x7cu
#define MODEL_REBUILD 0x20u
#define SEEN_FRAMES 8u /* the shop gone when none of its steps ran so long */
#define OV_SHOP 26
#define OV_CAMP 8

/* the shop's steps: each overlay's own function for each */
#define SHOP_STEPS(X)                                                                                                  \
    X(EnterSelectionScreen, KHDAYS_SHOP_OPENING)                                                                       \
    X(SelectionFadeInTick, KHDAYS_SHOP_OPENING)                                                                        \
    X(ShopEnterTabSelectWhenReady, KHDAYS_SHOP_MENU)                                                                   \
    X(ShopTabSelectTick, KHDAYS_SHOP_MENU)                                                                             \
    X(ShopFadeOutThenConfirm, KHDAYS_SHOP_TO_LIST)                                                                     \
    X(ConfirmSelection, KHDAYS_SHOP_TO_LIST)                                                                           \
    X(RebuildTabList, KHDAYS_SHOP_LIST) /* (also after a dialog: the list stays) */                                    \
    X(SelectionIdleTick, KHDAYS_SHOP_LIST)                                                                             \
    X(ShopListInput, KHDAYS_SHOP_LIST)                                                                                 \
    X(ShopFadeOutThenSelection, KHDAYS_SHOP_FROM_LIST)                                                                 \
    X(SelectionTick, KHDAYS_SHOP_FROM_LIST)                                                                            \
    X(EnterDetailView, KHDAYS_SHOP_QUANTITY)                                                                           \
    X(Shop_QuantityDialogTick, KHDAYS_SHOP_QUANTITY)                                                                   \
    X(ResetPanelAndRebuildTabs, KHDAYS_SHOP_LIST)                                                                      \
    X(ShopOpenDetailStep, KHDAYS_SHOP_SYNTHESIS)                                                                       \
    X(ShopDetailConfirmTick, KHDAYS_SHOP_SYNTHESIS)                                                                    \
    X(Shop_ReturnToList, KHDAYS_SHOP_LIST)                                                                             \
    X(Shop_OpenBuy, KHDAYS_SHOP_REWARD)                                                                                \
    X(Shop_WaitDialogClose, KHDAYS_SHOP_REWARD)                                                                        \
    X(ShopConfirmPurchase, KHDAYS_SHOP_REWARD)                                                                         \
    X(CommitSelection, KHDAYS_SHOP_REWARD)                                                                             \
    X(FadeToBlackStep, KHDAYS_SHOP_CLOSING)

#define DECLARE(name, step)                                                                                            \
    extern void Ov026_##name(void);                                                                                    \
    extern void Ov008_##name(void);
SHOP_STEPS(DECLARE)
#undef DECLARE

static const struct {
    void (*story)(void), (*mission)(void);
    int step;
} STEPS[] = {
#define ENTRY(name, step) {Ov026_##name, Ov008_##name, step},
    SHOP_STEPS(ENTRY)
#undef ENTRY
};

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysShopView shown;
static int seen;
static u32 seen_at;
static volatile LONG turn_milli; /* thousandths of a degree: the shop's screen sets it (host) */
static int turned;               /* the moogle turned at this visit: put back when the turn is 0 */
static int restore = -1;         /* the menu's option to put the cursor back on (the list just left), -1 none */

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

static u16 rd16(u32 address)
{
    return *(const volatile u16 *)KHDAYS_W(address);
}

static u8 rd8(u32 address)
{
    return *(const volatile u8 *)KHDAYS_W(address);
}

static void wr16(u32 address, u16 value)
{
    *(volatile u16 *)KHDAYS_W(address) = value;
}

static void wr32(u32 address, u32 value)
{
    *(volatile u32 *)KHDAYS_W(address) = value;
}

static int in_ram(u32 address)
{
    return khdays_game_ram(address);
}

/* A screen's master brightness: -16 black .. 16 white. */
static int16_t brightness(u32 reg)
{
    const u16 bright = IO16(reg);
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
}

void khdays_shop_set_turn(float degrees)
{
    InterlockedExchange(&turn_milli, (LONG)lroundf(degrees * 1000.0f));
}

/* The moogle turned to the viewer's left: its angle about Y written each frame, its rotation made again
 * from it at its next draw (Scene_DrawNode: MTX_RotY33; nothing in the shop writes it after its load:
 * 0, facing the camera). In the NitroSDK's row vectors a model's front (+Z, the camera's side) goes to
 * (sin, 0, cos): to the left is a negative angle. A turn of 0 puts it back (the two screens' shop). */
static void turn_moogle(u32 ctx)
{
    const u32 model = ctx + MODEL;
    const int angle = (int)lroundf(-(float)turn_milli / 1000.0f * 65536.0f / 360.0f);
    const u16 want = (u16)(angle & 0xffff);
    if (rd16(model + MODEL_TURN) != want) {
        wr16(model + MODEL_TURN, want);
        wr16(model + MODEL_FLAGS, (u16)(rd16(model + MODEL_FLAGS) | MODEL_REBUILD));
    }
}

void khdays_shop_update_ran(uintptr_t update)
{
    KhdaysShopView v;
    int step = KHDAYS_SHOP_NONE, mission = 0;
    u32 ctx, game;
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
        if (update == (uintptr_t)STEPS[i].story && khdays_overlay_loaded(OV_SHOP)) {
            step = STEPS[i].step;
            break;
        }
        if (update == (uintptr_t)STEPS[i].mission && khdays_overlay_loaded(OV_CAMP)) {
            step = STEPS[i].step;
            mission = 1;
            break;
        }
    }
    if (step == KHDAYS_SHOP_NONE) {
        if (seen && khdays_events_vblank_count() - seen_at > SEEN_FRAMES) {
            seen = 0;
            AcquireSRWLockExclusive(&lock);
            shown.shown = 0;
            ++shown.serial;
            ReleaseSRWLockExclusive(&lock);
        }
        return;
    }
    ctx = rd32(mission ? MISSION_CONTEXT : STORY_CONTEXT);
    game = rd32(GAME_STATE);
    if (!in_ram(ctx) || !in_ram(ctx + 0xc600u)) {
        return;
    }
    if (!seen) {
        restore = -1;
    }
    /* the Menu Overhaul's cursor memory: back from a list, the menu's cursor on its option again (the game's
     * EnterSelectionScreen puts it on Buy: InitFilterRows), through the fade in and the menu's first frame */
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        restore = -1;
    } else if (step == KHDAYS_SHOP_FROM_LIST) {
        const int tab = (int)rd32(ctx + TAB);
        restore = tab >= 0 && tab < 4 ? tab : -1;
    } else if (restore >= 0 && (step == KHDAYS_SHOP_OPENING || step == KHDAYS_SHOP_MENU)) {
        const int offered = (restore != KHDAYS_SHOP_SYNTHESIZE || rd32(ctx + SYNTHESIS_OFFERED) != 0) &&
                            (restore != KHDAYS_SHOP_REDEEM || rd32(ctx + REDEEM_OFFERED) != 0);
        if (offered) {
            wr32(ctx + CURSOR, (u32)restore);
        }
        if (step == KHDAYS_SHOP_MENU) {
            restore = -1;
        }
    } else if (step != KHDAYS_SHOP_OPENING) {
        restore = -1;
    }
    memset(&v, 0, sizeof(v));
    v.shown = 1;
    v.mission = mission;
    v.step = step;
    v.cursor = (int)rd32(ctx + CURSOR);
    v.options[KHDAYS_SHOP_BUY] = v.options[KHDAYS_SHOP_SELL] = v.options[KHDAYS_SHOP_EXIT] = 1;
    v.options[KHDAYS_SHOP_SYNTHESIZE] = rd32(ctx + SYNTHESIS_OFFERED) != 0;
    v.options[KHDAYS_SHOP_REDEEM] = rd32(ctx + REDEEM_OFFERED) != 0;
    v.tab = (int)rd32(ctx + TAB);
    v.quantity_stage = (int16_t)rd16(ctx + QUANTITY_STAGE);
    v.quantity_choice = rd8(ctx + QUANTITY_CHOICE);
    v.synthesis_stage = (int16_t)rd16(ctx + SYNTHESIS_STAGE);
    v.synthesis_choice = (int)rd32(ctx + SYNTHESIS_CHOICE);
    v.recipe = v.tab == KHDAYS_SHOP_SYNTHESIZE && rd32(ctx + RECIPE) != 0;
    if (in_ram(game)) {
        v.munny = rd32(game + MUNNY);
        v.hearts = rd32(game + HEARTS);
    }
    v.munny_change = (int32_t)rd32(ctx + MUNNY_CHANGE);
    v.hearts_change = (int32_t)rd32(ctx + HEARTS_CHANGE);
    v.language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    v.master_upper = brightness(0x006c);
    v.master_lower = brightness(0x106c);
    if (!seen) {
        turned = 0;
    }
    if (turn_milli != 0 || turned) {
        turn_moogle(ctx);
        turned = turn_milli != 0;
    }
    if (!seen || shown.step != step) {
        khdays_log("shop: %s, step %d, cursor %d, tab %d", mission ? "ov008" : "ov026", step, v.cursor, v.tab);
    }
    seen = 1;
    seen_at = khdays_events_vblank_count();
    AcquireSRWLockExclusive(&lock);
    v.serial = shown.serial + 1u;
    shown = v;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_shop_view(KhdaysShopView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}
