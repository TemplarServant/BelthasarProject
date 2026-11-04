// title_intro_mods.c -- the title sequence's picture at the host's options (CLAUDE.md, "MODS ARE OUTSIDE THE MANDATE"):
// wider (the widescreen's columns past the 256), sharper (the internal resolution: mode 7 sampled at the sub-pixel) and
// between two frames (the matrix and BG1's scroll t/256 of the way from the previous latch) -- title_intro.h
// CtTitleIntroRenderEx. The ROM's own picture is CtTitleIntroRender (title_intro.c).
//
// Not a translation unit of its own: title_intro.c includes this file at its end, because the mod draws with that file's
// static compositor (TiLine, the HDMA walker tiHdmaInit / tiHdmaRun, tiWinState, tiBgPixel, tiOPT, the mode-7 fetch
// tiM7Tile / tiM7Starts). CMake lists title_intro.c alone, and its OpenMP option (CT_OMP_SRC) is the one the parallel
// loop below needs.
#ifndef CT_TITLE_INTRO_MODS_HOST
#error "title_intro_mods.c is compiled as part of title_intro.c (included at its end), not on its own"
#endif

// ---- The host's options: wider, sharper, between two frames (title_intro.h CtTitleIntroRenderEx) ---------------------
// tiEvalSprites for the host's wide picture: pix/prio are w1 columns wide (the view's column xv at xv + x0). Every sprite
// stays where the PPU puts it, inside the view's 256 columns -- except the copyright block's (the latch's anchor slots),
// which the widescreen moves by the right side's width so the block keeps its place against the screen's right end.
static void tiEvalSpritesEx(const CtTitleIntro* t, int line, uint8_t* pix, uint8_t* prio, int w1, int x0) {
  const uint8_t* o = t->l.oam;
  const uint8_t* hi = t->l.oam + 0x200;
  const int dxA = w1 - 256 - x0;
  uint8_t found[32];
  int nfound = 0, ntiles = 0;
  memset(pix, 0, (size_t)w1);
  for (int i = 0; i < 128; i++) {
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    if (row < size) {
      int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
      if (x > 255) x -= 512;
      if (x > -size) {
        if (++nfound > 32) { nfound = 32; break; }
        found[nfound - 1] = (uint8_t)i;
      }
    }
  }
  for (int k = nfound; k > 0; k--) {
    int i = found[k - 1];
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
    if (x > 255) x -= 512;
    if (x <= -size) continue;
    const int anch = i >= t->l.anchorLo && i < t->l.anchorHi;
    const int lo = anch ? -x0 : 0, hiX = anch ? w1 - x0 : 256, sh = anch ? dxA : 0;
    int tile = o[i * 4 + 2];
    int attr = o[i * 4 + 3];
    int pal = (attr >> 1) & 7;
    int hFlip = attr & 0x40;
    if (attr & 0x80) row = (uint8_t)(size - 1 - row);
    for (int c = 0; c < size; c += 8) {
      if (c + x <= -8 || c + x >= 256) continue;
      if (++ntiles > 34) break;
      int usedCol = hFlip ? size - 1 - c : c;
      uint8_t used = (uint8_t)((((tile >> 4) + (row / 8)) << 4) | (((tile & 0xF) + (usedCol / 8)) & 0xF));
      int adr1 = (t->obsel & 7) << 13;
      int base = (attr & 1) ? adr1 + (((t->obsel & 0x18) + 8) << 9) : adr1;
      uint16_t p1 = tiVram(t, base + used * 16 + (row & 7));
      uint16_t p2 = tiVram(t, base + used * 16 + 8 + (row & 7));
      for (int px = 0; px < 8; px++) {
        int shb = hFlip ? px : 7 - px;
        int v = ((p1 >> shb) & 1) | (((p1 >> (8 + shb)) & 1) << 1) |
                (((p2 >> shb) & 1) << 2) | (((p2 >> (8 + shb)) & 1) << 3);
        int sc = c + x + px + sh;
        if (v > 0 && sc >= lo && sc < hiX) {
          pix[sc + x0] = (uint8_t)(0x80 + 16 * pal + v);
          prio[sc + x0] = (uint8_t)((attr & 0x30) >> 4);
        }
      }
    }
    if (ntiles > 34) break;
  }
}

// tiGetPixel with the mode-7 sample handed in (taken at the sub-pixel) and the view's edges: outside the SNES's 256
// columns there are no sprites and no tiled layer (their maps wrap), only mode 7.
static void tiOPTEx(const CtTitleIntro* t, const TiLine* L, int layer, int* lx, int* ly);
static int tiGetPixelEx(const CtTitleIntro* t, const TiLine* L, int x, int sub, int m7px, int inView,
                        const uint8_t* objPix, const uint8_t* objPrio, int* r, int* g, int* b) {
  static const int lay0[12] = { 4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3 };
  static const int pri0[12] = { 3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0 };
  static const int lay2[8] = { 4, 0, 4, 1, 4, 0, 4, 1 }, pri2[8] = { 3, 1, 2, 1, 1, 0, 0, 0 };
  static const int lay7[5] = { 4, 4, 4, 0, 4 }, pri7[5] = { 3, 2, 1, 0, 0 };
  const int* lay = L->mode == 7 ? lay7 : L->mode == 0 ? lay0 : lay2;
  const int* pri = L->mode == 7 ? pri7 : L->mode == 0 ? pri0 : pri2;
  int n = L->mode == 7 ? 5 : L->mode == 0 ? 12 : 8;
  int screen = sub ? L->ts : L->tm;
  int clipped = sub ? L->tsw : L->tmw;
  int layer = 5, pixel = 0;
  for (int i = 0; i < n; i++) {
    int cur = lay[i], curPri = pri[i];
    int bit = cur == 4 ? 0x10 : (1 << cur);
    if (!(screen & bit)) continue;
    if ((clipped & bit) && tiWinState(L, cur, x)) continue;
    if (cur < 4) {
      if (L->mode == 7) pixel = m7px;
      else {
        if (cur > 1) continue;
        // outside the view only a 64-wide map (the title's BG1, $2107 = $65) has its own columns there -- within 256 of
        // the view's middle, so no column is drawn twice; a 32-wide one would wrap back to the view's own picture
        if (!inView && !(cur == 0 && L->bg1Wide)) continue;
        if (cur == 0 && L->bg1Wide && (unsigned)(x + ((((L->bg1h & 0x3FF) ^ 0x200) - 0x200)) - L->bg1WinLo) >= 512u) continue;
        int lx = x + ((cur == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF);
        int ly = (L->y) + ((cur == 0 ? L->bg1v : 0) & 0x3FF);
        if (L->mode == 2) tiOPTEx(t, L, cur, &lx, &ly);
        pixel = tiBgPixel(t, L, lx & 0x3FF, ly & 0x3FF, cur, curPri);
      }
    } else pixel = (objPrio[x] == curPri) ? objPix[x] : 0;   // (objPix/objPrio point at the view's column 0)
    if (pixel > 0) { layer = cur; break; }
  }
  uint16_t c = t->l.cgram[pixel & 0xFF];
  *r = c & 31; *g = (c >> 5) & 31; *b = (c >> 10) & 31;
  if (layer == 4 && pixel < 0xC0) layer = 6;
  return layer;
}
// tiOPT for the widescreen's columns: a column past the table's 32 takes the ripple the table pushed off its right end
// (waveTail) -- the wave keeps travelling into the right side instead of wrapping back to the left's.
static void tiOPTEx(const CtTitleIntro* t, const TiLine* L, int layer, int* lx, int* ly) {
  int x = *lx, y = *ly;
  int hScroll = (layer == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF;
  int column = ((x - (x & 7)) - (hScroll & ~7)) >> 3;
  int e = (column - 1) + ((L->bg3h & 0x3FF) >> 3);
  if (column <= 32 || e < 32) { tiOPT(t, L, layer, lx, ly); return; }
  if (layer != 0) return;
  uint16_t vOff = e - 32 < 64 ? t->l.offsTail[e - 32] : 0;
  if (vOff & 0x2000) *ly = (vOff & 0x3FF) + (y - (L->bg1v & 0x3FF));
}
static long tiFloorDiv(long a, long b) { long q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

// One output row of a mode-7 line with no window anywhere (the swing): the mode-7 sample walks the row incrementally at
// the sub-pixel; the sprites, the layer order and the colour math are the 1x pixel's, as tiGetPixel resolves them.
static void tiRowM7(const CtTitleIntro* t, const TiLine* L, const uint8_t* objPix, const uint8_t* objPrio,
                    long sxk, long syk, int k, int x0, int W, int bri, unsigned char* row) {
  const long D = 256L * k, A = L->m7[0], C = L->m7[2];
  const int mathEnabled[6] = { L->cgadsub & 1, (L->cgadsub >> 1) & 1, (L->cgadsub >> 2) & 1,
                               (L->cgadsub >> 3) & 1, (L->cgadsub >> 4) & 1, (L->cgadsub >> 5) & 1 };
  long dx0 = -(long)x0 * k;
  long u = sxk + A * dx0, v = syk + C * dx0;
  long xp = tiFloorDiv(u, D), ru = u - xp * D, yp = tiFloorDiv(v, D), rv = v - yp * D;
  long qa = tiFloorDiv(A, D), ra = A - qa * D, qc = tiFloorDiv(C, D), rc = C - qc * D;
  for (int X = 0; X < W; X++) {
    int m7px = 0;
    if (xp >= 0 && xp < CT_M7_FIELD && yp >= 0 && yp < CT_M7_FIELD) {
      int tile = tiM7Tile((int)(yp >> 3), (int)(xp >> 3));
      if (tile < CT_TITLE_M7_TILES) m7px = ctTitleM7Chr[tile * 64 + (yp & 7) * 8 + (xp & 7)];
    }
    xp += qa; ru += ra; if (ru >= D) { ru -= D; xp++; }
    yp += qc; rv += rc; if (rv >= D) { rv -= D; yp++; }
    int xw = X / k;                                   // (the sprites are evaluated over the wide picture)
    int obj = objPix[xw], op = objPrio[xw];
    // main screen: OBJ3/OBJ2/OBJ1, BG1, OBJ0, the backdrop
    int ml = 5, mp = 0;
    if ((L->tm & 0x10) && obj && op >= 1) { ml = 4; mp = obj; }
    else if ((L->tm & 0x01) && m7px) { ml = 0; mp = m7px; }
    else if ((L->tm & 0x10) && obj) { ml = 4; mp = obj; }
    uint16_t c = t->l.cgram[mp & 0xFF];
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    if (ml == 4 && mp < 0xC0) ml = 6;
    if (ml < 6 && mathEnabled[ml]) {
      int sl = 5, sp = 0;
      if (L->addSub) {
        if ((L->ts & 0x10) && obj && op >= 1) { sl = 4; sp = obj; }
        else if ((L->ts & 0x01) && m7px) { sl = 0; sp = m7px; }
        else if ((L->ts & 0x10) && obj) { sl = 4; sp = obj; }
      }
      int useSub = L->addSub && sl != 5;
      int r2 = 0, g2 = 0, b2 = 0;
      if (useSub) { uint16_t c2 = t->l.cgram[sp & 0xFF]; r2 = c2 & 31; g2 = (c2 >> 5) & 31; b2 = (c2 >> 10) & 31; }
      if (L->subtract) { r -= useSub ? r2 : L->fixedR; g -= useSub ? g2 : L->fixedG; b -= useSub ? b2 : L->fixedB; }
      else { r += useSub ? r2 : L->fixedR; g += useSub ? g2 : L->fixedG; b += useSub ? b2 : L->fixedB; }
      if (L->halfColor && (sl == 5 || !L->addSub)) { r >>= 1; g >>= 1; b >>= 1; }
      if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
      if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    }
    row[X * 3 + 0] = (unsigned char)((((r << 3) | (r >> 2)) * bri) / 15);
    row[X * 3 + 1] = (unsigned char)((((g << 3) | (g >> 2)) * bri) / 15);
    row[X * 3 + 2] = (unsigned char)((((b << 3) | (b >> 2)) * bri) / 15);
  }
}

// One wide-picture pixel at the view's column xv (the mode-7 sample handed in), the colour math and the brightness applied.
static void tiPixEx(const CtTitleIntro* t, const TiLine* Ly, int xv, int m7px, int inView, const uint8_t* objPixV,
                    const uint8_t* objPrioV, int bri, unsigned char* o) {
    const int mathEnabled[6] = { Ly->cgadsub & 1, (Ly->cgadsub >> 1) & 1, (Ly->cgadsub >> 2) & 1,
                                 (Ly->cgadsub >> 3) & 1, (Ly->cgadsub >> 4) & 1, (Ly->cgadsub >> 5) & 1 };
    int r, g, b, r2 = 0, g2 = 0, b2 = 0;
    int main = tiGetPixelEx(t, Ly, xv, 0, m7px, inView, objPixV, objPrioV, &r, &g, &b);
    int cw = tiWinState(Ly, 5, xv);
    if (Ly->clipMode == 3 || (Ly->clipMode == 2 && cw) || (Ly->clipMode == 1 && !cw)) r = g = b = 0;
    int me = main < 6 && mathEnabled[main] &&
             !(Ly->preventMath == 3 || (Ly->preventMath == 2 && cw) || (Ly->preventMath == 1 && !cw));
    if (me) {
      int second = 5;
      if (Ly->addSub) second = tiGetPixelEx(t, Ly, xv, 1, m7px, inView, objPixV, objPrioV, &r2, &g2, &b2);
      int useSub = Ly->addSub && second != 5;
      if (Ly->subtract) { r -= useSub ? r2 : Ly->fixedR; g -= useSub ? g2 : Ly->fixedG; b -= useSub ? b2 : Ly->fixedB; }
      else { r += useSub ? r2 : Ly->fixedR; g += useSub ? g2 : Ly->fixedG; b += useSub ? b2 : Ly->fixedB; }
      if (Ly->halfColor && (second == 5 || !Ly->addSub)) { r >>= 1; g >>= 1; b >>= 1; }
      if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
      if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    }
    o[0] = (unsigned char)((((r << 3) | (r >> 2)) * bri) / 15);
    o[1] = (unsigned char)((((g << 3) | (g >> 2)) * bri) / 15);
    o[2] = (unsigned char)((((b << 3) | (b >> 2)) * bri) / 15);
}

void CtTitleIntroRenderEx(const CtTitleIntro* t, const struct CtTiLatch* prev, int t256, unsigned char* out,
                          int w1, int x0, int k) {
  if (k < 1) k = 1;
  const int W = w1 * k, H = 224 * k;
  memset(out, 0, (size_t)W * H * 3);
  if (t->l.bri & 0x80) return;
  int bri = t->l.bri & 0x0F;
  TiLine L;
  memset(&L, 0, sizeof L);
  L.mode = t->mode; L.tm = t->tm; L.ts = t->ts; L.tmw = t->tmw; L.tsw = t->tsw;
  L.cgadsub = (uint8_t)(t->cgadsub & 0x3F);
  L.halfColor = t->halfColor; L.subtract = t->subtract; L.addSub = t->addSub;
  L.clipMode = t->clipMode; L.preventMath = t->preventMath;
  L.bg1sc = t->bg1sc; L.bg2sc = t->bg2sc; L.bg12nba = t->bg12nba; L.obsel = t->obsel;
  L.win1l = t->win1l; L.win1r = t->win1r; L.win2l = t->win2l; L.win2r = t->win2r;
  memcpy(L.win, t->win, sizeof L.win);
  L.bg1h = t->l.bg1h; L.bg1v = t->l.bg1v; L.bg3h = t->l.bg3h;
  for (int q = 0; q < 4; q++) L.m7[q] = t->l.m7[q];
  // the frames between: the matrix and the scroll t/256 of the way from the previous latch (mode 7 only -- it is the
  // layer sampled at the sub-pixel; a jump is a cut)
  // (mode 2: BG1's scroll alone -- the TRIGGER logo sliding in)
  if (prev && t256 < 256 && (t->mode == 7 || t->mode == 2)) {
    int dh = s13(t->l.bg1h) - s13(prev->bg1h), dv = s13(t->l.bg1v) - s13(prev->bg1v), jump = abs(dh) > 64 || abs(dv) > 64;
    if (t->mode == 7) for (int q = 0; q < 4; q++) if (abs(t->l.m7[q] - prev->m7[q]) > 0x80) jump = 1;
    if (!jump) {
      if (t->mode == 7) for (int q = 0; q < 4; q++) L.m7[q] = (int16_t)(t->l.m7[q] - (t->l.m7[q] - prev->m7[q]) * (256 - t256) / 256);
      L.bg1h = (uint16_t)(t->l.bg1h - dh * (256 - t256) / 256); L.bg1v = (uint16_t)(t->l.bg1v - dv * (256 - t256) / 256);
    }
  }
  // the widescreen: BG1's slide stretched by the right side's width, so it starts beyond the wider edge (and covers it in
  // the same frames: faster) and ends where the ROM's does; outside a slide, BG1 stays inside the 256 view (a parked logo
  // past the view's edge is not on the ROM's screen)
  const int side = w1 - 256 - x0, slide = t->mode == 2 && t->slideOn && side > 0 && t->slideEnd != t->slideFrom;
  if (slide) {
    long D = (long)t->slideEnd - t->slideFrom, h = (int16_t)(((L.bg1h & 0x3FF) ^ 0x200) - 0x200);
    long e = t->slideEnd, from = t->slideFrom;
    if ((D > 0 && h >= from && h <= e) || (D < 0 && h <= from && h >= e)) {
      long hw = e - (e - h) * (D + (D > 0 ? side : -side)) / D;
      L.bg1h = (uint16_t)(hw & 0x3FF);
    }
  }
  L.bg1Wide = slide && (L.bg1sc & 1); L.bg1WinLo = slide ? t->slideEnd - 128 : 0;
  // Every line's registers and sprites first, in the PPU's order (HDMA rewrites them line by line); then the lines are
  // independent and are drawn in parallel.
  static TiLine lines[224];
  enum { TI_WMAX = 1024 };
  static uint8_t objPixL[224][TI_WMAX], objPrioL[224][TI_WMAX];
  if (w1 > TI_WMAX) return;
  TiHdmaRun run[8];
  tiHdmaInit(t, run);
  for (int y = 0; y < 224; y++) {
    tiHdmaRun(t, run, &L);
    L.y = y + 1;
    lines[y] = L;
    memset(objPrioL[y], 0, (size_t)w1);
    tiEvalSpritesEx(t, y, objPixL[y], objPrioL[y], w1, x0);
  }
  int yy;
  #pragma omp parallel for schedule(dynamic, 8)
  for (yy = 0; yy < 224; yy++) {
    const int y = yy;
    const TiLine* Ly = &lines[y];
    const uint8_t* objPix = objPixL[y]; const uint8_t* objPrio = objPrioL[y];
    const uint8_t* objPixV = objPix + x0; const uint8_t* objPrioV = objPrio + x0;   // (indexed by the view's x)
    int line = y + 1;
    TiM7Start m0 = tiM7Starts(Ly, line & 0xFF), m1 = tiM7Starts(Ly, (line + 1) & 0xFF);
    int fast = Ly->mode == 7 && !Ly->tmw && !Ly->tsw && !Ly->clipMode && !Ly->preventMath;
    const int mathEnabled[6] = { Ly->cgadsub & 1, (Ly->cgadsub >> 1) & 1, (Ly->cgadsub >> 2) & 1,
                                 (Ly->cgadsub >> 3) & 1, (Ly->cgadsub >> 4) & 1, (Ly->cgadsub >> 5) & 1 };
    for (int j = 0; j < k; j++) {
      unsigned char* row = out + (size_t)(y * k + j) * W * 3;
      long sxk = (long)m0.sx * k + (long)(m1.sx - m0.sx) * j, syk = (long)m0.sy * k + (long)(m1.sy - m0.sy) * j;
      if (fast) { tiRowM7(t, Ly, objPix, objPrio, sxk, syk, k, x0, W, bri, row); continue; }
      if (Ly->mode != 7) {                             // the tiled modes have nothing below the pixel: one 1x row, widened
        if (j > 0) { memcpy(row, out + (size_t)(y * k) * W * 3, (size_t)W * 3); continue; }
        for (int xw = 0; xw < w1; xw++) {
          int xv = xw - x0, inView = xv >= 0 && xv < 256;
          if (!inView && !Ly->bg1Wide) continue;   // (black: the pillarbox)
          unsigned char px[3];
          tiPixEx(t, Ly, xv, 0, inView, objPixV, objPrioV, bri, px);
          for (int i = 0; i < k; i++) memcpy(row + ((size_t)xw * k + i) * 3, px, 3);
        }
        continue;
      }
      for (int X = 0; X < W; X++) {
        long dx = (long)X - (long)x0 * k;                // the view's x, in 1/k pixels
        int xv = (int)tiFloorDiv(dx, k), inView = xv >= 0 && xv < 256;
        if (!inView && Ly->mode != 7 && !Ly->bg1Wide) continue;   // (black: the pillarbox)
        int m7px = 0;
        if (Ly->mode == 7) {
          long xp = tiFloorDiv(sxk + (long)Ly->m7[0] * dx, 256L * k), yp = tiFloorDiv(syk + (long)Ly->m7[2] * dx, 256L * k);
          if (xp >= 0 && xp < CT_M7_FIELD && yp >= 0 && yp < CT_M7_FIELD) {
            int tile = tiM7Tile((int)(yp >> 3), (int)(xp >> 3));
            if (tile < CT_TITLE_M7_TILES) m7px = ctTitleM7Chr[tile * 64 + (yp & 7) * 8 + (xp & 7)];
          }
        }
        tiPixEx(t, Ly, xv, m7px, inView, objPixV, objPrioV, bri, row + X * 3);
      }
    }
  }
}
