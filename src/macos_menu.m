#import <AppKit/AppKit.h>
#include "macos.h"

static BOOL settingsRequested;

@interface CtMacMenuActions : NSObject
- (void)showSettings:(id)sender;
@end

@implementation CtMacMenuActions
- (void)showSettings:(id)sender {
    // The SDL loop opens the existing overlay after Cocoa finishes the action.
    settingsRequested = YES;
}
@end

// NSMenuItem does not retain its target.
static CtMacMenuActions* menuActions;

void CtMacInstallSettingsMenu(void) {
    NSMenu* menu = NSApp.mainMenu.itemArray.firstObject.submenu;
    if (!menu || menuActions) return;
    menuActions = [CtMacMenuActions new];
    NSMenuItem* settings = nil;
    // SDL supplies a disabled Settings/Preferences item; reuse its position.
    for (NSMenuItem* item in menu.itemArray) {
        if (!item.action && [item.keyEquivalent isEqualToString:@","]) {
            settings = item;
            break;
        }
    }
    if (!settings) {
        settings = [[NSMenuItem alloc] initWithTitle:@"Settings…"
            action:@selector(showSettings:) keyEquivalent:@""];
        NSInteger index = NSApp.servicesMenu ? [menu indexOfItemWithSubmenu:NSApp.servicesMenu] : -1;
        if (index < 0) index = 0;
        [menu insertItem:settings atIndex:index];
        [menu insertItem:[NSMenuItem separatorItem] atIndex:index + 1];
    }
    settings.title = @"Settings…";
    settings.action = @selector(showSettings:);
    settings.target = menuActions;
    settings.enabled = YES;
    settings.keyEquivalent = @"";
}

int CtMacTakeSettingsRequest(void) {
    int requested = settingsRequested;
    settingsRequested = NO;
    return requested;
}
