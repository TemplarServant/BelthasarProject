#import <AppKit/AppKit.h>
#include "macos.h"
#include "SDL_syswm.h"

void CtMacSetWindowAspect(SDL_Window* window, int width, int height) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot constrain the Mac window: %s", SDL_GetError());
        return;
    }
    // Constrain the picture area, excluding the title bar. SDL owns the window.
    info.info.cocoa.window.contentAspectRatio = width > 0 && height > 0
        ? NSMakeSize(width, height) : NSZeroSize;
}
