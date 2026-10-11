#include "platform/menus.h"
#include "platform/config.h"
#include "platform/menu_input.h"
#include "platform/platform.h"
#include "platform/ui.h"
#include "platform/video.h"
#include "game/audio.h"
#include "game/game_state.h"
#include "game_main.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define W EB_VIEWPORT_WIDTH
#define H EB_VIEWPORT_HEIGHT

typedef enum { IT_HEADER, IT_ENUM, IT_INT, IT_FSMODE, IT_ACTION, IT_BIND_KEY, IT_BIND_PAD } item_kind_t;

typedef struct {
    item_kind_t kind;
    const char *label;
    int *value;                 /* IT_ENUM / IT_INT */
    const char *const *names;   /* IT_ENUM */
    int count;                  /* IT_ENUM: number of names; IT_INT: unused */
    int lo, hi, step;           /* IT_INT */
    int arg;                    /* IT_ACTION id / IT_BIND_*: BIND_* index; IT_ENUM: ARG_NEEDS_WIDE or 0 */
} item_t;

/* IT_ENUM flag: the setting only does something on a build with a frame wider than 256x224. */
#define ARG_NEEDS_WIDE 1

enum { ACT_CLOSE, ACT_KEYS, ACT_PADS, ACT_BACK, ACT_RESET_KEYS, ACT_RESET_PADS, ACT_RESET_ALL,
       ACT_SAVE_STATE, ACT_LOAD_STATE };

static const char *const n_renderer[] = { "Vulkan", "SDL renderer" };
static const char *const n_winmode[]  = { "Windowed", "Borderless fullscreen", "Exclusive fullscreen" };
static const char *const n_scale[]    = { "Integer (pixel perfect)", "Fit window", "Stretch" };
static const char *const n_display[]  = { "Classic 4:3", "Modern widescreen" };
/* Pixel shape of the Classic picture. (16:9 stretches the 8:7 picture; Modern ignores this setting.) */
static const char *const n_aspect[]   = { "Native 8:7", "4:3 (CRT)", "16:9 (stretched)" };
static const char *const n_onoff[]    = { "Off", "On" };
static const char *const n_ppu[]      = { "CPU", "GPU (VULKAN)" };
static const char *const n_speed[]    = { "Fast", "Medium", "Slow" };
static const char *const n_dsp[]      = { "Original", "Native (experimental)" };
static const char *const n_flavour[]  = { "Plain", "Mint", "Strawberry", "Banana", "Peanut" };


#define E_(l, n, c) { .kind = IT_ENUM, .label = l, .names = n, .count = c }
#define I_(l, lo_, hi_, st) { .kind = IT_INT, .label = l, .lo = lo_, .hi = hi_, .step = st }
#define A_(l, a) { .kind = IT_ACTION, .label = l, .arg = a }
#define EW_(l, n, c) { .kind = IT_ENUM, .label = l, .names = n, .count = c, .arg = ARG_NEEDS_WIDE }
static const item_t main_items[] = {
    { .kind = IT_HEADER, .label = "GRAPHICS" },
    E_("Renderer",     n_renderer, 2),
    EW_("Display mode", n_display, 2),
    E_("Window mode",  n_winmode, 3),
    I_("Window scale", 1, CFG_MAX_SCALE, 1),
    { .kind = IT_FSMODE, .label = "Fullscreen res" },
    E_("VSync",        n_onoff, 2),
    E_("Scaling",      n_scale, 3),
    E_("Aspect ratio", n_aspect, 3),
    E_("Smoothing",    n_onoff, 2),
    E_("PPU mode", n_ppu, 2),
    { .kind = IT_HEADER, .label = "AUDIO" },
    I_("Master volume", 0, 100, 5),
    I_("Music volume",  0, 100, 5),
    I_("SFX volume",    0, 100, 5),
    E_("HQ music (--mods)", n_onoff, 2),
    { .kind = IT_HEADER, .label = "GAME" },
    E_("Text speed",    n_speed, 3),
    E_("Border theme",  n_flavour, 5),
    E_("Audio DSP (restart)", n_dsp, 2),
    { .kind = IT_HEADER, .label = "SAVE ANYWHERE" },
    A_("Save state now (F6)", ACT_SAVE_STATE),
    A_("Load last state (F7)", ACT_LOAD_STATE),
    { .kind = IT_HEADER, .label = "CONTROLS" },
    I_("Fast-forward speed", 2, 8, 1),
    A_("Keyboard...", ACT_KEYS),
    A_("Gamepad...",  ACT_PADS),
    A_("Restore all defaults", ACT_RESET_ALL),
    { .kind = IT_HEADER, .label = "" },
    A_("Save and close", ACT_CLOSE),
};
#define MAIN_COUNT ((int)(sizeof(main_items) / sizeof(main_items[0])))

/* Bindings pages are built in code (12 entries + reset + back). */
static item_t bind_items[BIND_COUNT + 3];
static int bind_count;

static item_t main_live[sizeof(main_items) / sizeof(main_items[0])];

static const item_t *items;
static int item_count;
static int sel;
static int top;
static int page; /* 0 main, 1 keys, 2 pads */
static bool waiting_bind;
static bool want_close;
static char status[64];
static bool in_game = true;   /* false when opened from the launcher: no game to save */
static int post_action;      /* ACT_SAVE_STATE / ACT_LOAD_STATE, carried out after the menu closes */

/* Exclusive-fullscreen revert protection */
static bool confirm_active;
static Uint32 confirm_deadline;
static eb_config_t confirm_saved;

static void bind_pointers(void) {
    memcpy(main_live, main_items, sizeof(main_live));
    /* Look items up by label so inserting a row cannot silently bind the wrong setting. */
    static const struct { const char *label; int *value; } bindings[] = {
        { "Renderer", &cfg.renderer },       { "Display mode", &cfg.display_mode },
        { "Window mode", &cfg.window_mode }, { "Window scale", &cfg.window_scale },
        { "VSync", &cfg.vsync },             { "Scaling", &cfg.scale_mode },
        { "Aspect ratio", &cfg.aspect },     { "Smoothing", &cfg.smooth },
        { "PPU mode", &cfg.gpu_ppu },        { "Master volume", &cfg.volume_master },
        { "Music volume", &cfg.volume_music },{ "SFX volume", &cfg.volume_sfx },
        { "Text speed", &cfg.text_speed },   { "Border theme", &cfg.text_flavour },
        { "Fast-forward speed", &cfg.ff_speed }, { "Audio DSP (restart)", &cfg.audio_dsp }, { "HQ music (--mods)", &cfg.hq_music },
    };
    for (int i = 0; i < MAIN_COUNT; i++)
        for (size_t b = 0; b < sizeof(bindings) / sizeof(bindings[0]); b++)
            if (main_live[i].kind != IT_HEADER && !strcmp(main_live[i].label, bindings[b].label))
                main_live[i].value = bindings[b].value;
}

static void build_bind_page(bool pads) {
    bind_count = 0;
    for (int i = 0; i < BIND_COUNT; i++)
        bind_items[bind_count++] = (item_t){ .kind = pads ? IT_BIND_PAD : IT_BIND_KEY, .label = cfg_bind_name(i), .arg = i };
    bind_items[bind_count++] = (item_t){ .kind = IT_ACTION, .label = "Reset to defaults", .arg = pads ? ACT_RESET_PADS : ACT_RESET_KEYS };
    bind_items[bind_count++] = (item_t){ .kind = IT_ACTION, .label = "Back", .arg = ACT_BACK };
}

static void goto_page(int p) {
    page = p;
    sel = 0; top = 0;
    if (p == 0) { items = main_live; item_count = MAIN_COUNT; }
    else { build_bind_page(p == 2); items = bind_items; item_count = bind_count; }
    while (sel < item_count && items[sel].kind == IT_HEADER) sel++;
}

void settings_apply_audio(void) {
    audio_set_volumes(cfg.volume_master, cfg.volume_music, cfg.volume_sfx);
    audio_set_music_pack(eb_mods_dir, cfg.hq_music != 0);
}

void settings_apply_game(void) {
    game_state.text_speed = (uint8_t)(cfg.text_speed + 1);
    game_state.text_flavour = (uint8_t)(cfg.text_flavour + 1);
    eb_fast_forward_multiplier = cfg.ff_speed;
}

static void apply_all(void) {
    cfg_validate(&cfg);
    settings_apply_audio();
    settings_apply_game();
    video_apply_config();
}

static bool needs_confirm(const eb_config_t *before) {
    return cfg.window_mode == WINMODE_EXCLUSIVE &&
           (before->window_mode != WINMODE_EXCLUSIVE || before->fs_width != cfg.fs_width ||
            before->fs_height != cfg.fs_height || before->fs_refresh != cfg.fs_refresh);
}

static void after_change(const eb_config_t *before) {
    apply_all();
    if (needs_confirm(before)) {
        confirm_saved = *before;
        confirm_active = true;
        confirm_deadline = SDL_GetTicks() + 10000;
    }
}

static void fsmode_step(int dir) {
    /* Cycle: desktop mode, then every distinct WxH@Hz the display offers (>= native size). */
    int n = video_mode_count();
    int cur = -1;
    for (int i = 0; i < n; i++) {
        int w, h, hz;
        if (video_mode_get(i, &w, &h, &hz) && w == cfg.fs_width && h == cfg.fs_height && hz == cfg.fs_refresh) cur = i;
    }
    int next = cur + dir; /* -1 means "desktop" */
    if (cfg.fs_width == 0) next = dir > 0 ? 0 : n - 1;
    if (next < 0 || next >= n) { cfg.fs_width = cfg.fs_height = cfg.fs_refresh = 0; return; }
    int w, h, hz;
    if (video_mode_get(next, &w, &h, &hz)) { cfg.fs_width = w; cfg.fs_height = h; cfg.fs_refresh = hz; }
}

static void adjust(const item_t *it, int dir) {
    eb_config_t before = cfg;
    switch (it->kind) {
    case IT_ENUM: {
        if ((it->arg & ARG_NEEDS_WIDE) && !video_wide_available()) {
            snprintf(status, sizeof(status), "NEEDS A BUILD WITH EB_VIEWPORT_WIDTH > 256");
            return;
        }
        int v = *it->value + dir;
        if (v < 0) v = it->count - 1;
        if (v >= it->count) v = 0;
        *it->value = v;
        break;
    }
    case IT_INT: {
        int v = *it->value + dir * it->step;
        if (v < it->lo) v = it->lo;
        if (v > it->hi) v = it->hi;
        *it->value = v;
        break;
    }
    case IT_FSMODE:
        fsmode_step(dir);
        break;
    default:
        return;
    }
    status[0] = 0;
    after_change(&before);
}

static void activate(const item_t *it) {
    if (it->kind == IT_ENUM || it->kind == IT_INT || it->kind == IT_FSMODE) {
        if (it->kind == IT_ENUM && it->count == 2) adjust(it, 1);
        return;
    }
    if (it->kind == IT_BIND_KEY || it->kind == IT_BIND_PAD) { waiting_bind = true; return; }
    switch (it->arg) {
    case ACT_CLOSE: want_close = true; break;
    case ACT_KEYS:  goto_page(1); break;
    case ACT_PADS:  goto_page(2); break;
    case ACT_BACK:
        goto_page(0);
        for (int i = 0; i < item_count; i++)
            if (items[i].kind == IT_ACTION && items[i].arg == ACT_KEYS) { sel = i; break; }
        break;
    case ACT_SAVE_STATE:
    case ACT_LOAD_STATE:
        if (!in_game) { snprintf(status, sizeof(status), "ONLY AVAILABLE DURING THE GAME"); break; }
        post_action = it->arg;
        want_close = true; /* the request runs once the menu is closed and the game is running again */
        break;
    case ACT_RESET_KEYS: case ACT_RESET_PADS: {
        eb_config_t d;
        cfg_set_defaults(&d);
        if (it->arg == ACT_RESET_KEYS) memcpy(cfg.key, d.key, sizeof(cfg.key));
        else memcpy(cfg.pad, d.pad, sizeof(cfg.pad));
        snprintf(status, sizeof(status), "Controls reset");
        break;
    }
    case ACT_RESET_ALL: {
        eb_config_t before = cfg;
        cfg_set_defaults(&cfg);
        snprintf(status, sizeof(status), "All settings restored to defaults");
        after_change(&before);
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ drawing */

static const pixel_t *col(void) {
    static pixel_t c[6];
    static bool init;
    if (!init) {
        c[0] = ui_rgb(255, 255, 255); /* text */
        c[1] = ui_rgb(255, 214, 64);  /* selected */
        c[2] = ui_rgb(120, 190, 255); /* header */
        c[3] = ui_rgb(150, 150, 150); /* dim */
        c[4] = ui_rgb(60, 130, 230);  /* border */
        c[5] = ui_rgb(255, 120, 120); /* warning */
        init = true;
    }
    return c;
}

static void value_text(const item_t *it, char *out, size_t n) {
    switch (it->kind) {
    case IT_ENUM:
        if ((it->arg & ARG_NEEDS_WIDE) && !video_wide_available()) snprintf(out, n, "Classic only (N/A)");
        else snprintf(out, n, "< %s >", it->names[*it->value]);
        break;
    case IT_INT:
        if (it->hi == 100) {
            int bars = *it->value / 10;
            char b[11];
            for (int i = 0; i < 10; i++) b[i] = i < bars ? '#' : '-';
            b[10] = 0;
            snprintf(out, n, "%s %3d", b, *it->value);
        } else if (!strcmp(it->label, "Fast-forward speed")) {
            snprintf(out, n, "< %dx >", *it->value);
        } else {
            snprintf(out, n, "< %dx  (%dx%d) >", *it->value, video_view_width() * *it->value,
                     video_view_height() * *it->value);
        }
        break;
    case IT_FSMODE:
        if (cfg.fs_width == 0) snprintf(out, n, "< Desktop >");
        else snprintf(out, n, "< %dx%d @%dHz >", cfg.fs_width, cfg.fs_height, cfg.fs_refresh);
        break;
    case IT_BIND_KEY:
        snprintf(out, n, "%s", cfg_key_name(cfg.key[it->arg]));
        break;
    case IT_BIND_PAD:
        snprintf(out, n, "%s", cfg_pad_name(cfg.pad[it->arg]));
        break;
    default:
        out[0] = 0;
    }
}

static void draw_menu(pixel_t *f, int w, int h) {
    const pixel_t *c = col();
    ui_dim(f, w, h, 0, 0, w, h, 3);
    ui_fill(f, w, h, 8, 8, w - 16, h - 16, ui_rgb(16, 20, 48));
    ui_frame(f, w, h, 8, 8, w - 16, h - 16, c[4]);
    const char *title = page == 0 ? "SETTINGS" : page == 1 ? "KEYBOARD CONTROLS" : "GAMEPAD CONTROLS";
    ui_text(f, w, h, 16, 14, c[1], title);
    ui_text_right(f, w, h, w - 16, 14, c[3], video_backend_name()[0] == 'V' ? "VULKAN" : "SDL");
    ui_fill(f, w, h, 12, 24, w - 24, 1, c[4]);

    const int rows = 15, row_h = 10, y0 = 29;
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    for (int r = 0; r < rows && top + r < item_count; r++) {
        const item_t *it = &items[top + r];
        int y = y0 + r * row_h;
        bool on = (top + r == sel);
        if (it->kind == IT_HEADER) {
            ui_text(f, w, h, 16, y, c[2], it->label);
            continue;
        }
        if (on) ui_fill(f, w, h, 12, y - 1, w - 24, row_h - 1, ui_rgb(40, 52, 110));
        bool disabled = !in_game && it->kind == IT_ACTION &&
                        (it->arg == ACT_SAVE_STATE || it->arg == ACT_LOAD_STATE);
        pixel_t tc = disabled ? c[3] : (on ? c[1] : c[0]);
        ui_text(f, w, h, 20, y, tc, it->label);
        char v[64];
        value_text(it, v, sizeof(v));
        if (on && waiting_bind) snprintf(v, sizeof(v), "press a %s...", page == 1 ? "key" : "button");
        ui_text_right(f, w, h, w - 16, y, tc, v);
    }
    if (top > 0) ui_text(f, w, h, w / 2 - 3, 25, c[3], "^");
    if (top + rows < item_count) ui_text(f, w, h, w / 2 - 3, y0 + rows * row_h - 2, c[3], "v");

    ui_fill(f, w, h, 12, h - 34, w - 24, 1, c[4]);
    if (status[0]) ui_text(f, w, h, 16, h - 30, c[5], status);
    ui_text(f, w, h, 16, h - 20, c[3], page == 0 ? "ARROWS:CHANGE ENTER:OK ESC:SAVE+EXIT"
                                                 : "ENTER:REBIND  ESC:BACK");

    if (confirm_active) {
        int left = (int)((confirm_deadline - SDL_GetTicks() + 999) / 1000);
        ui_fill(f, w, h, 24, 80, w - 48, 52, ui_rgb(60, 10, 10));
        ui_frame(f, w, h, 24, 80, w - 48, 52, c[5]);
        ui_text(f, w, h, 32, 88, c[0], "KEEP THIS DISPLAY MODE?");
        ui_textf(f, w, h, 32, 102, c[1], "REVERTING IN %d S", left < 0 ? 0 : left);
        ui_text(f, w, h, 32, 116, c[3], "ENTER: KEEP    ANY OTHER KEY: REVERT");
    }
}

static void overlay_cb(pixel_t *f, int w, int h) { draw_menu(f, w, h); }

/* ------------------------------------------------------------------ main loop */

static void next_selectable(int dir) {
    int n = sel;
    do {
        n += dir;
        if (n < 0) n = item_count - 1;
        if (n >= item_count) n = 0;
    } while (items[n].kind == IT_HEADER);
    sel = n;
}

void settings_menu_run(void) {
    if (platform_headless) return;
    bind_pointers();
    want_close = false;
    waiting_bind = false;
    confirm_active = false;
    status[0] = 0;
    post_action = 0;
    goto_page(0);

    eb_config_t at_open = cfg;
    video_overlay_fn prev = video_set_overlay_get();
    video_set_overlay(overlay_cb);
    SDL_StartTextInput();

    while (!want_close && !platform_input_quit_requested()) {
        menu_in_t in;
        menu_poll(&in);

        if (confirm_active) {
            if ((Sint32)(SDL_GetTicks() - confirm_deadline) >= 0 || in.key || in.pad_button >= 0) {
                if (in.confirm && !((Sint32)(SDL_GetTicks() - confirm_deadline) >= 0)) {
                    confirm_active = false;
                } else {
                    cfg = confirm_saved;
                    apply_all();
                    confirm_active = false;
                    snprintf(status, sizeof(status), "Display mode reverted");
                }
            }
        } else if (waiting_bind) {
            if (in.back && page == 1) {
                waiting_bind = false;
            } else if (page == 1 && in.key) {
                int o = cfg_bind_assign(cfg.key, items[sel].arg, in.key);
                if (o >= 0) snprintf(status, sizeof(status), "SWAPPED WITH %s", cfg_bind_name(o));
                else status[0] = 0;
                waiting_bind = false;
            } else if (page == 2 && in.pad_button >= 0) {
                int o = cfg_bind_assign(cfg.pad, items[sel].arg, in.pad_button);
                if (o >= 0) snprintf(status, sizeof(status), "SWAPPED WITH %s", cfg_bind_name(o));
                else status[0] = 0;
                waiting_bind = false;
            } else if (page == 2 && in.back) {
                waiting_bind = false;
            }
        } else {
            int page_was = page;
            if (in.up) next_selectable(-1);
            if (in.down) next_selectable(1);
            if (in.left) adjust(&items[sel], -1);
            if (in.right) adjust(&items[sel], 1);
            if (in.confirm) activate(&items[sel]);
            if (in.back) {
                if (page != 0) {
                    goto_page(0);
                    for (int i = 0; i < item_count; i++)
                        if (items[i].kind == IT_ACTION && items[i].arg == (page_was == 2 ? ACT_PADS : ACT_KEYS)) { sel = i; break; }
                }
                else want_close = true;
            }
        }

        video_present_again();
        SDL_Delay(16);
    }

    SDL_StopTextInput();
    video_set_overlay(prev);
    cfg_validate(&cfg);
    if (memcmp(&at_open, &cfg, sizeof(cfg)) != 0) {
        if (!cfg_save()) fprintf(stderr, "settings: could not write %s\n", cfg_path());
    }
    video_present_again();

    if (post_action == ACT_SAVE_STATE) savestate_request(false);
    else if (post_action == ACT_LOAD_STATE) savestate_request(true);
    post_action = 0;
}

void settings_menu_run_launcher(void) {
    in_game = false;
    settings_menu_run();
    in_game = true;
}
