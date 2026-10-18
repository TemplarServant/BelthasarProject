// macOS bundle paths and first-launch game-data selection.
#import <AppKit/AppKit.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NSString* const RomName = @"Chrono_Trigger_USA_SNES.sfc";

static BOOL hasGameData(NSString* directory) {
    if (!directory.length) return NO;
    NSFileManager* fm = [NSFileManager defaultManager];
    return [fm isReadableFileAtPath:[directory stringByAppendingPathComponent:RomName]] &&
           [fm isReadableFileAtPath:[directory stringByAppendingPathComponent:@"ct_assets.dat"]];
}

static void showError(NSString* message) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Belthasar could not start";
    alert.informativeText = message;
    [alert runModal];
}

static void defaultEnv(const char* key, NSString* path) {
    if (!getenv(key)) setenv(key, path.fileSystemRepresentation, 1);
}

int CtMacConfigure(int* outArgc, char*** outArgv) {
    int argc = *outArgc;
    char** argv = *outArgv;
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        // Direct command-line builds retain all existing relative-path behavior.
        if (![bundle.bundlePath.pathExtension isEqualToString:@"app"]) return 0;
        NSString* support = [NSSearchPathForDirectoriesInDomains(
            NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
            stringByAppendingPathComponent:@"BelthasarProject"];
        NSError* error = nil;
        if (![[NSFileManager defaultManager] createDirectoryAtPath:support
                withIntermediateDirectories:YES attributes:nil error:&error]) {
            showError(error.localizedDescription);
            return 1;
        }
        defaultEnv("CT_SRAM", [support stringByAppendingPathComponent:@"ct_sram.srm"]);
        defaultEnv("CT_SETTINGS", [support stringByAppendingPathComponent:@"ct_settings.ini"]);

        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        NSString* data = nil;
        if (getenv("CT_DATA_DIR")) {
            data = [NSString stringWithUTF8String:getenv("CT_DATA_DIR")];
            if (!hasGameData(data)) {
                showError(@"CT_DATA_DIR must contain Chrono_Trigger_USA_SNES.sfc and ct_assets.dat.");
                return 1;
            }
        } else {
            NSArray* candidates = @[
                [bundle.resourcePath stringByAppendingPathComponent:@"GameData"],
                [support stringByAppendingPathComponent:@"GameData"],
                [defaults stringForKey:@"GameDataDirectory"] ?: @""];
            for (NSString* candidate in candidates) {
                if (hasGameData(candidate)) { data = candidate; break; }
            }
        }
        if (!data) {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.title = @"Choose your Chrono Trigger game-data folder";
            panel.message = @"Choose the folder containing Chrono_Trigger_USA_SNES.sfc and "
                @"ct_assets.dat generated from your ROM by tools/setup.py.";
            panel.canChooseDirectories = YES;
            panel.canChooseFiles = NO;
            panel.allowsMultipleSelection = NO;
            if ([panel runModal] != NSModalResponseOK) return 2;
            data = panel.URL.path;
            if (!hasGameData(data)) {
                showError(@"This folder needs Chrono_Trigger_USA_SNES.sfc and ct_assets.dat. "
                          @"Run tools/setup.py with your US ROM first (see docs/MACOS.md).");
                return 1;
            }
            [defaults setObject:data forKey:@"GameDataDirectory"];
        }
        if (!data.isAbsolutePath) {
            data = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:data];
        }
        defaultEnv("CT_ROM", [data stringByAppendingPathComponent:RomName]);
        // Some legacy loaders still use the canonical relative ROM filename.
        if (chdir(data.fileSystemRepresentation) != 0) {
            showError([NSString stringWithFormat:@"Cannot open game-data folder: %s", strerror(errno)]);
            return 1;
        }
        if (argc == 1 && !isatty(STDOUT_FILENO)) {
            NSString* log = [support stringByAppendingPathComponent:@"latest.log"];
            if (freopen(log.fileSystemRepresentation, "w", stdout)) dup2(STDOUT_FILENO, STDERR_FILENO);
        }
        char** args = calloc((size_t)argc + 4, sizeof(char*));
        if (!args) return 1;
        args[0] = argv[0];
        int count = 1;
        BOOL assetsGiven = NO;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--assets")) assetsGiven = YES;
            args[count++] = argv[i];
        }
        if (!assetsGiven) {
            args[count++] = "--assets";
            args[count++] = strdup([data stringByAppendingPathComponent:@"ct_assets.dat"].fileSystemRepresentation);
            if (!args[count - 1]) { free(args); return 1; }
        }
        if (argc == 1) args[count++] = "--boot";
        *outArgc = count;
        *outArgv = args;
        return 0;
    }
}
