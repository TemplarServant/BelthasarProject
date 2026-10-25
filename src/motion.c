// motion.c - see motion.h.
#include "motion.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void CtMotBegin(CtMotFrame* m, int w) {
  m->nspx = 0; m->native = NULL; m->owNative = NULL; m->nativeFx = NULL;
  m->valid = 0; m->w = w < 1 ? 1 : w > CT_MOT_W ? CT_MOT_W : w; m->mx0 = -(1 << 30); m->mx1 = 1 << 30; m->nspr = 0; m->canvas = NULL; m->cW = m->cH = 0; m->bright = 15;
  m->camX = m->camY = m->bg3X = m->bg3Y = 0; m->o2x = m->o2y = m->o3x = m->o3y = 0; m->canvasX0 = m->canvasY0 = 0;
  m->camE = 0; m->w7 = NULL;
  memset(m->own, CT_MOT_FIXED, sizeof m->own); memset(m->world, 0, sizeof m->world);
}

void CtMotEngine(CtMotFrame* m, const uint8_t* w7, int camWX, int camWY) {
  static int on = -1; if (on < 0) on = !(getenv("CT_MOTENGINE") && !strcmp(getenv("CT_MOTENGINE"), "0"));   // (measurement:
  if (!m || !w7 || !on) return;                                                                            //  the fit alone)
  m->w7 = w7; m->camE = 1;
  m->camEW[0] = camWX; m->camEX[0] = camWX * 16 + (int8_t)w7[0x1D3B];   // (the remainder: a signed nibble, C0/92CA)
  m->camEW[1] = camWY; m->camEX[1] = camWY * 16 + (int8_t)w7[0x1D3C];
}

int CtMotSprite(CtMotFrame* m, int key, int x, int y) {
  if (!m || m->nspr >= CT_MOT_MAXSPR) return CT_MOT_FIXED;
  m->sprKey[m->nspr] = key; m->sprX[m->nspr] = x; m->sprY[m->nspr] = y;
  m->sprWx[m->nspr] = m->sprWy[m->nspr] = -1;
  if (m->w7 && key >= 0 && key < 0x40) {           // (a field object: its 12.4 words)
    m->sprWx[m->nspr] = m->w7[0x1800 + key * 2] | (m->w7[0x1801 + key * 2] << 8);
    m->sprWy[m->nspr] = m->w7[0x1880 + key * 2] | (m->w7[0x1881 + key * 2] << 8);
  }
  return CT_MOT_SPR + m->nspr++;
}

// ---- the continuous places: the self-scrolling layers, the camera, the objects (motion.h CtMotFit) -----------------------
int g_ctMotNoFrac;                                 // (CT_MOTCHECK: the owners against the frame as drawn -- whole pixels)
#define FIT_N 32                                   // (frames: a run of 2 px with a 1 every 16 -- the Magus hall -- fits whole)
// (channels 0-5 the layers' offsets, 6-7 the camera's place)
// (qo: BG2's / BG3's offsets fitted on their own, where the place on the map is no steady line -- below)
// (eOk / eW / eX: the last frame's engine camera, CtMotFrame.camE)
static struct { int scene, n; double v[6][FIT_N]; double cam[2][FIT_N]; double q[8]; int qOk[8], steady[8]; double qo[4]; int qoOk[4];
                int eOk, eW[2], eX[2]; } g_fit;
// The slope over the history (least squares); 0 = not one steady sub-pixel slope. whole: a slope of whole pixels a frame
// is none either (the camera and the objects: a step every frame needs no fit, and its phase would put them off).
static double fitSlope(const double* v, int n, int whole) {
  if (n < 6) return 0;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < n; i++) { sx += i; sy += v[i]; sxx += (double)i * i; sxy += i * v[i]; }
  double den = n * sxx - sx * sx; if (den == 0) return 0;
  double b = (n * sxy - sx * sy) / den, a = (sy - b * sx) / n;
  if (b == 0 || b > 2.0 || b < -2.0) return 0;     // still, or fast: whole pixels a frame need no fit
  // (one step in the history is a start, not a slope: a run starting from still read as 0.04 px a frame and the party
  //  stayed two pixels behind the camera's pan into the Magus hall -- the owner, 2026-10-24)
  { int steps = 0; for (int i = 1; i < n; i++) if (v[i] != v[i - 1]) steps++; if (steps < 2) return 0; }
  if (whole && fabs(b - floor(b + 0.5)) < 0.02) return 0;
  for (int i = 0; i < n; i++) { double r = v[i] - (a + b * i); if (r > 0.75 || r < -0.75) return 0; }   // not steady
  return b;
}
// The continuous place: the last one advanced by the slope, TRAILING the drawn one -- never past it, never a whole pixel
// behind (for a motion up the axis inside (drawn - 1, drawn], down it [drawn, drawn + 1)), so a step re-phases it and the
// advance per frame stays the slope. Led instead (the drawn one its whole part), an object that stopped -- at a wall --
// was drawn up to a pixel past it between the frames and came back (the owner, 2026-10-24); trailing, it reaches the
// drawn place and stays. It never jumps: where the history is no steady slope (a pan changing its speed, a stop) it goes
// on at the last few frames' mean rate from where it was, and where the rate is whole pixels a frame or none it closes on
// the drawn place an eighth of a pixel a frame. On the drawn place and stepping whole pixels (a pan starting from still,
// a walk of 1 or 2 px a frame) it stays on it. *steady (may be NULL): a steady sub-pixel slope was found.
// known (may be NULL): the engine's own rate this frame -- the camera's remainder, an object's 12.4 words -- which
// stands for the fit's slope: it is there from the motion's first frame and follows a change of speed, a stop or a turn
// on the frame it happens, where the fit needs two steps and a steady history.
static double fitQ(double* qs, int* have, int* steady, const double* v, int n, double cur, int whole, const double* known) {
  double b = known ? (whole && fabs(*known - floor(*known + 0.5)) < 0.02 ? 0 : *known) : fitSlope(v, n, whole), rate = b;
  if (steady) *steady = b != 0;
  const double step = n >= 2 ? v[n - 1] - v[n - 2] : 0;
  if (!*have) { *qs = cur; *have = 1; return cur; }
  if (b == 0 && fabs(*qs - v[n - 2]) < 1e-9 && step != 0 && step == floor(step)) { *qs = cur; return cur; }
  if (b == 0 && known) rate = *known;
  else if (b == 0) {
    rate = fitSlope(v, n, 0);                      // (a whole-pixel slope, steady)
    if (rate == 0 && n >= 2) { int m = n < 5 ? n : 5; rate = (v[n - 1] - v[n - m]) / (m - 1); }
  }
  double q = *qs + rate;
  const int dir = rate > 1e-9 ? 1 : rate < -1e-9 ? -1 : step > 0 ? 1 : step < 0 ? -1 : 0;
  if (dir > 0) { if (q > cur) q = cur; if (q < cur - 0.999) q = cur - 0.999; }
  else if (dir < 0) { if (q < cur) q = cur; if (q > cur + 0.999) q = cur + 0.999; }
  else { if (q > cur + 0.999) q = cur + 0.999; if (q < cur - 0.999) q = cur - 0.999; }
  if (fabs(rate - floor(rate + 0.5)) < 0.02) q = fabs(q - cur) > 0.125 ? q + (q < cur ? 0.125 : -0.125) : cur;
  *qs = q;
  return q;
}
static double fitOne(int c, const double* v, int n, double cur, const double* known) {
  return fitQ(&g_fit.q[c], &g_fit.qOk[c], &g_fit.steady[c], v, n, cur, c >= 6, known);
}
// The engine's rate of an object's 12.4 word (w, the last frame's pw), when the drawn place stepped as its whole part
// did (dv): a drawn place the engine's does not explain -- a picture held a frame behind its camera, a
// re-seed -- has none, and the fit goes on.
static int engineRate16(int w, int pw, double dv, double* rate) {
  if (w < 0 || pw < 0) return 0;
  const int d = (int16_t)(w - pw);                 // (a 16-bit word: its step wraps)
  if (d > 32 * 16 || d < -32 * 16) return 0;
  if (dv != ((pw + d) >> 4) - (pw >> 4)) return 0; // (>> 4 floors: the whole part below 0 too)
  *rate = d / 16.0;
  return 1;
}
// Each object's history, by its key (the frames it was seen in a row).
typedef struct { int scene, last, n; double x[FIT_N], y[FIT_N]; double qx, qy; int okx, oky; double fx, fy; int wx, wy; } FitSpr;   // (wx/wy: its last 12.4 words)
#define FIT_KEYS 2048                              // (the overworld's keys: block * 16 + piece)
static FitSpr g_fitSpr[FIT_KEYS];
static int g_fitFrame;
void CtMotFit(CtMotFrame* m) {
  m->qFrac = 0; g_fitFrame++;
  m->qcx = m->camX; m->qcy = m->camY;
  for (int i = 0; i < m->nspr; i++) { m->qsx[i] = m->sprX[i]; m->qsy[i] = m->sprY[i]; }
  if (!m->valid) { g_fit.n = 0; g_fit.eOk = 0; memset(g_fit.qOk, 0, sizeof g_fit.qOk); memset(g_fit.steady, 0, sizeof g_fit.steady); memset(g_fit.qoOk, 0, sizeof g_fit.qoOk); return; }
  if (g_fit.scene != m->scene) { g_fit.scene = m->scene; g_fit.n = 0; g_fit.eOk = 0; memset(g_fit.qOk, 0, sizeof g_fit.qOk); memset(g_fit.steady, 0, sizeof g_fit.steady); memset(g_fit.qoOk, 0, sizeof g_fit.qoOk); }
  double now[6] = { m->o2x, m->o2y, m->o3x, m->o3y, m->bg3X, m->bg3Y }, cam[2] = { m->camX, m->camY };
  if (g_fit.n == FIT_N) { for (int c = 0; c < 6; c++) memmove(g_fit.v[c], g_fit.v[c] + 1, (FIT_N - 1) * sizeof(double));
                          for (int c = 0; c < 2; c++) memmove(g_fit.cam[c], g_fit.cam[c] + 1, (FIT_N - 1) * sizeof(double)); g_fit.n--; }
  for (int c = 0; c < 6; c++) g_fit.v[c][g_fit.n] = now[c];
  for (int c = 0; c < 2; c++) g_fit.cam[c][g_fit.n] = cam[c];
  g_fit.n++;
  for (int c = 0; c < 8 && g_fit.n > 1; c++) {
    // (a jump in the history -- a re-seed, a teleport, a cut of the camera -- restarts it)
    double a = c < 6 ? now[c] : cam[c - 6], p = c < 6 ? g_fit.v[c][g_fit.n - 2] : g_fit.cam[c - 6][g_fit.n - 2];
    if (a - p > 32 || p - a > 32) {
      for (int k = 0; k < 6; k++) g_fit.v[k][0] = now[k];
      for (int k = 0; k < 2; k++) g_fit.cam[k][0] = cam[k];
      g_fit.n = 1; memset(g_fit.qOk, 0, sizeof g_fit.qOk); memset(g_fit.steady, 0, sizeof g_fit.steady); memset(g_fit.qoOk, 0, sizeof g_fit.qoOk);
    }
  }
  // the camera first: the layers' and the objects' rules below read it
  double qc[2];
  for (int c = 0; c < 2; c++) {
    // (the remainder is cut toward zero, so the whole place is the ROM's own word, not the sixteenths' floor)
    const int d = m->camE ? m->camEX[c] - g_fit.eX[c] : 0;
    double er = d / 16.0;
    const int ek = m->camE && g_fit.eOk && g_fit.n > 1 && d <= 32 * 16 && d >= -32 * 16 &&
                   cam[c] - g_fit.cam[c][g_fit.n - 2] == m->camEW[c] - g_fit.eW[c];
    qc[c] = fitOne(6 + c, g_fit.cam[c], g_fit.n, cam[c], ek ? &er : NULL);
  }
  g_fit.eOk = m->camE;
  if (m->camE) for (int c = 0; c < 2; c++) { g_fit.eW[c] = m->camEW[c]; g_fit.eX[c] = m->camEX[c]; }
  double q[6];
  for (int c = 0; c < 6; c++) {
    // BG2's / BG3's offset over the canvas that only undoes the camera (camera + offset the same all along the history: a
    // layer held to the screen, the bridge's sky at 01C) does not scroll on its own: it stays where the screen is, the
    // camera's continuous place undone. Its fit took the camera's whole-pixel steps for a slope and put the layer half a
    // pixel off the screen between two frames -- the sky shifted, a strip of the layer's other side at the screen's edge.
    int held = c < 4 && g_fit.n > 1;
    for (int i = 1; held && i < g_fit.n; i++)
      if (g_fit.v[c][i] + g_fit.cam[c & 1][i] != g_fit.v[c][0] + g_fit.cam[c & 1][0]) held = 0;
    if (held) { g_fit.qOk[c] = 0; q[c] = now[c] + (cam[c & 1] - qc[c & 1]); continue; }
    // A layer that steps only on the frames the camera moves (a parallax: Magus's castle 0A5, its sky at half the
    // camera's speed) stops with the camera: the fit's slope ran on past the pan's end and the sky crept up to a pixel
    // between the frames, falling back on each one (the owner, 2026-10-24). (Not while the camera's own fit runs: a pan
    // at less than a pixel a frame stands still on most frames.)
    if (c < 4 && g_fit.n > 1 && !g_fit.steady[6 + (c & 1)] && g_fit.cam[c & 1][g_fit.n - 1] == g_fit.cam[c & 1][g_fit.n - 2]) {
      int tied = 0, i = 1;
      for (; i < g_fit.n; i++) {
        if (g_fit.v[c][i] == g_fit.v[c][i - 1]) continue;
        if (g_fit.cam[c & 1][i] == g_fit.cam[c & 1][i - 1]) break;
        tied = 1;
      }
      if (tied && i == g_fit.n) { g_fit.qOk[c] = 0; q[c] = now[c] + (cam[c & 1] - qc[c & 1]); continue; }
    }
    if (c >= 4) { q[c] = fitOne(c, g_fit.v[c], g_fit.n, now[c], NULL); continue; }
    // (an offset the same all along the history -- a layer that scrolls with the camera -- keeps it exactly: its place is
    //  the camera's continuous one plus the offset, never a fraction of its own off the camera's -- a seam across the
    //  overworld's mountain, tiles out of step with the map when a pan turned; the owner, 2026-10-24)
    { int same = 1; for (int i = 1; same && i < g_fit.n; i++) same = g_fit.v[c][i] == g_fit.v[c][0];
      if (same) { g_fit.qOk[c] = 0; q[c] = now[c]; continue; } }
    // (BG2's / BG3's place on the map -- the camera and the offset together -- is what is fitted, its whole part the drawn
    //  one: fitted apart, the camera's fraction and the offset's added past a pixel and the layer stood a pixel off at the
    //  frame itself -- the Blackbird's clouds, 16E)
    double wv[FIT_N];
    for (int i = 0; i < g_fit.n; i++) wv[i] = g_fit.v[c][i] + g_fit.cam[c & 1][i];
    // ...unless the two go opposite ways: the sum of two steady steppers is then no steady line (the 1000 A.D. intro's
    // clouds, 1F0: the camera climbing a pixel every 4 frames, the drift C2/75C3 adds going down one every 8 -- the drawn
    // place goes -1, +1, -1 and the fit took it for no slope, so the clouds stepped whole pixels all the way up; the owner,
    // 2026-10-24). The offset's own fit then goes on the camera's continuous place; the two trail their drawn places from
    // either side, so their sum stays inside a pixel of the drawn one (else, the same way round, the sum's own fit holds).
    if (fitSlope(wv, g_fit.n, 0) == 0 && fitSlope(g_fit.v[c], g_fit.n, 0) != 0) {
      if (!g_fit.qoOk[c]) { g_fit.qo[c] = g_fit.qOk[c] ? g_fit.q[c] - qc[c & 1] : now[c]; g_fit.qoOk[c] = 1; }
      const double qo = fitQ(&g_fit.qo[c], &g_fit.qoOk[c], NULL, g_fit.v[c], g_fit.n, now[c], 0, NULL);
      if (fabs(qo + qc[c & 1] - (now[c] + cam[c & 1])) < 0.999) {
        q[c] = qo; g_fit.q[c] = qo + qc[c & 1]; g_fit.qOk[c] = 1; g_fit.steady[c] = 1; continue; }
    }
    g_fit.qoOk[c] = 0;
    q[c] = fitOne(c, wv, g_fit.n, now[c] + cam[c & 1], NULL) - qc[c & 1];
  }
  m->q2x = q[0]; m->q2y = q[1]; m->q3x = q[2]; m->q3y = q[3]; m->qbx = q[4]; m->qby = q[5];
  m->qcx = qc[0]; m->qcy = qc[1];
  { static int tr = -1; if (tr < 0) tr = getenv("CT_MOTFITTRACE") ? 1 : 0;   // (probe) the fit's places, a line a frame
    if (tr) printf("motfit: g%d n%d cam %d,%d q %.3f,%.3f st %d%d | bg3 %d,%d q3 %.3f,%.3f st %d%d | e %d %d,%d\n", g_fitFrame, g_fit.n,
                   m->camX, m->camY, qc[0], qc[1], g_fit.steady[6], g_fit.steady[7], m->o3x + m->camX, m->o3y + m->camY,
                   q[2] + qc[0], q[3] + qc[1], g_fit.steady[2], g_fit.steady[3], m->camE, m->camE ? m->camEX[0] : 0,
                   m->camE ? m->camEX[1] : 0); }
  // the objects: each its own history; one that has moved with the camera all along (the lead a scroll follows) is placed
  // by the camera's continuous place, so the two never part by a fraction
  for (int i = 0; i < m->nspr; i++) {
    int key = m->sprKey[i];
    if (key < 0 || key >= FIT_KEYS) continue;
    FitSpr* s = &g_fitSpr[key];
    if (s->last == g_fitFrame) {                   // (a key twice in one frame -- an object of several sprites, the
      m->qsx[i] = m->sprX[i] + s->fx; m->qsy[i] = m->sprY[i] + s->fy; continue; }   //  Blackbird's three: the first one's)
    if (s->scene != m->scene || s->last != g_fitFrame - 1 || s->n == 0 ||
        fabs(m->sprX[i] - s->x[s->n - 1]) > 32 || fabs(m->sprY[i] - s->y[s->n - 1]) > 32) { s->n = 0; s->okx = s->oky = 0; s->wx = s->wy = -1; }
    s->scene = m->scene; s->last = g_fitFrame;
    if (s->n == FIT_N) { memmove(s->x, s->x + 1, (FIT_N - 1) * sizeof(double)); memmove(s->y, s->y + 1, (FIT_N - 1) * sizeof(double)); s->n--; }
    s->x[s->n] = m->sprX[i]; s->y[s->n] = m->sprY[i]; s->n++;
    int withCam = s->n > 1 && s->n <= g_fit.n, camMoved = 0;   // (and the camera moved in it: a still object while a
    for (int k = 1; withCam && k < s->n; k++) {                //  still camera settles is the map's, not the screen's)
      int gk = g_fit.n - s->n + k;
      if (s->x[k] - s->x[k - 1] != g_fit.cam[0][gk] - g_fit.cam[0][gk - 1] ||
          s->y[k] - s->y[k - 1] != g_fit.cam[1][gk] - g_fit.cam[1][gk - 1]) withCam = 0;
      if (g_fit.cam[0][gk] != g_fit.cam[0][gk - 1] || g_fit.cam[1][gk] != g_fit.cam[1][gk - 1]) camMoved = 1;
    }
    withCam = withCam && camMoved;
    if (withCam) { m->qsx[i] = m->sprX[i] + (qc[0] - cam[0]); m->qsy[i] = m->sprY[i] + (qc[1] - cam[1]);
                   s->qx = m->qsx[i]; s->qy = m->qsy[i]; s->okx = s->oky = 1; }   // (its own goes on from there)
    else {
      double rx, ry;
      const int kx = s->n > 1 && engineRate16(m->sprWx[i], s->wx, s->x[s->n - 1] - s->x[s->n - 2], &rx);
      const int ky = s->n > 1 && engineRate16(m->sprWy[i], s->wy, s->y[s->n - 1] - s->y[s->n - 2], &ry);
      m->qsx[i] = fitQ(&s->qx, &s->okx, NULL, s->x, s->n, m->sprX[i], 1, kx ? &rx : NULL);
      m->qsy[i] = fitQ(&s->qy, &s->oky, NULL, s->y, s->n, m->sprY[i], 1, ky ? &ry : NULL);
    }
    s->wx = m->sprWx[i]; s->wy = m->sprWy[i];
    { static int tr2 = -1; if (tr2 < 0) tr2 = getenv("CT_MOTFITTRACE") && atoi(getenv("CT_MOTFITTRACE")) >= 2;   // (probe)
      if (tr2) printf("motobj: g%d k%d at %d,%d q %.4f,%.4f w %d,%d\n", g_fitFrame, key, m->sprX[i], m->sprY[i],
                      m->qsx[i], m->qsy[i], m->sprWx[i], m->sprWy[i]); }
    s->fx = m->qsx[i] - m->sprX[i]; s->fy = m->qsy[i] - m->sprY[i];
  }
  m->qFrac = 1;
}
// (the continuous place between two frames against cur's, or the step's straight line where they part from the step)
static double qAt(double qp, double qc, double cur, double tt, int step, double lin) {
  double d = qp + (qc - qp) * tt - cur;
  return (fabs(qc - qp - step) <= 2 && fabs(d) <= 32) ? d : lin;
}
void CtMotCamShift(const CtMotFrame* cur, const CtMotFrame* prev, double tt, int dcx, int dcy, double* dx, double* dy) {
  *dx = -(1.0 - tt) * dcx; *dy = -(1.0 - tt) * dcy;
  if (!cur->qFrac || !prev->qFrac || g_ctMotNoFrac) return;
  *dx = qAt(prev->qcx, cur->qcx, cur->camX, tt, dcx, *dx); *dy = qAt(prev->qcy, cur->qcy, cur->camY, tt, dcy, *dy);
}
void CtMotSprShift(const CtMotFrame* cur, int i, const CtMotFrame* prev, int j, double tt, int wx, int wy, double* dx, double* dy) {
  *dx = -(1.0 - tt) * wx; *dy = -(1.0 - tt) * wy;
  if (!cur->qFrac || !prev->qFrac || g_ctMotNoFrac || j < 0) return;
  *dx = qAt(prev->qsx[j], cur->qsx[i], cur->sprX[i], tt, wx, *dx); *dy = qAt(prev->qsy[j], cur->qsy[i], cur->sprY[i], tt, wy, *dy);
}

static uint8_t dim(uint8_t c, int bright) { return bright >= 15 ? c : bright <= 0 ? 0 : (uint8_t)(c * bright / 15); }

void CtMotSettle(CtMotFrame* m, const uint8_t* pic, const uint8_t* fb, int bright) {
  m->bright = bright;
  for (int i = 0; i < m->w * CT_MOT_H; i++) {
    if (m->own[i] == CT_MOT_FIXED) continue;
    const uint8_t* a = pic + i * 3; const uint8_t* b = fb + i * 3;
    if (dim(a[0], bright) != b[0] || dim(a[1], bright) != b[1] || dim(a[2], bright) != b[2]) { m->own[i] = CT_MOT_FIXED; m->world[i] = 0; }
  }
}

static int floorDiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
// -(256 - t) * d / 256 pixels, in 1/k pixel units, rounded to the nearest
static int shiftOf(int d, int t, int k) {
  int num = -(256 - t) * d * k;                     // / 256
  return num >= 0 ? (num + 128) / 256 : -((-num + 128) / 256);
}

static void upscale(const uint8_t* fb, int w, int k, uint8_t* out) {
  int W = w * k;
  for (int y = 0; y < CT_MOT_H; y++) {
    uint8_t* row = out + (size_t)y * k * W * 3;
    if (k == 1) { memcpy(row, fb + (size_t)y * w * 3, (size_t)w * 3); continue; }
    for (int x = 0; x < w; x++) {
      const uint8_t* s = fb + ((size_t)y * w + x) * 3;
      for (int i = 0; i < k; i++) memcpy(row + ((size_t)x * k + i) * 3, s, 3);
    }
    for (int i = 1; i < k; i++) memcpy(row + (size_t)i * W * 3, row, (size_t)W * 3);
  }
}

// WHO IS IN FRONT where two owners' blocks meet -- pairwise, as the picture was composed: screen-fixed pixels over all,
// then an opaque cloud (the caption plane / BG3 in front); between two sprites the one painted later (OBJ against OBJ is
// the draw order, whatever their priorities); a sprite against the map's priority-1 foreground by the sprite's own front
// bit; any sprite over the map behind it; and the map's layers BG1 > BG2 > BG3 / the caption plane behind.
#define OWN_EMPTY 0xFE
static int bgRank(int o) {
  return (o == CT_MOT_BG1F || o == CT_MOT_BG2F) ? 4 : o == CT_MOT_BG1 ? 3 : o == CT_MOT_BG2 ? 2 : 1;
}
static int wins(int a, int b) {                    // does owner `a` cover owner `b` (b already there)?
  if (b == OWN_EMPTY) return 1;
  int ao = a & 0x7F, bo = b & 0x7F;
  if (ao == CT_MOT_FIXED) return 1;
  if (bo == CT_MOT_FIXED) return 0;
  if (ao == CT_MOT_CAPF) return 1;
  if (bo == CT_MOT_CAPF) return 0;
  int aS = ao >= CT_MOT_SPR, bS = bo >= CT_MOT_SPR;
  if (aS && bS) return ao >= bo;
  if (aS) return bgRank(bo) == 4 ? (a & CT_MOT_FRONT) != 0 : 1;
  if (bS) return bgRank(ao) == 4 ? !(b & CT_MOT_FRONT) : 0;
  return bgRank(ao) >= bgRank(bo);
}

#define JUMP 32                                          // a step this long is a cut, not a motion

void CtMotRaster(const CtMotFrame* cur, const CtMotFrame* prev, const uint8_t* fb, int w, int t, int k, uint8_t* out) {
  if (k < 1) k = 1;
  if (!cur || !prev || !cur->valid || !prev->valid || cur->scene != prev->scene || cur->w != w || prev->w != w ||
      (t >= 256 && (!cur->qFrac || !prev->qFrac || g_ctMotNoFrac))) {   // (at 256 the frame itself, but for the continuous places)
    upscale(fb, w, k, out); return; }
  int dcx = cur->camX - prev->camX, dcy = cur->camY - prev->camY;
  if (abs(dcx) > JUMP || abs(dcy) > JUMP) { upscale(fb, w, k, out); return; }
  // each owner's shift from where this frame drew it, in 1/k pixels: the screen moved D between the frames
  int sx[128], sy[128], any = 0;
  memset(sx, 0, sizeof sx); memset(sy, 0, sizeof sy);
  sx[CT_MOT_BG1] = sx[CT_MOT_BG1F] = shiftOf(-dcx, t, k); sy[CT_MOT_BG1] = sy[CT_MOT_BG1F] = shiftOf(-dcy, t, k);
  // (the camera and the objects at their continuous places, CtMotFit -- not in the owners' check)
  const int frac = cur->qFrac && prev->qFrac && !g_ctMotNoFrac;
  double cdx = -(1.0 - t / 256.0) * dcx, cdy = -(1.0 - t / 256.0) * dcy;
  #define RK(v) ((int)((v) * k + ((v) >= 0 ? 0.5 : -0.5)))
  if (frac) { CtMotCamShift(cur, prev, t / 256.0, dcx, dcy, &cdx, &cdy);
              sx[CT_MOT_BG1] = sx[CT_MOT_BG1F] = RK(-cdx); sy[CT_MOT_BG1] = sy[CT_MOT_BG1F] = RK(-cdy); }
  { int d2x = dcx + cur->o2x - prev->o2x, d2y = dcy + cur->o2y - prev->o2y;   // BG2 / BG3: the camera and their own scroll
    if (abs(d2x) > JUMP || abs(d2y) > JUMP) d2x = d2y = 0;
    sx[CT_MOT_BG2] = sx[CT_MOT_BG2F] = shiftOf(-d2x, t, k); sy[CT_MOT_BG2] = sy[CT_MOT_BG2F] = shiftOf(-d2y, t, k);
    int d3x = dcx + cur->o3x - prev->o3x, d3y = dcy + cur->o3y - prev->o3y;
    if (abs(d3x) > JUMP || abs(d3y) > JUMP) d3x = d3y = 0;
    sx[CT_MOT_BG3] = shiftOf(-d3x, t, k); sy[CT_MOT_BG3] = shiftOf(-d3y, t, k); }
  { int d3x = cur->bg3X - prev->bg3X, d3y = cur->bg3Y - prev->bg3Y;
    if (abs(d3x) > JUMP || abs(d3y) > JUMP) d3x = d3y = 0;
    sx[CT_MOT_CAPB] = sx[CT_MOT_CAPF] = shiftOf(-d3x, t, k); sy[CT_MOT_CAPB] = sy[CT_MOT_CAPF] = shiftOf(-d3y, t, k); }
  // ...the self-scrolling layers at their continuous offsets: the screen moves by the camera's step and by the layer's own
  // offset going from where it was (prev q) toward where it is (cur q), against where this frame drew it (the integer)
  if (frac) {
    double tt = t / 256.0, cx = -cdx, cy = -cdy;
    #define QSH(qp, qc, o) (-((qp) + ((qc) - (qp)) * tt - (o)))
    double s2x = cx + QSH(prev->q2x, cur->q2x, cur->o2x), s2y = cy + QSH(prev->q2y, cur->q2y, cur->o2y);
    double s3x = cx + QSH(prev->q3x, cur->q3x, cur->o3x), s3y = cy + QSH(prev->q3y, cur->q3y, cur->o3y);
    double sbx = QSH(prev->qbx, cur->qbx, cur->bg3X), sby = QSH(prev->qby, cur->qby, cur->bg3Y);
    #undef QSH
    if (abs(cur->o2x - prev->o2x) <= JUMP && abs(cur->o2y - prev->o2y) <= JUMP) {
      sx[CT_MOT_BG2] = sx[CT_MOT_BG2F] = RK(s2x); sy[CT_MOT_BG2] = sy[CT_MOT_BG2F] = RK(s2y); }
    if (abs(cur->o3x - prev->o3x) <= JUMP && abs(cur->o3y - prev->o3y) <= JUMP) { sx[CT_MOT_BG3] = RK(s3x); sy[CT_MOT_BG3] = RK(s3y); }
    if (abs(cur->bg3X - prev->bg3X) <= JUMP && abs(cur->bg3Y - prev->bg3Y) <= JUMP) {
      sx[CT_MOT_CAPB] = sx[CT_MOT_CAPF] = RK(sbx); sy[CT_MOT_CAPB] = sy[CT_MOT_CAPF] = RK(sby); }
  }
  for (int i = 0; i < cur->nspr; i++) {
    int j = 0; while (j < prev->nspr && prev->sprKey[j] != cur->sprKey[i]) j++;
    int dx = 0, dy = 0;                                   // new, or a jump: where this frame has it
    if (j < prev->nspr) {
      int wx = cur->sprX[i] - prev->sprX[j], wy = cur->sprY[i] - prev->sprY[j];
      if (abs(wx) <= JUMP && abs(wy) <= JUMP) {
        dx = wx - dcx; dy = wy - dcy;
        if (frac) { double sdx, sdy; CtMotSprShift(cur, i, prev, j, t / 256.0, wx, wy, &sdx, &sdy);
                    sx[CT_MOT_SPR + i] = RK(sdx - cdx); sy[CT_MOT_SPR + i] = RK(sdy - cdy); continue; }
      }
    }
    if (frac) { sx[CT_MOT_SPR + i] = RK(-cdx); sy[CT_MOT_SPR + i] = RK(-cdy); continue; }   // (new, or a jump: its map place)
    sx[CT_MOT_SPR + i] = shiftOf(dx, t, k); sy[CT_MOT_SPR + i] = shiftOf(dy, t, k);
  }
  #undef RK
  for (int i = 0; i < 128; i++) any |= sx[i] | sy[i];
  if (!any) { upscale(fb, w, k, out); return; }

  int W = w * k, H = CT_MOT_H * k;
  static uint8_t* depth; static size_t depthN;
  if (depthN < (size_t)W * H) { free(depth); depth = (uint8_t*)malloc((size_t)W * H); depthN = depth ? (size_t)W * H : 0; }
  if (!depth) { upscale(fb, w, k, out); return; }
  memset(depth, OWN_EMPTY, (size_t)W * H);
  // every pixel of the frame as a k x k block at its owner's shift; the nearer owner wins an overlap (wins(), above)
  for (int y = 0; y < CT_MOT_H; y++) {
    const uint8_t* orow = cur->own + y * w;
    const uint8_t* srow = fb + (size_t)y * w * 3;
    for (int x = 0; x < w; x++) {
      int own = orow[x], o = own & 0x7F;
      int X0 = x * k + sx[o], Y0 = y * k + sy[o];
      int i0 = X0 < 0 ? -X0 : 0, i1 = X0 + k > W ? W - X0 : k;
      if (i0 >= i1) continue;
      const uint8_t* s = srow + (size_t)x * 3;
      for (int j = 0; j < k; j++) {
        int Y = Y0 + j; if ((unsigned)Y >= (unsigned)H) continue;
        uint8_t* dp = depth + (size_t)Y * W + X0;
        uint8_t* op = out + ((size_t)Y * W + X0) * 3;
        for (int i = i0; i < i1; i++)
          if (wins(own, dp[i])) { dp[i] = (uint8_t)own; op[i * 3] = s[0]; op[i * 3 + 1] = s[1]; op[i * 3 + 2] = s[2]; }
      }
    }
  }
  // ...and every pixel the sprites put down, the covered ones too, at their sprite's shift: where a sprite moved off
  // another, the other is there (the same pairwise depth decides; the visible ones are already in place)
  for (int n = 0; n < cur->nspx; n++) {
    if (cur->spx[n].hole) continue;                // (an occluder's hole: its own pixel, already in place)
    int own = cur->spx[n].own, o = own & 0x7F;
    int X0 = cur->spx[n].x * k + sx[o], Y0 = cur->spx[n].y * k + sy[o];
    uint8_t c[3] = { dim(cur->spx[n].r, cur->bright), dim(cur->spx[n].g, cur->bright), dim(cur->spx[n].b, cur->bright) };
    for (int j = 0; j < k; j++) {
      int Y = Y0 + j; if ((unsigned)Y >= (unsigned)H) continue;
      for (int i = 0; i < k; i++) {
        int X = X0 + i; if ((unsigned)X >= (unsigned)W) continue;
        size_t q = (size_t)Y * W + X;
        if (depth[q] == own || !wins(own, depth[q])) continue;
        depth[q] = (uint8_t)own; memcpy(out + q * 3, c, 3);
      }
    }
  }
  // what a moving object uncovered (no block reached it): the map canvas at the shifted camera, under this frame's
  // brightness -- black outside the canvas or the scroll mask's columns
  int bx = sx[CT_MOT_BG1], by = sy[CT_MOT_BG1];
  for (int Y = 0; Y < H; Y++) {
    uint8_t* dp = depth + (size_t)Y * W;
    if (!memchr(dp, OWN_EMPTY, (size_t)W)) continue;
    uint8_t* d = out + (size_t)Y * W * 3;
    const uint8_t* crow = NULL;
    if (cur->canvas && cur->cW > 0 && cur->cH > 0) {
      int cy = (cur->canvasY0 + floorDiv(Y - by, k)) % cur->cH; if (cy < 0) cy += cur->cH;
      crow = cur->canvas + (size_t)cy * cur->cW * 3;
    }
    for (int X = 0; X < W; X++) {
      if (dp[X] != OWN_EMPTY) continue;
      uint8_t* q = d + (size_t)X * 3;
      int ux = cur->canvasX0 + floorDiv(X - bx, k);
      if (!crow || ux < cur->mx0 || ux >= cur->mx1) { q[0] = q[1] = q[2] = 0; continue; }
      int cx = ux % cur->cW; if (cx < 0) cx += cur->cW;
      const uint8_t* s = crow + (size_t)cx * 3;
      q[0] = dim(s[0], cur->bright); q[1] = dim(s[1], cur->bright); q[2] = dim(s[2], cur->bright);
    }
  }
}
