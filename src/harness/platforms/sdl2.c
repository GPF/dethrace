#include <SDL.h>

#include "harness.h"
#include "harness/config.h"
#include "harness/hooks.h"
#include "harness/trace.h"
#include "sdl2_scancode_to_dinput.h"
#include "sdl2_gamepad_to_dinput.h"
SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* screen_texture;
uint32_t converted_palette[256];
br_pixelmap* last_screen_src;
int render_width, render_height;

Uint32 last_frame_time;

uint8_t directinput_key_state[SDL_NUM_SCANCODES];
#include <kos.h>
static void* create_window_and_renderer(char* title, int x, int y, int width, int height) {
    // gdb_init();
    //dbgio_dev_select("fb");
    //SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    render_width = width;
    render_height = height;
    // SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "0");    
    //SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
    //SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO"); 
    if (SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO | SDL_INIT_JOYSTICK| SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_PANIC("SDL_INIT_VIDEO error: %s", SDL_GetError());
    }

    // if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
    //     LOG_WARN("SDL_INIT_GAMECONTROLLER error: %s", SDL_GetError());
    // }

    window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_FULLSCREEN_DESKTOP);

    if (window == NULL) {
        LOG_PANIC("Failed to create window: %s", SDL_GetError());
    }

    if (harness_game_config.start_full_screen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    
    // SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED); //SDL_RENDERER_PRESENTVSYNC
    if (renderer == NULL) {
        LOG_PANIC("Failed to create renderer: %s", SDL_GetError());
    }
    //printf("HERE\n");
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    //printf("HERE2\n");
    SDL_RenderSetLogicalSize(renderer, render_width, render_height);
    printf("Video res: width %d. height %d\n ", width, height);
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height); // 320x200
    //printf("HERE4\n");
    if (screen_texture == NULL) {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(renderer, &info);
        for (Uint32 i = 0; i < info.num_texture_formats; i++) {
            LOG_INFO("%s\n", SDL_GetPixelFormatName(info.texture_formats[i]));
        }
        LOG_PANIC("Failed to create screen_texture: %s", SDL_GetError());
    }
    printf("HERE5\n");
    return window;
}

static int set_window_pos(void* hWnd, int x, int y, int nWidth, int nHeight) {
// #ifndef __DREAMCAST__    
    // SDL_SetWindowPosition(hWnd, x, y);
    if (nWidth == 320 && nHeight == 200) {
        nWidth = 640;
        nHeight = 400;
    }
    SDL_SetWindowSize(hWnd, nWidth, nHeight);
// #endif    
    return 0;
}

static void destroy_window(void* hWnd) {
    // SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    window = NULL;
}

// Checks whether the `flag_check` is the only modifier applied.
// e.g. is_only_modifier(event.key.keysym.mod, KMOD_ALT) returns true when only the ALT key was pressed
static int is_only_key_modifier(int modifier_flags, int flag_check) {
    return (modifier_flags & flag_check) && (modifier_flags & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI)) == (modifier_flags & flag_check);
}

static int get_and_handle_message(MSG_* msg) {
    SDL_Event event;
    int dinput_key;

     while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                dinput_key = sdlScanCodeToDirectInputKeyNum[event.key.keysym.scancode];
                if (dinput_key != 0) {
                    directinput_key_state[dinput_key] = (event.type == SDL_KEYDOWN ? 0x80 : 0);
                }
                break;

            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(event.cdevice.which);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                dinput_key = sdlGamepadToDirectInputKeyNum.buttonMapping[event.cbutton.button];
                if (dinput_key != 0) {
                    directinput_key_state[dinput_key] = (event.type == SDL_CONTROLLERBUTTONDOWN ? 0x80 : 0);
                }
                break;

            case SDL_CONTROLLERAXISMOTION:
                if (event.caxis.value > 16000) {  // Axis positive
                    dinput_key = sdlGamepadToDirectInputKeyNum.axisPositive[event.caxis.axis];
                    if (dinput_key != 0) {
                        directinput_key_state[dinput_key] = 0x80;
                    }
                } else if (event.caxis.value < -16000) {  // Axis negative
                    dinput_key = sdlGamepadToDirectInputKeyNum.axisNegative[event.caxis.axis];
                    if (dinput_key != 0) {
                        directinput_key_state[dinput_key] = 0x80;
                    }
                } else {  // Reset when neutral
                    directinput_key_state[sdlGamepadToDirectInputKeyNum.axisPositive[event.caxis.axis]] = 0x00;
                    directinput_key_state[sdlGamepadToDirectInputKeyNum.axisNegative[event.caxis.axis]] = 0x00;
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    if (SDL_GetWindowID(window) == event.window.windowID) {
                        msg->message = WM_QUIT;
                        return 1;
                    }
                }
                break;

            case SDL_QUIT:
                msg->message = WM_QUIT;
                return 1;
        }
    }
    return 0;
}

static void get_keyboard_state(unsigned int count, uint8_t* buffer) {
    memcpy(buffer, directinput_key_state, count);
}

static int get_mouse_buttons(int* pButton1, int* pButton2) {
    if (SDL_GetMouseFocus() != window) {
        *pButton1 = 0;
        *pButton2 = 0;
        return 0;
    }
    int state = SDL_GetMouseState(NULL, NULL);
    *pButton1 = state & SDL_BUTTON_LMASK;
    *pButton2 = state & SDL_BUTTON_RMASK;
    return 0;
}

static int get_mouse_position(int* pX, int* pY) {
    float lX, lY;
    if (SDL_GetMouseFocus() != window) {
        return 0;
    }
    SDL_GetMouseState(pX, pY);
    SDL_RenderWindowToLogical(renderer, *pX, *pY, &lX, &lY);

#if defined(DETHRACE_FIX_BUGS)
    // In hires mode (640x480), the menus are still rendered at (320x240),
    // so prescale the cursor coordinates accordingly.
    lX *= 320;
    lX /= render_width;
    lY *= 200;
    lY /= render_height;
#endif
    *pX = (int)lX;
    *pY = (int)lY;
    return 0;
}

static void limit_fps(void) {
    Uint32 now = SDL_GetTicks();
    if (last_frame_time != 0) {
        unsigned int frame_time = now - last_frame_time;
        last_frame_time = now;
        if (frame_time < 100) {
            int sleep_time = (1000 / harness_game_config.fps) - frame_time;
            if (sleep_time > 5) {
                gHarness_platform.Sleep(sleep_time);
            }
        }
    }
    last_frame_time = SDL_GetTicks();
}

static void present_screen(br_pixelmap* src) {
    // fastest way to convert 8 bit indexed to 32 bit
    uint8_t* src_pixels = src->pixels;
    uint32_t* dest_pixels;
    int dest_pitch;

    SDL_LockTexture(screen_texture, NULL, (void**)&dest_pixels, &dest_pitch);
    for (int i = 0; i < src->height * src->width; i++) {
        *dest_pixels = converted_palette[*src_pixels];
        dest_pixels++;
        src_pixels++;
    }
    SDL_UnlockTexture(screen_texture);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
    //SDL_RenderCopyEx(renderer, screen_texture, NULL, NULL, 0, NULL, SDL_FLIP_VERTICAL | SDL_FLIP_HORIZONTAL);
    SDL_RenderPresent(renderer);

    last_screen_src = src;

    if (harness_game_config.fps != 0) {
        limit_fps();
    }
}

static void set_palette(PALETTEENTRY_* pal) {
    for (int i = 0; i < 256; i++) {
        converted_palette[i] = (0xff << 24 | pal[i].peBlue << 16 | pal[i].peGreen << 8 | pal[i].peRed);
    }
    if (last_screen_src != NULL) {
        present_screen(last_screen_src);
    }
}

int show_error_message(void* window, char* text, char* caption) {
    fprintf(stderr, "%s", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, caption, text, window);
    return 0;
}

void Harness_Platform_Init(tHarness_platform* platform) {
    platform->ProcessWindowMessages = get_and_handle_message;
    platform->Sleep = SDL_Delay;
    platform->GetTicks = SDL_GetTicks;
    platform->CreateWindowAndRenderer = create_window_and_renderer;
    platform->ShowCursor = SDL_ShowCursor;
    platform->SetWindowPos = set_window_pos;
    platform->DestroyWindow = destroy_window;
    platform->GetKeyboardState = get_keyboard_state;
    platform->GetMousePosition = get_mouse_position;
    platform->GetMouseButtons = get_mouse_buttons;
    platform->ShowErrorMessage = show_error_message;
    platform->Renderer_SetPalette = set_palette;
    platform->Renderer_Present = present_screen;
}
