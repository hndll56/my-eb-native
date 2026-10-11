#include "platform/platform.h"
#include "platform/config.h"
#include "platform/menu_input.h"
#include "platform/video.h"
#include "platform/menus.h"
#include "include/pad.h"
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

static uint16_t pad_state;
static uint16_t pad_prev;
static uint16_t aux_state;
static volatile sig_atomic_t quit_requested;
static SDL_GameController *controller;
static uint16_t test_pad;
void platform_test_set_pad(uint16_t mask) { test_pad = mask; }

/* Global mode flags */
bool platform_headless = false;
bool platform_skip_intro = false;
int platform_max_frames = 0;

/* Maps the 12 rebindable buttons to the game's pad mask. */
static const uint16_t bind_to_pad[BIND_COUNT] = {
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R, PAD_START, PAD_SELECT,
};

static void signal_handler(int sig) {
    (void)sig;
    quit_requested = 1;
}

static void open_first_controller(void) {
    if (controller) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                fprintf(stderr, "input: gamepad '%s' connected\n", SDL_GameControllerName(controller));
                return;
            }
        }
    }
}

bool platform_input_init(void) {
    pad_state = 0;
    pad_prev = 0;
    aux_state = 0;
    quit_requested = 0;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (!platform_headless) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        open_first_controller();
    }
    return true;
}

void platform_input_shutdown(void) {
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
}

void menu_input_flush(void) {
    pad_state = 0;
    pad_prev = 0;
    aux_state = 0;
}

/* Called by menu_poll so controller hot-plug also works while a menu is open. */
void input_handle_device_event(const SDL_Event *e) {
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        open_first_controller();
    } else if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == e->cdevice.which) {
            SDL_GameControllerClose(controller);
            controller = NULL;
            fprintf(stderr, "input: gamepad disconnected\n");
            open_first_controller();
        }
    }
}

void input_screenshot_hotkey(void) {
    static int serial;
    char name[96], stamp[40];
    time_t t = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&t));
    snprintf(name, sizeof(name), "eb-native-%s-%02d.bmp", stamp, ++serial);
    if (video_screenshot(name)) fprintf(stderr, "screenshot saved: %s\n", name);
}

void platform_input_poll(void) {
    SDL_Event event;
    bool open_settings = false, open_debug = false;

    pad_prev = pad_state;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            quit_requested = 1;
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            input_handle_device_event(&event);
            break;
        case SDL_KEYDOWN:
            if (event.key.repeat) break;
            switch (event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE:
            case SDL_SCANCODE_F10: open_settings = true; break;
            case SDL_SCANCODE_F12: open_debug = true; break;
            case SDL_SCANCODE_F9:  input_screenshot_hotkey(); break;
            case SDL_SCANCODE_F11:
                cfg.window_mode = cfg.window_mode == WINMODE_WINDOWED ? WINMODE_BORDERLESS : WINMODE_WINDOWED;
                video_apply_config();
                break;
            case SDL_SCANCODE_RETURN:
                if (event.key.keysym.mod & KMOD_ALT) {
                    cfg.window_mode = cfg.window_mode == WINMODE_WINDOWED ? WINMODE_BORDERLESS : WINMODE_WINDOWED;
                    video_apply_config();
                }
                break;
            default: break;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            /* Guide/Home button opens the settings menu from a gamepad. */
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) open_settings = true;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                video_present_again();
            break;
        }
    }

    /* Current held state: rebuilt every poll so rebinding takes effect immediately. */
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    uint16_t pad = 0;
    for (int i = 0; i < BIND_COUNT; i++) {
        if (ks[cfg.key[i]]) pad |= bind_to_pad[i];
        if (controller && SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)cfg.pad[i]))
            pad |= bind_to_pad[i];
    }
    if (controller) {
        const int dz = 16000;
        int ax = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -dz) pad |= PAD_LEFT;
        if (ax > dz)  pad |= PAD_RIGHT;
        if (ay < -dz) pad |= PAD_UP;
        if (ay > dz)  pad |= PAD_DOWN;
    }
    pad_state = pad | test_pad;

    uint16_t aux = 0;
    if (ks[SDL_SCANCODE_F1])  aux |= AUX_DEBUG_DUMP;
    if (ks[SDL_SCANCODE_F2])  aux |= AUX_VRAM_DUMP;
    if (ks[SDL_SCANCODE_F3])  aux |= AUX_FPS_TOGGLE;
    if (ks[SDL_SCANCODE_F6])  aux |= AUX_SAVESTATE;
    if (ks[SDL_SCANCODE_F7])  aux |= AUX_LOAD_STATE;
    if (ks[SDL_SCANCODE_TAB]) aux |= AUX_FAST_FORWARD;
    if (ks[SDL_SCANCODE_F5])  aux |= AUX_DEBUG_TOGGLE;
    aux_state = aux;

    if (open_settings && !platform_headless) {
        settings_menu_run();
        menu_input_flush();
    } else if (open_debug && !platform_headless) {
        debug_menu_open();
        menu_input_flush();
    }
    if (!platform_headless) savestate_toast_poll();
}

uint16_t platform_input_get_pad(void) {
    return pad_state;
}

uint16_t platform_input_get_pad_new(void) {
    return pad_state & ~pad_prev;
}

uint16_t platform_input_get_aux(void) {
    return aux_state;
}

bool platform_input_quit_requested(void) {
    return quit_requested != 0;
}

void platform_request_quit(void) {
    quit_requested = 1;
}

/* Gamepad self-test: attaches an in-process virtual SDL gamepad and checks that buttons, the left
 * stick, user remapping and hot-unplug all reach the game's pad mask. Needs no real hardware. */
bool input_selftest_pad(void) {
    bool ok = true;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "pad selftest: failed: %s\n", #c); ok = false; } } while (0)
    if (SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "pad selftest: SDL init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.name = "EB-Native test pad";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    if (idx < 0) { fprintf(stderr, "pad selftest: no virtual joystick: %s\n", SDL_GetError()); return false; }
    SDL_Joystick *vj = SDL_JoystickOpen(idx);
    CHECK(vj != NULL);
    platform_input_init();           /* headless skips controller init, so open it here */
    SDL_PumpEvents();
    open_first_controller();
    CHECK(controller != NULL);

    cfg_set_defaults(&cfg);
    uint16_t p;
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 1);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_START, 1);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
    platform_input_poll(); p = platform_input_get_pad();
    CHECK((p & PAD_A) && (p & PAD_START) && (p & PAD_UP) && !(p & PAD_B));
    CHECK(platform_input_get_pad_new() & PAD_A);
    platform_input_poll();
    CHECK(!(platform_input_get_pad_new() & PAD_A));      /* held, no longer "new" */
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_START, 0);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_DPAD_UP, 0);

    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTX, 30000);
    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTY, -30000);
    platform_input_poll(); p = platform_input_get_pad();
    CHECK((p & PAD_RIGHT) && (p & PAD_UP) && !(p & PAD_LEFT) && !(p & PAD_DOWN));
    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTX, 5000);   /* inside the dead zone */
    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTY, 0);
    platform_input_poll(); CHECK((platform_input_get_pad() & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) == 0);

    cfg.pad[BIND_A] = SDL_CONTROLLER_BUTTON_X;                         /* remap: physical X now acts as A */
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 1);
    platform_input_poll(); CHECK(!(platform_input_get_pad() & PAD_A));
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_X, 1);
    platform_input_poll(); CHECK(platform_input_get_pad() & PAD_A);
    SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_X, 0);

    SDL_JoystickClose(vj);
    SDL_JoystickDetachVirtual(idx);
    platform_input_poll(); platform_input_poll();
    CHECK(controller == NULL);                                         /* unplug must not leave a stale handle */
    platform_input_poll(); CHECK(platform_input_get_pad() == 0);
    platform_input_shutdown();
#undef CHECK
    return ok;
}
