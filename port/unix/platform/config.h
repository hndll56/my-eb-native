#ifndef EB_CONFIG_H
#define EB_CONFIG_H

/* Persistent user settings for the desktop port (settings.ini). */

#include <stdbool.h>
#include <stdint.h>

enum { RENDERER_VULKAN = 0, RENDERER_SDL = 1 };
enum { WINMODE_WINDOWED = 0, WINMODE_BORDERLESS = 1, WINMODE_EXCLUSIVE = 2 };
enum { SCALE_INTEGER = 0, SCALE_FIT = 1, SCALE_STRETCH = 2 };
enum { ASPECT_NATIVE = 0, ASPECT_4_3 = 1, ASPECT_16_9 = 2 };
/* Classic: the original 256x224 picture (centre crop of the rendered frame).
 * Modern: the whole rendered frame; only wider than Classic when the game library was built
 * with EB_VIEWPORT_WIDTH > 256 (the desktop port defaults to 400). */
enum { DISPLAY_CLASSIC = 0, DISPLAY_MODERN = 1 };

/* Game buttons that can be rebound (index into cfg.key / cfg.pad). */
enum {
    BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT,
    BIND_A, BIND_B, BIND_X, BIND_Y, BIND_L, BIND_R, BIND_START, BIND_SELECT,
    BIND_COUNT
};

typedef struct {
    int renderer;     /* RENDERER_* */
    int window_mode;  /* WINMODE_* */
    int window_scale; /* windowed size = native * scale (1..CFG_MAX_SCALE) */
    int fs_width;     /* exclusive fullscreen mode; 0 = desktop mode */
    int fs_height;
    int fs_refresh;
    int vsync;        /* 0/1 */
    int scale_mode;   /* SCALE_* */
    int aspect;       /* ASPECT_* (pixel shape of the Classic picture; Modern always uses square pixels) */
    int display_mode; /* DISPLAY_* */
    int smooth;       /* 0 = nearest (pixel art), 1 = bilinear */
    int gpu_ppu;      /* 1 = compose the SNES picture on the GPU (Vulkan compute), 0 = CPU */
    int volume_master, volume_music, volume_sfx; /* 0..100 */
    int text_speed;   /* 0=Fast 1=Medium 2=Slow (game_state uses +1) */
    int text_flavour; /* 0..4 window border theme (game_state uses +1) */
    int ff_speed;     /* fast-forward multiple while Tab / the fast-forward button is held, 2..8 */
    int hq_music;     /* 1 = play music/NNN.wav from the --mods directory when present */
    int audio_dsp;    /* 0 = original (lakesnes DSP), 1 = native S-DSP; read at start-up */
    int key[BIND_COUNT];  /* SDL_Scancode */
    int pad[BIND_COUNT];  /* SDL_GameControllerButton */
} eb_config_t;

#define CFG_MAX_SCALE 8

extern eb_config_t cfg;

bool cfg_selftest(void); /* file round trip, malformed input, binding conflicts */
void cfg_set_defaults(eb_config_t *c);
/* Clamp every field to a supported value. Returns true if something changed. */
bool cfg_validate(eb_config_t *c);
/* Path of the settings file (never NULL after cfg_init). */
const char *cfg_path(void);
/* Choose the settings path (NULL = per-user pref dir) and load it. A missing or
 * damaged file leaves safe defaults in place. */
void cfg_init(const char *override_path);
bool cfg_save(void);

/* Names for the menus / ini. */
/* Assign `val` to binding `idx` of `arr` (cfg.key or cfg.pad). If another action already uses `val`
 * the two actions swap, so no two actions ever share an input. Returns the index of the action that
 * received the old value, or -1 if there was no conflict. */
int cfg_bind_assign(int *arr, int idx, int val);

const char *cfg_bind_name(int bind);
const char *cfg_key_name(int scancode);
const char *cfg_pad_name(int button);

#endif
