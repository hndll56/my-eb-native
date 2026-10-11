#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "game_main.h"
#include "game/audio.h"
#include "game/game_state.h"
#include "data/assets.h"
#include "audio/audio_selftest.h"
#include "audio/hqmusic.h"
#include "core/log.h"
#include "core/state_dump.h"
#include "platform/config.h"
#include "platform/menus.h"
#include "platform/video.h"
#include "platform/gpu_ppu.h"

/* Unix-specific: set save file path (defined in sdl2_save.c) */
void platform_save_set_path(const char *path);
bool input_selftest_pad(void);
extern const char *const asset_names[];
int asset_override_load_dir(const char *dir);
const char *asset_override_check(int id, const unsigned char *data, unsigned long size);
#include "verify/verify.h"

/* Cleanup handler registered with atexit() — runs on any exit() call. */
static void platform_cleanup(void) {
    platform_timer_shutdown();
    platform_audio_shutdown();
    platform_input_shutdown();
    platform_video_shutdown();
    SDL_Quit();
}


/* In-game save self-test: save -> clobber -> load must restore; a bad primary copy must fall back
 * to the backup; two bad copies must be rejected (never loaded silently). Uses a temp file. */
static bool save_selftest(void) {
    const char *tmp = "eb-selftest.srm";
    remove(tmp);
    platform_save_set_path(tmp);
    bool ok = true;
    #define CHECK(c) do { if (!(c)) { fprintf(stderr, "save selftest: failed: %s\n", #c); ok = false; } } while (0)
    GameState orig = game_state;
    game_state.money_carried = 4242;
    CHECK(save_game(1));
    game_state.money_carried = 1;
    CHECK(load_game(1) && game_state.money_carried == 4242);
    uint8_t junk[16]; memset(junk, 0xA5, sizeof(junk));
    size_t b0 = (size_t)(1 * SAVE_COPY_COUNT) * sizeof(SaveBlock);
    platform_save_write(junk, b0 + 100, sizeof(junk));          /* corrupt primary */
    game_state.money_carried = 1;
    CHECK(load_game(1) && game_state.money_carried == 4242);    /* backup used */
    platform_save_write(junk, b0 + sizeof(SaveBlock) + 100, sizeof(junk)); /* corrupt backup too */
    CHECK(!load_game(1));
    game_state.favourite_thing[1] = 0;
    CHECK(!(load_game(0) && game_state.favourite_thing[1] != 0)); /* never-written slot reads as empty (file_select rule) */
    CHECK(!load_game(SAVE_COUNT) && !save_game(-1));
    #undef CHECK
    game_state = orig;
    remove(tmp);
    return ok;
}

/* Override validation: wrong-size fixed asset, empty file and corrupt LZHAL data are refused;
 * the original bytes of a fixed asset and of a compressed asset are accepted. */
static bool mods_selftest(void) {
    bool ok = true;
    #define CHECK(c) do { if (!(c)) { fprintf(stderr, "mods selftest: failed: %s\n", #c); ok = false; } } while (0)
    int fixed = -1, packed = -1;
    for (int i = 0; i < ASSET_COUNT && (fixed < 0 || packed < 0); i++) {
        const char *n = asset_names[i];
        size_t l = strlen(n);
        if (l > 6 && !strcmp(n + l - 6, ".lzhal")) { if (packed < 0) packed = i; }
        else if (l > 4 && !strcmp(n + l - 4, ".bin") && ASSET_SIZE(i) > 4) { if (fixed < 0) fixed = i; }
    }
    CHECK(fixed >= 0 && packed >= 0);
    if (fixed >= 0) {
        size_t sz = ASSET_SIZE(fixed);
        CHECK(asset_override_check(fixed, ASSET_DATA(fixed), sz) == NULL);
        CHECK(asset_override_check(fixed, ASSET_DATA(fixed), sz - 1) != NULL);
        CHECK(asset_override_check(fixed, ASSET_DATA(fixed), 0) != NULL);
    }
    if (packed >= 0) {
        CHECK(asset_override_check(packed, ASSET_DATA(packed), ASSET_SIZE(packed)) == NULL);
        unsigned char junk[64]; memset(junk, 0xFF, sizeof(junk));
        CHECK(asset_override_check(packed, junk, sizeof(junk)) != NULL);
    }
    CHECK(asset_override_check(-1, (const unsigned char *)"x", 1) != NULL);
    #undef CHECK
    return ok;
}

const char *eb_mods_dir;

int main(int argc, char *argv[]) {
    const char *verify_rom_path = NULL;
    const char *config_path = NULL;
    const char *mods_dir = NULL;
    bool dsp_from_cli = false;
    bool savestate_selftest = false;
    bool want_debug = getenv("EB_DEBUG") != NULL;
    int force_renderer = -1, force_winmode = -1, force_ppu = -1;
    int shot_frame = 0;
    const char *shot_path = NULL;
    static const char *debug_cmds[16384];
    int n_debug_cmds = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0 && i + 1 < argc) {
            verify_rom_path = argv[++i];
        } else if (strcmp(argv[i], "--selftest-savestate") == 0) {
            /* Run the savestate save->load->save round-trip self-test and exit.
             * Implies headless so it neither opens a window nor plays the game. */
            savestate_selftest = true;
            platform_headless = true;
        } else if (strcmp(argv[i], "--mods") == 0 && i + 1 < argc) {
            mods_dir = argv[++i];
        } else if (strcmp(argv[i], "--audio-dsp") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "lakesnes")) { audio_dsp_mode = AUDIO_DSP_LAKESNES; dsp_from_cli = true; }
            else if (!strcmp(m, "shadow")) { audio_dsp_mode = AUDIO_DSP_SHADOW; dsp_from_cli = true; }
            else if (!strcmp(m, "native")) { audio_dsp_mode = AUDIO_DSP_NATIVE; dsp_from_cli = true; }
            else { fprintf(stderr, "--audio-dsp: expected lakesnes|shadow|native\n"); return 1; }
        } else if (strcmp(argv[i], "--selftest-audio") == 0) {
            /* --selftest-audio [FIRST LAST FRAMES]: native DSP vs lakesnes DSP on real music + SFX */
            int a = 1, b = 8, fr = 360;
            if (i + 3 < argc && argv[i + 1][0] != '-') { a = atoi(argv[i + 1]); b = atoi(argv[i + 2]); fr = atoi(argv[i + 3]); i += 3; }
            bool ok = audio_selftest(a, b, fr);
            fprintf(stderr, "audio self-test: %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--selftest-config") == 0) {
            bool ok = cfg_selftest();
            fprintf(stderr, "config self-test: %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--selftest-hqmusic") == 0) {
            bool ok = hqmusic_selftest();
            fprintf(stderr, "hq music self-test: %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--selftest-mods") == 0) {
            bool ok = mods_selftest();
            fprintf(stderr, "asset override self-test: %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--selftest-pad") == 0) {
            /* Virtual-gamepad test: needs no window, audio or real controller. */
            cfg_set_defaults(&cfg);
            bool ok = input_selftest_pad();
            fprintf(stderr, "gamepad self-test: %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            platform_headless = true;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            platform_max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            platform_save_set_path(argv[++i]);
        } else if (strcmp(argv[i], "--skip-intro") == 0) {
            platform_skip_intro = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            want_debug = true;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            const char *r = argv[++i];
            force_renderer = (strcmp(r, "sdl") == 0) ? RENDERER_SDL : RENDERER_VULKAN;
        } else if (strcmp(argv[i], "--ppu") == 0 && i + 1 < argc) {
            force_ppu = strcmp(argv[++i], "cpu") == 0 ? 0 : 1;
        } else if (strcmp(argv[i], "--gpu-from") == 0 && i + 1 < argc) {
            gpu_ppu_set_start_frame(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--gpu-compare") == 0) {
            gpu_ppu_set_compare(true); /* GPU frame vs CPU frame, differences reported on stderr */
        } else if (strcmp(argv[i], "--window-mode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            force_winmode = !strcmp(m, "borderless") ? WINMODE_BORDERLESS : !strcmp(m, "exclusive") ? WINMODE_EXCLUSIVE : WINMODE_WINDOWED;
        } else if (strcmp(argv[i], "--screenshot-at") == 0 && i + 2 < argc) {
            shot_frame = atoi(argv[++i]);
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--debug-cmd") == 0 && i + 1 < argc) {
            if (n_debug_cmds < 16384) debug_cmds[n_debug_cmds++] = argv[++i]; else i++;
        } else if (strcmp(argv[i], "--debug-script") == 0 && i + 1 < argc) {
            /* One "FRAME:CMD" per line (same syntax as --debug-cmd); '#' starts a comment. */
            FILE *sf = fopen(argv[++i], "r");
            if (!sf) { fprintf(stderr, "cannot open debug script %s\n", argv[i]); return 1; }
            char line[256];
            while (fgets(line, sizeof(line), sf)) {
                line[strcspn(line, "\r\n")] = 0;
                if (!line[0] || line[0] == '#') continue;
                if (n_debug_cmds < 16384) debug_cmds[n_debug_cmds++] = strdup(line);
            }
            fclose(sf);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose_level++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'v' && argv[i][1] != '-') {
            /* Count v's: -v = 1, -vv = 2, -vvv = 3 */
            for (const char *p = &argv[i][1]; *p == 'v'; p++)
                verbose_level++;
        } else {
            if (strcmp(argv[i], "--help") && strcmp(argv[i], "-h"))
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--save FILE] [--headless] [--frames N] [--verbose] [--verify ROM] [--selftest-savestate] [--selftest-pad] [--selftest-mods] [--selftest-config] [--selftest-hqmusic] [--mods DIR]\n          [--audio-dsp lakesnes|shadow|native] [--selftest-audio [FIRST LAST FRAMES]]\n"
                    "          [--config FILE] [--renderer vulkan|sdl] [--ppu gpu|cpu] [--gpu-compare] [--window-mode windowed|borderless|exclusive]\n"
                    "          [--debug] [--debug-cmd FRAME:CMD] [--debug-script FILE] [--screenshot-at FRAME FILE]\n",
                    argv[0]);
            return (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) ? 0 : 1;
        }
    }

    /* Settings (settings.ini in the per-user pref dir, or --config FILE) */
    cfg_init(config_path);
    if (!dsp_from_cli) audio_dsp_mode = cfg.audio_dsp ? AUDIO_DSP_NATIVE : AUDIO_DSP_LAKESNES;
    eb_fast_forward_multiplier = cfg.ff_speed;
    if (force_renderer >= 0) cfg.renderer = force_renderer;
    if (force_winmode >= 0) cfg.window_mode = force_winmode;
    if (force_ppu >= 0) cfg.gpu_ppu = force_ppu;
    debug_set_enabled(want_debug);
    for (int i = 0; i < n_debug_cmds; i++)
        if (!debug_queue_script_command(debug_cmds[i])) fprintf(stderr, "bad --debug-cmd: %s\n", debug_cmds[i]);
    if (shot_path) video_schedule_screenshot(shot_frame, shot_path);

    /* Initialize SDL */
    uint32_t sdl_flags = SDL_INIT_TIMER;
    if (!platform_headless) {
        sdl_flags |= SDL_INIT_VIDEO;
    }
    if (SDL_Init(sdl_flags) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    /* Audio is optional: a machine without a sound device (or a CI box) still plays, silently. */
    bool audio_available = false;
#ifdef EB_ENABLE_AUDIO
    if (!platform_headless) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) audio_available = true;
        else fprintf(stderr, "audio: no usable sound device (%s); continuing without sound\n", SDL_GetError());
    }
#endif

    /* Initialize subsystems */
    if (!platform_video_init()) {
        fprintf(stderr, "Video init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    platform_input_init();
    debug_refresh_overlay();
    platform_save_init();
    atexit(platform_cleanup);
    platform_timer_init();

    eb_mods_dir = mods_dir;
    if (mods_dir) asset_override_load_dir(mods_dir);

    /* Initialize game systems */
    game_init();

    /* Savestate round-trip self-test: prove state_dump_load() reads back exactly
     * what state_dump_save() wrote (byte-identical), then exit. Runs against the
     * post-game_init boot state. */
    if (savestate_selftest) {
        bool ok = state_dump_roundtrip_test();
        fprintf(stderr, "savestate round-trip self-test: %s\n", ok ? "PASS" : "FAIL");
        bool ok_pert = state_dump_perturb_test();
        fprintf(stderr, "savestate perturb (cold-load) self-test: %s\n", ok_pert ? "PASS" : "FAIL");
        bool ok_cs = state_dump_crashsafe_test();
        fprintf(stderr, "savestate crash-safe self-test: %s\n", ok_cs ? "PASS" : "FAIL");
        bool ok_ap = state_dump_asset_pointer_test();
        fprintf(stderr, "savestate asset-pointer self-test: %s\n", ok_ap ? "PASS" : "FAIL");
        bool ok_save = save_selftest();
        fprintf(stderr, "in-game save self-test: %s\n", ok_save ? "PASS" : "FAIL");
        exit((ok && ok_pert && ok_cs && ok_ap && ok_save) ? 0 : 1);
    }

    /* Initialize audio (loads audio packs from embedded assets) */
    if (audio_available && !platform_audio_init())
        fprintf(stderr, "audio: initialisation failed; continuing without sound\n");
    settings_apply_audio();
    settings_apply_game();

#ifdef EB_ENABLE_VERIFY
    if (verify_rom_path) {
        if (!verify_init(verify_rom_path)) {
            fprintf(stderr, "Failed to initialize verification with ROM: %s\n", verify_rom_path);
        }
    }
#else
    (void)verify_rom_path;
#endif

    /* ── Pre-game main menu ── */
    if (main_menu_run() == MM_RESULT_EXIT)
        exit(0);
    /* New Game / Continue both enter the normal game loop;
     * the in-game title screen + file select handles the distinction. */

    /* Start frame timer, then run the single top-level game loop:
     * advance the game one frame (game_loop_step), then perform the one
     * per-frame host yield (host_process_frame). Restart-on-game-over is
     * handled inside the step machine, so there is no restart wrapper.
     * See docs/plans/savestate-unified-loop.md. */
    platform_timer_frame_start();

    while (!platform_input_quit_requested()) {
        game_loop_step();
        host_process_frame();
        host_root_boundary(); /* root boundary: perform any pending torn-safe capture */
        debug_root_boundary(); /* queued developer actions (warp, flags, battle) run here too */
    }

#ifdef EB_ENABLE_VERIFY
    verify_shutdown();
#endif

    return 0;
}
