// launcher_ui_stub.c - the ROM screen without Dear ImGui (launcher.h): the ROM is the file dropped on the executable
// (or named as its one argument), and the setup's progress goes to the console.
#include "launcher.h"
#include "rom_verify.h"
#include "SDL.h"
#include <stdio.h>

int CtLauncherUi(CtLauncher* L, const char* dropped) {
  char msg[512];
  const char* path = dropped;
  if (!path) {
    FILE* f = fopen(L->rom, "rb");                           // a ROM from an earlier, unfinished setup
    if (f) { fclose(f); path = L->rom; }
  }
  if (!path) {
    snprintf(msg, sizeof msg, "Belthasar needs your own dump of Chrono Trigger (USA), made from your cartridge: drop "
             "the ROM file on Belthasar to set it up.\n\nSupported dump: %ld bytes, SHA-1 %s.\nThe game's data is made "
             "from it in %s", (long)CT_ROM_SIZE, CT_ROM_SHA1, L->data);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Belthasar", msg, NULL);
    return 0;
  }
  if (!CtLauncherTakeRom(L, path, msg, sizeof msg)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Belthasar", msg, NULL);
    return 0;
  }
  printf("%s\nMaking the game's data from your ROM (about 15-20 minutes the first time)...\n", msg);
  CtSetupJob* j = CtLauncherSetupStart(L, msg, sizeof msg);
  if (!j) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Belthasar", msg, NULL); return 0; }
  CtSetupStatus st;
  int shown = -1;
  for (;;) {
    CtLauncherSetupPoll(j, &st);
    if (st.done != shown) { shown = st.done; printf("setup: %d / %d  %s\n", st.done, st.total, st.line); fflush(stdout); }
    if (!st.running) break;
    SDL_Delay(200);
  }
  CtLauncherSetupFree(j);
  if (st.exitCode != 0 || !CtLauncherReady(L)) {
    snprintf(msg, sizeof msg, "The setup stopped (exit code %d). The log is %ssetup.log.", st.exitCode, L->data);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Belthasar", msg, NULL);
    return 0;
  }
  return 1;
}
