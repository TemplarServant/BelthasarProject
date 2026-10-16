// overlay.h - the F1 options overlay (Dear ImGui over the game's picture). C interface; src/overlay.cpp implements it
// when third_party/imgui is present (CT_HAVE_IMGUI), src/overlay_stub.c otherwise (no overlay, F1 does nothing).
// The host (field.c) runs it MODAL: the game stands still while it is open, and the options it edits are
// settings.h's.
#ifndef CT_OVERLAY_H
#define CT_OVERLAY_H
#include "SDL.h"
#include "present.h"
#ifdef __cplusplus
extern "C" {
#endif

// What a frame of the overlay changed, for the host to apply.
enum {
  CT_OVL_VIDEO   = 1,   // picture options (scale, aspect, filter, vsync, fullscreen, window size)
  CT_OVL_CLOSE   = 2,   // the player closed the overlay
  CT_OVL_QUIT    = 4,   // the player asked to leave the game
  CT_OVL_RESTART = 8,   // an option that takes effect on the next start changed (the render driver)
};

int  CtOverlayInit(CtPresenter* pr);                      // 1 = the overlay is available (on pr's backend)
void CtOverlayShutdown(void);
int  CtOverlayAvailable(void);
// 1 = the overlay consumed the event (a key being bound, typing in a widget): the host must not act on it.
int  CtOverlayProcessEvent(const SDL_Event* e);
// Build this frame's options window; returns CT_OVL_* flags.
int  CtOverlayFrame(void);
void CtOverlayRender(void);                               // draw it (after the host drew the game picture)
void CtOverlaySetPadName(const char* name);               // the connected controller, or NULL
void CtOverlaySelectTab(int tab);                         // (CT_OVERLAY_SHOT) open on tab 0..3 next frame
// The virtual gamepad (touchpad.h) over the picture the host just drew, outside the modal overlay: a frame of its own.
struct CtTouchDraw;
void CtOverlayTouchDraw(const struct CtTouchDraw* d);
#ifdef __cplusplus
}
#endif
#endif
