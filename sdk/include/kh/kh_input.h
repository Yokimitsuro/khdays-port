/* KH Mod SDK: the player's buttons (kh.input) -- what this copy's player presses, and buttons kept from the game.
 *
 * The buttons are the game's own (the DS's, as the port maps the player's keyboard or gamepad onto them), once a
 * game frame, after the port's own screens took theirs. A plugin may hide buttons from the game: the game reads them
 * as let go for as long as the plugin hides them (hiding the four directions also stops the analog stick's
 * movement), while the plugin still reads them as pressed -- how a plugin starts a move of its own on a button the
 * game would act on. In a room the hidden buttons are hidden from what this copy sends the host too. On the game's
 * thread. */
#ifndef KH_INPUT_H
#define KH_INPUT_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_INPUT "kh.input"
#define KH_INPUT_VERSION 1

/* The buttons, as bits. */
#define KH_BUTTON_A (1u << 0)
#define KH_BUTTON_B (1u << 1)
#define KH_BUTTON_SELECT (1u << 2)
#define KH_BUTTON_START (1u << 3)
#define KH_BUTTON_RIGHT (1u << 4)
#define KH_BUTTON_LEFT (1u << 5)
#define KH_BUTTON_UP (1u << 6)
#define KH_BUTTON_DOWN (1u << 7)
#define KH_BUTTON_R (1u << 8)
#define KH_BUTTON_L (1u << 9)
#define KH_BUTTON_X (1u << 10)
#define KH_BUTTON_Y (1u << 11)
#define KH_BUTTONS_DIRECTIONS (KH_BUTTON_RIGHT | KH_BUTTON_LEFT | KH_BUTTON_UP | KH_BUTTON_DOWN)

typedef struct KhInput {
    uint32_t size;

    /* The buttons held at the game's last frame, those pressed then (up the frame before) and those let go then:
     * the player's, hidden or not. */
    uint32_t (*held)(void);
    uint32_t (*pressed)(void);
    uint32_t (*released)(void);

    /* From the game's next frame the game reads `buttons` as let go, until the plugin hides others (0: none) or
     * stops. Each plugin hides its own; the game misses what any of them hides. */
    void (*hide)(KhPlugin *plugin, uint32_t buttons);

    /* The game's frames so far (its pad reads: 60 a second, 30 with the 3D on both screens). */
    uint32_t (*frame)(void);
} KhInput;

#ifdef __cplusplus
}
#endif

#endif
