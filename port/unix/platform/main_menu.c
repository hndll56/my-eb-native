#include "platform/menus.h"
#include "platform/config.h"
#include "platform/menu_input.h"
#include "platform/platform.h"
#include "platform/ui.h"
#include "platform/video.h"
#include "game/game_state.h"
#include "include/constants.h"
#include <SDL.h>
#include <string.h>

#define W EB_VIEWPORT_WIDTH
#define H EB_VIEWPORT_HEIGHT

/* Check if any save file has data */
static bool any_save_exists(void) {
    GameState saved = game_state;
    for (int slot = 0; slot < SAVE_COUNT; slot++) {
        if (load_game(slot) && game_state.favourite_thing[1] != 0) {
            game_state = saved;
            return true;
        }
    }
    game_state = saved;
    return false;
}

typedef enum { MM_NEW_GAME, MM_CONTINUE, MM_OPTIONS, MM_EXIT, MM_COUNT } mm_item_t;

static const char *labels[MM_COUNT] = {
    "New Game", "Continue", "Options", "Exit"
};

static int sel;
static bool has_saves;

static void draw_main_menu(pixel_t *f, int w, int h) {
    /* Dark background */
    ui_fill(f, w, h, 0, 0, w, h, ui_rgb(12, 14, 32));

    /* Title */
    pixel_t title_col = ui_rgb(255, 214, 64);
    pixel_t white = ui_rgb(255, 255, 255);
    pixel_t dim = ui_rgb(100, 100, 100);
    pixel_t sel_col = ui_rgb(120, 190, 255);
    pixel_t border = ui_rgb(60, 130, 230);

    /* Centered title */
    const char *title = "EARTHBOUND";
    int tw = ui_text_width(title);
    ui_text(f, w, h, (w - tw) / 2, 50, title_col, title);

    /* Subtitle */
    const char *sub = "Native PC Port";
    int sw = ui_text_width(sub);
    ui_text(f, w, h, (w - sw) / 2, 64, dim, sub);

    /* Menu box */
    int box_w = 120, box_h = 62;
    int box_x = (w - box_w) / 2;
    int box_y = 88;
    ui_fill(f, w, h, box_x, box_y, box_w, box_h, ui_rgb(16, 20, 48));
    ui_frame(f, w, h, box_x, box_y, box_w, box_h, border);

    /* Menu items */
    for (int i = 0; i < MM_COUNT; i++) {
        int y = box_y + 8 + i * 12;
        bool is_sel = (i == sel);
        bool disabled = (i == MM_CONTINUE && !has_saves);

        if (is_sel && !disabled)
            ui_fill(f, w, h, box_x + 2, y - 1, box_w - 4, 11, ui_rgb(40, 52, 110));

        pixel_t c = disabled ? dim : (is_sel ? sel_col : white);
        int lw = ui_text_width(labels[i]);
        ui_text(f, w, h, (w - lw) / 2, y, c, labels[i]);
    }

    /* Footer hint */
    const char *hint = "ARROWS:SELECT  ENTER:OK";
    int hw = ui_text_width(hint);
    ui_text(f, w, h, (w - hw) / 2, h - 20, dim, hint);
}

static void overlay_cb(pixel_t *f, int w, int h) { draw_main_menu(f, w, h); }

main_menu_result_t main_menu_run(void) {
    if (platform_headless) return MM_RESULT_NEW_GAME;

    has_saves = any_save_exists();
    sel = has_saves ? MM_CONTINUE : MM_NEW_GAME;

    video_overlay_fn prev = video_set_overlay_get();
    video_set_overlay(overlay_cb);

    main_menu_result_t result = MM_RESULT_NEW_GAME;

    while (!platform_input_quit_requested()) {
        menu_in_t in;
        menu_poll(&in);

        if (in.up) {
            do { sel = (sel + MM_COUNT - 1) % MM_COUNT; }
            while (sel == MM_CONTINUE && !has_saves);
        }
        if (in.down) {
            do { sel = (sel + 1) % MM_COUNT; }
            while (sel == MM_CONTINUE && !has_saves);
        }
        if (in.confirm) {
            switch (sel) {
            case MM_NEW_GAME:
                result = MM_RESULT_NEW_GAME;
                goto done;
            case MM_CONTINUE:
                if (has_saves) { result = MM_RESULT_CONTINUE; goto done; }
                break;
            case MM_OPTIONS:
                /* Run the existing settings menu inline */
                settings_menu_run_launcher();
                /* Restore our overlay after settings closes */
                video_set_overlay(overlay_cb);
                break;
            case MM_EXIT:
                result = MM_RESULT_EXIT;
                goto done;
            }
        }
        if (in.back) {
            /* Escape on main menu = exit */
            result = MM_RESULT_EXIT;
            goto done;
        }

        video_present_again();
        SDL_Delay(16);
    }
    result = MM_RESULT_EXIT;

done:
    video_set_overlay(prev);
    video_present_again();
    return result;
}
