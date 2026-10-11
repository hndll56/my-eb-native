#ifndef EB_VK_RENDERER_H
#define EB_VK_RENDERER_H

/* Vulkan presenter for the 256x224 game frame. The game's PPU produces the frame
 * on the CPU (src/snes/ppu_render.c); this backend uploads it to a GPU texture and
 * draws it with a shader into an SDL_WINDOW_VULKAN swapchain. */

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include "core/types.h"

/* Window must have been created with SDL_WINDOW_VULKAN. On failure everything it
 * allocated is released and a reason is written to stderr. */
bool vkr_init(SDL_Window *window, int frame_w, int frame_h, bool vsync);
void vkr_shutdown(void);

/* Re-create the swapchain with a new vsync setting (takes effect immediately). */
bool vkr_set_vsync(bool vsync);

/* Upload `frame` (frame_w*frame_h pixel_t) and present its top-left vis_w x vis_h part using the
 * given layout settings (config.h SCALE_* / ASPECT_*; smooth = bilinear). vis_w/vis_h equal the
 * frame size when everything is shown. Returns false only on
 * an unrecoverable error; out-of-date swapchains are rebuilt internally. */
/* Current swapchain size in pixels; false before the swapchain exists. */
bool vkr_drawable_size(int *w, int *h);
bool vkr_present(const pixel_t *frame, int scale_mode, int aspect, bool smooth, int vis_w, int vis_h);

/* Copy the most recently presented image back to the CPU as tightly packed RGBA8.
 * Caller frees *out with free(). Used for screenshots and automated tests. */
bool vkr_screenshot(uint8_t **out, int *w, int *h);

const char *vkr_device_name(void);

/* GPU PPU: a compute shader that composes the SNES picture (BG layers, sprites, windows, color
 * math, mosaic, brightness) from the PPU state. The caller fills vkr_ppu_input() with
 * VKR_PPU_IN_WORDS 32-bit words (layout in shaders/ppu.comp, filled by gpu_ppu.c), then
 * vkr_ppu_render() writes the finished 256x224 frame. Unavailable -> use the CPU renderer. */
#define VKR_PPU_IN_WORDS 18784
#define VKR_PPU_OUT_BYTES (256 * 224 * 2)
bool vkr_ppu_init(void);
bool vkr_ppu_available(void);
uint32_t *vkr_ppu_input(void);
bool vkr_ppu_render(pixel_t *out);

#endif
