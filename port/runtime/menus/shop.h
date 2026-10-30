/* The Moogle Shop (the story's ov026, Mission Mode's byte-identical ov008 copy): what it shows, read from
 * the game's memory for the port's own page (host/screens/shop_screen.c), and its moogle turned a little
 * to the left. Addresses and offsets: the decomp's ov026_shop (Ov026_ShopCreate's context, 0xc608 bytes,
 * kept at data_ov026_02091368; ov008's at data_ov008_02090fac) -- see shop.c. */
#ifndef KHDAYS_SHOP_H
#define KHDAYS_SHOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The shop's step: its object's update (the game's function the task runs). */
enum {
    KHDAYS_SHOP_NONE,
    KHDAYS_SHOP_OPENING,   /* EnterSelectionScreen, SelectionFadeInTick: the first fade in */
    KHDAYS_SHOP_MENU,      /* ShopTabSelectTick (and ShopEnterTabSelectWhenReady): Buy, Sell... */
    KHDAYS_SHOP_TO_LIST,   /* an option chosen: the lower screen fades out, the list is made */
    KHDAYS_SHOP_LIST,      /* ShopListInput (and SelectionIdleTick: its fade in) */
    KHDAYS_SHOP_FROM_LIST, /* B on the list: back to the menu */
    KHDAYS_SHOP_QUANTITY,  /* Buy's and Sell's quantity, then OK or Cancel */
    KHDAYS_SHOP_SYNTHESIS, /* "Synthesize this panel?", then its result */
    KHDAYS_SHOP_REWARD,    /* Redeem's reward shown */
    KHDAYS_SHOP_CLOSING    /* FadeToBlackStep: Exit or B on the menu */
};

/* The menu's options (the cursor's values; Synthesize and Redeem may be left out). */
enum { KHDAYS_SHOP_BUY, KHDAYS_SHOP_SELL, KHDAYS_SHOP_SYNTHESIZE, KHDAYS_SHOP_REDEEM, KHDAYS_SHOP_EXIT };

typedef struct KhdaysShopView {
    int shown;                             /* the shop is open */
    int mission;                           /* Mission Mode's (ov008) */
    int step;                              /* KHDAYS_SHOP_* */
    int cursor;                            /* the menu's: KHDAYS_SHOP_BUY.. EXIT */
    int options[5];                        /* each option shown (Synthesize and Redeem may not be) */
    int tab;                               /* the list's: Buy, Sell, Synthesize, Redeem */
    int quantity_stage, quantity_choice;   /* 0 the count, 1 OK/Cancel; 0 OK, 1 Cancel */
    int synthesis_stage, synthesis_choice; /* 0 the question, 1 done, 2 "only one"; 0 Yes, 1 No */
    int recipe;                            /* the upper screen shows the recipe panel (no moogle) */
    uint32_t munny, hearts;
    int32_t munny_change, hearts_change; /* the price shown against them (a pending purchase or sale) */
    int language;                        /* 1-5 */
    int16_t master_upper, master_lower;  /* each screen's brightness: -16 black .. 16 white */
    unsigned serial;                     /* counts the views published */
} KhdaysShopView;

/* The game's thread: an object's update ran (runtime/core/diag.c check_update). */
void khdays_shop_update_ran(uintptr_t update);

/* The window's thread: the shop as last read. `shown` 0 when it is not open. */
void khdays_shop_view(KhdaysShopView *out);

/* The moogle's turn (degrees, positive to the viewer's left; the window's thread: the one-screen shop's
 * page sets it, host/screens/shop_screen.c); 0, the default, faces it to the player. */
void khdays_shop_set_turn(float degrees);

#ifdef __cplusplus
}
#endif

#endif
