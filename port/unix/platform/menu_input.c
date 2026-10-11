#include "platform/menu_input.h"
#include "platform/platform.h"
#include "platform/video.h"
#include <SDL.h>

void input_handle_device_event(const SDL_Event *e); /* sdl2_input.c */
void input_screenshot_hotkey(void);                 /* sdl2_input.c */

void menu_poll(menu_in_t *in) {
    SDL_Event e;
    *in = (menu_in_t){ .pad_button = -1 };
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            platform_request_quit();
            in->quit = true;
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            input_handle_device_event(&e);
            break;
        case SDL_WINDOWEVENT:
            in->redraw = true;
            break;
        case SDL_TEXTINPUT:
            if (e.text.text[0] >= 32 && e.text.text[0] < 127) in->text_char = e.text.text[0];
            break;
        case SDL_KEYDOWN: {
            SDL_Scancode sc = e.key.keysym.scancode;
            if (!e.key.repeat) in->key = sc;
            if (sc == SDL_SCANCODE_F9 && !e.key.repeat) { input_screenshot_hotkey(); in->key = 0; break; }
            switch (sc) {
            case SDL_SCANCODE_UP:    in->up = true; break;
            case SDL_SCANCODE_DOWN:  in->down = true; break;
            case SDL_SCANCODE_LEFT:  in->left = true; break;
            case SDL_SCANCODE_RIGHT: in->right = true; break;
            case SDL_SCANCODE_PAGEUP:   in->page_prev = true; break;
            case SDL_SCANCODE_PAGEDOWN: in->page_next = true; break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_Z:
                if (!e.key.repeat) in->confirm = true;
                break;
            case SDL_SCANCODE_ESCAPE:
            case SDL_SCANCODE_X:
                if (!e.key.repeat) in->back = true;
                break;
            default: break;
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            in->pad_button = e.cbutton.button;
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    in->up = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  in->down = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  in->left = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in->right = true; break;
            case SDL_CONTROLLER_BUTTON_A:          in->confirm = true; break;
            case SDL_CONTROLLER_BUTTON_B:          in->back = true; break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  in->page_prev = true; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: in->page_next = true; break;
            default: break;
            }
            break;
        }
    }
}
