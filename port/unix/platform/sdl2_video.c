#include "platform/platform.h"
#include "platform/video.h"
#include "platform/config.h"
#include "platform/video_layout.h"
#include "platform/gpu_ppu.h"
#include "snes/ppu.h"
#include "game_main.h"
#ifdef EB_HAVE_VULKAN
#include "platform/vk_renderer.h"
#endif
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FW EB_VIEWPORT_WIDTH
#define FH EB_VIEWPORT_HEIGHT

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static bool using_vk;
static char backend_name[96] = "none";

static pixel_t frame[FW * FH];    /* last frame produced by the PPU */
static pixel_t shown[FW * FH];    /* visible part of the frame + overlay, laid out with pitch FW (presenter input) */
static pixel_t vis[FW * FH];      /* contiguous copy of the visible part, so overlays can use pitch == width */
static bool have_frame;
static video_overlay_fn overlay_fn;

static int frame_counter;
static int shot_frame;
static char shot_path[512];

SDL_Window *video_window(void) { return window; }
void video_set_overlay(video_overlay_fn fn) { overlay_fn = fn; }
video_overlay_fn video_set_overlay_get(void) { return overlay_fn; }
const char *video_backend_name(void) { return backend_name; }
bool video_is_vulkan(void) { return using_vk; }

/* Part of the frame that is shown for the current display mode. */
static eb_rect_t static_view_rect(void) { return video_source_rect(FW, FH, cfg.display_mode); }

/* Size of what we draw into, in pixels (0,0 when unknown). */
static void drawable_size(int *w, int *h) {
    *w = *h = 0;
#ifdef EB_HAVE_VULKAN
    if (using_vk) { vkr_drawable_size(w, h); return; }
#endif
    if (renderer) SDL_GetRendererOutputSize(renderer, w, h);
}

/* Part of the frame shown right now. Classic: the fixed centred SNES area. Modern: full height and
 * as wide as the window's aspect needs, so the window is filled without stretching. */
static eb_rect_t view_rect(void) {
    if (cfg.display_mode == DISPLAY_MODERN) {
        int dw, dh;
        drawable_size(&dw, &dh);
        return video_modern_view(FW, FH, dw, dh);
    }
    return static_view_rect();
}
/* These size the window, so they use the fixed (window-independent) view. */
int video_view_width(void)  { return static_view_rect().w; }
int video_view_height(void) { return static_view_rect().h; }
bool video_wide_available(void) { return FW > SNES_WIDTH || FH > SNES_HEIGHT; }

/* ------------------------------------------------------------------ window helpers */

static int window_display(void) {
    int d = window ? SDL_GetWindowDisplayIndex(window) : 0;
    return d < 0 ? 0 : d;
}

/* Largest window scale that still fits on the display (with room for the title bar). */
static int max_fitting_scale(void) {
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(window_display(), &r) != 0) return CFG_MAX_SCALE;
    int s = CFG_MAX_SCALE;
    int vw = video_view_width(), vh = video_view_height();
    while (s > 1 && (vw * s > r.w || (vh * s + 40) > r.h)) s--;
    return s;
}

int video_mode_count(void) {
    int n = SDL_GetNumDisplayModes(window_display());
    return n < 0 ? 0 : n;
}

bool video_mode_get(int index, int *w, int *h, int *hz) {
    SDL_DisplayMode m;
    if (SDL_GetDisplayMode(window_display(), index, &m) != 0) return false;
    *w = m.w; *h = m.h; *hz = m.refresh_rate;
    return true;
}

static void destroy_backend(void) {
#ifdef EB_HAVE_VULKAN
    if (using_vk) vkr_shutdown();
#endif
    if (texture)  SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    texture = NULL; renderer = NULL; window = NULL;
    using_vk = false;
    snprintf(backend_name, sizeof(backend_name), "none");
}

static SDL_Window *make_window(bool vulkan) {
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (vulkan ? SDL_WINDOW_VULKAN : 0);
    int scale = cfg.window_scale;
    return SDL_CreateWindow("EarthBound (EB-Native)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            video_view_width() * scale, video_view_height() * scale, flags);
}

static bool create_sdl_backend(void) {
    window = make_window(false);
    if (!window) return false;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) return false;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING, FW, FH);
    if (!texture) return false;
    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    snprintf(backend_name, sizeof(backend_name), "SDL renderer (%s)", info.name);
    return true;
}

static bool create_backend(void) {
#ifdef EB_HAVE_VULKAN
    if (cfg.renderer == RENDERER_VULKAN) {
        window = make_window(true);
        if (window && vkr_init(window, FW, FH, cfg.vsync)) {
            using_vk = true;
            snprintf(backend_name, sizeof(backend_name), "Vulkan (%s)", vkr_device_name());
            return true;
        }
        fprintf(stderr, "video: Vulkan unavailable, falling back to the SDL renderer\n");
        if (window) { SDL_DestroyWindow(window); window = NULL; }
        if (!create_sdl_backend()) return false;
        snprintf(backend_name + strlen(backend_name), sizeof(backend_name) - strlen(backend_name),
                 " [Vulkan unavailable]");
        return true;
    }
#endif
    return create_sdl_backend();
}

bool platform_video_init(void) {
    memset(frame, 0, sizeof(frame));
    if (platform_headless) return true; /* no window; frames are still kept for screenshots */
    cfg_validate(&cfg);
    if (!create_backend()) return false;
    video_apply_config();
    return true;
}

void platform_video_shutdown(void) {
    destroy_backend();
}

/* ------------------------------------------------------------------ config -> window */

void video_apply_config(void) {
    if (!window) return;

    /* Renderer switch needs a new window: an SDL_WINDOW_VULKAN window cannot host an SDL_Renderer. */
    bool want_vk = false;
#ifdef EB_HAVE_VULKAN
    want_vk = (cfg.renderer == RENDERER_VULKAN);
#endif
    bool was_vk_requested = using_vk || strstr(backend_name, "Vulkan unavailable");
    if (want_vk != (bool)was_vk_requested) {
        destroy_backend();
        if (!create_backend()) {
            fprintf(stderr, "video: could not create a window for the new renderer\n");
            exit(1);
        }
    }

    int fit = max_fitting_scale();
    if (cfg.window_scale > fit) cfg.window_scale = fit;

    if (using_vk) {
#ifdef EB_HAVE_VULKAN
        vkr_set_vsync(cfg.vsync != 0);
#endif
    } else if (renderer) {
        SDL_RenderSetVSync(renderer, cfg.vsync ? 1 : 0);
    }

    switch (cfg.window_mode) {
    case WINMODE_WINDOWED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, video_view_width() * cfg.window_scale, video_view_height() * cfg.window_scale);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        break;
    case WINMODE_BORDERLESS:
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "video: borderless fullscreen failed: %s\n", SDL_GetError());
            cfg.window_mode = WINMODE_WINDOWED;
            SDL_SetWindowFullscreen(window, 0);
        }
        break;
    case WINMODE_EXCLUSIVE: {
        SDL_DisplayMode want, got;
        SDL_zero(want);
        SDL_GetDesktopDisplayMode(window_display(), &got);
        if (cfg.fs_width > 0) {
            want.w = cfg.fs_width; want.h = cfg.fs_height; want.refresh_rate = cfg.fs_refresh;
            if (!SDL_GetClosestDisplayMode(window_display(), &want, &got)) {
                /* Mode not supported on this display: safe fallback to the desktop mode. */
                fprintf(stderr, "video: %dx%d@%d not supported, using desktop mode\n", cfg.fs_width,
                        cfg.fs_height, cfg.fs_refresh);
                cfg.fs_width = cfg.fs_height = cfg.fs_refresh = 0;
                SDL_GetDesktopDisplayMode(window_display(), &got);
            }
        }
        SDL_SetWindowDisplayMode(window, &got);
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0) {
            fprintf(stderr, "video: exclusive fullscreen failed: %s\n", SDL_GetError());
            cfg.window_mode = WINMODE_BORDERLESS;
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        break;
    }
    }

    if (texture)
        SDL_SetTextureScaleMode(texture, cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    video_present_again();
}

/* ------------------------------------------------------------------ presentation */

/* Build what is shown: the visible part of the frame (all of it in Modern mode, the centred
 * 256x224 area in Classic) with the overlay drawn on top. The result is laid out with pitch FW so
 * the presenters can upload it unchanged. Returns the visible size through vw and vh. */
static const pixel_t *compose_visible(int *vw, int *vh) {
    eb_rect_t vr = view_rect();
    *vw = vr.w; *vh = vr.h;
    bool full = (vr.x == 0 && vr.y == 0 && vr.w == FW && vr.h == FH);
    if (full && !overlay_fn) return frame;
    if (full) {
        memcpy(shown, frame, sizeof(shown));
        overlay_fn(shown, FW, FH);
    } else {
        for (int y = 0; y < vr.h; y++)
            memcpy(&vis[y * vr.w], &frame[(vr.y + y) * FW + vr.x], (size_t)vr.w * sizeof(pixel_t));
        if (overlay_fn) overlay_fn(vis, vr.w, vr.h); /* menus lay themselves out from w/h */
        for (int y = 0; y < vr.h; y++)
            memcpy(&shown[y * FW], &vis[y * vr.w], (size_t)vr.w * sizeof(pixel_t));
    }
    return shown;
}

static void present(void) {
    int vw, vh;
    const pixel_t *src = compose_visible(&vw, &vh);
    /* The FPS counter is stamped into the frame by the game: keep it inside what is visible. */
    eb_rect_t vr = view_rect();
    eb_fps_overlay_right = vr.x + vr.w;
    int aspect = video_effective_aspect(cfg.display_mode, cfg.aspect);
    if (using_vk) {
#ifdef EB_HAVE_VULKAN
        vkr_present(src, cfg.scale_mode, aspect, cfg.smooth != 0, vw, vh);
#endif
    } else if (renderer && texture) {
        void *px; int pitch;
        if (SDL_LockTexture(texture, NULL, &px, &pitch) == 0) {
            for (int y = 0; y < FH; y++) memcpy((uint8_t *)px + y * pitch, src + y * FW, FW * sizeof(pixel_t));
            SDL_UnlockTexture(texture);
        }
        int ow, oh;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        eb_rect_t r = video_layout(ow, oh, vw, vh, cfg.scale_mode, aspect);
        SDL_Rect dst = { r.x, r.y, r.w, r.h };
        SDL_Rect srcr = { 0, 0, vw, vh }; /* visible part sits at the origin of the uploaded buffer */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, &srcr, &dst);
        SDL_RenderPresent(renderer);
    }
}

void video_present_again(void) {
    if (platform_headless || !window) return;
    present();
}

void platform_video_begin_frame(void) {}

/* Frame production. With Vulkan active and cfg.gpu_ppu set, the SNES picture is composed by the
 * compute shader (gpu_ppu.c); otherwise (SDL renderer, headless, unsupported viewport, or the
 * setting off) the CPU compositor in src/snes/ppu_render.c is used. This definition replaces the
 * library's default platform_render_frame (src/platform/platform_render.c). */
static scanline_stamp_cb_t stamp_cb;
static void stamped_scanline(int y, const pixel_t *pixels) {
    stamp_cb(y, (pixel_t *)pixels);
    platform_video_send_scanline(y, pixels);
}

void platform_render_frame(scanline_stamp_cb_t fps_overlay_cb) {
    platform_video_begin_frame();
    if (using_vk && cfg.gpu_ppu && gpu_ppu_render(frame)) {
        if (fps_overlay_cb)
            for (int y = 0; y < FH; y++) fps_overlay_cb(y, &frame[y * FW]);
    } else if (fps_overlay_cb) {
        stamp_cb = fps_overlay_cb;
        ppu_render_frame(stamped_scanline);
    } else {
        ppu_render_frame(platform_video_send_scanline);
    }
    platform_video_end_frame();
}

void platform_video_send_scanline(int y, const pixel_t *pixels) {
    if (y >= 0 && y < FH) memcpy(&frame[y * FW], pixels, FW * sizeof(pixel_t));
}

pixel_t *platform_video_get_framebuffer(void) {
    return platform_headless ? NULL : frame;
}

void platform_video_end_frame(void) {
    have_frame = true;
    frame_counter++;
    if (!platform_headless) present();
    if (shot_frame > 0 && frame_counter == shot_frame) {
        if (!video_screenshot(shot_path)) fprintf(stderr, "video: screenshot to %s failed\n", shot_path);
        else fprintf(stderr, "video: screenshot saved to %s (frame %d)\n", shot_path, frame_counter);
    }
}

void platform_video_set_vsync(bool enabled) {
    cfg.vsync = enabled ? 1 : 0;
    video_apply_config();
}

void video_schedule_screenshot(int frame_number, const char *path) {
    shot_frame = frame_number;
    snprintf(shot_path, sizeof(shot_path), "%s", path);
}

bool video_screenshot(const char *path) {
    if (!have_frame) return false;
    SDL_Surface *s = NULL;
    uint8_t *rgba = NULL;
    int w = 0, h = 0;
#ifdef EB_HAVE_VULKAN
    if (using_vk && vkr_screenshot(&rgba, &w, &h)) {
        s = SDL_CreateRGBSurfaceWithFormatFrom(rgba, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    }
#endif
    if (!s) {
        free(rgba);
        int vw, vh;
        const pixel_t *src = compose_visible(&vw, &vh); /* headless runs never present, so compose here */
        rgba = malloc((size_t)vw * vh * 4);
        if (!rgba) return false;
        w = vw; h = vh;
        for (int y = 0; y < vh; y++)
            for (int x = 0; x < vw; x++) {
                uint32_t c = pixel_to_rgb888(src[y * FW + x]);
                uint8_t *o = &rgba[((size_t)y * vw + x) * 4];
                o[0] = (c >> 16) & 0xFF;
                o[1] = (c >> 8) & 0xFF;
                o[2] = c & 0xFF;
                o[3] = 255;
            }
        s = SDL_CreateRGBSurfaceWithFormatFrom(rgba, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    }
    bool ok = s && SDL_SaveBMP(s, path) == 0;
    if (s) SDL_FreeSurface(s);
    free(rgba);
    return ok;
}
