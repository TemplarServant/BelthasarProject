// Native macOS app integration; only included by Apple builds.
#ifndef CT_MACOS_H
#define CT_MACOS_H
#include "SDL.h"

int CtMacConfigure(int* argc, char*** argv);
// Constrain user resizing of the content area; zero dimensions release it.
void CtMacSetWindowAspect(SDL_Window* window, int width, int height);
void CtMacInstallSettingsMenu(void);
int CtMacTakeSettingsRequest(void);

#endif
