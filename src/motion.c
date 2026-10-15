// motion.c - see motion.h.
#include "motion.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void CtMotBegin(CtMotFrame* m, int w) {
  m->nspx = 0; m->native = NULL; m->owNative = NULL; m->nativeFx = NULL;
  m->valid = 0; m->w = w < 1 ? 1 : w > CT_MOT_W ? CT_MOT_W : w; m->mx0 = -(1 << 30); m->mx1 = 1 << 30; m->nspr = 0; m->canvas = NULL; m->cW = m->cH = 0; m->bright = 15;
  m->camX = m->camY = m->bg3X = m->bg3Y = 0; m->o2x = m->o2y = m->o3x = m->o3y = 0; m->canvasX0 = m->canvasY0 = 0;
  memset(m->own, CT_MOT_FIXED, sizeof m->own);
}

int CtMotSprite(CtMotFrame* m, int key, int x, int y) {
  if (!m || m->nspr >= CT_MOT_MAXSPR) return CT_MOT_FIXED;
  m->sprKey[m->nspr] = key; m->sprX[m->nspr] = x; m->sprY[m->nspr] = y;
  return CT_MOT_SPR + m->nspr++;
}

// ---- the self-scrolling layers' continuous offsets (motion.h CtMotFit) ---------------------------------------------------
int g_ctMotNoFrac;                                 // (CT_MOTCHECK: the owners against the frame as drawn -- whole pixels)
#define FIT_N 12
static struct { int scene, n; double v[6][FIT_N]; double cam[2][FIT_N]; double q[6]; int qOk[6]; } g_fit;
// The layer's slope over the history (least squares); 0 = not one steady sub-pixel slope.
static double fitSlope(const double* v, int n) {
  if (n < 6) return 0;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < n; i++) { sx += i; sy += v[i]; sxx += (double)i * i; sxy += i * v[i]; }
  double den = n * sxx - sx * sx; if (den == 0) return 0;
  double b = (n * sxy - sx * sy) / den, a = (sy - b * sx) / n;
  if (b == 0 || b > 2.0 || b < -2.0) return 0;     // still, or fast: whole pixels a frame need no fit
  for (int i = 0; i < n; i++) { double r = v[i] - (a + b * i); if (r > 0.75 || r < -0.75) return 0; }   // not steady
  return b;
}
// The continuous offset: the last one advanced by the slope, kept inside [the drawn offset, +1) -- the drawn one is the
// whole part of where the layer really is, so a step re-phases it and the advance per frame stays the slope.
static double fitOne(int c, const double* v, int n, double cur) {
  double b = fitSlope(v, n);
  if (b == 0) { g_fit.qOk[c] = 0; return cur; }
  double q = g_fit.qOk[c] ? g_fit.q[c] + b : cur + 0.5;
  if (q < cur) q = cur;
  if (q > cur + 0.999) q = cur + 0.999;
  g_fit.q[c] = q; g_fit.qOk[c] = 1;
  return q;
}
void CtMotFit(CtMotFrame* m) {
  m->qFrac = 0;
  if (!m->valid) { g_fit.n = 0; memset(g_fit.qOk, 0, sizeof g_fit.qOk); return; }
  if (g_fit.scene != m->scene) { g_fit.scene = m->scene; g_fit.n = 0; memset(g_fit.qOk, 0, sizeof g_fit.qOk); }
  double now[6] = { m->o2x, m->o2y, m->o3x, m->o3y, m->bg3X, m->bg3Y }, cam[2] = { m->camX, m->camY };
  if (g_fit.n == FIT_N) { for (int c = 0; c < 6; c++) memmove(g_fit.v[c], g_fit.v[c] + 1, (FIT_N - 1) * sizeof(double));
                          for (int c = 0; c < 2; c++) memmove(g_fit.cam[c], g_fit.cam[c] + 1, (FIT_N - 1) * sizeof(double)); g_fit.n--; }
  for (int c = 0; c < 6; c++) g_fit.v[c][g_fit.n] = now[c];
  for (int c = 0; c < 2; c++) g_fit.cam[c][g_fit.n] = cam[c];
  g_fit.n++;
  double q[6];
  for (int c = 0; c < 6; c++) {
    // (a jump in the history -- a re-seed, a teleport -- restarts it)
    if (g_fit.n > 1 && (now[c] - g_fit.v[c][g_fit.n - 2] > 32 || g_fit.v[c][g_fit.n - 2] - now[c] > 32)) {
      for (int k = 0; k < 6; k++) g_fit.v[k][0] = now[k];
      for (int k = 0; k < 2; k++) g_fit.cam[k][0] = cam[k];
      g_fit.n = 1; memset(g_fit.qOk, 0, sizeof g_fit.qOk);
    }
  }
  for (int c = 0; c < 6; c++) {
    // BG2's / BG3's offset over the canvas that only undoes the camera (camera + offset the same all along the history: a
    // layer held to the screen, the bridge's sky at 01C) does not scroll on its own: the camera's own interpolation already
    // places it. Its fit took the camera's whole-pixel steps for a slope and put the layer half a pixel off the screen
    // between two frames -- the sky shifted, a strip of the layer's other side at the screen's edge.
    int held = c < 4 && g_fit.n > 1;
    for (int i = 1; held && i < g_fit.n; i++)
      if (g_fit.v[c][i] + g_fit.cam[c & 1][i] != g_fit.v[c][0] + g_fit.cam[c & 1][0]) held = 0;
    if (held) { g_fit.qOk[c] = 0; q[c] = now[c]; continue; }
    q[c] = fitOne(c, g_fit.v[c], g_fit.n, now[c]);
  }
  m->q2x = q[0]; m->q2y = q[1]; m->q3x = q[2]; m->q3y = q[3]; m->qbx = q[4]; m->qby = q[5];
  m->qFrac = 1;
}

static uint8_t dim(uint8_t c, int bright) { return bright >= 15 ? c : bright <= 0 ? 0 : (uint8_t)(c * bright / 15); }

void CtMotSettle(CtMotFrame* m, const uint8_t* pic, const uint8_t* fb, int bright) {
  m->bright = bright;
  for (int i = 0; i < m->w * CT_MOT_H; i++) {
    if (m->own[i] == CT_MOT_FIXED) continue;
    const uint8_t* a = pic + i * 3; const uint8_t* b = fb + i * 3;
    if (dim(a[0], bright) != b[0] || dim(a[1], bright) != b[1] || dim(a[2], bright) != b[2]) m->own[i] = CT_MOT_FIXED;
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
  if (t >= 256 || !cur || !prev || !cur->valid || !prev->valid || cur->scene != prev->scene || cur->w != w || prev->w != w) {
    upscale(fb, w, k, out); return; }
  int dcx = cur->camX - prev->camX, dcy = cur->camY - prev->camY;
  if (abs(dcx) > JUMP || abs(dcy) > JUMP) { upscale(fb, w, k, out); return; }
  // each owner's shift from where this frame drew it, in 1/k pixels: the screen moved D between the frames
  int sx[128], sy[128], any = 0;
  memset(sx, 0, sizeof sx); memset(sy, 0, sizeof sy);
  sx[CT_MOT_BG1] = sx[CT_MOT_BG1F] = shiftOf(-dcx, t, k); sy[CT_MOT_BG1] = sy[CT_MOT_BG1F] = shiftOf(-dcy, t, k);
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
  if (cur->qFrac && prev->qFrac && !g_ctMotNoFrac) {
    double tt = t / 256.0, cx = (1.0 - tt) * dcx, cy = (1.0 - tt) * dcy;
    #define QSH(qp, qc, o) (-((qp) + ((qc) - (qp)) * tt - (o)))
    double s2x = cx + QSH(prev->q2x, cur->q2x, cur->o2x), s2y = cy + QSH(prev->q2y, cur->q2y, cur->o2y);
    double s3x = cx + QSH(prev->q3x, cur->q3x, cur->o3x), s3y = cy + QSH(prev->q3y, cur->q3y, cur->o3y);
    double sbx = QSH(prev->qbx, cur->qbx, cur->bg3X), sby = QSH(prev->qby, cur->qby, cur->bg3Y);
    #undef QSH
    #define RK(v) ((int)((v) * k + ((v) >= 0 ? 0.5 : -0.5)))
    if (abs(cur->o2x - prev->o2x) <= JUMP && abs(cur->o2y - prev->o2y) <= JUMP) {
      sx[CT_MOT_BG2] = sx[CT_MOT_BG2F] = RK(s2x); sy[CT_MOT_BG2] = sy[CT_MOT_BG2F] = RK(s2y); }
    if (abs(cur->o3x - prev->o3x) <= JUMP && abs(cur->o3y - prev->o3y) <= JUMP) { sx[CT_MOT_BG3] = RK(s3x); sy[CT_MOT_BG3] = RK(s3y); }
    if (abs(cur->bg3X - prev->bg3X) <= JUMP && abs(cur->bg3Y - prev->bg3Y) <= JUMP) {
      sx[CT_MOT_CAPB] = sx[CT_MOT_CAPF] = RK(sbx); sy[CT_MOT_CAPB] = sy[CT_MOT_CAPF] = RK(sby); }
    #undef RK
  }
  for (int i = 0; i < cur->nspr; i++) {
    int j = 0; while (j < prev->nspr && prev->sprKey[j] != cur->sprKey[i]) j++;
    int dx = 0, dy = 0;                                   // new, or a jump: where this frame has it
    if (j < prev->nspr) {
      int wx = cur->sprX[i] - prev->sprX[j], wy = cur->sprY[i] - prev->sprY[j];
      if (abs(wx) <= JUMP && abs(wy) <= JUMP) { dx = wx - dcx; dy = wy - dcy; }
    }
    sx[CT_MOT_SPR + i] = shiftOf(dx, t, k); sy[CT_MOT_SPR + i] = shiftOf(dy, t, k);
  }
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
