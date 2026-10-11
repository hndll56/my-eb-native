#include "platform/config.h"
#include "core/types.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

eb_config_t cfg;
static char path_buf[1024];

static const char *const bind_names[BIND_COUNT] = {
    "up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "start", "select",
};
static const char *const renderer_names[] = { "vulkan", "sdl" };
static const char *const winmode_names[]  = { "windowed", "borderless", "exclusive" };
static const char *const scale_names[]    = { "integer", "fit", "stretch" };
static const char *const aspect_names[]   = { "native", "4:3", "16:9" };
static const char *const display_names[]  = { "classic", "modern" };

const char *cfg_bind_name(int b) { return (b >= 0 && b < BIND_COUNT) ? bind_names[b] : "?"; }
const char *cfg_key_name(int sc) {
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    return (n && *n) ? n : "(none)";
}
const char *cfg_pad_name(int b) {
    const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b);
    return n ? n : "(none)";
}

void cfg_set_defaults(eb_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->renderer = RENDERER_VULKAN;
    c->window_mode = WINMODE_WINDOWED;
    c->window_scale = 3;
    c->vsync = 1;
    c->gpu_ppu = 1;
    c->scale_mode = SCALE_INTEGER;
    c->aspect = ASPECT_NATIVE;
    /* Keep what a build has always shown: a widescreen build (EB_VIEWPORT_WIDTH > 256) defaults to
     * Modern, a native 256x224 build to Classic. */
    c->display_mode = (EB_VIEWPORT_WIDTH > SNES_WIDTH) ? DISPLAY_MODERN : DISPLAY_CLASSIC;
    /* Modern fills the window (Fit); Classic keeps pixel-exact integer scaling. */
    c->scale_mode = (c->display_mode == DISPLAY_MODERN) ? SCALE_FIT : SCALE_INTEGER;
    c->volume_master = c->volume_music = c->volume_sfx = 100;
    c->text_speed = 1;    /* Medium (0-indexed) */
    c->text_flavour = 0;  /* Plain (0-indexed) */
    c->ff_speed = 3;
    c->audio_dsp = 0;
    c->hq_music = 0;

    static const int keys[BIND_COUNT] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_A,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT,
    };
    static const int pads[BIND_COUNT] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
    };
    memcpy(c->key, keys, sizeof(keys));
    memcpy(c->pad, pads, sizeof(pads));
}

static int clampi(int v, int lo, int hi, bool *changed) {
    if (v < lo) { *changed = true; return lo; }
    if (v > hi) { *changed = true; return hi; }
    return v;
}

bool cfg_validate(eb_config_t *c) {
    bool ch = false;
    c->renderer     = clampi(c->renderer, 0, 1, &ch);
    c->window_mode  = clampi(c->window_mode, 0, 2, &ch);
    c->window_scale = clampi(c->window_scale, 1, CFG_MAX_SCALE, &ch);
    c->vsync        = clampi(c->vsync, 0, 1, &ch);
    c->scale_mode   = clampi(c->scale_mode, 0, 2, &ch);
    c->aspect       = clampi(c->aspect, 0, 2, &ch);
    c->display_mode = clampi(c->display_mode, 0, 1, &ch);
    /* Modern needs a frame wider than the SNES picture; without one it is just Classic. */
    if (c->display_mode == DISPLAY_MODERN && EB_VIEWPORT_WIDTH <= SNES_WIDTH && EB_VIEWPORT_HEIGHT <= SNES_HEIGHT) {
        c->display_mode = DISPLAY_CLASSIC;
        ch = true;
    }
    c->smooth       = clampi(c->smooth, 0, 1, &ch);
    c->gpu_ppu      = clampi(c->gpu_ppu, 0, 1, &ch);
    c->volume_master = clampi(c->volume_master, 0, 100, &ch);
    c->volume_music  = clampi(c->volume_music, 0, 100, &ch);
    c->volume_sfx    = clampi(c->volume_sfx, 0, 100, &ch);
    c->text_speed    = clampi(c->text_speed, 0, 2, &ch);
    c->text_flavour  = clampi(c->text_flavour, 0, 4, &ch);
    c->ff_speed      = clampi(c->ff_speed, 2, 8, &ch);
    c->audio_dsp     = clampi(c->audio_dsp, 0, 1, &ch);
    c->hq_music      = clampi(c->hq_music, 0, 1, &ch);
    if (c->fs_width < 0 || c->fs_height < 0 || c->fs_width > 16384 || c->fs_height > 16384 ||
        (c->fs_width == 0) != (c->fs_height == 0)) {
        c->fs_width = c->fs_height = c->fs_refresh = 0;
        ch = true;
    }
    eb_config_t d;
    cfg_set_defaults(&d);
    for (int i = 0; i < BIND_COUNT; i++) {
        if (c->key[i] <= SDL_SCANCODE_UNKNOWN || c->key[i] >= SDL_NUM_SCANCODES) {
            c->key[i] = d.key[i];
            ch = true;
        }
        if (c->pad[i] <= SDL_CONTROLLER_BUTTON_INVALID || c->pad[i] >= SDL_CONTROLLER_BUTTON_MAX) {
            c->pad[i] = d.pad[i];
            ch = true;
        }
    }
    /* A hand-edited file may bind one input to two actions; keep the first, reset the later ones. */
    for (int i = 0; i < BIND_COUNT; i++)
        for (int j = 0; j < i; j++) {
            if (c->key[i] == c->key[j]) { c->key[i] = d.key[i]; ch = true; }
            if (c->pad[i] == c->pad[j]) { c->pad[i] = d.pad[i]; ch = true; }
        }
    return ch;
}

int cfg_bind_assign(int *arr, int idx, int val) {
    for (int j = 0; j < BIND_COUNT; j++) {
        if (j != idx && arr[j] == val) {
            arr[j] = arr[idx];
            arr[idx] = val;
            return j;
        }
    }
    arr[idx] = val;
    return -1;
}

static int find_name(const char *const *names, int n, const char *v, int fallback) {
    for (int i = 0; i < n; i++)
        if (SDL_strcasecmp(names[i], v) == 0) return i;
    return fallback;
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
}

static void apply_kv(eb_config_t *c, const char *k, const char *v) {
    if      (!strcmp(k, "renderer"))     c->renderer = find_name(renderer_names, 2, v, c->renderer);
    else if (!strcmp(k, "window_mode"))  c->window_mode = find_name(winmode_names, 3, v, c->window_mode);
    else if (!strcmp(k, "window_scale")) c->window_scale = atoi(v);
    else if (!strcmp(k, "fs_width"))     c->fs_width = atoi(v);
    else if (!strcmp(k, "fs_height"))    c->fs_height = atoi(v);
    else if (!strcmp(k, "fs_refresh"))   c->fs_refresh = atoi(v);
    else if (!strcmp(k, "vsync"))        c->vsync = atoi(v);
    else if (!strcmp(k, "scale_mode"))   c->scale_mode = find_name(scale_names, 3, v, c->scale_mode);
    else if (!strcmp(k, "aspect"))       c->aspect = find_name(aspect_names, 3, v, c->aspect);
    else if (!strcmp(k, "display"))      c->display_mode = find_name(display_names, 2, v, c->display_mode);
    else if (!strcmp(k, "smooth"))       c->smooth = atoi(v);
    else if (!strcmp(k, "gpu_ppu"))      c->gpu_ppu = atoi(v);
    else if (!strcmp(k, "volume_master")) c->volume_master = atoi(v);
    else if (!strcmp(k, "volume_music"))  c->volume_music = atoi(v);
    else if (!strcmp(k, "volume_sfx"))    c->volume_sfx = atoi(v);
    else if (!strcmp(k, "text_speed"))    c->text_speed = atoi(v);
    else if (!strcmp(k, "text_flavour"))  c->text_flavour = atoi(v);
    else if (!strcmp(k, "ff_speed"))      c->ff_speed = atoi(v);
    else if (!strcmp(k, "audio_dsp"))     c->audio_dsp = atoi(v);
    else if (!strcmp(k, "hq_music"))      c->hq_music = atoi(v);
    else if (!strncmp(k, "key_", 4)) {
        for (int i = 0; i < BIND_COUNT; i++)
            if (!strcmp(k + 4, bind_names[i])) c->key[i] = SDL_GetScancodeFromName(v);
    } else if (!strncmp(k, "pad_", 4)) {
        for (int i = 0; i < BIND_COUNT; i++)
            if (!strcmp(k + 4, bind_names[i])) c->pad[i] = SDL_GameControllerGetButtonFromString(v);
    }
}

const char *cfg_path(void) { return path_buf; }

void cfg_init(const char *override_path) {
    cfg_set_defaults(&cfg);
    if (override_path) {
        snprintf(path_buf, sizeof(path_buf), "%s", override_path);
    } else {
        char *pref = SDL_GetPrefPath("EB-Native", "EarthBound");
        snprintf(path_buf, sizeof(path_buf), "%ssettings.ini", pref ? pref : "./");
        SDL_free(pref);
    }

    FILE *f = fopen(path_buf, "r");
    if (!f) return; /* first run: defaults */
    eb_config_t loaded = cfg;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        char *eq = strchr(line, '=');
        if (line[0] == '#' || line[0] == ';' || line[0] == '[' || !eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        trim(k);
        while (*v == ' ' || *v == '\t') v++;
        apply_kv(&loaded, k, v);
    }
    fclose(f);
    if (cfg_validate(&loaded))
        fprintf(stderr, "config: some values in %s were out of range and were reset\n", path_buf);
    cfg = loaded;
}

bool cfg_save(void) {
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path_buf);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, "# EB-Native settings\n[video]\n");
    fprintf(f, "renderer=%s\nwindow_mode=%s\nwindow_scale=%d\n", renderer_names[cfg.renderer],
            winmode_names[cfg.window_mode], cfg.window_scale);
    fprintf(f, "fs_width=%d\nfs_height=%d\nfs_refresh=%d\nvsync=%d\n", cfg.fs_width, cfg.fs_height,
            cfg.fs_refresh, cfg.vsync);
    fprintf(f, "display=%s\n", display_names[cfg.display_mode]);
    fprintf(f, "scale_mode=%s\naspect=%s\nsmooth=%d\ngpu_ppu=%d\n", scale_names[cfg.scale_mode],
            aspect_names[cfg.aspect], cfg.smooth, cfg.gpu_ppu);
    fprintf(f, "[audio]\nvolume_master=%d\nvolume_music=%d\nvolume_sfx=%d\n", cfg.volume_master,
            cfg.volume_music, cfg.volume_sfx);
    fprintf(f, "hq_music=%d\n", cfg.hq_music);
    fprintf(f, "[game]\ntext_speed=%d\ntext_flavour=%d\n", cfg.text_speed, cfg.text_flavour);
    fprintf(f, "ff_speed=%d\n", cfg.ff_speed);
    fprintf(f, "[advanced]\naudio_dsp=%d\n", cfg.audio_dsp);
    fprintf(f, "[controls]\n");
    for (int i = 0; i < BIND_COUNT; i++) fprintf(f, "key_%s=%s\n", bind_names[i], cfg_key_name(cfg.key[i]));
    for (int i = 0; i < BIND_COUNT; i++) fprintf(f, "pad_%s=%s\n", bind_names[i], cfg_pad_name(cfg.pad[i]));
    bool ok = fflush(f) == 0 && !ferror(f);
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, path_buf) != 0) {
        remove(path_buf); /* Windows cannot rename over an existing file */
        if (rename(tmp, path_buf) != 0) { remove(tmp); return false; }
    }
    return true;
}

/* Self-test of the settings file handling (no window, audio or controller needed). */
bool cfg_selftest(void) {
    bool ok = true;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "config selftest: failed: %s\n", #c); ok = false; } } while (0)
    const char *path = "cfg_selftest.ini";
    eb_config_t d;
    cfg_set_defaults(&d);
    { eb_config_t t = d; CHECK(!cfg_validate(&t)); }              /* defaults are already valid */

    /* round trip through the file */
    cfg_init(path); remove(path); cfg_set_defaults(&cfg);
    cfg.volume_music = 35; cfg.ff_speed = 5; cfg.text_flavour = 3; cfg.audio_dsp = 1;
    cfg_bind_assign(cfg.key, BIND_A, SDL_SCANCODE_E);
    CHECK(cfg_save());
    cfg_set_defaults(&cfg); cfg_init(path);
    CHECK(cfg.volume_music == 35 && cfg.ff_speed == 5 && cfg.text_flavour == 3 && cfg.audio_dsp == 1);
    CHECK(cfg.key[BIND_A] == SDL_SCANCODE_E);

    /* malformed / outdated / hostile file: garbage lines, unknown keys, out-of-range numbers, duplicate binds */
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("garbage line without equals\n[video]\nwindow_scale=9999\nvolume_master=-40\nff_speed=1000\n"
              "future_key=whatever\nkey_a=NoSuchKeyName\nkey_b=X\nkey_y=X\ntext_speed=abc\n", f);
        fclose(f);
        cfg_init(path);
        CHECK(cfg.window_scale >= 1 && cfg.window_scale <= CFG_MAX_SCALE);
        CHECK(cfg.volume_master >= 0 && cfg.volume_master <= 100);
        CHECK(cfg.ff_speed >= 2 && cfg.ff_speed <= 8);
        CHECK(cfg.key[BIND_A] == d.key[BIND_A]);                  /* unknown key name -> default */
        CHECK(cfg.key[BIND_B] != cfg.key[BIND_Y]);                /* duplicate binding repaired */
        for (int i = 0; i < BIND_COUNT; i++)
            for (int j = 0; j < i; j++) CHECK(cfg.key[i] != cfg.key[j] && cfg.pad[i] != cfg.pad[j]);
    }

    /* conflict handling: assigning a used key swaps the two actions */
    eb_config_t t = d;
    int old_a = t.key[BIND_A], old_b = t.key[BIND_B];
    CHECK(cfg_bind_assign(t.key, BIND_A, old_b) == BIND_B);
    CHECK(t.key[BIND_A] == old_b && t.key[BIND_B] == old_a);
    CHECK(cfg_bind_assign(t.key, BIND_A, SDL_SCANCODE_F) == -1 && t.key[BIND_A] == SDL_SCANCODE_F);

    /* empty file and missing file give defaults */
    f = fopen(path, "w"); if (f) fclose(f);
    cfg_init(path); CHECK(!memcmp(&cfg, &d, sizeof(d)));
    remove(path);
    cfg_init(path); CHECK(!memcmp(&cfg, &d, sizeof(d)));
    remove(path);
#undef CHECK
    return ok;
}
