// android.c - see android.h. Built only for Android (CMakeLists.txt's ANDROID branch).
#include "android.h"
#include "assets.h"
#include "datapack.h"
#include "SDL.h"
#include "SDL_system.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* const ASSETS_NAME = "ct_assets.dat";

static int readable(const char* dir, const char* name) {
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE* f = fopen(p, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int assetsOk(const char* dir) {             // CtAssetsOpen: the header, the directory, every blob in the file
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", dir, ASSETS_NAME);
  CtAssets a;
  if (!CtAssetsOpen(&a, p)) return 0;
  CtAssetsClose(&a);
  return 1;
}

static int tablesThere(const char* dir) {          // inside ct_assets.dat (today's setup), or an earlier one's ct_tables.dat
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", dir, ASSETS_NAME);
  if (CtDataPackFileHasTables(p)) return 1;
  snprintf(p, sizeof p, "%s/ct_tables.dat", dir);
  return CtDataPackFileHasTables(p);
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
  // The setup activity keeps only files that pass these same checks; a file changed behind its back (adb push) is
  // refused here with a message instead of reaching the engine. The app is a DATA-FREE build (android/CMakeLists.cmake):
  // a ROM-free ct_assets.dat with the tables inside (docs/ROM.md) is all it reads -- it never takes a ROM.
  char ap[1024];
  snprintf(ap, sizeof ap, "%s/%s", data ? data : ".", ASSETS_NAME);
  const char* bad = !data ? "no storage"
                  : !readable(data, ASSETS_NAME) ? "missing"
                  : !assetsOk(data) ? "ct_assets.dat is damaged or not one"
                  : !CtAssetsFileRomFree(ap) ? "an older ct_assets.dat that needs the ROM -- make a new one"
                  : !tablesThere(data) ? "it has no tables -- make a new one with today's Belthasar" : NULL;
  if (bad) {
    char msg[1200];
    snprintf(msg, sizeof msg, "The game needs %s in\n%s\n(%s).\n\nOpen the app again to choose it "
             "(docs/ANDROID.md).", ASSETS_NAME, data ? data : "(no storage)", bad);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Belthasar", msg, NULL);
    return 1;
  }
  defaultEnv("CT_SRAM", data, "ct_sram.srm");
  defaultEnv("CT_SETTINGS", data, "ct_settings.ini");
  if (chdir(data) != 0) return 1;                  // the relative paths the engine still uses (ct_wide.dat, the log)
  { char log[1024];
    snprintf(log, sizeof log, "%s/latest.log", data);
    if (freopen(log, "w", stdout)) { dup2(fileno(stdout), fileno(stderr)); setvbuf(stdout, NULL, _IOLBF, 0); } }

  int argc = *outArgc;
  char** argv = *outArgv;
  char** args = (char**)calloc((size_t)argc + 4, sizeof(char*));
  if (!args) return 1;
  int n = 0, assetsGiven = 0;
  args[n++] = argc > 0 ? argv[0] : (char*)"Belthasar";
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
