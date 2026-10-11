#ifndef EB_MENUS_H
#define EB_MENUS_H

#include <stdbool.h>

/* Pre-game launcher menu. Shown once at startup before the game loop. */
typedef enum {
    MM_RESULT_NEW_GAME,
    MM_RESULT_CONTINUE,
    MM_RESULT_EXIT,
} main_menu_result_t;
main_menu_result_t main_menu_run(void);

/* Settings menu (Esc / F10 / gamepad Guide). Modal: the game is paused while it is open.
 * Every change is applied to the running game immediately and saved when the menu closes. */
void settings_menu_run(void);

/* The same menu opened from the launcher, before a game is running: the save/load-state rows
 * are shown but disabled. */
void settings_menu_run_launcher(void);

/* Save anywhere. The game already has a crash-safe savestate (ping-pong slot files, see
 * platform/sdl2_savestate.c and src/core/state_dump.c); these hook it to the menu and give
 * visible feedback. A request is carried out at the next safe point of the game loop (the game
 * keeps running briefly so any blocking helper can unwind; an input prompt that never
 * returns makes the request fail and is reported, never written half-way). */
void savestate_request(bool load);
/* Call once per input poll: shows "STATE SAVED" / "SAVE FAILED" style messages for requests made
 * through savestate_request() or the F6/F7 hotkeys. */
void savestate_toast_poll(void);

/* Apply cfg to things other than the window (volumes). Called at startup and after changes. */
void settings_apply_audio(void);

/* Apply text_speed and text_flavour from cfg to game_state. Called at startup and after changes. */
void settings_apply_game(void);
extern const char *eb_mods_dir; /* --mods DIR, or NULL */

/* Debug / developer menu (F12). Disabled unless started with --debug or enabled in code. */
void debug_set_enabled(bool on);
bool debug_is_enabled(void);
void debug_menu_open(void);
/* Install the HUD / collision overlay if either is on (also used by --debug-cmd hud). */
void debug_refresh_overlay(void);

/* Called from the main loop between game steps, where game state may be changed safely. */
void debug_root_boundary(void);

/* Parse "--debug-cmd FRAME:command args" test hooks (queued and run at that frame). */
bool debug_queue_script_command(const char *spec);


/* Test hook (--debug-cmd FRAME:pad MASK): OR a pad mask into the real input until cleared. */
#include <stdint.h>
void platform_test_set_pad(uint16_t mask);

#endif
