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
// uint32_t converted_palette[256];
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
    // SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");    
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO"); 
    if (SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO | SDL_INIT_JOYSTICK| SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_PANIC("SDL_INIT_VIDEO error: %s", SDL_GetError());
    }
    SDL_ShowCursor(SDL_DISABLE);
    // if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
    //     LOG_WARN("SDL_INIT_GAMECONTROLLER error: %s", SDL_GetError());
    // }

    window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    printf("here6\n");
    if (window == NULL) {
        LOG_PANIC("Failed to create window: %s", SDL_GetError());
    }

    if (harness_game_config.start_full_screen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    printf("here7\n");
    // SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_SOFTWARE); //SDL_RENDERER_PRESENTVSYNC
    if (renderer == NULL) {
        LOG_PANIC("Failed to create renderer: %s", SDL_GetError());
    }
    //printf("HERE\n");
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    //printf("HERE2\n");
    SDL_RenderSetLogicalSize(renderer, render_width, render_height);
    printf("Video res: width %d. height %d\n ", width, height);
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING, width, height); // 320x200
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

static uint16_t converted_palette[256]; // Change to 16-bit for RGB565

static void present_screen(br_pixelmap* src) {
    #define VRAM_B(n) ((void *)(((uintptr_t)PVR_TA_TEX_MEM_32) + ((uintptr_t)vram_s - PVR_RAM_BASE) + (n))) // Fixed missing parenthesis
    #define SQ_WRITE(sq, off) do { \
            const uint16_t value = converted_palette[src_pixels[r * 320 + c + off]]; \
            sq[r * 640 + c + off] = (value << 16) | value; \
        } while(0)
    
    const uint8_t* src_pixels = src->pixels;
    uint32_t *sq = sq_lock(VRAM_B(40 * 640 * 2));
    uint32_t *sq2 = SQ_MASK_DEST(VRAM_B(41 * 640 * 2));
    
    for(int r = 0; r < 200; ++r) {
        int c;
        for (c = 0; c <= 320 - 32; c += 32) {
            dcache_pref_block(&src_pixels[r * 320 + c + 32]);
    
            SQ_WRITE(sq, 0);
            SQ_WRITE(sq, 1);
            SQ_WRITE(sq, 2);
            SQ_WRITE(sq, 3);
            SQ_WRITE(sq, 4);
            SQ_WRITE(sq, 5);
            SQ_WRITE(sq, 6);
            SQ_WRITE(sq, 7);
            sq_flush(&sq[r * 640 + c + 0]);
            sq_flush(&sq2[r * 640 + c + 0]);
    
            SQ_WRITE(sq, 8);
            SQ_WRITE(sq, 9);
            SQ_WRITE(sq,10);
            SQ_WRITE(sq,11);
            SQ_WRITE(sq,12);
            SQ_WRITE(sq,13);
            SQ_WRITE(sq,14);
            SQ_WRITE(sq,15);
            sq_flush(&sq[r * 640 + c + 8]);
            sq_flush(&sq2[r * 640 + c + 8]);
    
            SQ_WRITE(sq, 16);
            SQ_WRITE(sq, 17);
            SQ_WRITE(sq, 18);
            SQ_WRITE(sq, 19);
            SQ_WRITE(sq, 20);
            SQ_WRITE(sq, 21);
            SQ_WRITE(sq, 22);
            SQ_WRITE(sq, 23);
            sq_flush(&sq[r * 640 + c + 16]);
            sq_flush(&sq2[r * 640 + c + 16]);
    
            SQ_WRITE(sq, 24);
            SQ_WRITE(sq, 25);
            SQ_WRITE(sq, 26);
            SQ_WRITE(sq, 27);
            SQ_WRITE(sq, 28);
            SQ_WRITE(sq, 29);
            SQ_WRITE(sq, 30);
            SQ_WRITE(sq, 31);
            sq_flush(&sq[r * 640 + c + 24]);
            sq_flush(&sq2[r * 640 + c + 24]);
        }
    }
    sq_unlock();
}

static void set_palette(PALETTEENTRY_* pal) {
    for (int i = 0; i < 256; i++) {
        // Convert 8-bit color components to 5-6-5 RGB565 format
        uint16_t red = (pal[i].peRed >> 3) & 0x1F;   // 5 bits for red
        uint16_t green = (pal[i].peGreen >> 2) & 0x3F; // 6 bits for green
        uint16_t blue = (pal[i].peBlue >> 3) & 0x1F;  // 5 bits for blue

        // Pack into 16-bit RGB565 format
        converted_palette[i] = (red << 11) | (green << 5) | blue;
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
