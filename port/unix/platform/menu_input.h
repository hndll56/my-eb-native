#ifndef EB_MENU_INPUT_H
#define EB_MENU_INPUT_H

/* Input for the host menus (settings, debug). Menus run their own small loop while the game
 * is paused, so they read SDL events directly instead of going through the game pad mask. */

#include <stdbool.h>

typedef struct {
    bool up, down, left, right; /* navigation edges (keyboard arrows/WASD-free, D-pad, left stick) */
    bool confirm, back;         /* Enter/Z or A button; Escape/X or B button */
    bool page_prev, page_next;  /* PageUp/PageDown or shoulder buttons */
    bool redraw;                /* window exposed/resized: present again */
    bool quit;                  /* window closed */
    int key;                    /* raw SDL scancode pressed this call (0 = none) */
    int pad_button;             /* raw SDL_GameControllerButton pressed this call (-1 = none) */
    int text_char;              /* ASCII typed this call (0 = none), for numeric entry */
} menu_in_t;

/* Pump events once. Always returns the edges seen since the previous call. */
void menu_poll(menu_in_t *in);

/* Forget held keys/buttons (called when a menu closes so the game does not see stuck input). */
void menu_input_flush(void);

#endif
