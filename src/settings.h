// settings.h - the player's options for Belthasar: what the F1 overlay edits and ct_settings.ini keeps.
//
// The options are the HOST's (window, picture, sound volume, controls, how strictly the SNES's timing is kept); the
// game's own settings (battle speed, the Config screen's button map, Stereo/Mono) stay in the game's SRAM as on the
// cartridge. Defaults are the behaviour Belthasar had before the file existed, so a missing or partial file changes
// nothing.
//
// HEADLESS RUNS NEVER READ THE FILE (CtSettingsLoad's `interactive` = 0: --selftest, CT_NOPACE, a dummy video driver):
// the regression suite's outputs must not depend on what a player left in ct_settings.ini. CT_SETTINGS=<path> loads a
// file explicitly, headless or not.
#ifndef CT_SETTINGS_H
#define CT_SETTINGS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// the SNES controller's twelve buttons, in the order the bindings are kept and shown
enum {
  CT_BTN_UP, CT_BTN_DOWN, CT_BTN_LEFT, CT_BTN_RIGHT,
  CT_BTN_A, CT_BTN_B, CT_BTN_X, CT_BTN_Y, CT_BTN_L, CT_BTN_R, CT_BTN_SELECT, CT_BTN_START,
  CT_BTN_COUNT
};
#define CT_BIND_KEYS 2          // keyboard keys per button (SDL scancodes; 0 = none)

enum { CT_SCALE_FIT = 0, CT_SCALE_INTEGER = 1, CT_SCALE_STRETCH = 2 };
enum { CT_ASPECT_PIXEL = 0, CT_ASPECT_4_3 = 1 };          // 256x224 square pixels (8:7) / a 4:3 television
enum { CT_SYNC_FRAME_PERFECT = 0, CT_SYNC_FREE = 1 };
enum { CT_WIDE_OFF = 0, CT_WIDE_16_9 = 1, CT_WIDE_16_10 = 2, CT_WIDE_21_9 = 3 };

typedef struct CtSettings {
  // video
  int fullscreen;               // 0 window, 1 desktop fullscreen
  int windowScale;              // the window's starting size, x the 256x224 picture
  int scaleMode;                // CT_SCALE_*
  int aspect;                   // CT_ASPECT_*
  int linearFilter;             // 0 nearest (sharp pixels), 1 linear (smooth)
  int renderScale;              // the internal picture, x 256x224 (1..4): the field's sub-pixel positions (motion.h)
  int interpolate;              // the frames between: the field drawn at the screen's rate between two 60 Hz frames
  int widescreen;               // CT_WIDE_*: the field's picture wider than the SNES's 256 (the rest pillarboxed)
  int wideRoomWrap;             // widescreen: a room as wide as its map shows the map wrapping in the sides (else black)
  int vsync;
  int backend;                  // present.h CT_BACKEND_*: SDL_Renderer / OpenGL / Vulkan (next start)
  char renderDriver[32];        // SDL_Renderer driver ("" = SDL's choice; "direct3d", "direct3d11", "opengl", "software")
  // sound
  int volume;                   // 0..100, the host's output gain
  int mute;
  // controls
  int keys[CT_BTN_COUNT][CT_BIND_KEYS];   // SDL_Scancode per button
  int pad[CT_BTN_COUNT];                  // SDL_GameControllerButton per button (-1 = none)
  int padStick;                 // the left stick also drives the d-pad
  // the on-screen virtual gamepad (touchpad.h): a host option, on by default on Android only
  int touchPad;                 // shown while no controller is connected
  int touchOpacity;             // 10..100 (%)
  int touchScale;               // 50..150 (%), the controls' size
  // how the game keeps the SNES's timing
  int sync;                     // CT_SYNC_*
} CtSettings;

CtSettings* CtSettingsGet(void);                 // the live options (defaults until CtSettingsLoad)
void        CtSettingsDefaults(CtSettings* s);
// Reads the file (CT_SETTINGS, else ct_settings.ini next to the executable) when `interactive`; returns 1 if a file
// was read. A headless run keeps the defaults.
int         CtSettingsLoad(int interactive);
int         CtSettingsSave(void);                // writes the file it loaded from (or the default path); 1 = written
const char* CtSettingsPath(void);
const char* CtSettingsButtonName(int btn);        // "Up" .. "Start"
#ifdef __cplusplus
}
#endif
#endif
