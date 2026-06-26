#include <SDL.h>

#include "harness.h"
#include "harness/config.h"
#include "harness/hooks.h"
#include "harness/trace.h"
#include "sdl2_scancode_to_dinput.h"
#include "sdl2_gamepad_to_dinput.h"

#include <stdint.h>

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* screen_texture;
// uint32_t converted_palette[256];
br_pixelmap* last_screen_src;
int render_width, render_height;

Uint32 last_frame_time;

uint8_t directinput_key_state[SDL_NUM_SCANCODES];

#ifdef __DREAMCAST__
#define DC_FRAMEBUFFER_WIDTH 320
#define DC_FRAMEBUFFER_HEIGHT 240
#endif

static void* create_window_and_renderer(char* title, int x, int y, int width, int height) {
    render_width = width;
    render_height = height;

#ifdef __DREAMCAST__
    int window_width = DC_FRAMEBUFFER_WIDTH;
    int window_height = DC_FRAMEBUFFER_HEIGHT;
    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN;

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");    
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_STRIDED_VIDEO");
#else
    int window_width = width;
    int window_height = height;
    Uint32 window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

    if (SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO | SDL_INIT_JOYSTICK| SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_PANIC("SDL_INIT_VIDEO error: %s", SDL_GetError());
    }

    window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width, window_height,
        window_flags);
    if (window == NULL) {
        LOG_PANIC("Failed to create window: %s", SDL_GetError());
    }

#ifdef __DREAMCAST__
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (surface == NULL) {
        LOG_PANIC("Failed to create Dreamcast window framebuffer: %s", SDL_GetError());
    }
    printf("Video res: width %d. height %d\n", surface->w, surface->h);
    return window;
#else
    if (harness_game_config.start_full_screen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL) {
        LOG_PANIC("Failed to create renderer: %s", SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(renderer, render_width, render_height);
    printf("Video res: width %d. height %d\n", width, height);
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING, width, height); // 320x200
    if (screen_texture == NULL) {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(renderer, &info);
        for (Uint32 i = 0; i < info.num_texture_formats; i++) {
            LOG_INFO("%s\n", SDL_GetPixelFormatName(info.texture_formats[i]));
        }
        LOG_PANIC("Failed to create screen_texture: %s", SDL_GetError());
    }
    return window;
#endif
}

static int set_window_pos(void* hWnd, int x, int y, int nWidth, int nHeight) {
#ifdef __DREAMCAST__
    return 0;
#else
    // SDL_SetWindowPosition(hWnd, x, y);
    if (nWidth == 320 && nHeight == 200) {
        nWidth = 640;
        nHeight = 400;
    }
    SDL_SetWindowSize(hWnd, nWidth, nHeight);
    return 0;
#endif
}

static void destroy_window(void* hWnd) {
    if (screen_texture != NULL) {
        SDL_DestroyTexture(screen_texture);
        screen_texture = NULL;
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
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
    if (renderer != NULL) {
        SDL_RenderWindowToLogical(renderer, *pX, *pY, &lX, &lY);
    } else {
        int window_w;
        int window_h;
        SDL_GetWindowSize(window, &window_w, &window_h);
        lX = window_w != 0 ? (float)*pX * render_width / window_w : 0.0f;
        lY = window_h != 0 ? (float)*pY * render_height / window_h : 0.0f;
    }

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

static uint16_t converted_palette[256];

static void present_screen(br_pixelmap* src) {
#ifdef __DREAMCAST__
    SDL_Surface* surface;
    int copy_w;
    int copy_h;
    int y_offset;

    if (window == NULL || src == NULL || src->pixels == NULL) {
        return;
    }

    surface = SDL_GetWindowSurface(window);
    if (surface == NULL) {
        LOG_WARN("SDL_GetWindowSurface failed: %s", SDL_GetError());
        return;
    }

    if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
        LOG_WARN("SDL_LockSurface failed: %s", SDL_GetError());
        return;
    }

    memset(surface->pixels, 0, surface->h * surface->pitch);

    copy_w = src->width < DC_FRAMEBUFFER_WIDTH ? src->width : DC_FRAMEBUFFER_WIDTH;
    copy_h = src->height < DC_FRAMEBUFFER_HEIGHT ? src->height : DC_FRAMEBUFFER_HEIGHT;
    y_offset = (surface->h - copy_h) / 2;

    for (int y = 0; y < copy_h; y++) {
        const uint8_t* src_row = (const uint8_t*)src->pixels + y * src->row_bytes;
        uint16_t* dst_row = (uint16_t*)((uint8_t*)surface->pixels + (y + y_offset) * surface->pitch);

        for (int x = 0; x < copy_w; x++) {
            dst_row[x] = converted_palette[src_row[x]];
        }
    }

    if (SDL_MUSTLOCK(surface)) {
        SDL_UnlockSurface(surface);
    }
    SDL_UpdateWindowSurface(window);
#else
    SDL_UpdateTexture(screen_texture, NULL, src->pixels, src->row_bytes);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
#endif
}

static void set_palette(PALETTEENTRY_* pal) {
    for (int i = 0; i < 256; i++) {
        uint16_t red = (pal[i].peRed >> 3) & 0x1F;
        uint16_t green = (pal[i].peGreen >> 3) & 0x1F;
        uint16_t blue = (pal[i].peBlue >> 3) & 0x1F;

        converted_palette[i] = 0x8000 | (red << 10) | (green << 5) | blue;
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
