// settings.c - see settings.h. A plain INI: `[section]` lines and `key=value` lines; keys and buttons by SDL's own
// names (SDL_GetScancodeName / SDL_GameControllerGetStringForButton), so the file stays readable and editable by hand.
#include "settings.h"
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CtSettings g_set;
static int g_setInit;
static char g_path[1024];

static const char* const BTN_NAME[CT_BTN_COUNT] = {
  "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Select", "Start"
};
static const char* const BTN_KEY[CT_BTN_COUNT] = {     // the INI key stem
  "up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "select", "start"
};
const char* CtSettingsButtonName(int btn) { return (btn >= 0 && btn < CT_BTN_COUNT) ? BTN_NAME[btn] : "?"; }

void CtSettingsDefaults(CtSettings* s) {
  memset(s, 0, sizeof *s);
  s->fullscreen = 0; s->windowScale = 2; s->scaleMode = CT_SCALE_FIT; s->aspect = CT_ASPECT_PIXEL;
  s->linearFilter = 0; s->renderScale = 1; s->vsync = 1; s->renderDriver[0] = 0; s->backend = 0;
  s->volume = 100; s->mute = 0;
  // the default keyboard (field.c fieldKeyPad): the arrows; A = Z, B = X, X = A, Y = S, L = D, R = C,
  // Select = Backspace, Start = Enter (and the keypad's Enter)
  static const int k0[CT_BTN_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_C,
    SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RETURN };
  for (int b = 0; b < CT_BTN_COUNT; b++) { s->keys[b][0] = k0[b]; s->keys[b][1] = 0; }
  s->keys[CT_BTN_START][1] = SDL_SCANCODE_KP_ENTER;
  // a controller by position, as the SNES pad's face buttons sit: bottom = B, right = A, left = Y, top = X
  static const int p0[CT_BTN_COUNT] = {
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START };
  for (int b = 0; b < CT_BTN_COUNT; b++) s->pad[b] = p0[b];
  s->padStick = 1;
#ifdef __ANDROID__
  s->touchPad = 1; s->fullscreen = 1;               // a phone: the whole screen, the fingers as the pad
#else
  s->touchPad = 0;
#endif
  s->touchOpacity = 45; s->touchScale = 100;
  s->sync = CT_SYNC_FRAME_PERFECT;
  s->instantUploads = 0;
}

CtSettings* CtSettingsGet(void) {
  if (!g_setInit) { CtSettingsDefaults(&g_set); g_setInit = 1; }
  return &g_set;
}

const char* CtSettingsPath(void) {
  if (!g_path[0]) {
    const char* e = getenv("CT_SETTINGS");
    if (e && *e) snprintf(g_path, sizeof g_path, "%s", e);
    else {
      char* base = SDL_GetBasePath();                  // the executable's folder: the file travels with the game
      snprintf(g_path, sizeof g_path, "%sct_settings.ini", base ? base : "");
      SDL_free(base);
    }
  }
  return g_path;
}

static char* trim(char* p) {
  while (*p == ' ' || *p == '\t') p++;
  char* e = p + strlen(p);
  while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return p;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void setKey(CtSettings* s, const char* sec, const char* key, const char* val) {
  int iv = atoi(val);
  if (!strcmp(sec, "video")) {
    if (!strcmp(key, "fullscreen")) s->fullscreen = iv != 0;
    else if (!strcmp(key, "window_scale")) s->windowScale = clampi(iv, 1, 8);
    else if (!strcmp(key, "scale")) s->scaleMode = !strcmp(val, "integer") ? CT_SCALE_INTEGER : !strcmp(val, "stretch") ? CT_SCALE_STRETCH : CT_SCALE_FIT;
    else if (!strcmp(key, "aspect")) s->aspect = !strcmp(val, "4:3") ? CT_ASPECT_4_3 : CT_ASPECT_PIXEL;
    else if (!strcmp(key, "filter")) s->linearFilter = !strcmp(val, "linear");
    else if (!strcmp(key, "render_scale")) s->renderScale = clampi(iv, 1, 4);
    else if (!strcmp(key, "interpolation")) s->interpolate = iv != 0;
    else if (!strcmp(key, "wide_room_wrap")) {}                       // (a retired option: read, ignored)
    else if (!strcmp(key, "steam_margins")) s->wideSteamMargins = iv != 0;
    else if (!strcmp(key, "fill_bottom_lines")) s->fillBottomLines = iv != 0;
    else if (!strcmp(key, "scene_adjustments")) s->sceneAdjust = iv != 0;
    else if (!strcmp(key, "widescreen")) s->widescreen = !strcmp(val, "16:9") ? CT_WIDE_16_9 : !strcmp(val, "16:10") ? CT_WIDE_16_10 :
                                                         !strcmp(val, "21:9") ? CT_WIDE_21_9 : CT_WIDE_OFF;
    else if (!strcmp(key, "vsync")) s->vsync = iv != 0;
    else if (!strcmp(key, "render_driver")) snprintf(s->renderDriver, sizeof s->renderDriver, "%s", val);
    else if (!strcmp(key, "backend")) s->backend = !strcmp(val, "opengl") ? 1 : !strcmp(val, "vulkan") ? 2 : 0;
  } else if (!strcmp(sec, "sound")) {
    if (!strcmp(key, "volume")) s->volume = clampi(iv, 0, 100);
    else if (!strcmp(key, "mute")) s->mute = iv != 0;
  } else if (!strcmp(sec, "controls")) {
    if (!strcmp(key, "pad_stick")) { s->padStick = iv != 0; return; }
    for (int b = 0; b < CT_BTN_COUNT; b++) {
      char k1[32], k2[32], kp[32];
      snprintf(k1, sizeof k1, "key_%s", BTN_KEY[b]); snprintf(k2, sizeof k2, "key2_%s", BTN_KEY[b]);
      snprintf(kp, sizeof kp, "pad_%s", BTN_KEY[b]);
      if (!strcmp(key, k1)) s->keys[b][0] = *val ? (int)SDL_GetScancodeFromName(val) : 0;
      else if (!strcmp(key, k2)) s->keys[b][1] = *val ? (int)SDL_GetScancodeFromName(val) : 0;
      else if (!strcmp(key, kp)) s->pad[b] = *val ? (int)SDL_GameControllerGetButtonFromString(val) : -1;
    }
  } else if (!strcmp(sec, "touch")) {
    if (!strcmp(key, "virtual_pad")) s->touchPad = iv != 0;
    else if (!strcmp(key, "opacity")) s->touchOpacity = clampi(iv, 10, 100);
    else if (!strcmp(key, "size")) s->touchScale = clampi(iv, 50, 150);
  } else if (!strcmp(sec, "timing")) {
    if (!strcmp(key, "sync")) s->sync = !strcmp(val, "free") ? CT_SYNC_FREE : CT_SYNC_FRAME_PERFECT;
    else if (!strcmp(key, "instant_sound_uploads")) s->instantUploads = iv != 0;
  }
}

int CtSettingsLoad(int interactive) {
  CtSettings* s = CtSettingsGet();
  const char* e = getenv("CT_SETTINGS");
  if (!interactive && !(e && *e)) return 0;            // headless: the defaults, whatever a player saved
  FILE* f = fopen(CtSettingsPath(), "r");
  if (!f) return 0;
  char line[512], sec[32] = "";
  while (fgets(line, sizeof line, f)) {
    char* p = trim(line);
    if (!*p || *p == ';' || *p == '#') continue;
    if (*p == '[') { char* q = strchr(p, ']'); if (q) { *q = 0; snprintf(sec, sizeof sec, "%s", p + 1); } continue; }
    char* eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    setKey(s, sec, trim(p), trim(eq + 1));
  }
  fclose(f);
  return 1;
}

int CtSettingsSave(void) {
  const CtSettings* s = CtSettingsGet();
  FILE* f = fopen(CtSettingsPath(), "w");
  if (!f) return 0;
  fprintf(f, "; Belthasar -- options. Edited by the F1 overlay; safe to edit by hand.\n\n");
  fprintf(f, "[video]\nbackend=%s\nfullscreen=%d\nwindow_scale=%d\nscale=%s\naspect=%s\nfilter=%s\nwidescreen=%s\nsteam_margins=%d\nfill_bottom_lines=%d\nscene_adjustments=%d\nrender_scale=%d\ninterpolation=%d\nvsync=%d\nrender_driver=%s\n\n",
          s->backend == 1 ? "opengl" : s->backend == 2 ? "vulkan" : "sdl", s->fullscreen, s->windowScale,
          s->scaleMode == CT_SCALE_INTEGER ? "integer" : s->scaleMode == CT_SCALE_STRETCH ? "stretch" : "fit",
          s->aspect == CT_ASPECT_4_3 ? "4:3" : "pixel", s->linearFilter ? "linear" : "nearest",
          s->widescreen == CT_WIDE_16_9 ? "16:9" : s->widescreen == CT_WIDE_16_10 ? "16:10" : s->widescreen == CT_WIDE_21_9 ? "21:9" : "off",
          s->wideSteamMargins, s->fillBottomLines, s->sceneAdjust, s->renderScale, s->interpolate, s->vsync,
          s->renderDriver);
  fprintf(f, "[sound]\nvolume=%d\nmute=%d\n\n", s->volume, s->mute);
  fprintf(f, "[controls]\npad_stick=%d\n", s->padStick);
  for (int b = 0; b < CT_BTN_COUNT; b++) {
    fprintf(f, "key_%s=%s\n", BTN_KEY[b], s->keys[b][0] ? SDL_GetScancodeName((SDL_Scancode)s->keys[b][0]) : "");
    fprintf(f, "key2_%s=%s\n", BTN_KEY[b], s->keys[b][1] ? SDL_GetScancodeName((SDL_Scancode)s->keys[b][1]) : "");
    const char* pn = s->pad[b] >= 0 ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)s->pad[b]) : NULL;
    fprintf(f, "pad_%s=%s\n", BTN_KEY[b], pn ? pn : "");
  }
  fprintf(f, "\n[touch]\nvirtual_pad=%d\nopacity=%d\nsize=%d\n", s->touchPad, s->touchOpacity, s->touchScale);
  fprintf(f, "\n[timing]\nsync=%s\ninstant_sound_uploads=%d\n", s->sync == CT_SYNC_FREE ? "free" : "frame_perfect",
          s->instantUploads);
  fclose(f);
  return 1;
}
