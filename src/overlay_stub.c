// overlay_stub.c - the build without third_party/imgui (tools/get_imgui.ps1 not run): no overlay. See overlay.h.
#include "overlay.h"

int  CtOverlayInit(CtPresenter* pr) { (void)pr; return 0; }
void CtOverlayShutdown(void) {}
int  CtOverlayAvailable(void) { return 0; }
int  CtOverlayProcessEvent(const SDL_Event* e) { (void)e; return 0; }
int  CtOverlayFrame(void) { return CT_OVL_CLOSE; }
void CtOverlayRender(void) {}
void CtOverlaySetPadName(const char* name) { (void)name; }
void CtOverlaySelectTab(int tab) { (void)tab; }
