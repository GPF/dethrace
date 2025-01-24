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
        [SDL_CONTROLLER_BUTTON_A] = 0x48,  // KP_8 (Accelerate - fallback)
        [SDL_CONTROLLER_BUTTON_B] = 0x4C,  // KP_5 (Handbrake)
        [SDL_CONTROLLER_BUTTON_X] = 0x01,  // SPACEBAR (Action)
        [SDL_CONTROLLER_BUTTON_Y] = 0x1C,  // 'C' (Cockpit Toggle)
        [SDL_CONTROLLER_BUTTON_START] = 0x39, // ESCAPE (Pause/Menu)
        [SDL_CONTROLLER_BUTTON_DPAD_UP] = 0x48, // UP
        [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = 0xD0, // DOWN
        [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 0x4B, // LEFT
        [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = 0x4D  // RIGHT
    },
    .axisPositive = {
        [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0x48,  // KP_8 (Accelerate)
        [SDL_CONTROLLER_AXIS_TRIGGERLEFT]  = 0x4C,  // KP_2 (Brake)
        [SDL_CONTROLLER_AXIS_LEFTX] = 0x12, // 'E' (Look Right)
        [SDL_CONTROLLER_AXIS_LEFTY] = 0x2E  // 'C' (Cockpit Toggle)
    },
    .axisNegative = {
        [SDL_CONTROLLER_AXIS_LEFTX] = 0x10, // 'Q' (Look Left)
        [SDL_CONTROLLER_AXIS_LEFTY] = 0x11  // 'W' (Look Forward)
    }
};