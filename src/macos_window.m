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
    NSWindow* nsWindow = info.info.cocoa.window;
    if (width > 0 && height > 0) {
        nsWindow.contentAspectRatio = NSMakeSize(width, height);
    } else {
        // Unit increments clear the ratio (NSWindow's documented reset). A zero ratio
        // made the frame restored on leaving fullscreen NaN tall, and AppKit threw.
        nsWindow.contentResizeIncrements = NSMakeSize(1.0, 1.0);
    }
}
