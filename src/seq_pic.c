// seq_pic.c -- see seq_pic.h.
#include "seq_pic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _OPENMP
#include <omp.h>
#endif

// The layer order of each mode, front to back (0-3 the BGs, 4 the OBJ, 5 none), with the priority each entry takes; 8 is mode
// 1 with BG3's priority bit ($2105 bit 3), 9 mode 7 with EXTBG ($2133 bit 6). The bit depth of each BG by mode (5 = none).
static const uint8_t kLayers[10][12] = {
  {4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3}, {4, 0, 1, 4, 0, 1, 4, 2, 4, 2, 5, 5}, {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5}, {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5}, {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
  {4, 0, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5}, {4, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5, 5}, {2, 4, 0, 1, 4, 0, 1, 4, 4, 2, 5, 5},
  {4, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5, 5}
};
static const uint8_t kPrio[10][12] = {
  {3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0}, {3, 1, 1, 2, 0, 0, 1, 1, 0, 0, 5, 5}, {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5}, {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5}, {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
  {3, 1, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5}, {3, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5, 5}, {1, 3, 1, 1, 2, 0, 0, 1, 0, 0, 5, 5},
  {3, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5, 5}
};
static const uint8_t kCount[10] = { 12, 10, 8, 8, 8, 8, 6, 5, 10, 7 };
static const uint8_t kDepth[10][4] = {
  {2, 2, 2, 2}, {4, 4, 2, 5}, {4, 4, 5, 5}, {8, 4, 5, 5}, {8, 2, 5, 5}, {4, 2, 5, 5}, {4, 5, 5, 5}, {8, 5, 5, 5},
  {4, 4, 2, 5}, {8, 7, 5, 5}
};
static const uint8_t kObjSize[8][2] = { {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32} };

void CtSeqPicReset(CtSeqPic* p) { memset(p, 0, sizeof *p); p->r[0x00] = 0x80; p->mosaicStart = 1; }

void CtSeqPicWrite(CtSeqPic* p, uint8_t adr, uint8_t val, int line) {
  adr &= 0x3F;
  switch (adr) {
    case 0x0D: p->m7[6] = (int16_t)(((val << 8) | p->m7prev) & 0x1FFF); p->m7prev = val;    /* FALLTHROUGH: BG1's too */
    case 0x0F: case 0x11: case 0x13:
      p->hofs[(adr - 0x0D) / 2] = (uint16_t)(((val << 8) | (p->scrollPrev & 0xF8) | (p->scrollPrev2 & 7)) & 0x3FF);
      p->scrollPrev = val; p->scrollPrev2 = val; break;
    case 0x0E: p->m7[7] = (int16_t)(((val << 8) | p->m7prev) & 0x1FFF); p->m7prev = val;    /* FALLTHROUGH */
    case 0x10: case 0x12: case 0x14:
      p->vofs[(adr - 0x0E) / 2] = (uint16_t)(((val << 8) | p->scrollPrev) & 0x3FF);
      p->scrollPrev = val; break;
    case 0x1B: case 0x1C: case 0x1D: case 0x1E: p->m7[adr - 0x1B] = (int16_t)((val << 8) | p->m7prev); p->m7prev = val; break;
    case 0x1F: case 0x20: p->m7[adr - 0x1B] = (int16_t)(((val << 8) | p->m7prev) & 0x1FFF); p->m7prev = val; break;
    case 0x32:
      if (val & 0x20) p->fix[0] = val & 0x1F;
      if (val & 0x40) p->fix[1] = val & 0x1F;
      if (val & 0x80) p->fix[2] = val & 0x1F;
      break;
    case 0x06: p->r[adr] = val; p->mosaicStart = (uint8_t)line; break;
    case 0x02: p->r[adr] = val; p->oamAdr = p->oamAdrWritten = val; p->oamInHigh = p->oamInHighWritten; p->oamSecond = 0; break;
    case 0x03: p->r[adr] = val; p->oamInHigh = p->oamInHighWritten = val & 1; p->oamAdr = p->oamAdrWritten; p->oamSecond = 0; break;
    case 0x04:                                                         // OAM data: the low table a word at a time, then the high
      if (p->oamInHigh) {
        p->highOam[((p->oamAdr & 0x0F) << 1) | p->oamSecond] = val;
        if (p->oamSecond && ++p->oamAdr == 0) p->oamInHigh = 0;
      } else if (!p->oamSecond) p->oamBuffer = val;
      else { p->oam[p->oamAdr] = (uint16_t)((val << 8) | p->oamBuffer); if (++p->oamAdr == 0) p->oamInHigh = 1; }
      p->oamSecond ^= 1;
      break;
    case 0x18: case 0x19: case 0x22: {                                 // the data ports: a stage's native code fills the
      static int said = 0;                                             // arrays itself (no stage measured writes them by HDMA)
      if (!said) { said = 1; fprintf(stderr, "seqpic: a write to the data port $21%02X -- not modelled\n", adr); }
      break;
    }
    default: p->r[adr] = val; break;
  }
}

uint8_t CtSeqPicReg(CtSeqPic* p, uint8_t adr) {
  switch (adr & 0x3F) {
    case 0x38: {                                                       // OAM data: the address the writes and reads share
      uint8_t v;
      if (p->oamInHigh) {
        v = p->highOam[((p->oamAdr & 0x0F) << 1) | p->oamSecond];
        if (p->oamSecond && ++p->oamAdr == 0) p->oamInHigh = 0;
      } else if (!p->oamSecond) v = (uint8_t)p->oam[p->oamAdr];
      else { v = (uint8_t)(p->oam[p->oamAdr] >> 8); if (++p->oamAdr == 0) p->oamInHigh = 1; }
      p->oamSecond ^= 1; p->ppu1OpenBus = v;
      return v;
    }
    case 0x3E: {                                                       // STAT77: the version 1, the open bus' bit 4, the flags
      uint8_t v = (uint8_t)(0x01 | (p->ppu1OpenBus & 0x10) | (p->rangeOver << 6) | (p->timeOver << 7));
      p->ppu1OpenBus = v;
      return v;
    }
  }
  { static int said = 0; if (!said) { said = 1; fprintf(stderr, "seqpic: a read of $21%02X -- not modelled\n", adr & 0x3F); } }
  return 0;
}

typedef struct {
  int mode, actMode, bright, blank;
  int mosaic, mosaicOn[4];
  int big[4], wide[4], high[4], map[4], chr[4];
  int objSize, objAdr1, objAdr2, objPrioRot, oamIndex;
  int m7large, m7fill, m7xflip, m7yflip;
  int main[5], sub[5], mainWin[5], subWin[5];
  int w1en[6], w2en[6], w1inv[6], w2inv[6], logic[6];
  int clip, prevent, addSub, direct, subtract, half, math[6];
  int m7sx, m7sy;
  uint8_t objPix[256], objPri[256];
  uint8_t lum[32];                  // a 5-bit channel at the line's brightness, as 8 bits (shade)
} Line;

static void decode(const CtSeqPic* p, Line* L) {
  const uint8_t* r = p->r;
  L->blank = (r[0x00] & 0x80) != 0; L->bright = r[0x00] & 0x0F;
  for (int v = 0; v < 32; v++) L->lum[v] = (uint8_t)(((v << 3) | (v >> 2)) * L->bright / 15);
  L->objSize = r[0x01] >> 5; L->objAdr1 = (r[0x01] & 7) << 13; L->objAdr2 = L->objAdr1 + (((r[0x01] & 0x18) + 8) << 9);
  L->objPrioRot = (r[0x03] & 0x80) != 0; L->oamIndex = L->objPrioRot ? (p->oamAdr & 0xFE) : 0;   // (the OAM address now)
  L->mode = r[0x05] & 7;
  L->actMode = L->mode == 1 && (r[0x05] & 8) ? 8 : L->mode == 7 && (r[0x33] & 0x40) ? 9 : L->mode;
  for (int i = 0; i < 4; i++) {
    L->big[i] = (r[0x05] >> (4 + i)) & 1;
    L->mosaicOn[i] = (r[0x06] >> i) & 1;
    L->wide[i] = r[0x07 + i] & 1; L->high[i] = (r[0x07 + i] >> 1) & 1; L->map[i] = (r[0x07 + i] & 0xFC) << 8;
  }
  L->mosaic = (r[0x06] >> 4) + 1;
  L->chr[0] = (r[0x0B] & 0x0F) << 12; L->chr[1] = (r[0x0B] & 0xF0) << 8;
  L->chr[2] = (r[0x0C] & 0x0F) << 12; L->chr[3] = (r[0x0C] & 0xF0) << 8;
  L->m7large = (r[0x1A] >> 7) & 1; L->m7fill = (r[0x1A] >> 6) & 1; L->m7yflip = (r[0x1A] >> 1) & 1; L->m7xflip = r[0x1A] & 1;
  for (int i = 0; i < 5; i++) {
    L->main[i] = (r[0x2C] >> i) & 1; L->sub[i] = (r[0x2D] >> i) & 1;
    L->mainWin[i] = (r[0x2E] >> i) & 1; L->subWin[i] = (r[0x2F] >> i) & 1;
  }
  for (int i = 0; i < 6; i++) {
    uint8_t s = r[0x23 + i / 2] >> ((i & 1) * 4);
    L->w1inv[i] = s & 1; L->w1en[i] = (s >> 1) & 1; L->w2inv[i] = (s >> 2) & 1; L->w2en[i] = (s >> 3) & 1;
    L->logic[i] = i < 4 ? (r[0x2A] >> (i * 2)) & 3 : (r[0x2B] >> ((i - 4) * 2)) & 3;
  }
  L->clip = (r[0x30] >> 6) & 3; L->prevent = (r[0x30] >> 4) & 3; L->addSub = (r[0x30] >> 1) & 1; L->direct = r[0x30] & 1;
  L->subtract = (r[0x31] >> 7) & 1; L->half = (r[0x31] >> 6) & 1;
  for (int i = 0; i < 6; i++) L->math[i] = (r[0x31] >> i) & 1;
}

int g_seqPicWideSide = 0, g_seqPicWideSkip = 0, g_seqPicWideSkipModes = 0;
uint8_t* g_seqPicWideOut = NULL;
CtSeqPicArtFn g_seqPicArt = NULL;
int g_seqPicWideSubTexture = 0;
// x inside the window's span [l, r]: the hardware's test, or -- while the widescreen's sides are drawn -- snes/ppu.c
// ppu_winTest's (a narrow band at an edge is no window; a wide span's edge within 16 columns of the screen's runs on to the
// picture's; a curtain, one edge on the screen's edge, has its moving edge scaled to the picture).
static int winTest(int x, int l, int r) {
  if (g_seqPicWideSide <= 0) return x >= l && x <= r;
  if ((r >= 255 && l >= 240) || (l <= 0 && r <= 15)) return 0;
  if (r - l < 16) return x >= l && x <= r;
  if ((r >= 255) != (l <= 0)) {
    int w = 256 + 2 * g_seqPicWideSide, xp = x + g_seqPicWideSide;
    int lo = l <= 0 ? -(1 << 20) : (l * (w - 1) + 127) / 255, hi = r >= 255 ? (1 << 20) : (r * (w - 1) + 127) / 255;
    return xp >= lo && xp <= hi;
  }
  int lo = l <= 16 ? -(1 << 20) : l, hi = r >= 239 ? (1 << 20) : r;
  return x >= lo && x <= hi;
}

static int winState(const CtSeqPic* p, const Line* L, int layer, int x) {
  if (!L->w1en[layer] && !L->w2en[layer]) return 0;
  int t1 = winTest(x, p->r[0x26], p->r[0x27]), t2 = winTest(x, p->r[0x28], p->r[0x29]);
  if (L->w1en[layer] && !L->w2en[layer]) return L->w1inv[layer] ? !t1 : t1;
  if (!L->w1en[layer] && L->w2en[layer]) return L->w2inv[layer] ? !t2 : t2;
  if (L->w1inv[layer]) t1 = !t1;
  if (L->w2inv[layer]) t2 = !t2;
  switch (L->logic[layer]) { case 0: return t1 || t2; case 1: return t1 && t2; case 2: return t1 != t2; default: return t1 == t2; }
}

static void sprites(CtSeqPic* p, Line* L, int line) {
  memset(L->objPix, 0, sizeof L->objPix);
  if (L->blank) return;
  int idx = L->oamIndex, found = 0, tiles = 0;
  uint8_t list[32];
  for (int i = 0; i < 128; i++) {
    uint8_t y = (uint8_t)(p->oam[idx] >> 8), row = (uint8_t)(line - y);
    int size = kObjSize[L->objSize][(p->highOam[idx >> 3] >> ((idx & 7) + 1)) & 1];
    if (row < size) {
      int x = (p->oam[idx] & 0xFF) | (((p->highOam[idx >> 3] >> (idx & 7)) & 1) << 8);
      if (x > 255) x -= 512;
      if (x > -size) {
        if (++found > 32) { found = 32; p->rangeOver = 1; break; }       // STAT77 bit 6
        list[found - 1] = (uint8_t)idx;
      }
    }
    idx = (idx + 2) & 0xFF;
  }
  for (int i = found; i > 0; i--) {
    idx = list[i - 1];
    uint8_t y = (uint8_t)(p->oam[idx] >> 8), row = (uint8_t)(line - y);
    int size = kObjSize[L->objSize][(p->highOam[idx >> 3] >> ((idx & 7) + 1)) & 1];
    int x = (p->oam[idx] & 0xFF) | (((p->highOam[idx >> 3] >> (idx & 7)) & 1) << 8);
    if (x > 255) x -= 512;
    if (x <= -size) continue;
    uint16_t at = p->oam[idx + 1];
    int tile = at & 0xFF, pal = (at & 0xE00) >> 9, hf = (at & 0x4000) != 0;
    if (at & 0x8000) row = (uint8_t)(size - 1 - row);
    for (int col = 0; col < size; col += 8) {
      if (col + x <= -8 || col + x >= 256) continue;
      if (++tiles > 34) { p->timeOver = 1; break; }                   // STAT77 bit 7
      int uc = hf ? size - 1 - col : col;
      uint8_t t = (uint8_t)((((tile >> 4) + (row / 8)) << 4) | (((tile & 0xF) + (uc / 8)) & 0xF));
      int base = (at & 0x100) ? L->objAdr2 : L->objAdr1;
      uint16_t p1 = p->vram[(base + t * 16 + (row & 7)) & 0x7FFF], p2 = p->vram[(base + t * 16 + 8 + (row & 7)) & 0x7FFF];
      for (int px = 0; px < 8; px++) {
        int sh = hf ? px : 7 - px;
        int v = ((p1 >> sh) & 1) | (((p1 >> (8 + sh)) & 1) << 1) | (((p2 >> sh) & 1) << 2) | (((p2 >> (8 + sh)) & 1) << 3);
        int sc = col + x + px;
        if (v && sc >= 0 && sc < 256) { L->objPix[sc] = (uint8_t)(0x80 + 16 * pal + v); L->objPri[sc] = (uint8_t)((at & 0x3000) >> 12); }
      }
    }
    if (tiles > 34) break;
  }
}

static int bgPixel(const CtSeqPic* p, const Line* L, int x, int y, int layer, int pri) {
  int wideT = L->big[layer] || L->mode == 5 || L->mode == 6;
  int bx = wideT ? 4 : 3, hx = wideT ? 0x200 : 0x100, by = L->big[layer] ? 4 : 3, hy = L->big[layer] ? 0x200 : 0x100;
  unsigned ta = (unsigned)L->map[layer] + ((((unsigned)y >> by) & 0x1F) << 5 | (((unsigned)x >> bx) & 0x1F));
  if ((x & hx) && L->wide[layer]) ta += 0x400;
  if ((y & hy) && L->high[layer]) ta += L->wide[layer] ? 0x800 : 0x400;
  uint16_t tile = p->vram[ta & 0x7FFF];
  if (((tile & 0x2000) != 0) != pri) return 0;
  int pal = (tile & 0x1C00) >> 10;
  int row = (tile & 0x8000) ? 7 - (y & 7) : (y & 7), col = (tile & 0x4000) ? (x & 7) : 7 - (x & 7);
  int tn = tile & 0x3FF;
  if (wideT && (((x & 8) != 0) ^ ((tile & 0x4000) != 0))) tn += 1;
  if (L->big[layer] && (((y & 8) != 0) ^ ((tile & 0x8000) != 0))) tn += 0x10;
  int depth = kDepth[L->mode][layer];
  if (L->mode == 0) pal += 8 * layer;
  unsigned base = (unsigned)L->chr[layer] + (unsigned)((tn & 0x3FF) * 4 * depth);
  uint16_t a = p->vram[(base + row) & 0x7FFF];
  int v = ((a >> col) & 1) | (((a >> (8 + col)) & 1) << 1), size = 4;
  if (depth > 2) {
    size = 16; uint16_t b = p->vram[(base + 8 + row) & 0x7FFF];
    v |= (((b >> col) & 1) << 2) | (((b >> (8 + col)) & 1) << 3);
  }
  if (depth > 4) {
    size = 256; uint16_t c = p->vram[(base + 16 + row) & 0x7FFF], d = p->vram[(base + 24 + row) & 0x7FFF];
    v |= (((c >> col) & 1) << 4) | (((c >> (8 + col)) & 1) << 5) | (((d >> col) & 1) << 6) | (((d >> (8 + col)) & 1) << 7);
  }
  return v ? size * pal + v : 0;
}

static uint16_t optValue(const CtSeqPic* p, const Line* L, int col, int row) {
  int x = col * 8 + p->hofs[2], y = row * 8 + p->vofs[2];
  int bits = L->big[2] ? 4 : 3, hb = L->big[2] ? 0x200 : 0x100;
  unsigned ta = (unsigned)L->map[2] + ((((unsigned)y >> bits) & 0x1F) << 5 | (((unsigned)x >> bits) & 0x1F));
  if ((x & hb) && L->wide[2]) ta += 0x400;
  if ((y & hb) && L->high[2]) ta += L->wide[2] ? 0x800 : 0x400;
  return p->vram[ta & 0x7FFF];
}
static void opt(const CtSeqPic* p, const Line* L, int layer, int* lx, int* ly) {   // offset per tile (modes 2, 4, 6)
  int x = *lx, y = *ly, column;
  if (L->mode == 6) column = ((x - (x & 0xF)) - ((p->hofs[layer] * 2) & 0xFFF0)) >> 4;
  else column = ((x - (x & 7)) - (p->hofs[layer] & 0xFFF8)) >> 3;
  if (column <= 0) return;
  int valid = layer == 0 ? 0x2000 : 0x4000;
  uint16_t h = optValue(p, L, column - 1, 0), v = 0;
  if (L->mode == 4) { if (h & 0x8000) { v = h; h = 0; } }
  else v = optValue(p, L, column - 1, 1);
  if (L->mode == 6) { if (h & valid) *lx = (((h & 0x3F8) + (column * 8)) * 2) | (x & 0xF); }
  else if (h & valid) *lx = ((h & 0x3F8) + (column * 8)) | (x & 7);
  if (v & valid) *ly = (v & 0x3FF) + (y - p->vofs[layer]);
}

// THE OFFSET PER TILE PAST THE SCREEN'S EDGE (mode 2; a stage's art past its edge, a mod): the 32 columns' offsets carried
// on as a line through the two entries nearest that edge, so a stretch made with them -- the attract's FF 63, the machine
// opening from its middle -- goes on into the sides. x is the screen column, y the line; *mx / *my the layer's place
// (unwrapped, as g_seqPicArt takes it). 1 = an offset applied.
static int floorDiv8(int v) { return v >= 0 ? v / 8 : -((7 - v) / 8); }
static int optSigned(uint16_t v) { int s = v & 0x3FF; return s >= 512 ? s - 1024 : s; }
static int optSide(const CtSeqPic* p, const Line* L, int layer, int x, int y, int* mx, int* my) {
  int hs = p->hofs[layer] & 0x3FF, valid = layer == 0 ? 0x2000 : 0x4000, done = 0;
  int column = floorDiv8(x + (hs & 7)), e = column - 1, ea = e < 0 ? 0 : 30, eb = ea + 1;
  uint16_t ha = optValue(p, L, ea, 0), hb = optValue(p, L, eb, 0), va = optValue(p, L, ea, 1), vb = optValue(p, L, eb, 1);
  if ((ha & valid) && (hb & valid)) {
    int h = optSigned(ha) + (optSigned(hb) - optSigned(ha)) * (e - ea);
    *mx = (h & ~7) + column * 8 + ((x + hs) & 7); done = 1;
  }
  if ((va & valid) && (vb & valid)) {
    int v = optSigned(va) + (optSigned(vb) - optSigned(va)) * (e - ea);
    *my = (v + y) & 0x3FF; done = 1;
  }
  return done;
}

static void m7Start(const CtSeqPic* p, Line* L, int y) {
  int h = ((int16_t)(p->m7[6] << 3)) >> 3, v = ((int16_t)(p->m7[7] << 3)) >> 3;
  int xc = ((int16_t)(p->m7[4] << 3)) >> 3, yc = ((int16_t)(p->m7[5] << 3)) >> 3;
  int ch = h - xc, cv = v - yc;
  ch = (ch & 0x2000) ? (ch | ~1023) : (ch & 1023);
  cv = (cv & 0x2000) ? (cv | ~1023) : (cv & 1023);
  if (L->mosaicOn[0] && L->mosaic > 1) y -= (y - p->mosaicStart) % L->mosaic;
  uint8_t ry = (uint8_t)(L->m7yflip ? 255 - y : y);
  L->m7sx = ((p->m7[0] * ch) & ~63) + ((p->m7[1] * ry) & ~63) + ((p->m7[1] * cv) & ~63) + (xc << 8);
  L->m7sy = ((p->m7[2] * ch) & ~63) + ((p->m7[3] * ry) & ~63) + ((p->m7[3] * cv) & ~63) + (yc << 8);
}
static int m7Pixel(const CtSeqPic* p, const Line* L, int x, int layer, int pri) {
  int rx = L->m7xflip ? 255 - x : x;
  int xp = (L->m7sx + p->m7[0] * rx) >> 8, yp = (L->m7sy + p->m7[2] * rx) >> 8;
  int out = xp < 0 || xp >= 1024 || yp < 0 || yp >= 1024;
  xp &= 0x3FF; yp &= 0x3FF;
  if (!L->m7large) out = 0;
  uint8_t tile = out ? 0 : (uint8_t)(p->vram[(yp >> 3) * 128 + (xp >> 3)] & 0xFF);
  uint8_t px = out && !L->m7fill ? 0 : (uint8_t)(p->vram[tile * 64 + (yp & 7) * 8 + (xp & 7)] >> 8);
  if (layer == 1) { if (((px & 0x80) != 0) != pri) return 0; return px & 0x7F; }
  return px;
}

// The colour of one screen (main or sub) at x: the first opaque layer in the mode's order; returns its layer (0-3 a BG, 4 an
// OBJ of palettes 4-7, 6 one of palettes 0-3, 5 the backdrop).
// One BG layer's pixel at x of the line (y), at the priority cp: the stage's art, offset per tile, mode 7, the fetch.
static int bgLayer(const CtSeqPic* p, const Line* L, int x, int y, int cl, int cp, int sub) {
  int px = 0;
  int lx = x, ly = y;
  if (L->mosaicOn[cl] && L->mosaic > 1) { lx -= lx % L->mosaic; ly -= (ly - p->mosaicStart) % L->mosaic; }
  int art = -1, artOn = g_seqPicArt && g_seqPicWideSide > 0 && L->mode < 5, optOut = 0, amx = 0, amy = 0;
  if (artOn) {                                                       // (a stage's art past its edge, a mod)
    int hs = p->hofs[cl] & 0x3FF;
    if (hs >= 512) hs -= 1024;
    amx = lx + hs; amy = (ly + p->vofs[cl]) & 0x3FF;
    if ((x < 0 || x > 255) && L->mode == 2) optOut = optSide(p, L, cl, x, ly, &amx, &amy);
    art = g_seqPicArt(p, cl, &amx, amy, cp);
    lx = amx - hs;                                                  // (it may send the layer to another of its places)
  }
  if (art >= 0) px = art;
  else if (optOut) px = bgPixel(p, L, amx & 0x3FF, amy & 0x3FF, cl, cp);
  else if (L->mode == 7) px = m7Pixel(p, L, lx, cl, cp);
  else {
    lx += p->hofs[cl];
    if (L->mode == 5 || L->mode == 6) { lx *= 2; lx += (sub || L->mosaicOn[cl]) ? 0 : 1; }
    ly += p->vofs[cl];
    if ((L->mode == 2 || L->mode == 4 || L->mode == 6) && !(artOn && (x < 0 || x > 255))) opt(p, L, cl, &lx, &ly);
    px = bgPixel(p, L, lx & 0x3FF, ly & 0x3FF, cl, cp);
  }
  return px;
}
static int pixel(const CtSeqPic* p, const Line* L, int x, int y, int sub, int* rr, int* gg, int* bb) {
  int layer = 5, px = 0;
  for (int i = 0; i < kCount[L->actMode]; i++) {
    int cl = kLayers[L->actMode][i], cp = kPrio[L->actMode][i];
    int on = sub ? L->sub[cl] && (!L->subWin[cl] || !winState(p, L, cl, x))
                 : L->main[cl] && (!L->mainWin[cl] || !winState(p, L, cl, x));
    if ((x < 0 || x > 255) && (g_seqPicWideSkip >> cl & 1) && (!g_seqPicWideSkipModes || (g_seqPicWideSkipModes >> L->mode & 1)))
      on = 0;                                                          // (a scene layer: no copy of it in the sides)
    if (!on) continue;
    if (cl < 4) {
      px = bgLayer(p, L, x, y, cl, cp, sub);
    } else {                                                           // (a widescreen column has no sprite: ppu.c's rule;
      int ox = x;                                                      //  the sub screen's texture repeated 256 along if set)
      if ((x < 0 || x > 255) && sub && !L->main[4] && g_seqPicWideSubTexture) ox = x & 255;
      px = (ox >= 0 && ox < 256 && L->objPri[ox] == cp) ? L->objPix[ox] : 0;
    }
    if (px > 0) { layer = cl; break; }
  }
  if (px >= 0x10000) {                                                // (a stage's art in direct colour: BGR15)
    *rr = px & 0x1F; *gg = (px >> 5) & 0x1F; *bb = (px >> 10) & 0x1F;
  } else if (L->direct && layer < 4 && kDepth[L->actMode][layer] == 8) {
    *rr = ((px & 7) << 2) | ((px & 0x100) >> 7); *gg = ((px & 0x38) >> 1) | ((px & 0x200) >> 8); *bb = ((px & 0xC0) >> 3) | ((px & 0x400) >> 8);
  } else {
    uint16_t c = p->cgram[px & 0xFF];
    *rr = c & 0x1F; *gg = (c >> 5) & 0x1F; *bb = (c >> 10) & 0x1F;
  }
  if (layer == 4 && px < 0xC0) layer = 6;
  return layer;
}

static void shade(CtSeqPic* p, Line* L, int x, int line, uint8_t* o);
// A line in two halves: what it takes from the registers as they stand (the layers' setup, the sprites' evaluation -- which
// advances the OAM address and the overflow flags --, mode 7's start), in the line's order; then its pixels, which read only
// the registers and the memories, so lines can be drawn at once (CtSeqPicFrame, the widescreen's sides).
static void linePrep(CtSeqPic* p, Line* L, int line) {
  decode(p, L);
  if (p->r[0x33] & 0x0B) {                                             // pseudo-hires / interlace / overscan
    static int said = 0;
    if (!said) { said = 1; fprintf(stderr, "seqpic: SETINI $%02X asks for hires / interlace -- drawn as the plain picture\n", p->r[0x33]); }
  }
  sprites(p, L, line - 1);
  if (L->mode == 7) m7Start(p, L, line);
}
static void linePixels(CtSeqPic* p, Line* L, int line, uint8_t* out);
static void drawLine(CtSeqPic* p, Line* L, int line, uint8_t* out) {
  linePrep(p, L, line);
  linePixels(p, L, line, out);
}
static void linePixels(CtSeqPic* p, Line* L, int line, uint8_t* out) {
  for (int x = 0; x < 256; x++) shade(p, L, x, line, out + x * 3);
  if (g_seqPicWideSide > 0 && g_seqPicWideOut && line >= 1 && line <= 224) {   // the widescreen's sides (a mod)
    uint8_t* row = g_seqPicWideOut + (size_t)(line - 1) * (256 + 2 * g_seqPicWideSide) * 3;
    for (int c = 0; c < g_seqPicWideSide; c++) {
      shade(p, L, c - g_seqPicWideSide, line, row + c * 3);
      shade(p, L, 256 + c, line, row + (g_seqPicWideSide + 256 + c) * 3);
    }
  }
}
// One column of the line: the main screen, the colour window's clip, the colour math, the brightness.
static void shade(CtSeqPic* p, Line* L, int x, int line, uint8_t* o) {
  {
    int r = 0, g = 0, b = 0;
    if (!L->blank) {
      int ml = pixel(p, L, x, line, 0, &r, &g, &b);
      int cw = winState(p, L, 5, x);
      if (L->clip == 3 || (L->clip == 2 && cw) || (L->clip == 1 && !cw)) r = g = b = 0;
      int sl = 5, r2 = 0, g2 = 0, b2 = 0;
      int math = ml < 6 && L->math[ml] && !(L->prevent == 3 || (L->prevent == 2 && cw) || (L->prevent == 1 && !cw));
      if (math && L->addSub) sl = pixel(p, L, x, line, 1, &r2, &g2, &b2);
      if (math) {
        int useSub = L->addSub && sl != 5;
        int dr = useSub ? r2 : p->fix[0], dg = useSub ? g2 : p->fix[1], db = useSub ? b2 : p->fix[2];
        if (L->subtract) { r -= dr; g -= dg; b -= db; } else { r += dr; g += dg; b += db; }
        if (L->half && (sl != 5 || !L->addSub)) { r >>= 1; g >>= 1; b >>= 1; }
        r = r > 31 ? 31 : r < 0 ? 0 : r; g = g > 31 ? 31 : g < 0 ? 0 : g; b = b > 31 ? 31 : b < 0 ? 0 : b;
      }
    }
    o[0] = L->lum[r]; o[1] = L->lum[g]; o[2] = L->lum[b];
  }
}

// ---- HDMA: the hardware's walk over the stage's tables (the init at the frame's start, a transfer at each line's end) ----
static const uint8_t kBOff[8][4] = { {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}, {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1} };
static const uint8_t kLen[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };
typedef struct { uint16_t table, size; uint8_t rep, doTransfer, done; } Walk;

static void hdmaInit(const CtSeqPicHdma ch[8], uint8_t en, Walk* w, CtSeqPicRead rd, void* ctx) {
  for (int i = 0; i < 8; i++) {
    w[i].doTransfer = 0; w[i].done = 0;
    if (!((en >> i) & 1)) continue;
    w[i].table = ch[i].aAdr;
    w[i].rep = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++);
    if (!w[i].rep) w[i].done = 1;
    if (ch[i].dmap & 0x40) {
      w[i].size = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++);
      w[i].size |= (uint16_t)(rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++) << 8);
    }
    w[i].doTransfer = 1;
  }
}
static void hdmaRun(CtSeqPic* p, const CtSeqPicHdma ch[8], uint8_t en, Walk* w, CtSeqPicRead rd, void* ctx, int line) {
  int any = 0, last = 0;
  for (int i = 0; i < 8; i++) if ((en >> i) & 1) { any = 1; if (!w[i].done) last = i; }
  if (!any) return;
  for (int i = 0; i < 8; i++) {
    if (!((en >> i) & 1) || w[i].done || !w[i].doTransfer) continue;
    int m = ch[i].dmap & 7;
    for (int j = 0; j < kLen[m]; j++) {
      uint8_t v = (ch[i].dmap & 0x40) ? rd(ctx, ((uint32_t)ch[i].indBank << 16) | w[i].size++)
                                      : rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++);
      uint8_t b = (uint8_t)(ch[i].bAdr + kBOff[m][j]);
      if (b < 0x40) CtSeqPicWrite(p, b, v, line);
    }
  }
  for (int i = 0; i < 8; i++) {
    if (!((en >> i) & 1) || w[i].done) continue;
    w[i].rep--;
    w[i].doTransfer = (w[i].rep & 0x80) != 0;
    uint8_t next = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table);
    if ((w[i].rep & 0x7F) == 0) {
      w[i].rep = next; w[i].table++;
      if (ch[i].dmap & 0x40) {
        if (w[i].rep == 0 && i == last) w[i].size = 0;
        else w[i].size = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++);
        w[i].size |= (uint16_t)(rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].table++) << 8);
      }
      if (!w[i].rep) w[i].done = 1;
      w[i].doTransfer = 1;
    }
  }
}

void CtSeqPicFrame(CtSeqPic* p, const CtSeqPicHdma ch[8], uint8_t en, CtSeqPicRead rd, void* ctx, uint8_t* rgb) {
  static uint8_t scratch[256 * 3];
  Line L;
  Walk w[8];
  memset(w, 0, sizeof w);
  p->mosaicStart = 1;                                                  // (the frame's start: a vblank write counts from line 1)
  p->rangeOver = p->timeOver = 0;                                      // (and the sprite flags cleared: ppu.c handleFrameStart)
  if (g_seqPicArt && g_seqPicWideSide > 0) { int z = 0; g_seqPicArt(p, -1, &z, 0, 0); } // (the scene adjustments' art: a new frame, a mod)
  if (rd) { hdmaInit(ch, en, w, rd, ctx); hdmaRun(p, ch, en, w, rd, ctx, 0); }
  if (g_seqPicWideSide > 0 && g_seqPicWideOut && rgb) {
    // THE WIDESCREEN'S PICTURE ON EVERY CORE (a mod: the vanilla picture is drawn as below, a line at a time). Twice the
    // columns at 21:9 and a stage's art past its edge cost ~13 ms a frame drawn alone (entry 3, the owner: "very slow").
    // The lines' setup and the HDMA walk run in the line's order and keep each line's registers; the pixels of all the
    // lines are then drawn at once, each worker on its own copy of the picture (the memories copied once a frame, a
    // line's registers before its pixels). HDMA writes registers only (no data port), so the copies hold the frame.
    static Line Ls[225];
    static struct { uint8_t r[0x40]; uint16_t hofs[4], vofs[4]; int16_t m7[8]; uint8_t fix[3], mosaicStart; } regs[225];
    for (int line = 1; line <= 224; line++) {
      linePrep(p, &Ls[line], line);
      memcpy(regs[line].r, p->r, sizeof p->r); memcpy(regs[line].hofs, p->hofs, sizeof p->hofs);
      memcpy(regs[line].vofs, p->vofs, sizeof p->vofs); memcpy(regs[line].m7, p->m7, sizeof p->m7);
      memcpy(regs[line].fix, p->fix, sizeof p->fix); regs[line].mosaicStart = p->mosaicStart;
      if (g_seqPicArt) { int z = line; g_seqPicArt(p, -2, &z, 0, 0); }  // (the art sees the line's registers in order)
      if (rd) hdmaRun(p, ch, en, w, rd, ctx, line);
    }
    enum { kMaxWorkers = 64 };
    static CtSeqPic* copies[kMaxWorkers];
    int workers = 1;
#ifdef _OPENMP
    workers = omp_get_max_threads();
    if (workers > kMaxWorkers) workers = kMaxWorkers;
#endif
    for (int k = 0; k < workers; k++) {
      if (!copies[k] && !(copies[k] = (CtSeqPic*)malloc(sizeof(CtSeqPic)))) { workers = k; break; }
      memcpy(copies[k], p, sizeof(CtSeqPic));
    }
    if (workers < 1) {                                                 // (no memory for a copy: the picture as it stands)
      for (int line = 1; line <= 224; line++) linePixels(p, &Ls[line], line, rgb + (size_t)(line - 1) * 256 * 3);
    } else {
      int line;
      #pragma omp parallel for schedule(dynamic, 4) num_threads(workers)
      for (line = 1; line <= 224; line++) {
        int k = 0;
#ifdef _OPENMP
        k = omp_get_thread_num();
#endif
        CtSeqPic* q = copies[k];
        memcpy(q->r, regs[line].r, sizeof q->r); memcpy(q->hofs, regs[line].hofs, sizeof q->hofs);
        memcpy(q->vofs, regs[line].vofs, sizeof q->vofs); memcpy(q->m7, regs[line].m7, sizeof q->m7);
        memcpy(q->fix, regs[line].fix, sizeof q->fix); q->mosaicStart = regs[line].mosaicStart;
        linePixels(q, &Ls[line], line, rgb + (size_t)(line - 1) * 256 * 3);
      }
    }
    if (!(p->r[0x00] & 0x80)) { p->oamAdr = p->oamAdrWritten; p->oamInHigh = p->oamInHighWritten; p->oamSecond = 0; }   // the vblank
    return;
  }
  for (int line = 1; line <= 224; line++) {
    if (g_seqPicArt && g_seqPicWideSide > 0) { int z = line; g_seqPicArt(p, -2, &z, 0, 0); }
    drawLine(p, &L, line, rgb ? rgb + (size_t)(line - 1) * 256 * 3 : scratch);
    if (rd) hdmaRun(p, ch, en, w, rd, ctx, line);
  }
  if (!(p->r[0x00] & 0x80)) { p->oamAdr = p->oamAdrWritten; p->oamInHigh = p->oamInHighWritten; p->oamSecond = 0; }   // the vblank
}

// ---- a stage's data ports ----
static void vramPortWrite(CtSeqStagePic* sp, int high, uint8_t v) {
  if (sp->vmain & 0x0C) {
    static int said = 0;
    if (!said) { said = 1; fprintf(stderr, "seqpic: VMAIN $%02X remaps the VRAM address -- not modelled\n", sp->vmain); }
  }
  unsigned a = sp->vaddr & 0x7FFF;
  if (high) sp->p.vram[a] = (uint16_t)((sp->p.vram[a] & 0x00FF) | (v << 8));
  else sp->p.vram[a] = (uint16_t)((sp->p.vram[a] & 0xFF00) | v);
  int inc = (sp->vmain & 3) == 0 ? 1 : (sp->vmain & 3) == 1 ? 32 : 128;
  if (((sp->vmain & 0x80) != 0) == high) sp->vaddr = (uint16_t)(sp->vaddr + inc);
}
void CtSeqStagePicVramWrite(CtSeqStagePic* sp, int high, uint8_t v) { vramPortWrite(sp, high, v); }
void CtSeqStagePicDma(CtSeqStagePic* sp, const uint8_t* w, uint8_t dmap, uint8_t bbad, uint32_t src, unsigned n) {
  int mode = dmap & 7, fixed = (dmap & 0x08) != 0, step = (dmap & 0x10) ? -1 : 1;
  unsigned oamAdr = 0, second = 0; uint8_t buf = 0;                  // ($2102 = 0 and $2121 written just before, in every stage)
  for (unsigned i = 0, j = 0; i < n; i++, j = (j + 1) % kLen[mode]) {
    uint8_t v = w[src & 0x1FFFF];
    if (!fixed) src = (src & 0x10000) | ((src + (unsigned)step) & 0xFFFF);
    uint8_t b = (uint8_t)(bbad + kBOff[mode][j]);
    if (b == 0x18 || b == 0x19) vramPortWrite(sp, b == 0x19, v);
    else if (b == 0x22) {                                              // CGRAM: the low byte buffered, the pair on the high
      if (!second) buf = v; else sp->p.cgram[sp->cgadd++] = (uint16_t)((v << 8) | buf);
      second ^= 1;
    } else if (b == 0x04) {                                            // OAM: 256 words, then the 32 bytes of the high table
      CtSeqPicWrite(&sp->p, 0x04, v, 0);                                // (through the OAM address the stage set)
    } else {
      static int said = 0;
      if (!said) { said = 1; fprintf(stderr, "seqpic: a DMA to $21%02X -- not modelled\n", b); }
    }
  }
}
// $2139: the word the read buffer holds (filled when $2116/$2117 were written), then the buffer refilled from the port's address
// and the address stepped -- with VMAIN bit 7 clear, as on the low byte (ppu.c `case 0x39`). A DMA of mode 0 from $2139 reads the
// low byte n times, the first word twice.
void CtSeqStagePicVramRead(CtSeqStagePic* sp, uint8_t* w, uint32_t dst, unsigned n) {
  uint16_t bufw = sp->p.vram[sp->vaddr & 0x7FFF];
  int inc = (sp->vmain & 3) == 0 ? 1 : (sp->vmain & 3) == 1 ? 32 : 128;
  for (unsigned i = 0; i < n; i++) {
    w[(dst + i) & 0x1FFFF] = (uint8_t)bufw;
    if (!(sp->vmain & 0x80)) { bufw = sp->p.vram[sp->vaddr & 0x7FFF]; sp->vaddr = (uint16_t)(sp->vaddr + inc); }
  }
}
void CtSeqStagePicVramReadWords(CtSeqStagePic* sp, uint8_t* w, uint32_t dst, unsigned n) {
  uint16_t bufw = sp->p.vram[sp->vaddr & 0x7FFF];
  int inc = (sp->vmain & 3) == 0 ? 1 : (sp->vmain & 3) == 1 ? 32 : 128;
  for (unsigned i = 0; i < n; i++) {
    int hi = (int)(i & 1);
    w[(dst + i) & 0x1FFFF] = (uint8_t)(hi ? bufw >> 8 : bufw);
    if (hi == ((sp->vmain & 0x80) != 0)) { bufw = sp->p.vram[sp->vaddr & 0x7FFF]; sp->vaddr = (uint16_t)(sp->vaddr + inc); }
  }
}
static uint8_t stageRead(void* ctx, uint32_t adr) {
  const uint8_t* w = (const uint8_t*)ctx;
  uint8_t bank = (uint8_t)(adr >> 16);
  return w[(((uint32_t)bank & 1) << 16) | (adr & 0xFFFF)];
}
void CtSeqStagePicFrame(CtSeqStagePic* sp, const uint8_t* w, uint8_t* rgb) {
  CtSeqPicFrame(&sp->p, sp->ch, sp->hdmaen, stageRead, (void*)w, rgb);
}
