#include "harness.h"
#include "harness/config.h"
#include "harness/hooks.h"
#include "harness/trace.h"

#include <kos.h>
 
#include <SDL.h>
#include "sdl2_scancode_to_dinput.h"
#include "sdl2_gamepad_to_dinput.h"

#define TEX_WIDTH         1024 // dhould be fine 
#define TEX_HEIGHT        512

static pvr_ptr_t  pvram          = NULL;
static uint32_t  *pvram_sq       = NULL;
static uint32_t   converted_palette[256];
static br_pixelmap *last_screen_src = NULL;
static int render_width  = 0;
static int render_height = 0;

static uint8_t directinput_key_state[SDL_NUM_SCANCODES];

static void dc_sleep(uint32_t ms) { thd_sleep(ms); }
static uint32_t dc_ticks(void) {
    return (uint32_t)(timer_ms_gettime64() & 0xFFFFFFFF);
}

static void* create_window_and_renderer(char* title, int x, int y, int width, int height) {
    (void)title; (void)x; (void)y;
    render_width  = width;
    render_height = height;

     pvr_init_defaults();

     pvram = pvr_mem_malloc(TEX_WIDTH * TEX_HEIGHT * 2);
     pvram_sq = (uint32_t*)(((uintptr_t)pvram & 0xFFFFFF) | PVR_TA_TEX_MEM);

 
     SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);

    return NULL; 
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
                if (event.caxis.value > 16000) {   
                    dinput_key = sdlGamepadToDirectInputKeyNum.axisPositive[event.caxis.axis];
                    if (dinput_key != 0) {
                        directinput_key_state[dinput_key] = 0x80;
                    }
                } else if (event.caxis.value < -16000) {   
                    dinput_key = sdlGamepadToDirectInputKeyNum.axisNegative[event.caxis.axis];
                    if (dinput_key != 0) {
                        directinput_key_state[dinput_key] = 0x80;
                    }
                } else {   
                    directinput_key_state[sdlGamepadToDirectInputKeyNum.axisPositive[event.caxis.axis]] = 0x00;
                    directinput_key_state[sdlGamepadToDirectInputKeyNum.axisNegative[event.caxis.axis]] = 0x00;
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
    if (count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
    memcpy(buffer, directinput_key_state, count);
}

static inline uint16_t rgba8888_to_rgb565(uint32_t rgba) {
    uint8_t r = (rgba >> 16) & 0xFF;
    uint8_t g = (rgba >>  8) & 0xFF;
    uint8_t b = (rgba & 0xFF);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static void fill_texture_rgb565(uint16_t color565) {
    uint32_t color32 = ((uint32_t)color565 << 16) | color565;
    uint32_t* dest32 = pvram_sq;
    size_t total32   = (TEX_WIDTH * TEX_HEIGHT) / 2;

    sq_lock(dest32);
    for (size_t i = 0; i < total32; i++) {
        dest32[i] = color32;
    }
    sq_unlock();
}

static void present_screen(br_pixelmap* src) {
    if (!pvram_sq) return;

    if (!src || !src->pixels) {
        fill_texture_rgb565(0x07E0); // bright green
    } else {
        uint8_t* src_pixels = (uint8_t*)src->pixels;
        int w = src->width, h = src->height;

        for (int y = 0; y < h; y++) {
            uint32_t* dest_line32 = pvram_sq + (TEX_WIDTH / 2) * y;
            sq_lock(dest_line32);
            for (int x = 0; x < w; x++) {
                uint8_t idx = src_pixels[x];
                uint16_t c = rgba8888_to_rgb565(converted_palette[idx]);
                ((uint16_t*)dest_line32)[x] = c;
            }
            sq_unlock();
            src_pixels += w;
        }
    }

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
        PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
        TEX_WIDTH, TEX_HEIGHT, pvram, PVR_FILTER_NONE);

    cxt.gen.alpha         = PVR_ALPHA_DISABLE;
    cxt.gen.culling       = PVR_CULLING_NONE;
    cxt.depth.comparison  = PVR_DEPTHCMP_ALWAYS;
    cxt.depth.write       = PVR_DEPTHWRITE_DISABLE;
    cxt.txr.env           = PVR_TXRENV_MODULATE;

    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.argb  = PVR_PACK_COLOR(1.f,1.f,1.f,1.f);
    vert.oargb = 0;
    vert.flags = PVR_CMD_VERTEX;

     
    vert.x = 0.0f; vert.y = 0.0f; vert.z = 1.0f;
    vert.u = 0.0f; vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    
    vert.x = 640.0f; vert.y = 0.0f;
    vert.u = (src && src->width)  ? (float)src->width / TEX_WIDTH  : 1.0f;
    vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    
    vert.x = 0.0f; vert.y = 480.0f;
    vert.u = 0.0f;
    vert.v = (src && src->height) ? (float)src->height / TEX_HEIGHT : 1.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 640.0f; vert.y = 480.0f;
    vert.u = (src && src->width)  ? (float)src->width / TEX_WIDTH  : 1.0f;
    vert.v = (src && src->height) ? (float)src->height / TEX_HEIGHT: 1.0f;
    vert.flags = PVR_CMD_VERTEX_EOL;
    pvr_prim(&vert, sizeof(vert));

    pvr_list_finish();
    pvr_scene_finish();

    last_screen_src = src;
}

static void set_palette(PALETTEENTRY_* pal) {
    for (int i = 0; i < 256; i++) {
        converted_palette[i] =
            (0xFF << 24) |
            ((pal[i].peRed   & 0xFF) << 16) |
            ((pal[i].peGreen & 0xFF) <<  8) |
            ( pal[i].peBlue  & 0xFF);
    }
    if (last_screen_src) {
        present_screen(last_screen_src);
    }
}

static int set_window_pos(void* hWnd, int x, int y, int nWidth, int nHeight) { return 0; }
static void destroy_window(void* hWnd) {}
static int is_only_key_modifier(int modifier_flags, int flag_check) { return 0; }
static int get_mouse_buttons(int* pButton1, int* pButton2) { *pButton1=0; *pButton2=0; return 0; }
static int get_mouse_position(int* pX, int* pY) { *pX=0; *pY=0; return 0; }
int show_error_message(void* window, char* text, char* caption) { return 0; }

void Harness_Platform_Init(tHarness_platform* platform) {
    platform->ProcessWindowMessages    = get_and_handle_message;
    platform->Sleep                    = dc_sleep;
    platform->GetTicks                 = dc_ticks;
    platform->CreateWindowAndRenderer  = create_window_and_renderer;
    platform->ShowCursor               = SDL_ShowCursor;
    platform->SetWindowPos             = set_window_pos;
    platform->DestroyWindow            = destroy_window;
    platform->GetKeyboardState         = get_keyboard_state;
    platform->GetMousePosition         = get_mouse_position;
    platform->GetMouseButtons          = get_mouse_buttons;
    platform->ShowErrorMessage         = show_error_message;
    platform->Renderer_SetPalette      = set_palette;
    platform->Renderer_Present         = present_screen;
}
