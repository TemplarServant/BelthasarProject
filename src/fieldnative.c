// fieldnative.c - the field's frames between, drawn NATIVELY (a mod: settings.h interpolate; CLAUDE.md "MODS ARE OUTSIDE THE
// MANDATE"). The reprojection (motion.c CtMotRaster) shifts the pixels of the composed frame; this redraws the frame at the
// moment t between two logic frames from what it is made of:
//   - the map: its layers (scene.c CtFieldSceneLayers -- BG1/BG2/BG3 x priority 0/1, one period each), sampled at the
//     camera's and each layer's own scroll AT t (the self-scrolling layers at their continuous offset, CtMotFit), with the
//     bake's own layer order, sub screen and colour math, per output pixel;
//   - the sprites: every pixel each one put down this frame (CtMotFrame.spx, in paint order: the later in front, an
//     occluder's hole clears what is under it), each sprite at its object's place at t; a sprite pixel shows unless the
//     map's priority-1 foreground is there and the pixel is not in front of it;
//   - what is screen-fixed (the box, the curtain, the blank lines -- CtMotSettle's FIXED): the composed frame's pixel.
// No hole to fill, no pixel of two layers moved as one. The caption plane, the iris and the colour window are not drawn here
// (the caller keeps the reprojection for a frame with any of them).
#include "fieldnative.h"
#include "motion.h"
#include "scene.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BL1 1
#define BL2 2
#define BL3 4

static long fdiv(long a, long b) { long q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
static int wrapi(long v, int n) { long r = v % n; if (r < 0) r += n; return (int)r; }
static uint8_t dimc(uint8_t c, int bright) { return bright >= 15 ? c : bright <= 0 ? 0 : (uint8_t)(c * bright / 15); }

int CtFieldRasterNative(const CtMotFrame* cur, const CtMotFrame* prev, const uint8_t* fb, int w, int t, int k, uint8_t* out) {
  const CtSceneLayers* L = (const CtSceneLayers*)cur->native;
  if (!L || !cur->valid || !prev || !prev->valid || cur->scene != prev->scene || cur->w != w || prev->w != w || k < 1) return 0;
  int dcx = cur->camX - prev->camX, dcy = cur->camY - prev->camY;
  if (abs(dcx) > 32 || abs(dcy) > 32) return 0;
  const double tt = t / 256.0, back = 1.0 - tt;
  const int W = w * k, H = 224 * k;
  // the camera and the layers' own offsets at t, in 1/k pixels
  double camXt = cur->camX - back * dcx, camYt = cur->camY - back * dcy;
  double o2x = cur->o2x - back * (cur->o2x - prev->o2x), o2y = cur->o2y - back * (cur->o2y - prev->o2y);
  double o3x = cur->o3x - back * (cur->o3x - prev->o3x), o3y = cur->o3y - back * (cur->o3y - prev->o3y);
  if (cur->qFrac && prev->qFrac) {
    o2x = prev->q2x + (cur->q2x - prev->q2x) * tt; o2y = prev->q2y + (cur->q2y - prev->q2y) * tt;
    o3x = prev->q3x + (cur->q3x - prev->q3x) * tt; o3y = prev->q3y + (cur->q3y - prev->q3y) * tt;
  }
  if (fabs(o2x - cur->o2x) > 32 || fabs(o2y - cur->o2y) > 32) { o2x = cur->o2x; o2y = cur->o2y; }
  if (fabs(o3x - cur->o3x) > 32 || fabs(o3y - cur->o3y) > 32) { o3x = cur->o3x; o3y = cur->o3y; }
  // EVERY PLACE AT t BY ONE RULE: the whole 1/k pixel under it on the map, floor(p * k). The screen shows map pixel
  // camXk / k + X / k, a layer its pixel at the camera plus its offset, an object its map place less the camera -- each
  // the floor of its own sum, so whatever moves together at t lands together: a layer held to the camera (BG1, a BG2
  // part of the map) and one held to the screen (camera + offset constant, a sky) exactly, an object standing on the map
  // or walking with the camera exactly. (Rounded apart -- the camera to the nearest, the offset down -- a layer held to
  // the screen came out a quarter pixel off on some t and showed a column of its other side at the screen's edge.) The
  // floor is also the drawn frame's: the drawn offset is the whole part of the continuous one, so at t = 256 and 1x this
  // is the frame as composed.
  #define FLK(p) ((long)floor((p) * k + 1e-9))
  long camXk = FLK(camXt), camYk = FLK(camYt);
  long offXk[6], offYk[6];
  { const double ox[6] = { o3x, o2x, 0, o2x, 0, o3x }, oy[6] = { o3y, o2y, 0, o2y, 0, o3y };
    for (int i = 0; i < 6; i++) { offXk[i] = FLK(camXt + ox[i]); offYk[i] = FLK(camYt + oy[i]); } }
  // each object's shift at t (its place at t against where this frame drew it), in 1/k pixels
  int sx[128], sy[128];
  memset(sx, 0, sizeof sx); memset(sy, 0, sizeof sy);
  for (int i = 0; i < cur->nspr && i < 120; i++) {
    int j = 0; while (j < prev->nspr && prev->sprKey[j] != cur->sprKey[i]) j++;
    if (j < prev->nspr) {                                 // (new, or a jump: where this frame has it on the screen)
      int wx = cur->sprX[i] - prev->sprX[j], wy = cur->sprY[i] - prev->sprY[j];
      if (abs(wx) <= 32 && abs(wy) <= 32) {
        // (its map place at t, floor(sprX - back * wx), less the screen's at t, camXk, against where this frame drew it:
        // the whole pixels cancel)
        sx[CT_MOT_SPR + i] = (int)(FLK(-back * wx) - FLK(-back * dcx)); sy[CT_MOT_SPR + i] = (int)(FLK(-back * wy) - FLK(-back * dcy));
      }
    }
  }
  #undef FLK
  // the sprites, in paint order, into their own plane: 0 none, 1 a sprite pixel, 2 one in front of the foreground
  static uint8_t* sp; static uint8_t* spf; static uint8_t* spp; static size_t spN;
  if (spN < (size_t)W * H) {
    free(sp); free(spf); free(spp);
    sp = (uint8_t*)malloc((size_t)W * H * 3); spf = (uint8_t*)malloc((size_t)W * H); spp = (uint8_t*)malloc((size_t)W * H);
    spN = (sp && spf && spp) ? (size_t)W * H : 0;
  }
  if (!spN) return 0;
  memset(spf, 0, (size_t)W * H);
  for (int n = 0; n < cur->nspx; n++) {
    int own = cur->spx[n].own, o = own & 0x7F;
    int X0 = cur->spx[n].x * k + sx[o], Y0 = cur->spx[n].y * k + sy[o];
    uint8_t f = cur->spx[n].hole ? 0 : (own & CT_MOT_FRONT) ? 2 : 1;
    for (int j = 0; j < k; j++) {
      int Y = Y0 + j; if ((unsigned)Y >= (unsigned)H) continue;
      for (int i = 0; i < k; i++) {
        int X = X0 + i; if ((unsigned)X >= (unsigned)W) continue;
        size_t q = (size_t)Y * W + X;
        // (in front of the foreground is the PIXEL's, as the compositor's front mask: the sprite that wins it carries its
        // own priority -- a later priority-2 unit over a priority-3 one goes behind the foreground again, drawSprite)
        spf[q] = f;
        if (f) { sp[q * 3] = cur->spx[n].r; sp[q * 3 + 1] = cur->spx[n].g; sp[q * 3 + 2] = cur->spx[n].b; spp[q] = cur->spx[n].pal; }
      }
    }
  }
  const int mainT = L->mainT, subT = L->subT, blend = L->blendEn, fgT = mainT | subT;
  static const uint8_t bitOf[6] = { BL3, BL2, BL1, BL2, BL1, BL3 };
  const int bright = cur->bright, wide = w > 256;
  int act[6];
  for (int i = 0; i < 6; i++) act[i] = ((mainT | subT) & bitOf[i]) != 0;
  // a still backdrop (field.c fieldWideStill): held to the picture -- its layer column at the view's column 0 (stillK) shown
  // at the picture's middle 256 (from stillA), in 1/k pixels -- and mirrored past them
  long stillK[6], stillA[6]; int still[6];
  for (int i = 0; i < 6; i++) {
    still[i] = bitOf[i] != BL1 && (L->still & bitOf[i]);
    long o = still[i] ? cur->camX + (bitOf[i] == BL2 ? cur->o2x : cur->o3x) : 0;
    stillK[i] = (o + L->viewX) * k; stillA[i] = (o + (w - 256) / 2) * k;
  }
  // the columns, once for every row: each layer's x, the 1x column, the scroll mask (the widescreen's sides)
  static int* tab; static size_t tabN;
  if (tabN < (size_t)W * 12) { free(tab); tab = (int*)malloc((size_t)W * 12 * sizeof(int)); tabN = tab ? (size_t)W * 12 : 0; }
  if (!tab) return 0;
  int* x1T = tab + 6 * W; int* inT = tab + 7 * W; int* fxT = tab + 8 * W; int* fcT = tab + 9 * W;
  int* fx2T = tab + 10 * W; int* fc2T = tab + 11 * W;
  for (int X = 0; X < W; X++) {
    x1T[X] = X / k;
    long cx = fdiv(camXk + X, k);
    inT[X] = !wide || (cx >= cur->mx0 && cx < cur->mx1);
    // (the frame's own column showing map column cx: a world pixel the frame drew screen-fixed -- the Steam margins,
    //  field.c, CtMotFrame.world -- is taken from there, so it moves with the camera at t. Copied at the screen's place
    //  they stepped at 60 Hz beside a room drawn at t -- the owner, 2026-10-23 -- and a room column that fell past the
    //  edge at t, 01C's shake, took the self-scrolling sky instead of them, a column of cloud in the margins)
    long xs = cx - cur->camX;
    fxT[X] = xs >= 0 && xs < w ? (int)xs : -1;
    fcT[X] = xs < 0 ? 0 : xs >= w ? w - 1 : (int)xs;   // (a side column the frame never drew -- the picture's edge, a step of
                                                        //  the camera at t -- takes the frame's nearest: 01C's shake left a
                                                        //  column of the sky at the picture's left edge, the owner 2026-10-24)
    // (the margins' BG2 plane -- world 2 -- at BG2's place at t: the frame's column showing BG2's column there. Taken at the
    //  camera's, 0A5's sky climbed a pixel between two frames and fell back on the next, at the end of its pan -- the owner,
    //  2026-10-24)
    long xs2 = fdiv(offXk[1] + X, k) - (cur->camX + cur->o2x);
    fx2T[X] = xs2 >= 0 && xs2 < w ? (int)xs2 : -1;
    fc2T[X] = xs2 < 0 ? 0 : xs2 >= w ? w - 1 : (int)xs2;
    for (int i = 0; i < 6; i++) {
      long u = offXk[i] + X;
      if (still[i]) { long p = 512L * k, s = ((u - stillA[i]) % p + p) % p; u = stillK[i] + (s < 256L * k ? s : p - 1 - s); }
      tab[i * W + X] = act[i] ? wrapi(fdiv(u, k), L->w[i]) : 0;
    }
  }
  // the iris, the colour window, the caption plane (the frame's own; the caption plane at its scroll at t)
  const CtFieldNativeFx* fx = (const CtFieldNativeFx*)cur->nativeFx;
  long capXk = 0, capYk = 0;
  if (fx && fx->capOn) {
    double bx = cur->bg3X - back * (cur->bg3X - prev->bg3X), by = cur->bg3Y - back * (cur->bg3Y - prev->bg3Y);
    if (cur->qFrac && prev->qFrac) { bx = prev->qbx + (cur->qbx - prev->qbx) * tt; by = prev->qby + (cur->qby - prev->qby) * tt; }
    if (fabs(bx - cur->bg3X) > 32 || fabs(by - cur->bg3Y) > 32) { bx = cur->bg3X; by = cur->bg3Y; }
    capXk = (long)floor(bx * k + 1e-9); capYk = (long)floor(by * k + 1e-9);
  }
  int Y;
  #pragma omp parallel for schedule(dynamic, 16)
  for (Y = 0; Y < H; Y++) {
    int y1 = Y / k;
    uint8_t* o = out + (size_t)Y * W * 3;
    const uint8_t* ownRow = cur->own + (size_t)y1 * w; const uint8_t* fbRow = fb + (size_t)y1 * w * 3;
    const uint8_t* lrow[6]; const uint8_t* crow[6];
    for (int i = 0; i < 6; i++) {
      int ly = wrapi(fdiv(offYk[i] + Y, k), L->h[i]);
      lrow[i] = L->rgb[i] + (size_t)ly * L->w[i] * 3; crow[i] = L->cov[i] + (size_t)ly * L->w[i];
    }
    const uint8_t* spRow = sp + (size_t)Y * W * 3; const uint8_t* spfRow = spf + (size_t)Y * W; const uint8_t* sppRow = spp + (size_t)Y * W;
    const int capY = fx && fx->capOn ? wrapi(fdiv(capYk + Y, k), fx->capH) : 0;
    const long ys = fdiv(camYk + Y, k) - cur->camY;    // (the frame's row showing this one's map row at t: the world pixels)
    const uint8_t* worldRow = ys >= 0 && ys < CT_MOT_H ? cur->world + (size_t)ys * w : NULL;
    const uint8_t* fbWorld = worldRow ? fb + (size_t)ys * w * 3 : NULL;
    const uint8_t* worldHere = cur->world + (size_t)y1 * w;
    const long ys2 = fdiv(offYk[1] + Y, k) - (cur->camY + cur->o2y);   // (...and BG2's row there: the margins' BG2 plane)
    const uint8_t* world2Row = ys2 >= 0 && ys2 < CT_MOT_H ? cur->world + (size_t)ys2 * w : NULL;
    const uint8_t* fbWorld2 = world2Row ? fb + (size_t)ys2 * w * 3 : NULL;
    for (int X = 0; X < W; X++, o += 3) {
      int x1 = x1T[X];
      if (worldRow && fxT[X] >= 0 && worldRow[fxT[X]] == 1) { memcpy(o, fbWorld + fxT[X] * 3, 3); continue; }
      if (world2Row && fx2T[X] >= 0 && world2Row[fx2T[X]] == 2) { memcpy(o, fbWorld2 + fx2T[X] * 3, 3); continue; }
      if (ownRow[x1] == CT_MOT_FIXED && !worldHere[x1]) { memcpy(o, fbRow + x1 * 3, 3); continue; }
      if (!inT[X] && worldRow && fxT[X] < 0 && worldRow[fcT[X]] == 1) { memcpy(o, fbWorld + fcT[X] * 3, 3); continue; }
      if (!inT[X] && world2Row && (fx2T[X] < 0 || world2Row[fx2T[X]] != 2) && world2Row[fc2T[X]] == 2) { memcpy(o, fbWorld2 + fc2T[X] * 3, 3); continue; }
      if (!inT[X] && worldHere[x1]) { memcpy(o, fbRow + x1 * 3, 3); continue; }   // (where the two planes meet: the frame's own)
      if (!inT[X]) {                                     // the widescreen's sides end at the scroll mask -- but for the layers
        uint8_t c[3] = { 0, 0, 0 };                      //   scrolling on their own (field.c fieldBeyondPixel)
        if (L->beyond) {
          int i = 5;
          for (; i >= 0; i--) if ((L->beyond & bitOf[i]) && (mainT & bitOf[i]) && act[i] && crow[i][tab[i * W + X]]) break;
          if (i >= 0) memcpy(c, lrow[i] + tab[i * W + X] * 3, 3); else memcpy(c, L->backdrop, 3);
        }
        o[0] = dimc(c[0], bright); o[1] = dimc(c[1], bright); o[2] = dimc(c[2], bright);
        continue;
      }
      int cv[6];
      for (int i = 0; i < 6; i++) cv[i] = act[i] ? crow[i][tab[i * W + X]] : 0;
      const int xv = fx ? x1 - fx->viewX : 0, inView = xv >= 0 && xv < 256;
      // the iris: window 1 takes BG1 and BG2 off the screen inside its row
      const int iris = fx && fx->irisOn && y1 < 224 && xv >= fx->irisL[y1] && xv <= fx->irisR[y1];
      const int mT = iris ? mainT & ~(BL1 | BL2) : mainT, sT = iris ? subT & ~(BL1 | BL2) : subT;
      int mt = -1, st = -1, mtFull = -1;
      for (int i = 5; i >= 0; i--) if ((mainT & bitOf[i]) && cv[i]) { mtFull = i; break; }
      for (int i = 5; i >= 0; i--) if ((mT & bitOf[i]) && cv[i]) { mt = i; break; }
      if (sT) for (int i = 5; i >= 0; i--) if ((sT & bitOf[i]) && cv[i]) { st = i; break; }
      int mc = mt >= 0 ? bitOf[mt] : 0, sc = st >= 0 ? bitOf[st] : 0;
      uint8_t c[3];
      if (mt >= 0) memcpy(c, lrow[mt] + tab[mt * W + X] * 3, 3);
      if (sT) {
        if ((mc & blend) && sc) {
          const uint8_t* s = lrow[st] + tab[st * W + X] * 3;
          for (int ch = 0; ch < 3; ch++) {
            int d = c[ch] >> 3, v = s[ch] >> 3, r = L->subOp ? d - v : d + v;
            if (L->half) r >>= 1;
            if (r < 0) r = 0; if (r > 31) r = 31;
            c[ch] = (uint8_t)((r << 3) | (r >> 2));
          }
        } else if (!mc && sc) memcpy(c, lrow[st] + tab[st * W + X] * 3, 3);
      }
      if (!mc && !(sT && sc)) memcpy(c, L->backdrop, 3);
      // the caption plane's back cells, where the map shows its backdrop (not inside the iris: the window's picture is
      // the map without BG1 / BG2)
      int capX = 0;
      if (fx && fx->capOn && inView) {
        capX = wrapi(fdiv(capXk + X, k), fx->capW);
        size_t ci = (size_t)capY * fx->capW + capX;
        if (!iris && mtFull < 0 && fx->capOpq[0][ci]) memcpy(c, fx->capRgb[0] + ci * 3, 3);
      }
      // a sprite, unless the priority-1 foreground (BG1 / BG2) is here and the pixel is not in front of it
      int sprPal = 0xFF;
      if (spfRow[X]) {
        int fg = !iris && (((fgT & BL1) && cv[4]) || ((fgT & BL2) && cv[3]));
        if (spfRow[X] == 2 || !fg) { memcpy(c, spRow + X * 3, 3); sprPal = sppRow[X]; }
      }
      if (fx && fx->capOn && inView) {                   // ...its front cells over everything
        size_t ci = (size_t)capY * fx->capW + capX;
        if (fx->capOpq[1][ci]) memcpy(c, fx->capRgb[1] + ci * 3, 3);
      }
      // the colour window: inside its row, the fixed colour added / subtracted where the pixel's layer has its math bit
      if (fx && fx->colOn && y1 < 224 && xv >= (fx->colL[y1] < 0 ? 0 : fx->colL[y1]) && xv <= (fx->colR[y1] > 255 ? 255 : fx->colR[y1])) {
        int on;
        if (sprPal != 0xFF) on = (fx->colMath & 0x10) && sprPal >= 4;
        else { int lay = mtFull >= 0 ? bitOf[mtFull] : 0; on = lay ? (fx->colMath & lay) != 0 : (fx->colMath & 0x20) != 0; }
        if (on)
          for (int ch = 0; ch < 3; ch++) {
            int c5 = c[ch] >> 3, r = (fx->colMath & 0x80) ? c5 - fx->colFix[ch] : c5 + fx->colFix[ch];
            if (fx->colHalf) r >>= 1;
            if (r > 31) r = 31;
            if (r < 0) r = 0;
            c[ch] = (uint8_t)((r << 3) | (r >> 2));
          }
      }
      o[0] = dimc(c[0], bright); o[1] = dimc(c[1], bright); o[2] = dimc(c[2], bright);
    }
  }
  return 1;
}
