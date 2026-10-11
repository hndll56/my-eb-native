/* Unit tests for the pure viewport helpers in platform/video_layout.c (no SDL needed).
 * Build:  cc -I../../src -I. tests/test_video_layout.c platform/video_layout.c -o test_video_layout
 * or enable the CMake option EB_BUILD_TESTS and run ctest. */
#include <stdio.h>
#include <stdlib.h>
#include "platform/config.h"
#include "platform/video_layout.h"
#include "core/types.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_source_rect(void) {
    /* Wide frame (400x224): Classic is the centred 256x224 area, Modern is everything. */
    eb_rect_t c = video_source_rect(400, 224, DISPLAY_CLASSIC);
    CHECK(c.x == 72 && c.y == 0 && c.w == 256 && c.h == 224);
    CHECK(c.x == (400 - SNES_WIDTH) / 2); /* matches EB_VIEWPORT_PAD_LEFT for 400 */
    eb_rect_t m = video_source_rect(400, 224, DISPLAY_MODERN);
    CHECK(m.x == 0 && m.y == 0 && m.w == 400 && m.h == 224);
    /* Native frame: both modes are the whole frame. */
    eb_rect_t n1 = video_source_rect(256, 224, DISPLAY_CLASSIC);
    eb_rect_t n2 = video_source_rect(256, 224, DISPLAY_MODERN);
    CHECK(n1.x == 0 && n1.w == 256 && n1.h == 224 && n2.w == 256);
    /* Taller frame is cropped vertically too; odd padding rounds down and stays inside. */
    eb_rect_t t = video_source_rect(401, 240, DISPLAY_CLASSIC);
    CHECK(t.x == 72 && t.y == 8 && t.w == 256 && t.h == 224);
    CHECK(t.x + t.w <= 401 && t.y + t.h <= 240);
    /* Frame smaller than the SNES area is never "cropped" past its size. */
    eb_rect_t s = video_source_rect(200, 150, DISPLAY_CLASSIC);
    CHECK(s.x == 0 && s.y == 0 && s.w == 200 && s.h == 150);
}

static void test_effective_aspect(void) {
    CHECK(video_effective_aspect(DISPLAY_MODERN, ASPECT_4_3) == ASPECT_NATIVE);
    CHECK(video_effective_aspect(DISPLAY_MODERN, ASPECT_16_9) == ASPECT_NATIVE);
    CHECK(video_effective_aspect(DISPLAY_CLASSIC, ASPECT_4_3) == ASPECT_4_3);
}

static void test_layout_integer(void) {
    /* 1920x1080 window, Modern 400x224, integer: 4x = 1600x896, centred. */
    eb_rect_t r = video_layout(1920, 1080, 400, 224, SCALE_INTEGER, ASPECT_NATIVE);
    CHECK(r.w == 1600 && r.h == 896 && r.x == 160 && r.y == 92);
    CHECK(r.w % 400 == 0 && r.h % 224 == 0 && r.w / 400 == r.h / 224); /* square pixels */
    /* Classic 256x224 in the same window: 4x = 1024x896. */
    r = video_layout(1920, 1080, 256, 224, SCALE_INTEGER, ASPECT_NATIVE);
    CHECK(r.w == 1024 && r.h == 896 && r.x == 448 && r.y == 92);
    /* Classic 4:3 CRT: height snaps to a multiple of 224, width follows 4:3. */
    r = video_layout(1920, 1080, 256, 224, SCALE_INTEGER, ASPECT_4_3);
    CHECK(r.h == 896 && r.w == 896 * 4 / 3);
}

static void test_layout_aspect_ratios(void) {
    /* Modern must never be stretched on 16:9, 16:10, 21:9 or 4:3 windows (fit mode): pixels stay square. */
    int win[][2] = { {1920,1080}, {1920,1200}, {2560,1080}, {1024,768}, {3440,1440} };
    for (unsigned i = 0; i < sizeof(win) / sizeof(win[0]); i++) {
        eb_rect_t r = video_layout(win[i][0], win[i][1], 400, 224, SCALE_FIT, ASPECT_NATIVE);
        CHECK(r.w <= win[i][0] && r.h <= win[i][1]);
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= win[i][0] && r.y + r.h <= win[i][1]);
        /* |r.w/r.h - 400/224| tiny (integer rounding only) */
        long lhs = (long)r.w * 224, rhs = (long)r.h * 400;
        CHECK(labs(lhs - rhs) <= 400 + 224);
        /* image is centred */
        CHECK(labs((long)r.x - (win[i][0] - r.w - r.x)) <= 1);
        CHECK(labs((long)r.y - (win[i][1] - r.h - r.y)) <= 1);
    }
    /* Ultrawide 21:9 window: pillarboxed, not stretched. */
    eb_rect_t u = video_layout(3440, 1440, 400, 224, SCALE_FIT, ASPECT_NATIVE);
    CHECK(u.h == 1440 && u.w < 3440 && u.x > 0);
}

static void test_layout_edge_cases(void) {
    eb_rect_t r = video_layout(0, 0, 400, 224, SCALE_FIT, ASPECT_NATIVE);
    CHECK(r.w == 0 && r.h == 0);
    r = video_layout(800, 600, 0, 0, SCALE_FIT, ASPECT_NATIVE); /* bad source: no divide by zero */
    CHECK(r.w == 800 && r.h == 600);
    /* Window smaller than 1x with integer scaling falls back to fit. */
    r = video_layout(300, 200, 400, 224, SCALE_INTEGER, ASPECT_NATIVE);
    CHECK(r.w <= 300 && r.h <= 200 && r.w > 0 && r.h > 0);
    /* Stretch fills the drawable. */
    r = video_layout(1000, 500, 400, 224, SCALE_STRETCH, ASPECT_NATIVE);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1000 && r.h == 500);
}

static void test_full_rect(void) {
    /* Classic crop of a 400-wide frame at 4x: dst 1024x896; whole frame would be 1600x896 starting at dst.x. */
    eb_rect_t dst = video_layout(1920, 1080, 256, 224, SCALE_INTEGER, ASPECT_NATIVE);
    eb_rect_t f = video_full_rect(dst, 256, 224, 400, 224);
    CHECK(f.x == dst.x && f.y == dst.y && f.w == 1600 && f.h == 896);
    /* The visible part of the full rect (its first 256 columns) is exactly dst. */
    CHECK(f.w * 256 / 400 == dst.w);
    /* Whole frame visible: full rect == dst. */
    eb_rect_t d2 = video_layout(1920, 1080, 400, 224, SCALE_INTEGER, ASPECT_NATIVE);
    eb_rect_t f2 = video_full_rect(d2, 400, 224, 400, 224);
    CHECK(f2.x == d2.x && f2.w == d2.w && f2.h == d2.h);
    /* Degenerate visible size leaves the rect alone. */
    eb_rect_t f3 = video_full_rect(d2, 0, 0, 400, 224);
    CHECK(f3.w == d2.w);
}

static void test_modern_fill(void) {
    /* Windows of common shapes: the Modern view + Fit layout must cover the drawable with no bars. */
    int win[][2] = { {1920,1080}, {1919,1199}, {1920,1200}, {1280,720}, {1600,900}, {2560,1440}, {1280,1024}, {1024,768} };
    for (unsigned i = 0; i < sizeof(win) / sizeof(win[0]); i++) {
        eb_rect_t v = video_modern_view(400, 224, win[i][0], win[i][1]);
        CHECK(v.h == 224 && v.w >= SNES_WIDTH && v.w <= 400 && v.x >= 0 && v.x + v.w <= 400);
        eb_rect_t r = video_layout(win[i][0], win[i][1], v.w, v.h, SCALE_FIT, ASPECT_NATIVE);
        CHECK(r.x == 0 && r.y == 0 && r.w == win[i][0] && r.h == win[i][1]);
    }
    /* Ultrawide beyond the frame: width clamps to the frame, side bars remain (no stretching). */
    eb_rect_t v = video_modern_view(400, 224, 3440, 1440);
    CHECK(v.w == 400);
    eb_rect_t u = video_layout(3440, 1440, v.w, v.h, SCALE_FIT, ASPECT_NATIVE);
    CHECK(u.h == 1440 && u.w < 3440);
    /* Portrait/narrow window never goes below the original picture width. */
    CHECK(video_modern_view(400, 224, 500, 1000).w == SNES_WIDTH);
    /* Unknown drawable size: whole frame. */
    CHECK(video_modern_view(400, 224, 0, 0).w == 400);
}

int main(void) {
    test_modern_fill();
    test_source_rect();
    test_effective_aspect();
    test_layout_integer();
    test_layout_aspect_ratios();
    test_layout_edge_cases();
    test_full_rect();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
