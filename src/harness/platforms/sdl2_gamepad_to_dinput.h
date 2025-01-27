#include <SDL.h>

// Define a struct for mapping SDL buttons and axes to DirectInput keys
typedef struct {
    int buttonMapping[SDL_CONTROLLER_BUTTON_MAX];  // Button to DirectInput key
    int axisPositive[SDL_CONTROLLER_AXIS_MAX];     // Axis + direction to DirectInput key
    int axisNegative[SDL_CONTROLLER_AXIS_MAX];     // Axis - direction to DirectInput key
} GamepadMapping;

// Define the mappings
static GamepadMapping sdlGamepadToDirectInputKeyNum = {
    .buttonMapping = {
        [SDL_CONTROLLER_BUTTON_A] = 0x1C,  // SDL_SCANCODE_RETURN
        [SDL_CONTROLLER_BUTTON_B] = 0x4C,  // KP_5 (Handbrake)
        [SDL_CONTROLLER_BUTTON_X] = 0xD2,  // SDL_SCANCODE_INSERT -> Reset
        [SDL_CONTROLLER_BUTTON_Y] = 0x0E,  // SDL_SCANCODE_BACKSPACE -> Repair
        [SDL_CONTROLLER_BUTTON_START] = 0x01, // ESCAPE (Pause/Menu)
        [SDL_CONTROLLER_BUTTON_DPAD_UP] = 0x48, // UP
        [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = 0x50, // DOWN
        [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 0x4B, // LEFT
        [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = 0x4D  // RIGHT
    },
    .axisPositive = {
        [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0x48,  // SDL_SCANCODE_UP
        [SDL_CONTROLLER_AXIS_TRIGGERLEFT]  = 0x50,  // SDL_SCANCODE_DOWN
        [SDL_CONTROLLER_AXIS_LEFTX] = 0x4B, // SDL_SCANCODE_LEFT
        [SDL_CONTROLLER_AXIS_LEFTY] = 0x2E  // SDL_SCANCODE_C
    },
    .axisNegative = {
        [SDL_CONTROLLER_AXIS_LEFTX] = 0x0F, // SDL_SCANCODE_TAB
        [SDL_CONTROLLER_AXIS_LEFTY] = 0x48  // SDL_SCANCODE_UP
    }
};