// launcher_ui.cpp - the ROM screen (launcher.h): a window of its own, before the game's, drawn with Dear ImGui over
// SDL's renderer in the look of a desktop form (a white header band, group boxes, a file box with Browse..., a progress
// bar and the buttons at the bottom right) with the system's own UI font. The player chooses or drops their dump; it is
// verified, and the setup makes the game's data from it. Nothing of the game is shown or needed here.
//
// CT_LAUNCHER_SHOT=<file.bmp> (debug, docs): render CT_LAUNCHER_DEMO's page (choose | verified | running | failed |
// done) with made-up status, save the window to the file and quit -- the screen's pictures without a setup run.
#include "launcher.h"
#include "rom_verify.h"
#include "SDL.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Phase { CHOOSE, VERIFIED, RUNNING, FAILED, DONE };

// ---- the form's look ---------------------------------------------------------------------------------------------
static ImFont* gFont;
static ImFont* gBold;
static ImFont* gTitle;
static float gK = 1.0f;                                     // the screen's scale (HiDPI)

static ImU32 rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }
static const ImVec4 kText(0.0f, 0.0f, 0.0f, 1.0f), kGrey(0.38f, 0.38f, 0.38f, 1.0f);
static const ImVec4 kOk(0.06f, 0.49f, 0.06f, 1.0f), kBad(0.77f, 0.11f, 0.11f, 1.0f);

static ImFont* loadFont(ImGuiIO& io, const char* const* paths, float px) {
  for (int i = 0; paths[i]; i++) {
    FILE* f = fopen(paths[i], "rb");
    if (!f) continue;
    fclose(f);
    ImFontConfig fc; fc.OversampleH = 2; fc.OversampleV = 2;
    if (ImFont* ft = io.Fonts->AddFontFromFileTTF(paths[i], px, &fc, io.Fonts->GetGlyphRangesDefault())) return ft;
  }
  return NULL;
}

static void formStyle(ImGuiIO& io) {
  static const char* const regular[] = {
    "C:\\Windows\\Fonts\\segoeui.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", NULL };
  static const char* const bold[] = {
    "C:\\Windows\\Fonts\\segoeuib.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf", NULL };
  gFont = loadFont(io, regular, 15.0f * gK);
  if (!gFont) { ImFontConfig fc; fc.SizePixels = 13.0f * gK; gFont = io.Fonts->AddFontDefault(&fc); }
  gBold = loadFont(io, bold, 15.0f * gK);
  if (!gBold) gBold = gFont;
  gTitle = loadFont(io, bold, 21.0f * gK);
  if (!gTitle) gTitle = gBold;
  io.FontDefault = gFont;

  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsLight(&s);
  s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.ChildRounding = 0; s.PopupRounding = 0;
  s.FrameBorderSize = 1; s.WindowBorderSize = 0; s.ChildBorderSize = 0;
  s.FramePadding = ImVec2(10, 5); s.ItemSpacing = ImVec2(8, 7); s.WindowPadding = ImVec2(0, 0);
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kGrey;
  c[ImGuiCol_WindowBg] = ImVec4(0.941f, 0.941f, 0.941f, 1);            // the classic form grey, F0F0F0
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_Border] = ImVec4(0.678f, 0.678f, 0.678f, 1);              // ADADAD
  c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 1);
  c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 1);
  c[ImGuiCol_Button] = ImVec4(0.882f, 0.882f, 0.882f, 1);              // E1E1E1
  c[ImGuiCol_ButtonHovered] = ImVec4(0.898f, 0.945f, 0.984f, 1);       // E5F1FB
  c[ImGuiCol_ButtonActive] = ImVec4(0.800f, 0.894f, 0.969f, 1);        // CCE4F7
  c[ImGuiCol_PlotHistogram] = ImVec4(0.024f, 0.690f, 0.145f, 1);       // the progress bar's green, 06B025
  c[ImGuiCol_Separator] = ImVec4(0.874f, 0.874f, 0.874f, 1);
  s.ScaleAllSizes(gK);
}

// A button in the form's manner: the accent border on the default one (the Enter key's), disabled ones greyed.
static bool formButton(const char* label, bool isDefault = false, bool enabled = true) {
  ImVec2 sz(ImMax(ImGui::CalcTextSize(label).x + 28 * gK, 88 * gK), 0);
  if (!enabled) ImGui::BeginDisabled();
  if (isDefault) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.471f, 0.843f, 1));   // 0078D7
  if (isDefault) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f * gK);
  bool hit = ImGui::Button(label, sz);
  if (isDefault) { ImGui::PopStyleVar(); ImGui::PopStyleColor(); }
  if (!enabled) ImGui::EndDisabled();
  if (isDefault && enabled && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) hit = true;
  return hit;
}

// A group box: a 1-px frame with its caption set into the top edge, around what is drawn until groupEnd().
struct Group { ImVec2 top; float w; const char* caption; };
static Group groupBegin(const char* caption, float w) {
  Group g; g.caption = caption; g.w = w;
  ImGui::Dummy(ImVec2(0, 4 * gK));
  g.top = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(ImVec2(g.top.x + 14 * gK, g.top.y + ImGui::GetTextLineHeight() * 0.5f + 10 * gK));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(g.top.x + w - 14 * gK);
  return g;
}
static void groupEnd(const Group& g) {
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  float bottom = ImGui::GetItemRectMax().y + 12 * gK;
  ImDrawList* d = ImGui::GetWindowDrawList();
  float half = ImGui::GetTextLineHeight() * 0.5f;
  ImVec2 a(g.top.x, g.top.y + half), b(g.top.x + g.w, bottom);
  d->AddRect(a, b, rgb(0xDC, 0xDC, 0xDC), 0.0f, 0, 1.0f * gK);
  ImVec2 ts = ImGui::CalcTextSize(g.caption);
  ImVec2 tp(g.top.x + 9 * gK, g.top.y);
  d->AddRectFilled(ImVec2(tp.x - 3 * gK, tp.y), ImVec2(tp.x + ts.x + 3 * gK, tp.y + ts.y), rgb(0xF0, 0xF0, 0xF0));
  d->AddText(tp, rgb(0, 0x33, 0x99), g.caption);                        // the caption's blue, 003399
  ImGui::SetCursorScreenPos(ImVec2(g.top.x, bottom + 6 * gK));
  ImGui::Dummy(ImVec2(g.w, 0));
}

// A status icon at the cursor: a filled disc with a tick (ok), a cross (bad) or an i (info), then the text.
static void statusLine(int kind, const char* text) {
  float h = ImGui::GetTextLineHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* d = ImGui::GetWindowDrawList();
  ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
  float r = h * 0.42f;
  ImU32 col = kind == 1 ? rgb(0x10, 0x7C, 0x10) : kind == 2 ? rgb(0xC4, 0x2B, 0x1C) : rgb(0x00, 0x67, 0xC0);
  d->AddCircleFilled(c, r, col, 20);
  ImU32 w = rgb(255, 255, 255);
  float t = 1.6f * gK;
  if (kind == 1) { d->AddLine(ImVec2(c.x - r * 0.45f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), w, t);
                   d->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.38f), w, t); }
  else if (kind == 2) { d->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), ImVec2(c.x + r * 0.4f, c.y + r * 0.4f), w, t);
                        d->AddLine(ImVec2(c.x + r * 0.4f, c.y - r * 0.4f), ImVec2(c.x - r * 0.4f, c.y + r * 0.4f), w, t); }
  else { d->AddLine(ImVec2(c.x, c.y - r * 0.05f), ImVec2(c.x, c.y + r * 0.5f), w, t);
         d->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), t * 0.75f, w); }
  ImGui::Dummy(ImVec2(h + 4 * gK, h));
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, kind == 1 ? kOk : kind == 2 ? kBad : kText);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// A path in at most `maxw` pixels: its start dropped for "..." when it is longer (the end -- the folder -- matters).
static void fitText(const char* t, float maxw) {
  if (ImGui::CalcTextSize(t).x <= maxw) { ImGui::TextUnformatted(t); return; }
  size_t n = strlen(t);
  for (size_t i = 1; i < n; i++) {
    char b[1200]; snprintf(b, sizeof b, "...%s", t + i);
    if (ImGui::CalcTextSize(b).x <= maxw) { ImGui::TextUnformatted(b); return; }
  }
  ImGui::TextUnformatted("...");
}

// The wizard's three steps across the top of the body, the current one in bold.
static void steps(int cur) {
  static const char* const name[3] = { "1  Your ROM", "2  The game's data", "3  Play" };
  for (int i = 0; i < 3; i++) {
    if (i) { ImGui::SameLine(0, 10 * gK); ImGui::TextDisabled(">"); ImGui::SameLine(0, 10 * gK); }
    if (i == cur) { ImGui::PushFont(gBold); ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1), "%s", name[i]); ImGui::PopFont(); }
    else ImGui::TextDisabled("%s", name[i]);
  }
}

static void saveShot(SDL_Renderer* ren, const char* path) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(ren, &w, &h);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return;
  if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, path);
  SDL_FreeSurface(s);
}

extern "C" int CtLauncherUi(CtLauncher* L, const char* dropped) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
  { float ddpi = 0;                                        // the screen's scale: 96 dpi = 1
    if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi > 0) gK = ddpi / 96.0f;
    if (gK < 1.0f) gK = 1.0f;
    if (gK > 3.0f) gK = 3.0f; }
  const char* shot = getenv("CT_LAUNCHER_SHOT");
  SDL_Window* win = SDL_CreateWindow("Belthasar Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     (int)(720 * gK), (int)(520 * gK), SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
                                     | (shot ? SDL_WINDOW_HIDDEN : 0));
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
  if (!ren) ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren) { if (win) SDL_DestroyWindow(win); SDL_QuitSubSystem(SDL_INIT_VIDEO); return 0; }
  SDL_SetWindowMinimumSize(win, (int)(560 * gK), (int)(460 * gK));
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = NULL;
  formStyle(io);
  ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
  ImGui_ImplSDLRenderer2_Init(ren);

  Phase phase = CHOOSE;
  char msg[512] = "", romShown[1100] = "";
  CtSetupJob* job = NULL;
  CtSetupStatus st; memset(&st, 0, sizeof st);
  Uint32 started = 0;
  int result = 0, quit = 0, play = 0;

  auto take = [&](const char* path) {                       // verify + copy into the data folder
    if (phase == RUNNING) return false;
    snprintf(romShown, sizeof romShown, "%s", path);
    if (!CtLauncherTakeRom(L, path, msg, sizeof msg)) { phase = CHOOSE; return false; }
    phase = VERIFIED;
    return true;
  };
  auto start = [&]() {
    job = CtLauncherSetupStart(L, msg, sizeof msg);
    memset(&st, 0, sizeof st);
    phase = job ? RUNNING : FAILED;
    started = SDL_GetTicks();
  };
  const char* demo = shot ? getenv("CT_LAUNCHER_DEMO") : NULL;
  if (demo) {
    snprintf(romShown, sizeof romShown, "%s", "D:\\Games\\Chrono Trigger (USA).sfc");
    if (!strcmp(demo, "verified")) { phase = VERIFIED; snprintf(msg, sizeof msg, "ROM verified: Chrono Trigger (USA), SHA-1 %s.", CT_ROM_SHA1); }
    else if (!strcmp(demo, "running")) { phase = RUNNING; st.done = 23; st.total = 56; st.running = 1;
      snprintf(st.line, sizeof st.line, "%s", "setup: tools/gen/lz_pack.py -- the LZ streams the game decompresses");
      started = SDL_GetTicks() - 372000; }
    else if (!strcmp(demo, "failed")) { phase = FAILED; snprintf(msg, sizeof msg, "The setup stopped (exit code 1).");
      snprintf(st.line, sizeof st.line, "%s", "setup: assets/extract_assets.py failed"); }
    else if (!strcmp(demo, "done")) phase = DONE;
    else { romShown[0] = 0; if (!strcmp(demo, "bad")) snprintf(msg, sizeof msg, "%s", "This file is not the supported dump: Chrono Trigger (USA), 4194304 bytes (got 3145728 bytes)."); }
  } else if (dropped) {
    if (take(dropped)) start();                             // dropped on the executable: straight on
  } else if (CtLauncherReady(L)) phase = DONE;
  else if (FILE* f = fopen(L->rom, "rb")) { fclose(f); if (take(L->rom)) start(); }   // an earlier, unfinished setup

  int frames = 0;
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
        if (st.exitCode == 0 && CtLauncherReady(L)) phase = DONE;
        else { phase = FAILED; snprintf(msg, sizeof msg, "The setup stopped (exit code %d).", st.exitCode); }
        CtLauncherSetupFree(job); job = NULL;
      }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("form", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                 | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* d = ImGui::GetWindowDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, pad = 18 * gK;

    // the header band: white, the title and a line under it
    float hh = 70 * gK;
    d->AddRectFilled(ImVec2(0, 0), ImVec2(W, hh), rgb(255, 255, 255));
    d->AddLine(ImVec2(0, hh), ImVec2(W, hh), rgb(0xDF, 0xDF, 0xDF), 1.0f * gK);
    ImGui::SetCursorPos(ImVec2(pad, 13 * gK));
    ImGui::PushFont(gTitle); ImGui::TextUnformatted("Belthasar Setup"); ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(pad, 13 * gK + ImGui::GetTextLineHeight() + 12 * gK));
    ImGui::TextDisabled("Makes the game's data from your own Chrono Trigger (USA) cartridge dump. Nothing is downloaded.");

    // the body
    float bw = W - 2 * pad;
    ImGui::SetCursorPos(ImVec2(pad, hh + 14 * gK));
    ImGui::BeginGroup();
    steps(phase == DONE ? 2 : phase == RUNNING || phase == FAILED ? 1 : 0);
    ImGui::Dummy(ImVec2(0, 4 * gK));

    Group g = groupBegin("ROM file", bw);
    {
      float bwid = ImMax(ImGui::CalcTextSize("Browse...").x + 28 * gK, 88 * gK);
      ImGui::SetNextItemWidth(bw - 28 * gK - bwid - 8 * gK);
      char shown[1100];
      snprintf(shown, sizeof shown, "%s", romShown[0] ? romShown : "");
      ImGui::BeginDisabled(phase == RUNNING);
      ImGui::InputTextWithHint("##rom", "Drag your .sfc file onto this window, or click Browse...", shown, sizeof shown,
                               ImGuiInputTextFlags_ReadOnly);
      ImGui::SameLine();
      if (formButton("Browse...", false, phase != RUNNING) && !demo) {
        char path[1100];
        if (CtLauncherBrowse(path, sizeof path)) take(path);
      }
      ImGui::EndDisabled();
      if (phase == CHOOSE && msg[0]) statusLine(2, msg);
      else if (phase == CHOOSE) {
        char t[200]; snprintf(t, sizeof t, "Supported dump: Chrono Trigger (USA), %ld,%03ld,%03ld bytes, SHA-1 %.12s...",
                              (long)CT_ROM_SIZE / 1000000, (long)CT_ROM_SIZE / 1000 % 1000, (long)CT_ROM_SIZE % 1000, CT_ROM_SHA1);
        statusLine(0, t);
      } else statusLine(1, "Verified: Chrono Trigger (USA). Belthasar ships none of the game; this file stays yours.");
    }
    groupEnd(g);

    g = groupBegin("Game data", bw);
    {
      float ob = ImMax(ImGui::CalcTextSize("Open folder").x + 28 * gK, 88 * gK), right = pad + bw - 14 * gK;
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("Folder:");
      ImGui::SameLine();
      fitText(L->data, right - ob - 12 * gK - ImGui::GetCursorScreenPos().x);
      ImGui::SameLine();
      ImGui::SetCursorScreenPos(ImVec2(right - ob, ImGui::GetCursorScreenPos().y));   // (SameLine's offset is the group's)
      if (formButton("Open folder") && !demo) CtLauncherReveal(L->data);
      if (phase == CHOOSE || phase == VERIFIED) {
        ImGui::TextWrapped("The data is made on this computer from the ROM, once (about 15-20 minutes). After that the "
                           "game reads only its own data: the ROM is not needed to play.");
      } else if (phase == RUNNING) {
        float f = st.total > 0 ? (float)st.done / (float)st.total : 0.0f;
        char over[48]; snprintf(over, sizeof over, st.total > 0 ? "%d %%" : "starting", (int)(f * 100.0f + 0.5f));
        ImGui::ProgressBar(f, ImVec2(bw - 28 * gK, 22 * gK), over);
        Uint32 s = (SDL_GetTicks() - started) / 1000;
        ImGui::TextDisabled("Step %d of %d  -  %u:%02u elapsed", st.done, st.total, s / 60, s % 60);
        ImGui::TextWrapped("%s", st.line[0] ? st.line : "Starting the setup...");
      } else if (phase == FAILED) {
        statusLine(2, msg[0] ? msg : "The setup stopped.");
        if (st.line[0]) ImGui::TextWrapped("%s", st.line);
        ImGui::TextDisabled("The whole output is in setup.log in the folder above.");
      } else {
        statusLine(1, "The game's data is ready. You can play now -- and keep your ROM wherever you like.");
      }
    }
    groupEnd(g);
    ImGui::EndGroup();

    // the bottom bar: a line, the buttons at the right
    float by = H - 54 * gK;
    d->AddLine(ImVec2(0, by), ImVec2(W, by), rgb(0xDF, 0xDF, 0xDF), 1.0f * gK);
    float bx = W - pad, bh = by + 13 * gK;
    auto rightButtons = [&](const char* a, bool aDef, bool aOn, const char* b) -> int {   // 1 = a, 2 = b
      float wa = ImMax(ImGui::CalcTextSize(a).x + 28 * gK, 88 * gK), wb = b ? ImMax(ImGui::CalcTextSize(b).x + 28 * gK, 88 * gK) : 0;
      float x = bx - wa - (b ? wb + 8 * gK : 0);
      ImGui::SetCursorPos(ImVec2(x, bh));
      int r = formButton(a, aDef, aOn) ? 1 : 0;
      if (b) { ImGui::SameLine(0, 8 * gK); if (formButton(b)) r = 2; }
      return r;
    };
    if (phase == CHOOSE || phase == VERIFIED) {
      int r = rightButtons("Make the game data", true, phase == VERIFIED, "Quit");
      if (r == 1 && !demo) start();
      if (r == 2) quit = 1;
    } else if (phase == RUNNING) {
      if (rightButtons("Cancel", false, true, NULL) == 1 && !demo) {
        CtLauncherSetupFree(job); job = NULL; phase = FAILED; snprintf(msg, sizeof msg, "The setup was cancelled.");
      }
    } else if (phase == FAILED) {
      int r = rightButtons("Try again", true, true, "Quit");
      if (r == 1 && !demo) { if (take(L->rom)) start(); }
      if (r == 2) quit = 1;
    } else {
      int r = rightButtons("Play", true, true, "Close");
      if (r == 1) { result = 1; play = 1; quit = 1; }
      if (r == 2) quit = 1;
    }
    ImGui::End();
    ImGui::Render();
    SDL_SetRenderDrawColor(ren, 0xF0, 0xF0, 0xF0, 255);
    SDL_RenderClear(ren);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
    if (shot && ++frames == 4) { saveShot(ren, shot); quit = 1; }
    SDL_RenderPresent(ren);
  }
  (void)play;
  if (job) CtLauncherSetupFree(job);                        // closed mid-setup: the setup is stopped
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return shot ? 0 : result;
}
