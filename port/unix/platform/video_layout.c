#include "platform/video_layout.h"
#include "platform/config.h"
#include "core/types.h"

eb_rect_t video_source_rect(int frame_w, int frame_h, int display_mode) {
    eb_rect_t r = { 0, 0, frame_w, frame_h };
    if (display_mode != DISPLAY_CLASSIC) return r;
    if (frame_w > SNES_WIDTH)  { r.w = SNES_WIDTH;  r.x = (frame_w - SNES_WIDTH) / 2; }
    if (frame_h > SNES_HEIGHT) { r.h = SNES_HEIGHT; r.y = (frame_h - SNES_HEIGHT) / 2; }
    return r;
}

eb_rect_t video_modern_view(int frame_w, int frame_h, int dw, int dh) {
    eb_rect_t r = { 0, 0, frame_w, frame_h };
    if (dw <= 0 || dh <= 0 || frame_h <= 0) return r;
    /* Full height at the window's aspect: the width that makes square pixels fill the window. */
    long w = ((long)dw * frame_h + dh / 2) / dh;
    if (w < SNES_WIDTH) w = SNES_WIDTH;   /* never narrower than the original picture */
    if (w > frame_w) w = frame_w;         /* wider windows than the frame is rendered for get side bars */
    r.w = (int)w;
    r.x = (frame_w - r.w) / 2;
    return r;
}

int video_effective_aspect(int display_mode, int aspect) {
    return display_mode == DISPLAY_MODERN ? ASPECT_NATIVE : aspect;
}

eb_rect_t video_layout(int dw, int dh, int src_w, int src_h, int scale_mode, int aspect) {
    eb_rect_t r = { 0, 0, dw, dh };
    if (dw <= 0 || dh <= 0 || src_w <= 0 || src_h <= 0) return r;
    if (scale_mode == SCALE_STRETCH) return r;

    /* Display aspect of the image as a fraction aw:ah (native pixels are square;
     * ASPECT_4_3 stretches the 8:7 SNES frame to the 4:3 a CRT showed). */
    long aw = src_w, ah = src_h;
    if (aspect == ASPECT_4_3) { aw = 4; ah = 3; }
    else if (aspect == ASPECT_16_9) { aw = 16; ah = 9; }

    /* Largest size with that aspect that fits the drawable. */
    long fw = dw, fh = (long)dw * ah / aw;
    if (fh > dh) { fh = dh; fw = (long)dh * aw / ah; }

    if (scale_mode == SCALE_FIT) {
        /* Rounding the aspect to whole pixels can leave a sliver (under one source pixel) at an edge.
         * Close it so a window that matches the picture's aspect is covered with no bar at all; the
         * resulting stretch is below one source pixel across the whole image. */
        if ((dw - fw) * src_w < fw) fw = dw;
        if ((dh - fh) * src_h < fh) fh = dh;
    }

    if (scale_mode == SCALE_INTEGER) {
        /* Integer multiple of the source height (keeps scanlines even); width follows aspect. */
        long k = fh / src_h;
        if (k >= 1) {
            fh = k * src_h;
            fw = fh * aw / ah;
        } /* else: smaller than 1x, fall back to fit */
    }
    r.w = (int)fw;
    r.h = (int)fh;
    r.x = (dw - r.w) / 2;
    r.y = (dh - r.h) / 2;
    return r;
}

eb_rect_t video_full_rect(eb_rect_t dst, int vis_w, int vis_h, int frame_w, int frame_h) {
    eb_rect_t f = dst;
    if (vis_w <= 0 || vis_h <= 0) return f;
    f.w = (int)((long)dst.w * frame_w / vis_w);
    f.h = (int)((long)dst.h * frame_h / vis_h);
    return f;
}
