#ifndef EB_UI_H
#define EB_UI_H

/* Tiny immediate-mode drawing helpers for the host menus (settings, debug).
 * They draw straight into the 256x224 game frame copy that the video layer hands to the
 * overlay callback, so every presenter (Vulkan or SDL) shows them identically. */

#include <stdarg.h>
#include "core/types.h"

#define UI_CHAR_W 6
#define UI_CHAR_H 8

pixel_t ui_rgb(int r, int g, int b);

void ui_fill(pixel_t *f, int w, int h, int x, int y, int rw, int rh, pixel_t c);
/* Alpha-blended dark rectangle: shade is 0..4 (quarters of the original brightness removed). */
void ui_dim(pixel_t *f, int w, int h, int x, int y, int rw, int rh, int shade);
void ui_frame(pixel_t *f, int w, int h, int x, int y, int rw, int rh, pixel_t c);
void ui_text(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *s);
void ui_textf(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *fmt, ...)
    __attribute__((format(printf, 7, 8)));
int ui_text_width(const char *s);
/* Right-aligned text ending at x. */
void ui_text_right(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *s);

#endif
