// launcher_ui.cpp - the ROM screen (launcher.h): a window of its own, before the game's, drawn with Dear ImGui over
// SDL's renderer. The player drops their dump on it; it is verified, and the setup makes the game's data from it.
// Nothing of the game is shown or needed here: the text is the built-in font's, the screen has no picture.
#include "launcher.h"
#include "rom_verify.h"
#include "SDL.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <string.h>

enum Phase { WAIT_ROM, RUNNING, FAILED, DONE };

extern "C" int CtLauncherUi(CtLauncher* L, const char* dropped) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;
  SDL_Window* win = SDL_CreateWindow("Belthasar - setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 760, 420,
                                     SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
  if (!ren) ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren) { if (win) SDL_DestroyWindow(win); SDL_QuitSubSystem(SDL_INIT_VIDEO); return 0; }
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = NULL;
  { int ow = 0, oh = 0; SDL_GetRendererOutputSize(ren, &ow, &oh);
    int k = (oh + 300) / 600; if (k < 1) k = 1; if (k > 3) k = 3;           // a HiDPI screen: the font at 2x
    ImFontConfig fc; fc.SizePixels = 13.0f * (float)k; io.Fonts->AddFontDefault(&fc);
    ImGui::GetStyle().ScaleAllSizes((float)k); }
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
  ImGui_ImplSDLRenderer2_Init(ren);

  Phase phase = WAIT_ROM;
  char msg[512] = "";
  CtSetupJob* job = NULL;
  CtSetupStatus st; memset(&st, 0, sizeof st);
  int result = 0, quit = 0;

  auto take = [&](const char* path) {
    if (phase == RUNNING) return;
    if (!CtLauncherTakeRom(L, path, msg, sizeof msg)) { phase = WAIT_ROM; return; }
    job = CtLauncherSetupStart(L, msg, sizeof msg);
    phase = job ? RUNNING : FAILED;
  };
  if (dropped) take(dropped);
  else if (CtLauncherReady(L)) phase = DONE;
  else if (FILE* f = fopen(L->rom, "rb")) { fclose(f); take(L->rom); }  // a ROM from an earlier, unfinished setup

  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT) quit = 1;
      if (e.type == SDL_DROPFILE) { take(e.drop.file); SDL_free(e.drop.file); }
    }
    if (job) {
      CtLauncherSetupPoll(job, &st);
      if (!st.running) {
        if (st.exitCode == 0 && CtLauncherReady(L)) { phase = DONE; result = 1; quit = 1; }
        else { phase = FAILED; snprintf(msg, sizeof msg, "The setup stopped (exit code %d).", st.exitCode); }
        CtLauncherSetupFree(job); job = NULL;
      }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Belthasar");
    ImGui::Separator();
    ImGui::Spacing();
    if (phase == WAIT_ROM) {
      ImGui::TextWrapped("Belthasar needs your own dump of Chrono Trigger (USA), made from your cartridge. It ships none "
                         "of the game.");
      ImGui::Spacing();
      ImGui::TextWrapped("Drag the ROM file (.sfc / .smc) onto this window.");
      ImGui::Spacing();
      ImGui::TextDisabled("Supported dump: %ld bytes, SHA-1 %s", (long)CT_ROM_SIZE, CT_ROM_SHA1);
      ImGui::TextDisabled("The game's data is made from it on this computer, in:");
      ImGui::TextDisabled("%s", L->data);
    } else if (phase == RUNNING) {
      ImGui::TextWrapped("%s", msg[0] ? msg : "ROM verified.");
      ImGui::Spacing();
      ImGui::TextWrapped("Making the game's data from your ROM. The first time takes about 15-20 minutes; nothing is "
                         "downloaded.");
      ImGui::Spacing();
      float f = st.total > 0 ? (float)st.done / (float)st.total : 0.0f;
      char over[32]; snprintf(over, sizeof over, "%d / %d", st.done, st.total);
      ImGui::ProgressBar(f, ImVec2(-1, 0), st.total > 0 ? over : "starting");
      ImGui::TextDisabled("%s", st.line);
    } else if (phase == FAILED) {
      ImGui::TextWrapped("%s", msg);
      ImGui::TextDisabled("%s", st.line);
      ImGui::TextWrapped("The log is %ssetup.log.", L->data);
      ImGui::Spacing();
      if (ImGui::Button("Try again")) take(L->rom);
      ImGui::SameLine();
      if (ImGui::Button("Quit")) quit = 1;
    } else {
      result = 1; quit = 1;
    }
    if (phase == WAIT_ROM && msg[0]) { ImGui::Spacing(); ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1), "%s", msg); }
    ImGui::End();
    ImGui::Render();
    SDL_SetRenderDrawColor(ren, 18, 18, 22, 255);
    SDL_RenderClear(ren);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
    SDL_RenderPresent(ren);
  }
  if (job) CtLauncherSetupFree(job);                        // closed mid-setup: the setup is stopped
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return result;
}
