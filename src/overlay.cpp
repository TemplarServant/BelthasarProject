// overlay.cpp - the F1 options overlay: Dear ImGui (third_party/imgui, tools/get_imgui.ps1) over the paused game,
// on the SDL_Renderer the game presents with. See overlay.h. Edits settings.h's options; the host applies them.
#include "overlay.h"
#include "settings.h"
#include "touchpad.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "backends/imgui_impl_opengl3.h"
#ifdef CT_HAVE_VULKAN
#include "backends/imgui_impl_vulkan.h"
#include "present_vk.h"
#endif
#include <stdio.h>
#include <string.h>

static SDL_Renderer* g_ren;
static int g_ok;
static int g_gl;                                           // the OpenGL backend (else SDL_Renderer)
static CtVk* g_vk;                                         // the Vulkan backend
#ifdef CT_HAVE_VULKAN
static PFN_vkVoidFunction vkLoader(const char* name, void* user) { return (PFN_vkVoidFunction)CtVkProc((CtVk*)user, name); }
#endif
static char g_backendName[160];
static char g_padName[128];
// a binding being captured: the button, and which slot (0 / 1 = keyboard keys, 2 = the controller)
static int g_capBtn = -1, g_capSlot = 0;
static int g_selTab = -1;                                  // CtOverlaySelectTab: the tab to open on
extern "C" void CtOverlaySelectTab(int tab) { g_selTab = tab; }

extern "C" int CtOverlayAvailable(void) { return g_ok; }

extern "C" int CtOverlayInit(CtPresenter* pr) {
  if (g_ok) return 1;
  SDL_Window* win = CtPresentWindow(pr);
  SDL_Renderer* ren = CtPresentSdlRenderer(pr);
  void* glc = CtPresentGlContext(pr);
  CtVk* vk = CtPresentVk(pr);
  if (!win || (!ren && !glc && !vk)) return 0;
  snprintf(g_backendName, sizeof g_backendName, "%s", CtPresentName(pr));
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = NULL;                                   // the options live in ct_settings.ini, not imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  // The built-in bitmap font at a whole multiple of its 13 px, from the window's height: sharp at 2x on a 1080p
  // fullscreen, 1x in the default 512x448 window.
  { int ow = 0, oh = 0; CtPresentOutputSize(pr, &ow, &oh);
#ifdef __ANDROID__
    int k = (oh + 180) / 360; if (k < 1) k = 1; if (k > 4) k = 4;   // a phone's screen: big enough for a finger
#else
    int k = (oh + 270) / 540; if (k < 1) k = 1; if (k > 4) k = 4;
#endif
    ImFontConfig fc; fc.SizePixels = 13.0f * (float)k; io.Fonts->AddFontDefault(&fc);
    ImGui::GetStyle().ScaleAllSizes((float)k); }
  ImGui::StyleColorsDark();
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = 6.0f; st.FrameRounding = 4.0f; st.GrabRounding = 4.0f;
  bool ok;
#ifdef CT_HAVE_VULKAN
  if (vk) {
    CtVkInfo vi; CtVkInfoGet(vk, &vi);
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.Instance = (VkInstance)vi.instance; ii.PhysicalDevice = (VkPhysicalDevice)vi.physicalDevice;
    ii.Device = (VkDevice)vi.device; ii.QueueFamily = vi.queueFamily; ii.Queue = (VkQueue)vi.queue;
    ii.DescriptorPool = (VkDescriptorPool)vi.descriptorPool; ii.RenderPass = (VkRenderPass)vi.renderPass;
    ii.MinImageCount = vi.minImageCount; ii.ImageCount = vi.imageCount; ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ok = ImGui_ImplSDL2_InitForVulkan(win) && ImGui_ImplVulkan_LoadFunctions(vkLoader, vk) && ImGui_ImplVulkan_Init(&ii);
  } else
#endif
  ok = glc ? (ImGui_ImplSDL2_InitForOpenGL(win, glc) && ImGui_ImplOpenGL3_Init("#version 330 core"))
           : (ImGui_ImplSDL2_InitForSDLRenderer(win, ren) && ImGui_ImplSDLRenderer2_Init(ren));
  if (!ok) { ImGui::DestroyContext(); return 0; }
  g_ren = ren; g_gl = glc != NULL; g_vk = vk; g_ok = 1;
  return 1;
}

extern "C" void CtOverlayShutdown(void) {
  if (!g_ok) return;
#ifdef CT_HAVE_VULKAN
  if (g_vk) ImGui_ImplVulkan_Shutdown(); else
#endif
  if (g_gl) ImGui_ImplOpenGL3_Shutdown(); else ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
  g_ok = 0;
}

extern "C" void CtOverlaySetPadName(const char* name) {
  snprintf(g_padName, sizeof g_padName, "%s", name ? name : "");
}

extern "C" int CtOverlayProcessEvent(const SDL_Event* e) {
  if (!g_ok) return 0;
  if (g_capBtn >= 0) {                                     // binding: the next key / controller button is the answer
    CtSettings* s = CtSettingsGet();
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
      if (e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) { g_capBtn = -1; return 1; }   // cancel
      if (g_capSlot < 2) s->keys[g_capBtn][g_capSlot] = (int)e->key.keysym.scancode;
      g_capBtn = -1;
      return 1;
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN && g_capSlot == 2) {
      s->pad[g_capBtn] = e->cbutton.button;
      g_capBtn = -1;
      return 1;
    }
    if (e->type == SDL_KEYUP || e->type == SDL_CONTROLLERBUTTONUP) return 1;
  }
  ImGui_ImplSDL2_ProcessEvent(e);
  const ImGuiIO& io = ImGui::GetIO();
  if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && io.WantTextInput) return 1;
  return 0;
}

static const char* keyName(int sc) {
  if (!sc) return "-";
  const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
  return (n && *n) ? n : "?";
}
static const char* padName(int b) {
  if (b < 0) return "-";
  const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b);
  return n ? n : "?";
}
// One binding cell: shows the binding; left click captures a new one, right click clears it.
static void bindCell(int btn, int slot, const char* label) {
  CtSettings* s = CtSettingsGet();
  char id[64];
  int capturing = g_capBtn == btn && g_capSlot == slot;
  snprintf(id, sizeof id, "%s##b%d_%d", capturing ? "...press..." : label, btn, slot);
  if (ImGui::Button(id, ImVec2(-1, 0))) { g_capBtn = btn; g_capSlot = slot; }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    if (slot < 2) s->keys[btn][slot] = 0; else s->pad[btn] = -1;
    if (capturing) g_capBtn = -1;
  }
}

extern "C" int CtOverlayFrame(void) {
  if (!g_ok) return CT_OVL_CLOSE;
  int ch = 0;
  CtSettings* s = CtSettingsGet();
#ifdef CT_HAVE_VULKAN
  if (g_vk) ImGui_ImplVulkan_NewFrame(); else
#endif
  if (g_gl) ImGui_ImplOpenGL3_NewFrame(); else ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImVec2 size(vp->WorkSize.x * 0.8f, vp->WorkSize.y * 0.85f);
  if (size.x < 420) size.x = vp->WorkSize.x; if (size.y < 320) size.y = vp->WorkSize.y;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  bool open = true;
#ifdef __ANDROID__
  const char* title = "Options  (Back closes)";
#else
  const char* title = "Options  (F1 / Esc closes)";
#endif
  ImGui::Begin(title, &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse);
  if (!open) ch |= CT_OVL_CLOSE;
  if (ImGui::BeginTabBar("tabs")) {
    if (ImGui::BeginTabItem("Video", NULL, g_selTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
#ifndef __ANDROID__                                        // (a phone's window is always the whole screen)
      bool fs = s->fullscreen != 0;
      if (ImGui::Checkbox("Fullscreen", &fs)) { s->fullscreen = fs; ch |= CT_OVL_VIDEO; }
#endif
      const char* scales[] = { "Fit to window", "Integer scale (sharp pixels)", "Stretch" };
      if (ImGui::Combo("Scale", &s->scaleMode, scales, 3)) ch |= CT_OVL_VIDEO;
      const char* aspects[] = { "Square pixels (8:7)", "TV 4:3 (as on the console)" };
      if (ImGui::Combo("Aspect ratio", &s->aspect, aspects, 2)) ch |= CT_OVL_VIDEO;
      const char* filters[] = { "Sharp (nearest)", "Smooth (linear)" };
      if (ImGui::Combo("Filter", &s->linearFilter, filters, 2)) ch |= CT_OVL_VIDEO;
      const char* wides[] = { "Off (256 columns, as on the SNES)", "16:9", "16:10", "21:9" };
      if (ImGui::Combo("Widescreen", &s->widescreen, wides, 4)) ch |= CT_OVL_VIDEO;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The field, the world map, battles, the title and the special sequences (the race, the gate) show\n"
                          "more picture at the sides; menus and boxes stay in the middle. Outside the original camera's\n"
                          "area the game runs nothing extra: an object seen only in the extra strip is animated in the\n"
                          "picture only. In a room wider than the screen the picture stops at the room's edges (no black\n"
                          "strip) and the text box and the HUD stay in the middle.");
      bool rw = s->wideRoomWrap != 0;
      if (ImGui::Checkbox("Narrow rooms: continue the map at the sides", &rw)) s->wideRoomWrap = rw;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In a narrow room as wide as its own map (the sky of the fight with Flea, GATO's room, the Telepod)\n"
                          "the sides show the map wrapping round, as the camera would see it -- sometimes repeating what\n"
                          "is in the middle. Off: black past the room's edges. A room inside a larger map (Crono's\n"
                          "house) is always black past its edge: other rooms are there.");
      bool ip = s->interpolate != 0;
      if (ImGui::Checkbox("Frames between (60+ fps)", &ip)) s->interpolate = ip;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The game still runs at 60 frames per second; the screen is drawn at the monitor's rate, with the\n"
                          "camera and the field's objects between two frames (one frame of delay, ~16 ms). It only has an\n"
                          "effect on a monitor above 60 Hz (or without VSync).");
      ImGui::SliderInt("Internal resolution", &s->renderScale, 1, 4, "%dx");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The field's picture drawn at %dx%d: objects and the camera get positions between the SNES's\n"
                          "pixels. With frames between, 1x uses 4x.", 256 * s->renderScale, 224 * s->renderScale);
      bool vs = s->vsync != 0;
      if (ImGui::Checkbox("VSync", &vs)) { s->vsync = vs; ch |= CT_OVL_VIDEO; }
#ifndef __ANDROID__                                        // (Android: SDL_Renderer on OpenGL ES, the only backend)
      if (!s->fullscreen && ImGui::SliderInt("Window size", &s->windowScale, 1, 8, "%dx")) ch |= CT_OVL_VIDEO;
      ImGui::Separator();
#ifdef __APPLE__
      const char* bk[] = { "SDL (automatic)", "OpenGL 4.1", "Vulkan" };
#else
      const char* bk[] = { "SDL (automatic)", "OpenGL 3.3", "Vulkan" };
#endif
      if (ImGui::Combo("Video backend", &s->backend, bk, 3)) ch |= CT_OVL_RESTART;
      if (s->backend == CT_BACKEND_SDL) {
        const char* current = s->renderDriver[0] ? s->renderDriver : "Automatic";
        if (ImGui::BeginCombo("SDL driver", current)) {
          if (ImGui::Selectable("Automatic", !s->renderDriver[0])) {
            s->renderDriver[0] = 0;
            ch |= CT_OVL_RESTART;
          }
          for (int i = 0; i < SDL_GetNumRenderDrivers(); i++) {
            SDL_RendererInfo info;
            if (SDL_GetRenderDriverInfo(i, &info) != 0) continue;
            if (ImGui::Selectable(info.name, !strcmp(s->renderDriver, info.name))) {
              snprintf(s->renderDriver, sizeof s->renderDriver, "%s", info.name);
              ch |= CT_OVL_RESTART;
            }
          }
          ImGui::EndCombo();
        }
      }
      ImGui::TextDisabled("In use: %s", g_backendName);
      ImGui::TextDisabled("The backend and the driver take effect the next time the game starts.");
#else
      ImGui::TextDisabled("In use: %s", g_backendName);
#endif
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio", NULL, g_selTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::SliderInt("Volume", &s->volume, 0, 100, "%d%%");
      bool m = s->mute != 0;
      if (ImGui::Checkbox("Mute", &m)) s->mute = m;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Controls", NULL, g_selTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
#ifdef __ANDROID__                                        // (first: a finger cannot scroll the list below)
      bool tp = s->touchPad != 0;
      if (ImGui::Checkbox("On-screen gamepad", &tp)) s->touchPad = tp;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Touch controls drawn at the screen's sides. They hide when a controller is used and come back\n"
                          "at the next touch.");
      ImGui::SliderInt("Gamepad opacity", &s->touchOpacity, 10, 100, "%d%%");
      ImGui::SliderInt("Gamepad size", &s->touchScale, 50, 150, "%d%%");
      ImGui::Separator();
#endif
      ImGui::Text("Controller: %s", g_padName[0] ? g_padName : "none connected");
      bool st = s->padStick != 0;
      if (ImGui::Checkbox("Left stick moves like the d-pad", &st)) s->padStick = st;
      ImGui::TextDisabled("Click to change (Esc cancels); right click clears.");
      if (ImGui::BeginTable("binds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("SNES button", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Key 2");
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        for (int b = 0; b < CT_BTN_COUNT; b++) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(CtSettingsButtonName(b));
          ImGui::TableSetColumnIndex(1); bindCell(b, 0, keyName(s->keys[b][0]));
          ImGui::TableSetColumnIndex(2); bindCell(b, 1, keyName(s->keys[b][1]));
          ImGui::TableSetColumnIndex(3); bindCell(b, 2, padName(s->pad[b]));
        }
        ImGui::EndTable();
      }
      if (ImGui::Button("Restore defaults")) {
        CtSettings d; CtSettingsDefaults(&d);
        memcpy(s->keys, d.keys, sizeof s->keys); memcpy(s->pad, d.pad, sizeof s->pad); s->padStick = d.padStick;
#ifdef __ANDROID__
        s->touchPad = d.touchPad; s->touchOpacity = d.touchOpacity; s->touchScale = d.touchScale;
#endif
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sync", NULL, g_selTab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::RadioButton("Frame-perfect", &s->sync, CT_SYNC_FRAME_PERFECT);
      ImGui::TextWrapped("Reproduces the SNES's timing frame by frame: the pauses while music loads, the load and menu "
                         "waits, the frames the console spent for being slow.");
      ImGui::RadioButton("Free", &s->sync, CT_SYNC_FREE);
      ImGui::TextWrapped("Without the waits that only existed because of the hardware's limits, when they do not change the game.");
      ImGui::Spacing();
      ImGui::TextWrapped("In Free: location loads and the reload after the menu are not black; scripts do not stop while "
                         "music loads; a battle starts and ends without waiting for the sound upload.");
      ImGui::TextDisabled("Takes effect at once. The play clock counts fewer frames, as on a faster console.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    g_selTab = -1;
  }
  ImGui::Separator();
  if (ImGui::Button("Back to the game")) ch |= CT_OVL_CLOSE;
  ImGui::SameLine();
  if (ImGui::Button("Quit the game")) ch |= CT_OVL_QUIT;
  ImGui::End();
  ImGui::Render();
  return ch;
}

// The virtual gamepad: a frame of ImGui of its own (no window, only the foreground draw list), drawn over the picture.
extern "C" void CtOverlayTouchDraw(const CtTouchDraw* d) {
  if (!g_ok || !d || d->n <= 0) return;
#ifdef CT_HAVE_VULKAN
  if (g_vk) ImGui_ImplVulkan_NewFrame(); else
#endif
  if (g_gl) ImGui_ImplOpenGL3_NewFrame(); else ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  float a = d->alpha;
  ImU32 fill = IM_COL32(40, 40, 48, (int)(170 * a)), fillOn = IM_COL32(230, 230, 240, (int)(200 * a));
  ImU32 edge = IM_COL32(255, 255, 255, (int)(220 * a)), text = IM_COL32(255, 255, 255, (int)(255 * a));
  ImU32 textOn = IM_COL32(20, 20, 24, (int)(255 * a));
  ImFont* font = ImGui::GetFont();
  // the layout is in drawable pixels; ImGui draws in the window's units (the same on Android, not on a high-DPI desktop)
  const ImGuiIO& io = ImGui::GetIO();
  float sx = d->outW > 0 ? io.DisplaySize.x / d->outW : 1.0f, sy = d->outH > 0 ? io.DisplaySize.y / d->outH : 1.0f;
  for (int i = 0; i < d->n; i++) {
    CtTouchControl sc = d->c[i];
    sc.x *= sx; sc.y *= sy; sc.w *= sx; sc.h *= sy;
    const CtTouchControl* c = &sc;
    ImVec2 ctr(c->x, c->y);
    if (c->shape == CT_TOUCH_SHAPE_DPAD) {                 // a plus: four arms, each lit while its direction is held
      float r = c->w / 2, t = r / 3;
      dl->AddCircleFilled(ctr, r, IM_COL32(40, 40, 48, (int)(70 * a)), 48);
      struct { int btn; float x0, y0, x1, y1; } arm[4] = {
        { CT_BTN_UP, -t, -r, t, -t }, { CT_BTN_DOWN, -t, t, t, r }, { CT_BTN_LEFT, -r, -t, -t, t }, { CT_BTN_RIGHT, t, -t, r, t } };
      dl->AddRectFilled(ImVec2(ctr.x - t, ctr.y - t), ImVec2(ctr.x + t, ctr.y + t), fill);
      for (int k = 0; k < 4; k++) {
        int on = (d->dpadDirs >> arm[k].btn) & 1;
        ImVec2 p0(ctr.x + arm[k].x0, ctr.y + arm[k].y0), p1(ctr.x + arm[k].x1, ctr.y + arm[k].y1);
        dl->AddRectFilled(p0, p1, on ? fillOn : fill, t * 0.25f);
        dl->AddRect(p0, p1, edge, t * 0.25f, 0, 2.0f);
      }
      continue;
    }
    float fs = (c->shape == CT_TOUCH_SHAPE_CIRCLE ? c->w * 0.45f : c->h * 0.5f);
    if (c->shape == CT_TOUCH_SHAPE_CIRCLE) {
      dl->AddCircleFilled(ctr, c->w / 2, c->pressed ? fillOn : fill, 40);
      dl->AddCircle(ctr, c->w / 2, edge, 40, 2.0f);
    } else {
      ImVec2 p0(c->x - c->w / 2, c->y - c->h / 2), p1(c->x + c->w / 2, c->y + c->h / 2);
      dl->AddRectFilled(p0, p1, c->pressed ? fillOn : fill, c->h / 2);
      dl->AddRect(p0, p1, edge, c->h / 2, 0, 2.0f);
    }
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, c->label);
    dl->AddText(font, fs, ImVec2(ctr.x - ts.x / 2, ctr.y - ts.y / 2), c->pressed ? textOn : text, c->label);
  }
  ImGui::Render();
  CtOverlayRender();
}

extern "C" void CtOverlayRender(void) {
  if (!g_ok) return;
#ifdef CT_HAVE_VULKAN
  if (g_vk) { VkCommandBuffer c = (VkCommandBuffer)CtVkCommandBuffer(g_vk);
             if (c) ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), c); return; }
#endif
  if (g_gl) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  else ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_ren);
}
