/* Save-anywhere front end: queues a savestate save/load through the game's crash-safe
 * host_request_capture()/host_request_load() and tells the player how it went.
 *
 * The capture itself is done by the game loop (src/game_main.c: host_root_boundary) and by
 * src/core/state_dump.c; nothing here touches game data. This file only (1) makes the request,
 * (2) watches host_capture_status(), and (3) draws a short on-screen message. */
#include "platform/menus.h"
#include "platform/platform.h"
#include "platform/ui.h"
#include "platform/video.h"
#include "game_main.h"
#include <stdio.h>
#include <string.h>

enum { KIND_NONE, KIND_SAVE, KIND_LOAD };
#define TOAST_FRAMES 150 /* ~2.5 s at 60 fps */

static int kind;             /* what the pending request is */
static bool was_pending;     /* a request was in flight at the previous poll */
static int toast_left;
static bool toast_failed;
static char toast_msg[64];
static uint16_t prev_aux;
static video_overlay_fn toast_prev; /* overlay that was active under the toast (normally none) */
static bool toast_installed;

static void toast_overlay(pixel_t *f, int w, int h) {
    if (toast_prev) toast_prev(f, w, h);
    int tw = ui_text_width(toast_msg);
    int bw = tw + 16, bh = 16;
    int bx = (w - bw) / 2, by = h - bh - 12;
    ui_fill(f, w, h, bx, by, bw, bh, toast_failed ? ui_rgb(70, 12, 12) : ui_rgb(16, 20, 48));
    ui_frame(f, w, h, bx, by, bw, bh, toast_failed ? ui_rgb(255, 120, 120) : ui_rgb(60, 130, 230));
    ui_text(f, w, h, bx + 8, by + 4, ui_rgb(255, 255, 255), toast_msg);
}

static void show_toast(bool failed, const char *msg) {
    toast_failed = failed;
    snprintf(toast_msg, sizeof(toast_msg), "%s", msg);
    toast_left = TOAST_FRAMES;
    if (!toast_installed && video_set_overlay_get() == NULL) {
        toast_prev = NULL;
        video_set_overlay(toast_overlay);
        toast_installed = true;
    }
}

void savestate_request(bool load) {
    if (host_capture_status() == HOST_CAPTURE_PENDING) return; /* one at a time */
    kind = load ? KIND_LOAD : KIND_SAVE;
    if (load) host_request_load();
    else host_request_capture();
}

void savestate_toast_poll(void) {
    /* Hotkeys are handled inside the game; just remember which one started a request. */
    uint16_t aux = platform_input_get_aux();
    uint16_t pressed = aux & (uint16_t)~prev_aux;
    prev_aux = aux;
    if (pressed & AUX_SAVESTATE) kind = KIND_SAVE;
    if (pressed & AUX_LOAD_STATE) kind = KIND_LOAD;

    HostCaptureStatus st = host_capture_status();
    if (was_pending && st != HOST_CAPTURE_PENDING && kind != KIND_NONE) {
        bool ok = (st == HOST_CAPTURE_COMMITTED);
        if (kind == KIND_SAVE) show_toast(!ok, ok ? "STATE SAVED" : "SAVE FAILED - SEE LOG");
        else show_toast(!ok, ok ? "STATE LOADED" : "LOAD FAILED - NO STATE FOUND");
        kind = KIND_NONE;
    }
    was_pending = (st == HOST_CAPTURE_PENDING);

    if (toast_left > 0 && --toast_left == 0 && toast_installed) {
        if (video_set_overlay_get() == toast_overlay) video_set_overlay(NULL);
        toast_installed = false;
        video_present_again();
    }
}
