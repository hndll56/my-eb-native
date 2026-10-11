#include "platform/ui.h"
#include "platform/ui_font8.h"
#include <stdio.h>
#include <string.h>

pixel_t ui_rgb(int r, int g, int b) {
    return PIXEL_RGB(r, g, b);
}

void ui_fill(pixel_t *f, int w, int h, int x, int y, int rw, int rh, pixel_t c) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    for (int j = 0; j < rh; j++)
        for (int i = 0; i < rw; i++) f[(y + j) * w + x + i] = c;
}

void ui_dim(pixel_t *f, int w, int h, int x, int y, int rw, int rh, int shade) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (shade < 0) shade = 0;
    if (shade > 4) shade = 4;
    for (int j = 0; j < rh; j++)
        for (int i = 0; i < rw; i++) {
            pixel_t *p = &f[(y + j) * w + x + i];
            /* Layout-independent: the green field is 6 bits at [10:5], the others 5 bits. */
            unsigned v = *p;
            unsigned lo = v & 0x1F, g = (v >> 5) & 0x3F, hi = (v >> 11) & 0x1F;
            lo = lo * (4 - shade) / 4; g = g * (4 - shade) / 4; hi = hi * (4 - shade) / 4;
            *p = (pixel_t)(lo | (g << 5) | (hi << 11));
        }
}

void ui_frame(pixel_t *f, int w, int h, int x, int y, int rw, int rh, pixel_t c) {
    ui_fill(f, w, h, x, y, rw, 1, c);
    ui_fill(f, w, h, x, y + rh - 1, rw, 1, c);
    ui_fill(f, w, h, x, y, 1, rh, c);
    ui_fill(f, w, h, x + rw - 1, y, 1, rh, c);
}

int ui_text_width(const char *s) { return (int)strlen(s) * UI_CHAR_W; }

void ui_text(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *s) {
    for (; *s; s++, x += UI_CHAR_W) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        if (x + UI_CHAR_W < 0 || x >= w) continue;
        const uint8_t *g = ui_font8[ch - 32];
        for (int j = 0; j < 8; j++) {
            int py = y + j;
            if (py < 0 || py >= h) continue;
            for (int i = 0; i < 8; i++) {
                int px = x + i;
                if (px < 0 || px >= w) continue;
                if (g[j] & (0x80 >> i)) f[py * w + px] = c;
            }
        }
    }
}

void ui_textf(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_text(f, w, h, x, y, c, buf);
}

void ui_text_right(pixel_t *f, int w, int h, int x, int y, pixel_t c, const char *s) {
    ui_text(f, w, h, x - ui_text_width(s), y, c, s);
}
