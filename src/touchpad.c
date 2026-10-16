// touchpad.c - see touchpad.h. The layout is made from the drawable's size and the player's scale; each finger is
// tracked by its SDL id, so several buttons can be held at once and a finger can slide from one button to the next.
#include "touchpad.h"
#include "settings.h"
#include <math.h>
#include <string.h>

#define TP_FINGERS 10
static struct { SDL_FingerID id; int active, onDpad; uint32_t mask; } g_fing[TP_FINGERS];
static int g_outW = 1280, g_outH = 720;
static uint32_t g_tapLatch;   // this frame's new presses: a tap that goes down and up inside one frame still holds one

int CtTouchPadEnabled(int padInUse) { return CtSettingsGet()->touchPad && !padInUse; }

void CtTouchPadSetOutput(int w, int h) { if (w > 0 && h > 0) { g_outW = w; g_outH = h; } }

void CtTouchPadReset(void) { memset(g_fing, 0, sizeof g_fing); g_tapLatch = 0; }

void CtTouchPadNewFrame(void) { g_tapLatch = 0; }

// The controls, at the current size. Index 0 is the d-pad.
static int tpLayout(CtTouchControl* c) {
  const CtSettings* st = CtSettingsGet();
  float W = (float)g_outW, H = (float)g_outH;
  float s = (W < H ? W : H) * (float)st->touchScale / 100.0f;   // the unit: the screen's short side, scaled
  float mx = W * 0.04f, my = H * 0.05f;                         // the margins
  int n = 0;
  float dR = 0.21f * s;                                         // the d-pad's radius
  float rowY = H - my - dR;
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_DPAD, mx + dR, rowY, 2 * dR, 2 * dR, "", 0, 0 };
  float bR = 0.075f * s, bD = 0.135f * s;                       // a face button's radius, its distance from the centre
  float fx = W - mx - bD - bR, fy = rowY;
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_CIRCLE, fx + bD, fy, 2 * bR, 2 * bR, "A", 1u << CT_BTN_A, 0 };
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_CIRCLE, fx, fy + bD, 2 * bR, 2 * bR, "B", 1u << CT_BTN_B, 0 };
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_CIRCLE, fx, fy - bD, 2 * bR, 2 * bR, "X", 1u << CT_BTN_X, 0 };
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_CIRCLE, fx - bD, fy, 2 * bR, 2 * bR, "Y", 1u << CT_BTN_Y, 0 };
  float sw = 0.24f * s, sh = 0.09f * s;                         // L / R
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_RECT, mx + sw / 2, my + sh / 2, sw, sh, "L", 1u << CT_BTN_L, 0 };
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_RECT, W - mx - sw / 2, my + sh / 2, sw, sh, "R", 1u << CT_BTN_R, 0 };
  float pw = 0.20f * s, ph = 0.07f * s, py = my + sh + 0.06f * s + ph / 2;   // Select / Start
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_RECT, mx + sw / 2, py, pw, ph, "SELECT", 1u << CT_BTN_SELECT, 0 };
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_RECT, W - mx - sw / 2, py, pw, ph, "START", 1u << CT_BTN_START, 0 };
  float mw = 0.16f * s, mh = 0.06f * s;                         // the options button, under Select (off the picture)
  c[n++] = (CtTouchControl){ CT_TOUCH_SHAPE_RECT, mx + sw / 2, py + ph / 2 + 0.06f * s + mh / 2, mw, mh, "MENU", CT_TOUCH_MENU, 0 };
  return n;
}

// The d-pad's directions at (dx, dy) from its centre: eight ways, with a dead zone in the middle.
static uint32_t tpDpadDirs(float dx, float dy, float r) {
  if (dx * dx + dy * dy < (0.18f * r) * (0.18f * r)) return 0;
  const float t = 0.4142f;                                      // tan(22.5 deg): the diagonals' sectors
  float ax = fabsf(dx), ay = fabsf(dy);
  uint32_t m = 0;
  if (ax > t * ay) m |= dx > 0 ? 1u << CT_BTN_RIGHT : 1u << CT_BTN_LEFT;
  if (ay > t * ax) m |= dy > 0 ? 1u << CT_BTN_DOWN : 1u << CT_BTN_UP;
  return m;
}

// What a finger at (x, y) presses. `dpad`: -1 = a new touch (it may land anywhere), 1 = it began on the d-pad (it
// stays the d-pad's while it slides), 0 = it began elsewhere.
static uint32_t tpHit(float x, float y, int dpad, int* onDpad) {
  CtTouchControl c[CT_TOUCH_MAX_CONTROLS];
  int n = tpLayout(c);
  float dr = c[0].w / 2, dx = x - c[0].x, dy = y - c[0].y;
  if (dpad == 1 || (dpad == -1 && dx * dx + dy * dy <= (1.25f * dr) * (1.25f * dr))) {
    if (onDpad) *onDpad = 1;
    return tpDpadDirs(dx, dy, dr);
  }
  if (onDpad) *onDpad = 0;
  uint32_t m = 0;
  for (int i = 1; i < n; i++) {
    if (c[i].mask == CT_TOUCH_MENU && dpad != -1) continue;     // the options button only on a fresh touch
    if (c[i].shape == CT_TOUCH_SHAPE_CIRCLE) {
      float r = c[i].w / 2 * 1.2f, ex = x - c[i].x, ey = y - c[i].y;
      if (ex * ex + ey * ey <= r * r) m |= c[i].mask;
    } else {
      float hw = c[i].w / 2 * 1.15f, hh = c[i].h / 2 * 1.3f;
      if (fabsf(x - c[i].x) <= hw && fabsf(y - c[i].y) <= hh) m |= c[i].mask;
    }
  }
  return m;
}

uint32_t CtTouchPadEvent(const SDL_Event* e) {
  if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return 0;
  float x = e->tfinger.x * (float)g_outW, y = e->tfinger.y * (float)g_outH;
  int slot = -1;
  for (int i = 0; i < TP_FINGERS; i++) if (g_fing[i].active && g_fing[i].id == e->tfinger.fingerId) { slot = i; break; }
  if (e->type == SDL_FINGERUP) { if (slot >= 0) g_fing[slot].active = 0; return 0; }
  if (e->type == SDL_FINGERMOTION) {
    if (slot >= 0) g_fing[slot].mask = tpHit(x, y, g_fing[slot].onDpad, NULL) & ~CT_TOUCH_MENU;
    return 0;
  }
  if (slot < 0) for (int i = 0; i < TP_FINGERS; i++) if (!g_fing[i].active) { slot = i; break; }
  int onDpad = 0;
  uint32_t m = tpHit(x, y, -1, &onDpad);
  if (slot >= 0) {
    g_fing[slot].id = e->tfinger.fingerId; g_fing[slot].active = 1; g_fing[slot].onDpad = onDpad;
    g_fing[slot].mask = m & ~CT_TOUCH_MENU;
  }
  g_tapLatch |= m & ~CT_TOUCH_MENU;
  return m;
}

uint32_t CtTouchPadButtons(void) {
  uint32_t m = g_tapLatch;
  for (int i = 0; i < TP_FINGERS; i++) if (g_fing[i].active) m |= g_fing[i].mask;
  return m;
}

void CtTouchPadBuild(CtTouchDraw* d) {
  memset(d, 0, sizeof *d);
  d->n = tpLayout(d->c);
  uint32_t held = CtTouchPadButtons();
  const uint32_t dirs = (1u << CT_BTN_UP) | (1u << CT_BTN_DOWN) | (1u << CT_BTN_LEFT) | (1u << CT_BTN_RIGHT);
  d->dpadDirs = held & dirs;
  for (int i = 1; i < d->n; i++) d->c[i].pressed = (held & d->c[i].mask) != 0;
  d->c[0].pressed = d->dpadDirs != 0;
  d->alpha = (float)CtSettingsGet()->touchOpacity / 100.0f;
  d->outW = (float)g_outW; d->outH = (float)g_outH;
}
