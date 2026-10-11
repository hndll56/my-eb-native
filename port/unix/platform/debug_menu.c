/* Developer tools: HUD, collision overlay, warp, flag editor, party/inventory cheats,
 * battle trigger and a log viewer. Off unless the game is started with --debug.
 *
 * Everything that changes game state is queued and executed from debug_root_boundary(),
 * the same point at which savestates are taken: no game logic is suspended mid-stack
 * there, so the change cannot tear a half-finished mode. */
#define _GNU_SOURCE
#include <unistd.h>
#include "platform/menus.h"
#include "platform/config.h"
#include "platform/menu_input.h"
#include "platform/platform.h"
#include "platform/ui.h"
#include "platform/video.h"
#include "core/mode_stack.h"
#include "game/game_state.h"
#include "game/inventory.h"
#include "game/map_loader.h"
#include "game/display_text.h"
#include "game/overworld.h"
#include "snes/ppu.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W EB_VIEWPORT_WIDTH
#define H EB_VIEWPORT_HEIGHT

static bool g_enabled;
static bool hud_on, collision_on;
static unsigned long root_frame;

/* ------------------------------------------------------------------ log capture */

#define LOG_LINES 128
#define LOG_COLS 80
static char log_ring[LOG_LINES][LOG_COLS];
static int log_head, log_count;
static char log_cur[LOG_COLS];
static int log_cur_len;
static FILE *real_stderr;

static void log_push_char(char c) {
    if (c == '\n' || log_cur_len == LOG_COLS - 1) {
        log_cur[log_cur_len] = 0;
        memcpy(log_ring[log_head], log_cur, LOG_COLS);
        log_head = (log_head + 1) % LOG_LINES;
        if (log_count < LOG_LINES) log_count++;
        log_cur_len = 0;
        if (c == '\n') return;
    }
    if (c >= 32 && c < 127) log_cur[log_cur_len++] = c;
}

#ifdef __GLIBC__
static ssize_t log_write(void *cookie, const char *buf, size_t n) {
    (void)cookie;
    for (size_t i = 0; i < n; i++) log_push_char(buf[i]);
    if (real_stderr) fwrite(buf, 1, n, real_stderr);
    return (ssize_t)n;
}

static void log_capture_start(void) {
    if (real_stderr) return;
    real_stderr = fdopen(dup(2), "w");
    if (!real_stderr) return;
    setvbuf(real_stderr, NULL, _IOLBF, 0);
    cookie_io_functions_t f = { .write = log_write };
    FILE *c = fopencookie(NULL, "w", f);
    if (c) { setvbuf(c, NULL, _IOLBF, 0); stderr = c; }
}
#else
static void log_capture_start(void) {}
#endif

void debug_set_enabled(bool on) {
    g_enabled = on;
    if (on) log_capture_start();
}
bool debug_is_enabled(void) { return g_enabled; }

/* ------------------------------------------------------------------ queued actions */

typedef enum { A_WARP, A_HEAL, A_MONEY, A_ITEM, A_BATTLE, A_FLAG, A_SHOT, A_PAD, A_TELE } act_t;
typedef struct { act_t type; int a, b; char path[256]; } action_t;
#define MAXQ 32
static action_t queue[MAXQ];
static int qn;

typedef struct { unsigned long frame; action_t a; bool done; } script_t;
static script_t scripts[16384];
static int script_n;

static void enqueue(act_t t, int a, int b) {
    if (qn < MAXQ) queue[qn++] = (action_t){ t, a, b, "" };
}

static GameMode top_mode(void) {
    return g_mode_stack.depth ? (GameMode)g_mode_stack.mode[g_mode_stack.depth - 1] : (GameMode)0;
}

static bool in_overworld(void) {
    return g_mode_stack.depth == 1 && top_mode() == GAME_MODE_OVERWORLD;
}

static bool any_mode(GameMode m) {
    for (int i = 0; i < g_mode_stack.depth; i++)
        if (g_mode_stack.mode[i] == m) return true;
    return false;
}

static void run_action(const action_t *a) {
    switch (a->type) {
    case A_WARP:
        if (!in_overworld()) { fprintf(stderr, "debug: warp only works on the overworld (no menu/text/battle)\n"); break; }
        initialize_map((uint16_t)a->a, (uint16_t)a->b, game_state.leader_direction);
        fprintf(stderr, "debug: warped to %d,%d\n", a->a, a->b);
        break;
    case A_HEAL:
        for (int i = 0; i < TOTAL_PARTY_COUNT; i++) {
            CharStruct *c = &party_characters[i];
            c->current_hp = c->current_hp_target = c->max_hp;
            c->current_pp = c->current_pp_target = c->max_pp;
        }
        fprintf(stderr, "debug: party healed\n");
        break;
    case A_MONEY:
        game_state.money_carried += (uint32_t)a->a;
        fprintf(stderr, "debug: money is now %u\n", game_state.money_carried);
        break;
    case A_ITEM: {
        uint16_t who = give_item_to_character(0xFF, (uint16_t)a->a);
        fprintf(stderr, "debug: item %d %s\n", a->a, who ? "given" : "not given (inventory full)");
        break;
    }
    case A_FLAG:
        if (a->a >= 0 && a->a < EVENT_FLAG_COUNT) {
            if (a->b) event_flag_set((uint16_t)a->a); else event_flag_clear((uint16_t)a->a);
            fprintf(stderr, "debug: event flag %d = %d\n", a->a, a->b);
        }
        break;
    case A_BATTLE:
        if (!in_overworld()) { fprintf(stderr, "debug: battle can only be started from the overworld\n"); break; }
        {
            ModeState init;
            memset(&init, 0, sizeof(init));
            init.battle_scripted.battle_group = (uint16_t)a->a;
            mode_push(GAME_MODE_BATTLE_SCRIPTED, &init);
            fprintf(stderr, "debug: battle group %d started\n", a->a);
        }
        break;
    case A_PAD:
        platform_test_set_pad((uint16_t)a->a);
        break;
    case A_TELE: {
        const TeleportDestination *d = get_teleport_dest((uint16_t)a->a);
        if (!d) { fprintf(stderr, "debug: no teleport destination %d\n", a->a); break; }
        action_t w = { A_WARP, d->x_coord << 3, d->y_coord << 3, "" };
        run_action(&w);
        break;
    }
    case A_SHOT:
        if (video_screenshot(a->path)) fprintf(stderr, "debug: screenshot %s\n", a->path);
        break;
    }
}

void debug_root_boundary(void) {
    root_frame++;
    for (int i = 0; i < script_n; i++)
        if (!scripts[i].done && root_frame >= scripts[i].frame) {
            scripts[i].done = true;
            run_action(&scripts[i].a);
        }
    for (int i = 0; i < qn; i++) run_action(&queue[i]);
    qn = 0;
}

/* "FRAME:warp X Y", "FRAME:tele DEST", "FRAME:pad HEXMASK", "FRAME:heal", "FRAME:money N", "FRAME:item N", "FRAME:battle N",
 * "FRAME:flag N 0|1", "FRAME:shot FILE" -- scripted debug actions for automated tests. */
bool debug_queue_script_command(const char *spec) {
    if (script_n >= (int)(sizeof(scripts) / sizeof(scripts[0]))) return false;
    char *end;
    unsigned long fr = strtoul(spec, &end, 10);
    if (*end != ':') return false;
    const char *cmd = end + 1;
    action_t a = { 0 };
    int x = 0, y = 0;
    if (sscanf(cmd, "warp %d %d", &x, &y) == 2) a = (action_t){ A_WARP, x, y, "" };
    else if (!strncmp(cmd, "heal", 4)) a = (action_t){ A_HEAL, 0, 0, "" };
    else if (sscanf(cmd, "money %d", &x) == 1) a = (action_t){ A_MONEY, x, 0, "" };
    else if (sscanf(cmd, "item %d", &x) == 1) a = (action_t){ A_ITEM, x, 0, "" };
    else if (sscanf(cmd, "battle %d", &x) == 1) a = (action_t){ A_BATTLE, x, 0, "" };
    else if (sscanf(cmd, "flag %d %d", &x, &y) == 2) a = (action_t){ A_FLAG, x, y, "" };
    else if (sscanf(cmd, "pad %x", &x) == 1) a = (action_t){ A_PAD, x, 0, "" };
    else if (sscanf(cmd, "tele %d", &x) == 1) a = (action_t){ A_TELE, x, 0, "" };
    else if (!strncmp(cmd, "shot ", 5)) { a.type = A_SHOT; snprintf(a.path, sizeof(a.path), "%s", cmd + 5); }
    else if (!strncmp(cmd, "hud", 3)) { hud_on = true; g_enabled = true; return true; }
    else if (!strncmp(cmd, "collision", 9)) { collision_on = true; g_enabled = true; return true; }
    else return false;
    scripts[script_n++] = (script_t){ fr, a, false };
    return true;
}

/* ------------------------------------------------------------------ overlays */

static const char *mode_name(GameMode m) {
    if (m == GAME_MODE_OVERWORLD) return "OVERWORLD";
    if (m == GAME_MODE_BATTLE) return "BATTLE";
    if (m == GAME_MODE_BATTLE_ENTRY) return "BATTLE ENTRY";
    if (m == GAME_MODE_BATTLE_SCRIPTED) return "BATTLE (SCRIPT)";
    return "OTHER";
}

static void draw_collision(pixel_t *f, int w, int h) {
    if (!any_mode(GAME_MODE_OVERWORLD) || any_mode(GAME_MODE_BATTLE) || any_mode(GAME_MODE_BATTLE_ENTRY)) return;
    int cx = ppu.bg_hofs[0], cy = ppu.bg_vofs[0];
    for (int ty = 0; ty < h; ty += 8)
        for (int tx = 0; tx < w; tx += 8) {
            uint16_t fl = get_collision_at_pixel((int16_t)(cx + tx + 4), (int16_t)(cy + ty + 4));
            if (!fl) continue;
            pixel_t c = (fl & 0x80) ? ui_rgb(255, 40, 40) : (fl & 0x0C) ? ui_rgb(60, 120, 255) : ui_rgb(255, 220, 40);
            /* Checker-tint so the artwork underneath stays readable. */
            for (int j = 0; j < 8; j += 2)
                for (int i = (j / 2) & 1; i < 8; i += 2)
                    if (tx + i < w && ty + j < h) f[(ty + j) * w + tx + i] = c;
        }
}

static void draw_hud(pixel_t *f, int w, int h) {
    uint32_t fps = platform_timer_get_fps_tenths();
    pixel_t t = ui_rgb(255, 255, 255), s = ui_rgb(0, 0, 0);
    char line[8][48];
    int n = 0;
    snprintf(line[n++], 48, "%u.%u FPS %u.%u MS", fps / 10, fps % 10, fps ? 10000 / fps / 10 : 0, fps ? (10000 / fps) % 10 : 0);
    snprintf(line[n++], 48, "MODE %s D%d", mode_name(top_mode()), g_mode_stack.depth);
    snprintf(line[n++], 48, "POS %u,%u DIR %u", game_state.leader_x_coord, game_state.leader_y_coord, game_state.leader_direction);
    snprintf(line[n++], 48, "SECTOR %u,%u", ow.last_sector_x, ow.last_sector_y);
    snprintf(line[n++], 48, "SCROLL %u,%u", ppu.bg_hofs[0], ppu.bg_vofs[0]);
    snprintf(line[n++], 48, "$%u PARTY %d", game_state.money_carried, game_state.party_count);
    snprintf(line[n++], 48, "%s", video_backend_name());
    ui_dim(f, w, h, 0, 0, 148, n * 8 + 3, 2);
    for (int i = 0; i < n; i++) {
        ui_text(f, w, h, 3, 2 + i * 8 + 1, s, line[i]);
        ui_text(f, w, h, 2, 2 + i * 8, t, line[i]);
    }
}

/* ------------------------------------------------------------------ menu */

typedef enum { D_HUD, D_COLL, D_WARP_X, D_WARP_Y, D_WARP_GO, D_FLAG_ID, D_FLAG_TOGGLE, D_HEAL, D_MONEY,
               D_ITEM_ID, D_ITEM_GO, D_BATTLE_ID, D_BATTLE_GO, D_STATE, D_LOG, D_CLOSE, D_COUNT } dsel_t;

static int dsel;
static int warp_x, warp_y, flag_id, item_id = 1, battle_id = 1;
static int dview; /* 0 menu, 1 state, 2 log */
static int log_scroll;
static bool dclose;
static char dstatus[64];

static int step_for(int dir_mod_shift, int dir_mod_ctrl) { return dir_mod_ctrl ? 256 : dir_mod_shift ? 16 : 1; }

static void draw_debug(pixel_t *f, int w, int h) {
    pixel_t tx = ui_rgb(255, 255, 255), sel = ui_rgb(255, 214, 64), dim = ui_rgb(150, 150, 150), bd = ui_rgb(60, 130, 230);
    ui_dim(f, w, h, 0, 0, w, h, 3);
    ui_fill(f, w, h, 8, 8, w - 16, h - 16, ui_rgb(24, 12, 36));
    ui_frame(f, w, h, 8, 8, w - 16, h - 16, bd);
    ui_text(f, w, h, 16, 14, sel, dview == 0 ? "DEBUG MENU" : dview == 1 ? "GAME STATE" : "LOG");
    ui_text_right(f, w, h, w - 16, 14, dim, mode_name(top_mode()));
    ui_fill(f, w, h, 12, 24, w - 24, 1, bd);

    if (dview == 1) {
        int y = 30;
        ui_textf(f, w, h, 16, y, tx, "LEADER %u,%u  DIR %u", game_state.leader_x_coord, game_state.leader_y_coord, game_state.leader_direction); y += 10;
        ui_textf(f, w, h, 16, y, tx, "WALKING STYLE %u  CHAR MODE %u", game_state.walking_style, game_state.character_mode); y += 10;
        ui_textf(f, w, h, 16, y, tx, "MONEY %u  BANK %u", game_state.money_carried, game_state.bank_balance); y += 12;
        for (int i = 0; i < 4; i++) {
            const CharStruct *c = &party_characters[i];
            ui_textf(f, w, h, 16, y, i < game_state.party_count ? tx : dim, "PC%d LV%-2u HP %3u/%-3u PP %3u/%-3u", i + 1, c->level,
                     c->current_hp, c->max_hp, c->current_pp, c->max_pp);
            y += 10;
            ui_textf(f, w, h, 16, y, dim, "  ITEMS %u %u %u %u %u %u %u", c->items[0], c->items[1], c->items[2], c->items[3], c->items[4], c->items[5], c->items[6]);
            y += 10;
        }
        ui_textf(f, w, h, 16, y + 2, tx, "FLAG SET COUNT:");
        int cnt = 0;
        for (int i = 0; i < EVENT_FLAG_COUNT; i++) cnt += event_flag_get((uint16_t)i);
        ui_textf(f, w, h, 100, y + 2, sel, "%d / %d", cnt, EVENT_FLAG_COUNT);
        ui_text(f, w, h, 16, h - 20, dim, "ESC: BACK");
        return;
    }
    if (dview == 2) {
        int rows = 17;
        if (log_scroll > log_count - rows) log_scroll = log_count - rows;
        if (log_scroll < 0) log_scroll = 0;
        for (int r = 0; r < rows && r < log_count; r++) {
            int idx = (log_head - 1 - log_scroll - (rows - 1 - r) + LOG_LINES * 2) % LOG_LINES;
            if (log_count < LOG_LINES) idx = log_count - 1 - log_scroll - (rows - 1 - r);
            if (idx < 0) continue;
            char ln[44];
            snprintf(ln, sizeof(ln), "%s", log_ring[idx]);
            ui_text(f, w, h, 14, 28 + r * 9, strstr(ln, "FATAL") || strstr(ln, "fail") ? ui_rgb(255, 120, 120) : tx, ln);
        }
        ui_textf(f, w, h, 16, h - 20, dim, "UP/DOWN: SCROLL   ESC: BACK   %d LINES", log_count);
        return;
    }

    char v[40];
    for (int i = 0; i < D_COUNT; i++) {
        const char *lab = "", *val = v;
        v[0] = 0;
        switch (i) {
        case D_HUD:   lab = "HUD (FPS, POSITION, STATE)"; val = hud_on ? "ON" : "OFF"; break;
        case D_COLL:  lab = "COLLISION OVERLAY"; val = collision_on ? "ON" : "OFF"; break;
        case D_WARP_X: lab = "WARP X"; snprintf(v, sizeof(v), "%d", warp_x); break;
        case D_WARP_Y: lab = "WARP Y"; snprintf(v, sizeof(v), "%d", warp_y); break;
        case D_WARP_GO: lab = "  WARP NOW (OVERWORLD ONLY)"; break;
        case D_FLAG_ID: lab = "EVENT FLAG"; snprintf(v, sizeof(v), "%d = %d", flag_id, event_flag_get((uint16_t)flag_id)); break;
        case D_FLAG_TOGGLE: lab = "  TOGGLE FLAG"; break;
        case D_HEAL:  lab = "HEAL PARTY"; break;
        case D_MONEY: lab = "MONEY +1000"; snprintf(v, sizeof(v), "$%u", game_state.money_carried); break;
        case D_ITEM_ID: lab = "ITEM ID"; snprintf(v, sizeof(v), "%d", item_id); break;
        case D_ITEM_GO: lab = "  GIVE ITEM TO PARTY"; break;
        case D_BATTLE_ID: lab = "BATTLE GROUP"; snprintf(v, sizeof(v), "%d", battle_id); break;
        case D_BATTLE_GO: lab = "  START BATTLE"; break;
        case D_STATE: lab = "GAME STATE..."; break;
        case D_LOG:   lab = "LOG..."; break;
        case D_CLOSE: lab = "CLOSE"; break;
        }
        int y = 29 + i * 10;
        if (i == dsel) ui_fill(f, w, h, 12, y - 1, w - 24, 9, ui_rgb(70, 36, 100));
        ui_text(f, w, h, 18, y, i == dsel ? sel : tx, lab);
        ui_text_right(f, w, h, w - 16, y, i == dsel ? sel : tx, val);
    }
    if (dstatus[0]) ui_text(f, w, h, 16, h - 38, ui_rgb(255, 120, 120), dstatus);
    ui_text(f, w, h, 16, h - 28, dim, "LEFT/RIGHT:VALUE (SHIFT=X16)");
    ui_text(f, w, h, 16, h - 19, dim, "ENTER:RUN  ESC:CLOSE");
}

static void overlay_menu(pixel_t *f, int w, int h) {
    if (collision_on) draw_collision(f, w, h);
    if (hud_on) draw_hud(f, w, h);
    draw_debug(f, w, h);
}

static void overlay_hud(pixel_t *f, int w, int h) {
    if (collision_on) draw_collision(f, w, h);
    if (hud_on) draw_hud(f, w, h);
}

static void adjust_value(int dir) {
    SDL_Keymod m = SDL_GetModState();
    int st = step_for(m & KMOD_SHIFT, m & KMOD_CTRL) * dir;
    switch (dsel) {
    case D_WARP_X: warp_x += st * 8; if (warp_x < 0) warp_x = 0; break;
    case D_WARP_Y: warp_y += st * 8; if (warp_y < 0) warp_y = 0; break;
    case D_FLAG_ID: flag_id += st; if (flag_id < 0) flag_id = 0; if (flag_id >= EVENT_FLAG_COUNT) flag_id = EVENT_FLAG_COUNT - 1; break;
    case D_ITEM_ID: item_id += st; if (item_id < 1) item_id = 1; if (item_id > 254) item_id = 254; break;
    case D_BATTLE_ID: battle_id += st; if (battle_id < 1) battle_id = 1; if (battle_id > 511) battle_id = 511; break;
    case D_HUD: case D_COLL: if (dsel == D_HUD) hud_on = !hud_on; else collision_on = !collision_on; break;
    default: break;
    }
}

static void run_selected(void) {
    dstatus[0] = 0;
    switch (dsel) {
    case D_HUD: hud_on = !hud_on; break;
    case D_COLL: collision_on = !collision_on; break;
    case D_WARP_GO:
        if (!in_overworld()) snprintf(dstatus, sizeof(dstatus), "WARP NEEDS THE OVERWORLD (CLOSE TEXT BOXES)");
        else { enqueue(A_WARP, warp_x, warp_y); dclose = true; }
        break;
    case D_FLAG_TOGGLE: enqueue(A_FLAG, flag_id, !event_flag_get((uint16_t)flag_id)); snprintf(dstatus, sizeof(dstatus), "FLAG QUEUED"); break;
    case D_HEAL: enqueue(A_HEAL, 0, 0); snprintf(dstatus, sizeof(dstatus), "PARTY HEAL QUEUED"); break;
    case D_MONEY: enqueue(A_MONEY, 1000, 0); snprintf(dstatus, sizeof(dstatus), "MONEY QUEUED"); break;
    case D_ITEM_GO: enqueue(A_ITEM, item_id, 0); snprintf(dstatus, sizeof(dstatus), "ITEM QUEUED"); break;
    case D_BATTLE_GO:
        if (!in_overworld()) snprintf(dstatus, sizeof(dstatus), "BATTLE NEEDS THE OVERWORLD");
        else { enqueue(A_BATTLE, battle_id, 0); dclose = true; }
        break;
    case D_STATE: dview = 1; break;
    case D_LOG: dview = 2; log_scroll = 0; break;
    case D_CLOSE: dclose = true; break;
    }
}

void debug_menu_open(void) {
    if (!g_enabled) {
        fprintf(stderr, "debug menu is disabled; start the game with --debug\n");
        return;
    }
    if (platform_headless) return;
    warp_x = game_state.leader_x_coord;
    warp_y = game_state.leader_y_coord;
    dview = 0; dclose = false; dstatus[0] = 0;
    video_overlay_fn prev = video_set_overlay_get();
    video_set_overlay(overlay_menu);
    while (!dclose && !platform_input_quit_requested()) {
        menu_in_t in;
        menu_poll(&in);
        if (dview == 0) {
            if (in.up) dsel = (dsel + D_COUNT - 1) % D_COUNT;
            if (in.down) dsel = (dsel + 1) % D_COUNT;
            if (in.left) adjust_value(-1);
            if (in.right) adjust_value(1);
            if (in.confirm) run_selected();
            if (in.back) dclose = true;
        } else {
            if (in.back) dview = 0;
            if (in.up) log_scroll++;
            if (in.down) log_scroll--;
            if (in.page_prev) log_scroll += 10;
            if (in.page_next) log_scroll -= 10;
        }
        video_present_again();
        SDL_Delay(16);
    }
    /* HUD / collision overlays keep running while the game plays. */
    video_set_overlay((hud_on || collision_on) ? overlay_hud : prev);
    video_present_again();
    /* If another module's overlay was active, restore it when both debug overlays are off. */
}

void debug_refresh_overlay(void) {
    if (g_enabled && (hud_on || collision_on)) video_set_overlay(overlay_hud);
}
