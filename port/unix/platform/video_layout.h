#ifndef EB_VIDEO_LAYOUT_H
#define EB_VIDEO_LAYOUT_H

/* Where the game image lands inside a window/drawable. Pure functions, shared by the Vulkan
 * and SDL backends so both place pixels identically (and unit-testable without SDL).
 *
 * Terminology:
 *   frame    the buffer the PPU renders (EB_VIEWPORT_WIDTH x EB_VIEWPORT_HEIGHT).
 *   visible  the part of the frame that is shown: all of it in Modern mode, the centred
 *            SNES_WIDTH x SNES_HEIGHT area in Classic mode.
 *   drawable the window/swapchain in pixels. */

typedef struct { int x, y, w, h; } eb_rect_t;

/* Region of the frame that is shown for display_mode (DISPLAY_* in config.h), in frame pixels.
 * Never larger than the frame; Classic on a frame that is not larger than the SNES area is the
 * whole frame. */
eb_rect_t video_source_rect(int frame_w, int frame_h, int display_mode);

/* Modern mode: the part of the frame to show in a dw x dh drawable. Full height, centred, as wide
 * as the window's aspect needs (clamped to SNES_WIDTH..frame_w), so the world extends to the left
 * and right instead of leaving bars. */
eb_rect_t video_modern_view(int frame_w, int frame_h, int dw, int dh);

/* The aspect setting that actually applies: Modern mode always uses square pixels. */
int video_effective_aspect(int display_mode, int aspect);

/* scale_mode: SCALE_INTEGER / SCALE_FIT / SCALE_STRETCH, aspect: ASPECT_NATIVE / ASPECT_4_3 / ASPECT_16_9
 * (see config.h). src_w x src_h is the visible picture; dw x dh the drawable size in pixels. */
eb_rect_t video_layout(int dw, int dh, int src_w, int src_h, int scale_mode, int aspect);

/* Where the WHOLE frame buffer must be drawn so that its top-left vis_w x vis_h part exactly
 * covers `dst` (the rect returned by video_layout). Used by the Vulkan presenter: it draws the
 * full texture with this as the viewport and `dst` as the scissor, which crops without a shader
 * change. */
eb_rect_t video_full_rect(eb_rect_t dst, int vis_w, int vis_h, int frame_w, int frame_h);

#endif
