// android.c - see android.h. Built only for Android (CMakeLists.txt's ANDROID branch).
#include "android.h"
#include "SDL.h"
#include "SDL_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* const ROM_NAME = "Chrono_Trigger_USA_SNES.sfc";
static const char* const ASSETS_NAME = "ct_assets.dat";

static int readable(const char* dir, const char* name) {
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE* f = fopen(p, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static void defaultEnv(const char* key, const char* dir, const char* name) {
  if (getenv(key)) return;
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  setenv(key, p, 1);
}

int CtAndroidConfigure(int* outArgc, char*** outArgv) {
  // the phone's sensors are not a controller; the field's picture is landscape
  SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  const char* data = getenv("CT_DATA_DIR");
  if (!data || !*data) data = SDL_AndroidGetExternalStoragePath();
  if (!data || !*data) data = SDL_AndroidGetInternalStoragePath();
  if (!data || !readable(data, ROM_NAME) || !readable(data, ASSETS_NAME)) {
    char msg[1200];
    snprintf(msg, sizeof msg, "The game needs %s and %s in\n%s\n\nOpen the app again to choose them (docs/ANDROID.md).",
             ROM_NAME, ASSETS_NAME, data ? data : "(no storage)");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chrono Trigger", msg, NULL);
    return 1;
  }
  defaultEnv("CT_ROM", data, ROM_NAME);
  defaultEnv("CT_SRAM", data, "ct_sram.srm");
  defaultEnv("CT_SETTINGS", data, "ct_settings.ini");
  if (chdir(data) != 0) return 1;                  // the relative paths the engine still uses (the ROM, the log)
  { char log[1024];
    snprintf(log, sizeof log, "%s/latest.log", data);
    if (freopen(log, "w", stdout)) { dup2(fileno(stdout), fileno(stderr)); setvbuf(stdout, NULL, _IOLBF, 0); } }

  int argc = *outArgc;
  char** argv = *outArgv;
  char** args = (char**)calloc((size_t)argc + 4, sizeof(char*));
  if (!args) return 1;
  int n = 0, assetsGiven = 0;
  args[n++] = argc > 0 ? argv[0] : (char*)"ct_field";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--assets")) assetsGiven = 1;
    args[n++] = argv[i];
  }
  if (!assetsGiven) { args[n++] = (char*)"--assets"; args[n++] = (char*)ASSETS_NAME; }
  if (argc <= 1) args[n++] = (char*)"--boot";          // the cartridge's cold start: the title, then load or new game
  *outArgc = n;
  *outArgv = args;
  return 0;
}
