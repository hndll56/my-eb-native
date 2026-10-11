/* Packs the game's PPU state (src/snes/ppu.h) into the flat word buffer read by
 * shaders/ppu.comp and runs it through the Vulkan renderer. The shader is a faithful port of
 * src/snes/ppu_render.c for the native 256x224 viewport; layout constants must match it. */
#include "platform/gpu_ppu.h"
#include "platform/vk_renderer.h"
#include "snes/ppu.h"
#include "game/battle_bg.h"
#include <stdio.h>
#include <string.h>

#if !defined(EB_HAVE_VULKAN) || EB_VIEWPORT_WIDTH != 256 || EB_VIEWPORT_HEIGHT != 224 || defined(EB_PIXEL_RGB565)
bool gpu_ppu_render(pixel_t *out) { (void)out; return false; }
void gpu_ppu_set_compare(bool on) { (void)on; }
void gpu_ppu_set_start_frame(int frame) { (void)frame; }
#else

enum { OFF_PAL = 64, OFF_WH = 320, OFF_TMTS = 1216, OFF_DIST = 1664, OFF_OBJ = 1888, OFF_VRAM = 2400 };
enum {
    R_INIDISP = 0, R_OBSEL = 1, R_BGMODE = 2, R_MOSAIC = 3, R_BGSC = 4, R_NBA = 8, R_HOFS = 10, R_VOFS = 14,
    R_TM = 18, R_TS = 19, R_TMW = 20, R_TSW = 21, R_CGWSEL = 22, R_CGADSUB = 23, R_FIXED = 24, R_W12SEL = 25,
    R_W34SEL = 26, R_WOBJSEL = 27, R_WH0 = 28, R_WBGLOG = 32, R_WOBJLOG = 33, R_WHDMA = 34, R_W2HDMA = 35,
    R_TMHDMA = 36, R_SPRYOFF = 37, R_DIST = 38, R_BPP = 39, R_GP = 43, R_THRESH = 51, R_NOBJ = 56, R_MODE = 57
};

static const int obj_sizes[8][2][2] = {
    {{8, 8}, {16, 16}}, {{8, 8}, {32, 32}}, {{8, 8}, {64, 64}}, {{16, 16}, {32, 32}},
    {{16, 16}, {64, 64}}, {{32, 32}, {64, 64}}, {{16, 32}, {32, 64}}, {{16, 32}, {32, 32}},
};

static bool compare_mode;
static int active_from;
void gpu_ppu_set_compare(bool on) { compare_mode = on; }
void gpu_ppu_set_start_frame(int frame) { active_from = frame; }

static void pack_state(uint32_t *w) {
    memset(w, 0, VKR_PPU_IN_WORDS * 4);
    int mode = ppu.bgmode & BGMODE_MODE_MASK;
    w[R_INIDISP] = ppu.inidisp; w[R_OBSEL] = ppu.obsel; w[R_BGMODE] = ppu.bgmode; w[R_MOSAIC] = ppu.mosaic;
    for (int i = 0; i < 4; i++) { w[R_BGSC + i] = ppu.bg_sc[i]; w[R_HOFS + i] = ppu.bg_hofs[i]; w[R_VOFS + i] = ppu.bg_vofs[i]; }
    w[R_NBA] = ppu.bg_nba[0]; w[R_NBA + 1] = ppu.bg_nba[1];
    w[R_TM] = ppu.tm; w[R_TS] = ppu.ts; w[R_TMW] = ppu.tmw; w[R_TSW] = ppu.tsw;
    w[R_CGWSEL] = ppu.cgwsel; w[R_CGADSUB] = ppu.cgadsub;
    w[R_FIXED] = (uint32_t)ppu.coldata_r | ((uint32_t)ppu.coldata_g << 6) | ((uint32_t)ppu.coldata_b << 11);
    w[R_W12SEL] = ppu.w12sel; w[R_W34SEL] = ppu.w34sel; w[R_WOBJSEL] = ppu.wobjsel;
    w[R_WH0] = ppu.wh0; w[R_WH0 + 1] = ppu.wh1; w[R_WH0 + 2] = ppu.wh2; w[R_WH0 + 3] = ppu.wh3;
    w[R_WBGLOG] = ppu.wbglog; w[R_WOBJLOG] = ppu.wobjlog;
    w[R_WHDMA] = ppu.window_hdma_active; w[R_W2HDMA] = ppu.window2_hdma_active; w[R_TMHDMA] = ppu.tm_hdma_active;
    w[R_SPRYOFF] = (uint32_t)(int32_t)ppu.sprite_y_offset;
    w[R_DIST] = bg2_distortion_active;
    w[R_MODE] = (uint32_t)mode;

    int bpp[4] = {0, 0, 0, 0};
    switch (mode) {
    case 0: bpp[0] = bpp[1] = bpp[2] = bpp[3] = 2; break;
    case 1: bpp[0] = 4; bpp[1] = 4; bpp[2] = 2; break;
    case 2: bpp[0] = 4; bpp[1] = 4; break;
    case 3: bpp[0] = 8; bpp[1] = 4; break;
    case 4: bpp[0] = 8; bpp[1] = 2; break;
    case 5: bpp[0] = 4; bpp[1] = 2; break;
    case 7: bpp[0] = 8; break;
    }
    for (int i = 0; i < 4; i++) w[R_BPP + i] = (uint32_t)bpp[i];

    /* Global priority tables, identical to ppu_render_frame_ex. */
    bool bg3prio = (ppu.bgmode & BGMODE_BG3_PRIO) && mode == 1;
    uint8_t gp[4][2] = {{0}}, th[5];
    switch (mode) {
    case 1:
        gp[0][0] = 5; gp[0][1] = 7; gp[1][0] = 4; gp[1][1] = 6; gp[2][0] = 2; gp[2][1] = bg3prio ? 8 : 3;
        th[0] = 0; th[1] = 3; th[2] = bg3prio ? 3 : 4; th[3] = 6; th[4] = 8; break;
    case 0:
        gp[0][0] = 7; gp[0][1] = 11; gp[1][0] = 5; gp[1][1] = 9; gp[2][0] = 3; gp[2][1] = 6; gp[3][0] = 1; gp[3][1] = 2;
        th[0] = 0; th[1] = 2; th[2] = 5; th[3] = 7; th[4] = 11; break;
    default:
        gp[0][0] = 3; gp[0][1] = 5; gp[1][0] = 2; gp[1][1] = 4;
        th[0] = 0; th[1] = 2; th[2] = 3; th[3] = 5; th[4] = 6; break;
    }
    for (int i = 0; i < 4; i++) { w[R_GP + i * 2] = gp[i][0]; w[R_GP + i * 2 + 1] = gp[i][1]; }
    for (int i = 0; i < 5; i++) w[R_THRESH + i] = th[i];

    for (int i = 0; i < 256; i++) w[OFF_PAL + i] = bgr555_to_pixel(ppu.cgram[i]);
    for (int y = 0; y < 224; y++) {
        w[OFF_WH + y] = ppu.wh0_table[y]; w[OFF_WH + 224 + y] = ppu.wh1_table[y];
        w[OFF_WH + 448 + y] = ppu.wh2_table[y]; w[OFF_WH + 672 + y] = ppu.wh3_table[y];
        w[OFF_TMTS + y] = ppu.tm_per_scanline[y]; w[OFF_TMTS + 224 + y] = ppu.ts_per_scanline[y];
        w[OFF_DIST + y] = (uint16_t)bg2_scanline_hoffset[y];
    }

    /* Sprites that can touch the picture, ascending OAM index (lowest index wins on the GPU). */
    int size_sel = (ppu.obsel >> 5) & 7, n = 0;
    for (int i = 0; i < 128; i++) {
        int size_bit = (((ppu.oam_hi[i / 4] >> ((i % 4) * 2)) & 3) >> 1) & 1;
        int sx = ppu.oam_full_x[i] + ppu.sprite_x_offset, sy = ppu.oam_full_y[i] + ppu.sprite_y_offset;
        int sw = obj_sizes[size_sel][size_bit][0], sh = obj_sizes[size_sel][size_bit][1];
        if (sx >= 256 || sx + sw <= 0 || sy + sh <= 0 || sy >= 264) continue;
        uint32_t *o = &w[OFF_OBJ + n * 4];
        o[0] = (uint16_t)sx; o[1] = (uint16_t)sy;
        o[2] = (uint32_t)sw | ((uint32_t)sh << 8) | ((uint32_t)ppu.oam[i].attr << 16);
        o[3] = ppu.oam[i].tile | ((uint32_t)(ppu.oam[i].attr & 1) << 8);
        n++;
    }
    w[R_NOBJ] = (uint32_t)n;

    for (int i = 0; i < VRAM_SIZE / 4; i++) {
        const uint8_t *v = &ppu.vram[i * 4];
        w[OFF_VRAM + i] = v[0] | (v[1] << 8) | (v[2] << 16) | ((uint32_t)v[3] << 24);
    }
}

static pixel_t cpu_frame[EB_VIEWPORT_WIDTH * EB_VIEWPORT_HEIGHT];
static void cpu_line(int y, const pixel_t *px) { memcpy(&cpu_frame[y * EB_VIEWPORT_WIDTH], px, EB_VIEWPORT_WIDTH * sizeof(pixel_t)); }

bool gpu_ppu_render(pixel_t *out) {
    static int seen;
    if (!vkr_ppu_available() || seen++ < active_from) return false; /* test aid: CPU path before frame N */
    pack_state(vkr_ppu_input());
    if (!vkr_ppu_render(out)) return false;
    if (compare_mode) {
        static int frame_no, bad_frames;
        ppu_render_frame(cpu_line);
        int diff = 0, first = -1;
        for (int i = 0; i < EB_VIEWPORT_WIDTH * EB_VIEWPORT_HEIGHT; i++)
            if (cpu_frame[i] != out[i]) { if (first < 0) first = i; diff++; }
        if (diff) {
            bad_frames++;
            fprintf(stderr, "gpu_ppu: frame %d differs from CPU in %d px (first at x=%d y=%d: cpu=%04x gpu=%04x) "
                    "[mode %d tm %02x ts %02x cgadsub %02x mosaic %02x]\n", frame_no, diff, first % 256, first / 256,
                    cpu_frame[first], out[first], ppu.bgmode & 7, ppu.tm, ppu.ts, ppu.cgadsub, ppu.mosaic);
        }
        if (++frame_no % 600 == 0)
            fprintf(stderr, "gpu_ppu: compared %d frames, %d differed\n", frame_no, bad_frames);
    }
    return true;
}
#endif
