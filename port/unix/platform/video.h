#ifndef EB_VIDEO_H
#define EB_VIDEO_H

/* Desktop video layer: owns the window, the CPU-side game frame, and the presenter
 * (Vulkan, or the SDL renderer as fallback). Used by sdl2_video.c, the settings
 * menu and the debug overlay. */

#include <SDL.h>
#include <stdbool.h>
#include "core/types.h"

/* Called on a copy of the finished frame just before it is presented. The menus use it
 * to draw on top without touching the game's own frame buffer. */
typedef void (*video_overlay_fn)(pixel_t *frame, int w, int h);
void video_set_overlay(video_overlay_fn fn);
video_overlay_fn video_set_overlay_get(void);

/* Re-apply cfg (window mode, size, vsync, scaling, renderer) to the live window.
 * Anything the system cannot do is reverted to a safe value in cfg. */
void video_apply_config(void);

/* Present the last game frame again (with the overlay). Used while a menu is open and
 * the game is not producing frames. */
void video_present_again(void);

const char *video_backend_name(void); /* "Vulkan (<gpu>)" / "SDL renderer" / "none" */
bool video_is_vulkan(void);

/* Display modes of the window's display, for the exclusive-fullscreen picker. */
int video_mode_count(void);
bool video_mode_get(int index, int *w, int *h, int *hz);

/* Size of the picture that is actually shown (depends on cfg.display_mode): the whole
 * frame in Modern mode, 256x224 in Classic. Use these for window sizes, not EB_VIEWPORT_*. */
int video_view_width(void);
int video_view_height(void);
/* True when the game was built with a frame wider than the SNES 256x224, i.e. Modern
 * widescreen can show more than Classic. */
bool video_wide_available(void);

/* Save what is on screen (final window image when possible) as a BMP. */
bool video_screenshot(const char *path);


/* Save a screenshot when the Nth presented frame is reached (used by automated tests;
 * works headless because the CPU-side frame is always kept). */
void video_schedule_screenshot(int frame_number, const char *path);

/* Window the game is shown in (NULL when headless). */
SDL_Window *video_window(void);

#endif
