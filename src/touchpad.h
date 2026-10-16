// touchpad.h - the on-screen VIRTUAL GAMEPAD (a player option, settings.h `touchPad`; on by default on Android only).
//
// A HOST feature, like the keyboard and the controller: the fingers on the screen become the SNES controller's twelve
// buttons (settings.h CT_BTN_*), which field.c folds into the same pad word the keyboard and a physical pad produce
// (fieldKeyPad: the level; its edges the same way the left stick's are made). Nothing here reaches the game in any
// other way. It is drawn over the picture by the overlay (overlay.h CtOverlayTouchDraw) and hidden while a physical
// controller is in use (from its first press until the next touch). A headless run never sees a finger, and the option is off by default off Android, so the
// desktop game and every regression run are unchanged.
//
// Layout (landscape; the picture sits in the middle, the pad in the side bars): the d-pad bottom left, A/B/X/Y as on
// the SNES pad (A right, B bottom, X top, Y left) bottom right, L and R in the top corners with Select under L and
// Start under R, and a small options button under Select (it opens the F1 overlay).
#ifndef CT_TOUCHPAD_H
#define CT_TOUCHPAD_H
#include "SDL.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { CT_TOUCH_SHAPE_CIRCLE = 0, CT_TOUCH_SHAPE_RECT = 1, CT_TOUCH_SHAPE_DPAD = 2 };
#define CT_TOUCH_MENU (1u << 31)   // CtTouchPadEvent: the options button was tapped

// One control as it is drawn, in output pixels.
typedef struct CtTouchControl {
  int shape;                 // CT_TOUCH_SHAPE_*
  float x, y;                // the centre
  float w, h;                // a circle's diameter is w; the d-pad's is w
  const char* label;
  uint32_t mask;             // the buttons it presses (1 << CT_BTN_*), or CT_TOUCH_MENU
  int pressed;
} CtTouchControl;

#define CT_TOUCH_MAX_CONTROLS 12
typedef struct CtTouchDraw {
  CtTouchControl c[CT_TOUCH_MAX_CONTROLS];
  int n;
  float alpha;               // 0..1, the player's opacity
  float outW, outH;          // the drawable the coordinates are in (CtTouchPadSetOutput)
  uint32_t dpadDirs;         // the d-pad's held directions (1 << CT_BTN_UP .. CT_BTN_RIGHT), for drawing them lit
} CtTouchDraw;

// The pad is in use: the option is on and no physical controller is in use (`padInUse`: a button or the stick moved
// since the last touch -- a phone's own keys may enumerate as a controller, so being connected does not count).
int      CtTouchPadEnabled(int padInUse);
// The drawable's size, in pixels (the layout is made from it). Call before the events and the draw.
void     CtTouchPadSetOutput(int w, int h);
// SDL_FINGERDOWN / SDL_FINGERMOTION / SDL_FINGERUP: returns the buttons a NEW touch went down on (CT_TOUCH_MENU for
// the options button), 0 otherwise.
uint32_t CtTouchPadEvent(const SDL_Event* e);
uint32_t CtTouchPadButtons(void);   // the buttons held now (1 << CT_BTN_*)
void     CtTouchPadReset(void);     // forget every finger (the options overlay opened / closed, the app paused)
// Once per frame, before the events: a tap that went down and up inside the last frame has been held for that frame.
void     CtTouchPadNewFrame(void);
void     CtTouchPadBuild(CtTouchDraw* d);   // the controls to draw, with what is pressed

#ifdef __cplusplus
}
#endif
#endif
