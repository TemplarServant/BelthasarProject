// scene.c — render a Chrono Trigger field scene in C (the project's own engine).
// The renderer is split into LOAD (gather a scene's data — from the ROM, or from
// ct_assets.dat) and COMPOSE (turn that data into pixels). Both data sources feed
// the same compose path, so a .dat render is byte-identical to a ROM render.
//
// scene header -> tileset L12 (8 chipsets of LZ 4bpp chips + 512 16x16 metatiles,
// + chip animation) and L3 (2bpp + 256 metatiles) + scene palette (+ palette
// animation) -> tilemap (LZ: header, L1, L2, L3, RLE props, +256 upper bank) ->
// composite L3p0,L2p0,L1p0,L2p1,L1p1,L3p1 with SNES main/sub color-math, BG layers
// wrapping to fill -> crop to the scroll mask.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "scene.h"
#include "lz.h"
#include "assets.h"

#define SCENE_HDR 0x360000
#define SCENE_TS  0x361C00
#define SCENE_PAL 0x3624C0
#define MAP_PTRS  0x361E00
#define TS_PTRS   0x362220
#define ASM_PTRS  0x362100
#define ASM3_PTRS 0x3621C0
#define ANIM_PTRS 0x3DF290
#define ANIM_REL  0x3DF310
#define ANIM_END  0x3DF9CC
#define PAL_ADDR  0x3DF9C7
#define PAL_ANIM  0x3DFA77
#define PAL_COLOR 0x367380
#define BL1 0x01
#define BL2 0x02
#define BL3 0x04

static uint16_t rd16(const uint8_t* d, size_t o) { return (uint16_t)(d[o] | (d[o + 1] << 8)); }

static uint32_t ptr3(const uint8_t* rom, uint32_t table, int i) {
  uint32_t o = table + (uint32_t)i * 3;
  uint16_t p = rd16(rom, o);
  uint8_t bank = rom[o + 2];
  return (bank == 0 && p == 0) ? 0 : 0x10000u * (bank - 0xC0) + p;
}

static void snes_rgb(uint16_t w, uint8_t* out) {
  int r = w & 0x1F, g = (w >> 5) & 0x1F, b = (w >> 10) & 0x1F;
  out[0] = (uint8_t)((r << 3) | (r >> 2));
  out[1] = (uint8_t)((g << 3) | (g >> 2));
  out[2] = (uint8_t)((b << 3) | (b >> 2));
}

static void decode_tile(const uint8_t* d, int bpp, uint8_t* out) {
  int pairs = bpp / 2;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int v = 0, bit = 7 - x;
      for (int p = 0; p < pairs; p++) {
        uint8_t lo = d[p * 16 + y * 2], hi = d[p * 16 + y * 2 + 1];
        v |= ((lo >> bit) & 1) << (2 * p);
        v |= ((hi >> bit) & 1) << (2 * p + 1);
      }
      out[y * 8 + x] = (uint8_t)v;
    }
}

typedef struct { uint16_t chip; uint8_t base, fx, fy, prio; } Corner;
typedef struct { Corner c[4]; } Meta;

static void load_chips(const uint8_t* rom, uint32_t off, int bpp, int n, int base,
                       uint8_t* chips, uint8_t* present) {
  uint8_t* buf = malloc(0x20000);
  size_t len = ct_lz_decompress(rom, buf, off);
  int avail = (int)(len / (8 * bpp));
  if (n > avail) n = avail;
  for (int j = 0; j < n; j++) {
    decode_tile(buf + (size_t)j * (8 * bpp), bpp, chips + (size_t)(base + j) * 64);
    present[base + j] = 1;
  }
  free(buf);
}

static void parse_metas(const uint8_t* a, size_t alen, int count, int pal_mul, int sub256, Meta* out) {
  size_t p = 0;
  for (int t = 0; t < count; t++)
    for (int k = 0; k < 4; k++) {
      Corner* c = &out[t].c[k];
      if (p + 1 >= alen) { c->chip = 0; c->base = c->fx = c->fy = c->prio = 0; continue; }
      uint16_t v = (uint16_t)(a[p] | (a[p + 1] << 8)); p += 2;
      uint16_t chip = v & 0x3FF;
      // rebase to the scene chip space; non-scene words map to the 0xFFFF sentinel
      if (sub256) chip = (chip >= 256) ? (uint16_t)(chip - 256) : 0xFFFF;
      else if (chip == 0) chip = 0xFFFF;
      c->chip = chip;
      c->base = (uint8_t)(((v >> 10) & 7) * pal_mul);
      c->fx = (v & 0x4000) ? 1 : 0; c->fy = (v & 0x8000) ? 1 : 0; c->prio = (v & 0x2000) ? 1 : 0;
    }
}

// Read `count` metatiles from a ct_assets.dat blob (4 corners x {chip u16, base u8,
// flags u8: fx|fy<<1|prio<<2}) — the inverse of extract_assets.py's _ser_metas.
static void deserialize_metas(const uint8_t* b, int count, Meta* out) {
  for (int t = 0; t < count; t++)
    for (int k = 0; k < 4; k++) {
      const uint8_t* e = b + ((size_t)t * 4 + k) * 4;
      Corner* c = &out[t].c[k];
      c->chip = (uint16_t)(e[0] | (e[1] << 8));
      c->base = e[2];
      c->fx = e[3] & 1; c->fy = (e[3] >> 1) & 1; c->prio = (e[3] >> 2) & 1;
    }
}

// Apply a chip animation set (already-decoded animated chips + descriptors) onto the
// static chips for 60Hz tick `frame`. Pure: no ROM access — used by both data paths.
static void apply_chip_anim_data(uint8_t* chips, uint8_t* present, const uint8_t* animchips,
                                 int anim_chips, const uint8_t* desc, int desc_len, int frame) {
  int p = 0;
  while (p + 2 < desc_len) {
    uint8_t fc = desc[p++];
    if (fc == 0 || fc == 0x80) break;
    int dest = desc[p] | (desc[p + 1] << 8); p += 2;
    int durs[128], total = 0;
    for (int f = 0; f < fc; f++) { durs[f & 127] = desc[p + f]; total += desc[p + f]; }
    p += fc;
    int sel = 0;
    if (total > 0) { int t = frame % total, acc = 0; for (int f = 0; f < fc; f++) { acc += durs[f & 127]; if (t < acc) { sel = f; break; } } }
    int src = 0;
    for (int f = 0; f < fc; f++) { int sc = desc[p] | (desc[p + 1] << 8); p += 2; if (f == sel) src = sc; }
    for (int k = 0; k < 4; k++)
      if (dest + k >= 0 && dest + k < 768 && src + k >= 0 && src + k < anim_chips) {
        memcpy(chips + (size_t)(dest + k) * 64, animchips + (size_t)(src + k) * 64, 64);
        present[dest + k] = 1;
      }
  }
}

// Serialize a tileset's chip-animation descriptors from the ROM into a flat byte
// stream consumed by apply_chip_anim_data: per anim [fc, dest_lo, dest_hi,
// fc*dur_byte, fc*(src_lo,src_hi)], terminated by fc==0. Returns bytes written.
static int build_chip_anim_desc(const uint8_t* rom, uint32_t ts_index, uint8_t* out) {
  uint32_t a = rd16(rom, ANIM_PTRS + ts_index * 2) + ANIM_REL, nearest = ANIM_END;
  for (int i = 0; i < 64; i++) { uint32_t b = rd16(rom, ANIM_PTRS + i * 2) + ANIM_REL; if (b > a && b < nearest) nearest = b; }
  uint32_t p = a; int o = 0;
  while (p < nearest && p + 2 < ANIM_END) {
    uint8_t fc = rom[p++];
    if (fc == 0 || fc == 0x80) break;
    int dest = (rd16(rom, p) - 0x2000) / 16; p += 2;
    out[o++] = fc; out[o++] = (uint8_t)(dest & 0xFF); out[o++] = (uint8_t)(dest >> 8);
    for (int f = 0; f < fc; f++) {
      int nib = rom[p + f] & 0xF0;
      out[o++] = (uint8_t)(nib == 0x10 ? 16 : nib == 0x20 ? 12 : nib == 0x40 ? 8 : nib == 0x80 ? 4 : 0);
    }
    p += fc;
    for (int f = 0; f < fc; f++) { int sc = (rd16(rom, p) - 0x6000) / 32; p += 2; out[o++] = (uint8_t)(sc & 0xFF); out[o++] = (uint8_t)(sc >> 8); }
  }
  out[o++] = 0;
  return o;
}

// Apply a palette animation set (descriptors built by build_pal_anim_desc) at tick
// `frame`, mutating the 128-color palette. Pure. Descriptor per anim:
// [typ, ci, cc, delay, fc, fc*frame_byte, ncolors_lo,hi, ncolors*3 RGB], end typ==0.
static void apply_pal_anim_data(uint8_t pal[128][3], const uint8_t* desc, int desc_len, int frame) {
  int p = 0;
  while (p < desc_len) {
    uint8_t typ = desc[p++];
    if (typ == 0) break;
    int ci = desc[p], cc = desc[p + 1], delay = desc[p + 2], fc = desc[p + 3]; p += 4;
    int frames[256];
    for (int f = 0; f < fc && f < 256; f++) frames[f] = desc[p + f];
    p += fc;
    int ncol = desc[p] | (desc[p + 1] << 8); p += 2;
    const uint8_t* colors = desc + p; p += ncol * 3;
    int step = frame / (delay < 1 ? 1 : delay);
    if (typ == 0x30 || typ == 0x20) {
      int n = cc + 1;
      if (n <= 0 || ci + n > 128) continue;
      uint8_t tmp[129][3];
      for (int i = 0; i < n; i++) memcpy(tmp[i], pal[ci + i], 3);
      int steps = step % n;
      for (int s = 0; s < steps; s++) {
        if (typ == 0x30) {
          uint8_t last[3]; memcpy(last, tmp[n - 1], 3);
          for (int i = n - 1; i > 0; i--) memcpy(tmp[i], tmp[i - 1], 3);
          memcpy(tmp[0], last, 3);
        } else {
          uint8_t first[3]; memcpy(first, tmp[0], 3);
          for (int i = 0; i < n - 1; i++) memcpy(tmp[i], tmp[i + 1], 3);
          memcpy(tmp[n - 1], first, 3);
        }
      }
      for (int i = 0; i < n; i++) memcpy(pal[ci + i], tmp[i], 3);
    } else if (fc > 0 && ncol > 0) {
      int sel = frames[step % fc];
      int src = sel * cc;
      for (int i = 0; i < cc && ci + i < 128 && src + i < ncol; i++) memcpy(pal[ci + i], colors + (size_t)(src + i) * 3, 3);
    }
  }
}

// Serialize a scene's palette-animation set from the ROM into the flat stream that
// apply_pal_anim_data consumes. Returns bytes written.
static int build_pal_anim_desc(const uint8_t* rom, int index, uint8_t* out) {
  int o = 0;
  if (index * 2 + 1 < 0xB6) {
    uint32_t p = PAL_ANIM + rd16(rom, PAL_ADDR + index * 2), end = PAL_ANIM + 0x56E;
    while (p < end) {
      uint8_t idb = rom[p++];
      if (idb == 0) break;
      int typ = idb & 0xF0;
      if (typ != 0x80 && typ != 0x30 && typ != 0x20 && typ != 0x10) continue;
      int ci = rom[p], cc = rom[p + 1], delay = rom[p + 2]; p += 3;
      int fc = 0, frames[256], ncol = 0; uint8_t colbuf[256 * 3];
      if (typ == 0x80 || typ == 0x10) {
        uint32_t addr = (uint32_t)(rd16(rom, p) - 0x7380); p += 3;
        fc = (idb & 0x0F) + 1;
        int maxf = 0;
        for (int f = 0; f < fc && f < 256; f++) { frames[f] = (typ == 0x80) ? rom[p++] : f; if (frames[f] > maxf) maxf = frames[f]; }
        ncol = (maxf + 1) * cc;
        if (ncol > 256) ncol = 256;
        uint32_t cp = PAL_COLOR + addr;
        for (int i = 0; i < ncol; i++) snes_rgb(rd16(rom, cp + i * 2), colbuf + (size_t)i * 3);
      }
      out[o++] = (uint8_t)typ; out[o++] = (uint8_t)ci; out[o++] = (uint8_t)cc; out[o++] = (uint8_t)delay; out[o++] = (uint8_t)fc;
      for (int f = 0; f < fc; f++) out[o++] = (uint8_t)frames[f];
      out[o++] = (uint8_t)(ncol & 0xFF); out[o++] = (uint8_t)(ncol >> 8);
      for (int i = 0; i < ncol * 3; i++) out[o++] = colbuf[i];
    }
  }
  out[o++] = 0;
  return o;
}

static void draw_layer(uint8_t* px, uint8_t* cov, int W, int H, const uint16_t* tiles, int lw, int lh,
                       const Meta* metas, int nmeta, const uint8_t* chips, const uint8_t* present,
                       int nchips, uint8_t pal[128][3], int prio, uint8_t bit, uint8_t* priomask,
                       const uint8_t* tprio, int tpw, int tph, int sox, int soy, int mapRing, const uint16_t* held) {
  if (!tiles || !metas || !chips) return;
  // SNES BG layers wrap: a layer smaller than the canvas tiles to fill it
  // (chip_x % width, chip_y % height in CTViewer). Iterate the whole canvas in
  // metatiles and sample the layer modulo its own dimensions.
  // (sox,soy) = this layer's SAMPLE OFFSET: canvas pixel p shows layer pixel p + (sox,soy), wrapped.
  // It is how a layer that scrolls on its own (C0/9252/928E) is drawn on a canvas the field crops at
  // BG1's camera. With (0,0) -- every layer of every scene before layer scroll existed -- the loops
  // below visit exactly the cells they always did, plus one extra row and column that clip at once.
  int cwm = W / 16, chm = H / 16;
  int LW = lw * 16, LH = lh * 16;
  // ⭐ A MAP-BUILT LAYER WRAPS AT ITS TILEMAP, NOT AT ITS MAP. FD/C0D7 gives every field BG a 64x32 tilemap (BGnSC
  // $69/$71/$79), which the PPU addresses as two 32x32 halves (x < 256 and x >= 256; snes/ppu.c ppu_getPixelForBgLayer),
  // and C0/794E (BG2) / C0/7CE7 (BG3) fill them from the map: the first half with metatiles (0..15, 0..15); the second
  // with metatile columns 16..31 of rows 0..15 when the layer is wider than 16 ($0BCF / $0BD3 != $10, C0/7991 / 7D28),
  // else with rows 16..31 of columns 0..15 when it is taller ($0BD1 / $0BD5, C0/79AE / 7D45), else the first half again.
  // So the picture repeats every 256 px down whatever the map's height, and a 16-wide, 32-tall map alternates its top
  // and bottom halves across. (loc 1B0's BG2 is 32x32: the cartridge shows its top 16 rows on every line.)
  if (mapRing) { LW = (lw != 16 || lh != 16) ? 512 : 256; LH = 256; }
  int ox = ((sox % LW) + LW) % LW, oy = ((soy % LH) + LH) % LH;
  int mx0 = ox / 16, my0 = oy / 16, fx0 = ox % 16, fy0 = oy % 16;
  for (int cy = 0; cy <= chm; cy++)
    for (int cx = 0; cx <= cwm; cx++) {
      if (cy * 16 - fy0 >= H || cx * 16 - fx0 >= W) continue;
      int mr, mc;                                        // the map cell this metatile shows
      if (!mapRing) { mr = (cy + my0) % lh; mc = (cx + mx0) % lw; }
      else {
        int rx = (cx + mx0) % 32, ry = (cy + my0) % 16;
        if (lw != 16)      { mr = ry; mc = rx; }                                         // C0/7991 / 7D28
        else if (lh != 16) { mr = ry + (rx >= 16 ? 16 : 0); mc = rx % 16; }              // C0/79AE / 7D45
        else               { mr = ry; mc = rx % 16; }                                    // C0/7976 / 7D0D
      }
      int ti = tiles[mr * lw + mc];
      (void)tprio; (void)tpw; (void)tph;
      // (A "high-bank metas = prio" rule was tried and REFUTED against the live VRAM
      // tilemap: the real map prios at the bed are exactly the per-corner P bits.)
      for (int k = 0; k < 4; k++) {
        // A chip the VRAM still holds from an earlier upload (CtFieldScenePicHold) shows that metatile's corner.
        int tk = ti;
        if (held) { uint16_t hv = held[(mr * 2 + k / 2) * (lw * 2) + mc * 2 + (k & 1)]; if (hv != 0xFFFF) tk = hv; }
        if (tk >= nmeta) continue;
        const Corner* c = &metas[tk].c[k];
        if (c->chip >= nchips || !present[c->chip]) continue;
        if ((int)c->prio != (prio == 1)) continue;
        const uint8_t* tile = chips + (size_t)c->chip * 64;
        int kox = (k % 2) * 8, koy = (k / 2) * 8;
        for (int y = 0; y < 8; y++) {
          int py = cy * 16 - fy0 + koy + y; if (py >= H) break;
          if (py < 0) continue;
          for (int x = 0; x < 8; x++) {
            uint8_t v = tile[(c->fy ? 7 - y : y) * 8 + (c->fx ? 7 - x : x)];
            if (v == 0) continue;
            int pxx = cx * 16 - fx0 + kox + x; if (pxx >= W) break;
            if (pxx < 0) continue;
            int idx = py * W + pxx;
            memcpy(px + (size_t)idx * 3, pal[c->base + v], 3);
            cov[idx] = bit;
            // BG1/BG2 priority-1 tiles draw OVER field sprites (the player walks behind them);
            // BG3 priority-1 stays background. Record those pixels for the field to overlay.
            if (prio == 1 && priomask && (bit == BL1 || bit == BL2)) priomask[idx] = 1;
          }
        }
      }
    }
}

typedef struct { const uint16_t* tiles; int lw, lh; const Meta* metas; int nmeta;
                 const uint8_t* chips; const uint8_t* present; int nchips;
                 const uint8_t* tprio; int tpw, tph; int sox, soy; int mapRing; const uint16_t* held; } LInfo;

static void draw_pass(uint8_t* px, uint8_t* cov, int W, int H, int targets, uint8_t pal[128][3],
                      LInfo* l1, LInfo* l2, LInfo* l3, uint8_t* priomask) {
  int order_bit[6] = { BL3, BL2, BL1, BL2, BL1, BL3 };
  int order_prio[6] = { 0, 0, 0, 1, 1, 1 };
  LInfo* order_l[6] = { l3, l2, l1, l2, l1, l3 };
  for (int i = 0; i < 6; i++) {
    if (!(targets & order_bit[i])) continue;
    LInfo* L = order_l[i];
    draw_layer(px, cov, W, H, L->tiles, L->lw, L->lh, L->metas, L->nmeta,
               L->chips, L->present, L->nchips, pal, order_prio[i], (uint8_t)order_bit[i], priomask,
               L->tprio, L->tpw, L->tph, L->sox, L->soy, L->mapRing, L->held);
  }
}

// A scene's render-ready data: decoded tilesets, palette (anim baked), tilemaps
// (+256 baked), flags, scroll mask. Populated from the ROM or from ct_assets.dat.
typedef struct {
  uint8_t* chips12; uint8_t* pres12; Meta* meta12;
  uint8_t* chips3;  uint8_t* pres3;  Meta* meta3; int nchips3;
  uint8_t pal[128][3];
  uint16_t *l1t, *l2t, *l3t;
  uint8_t* tprio;              // per-tile props prio: bit0 = upper half (prop1&0x40),
  int tpw, tph;                // bit1 = lower half (prop2&0x40); dims = max(L1,L2)
  int w1, h1, w2, h2, w3, h3, has_l3;
  int screen_flags, effect_flags;
  int scrOn, scrCGWSEL, scrCGADSUB;   // CtFieldSceneSetScreen: a battle's own colour math
  int fxLive, fxLiveOn;                // CtFieldSceneSetEffect: the field's live CGADSUB ($0BE0), over the header's byte
  int scrTM, scrTS;                   // ...and its layer designations (BL1/BL2/BL3 bits)
  int scrObjM, scrObjS;               // ...and OBJ's ($212C/$212D bit 4)
  // The map header's layer-scroll bytes md[1..3] (ROM C0/A385-A4FB decodes them; see
  // CtSceneLayerScrollLoad) and whether this map carried them (older packs do not).
  uint8_t md1, md2, md3; int haveMdScroll;
  // Per-layer SAMPLE OFFSET in pixels, relative to BG1 (the camera): the canvas is cropped at the
  // camera, so a layer scrolling on its own is drawn sampled at +offset. 0 = locked to the camera.
  int l2ox, l2oy, l3ox, l3oy;
  uint8_t mapBuilt;            // $1D35 & 6 as the field last passed it: bit1 BG2, bit2 BG3 built from the map (see draw_layer)
  uint8_t vramOff;             // CtFieldSceneSetVramLayers: layers whose VRAM tilemap is no longer the map's (BL3: the caption plane)
  int sm_l, sm_t, sm_r, sm_b, sm_full;
  // retained chip/palette animation source: lets the field re-apply the BG animation for
  // any frame (flags waving, water, fire) instead of freezing it at the load-time frame.
  uint8_t basePal[128][3];                 // palette BEFORE palette-anim (apply mutates in place)
  uint8_t* animChips; int nAnimChips;      // decoded animated chips (nAnimChips * 64)
  uint8_t* chipAnimDesc; int chipAnimDescLen;
  uint8_t* palAnimDesc;  int palAnimDescLen;
  uint8_t* prevChips;                      // shadow of chips12 to detect when a re-bake is needed
  int animFrame;                           // last frame applied to chips12/pal
  uint8_t* bakeMain;                       // ...and its MAIN screen before the colour math (CtFieldSceneMainScreen)
  uint8_t* bakeSub; uint8_t* bakeSubCov;   // ...and its SUB screen (pixels, layer bits; NULL with no sub screen) and the
  int bakeBlend, bakeSubOp, bakeHalf;      //   math the bake applied with it (CtFieldSceneScreens)
  uint8_t* bakeCov; int bakeCovW, bakeCovH; // the last CtFieldSceneBake's MAIN-screen coverage: per pixel the layer bit
                                           //   (L1 1, L2 2, L3 4) of the pixel on top, 0 = backdrop
  uint8_t* layRgb[6]; uint8_t* layCov[6]; int layW[6], layH[6], layDirty;   // CtFieldSceneLayers, built on demand
  int bakedO[4];                           // the layer offsets the last bake drew at (l2ox l2oy l3ox l3oy then)
  uint8_t* bakeMotCov;                     // ...and the layer the pixel's picture comes from: the main screen's, else the
                                           //   sub screen's where only it shows (motion.h: what the pixel moves with)
  // THE CHIPS THE VRAM STILL HOLDS FROM AN EARLIER UPLOAD (CtFieldScenePicHold): per layer, (2 w) x (2 h) chips, the
  // metatile id whose corner that chip shows, 0xFFFF = the layer's own cell (l1t..). NULL until a chip is held.
  uint16_t* held[3]; int heldN[3];
} SceneData;

static void CtFreeSceneData(SceneData* sd) {
  free(sd->chips12); free(sd->pres12); free(sd->meta12);
  free(sd->chips3); free(sd->pres3); free(sd->meta3);
  free(sd->l1t); free(sd->l2t); free(sd->l3t); free(sd->tprio);
  for (int i = 0; i < 3; i++) { free(sd->held[i]); sd->held[i] = NULL; sd->heldN[i] = 0; }
  free(sd->animChips); free(sd->chipAnimDesc); free(sd->palAnimDesc); free(sd->prevChips); free(sd->bakeCov); free(sd->bakeMotCov); free(sd->bakeMain); free(sd->bakeSub); free(sd->bakeSubCov);
  for (int i = 0; i < 6; i++) { free(sd->layRgb[i]); free(sd->layCov[i]); sd->layRgb[i] = sd->layCov[i] = NULL; }
}

// Build a scene's render data from the ROM (decode tilesets/map/palette, bake the
// +256 props and the chip/palette animation for `frame`).
static void CtLoadSceneFromRom(const uint8_t* rom, int index, int frame,
                               const uint8_t* pal_override, SceneData* sd) {
  memset(sd, 0, sizeof(*sd));
  const uint8_t* h = rom + SCENE_HDR + index * 14;
  int ts_l12 = h[1], ts_l3 = h[2], pal_i = h[3], map_i = h[4] | (h[5] << 8);
  sd->sm_full = (h[10] == 0x80);
  sd->sm_l = h[10]; sd->sm_t = h[11]; sd->sm_r = h[12]; sd->sm_b = h[13];

  sd->chips12 = calloc(768, 64);
  sd->pres12 = calloc(768, 1);
  const uint8_t* chipsets = rom + SCENE_TS + ts_l12 * 8;
  for (int slot = 0; slot < 6; slot++) {
    uint8_t cs = chipsets[slot];
    if (cs == 0xFF || cs >= 204) continue;
    load_chips(rom, ptr3(rom, TS_PTRS, cs), 4, 128, slot * 128, sd->chips12, sd->pres12);
  }
  uint8_t cs6 = chipsets[6];
  if (cs6 != 0xFF && cs6 < 204) {
    uint8_t* anim = malloc(0x20000);
    size_t al = ct_lz_decompress(rom, anim, ptr3(rom, TS_PTRS, cs6));
    int nac = (int)(al / 32);
    uint8_t* achips = malloc((size_t)(nac > 0 ? nac : 1) * 64);
    for (int j = 0; j < nac; j++) decode_tile(anim + (size_t)j * 32, 4, achips + (size_t)j * 64);
    free(anim);
    uint8_t* desc = malloc(0x4000);
    int dl = build_chip_anim_desc(rom, ts_l12, desc);
    apply_chip_anim_data(sd->chips12, sd->pres12, achips, nac, desc, dl, frame);
    free(achips); free(desc);
  }

  uint8_t* abuf = malloc(0x20000);
  size_t alen = ct_lz_decompress(rom, abuf, ptr3(rom, ASM_PTRS, ts_l12 % 64));
  sd->meta12 = calloc(512, sizeof(Meta));
  parse_metas(abuf, alen, 512, 16, 1, sd->meta12);
  free(abuf);

  if (pal_override) memcpy(sd->pal, pal_override, sizeof(sd->pal));
  else {
    uint32_t pp = SCENE_PAL + pal_i * 210; int pi = 16;
    for (int s = 0; s < 7; s++) { pi++; for (int c = 0; c < 15; c++) { snes_rgb(rd16(rom, pp), sd->pal[pi++]); pp += 2; } }
  }
  uint8_t* pdesc = malloc(0x4000);
  int pdl = build_pal_anim_desc(rom, pal_i, pdesc);
  apply_pal_anim_data(sd->pal, pdesc, pdl, frame);
  free(pdesc);

  uint8_t* md = malloc(0x20000);
  size_t mdlen = ct_lz_decompress(rom, md, ptr3(rom, MAP_PTRS, map_i));
  int size = md[0], bits = md[1];
  sd->screen_flags = md[4]; sd->effect_flags = md[5];
  sd->md1 = md[1]; sd->md2 = md[2]; sd->md3 = md[3]; sd->haveMdScroll = 1;
  int w1 = ((size >> 0) & 3) * 16 + 16, h1 = ((size >> 2) & 3) * 16 + 16;
  int w2 = ((size >> 4) & 3) * 16 + 16, h2 = ((size >> 6) & 3) * 16 + 16;
  int w3 = ((bits >> 0) & 3) * 16 + 16, h3 = ((bits >> 2) & 3) * 16 + 16;
  int has_l3 = (bits & 0x80) != 0;
  sd->w1 = w1; sd->h1 = h1; sd->w2 = w2; sd->h2 = h2; sd->w3 = w3; sd->h3 = h3; sd->has_l3 = has_l3;
  size_t mp = 6;
  sd->l1t = malloc((size_t)w1 * h1 * 2);
  sd->l2t = malloc((size_t)w2 * h2 * 2);
  sd->l3t = has_l3 ? malloc((size_t)w3 * h3 * 2) : NULL;
  for (int i = 0; i < w1 * h1; i++) sd->l1t[i] = md[mp + i];
  mp += (size_t)w1 * h1;
  for (int i = 0; i < w2 * h2; i++) sd->l2t[i] = md[mp + i];
  mp += (size_t)w2 * h2;
  if (has_l3) { for (int i = 0; i < w3 * h3; i++) sd->l3t[i] = md[mp + i]; mp += (size_t)w3 * h3; }

  int sw = w1 > w2 ? w1 : w2, shh = h1 > h2 ? h1 : h2;
  uint8_t* a1 = calloc((size_t)sw * shh, 1);
  uint8_t* a2 = calloc((size_t)sw * shh, 1);
  sd->tprio = calloc((size_t)sw * shh, 1); sd->tpw = sw; sd->tph = shh;
  { int n = sw * shh, cnt = 0; size_t i = mp;
    while (cnt < n && i + 2 < mdlen) {
      uint8_t b0 = md[i], b1 = md[i + 1], b2 = md[i + 2]; i += 3; int rep = 1;
      if (b0 & 0x80) { rep = (i < mdlen) ? md[i] : 0; i++; if (rep == 0) rep = 256; }
      uint8_t tp = (uint8_t)(((b1 & 0x40) ? 1 : 0) | ((b2 & 0x40) ? 2 : 0));
      for (int r = 0; r < rep && cnt < n; r++) { a1[cnt] = b0 & 1; a2[cnt] = (b0 >> 1) & 1; sd->tprio[cnt] = tp; cnt++; }
    } }
  for (int y = 0; y < h1; y++) for (int x = 0; x < w1; x++) if (a1[y * sw + x]) sd->l1t[y * w1 + x] |= 256;
  for (int y = 0; y < h2; y++) for (int x = 0; x < w2; x++) if (a2[y * sw + x]) sd->l2t[y * w2 + x] |= 256;
  free(a1); free(a2); free(md);

  if (has_l3 && ts_l3 < 204 && ptr3(rom, TS_PTRS, ts_l3) != 0) {
    uint8_t* b3 = malloc(0x20000);
    size_t l3len = ct_lz_decompress(rom, b3, ptr3(rom, TS_PTRS, ts_l3));
    int n = (int)(l3len / 16); sd->nchips3 = n < 1 ? 1 : n;
    sd->chips3 = calloc(sd->nchips3, 64); sd->pres3 = calloc(sd->nchips3, 1);
    for (int j = 0; j < n; j++) { decode_tile(b3 + (size_t)j * 16, 2, sd->chips3 + (size_t)j * 64); sd->pres3[j] = 1; }
    free(b3);
    if (ts_l3 < 19 && ptr3(rom, ASM3_PTRS, ts_l3) != 0) {
      uint8_t* a3 = malloc(0x20000);
      size_t a3len = ct_lz_decompress(rom, a3, ptr3(rom, ASM3_PTRS, ts_l3));
      sd->meta3 = calloc(256, sizeof(Meta));
      parse_metas(a3, a3len, 256, 4, 0, sd->meta3);
      free(a3);
    }
  }
}

// Compose loaded scene data into a fresh RGB image (animation already baked in). If outPrio
// is non-NULL, also returns a 1-byte/pixel mask of BG1/BG2 priority-1 pixels (the foreground
// tiles a field engine should draw OVER sprites), cropped to match the returned image.
// THE FIELD'S COLOUR MATH IS $0BE0, not the header's byte: the load copies the map header's byte 5 there (C0/A40B-A412
// `LDA $7EB505 / STA $0BDF / STA $0BE0`, the same byte effect_flags holds) and channel 6's rows take it every frame (FD/C6F6);
// op 0xF1 (C0/3D2C-3D74) and C0/21E1's entry 9 (C0/25AE) rewrite it. CtFieldSceneSetEffect hands the bake the live byte.
static int sceneEffect(const SceneData* sd) { return sd->fxLiveOn ? sd->fxLive : sd->effect_flags; }
static int gSceneLayersOff = 0;   // BL1/BL2/BL3 bits removed from both screens for one compose (see below)
static uint8_t* gSceneMotCov;     // CtComposeScene's motion coverage, when it is asked for the coverage (CtFieldSceneBake)
static uint8_t* gSceneMainPre;    // ...and its main screen before the colour math (the same crop)
static uint8_t* gSceneSubPx;      // ...and its sub screen, pixels and layer bits (NULL with none), and the math it took
static uint8_t* gSceneSubCov;
static int gSceneBlend, gSceneSubOp, gSceneHalf;
static uint8_t* CtComposeScene(const SceneData* sd, int blend, int full, int* outW, int* outH, uint8_t** outPrio,
                               uint8_t** outCov) {
  int main_t = sd->screen_flags & 0x07, sub_t = (sd->screen_flags >> 4) & 0x07;
  int ef = sceneEffect(sd);
  int blend_en = ef & 0x07, sub_op = (ef & 0x80) != 0, half = (ef & 0x40) != 0;
  if (sd->scrOn) {                                   // a battle's screen (CtFieldSceneSetScreen): its TM/TS, CGWSEL bit 1 = add
    main_t = sd->scrTM; sub_t = sd->scrTS;           //   the SUB screen (clear: the fixed colour, the battle compositor's), and
    if (!(sd->scrCGWSEL & 0x02)) sub_t = 0;          //   CGADSUB. A BG3 on the effect's tilemap is not in scrTM/scrTS: it is the
    blend_en = sd->scrCGADSUB & 0x07;                //   EFFECT's layer (CtBattleFxLayerCompose draws it), not the field's that
    sub_op = (sd->scrCGADSUB & 0x80) != 0;           //   this bake holds.
    half = (sd->scrCGADSUB & 0x40) != 0;
  }
  if (!blend) { main_t |= sub_t; sub_t = 0; }
  main_t &= ~gSceneLayersOff; sub_t &= ~gSceneLayersOff;   // CtFieldSceneBakeLayersOff: layers a WINDOW takes away
  main_t &= ~sd->vramOff; sub_t &= ~sd->vramOff;           // CtFieldSceneSetVramLayers: the host draws them from VRAM
  // CT_SCENEDBG=1: the scene's LAYER SETUP, once per compose. main_t/sub_t are the TM/TS
  // designations the screen_flags byte carries (bits 0-2 main, 4-6 sub; BL1=1 BL2=2 BL3=4), and
  // has_l3 says whether the map even packs an L3. A scene whose background looks wrong is almost
  // always one of these three numbers, and there was no way to read them without a rebuild.
  { static int t = -1; if (t < 0) { const char* e = getenv("CT_SCENEDBG"); t = (e && *e) ? 1 : 0; }
    if (t) fprintf(stderr, "scenedbg: screen=%02X effect=%02X -> main_t=%X sub_t=%X blend_en=%X "
                           "half=%d sub_op=%d | has_l3=%d L1=%dx%d L2=%dx%d L3=%dx%d | md=%02X %02X %02X "
                           "off2=(%d,%d) off3=(%d,%d)\n",
                   sd->screen_flags, sd->effect_flags, main_t, sub_t, blend_en, half, sub_op,
                   sd->has_l3, sd->w1, sd->h1, sd->w2, sd->h2, sd->w3, sd->h3,
                   sd->md1, sd->md2, sd->md3, sd->l2ox, sd->l2oy, sd->l3ox, sd->l3oy); }
  // CT_SCENEISO=<hex>: compose ONLY these layers (1 = L1, 2 = L2, 4 = L3), main screen, sub off.
  // The field counterpart of the oracle's PEEK_ISOBG, and the only way to ask which of a scene's
  // layers is drawing a given thing without rebuilding.
  { static int iso = -2; if (iso == -2) { const char* e = getenv("CT_SCENEISO"); iso = e ? (int)strtol(e, NULL, 16) : -1; }
    if (iso >= 0) { main_t = iso & 0x07; sub_t = 0; } }

  int CW = sd->w1 > sd->w2 ? sd->w1 : sd->w2; if (sd->has_l3 && sd->w3 > CW) CW = sd->w3;
  int CH = sd->h1 > sd->h2 ? sd->h1 : sd->h2; if (sd->has_l3 && sd->h3 > CH) CH = sd->h3;
  int W = CW * 16, H = CH * 16;
  uint8_t* main_px = calloc((size_t)W * H, 3);
  uint8_t* main_cov = calloc((size_t)W * H, 1);
  uint8_t* sub_px = calloc((size_t)W * H, 3);
  uint8_t* sub_cov = calloc((size_t)W * H, 1);
  uint8_t* priomask = outPrio ? calloc((size_t)W * H, 1) : NULL;
  uint8_t palc[128][3]; memcpy(palc, sd->pal, sizeof(palc));

  LInfo li1 = { sd->l1t, sd->w1, sd->h1, sd->meta12, 512, sd->chips12, sd->pres12, 768, NULL, 1, 1, 0, 0, 0, sd->held[0] };
  LInfo li2 = { sd->l2t, sd->w2, sd->h2, sd->meta12, 512, sd->chips12, sd->pres12, 768, NULL, 1, 1, sd->l2ox, sd->l2oy, (sd->mapBuilt & 0x02) != 0, sd->held[1] };
  LInfo li3 = { sd->l3t, sd->w3, sd->h3, sd->meta3, 256, sd->chips3, sd->pres3, sd->nchips3, NULL, 1, 1, sd->l3ox, sd->l3oy, (sd->mapBuilt & 0x04) != 0, sd->held[2] };
  // ⭐ THE PRIORITY MASK IS A **LAYER-ORDER** FACT, NOT A MAIN-SCREEN ONE. main_t/sub_t are the
  // TM/TS designations, i.e. which layers feed COLOUR MATH; the SNES priority ladder (OBJ prio-3 >
  // BG1.1 > BG2.1 > OBJ prio-2 > BG1.0 > BG2.0 ...) is independent of them, and this compose already
  // shows sub-screen pixels wherever the main screen is uncovered (the `mc == 0 && sc` arm below).
  // Passing the mask only to the MAIN pass therefore lost every foreground tile of a scene that puts
  // BG1/BG2 on the SUB screen -- scene 129 (Manoria Cathedral) has screen_flags = 0xB4, so main_t = 4
  // (BG3 only) and sub_t = 0xB, and the whole scene reported fgpx = 0 with nothing able to occlude a
  // sprite. Measured: the oracle's live BG1 tilemap at loc $081 has 392 priority-bit entries, the
  // pews at metatiles (4..6, 18..19) carry them (tileset 11 metas 213/214/216/229/200/232), and the
  // oracle draws the nuns (obj10/obj11, $0C00 = $0C01 = 0x20 = OBJ prio 2) BEHIND those pews.
  draw_pass(main_px, main_cov, W, H, main_t, palc, &li1, &li2, &li3, priomask);
  draw_pass(sub_px, sub_cov, W, H, sub_t, palc, &li1, &li2, &li3, priomask);

  // THE COLOUR MATH IS THE PPU'S, IN 5 BITS (snes/ppu.c ppu_handlePixel): add or subtract the sub-screen colour, halve,
  // THEN clamp to 0..31. The palette holds conv5 values, so >> 3 recovers each channel's 5 bits exactly. The 8-bit
  // `(d >> 1) + (s >> 1)` this used to do is off by up to 4 per channel -- every shadow BG2 half-adds over BG3 (the
  // tent keeper, CGADSUB = $42) and every BG3 glow full-adds over BG1/BG2 (scene 130, CGADSUB = $24) came out wrong.
  uint8_t* pre = outCov ? malloc((size_t)W * H * 3) : NULL;   // the main screen as it stands before the math
  int keepSub = outCov && sub_t;                               // (and the sub screen, for a window that limits the math)
  gSceneBlend = blend_en; gSceneSubOp = sub_op; gSceneHalf = half;
  if (pre) memcpy(pre, main_px, (size_t)W * H * 3);
  if (sub_t) {
    static const uint8_t BLACK[3] = { 0, 0, 0 };
    for (int i = 0; i < W * H; i++) {
      int o = i * 3, mc = main_cov[i], sc = sub_cov[i];
      const uint8_t* inp = sc ? (sub_px + o) : BLACK;
      if ((mc & blend_en) && sc) {
        for (int k = 0; k < 3; k++) {
          int d = main_px[o + k] >> 3, s = inp[k] >> 3;
          int r = sub_op ? d - s : d + s;
          if (half) r >>= 1;
          if (r < 0) r = 0; if (r > 31) r = 31;
          main_px[o + k] = (uint8_t)((r << 3) | (r >> 2));
        }
      } else if (mc == 0 && sc) {
        memcpy(main_px + o, inp, 3);
      }
    }
  }
  // ...AND A MAIN-SCREEN PIXEL NO LAYER COVERS IS THE BACKDROP, CGRAM 0 (the PPU's own rule: snes/ppu.c ppu_handlePixel
  // takes colour 0 when every layer is transparent). The compose left it at calloc's black, which is the same colour in
  // nearly every location -- their $7E:2200 word is 0000 -- but not where a script paints it: the combat tutorial (loc
  // 0FE, obj0 `4B 00 22 7E D8 5C` = C0/67FB: $2200 := 5CD8) shows it inside its iris (FF 90 -> C0/2214: window 1 takes
  // BG1/BG2 off, TM $17), and the cartridge's circle is that magenta (saves/field_re/scene254_tutorial.state, CGRAM[0] =
  // 5CD8 in `--ppuregs`). The arm above keeps its sub-screen pixels; this is the rest.
  for (int i = 0; i < W * H; i++)
    if (!main_cov[i] && !(sub_t && sub_cov[i])) memcpy(main_px + (size_t)i * 3, palc[0], 3);
  uint8_t* mot = NULL;
  if (outCov && (mot = malloc((size_t)W * H)))
    for (int i = 0; i < W * H; i++) mot[i] = main_cov[i] ? main_cov[i] : (sub_t && sub_cov[i]) ? sub_cov[i] : 0;

  // crop to the scroll mask (metatiles; sm_full => whole layer 1)
  int cx0, cy0, cx1, cy1;
  if (full) { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }
  else if (sd->sm_full) { cx0 = 0; cy0 = 0; cx1 = sd->w1 * 16; cy1 = sd->h1 * 16; }
  else { cx0 = sd->sm_l * 16; cy0 = sd->sm_t * 16; cx1 = (sd->sm_r + 1) * 16; cy1 = (sd->sm_b + 1) * 16; }
  if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0;
  if (cx1 > W) cx1 = W; if (cy1 > H) cy1 = H;
  int cw = cx1 - cx0, ch = cy1 - cy0;
  uint8_t* out;
  if (cw > 0 && ch > 0 && (cw != W || ch != H)) {
    out = malloc((size_t)cw * ch * 3);
    for (int y = 0; y < ch; y++)
      memcpy(out + (size_t)y * cw * 3, main_px + ((size_t)(cy0 + y) * W + cx0) * 3, (size_t)cw * 3);
    *outW = cw; *outH = ch;
    free(main_px);
    if (outPrio) {                                     // crop the priority mask to match
      uint8_t* pm = malloc((size_t)cw * ch);
      for (int y = 0; y < ch; y++) memcpy(pm + (size_t)y * cw, priomask + (size_t)(cy0 + y) * W + cx0, (size_t)cw);
      free(priomask); *outPrio = pm;
    }
    if (outCov) {                                      // ...and the main-screen coverage
      uint8_t* cm = malloc((size_t)cw * ch);
      for (int y = 0; y < ch; y++) memcpy(cm + (size_t)y * cw, main_cov + (size_t)(cy0 + y) * W + cx0, (size_t)cw);
      *outCov = cm;
      uint8_t* mm = mot ? malloc((size_t)cw * ch) : NULL;
      if (mm) for (int y = 0; y < ch; y++) memcpy(mm + (size_t)y * cw, mot + (size_t)(cy0 + y) * W + cx0, (size_t)cw);
      free(mot); mot = mm;
      uint8_t* pp = pre ? malloc((size_t)cw * ch * 3) : NULL;
      if (pp) for (int y = 0; y < ch; y++) memcpy(pp + (size_t)y * cw * 3, pre + ((size_t)(cy0 + y) * W + cx0) * 3, (size_t)cw * 3);
      free(pre); pre = pp;
      if (keepSub) {
        uint8_t* sp = malloc((size_t)cw * ch * 3); uint8_t* sc = malloc((size_t)cw * ch);
        if (sp && sc)
          for (int y = 0; y < ch; y++) {
            memcpy(sp + (size_t)y * cw * 3, sub_px + ((size_t)(cy0 + y) * W + cx0) * 3, (size_t)cw * 3);
            memcpy(sc + (size_t)y * cw, sub_cov + (size_t)(cy0 + y) * W + cx0, (size_t)cw);
          }
        free(sub_px); free(sub_cov); sub_px = sp; sub_cov = sc;
      }
    }
  } else {
    out = main_px; *outW = W; *outH = H;
    if (outPrio) *outPrio = priomask;
    if (outCov) { *outCov = main_cov; main_cov = NULL; }
  }
  free(main_cov);
  free(gSceneSubPx); free(gSceneSubCov); gSceneSubPx = gSceneSubCov = NULL;
  if (keepSub) { gSceneSubPx = sub_px; gSceneSubCov = sub_cov; } else { free(sub_px); free(sub_cov); }
  free(gSceneMotCov); gSceneMotCov = mot;
  free(gSceneMainPre); gSceneMainPre = pre;
  return out;
}

uint8_t* CtRenderScene(const uint8_t* rom, int romlen, int index, int frame, int blend, int full,
                       const uint8_t* pal_override, int* outW, int* outH) {
  (void)romlen;
  SceneData sd;
  CtLoadSceneFromRom(rom, index, frame, pal_override, &sd);
  uint8_t* out = CtComposeScene(&sd, blend, full, outW, outH, NULL, NULL);
  CtFreeSceneData(&sd);
  return out;
}

// Build a scene's render data entirely from ct_assets.dat (no ROM): fetch the
// pre-decoded tilesets / map / palette / anim descriptors and apply the chip and
// palette animation for `frame`. The deserialized data feeds the same compose path.
static void CtLoadSceneFromAssets(const CtAssets* a, int index, int frame, SceneData* sd, int bakeAnim) {
  memset(sd, 0, sizeof(*sd));
  uint32_t n;
  const uint8_t* h = CtAssetGet(a, CT_ASSET_SCENE_HEADER, (uint32_t)index, &n);
  if (!h || n < 14) return;
  int ts12 = h[1], ts3 = h[2], pal_i = h[3], map_i = h[4] | (h[5] << 8);
  sd->sm_full = (h[10] == 0x80);
  sd->sm_l = h[10]; sd->sm_t = h[11]; sd->sm_r = h[12]; sd->sm_b = h[13];

  // tileset L12
  sd->chips12 = calloc(768, 64); sd->pres12 = calloc(768, 1); sd->meta12 = calloc(512, sizeof(Meta));
  const uint8_t* ts = CtAssetGet(a, CT_ASSET_TILESET12, (uint32_t)ts12, &n);
  if (ts) {
    const uint8_t* q = ts;
    memcpy(sd->chips12, q, 768 * 64); q += 768 * 64;
    memcpy(sd->pres12, q, 768); q += 768;
    deserialize_metas(q, 512, sd->meta12); q += 512 * 16;
    int nanim = q[0] | (q[1] << 8); q += 2;
    const uint8_t* animchips = q; q += (size_t)nanim * 64;
    int dl = q[0] | (q[1] << 8); q += 2;
    if (bakeAnim) apply_chip_anim_data(sd->chips12, sd->pres12, animchips, nanim, q, dl, frame);
    if (nanim > 0 && dl > 0) {                          // retain for per-frame re-bake (field anim)
      sd->nAnimChips = nanim;
      sd->animChips = malloc((size_t)nanim * 64); memcpy(sd->animChips, animchips, (size_t)nanim * 64);
      sd->chipAnimDescLen = dl;
      sd->chipAnimDesc = malloc((size_t)dl); memcpy(sd->chipAnimDesc, q, (size_t)dl);
      sd->prevChips = malloc(768 * 64); memcpy(sd->prevChips, sd->chips12, 768 * 64);
    }
  }

  // palette (+ palette animation)
  const uint8_t* p = CtAssetGet(a, CT_ASSET_SCENE_PALETTE, (uint32_t)pal_i, &n);
  if (p && n >= 128 * 3) memcpy(sd->pal, p, 128 * 3);
  memcpy(sd->basePal, sd->pal, sizeof sd->basePal);     // snapshot before anim (re-apply needs the base)
  const uint8_t* pa = CtAssetGet(a, CT_ASSET_PAL_ANIM, (uint32_t)pal_i, &n);
  if (pa) {
    if (bakeAnim) apply_pal_anim_data(sd->pal, pa, (int)n, frame);
    sd->palAnimDescLen = (int)n;
    sd->palAnimDesc = malloc((size_t)n); memcpy(sd->palAnimDesc, pa, (size_t)n);
  }
  sd->animFrame = frame;

  // map (dims + flags + tilemaps, +256 already baked)
  const uint8_t* m = CtAssetGet(a, CT_ASSET_MAP, (uint32_t)map_i, &n);
  if (m) {
    sd->w1 = m[0]; sd->h1 = m[1]; sd->w2 = m[2]; sd->h2 = m[3]; sd->w3 = m[4]; sd->h3 = m[5];
    sd->has_l3 = m[6]; sd->screen_flags = m[7]; sd->effect_flags = m[8];
    const uint8_t* t = m + 9;
    int n1 = sd->w1 * sd->h1, n2 = sd->w2 * sd->h2, n3 = sd->has_l3 ? sd->w3 * sd->h3 : 0;
    sd->l1t = malloc((size_t)n1 * 2);
    for (int i = 0; i < n1; i++) sd->l1t[i] = (uint16_t)(t[i * 2] | (t[i * 2 + 1] << 8));
    t += (size_t)n1 * 2;
    sd->l2t = malloc((size_t)n2 * 2);
    for (int i = 0; i < n2; i++) sd->l2t[i] = (uint16_t)(t[i * 2] | (t[i * 2 + 1] << 8));
    t += (size_t)n2 * 2;
    if (sd->has_l3) {
      sd->l3t = malloc((size_t)n3 * 2);
      for (int i = 0; i < n3; i++) sd->l3t[i] = (uint16_t)(t[i * 2] | (t[i * 2 + 1] << 8));
      t += (size_t)n3 * 2;
    }
    // trailing per-tile props-prio plane (newer packs; bit0 = upper half, bit1 = lower)
    int sw = sd->w1 > sd->w2 ? sd->w1 : sd->w2, shh = sd->h1 > sd->h2 ? sd->h1 : sd->h2;
    sd->tpw = sw; sd->tph = shh;
    if ((size_t)(t - m) + (size_t)sw * shh <= n) {
      sd->tprio = malloc((size_t)sw * shh);
      memcpy(sd->tprio, t, (size_t)sw * shh);
      // ...then the layer-scroll bytes md[1..3], when the pack carries them (extract_assets.py).
      const uint8_t* ms = t + (size_t)sw * shh;
      if ((size_t)(ms - m) + 3 <= n) { sd->md1 = ms[0]; sd->md2 = ms[1]; sd->md3 = ms[2]; sd->haveMdScroll = 1; }
    } else sd->tprio = calloc((size_t)sw * shh, 1);
    if (getenv("CT_SCNDBG")) {
      int nz = 0; for (int i = 0; i < sw * shh; i++) nz += sd->tprio[i] != 0;
      fprintf(stderr, "[scn] map dims L1 %dx%d L2 %dx%d blobLen=%u used=%d tprio nz=%d l2t[8*64+24]=%03X\n",
              sd->w1, sd->h1, sd->w2, sd->h2, n, (int)(t - m), nz, sd->l2t[8 * sd->w2 + 24]);
    }
  }

  // tileset L3
  const uint8_t* t3 = CtAssetGet(a, CT_ASSET_TILESET3, (uint32_t)ts3, &n);
  if (sd->has_l3 && t3) {
    const uint8_t* q = t3;
    int nc = q[0] | (q[1] << 8); q += 2;
    sd->nchips3 = nc < 1 ? 1 : nc;
    sd->chips3 = calloc(sd->nchips3, 64); sd->pres3 = calloc(sd->nchips3, 1);
    memcpy(sd->chips3, q, (size_t)nc * 64); q += (size_t)nc * 64;
    memcpy(sd->pres3, q, nc); q += nc;
    int has_metas = q[0]; q += 1;
    if (has_metas) { sd->meta3 = calloc(256, sizeof(Meta)); deserialize_metas(q, 256, sd->meta3); }
  }
}

uint8_t* CtRenderSceneFromAssets(const CtAssets* a, int index, int frame, int blend, int full,
                                 int* outW, int* outH) {
  SceneData sd;
  CtLoadSceneFromAssets(a, index, frame, &sd, 1);
  if (sd.w1 == 0) { CtFreeSceneData(&sd); *outW = 0; *outH = 0; return calloc(1, 3); }
  uint8_t* out = CtComposeScene(&sd, blend, full, outW, outH, NULL, NULL);
  CtFreeSceneData(&sd);
  return out;
}

// Render a field scene from ct_assets.dat (blended, full, no crop) AND return a malloc'd
// priority mask (*outPrioMask, 1 byte/pixel: BG1/BG2 priority-1 = a foreground tile the
// player walks behind). The field draws the base, then sprites, then re-draws the masked
// foreground pixels from the base on top. Caller frees both buffers.
uint8_t* CtRenderFieldBase(const CtAssets* a, int index, int frame, int* outW, int* outH, uint8_t** outPrioMask) {
  SceneData sd;
  CtLoadSceneFromAssets(a, index, frame, &sd, 1);
  if (sd.w1 == 0) { CtFreeSceneData(&sd); *outW = 0; *outH = 0; if (outPrioMask) *outPrioMask = NULL; return calloc(1, 3); }
  uint8_t* out = CtComposeScene(&sd, 1, 1, outW, outH, outPrioMask, NULL);
  CtFreeSceneData(&sd);
  return out;
}

// ---- mutable scene handle: lets the field engine apply scripted TILE COPIES (event ops
// 0xE4/0xE5 -- curtains, doors, switches) and re-bake. The ROM worker C0/AF4E copies an
// inclusive tile rect between map positions, per layer-select flag bits (low+high tile
// bytes; our layers are u16 so one copy each). Collision props (flag bit 3) are not
// mutated here (the viewer's collision stays the load-time snapshot). ----
void* CtFieldSceneOpen(const CtAssets* a, int index, int frame) {
  SceneData* sd = malloc(sizeof(SceneData));
  if (!sd) return NULL;
  CtLoadSceneFromAssets(a, index, frame, sd, 0);
  if (sd->w1 == 0) { CtFreeSceneData(sd); free(sd); return NULL; }
  return sd;
}
void CtFieldSceneClose(void* h) {
  if (!h) return;
  CtFreeSceneData((SceneData*)h);
  free(h);
}
uint8_t* CtFieldSceneBake(void* h, int* outW, int* outH, uint8_t** outPrioMask) {
  SceneData* sd = (SceneData*)h;
  uint8_t* cov = NULL;
  uint8_t* out = CtComposeScene(sd, 1, 1, outW, outH, outPrioMask, &cov);
  if (out) { free(sd->bakeCov); sd->bakeCov = cov; sd->bakeCovW = *outW; sd->bakeCovH = *outH;
             free(sd->bakeMotCov); sd->bakeMotCov = gSceneMotCov; gSceneMotCov = NULL; sd->layDirty = 1;
             free(sd->bakeMain); sd->bakeMain = gSceneMainPre; gSceneMainPre = NULL;
             free(sd->bakeSub); free(sd->bakeSubCov); sd->bakeSub = gSceneSubPx; sd->bakeSubCov = gSceneSubCov;
             gSceneSubPx = gSceneSubCov = NULL;
             sd->bakeBlend = gSceneBlend; sd->bakeSubOp = gSceneSubOp; sd->bakeHalf = gSceneHalf;
             sd->bakedO[0] = sd->l2ox; sd->bakedO[1] = sd->l2oy; sd->bakedO[2] = sd->l3ox; sd->bakedO[3] = sd->l3oy; }
  else free(cov);
  return out;
}
// The same compose with `layersOff` (BL1 1 / BL2 2 / BL3 4) taken off BOTH screens -- what a pixel shows where a window
// masks those layers on the main and the sub screen (TMW/TSW): the field's iris hands window 1 to BG1 and BG2 (W12SEL $22,
// TMW = TSW = $17), and inside it the pixel is whatever the other layers make of it. Leaves the bake's coverage alone.
uint8_t* CtFieldSceneBakeLayersOff(void* h, int layersOff, int* outW, int* outH) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return NULL;
  gSceneLayersOff = layersOff & 0x07;
  uint8_t* out = CtComposeScene(sd, 1, 1, outW, outH, NULL, NULL);
  gSceneLayersOff = 0;
  return out;
}
// THE MAIN SCREEN OF THE LAST BAKE, BEFORE THE COLOUR MATH (same size as the bake), and how a SPRITE meets it. The bake
// holds the map's two screens already mixed; a sprite drawn over that picture is right only where OBJ is on the main
// screen. Where the screen setup puts OBJ on the SUB screen alone, the PPU (ppu_handlePixel) takes the main pixel -- the
// layer on top of the main screen there -- and, when that layer has colour math, adds or subtracts the sub pixel, which is
// the sprite wherever the sprite wins the sub screen. Returns NULL when that is not the case (OBJ on the main screen, or
// no sub screen); else the image, and the math the bake itself applies (blend layers, subtract, half).
const uint8_t* CtFieldSceneMainScreen(void* h, int* blendEn, int* subOp, int* half) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->bakeMain) return NULL;
  int objM = (sd->screen_flags & 0x08) != 0, objS = (sd->screen_flags & 0x80) != 0;
  int ef = sceneEffect(sd);
  int be = ef & 0x07, so = (ef & 0x80) != 0, hf = (ef & 0x40) != 0;
  if (sd->scrOn) {
    objM = sd->scrObjM; objS = sd->scrObjS && (sd->scrCGWSEL & 0x02);
    be = sd->scrCGADSUB & 0x07; so = (sd->scrCGADSUB & 0x80) != 0; hf = (sd->scrCGADSUB & 0x40) != 0;
  }
  if (objM || !objS || !be) return NULL;
  if (blendEn) *blendEn = be; if (subOp) *subOp = so; if (half) *half = hf;
  return sd->bakeMain;
}
// THE LAST BAKE'S TWO SCREENS, for a COLOUR WINDOW that limits the math (field.c fieldColWindowApply): the sub screen's
// pixels (NULL when the bake had no sub screen) and its layer bits (0 = transparent), the main screen before the math, and
// the math the bake applied everywhere (blend layers, subtract, half) -- what a pixel outside the window must lose.
const uint8_t* CtFieldSceneScreens(void* h, const uint8_t** subCov, const uint8_t** mainPre, int* blendEn, int* subOp,
                                   int* half) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->bakeSub || !sd->bakeSubCov || !sd->bakeMain) return NULL;
  if (subCov) *subCov = sd->bakeSubCov; if (mainPre) *mainPre = sd->bakeMain;
  if (blendEn) *blendEn = sd->bakeBlend; if (subOp) *subOp = sd->bakeSubOp; if (half) *half = sd->bakeHalf;
  return sd->bakeSub;
}
// The main-screen coverage of the last bake (see SceneData.bakeCov); NULL before the first.
// A MOD's picture (widext.h, settings.h wideSteamMargins): a w x h metatile map in four planes drawn with the Steam
// release's palette (pal555: 128 BGR555 colours; NULL = this scene's) and tiles -- its chip sets where slotChips names one (else this scene's), its definitions of
// the metatiles in metaIds (4 corners of u16 chip, u8 palette row, u8 flags H 1 / V 2 / priority 4 at metaCorners + 16 i)
// over this scene's -- plane 3 first up to plane 0, each whole. Ids 0 (none) and 512 and up (tiles the Steam release
// added) draw nothing. rgb is (16 w) x (16 h) x 3, cov 1 byte a pixel (BL1 where drawn); both are cleared first.
// (Study knobs: CT_WIDE_EXT_ORDER = the planes, first drawn first; CT_WIDE_EXT_PRIOSPLIT=1 = every plane's priority-0
// corners before any priority-1 one.)
int CtFieldSceneBakeExt(void* h, const uint16_t* planes, int w, int hgt, const uint8_t* const slotChips[6], int nMeta,
                        const uint16_t* metaIds, const uint8_t* metaCorners, const uint8_t* pal555, uint8_t* rgb, uint8_t* cov) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->meta12 || !sd->chips12 || !planes || w <= 0 || hgt <= 0) return 0;
  int W = w * 16, H = hgt * 16, n = w * hgt;
  memset(rgb, 0, (size_t)W * H * 3); memset(cov, 0, (size_t)W * H);
  uint16_t* t = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)n);
  uint8_t* chips = (uint8_t*)malloc(768 * 64); uint8_t* pres = (uint8_t*)malloc(768); Meta* metas = (Meta*)malloc(512 * sizeof(Meta));
  if (!t || !chips || !pres || !metas) { free(t); free(chips); free(pres); free(metas); return 0; }
  memcpy(chips, sd->chips12, 768 * 64); memcpy(pres, sd->pres12, 768); memcpy(metas, sd->meta12, 512 * sizeof(Meta));
  for (int sl = 0; sl < 6; sl++)
    if (slotChips && slotChips[sl]) { memcpy(chips + (size_t)sl * 128 * 64, slotChips[sl], 128 * 64); memset(pres + sl * 128, 1, 128); }
  for (int m = 0; m < nMeta; m++) {
    if (metaIds[m] >= 512) continue;
    for (int k = 0; k < 4; k++) {
      const uint8_t* e = metaCorners + (size_t)m * 16 + k * 4;
      Corner* c = &metas[metaIds[m]].c[k];
      c->chip = (uint16_t)(e[0] | e[1] << 8); c->base = (uint8_t)(e[2] * 16);
      c->fx = e[3] & 1; c->fy = (e[3] >> 1) & 1; c->prio = (e[3] >> 2) & 1;
    }
  }
  uint8_t pal[128][3];                             // the Steam release's colours, else this scene's (CT_WIDE_EXT_ROMPAL=1)
  if (pal555 && !getenv("CT_WIDE_EXT_ROMPAL")) for (int i = 0; i < 128; i++) snes_rgb((uint16_t)(pal555[2 * i] | pal555[2 * i + 1] << 8), pal[i]);
  else memcpy(pal, sd->pal, sizeof pal);
  const char* ord = getenv("CT_WIDE_EXT_ORDER"); if (!ord || !*ord) ord = "3210";
  int split = getenv("CT_WIDE_EXT_PRIOSPLIT") && atoi(getenv("CT_WIDE_EXT_PRIOSPLIT"));
  for (int pass = 0; pass < (split ? 2 : 1); pass++)
    for (const char* c = ord; *c >= '0' && *c <= '3'; c++) {
      int k = *c - '0';
      for (int i = 0; i < n; i++) { uint16_t v = planes[(size_t)k * n + i]; t[i] = (v == 0 || v >= 512) ? 0xFFFF : v; }
      for (int prio = split ? pass : 0; prio < (split ? pass + 1 : 2); prio++)
        draw_layer(rgb, cov, W, H, t, w, hgt, metas, 512, chips, pres, 768, pal, prio, BL1, NULL, NULL, 0, 0, 0, 0, 0, NULL);
    }
  free(t); free(chips); free(pres); free(metas);
  return 1;
}

const uint8_t* CtFieldSceneCoverage(void* h, int* w, int* hgt) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->bakeCov) return NULL;
  if (w) *w = sd->bakeCovW; if (hgt) *hgt = sd->bakeCovH;
  return sd->bakeCov;
}
int CtFieldSceneLayerOffsetsBaked(void* h, int* o2x, int* o2y, int* o3x, int* o3y) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  *o2x = sd->bakedO[0]; *o2y = sd->bakedO[1]; *o3x = sd->bakedO[2]; *o3y = sd->bakedO[3];
  return 1;
}
int CtFieldSceneLayers(void* h, CtSceneLayers* o) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !o) return 0;
  // the bake's own setup (CtComposeScene's head, blend on)
  int main_t = sd->screen_flags & 0x07, sub_t = (sd->screen_flags >> 4) & 0x07;
  int ef = sceneEffect(sd);
  int blend_en = ef & 0x07, sub_op = (ef & 0x80) != 0, half = (ef & 0x40) != 0;
  if (sd->scrOn) {
    main_t = sd->scrTM; sub_t = sd->scrTS;
    if (!(sd->scrCGWSEL & 0x02)) sub_t = 0;
    blend_en = sd->scrCGADSUB & 0x07; sub_op = (sd->scrCGADSUB & 0x80) != 0; half = (sd->scrCGADSUB & 0x40) != 0;
  }
  main_t &= ~sd->vramOff; sub_t &= ~sd->vramOff;
  if (sd->layDirty || !sd->layRgb[0]) {
    uint8_t palc[128][3]; memcpy(palc, sd->pal, sizeof palc);
    LInfo li[3] = {
      { sd->l1t, sd->w1, sd->h1, sd->meta12, 512, sd->chips12, sd->pres12, 768, NULL, 1, 1, 0, 0, 0, sd->held[0] },
      { sd->l2t, sd->w2, sd->h2, sd->meta12, 512, sd->chips12, sd->pres12, 768, NULL, 1, 1, 0, 0, (sd->mapBuilt & 0x02) != 0, sd->held[1] },
      { sd->l3t, sd->w3, sd->h3, sd->meta3, 256, sd->chips3, sd->pres3, sd->nchips3, NULL, 1, 1, 0, 0, (sd->mapBuilt & 0x04) != 0, sd->held[2] } };
    static const int ordLay[6] = { 2, 1, 0, 1, 0, 2 }, ordPri[6] = { 0, 0, 0, 1, 1, 1 };
    static const uint8_t ordBit[6] = { BL3, BL2, BL1, BL2, BL1, BL3 };
    for (int i = 0; i < 6; i++) {
      LInfo* L = &li[ordLay[i]];
      int LW = L->lw * 16, LH = L->lh * 16;
      if (L->mapRing) { LW = (L->lw != 16 || L->lh != 16) ? 512 : 256; LH = 256; }
      if (LW <= 0 || LH <= 0) { LW = LH = 16; }
      if (sd->layW[i] != LW || sd->layH[i] != LH || !sd->layRgb[i]) {
        free(sd->layRgb[i]); free(sd->layCov[i]);
        sd->layRgb[i] = malloc((size_t)LW * LH * 3); sd->layCov[i] = malloc((size_t)LW * LH);
        sd->layW[i] = LW; sd->layH[i] = LH;
      }
      if (!sd->layRgb[i] || !sd->layCov[i]) return 0;
      memset(sd->layRgb[i], 0, (size_t)LW * LH * 3); memset(sd->layCov[i], 0, (size_t)LW * LH);
      if (ordLay[i] == 2 && !sd->has_l3) continue;
      draw_layer(sd->layRgb[i], sd->layCov[i], LW, LH, L->tiles, L->lw, L->lh, L->metas, L->nmeta, L->chips, L->present,
                 L->nchips, palc, ordPri[i], ordBit[i], NULL, L->tprio, L->tpw, L->tph, 0, 0, L->mapRing, L->held);
    }
    sd->layDirty = 0;
  }
  for (int i = 0; i < 6; i++) { o->rgb[i] = sd->layRgb[i]; o->cov[i] = sd->layCov[i]; o->w[i] = sd->layW[i]; o->h[i] = sd->layH[i]; }
  o->mainT = main_t; o->subT = sub_t; o->blendEn = blend_en; o->subOp = sub_op; o->half = half;
  memcpy(o->backdrop, sd->pal[0], 3);
  return 1;
}
// The layer each pixel of the last bake moves with (SceneData.bakeMotCov), the coverage's size; NULL before the first.
const uint8_t* CtFieldSceneMotionCoverage(void* h) {
  SceneData* sd = (SceneData*)h;
  return sd ? sd->bakeMotCov : NULL;
}
// The map header's layer-scroll bytes md[1..3]. Returns 0 (and zeros) for a pack that predates them.
int CtFieldSceneMapHeader(void* h, uint8_t* md1, uint8_t* md2, uint8_t* md3) {
  SceneData* sd = (SceneData*)h;
  int ok = sd && sd->haveMdScroll;
  if (md1) *md1 = ok ? sd->md1 : 0;
  if (md2) *md2 = ok ? sd->md2 : 0;
  if (md3) *md3 = ok ? sd->md3 : 0;
  return ok;
}
int CtFieldSceneBg1Size(void* h, int* w, int* hgt) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  if (w) *w = sd->w1;
  if (hgt) *hgt = sd->h1;
  return 1;
}
// The layers (BL1/BL2/BL3 bits) whose VRAM tilemap no longer holds this map -- the field's NMI uploaded WRAM over it (the
// caption plane, C0/EB46-EB77) -- so the bake leaves them out and the host draws them from its VRAM image. Returns 1 when
// that changes (re-bake). And the screen designations the bake composes with (TM / TS as the scene's flags give them).
int CtFieldSceneSetVramLayers(void* h, int layers) {
  SceneData* sd = (SceneData*)h;
  if (!sd || sd->vramOff == (uint8_t)(layers & 0x07)) return 0;
  sd->vramOff = (uint8_t)(layers & 0x07);
  return 1;
}
int CtFieldSceneScreenLayers(void* h, int* mainT, int* subT, int* mathEn) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  if (mainT) *mainT = sd->scrOn ? sd->scrTM : sd->screen_flags & 0x07;
  if (subT) *subT = sd->scrOn ? sd->scrTS : (sd->screen_flags >> 4) & 0x07;
  if (mathEn) *mathEn = sd->scrOn ? (sd->scrCGADSUB & 0x07) : (sd->effect_flags & 0x07);
  return 1;
}
// BG2/BG3 sample offsets relative to BG1, in pixels. Returns 1 when they changed (re-bake needed).
int CtFieldSceneSetLayerOffsets(void* h, int bg2x, int bg2y, int bg3x, int bg3y, int mapBuilt) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  mapBuilt &= 0x06;
  if (sd->l2ox == bg2x && sd->l2oy == bg2y && sd->l3ox == bg3x && sd->l3oy == bg3y && sd->mapBuilt == mapBuilt) return 0;
  sd->l2ox = bg2x; sd->l2oy = bg2y; sd->l3ox = bg3x; sd->l3oy = bg3y; sd->mapBuilt = (uint8_t)mapBuilt;
  return 1;
}

// A DMA into the BG1/BG2 characters: VRAM words $2000-$4FFF are what chips12 holds (chip c = word $2000 + c*16 -- the
// metatiles' chip numbers are the tile numbers minus 256, parse_metas's sub256), so `n` bytes of 4bpp planar
// characters replace n/32 chips from `vramWord` on. FD/E022's $80 bytes are four. Returns 1 when a chip changed.
int CtFieldSceneTileDma(void* h, int vramWord, const uint8_t* bytes, int n) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->chips12 || !bytes || vramWord < 0x2000 || ((vramWord - 0x2000) & 15)) return 0;
  int c0 = (vramWord - 0x2000) >> 4, changed = 0;
  for (int j = 0; j < n / 32 && c0 + j < 768; j++) {
    uint8_t t[64];
    decode_tile(bytes + j * 32, 4, t);
    uint8_t* d = sd->chips12 + (size_t)(c0 + j) * 64;
    if (!sd->pres12[c0 + j] || memcmp(d, t, 64)) { memcpy(d, t, 64); sd->pres12[c0 + j] = 1; changed = 1; }
  }
  return changed;
}
// The BG colours: CGRAM 0-127 as the NMI uploads them, from the live page $7E:2200 (BGR555 words). Every writer of the
// field's colours meets there -- C0/7084's load, FD/E39C's animation records, op 0x88 -- so the bake reads the page, not
// a palette of its own. Returns 1 when a colour changed.
int CtFieldSceneSetEffect(void* h, int cgadsub) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  int ef = cgadsub & 0xFF, ch = !sd->fxLiveOn ? ef != sd->effect_flags : ef != sd->fxLive;
  sd->fxLive = ef; sd->fxLiveOn = 1;
  return ch;
}
int CtFieldSceneSetScreen(void* h, int on, int cgwsel, int cgadsub, int tm, int ts) {
  SceneData* sd = (SceneData*)h;
  if (!sd) return 0;
  if (!on) { int ch = sd->scrOn != 0; sd->scrOn = 0; return ch; }
  int om = (tm & 0x10) != 0, os = (ts & 0x10) != 0;
  tm &= 0x07; ts &= 0x07;
  if (sd->scrOn && sd->scrCGWSEL == cgwsel && sd->scrCGADSUB == cgadsub && sd->scrTM == tm && sd->scrTS == ts
      && sd->scrObjM == om && sd->scrObjS == os) return 0;
  sd->scrOn = 1; sd->scrCGWSEL = cgwsel; sd->scrCGADSUB = cgadsub; sd->scrTM = tm; sd->scrTS = ts;
  sd->scrObjM = om; sd->scrObjS = os;
  return 1;
}
int CtFieldSceneSetCgram(void* h, const uint8_t* page) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !page) return 0;
  uint8_t np[128][3];
  for (int i = 0; i < 128; i++) snes_rgb((uint16_t)(page[i * 2] | (page[i * 2 + 1] << 8)), np[i]);
  if (!memcmp(np, sd->pal, sizeof np)) return 0;
  memcpy(sd->pal, np, sizeof np);
  return 1;
}
// The three maps as WRAM holds them (C0/A521-A66B's layout, lz.c ct_lz_map_to_wram): BG1 cells at $7E:3000, BG2 at
// $3040, BG3 at $3080, each row $100 bytes apart, and the +256 bits in $30C0 (bit 0 BG1, bit 1 BG2). A door or a chest
// opens by INCrementing a cell there (C0/0D4C, C0/1705), so a WRAM image that came from somewhere else -- a seed --
// carries its open doors only in these bytes. Returns 1 when a cell changed.
int CtFieldSceneMapFromWram(void* h, const uint8_t* s7e) {
  return CtFieldSceneMapFromWramRect(h, s7e, 0, 0, 63, 63, 7);
}
// ...the same over a rect of chosen layers (bit 0/1/2 = BG1/BG2/BG3): what op 0xE5's style branch re-uploads -- C0/75E9 /
// 78EC / 7CB5 build the tilemaps from this WRAM map, after C0/1790's AF4E moved the rect in it. Reading the map rather
// than copying inside the render's own layers matters: a location's copies park rects in rows the layers do not have
// (loc 002's curtain at rows $12-$1C and $32-$3C of a 64-row map), and a render-side copy clipped them away.
int CtFieldSceneMapFromWramRect(void* h, const uint8_t* s7e, int x0, int y0, int x1, int y1, int layers) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !s7e) return 0;
  int changed = 0;
  for (int L = 0; L < 3; L++) {
    if (!(layers & (1 << L))) continue;
    uint16_t* t = L == 0 ? sd->l1t : L == 1 ? sd->l2t : sd->l3t;
    int w = L == 0 ? sd->w1 : L == 1 ? sd->w2 : sd->w3, hh = L == 0 ? sd->h1 : L == 1 ? sd->h2 : sd->h3;
    if (!t || w > 64 || hh > 64) continue;
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < hh; y++)
      for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
        int o = y * 256 + x;
        uint16_t v = (uint16_t)(s7e[0x3000 + L * 0x40 + o] | ((L < 2 && (s7e[0x30C0 + o] >> L) & 1) ? 0x100 : 0));
        if (t[y * w + x] != v) { t[y * w + x] = v; changed = 1; }
      }
  }
  return changed;
}
// THE PICTURE IS THE VRAM'S, NOT THE WRAM MAP'S. The field's tilemaps are built from the map in WRAM only at the ROM's
// uploads: the load's fill (C0/0A50), op 0xE5's style branch (C0/179A: the layer's whole window, C0/75E9 / 78EC / 7CB5),
// the camera streamer's edges (C0/93E1 -> C0/979B..9918 -> the twelve builders C0/8243-8442, uploaded by the NMI's
// C0/EB91 -> C0/8445..), and the door / chest handlers' own writes. Op 0xE4 (C0/3D97) is `JSR C0/AF4E` on the WRAM map
// and nothing else, so a chip the VRAM holds keeps its old metatile until one of those rewrites it. The layers here
// (l1t..) follow the WRAM map; a HELD chip shows the metatile it had when the copy came. disasm/notes/field_tile_copy_e4.md.
static int picInWin(int c, int lo, int hi, int cm) {   // chip c inside [lo..hi] on a map that wraps at cm + 1 chips
  int span = (hi - lo) & 0xFF;
  return span >= cm || ((c - lo) & cm) <= span;
}
static uint16_t* picHeld(SceneData* sd, int L, int* w, int* hh) {
  *w = L == 0 ? sd->w1 : L == 1 ? sd->w2 : sd->w3; *hh = L == 0 ? sd->h1 : L == 1 ? sd->h2 : sd->h3;
  return L == 0 ? sd->l1t : L == 1 ? sd->l2t : sd->l3t;
}
// Before a copy lands on the layers: the chips of the metatile rect (x0..x1, y0..y1) that the VRAM holds -- inside the
// chip window win[0..3] (left, top, right, bottom, inclusive; the map wraps at cmx + 1 / cmy + 1 chips), or every chip
// when win is NULL (a layer built from the map, which no streamer rewrites) -- keep the metatile they show now.
int CtFieldScenePicHold(void* h, int layer, int x0, int y0, int x1, int y1, const int* win, int cmx, int cmy) {
  SceneData* sd = (SceneData*)h;
  if (!sd || layer < 0 || layer > 2) return 0;
  int w, hh; const uint16_t* t = picHeld(sd, layer, &w, &hh);
  if (!t || w <= 0 || hh <= 0) return 0;
  int n = 0;
  for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < hh; y++)
    for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++)
      for (int k = 0; k < 4; k++) {
        int cx = x * 2 + (k & 1), cy = y * 2 + k / 2;
        if (win && !(picInWin(cx, win[0], win[2], cmx) && picInWin(cy, win[1], win[3], cmy))) continue;
        if (!sd->held[layer]) {
          sd->held[layer] = (uint16_t*)malloc((size_t)w * hh * 4 * sizeof(uint16_t));
          if (!sd->held[layer]) return n;
          memset(sd->held[layer], 0xFF, (size_t)w * hh * 4 * sizeof(uint16_t));
        }
        uint16_t* p = &sd->held[layer][cy * (w * 2) + cx];
        if (*p == 0xFFFF) { *p = t[y * w + x]; sd->heldN[layer]++; n++; }
      }
  return n;
}
// An upload rewrote chips from the WRAM map: release the held ones OUTSIDE the chip window (the streamer builds every
// chip that enters it, C0/8243-8442; what lies outside is not on the screen), or all of them when win is NULL (a rebuild).
// Returns the number released (the picture changed).
int CtFieldScenePicRelease(void* h, int layer, const int* win, int cmx, int cmy) {
  SceneData* sd = (SceneData*)h;
  if (!sd || layer < 0 || layer > 2 || !sd->held[layer] || !sd->heldN[layer]) return 0;
  int w, hh; picHeld(sd, layer, &w, &hh);
  int n = 0;
  for (int cy = 0; cy < hh * 2; cy++)
    for (int cx = 0; cx < w * 2; cx++) {
      uint16_t* p = &sd->held[layer][cy * (w * 2) + cx];
      if (*p == 0xFFFF) continue;
      if (win && picInWin(cx, win[0], win[2], cmx) && picInWin(cy, win[1], win[3], cmy)) continue;
      *p = 0xFFFF; n++;
    }
  sd->heldN[layer] -= n;
  return n;
}
// ...the same over a metatile rect: a handler that writes its block to VRAM itself (the doors' C0/0D78..1454, the chest's
// C0/1705) shows the WRAM's cells there at once.
int CtFieldScenePicReleaseRect(void* h, int layer, int x0, int y0, int x1, int y1) {
  SceneData* sd = (SceneData*)h;
  if (!sd || layer < 0 || layer > 2 || !sd->held[layer] || !sd->heldN[layer]) return 0;
  int w, hh; picHeld(sd, layer, &w, &hh);
  int n = 0;
  for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < hh; y++)
    for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++)
      for (int k = 0; k < 4; k++) {
        uint16_t* p = &sd->held[layer][(y * 2 + k / 2) * (w * 2) + x * 2 + (k & 1)];
        if (*p != 0xFFFF) { *p = 0xFFFF; n++; }
      }
  sd->heldN[layer] -= n;
  return n;
}
void CtFieldSceneDumpTiles(void* h, int x0, int y0, int x1, int y1) {
  SceneData* sd = (SceneData*)h;
  fprintf(stderr, "[tiles] L1 (%d..%d, %d..%d):\n", x0, x1, y0, y1);
  for (int y = y0; y <= y1; y++) {
    fprintf(stderr, "  y=%2d:", y);
    for (int x = x0; x <= x1; x++) fprintf(stderr, " %03X", sd->l1t[y * sd->w1 + x]);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "[tiles] L2:\n");
  for (int y = y0; y <= y1; y++) {
    fprintf(stderr, "  y=%2d:", y);
    for (int x = x0; x <= x1; x++) fprintf(stderr, " %03X", sd->l2t[y * sd->w2 + x]);
    fprintf(stderr, "\n");
  }
}

// ⭐ A DOOR OPENS BY INCREMENTING ITS LAYER-1 METATILE IDS. The ROM's door machine is
// C0/0D3E: when the player-collision commit reports a door underfoot ($0117 bit4, set by
// C0/9D47), it clears the bit, reads the L1 metatile at the latched tile ($015B -> `LDX $5B /
// LDA $7E3000,X`) and dispatches on its VALUE -- 0xE6 -> C0/0D78, 0xEC -> C0/0E5F, 0xEE ->
// C0/1014, 0xFA -> C0/11C9, 0xFC -> C0/1454, anything else -> C0/16DC (not a door it knows).
// Every one of those handlers is the same shape: `INC A / STA $7E3000,X` over a fixed little
// block of tiles, walking X by +-1 (a COLUMN step; the index is (row << 8) | col) and by
// -0x100 (one ROW up). So "open" is simply "closed + 1" in the metatile table, and the shape of
// the block is what distinguishes a 1-wide door from a 2-wide one:
//   0xE6  2 tiles  (r,c) (r-1,c)
//   0xEC  4 tiles  (r,c) (r,c+1) (r-1,c+1) (r-1,c)
//   0xEE  4 tiles  (r,c) (r,c-1) (r-1,c-1) (r-1,c)
//   0xFA  6 tiles  (r,c) (r,c+1) (r-1,c+1) (r-1,c) (r-2,c) (r-2,c+1)
//   0xFC  6 tiles  (r,c) (r,c-1) (r-1,c-1) (r-1,c) (r-2,c) (r-2,c-1)
// It is a ONE-SHOT: the bumped ids no longer match any case, so a second step is inert and the
// door stays open until the location reloads (measured -- walking back off the tile does not
// close it). MEASURED on loc 004 (`--monitor 7E320A:2 --writer` walking into the top door):
// $7E320A/320B F2->F3 F4->F5, $7E330A/330B F6->F7 F8->F9, $7E340A/340B FA->FB FC->FD at f17,
// writer C0:1212 = inside the 0xFA handler; NOTHING in the collision planes changed.
// The +256 bank bit lives in the top bit of our l1t word (the L1_TILE_ADD prop), and the ROM's
// INC is an 8-bit one on the raw plane byte, so it must not carry into it.
int CtFieldSceneDoorOpen(void* h, int tx, int ty) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->l1t || tx < 0 || ty < 0 || tx >= sd->w1 || ty >= sd->h1) return 0;
  int id = sd->l1t[ty * sd->w1 + tx] & 0xFF;
  static const struct { uint8_t id; int8_t n; int8_t dc[6], dr[6]; } DOOR[5] = {
    { 0xE6, 2, { 0, 0, 0, 0, 0, 0 },  {  0, -1,  0,  0,  0,  0 } },   // C0/0D78
    { 0xEC, 4, { 0, 1, 1, 0, 0, 0 },  {  0,  0, -1, -1,  0,  0 } },   // C0/0E5F
    { 0xEE, 4, { 0,-1,-1, 0, 0, 0 },  {  0,  0, -1, -1,  0,  0 } },   // C0/1014
    { 0xFA, 6, { 0, 1, 1, 0, 0, 1 },  {  0,  0, -1, -1, -2, -2 } },   // C0/11C9
    { 0xFC, 6, { 0,-1,-1, 0, 0,-1 },  {  0,  0, -1, -1, -2, -2 } },   // C0/1454
  };
  for (int k = 0; k < 5; k++) {
    if (DOOR[k].id != id) continue;
    for (int u = 0; u < DOOR[k].n; u++) {
      int cx = tx + DOOR[k].dc[u], cy = ty + DOOR[k].dr[u];
      if (cx < 0 || cy < 0 || cx >= sd->w1 || cy >= sd->h1) continue;
      uint16_t* t = &sd->l1t[cy * sd->w1 + cx];
      *t = (uint16_t)((*t & 0xFF00) | ((*t + 1) & 0xFF));   // 8-bit INC of the plane byte only
    }
    return 1;
  }
  return 0;                                              // C0/16DC: not a door metatile
}

// THE OTHER HALF OF C0/16DC -- the TREASURE CHEST. The door dispatcher above falls straight into
// this routine (C0/0D75 BRL ->16DC), and the two arms are the same idea with different ids and a
// different requester: a door is asked for by the collision commit ($0117 bit4 + $015B), a chest by
// the A-button treasure probe C0/1E4C ($0117 bit1 + $015D). The body, read verbatim:
//   16DC LDA $17 / BIT #02 / BNE ->16E5 / BRL ->1770      ; the request bit
//   16E5 STZ $61 / LDA #02 / TRB $17 / JSR C0/1BA7
//   16EE LDX $5D / LDA $7E3000,X                          ; the L1 metatile at the requested tile
//   16F4 CMP #FE / BEQ ->1700 ; INC $61 ; CMP #E0 / BEQ ->1700 ; BRA ->1770
//   1700 INC A / STA $7E3000,X / [1705..] re-upload the metatile to VRAM
// So exactly TWO closed-chest ids exist, $FE and $E0, and "open" is again "closed + 1" -- ONE tile,
// not a block (a chest is a single metatile). $0161 (0 for the $FE family, 1 for the $E0 family) is
// the id the SFX/animation arm reads; it carries no map effect, so nothing here mirrors it.
// MEASURED: loc 26 tile(30,37) $7E551E FE->FF and loc 14 tile(41,8) $7E3829 FE->FF, both writer
// C0:1705 with $0161=0; loc 47 tile(12,11) $7E3B0C E0->E1, writer C0:1705, $0161=01 (C0:16FA).
// Like the door it is a ONE-SHOT: $FF/$E1 match neither compare, so a second request is inert.
int CtFieldSceneChestOpen(void* h, int tx, int ty) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !sd->l1t || tx < 0 || ty < 0 || tx >= sd->w1 || ty >= sd->h1) return 0;
  uint16_t* t = &sd->l1t[ty * sd->w1 + tx];
  int id = *t & 0xFF;
  if (id != 0xFE && id != 0xE0) return 0;                // 16F4/16F8: neither chest family
  *t = (uint16_t)((*t & 0xFF00) | ((id + 1) & 0xFF));    // 1700 INC A (8-bit, the +256 bank bit stays)
  return 1;
}

void CtFieldSceneCopyTiles(void* h, int sl, int st, int el, int eb, int dl, int dt, int flags) {
  SceneData* sd = (SceneData*)h;
  int rw = el - sl + 1, rh = eb - st + 1;
  if (rw <= 0 || rh <= 0) return;
  for (int pass = 0; pass < 3; pass++) {
    uint16_t* L; int lw, lh, bit;
    // ROM C0/AF4E flag dispatch: bit0/1/2 = tile planes $3000/$3040/$3080 (L1/L2/L3); bits
    // 3/4/5 = the PROP planes $30C0/$7000/$7040 (bit3's full-byte copy carries the prio bits
    // 0-1 = tprio below; the shape/z part lands in the field's collision planes, field.c);
    // bits 6/7 = partial $30C0 bit0/bit1-only copies (not implemented -- no user yet).
    if (pass == 0) { L = sd->l1t; lw = sd->w1; lh = sd->h1; bit = flags & 0x01; }
    else if (pass == 1) { L = sd->l2t; lw = sd->w2; lh = sd->h2; bit = flags & 0x02; }
    else { L = sd->l3t; lw = sd->w3; lh = sd->h3; bit = flags & 0x04; }
    if (!bit || !L) continue;
    for (int y = 0; y < rh; y++)
      for (int x = 0; x < rw; x++) {
        int sx = sl + x, sy = st + y, dx = dl + x, dy = dt + y;
        if (sx < 0 || sx >= lw || sy < 0 || sy >= lh) continue;
        if (dx < 0 || dx >= lw || dy < 0 || dy >= lh) continue;
        L[dy * lw + dx] = L[sy * lw + sx];
      }
  }
  if ((flags & 0x08) && sd->tprio) {                    // $30C0 full-byte copy: the half-tile
    int lw = sd->tpw, lh = sd->tph;                     // BG-prio bits travel with the room
    for (int y = 0; y < rh; y++)                        // state (the shape part = field.c
      for (int x = 0; x < rw; x++) {                    // fieldCollFromWramRect)
        int sx = sl + x, sy = st + y, dx = dl + x, dy = dt + y;
        if (sx < 0 || sx >= lw || sy < 0 || sy >= lh) continue;
        if (dx < 0 || dx >= lw || dy < 0 || dy >= lh) continue;
        sd->tprio[dy * lw + dx] = sd->tprio[sy * lw + sx];
      }
  }
}

// Sprite compositor: ports assets/sprites.py compose_frame. The packed blob is
// [size u8][nframes u8][ntiles u16] palette(16*RGB) tiles(ntiles*64) then per frame
// units*4 chip u16 + units*2 signed offset bytes. Each of the 4 chips per unit is a
// quadrant (TL/TR/BL/BR); chip low 10 bits = source tile, 0x4000 flip-x, 0x8000 flip-y.
int CtComposeSprite(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal) {
  return CtComposeSpriteHalves(blob, blobLen, frame, out, outPal, NULL, NULL);
}
int CtComposeSpriteBox(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal, int* box) {
  return CtComposeSpriteHalves(blob, blobLen, frame, out, outPal, box, NULL);
}
// WHICH PRIORITY BYTE EACH UNIT TAKES -- the anim drain's attr fill, read off the four class bodies (bank C0):
//   class 0 (4 units), C0/CD47-CDBB and its twin C0/CE77..: per unit, `LDA $7F4804,X` (the unit's Y offset, the frame
//     record's +0x21 + 2k byte) / `CMP #E8` / `BCC` -> `ORA $0C00,Y` (the TOP byte) when the offset is below E8
//     unsigned -- every offset from -128 to -25 AND every non-negative one -- else `ORA $0C01,Y` (the BOTTOM byte): a
//     unit whose offset is -24..-1, i.e. the one at the anchor's foot.
//   class 1 (8 units), C0/D0F0-D114 / C0/D257-D27B / C0/D4C6..: units 0-3 `ORA $0C00,Y`, units 4-7 `ORA $0C01,Y`.
//   class 2 (12 units), C0/DA23-DA5C / C0/DCE2.. / C0/DEE7-DF20 / C0/E0E2..: units 0-3 $0C00, units 4-11 $0C01.
//   class 3 (24 units), C0/E4D1-E52F: `LDA #22` into all 24 attrs -- OBJ priority 2, neither byte read.
// `half` (may be NULL) receives, per opaque pixel, the source of the unit that owns it: 0 = $0C00, 1 = $0C01, 2 = the
// fixed priority 2. disasm/notes/field_obj_unit_priority.md.
int CtComposeSpriteHalves(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal, int* box,
                          uint8_t* half) {
  int bx0 = CT_SPR_W, by0 = CT_SPR_H, bx1 = -1, by1 = -1;
  if (box) { box[0] = bx0; box[1] = by0; box[2] = bx1; box[3] = by1; }
  if (!blob || blobLen < 4 + 48) return 0;
  int size = blob[0] & 3, nframes = blob[1];
  int ntiles = blob[2] | (blob[3] << 8);
  if (frame < 0 || frame >= nframes) return 0;
  static const int UNITS[4] = { 4, 8, 12, 24 };          // groups*tpg for size 0..3
  int units = UNITS[size];
  const uint8_t* pal = blob + 4;
  const uint8_t* tiles = pal + 48;
  uint32_t framesz = (uint32_t)units * 10;               // units*4*2 chips + units*2 offsets
  uint32_t need = 4 + 48 + (uint32_t)ntiles * 64 + framesz * (uint32_t)(frame + 1);
  if (blobLen < need) return 0;
  const uint8_t* fr = tiles + (uint32_t)ntiles * 64 + (uint32_t)frame * framesz;
  const uint8_t* offs = fr + (uint32_t)units * 8;        // after units*4 u16 chips
  memcpy(outPal, pal, 48);
  memset(out, 0, CT_SPR_W * CT_SPR_H);
  for (int k = 0; k < units * 4; k++) {
    int chip = fr[k * 2] | (fr[k * 2 + 1] << 8);
    int src = chip & 0x3FF;
    if (src >= ntiles) continue;
    int fx = chip & 0x4000, fy = chip & 0x8000;
    int lx = (k & 1) ? 8 : 0, ly = (k & 2) ? 8 : 0;      // k%4: 0 TL,1 TR,2 BL,3 BR
    int ox = (int8_t)offs[(k / 4) * 2], oy = (int8_t)offs[(k / 4) * 2 + 1];
    int u = k / 4;                                       // the unit = the ROM's OBJ tile t
    uint8_t hs = size == 3 ? 2 : size ? (u < 4 ? 0 : 1) : (offs[u * 2 + 1] >= 0xE8 ? 1 : 0);
    const uint8_t* tile = tiles + (uint32_t)src * 64;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        uint8_t v = tile[(fy ? 7 - y : y) * 8 + (fx ? 7 - x : x)];
        if (!v) continue;
        int px = ox + lx + x + CT_SPR_OX, py = oy + ly + y + CT_SPR_OY;
        // ⭐ THE FIRST UNIT IS IN FRONT. Unit k is the ROM's OBJ tile t = k: its VRAM tile is $0D00 + t*2 and
        // the OAM build copies the tiles in t order into ascending slots (oam.md, C0/CC22 decoder + C0/B330
        // copy), and a LOWER OAM slot wins. The oracle's attract demo battle (build/_loc0AD.state f1) has Flea
        // at slots 40..47 = tiles 080..08E. Painting later units over earlier ones hid her face under her
        // own cape -- invisible for any frame whose units do not overlap, which is nearly all of them.
        if (px >= 0 && px < CT_SPR_W && py >= 0 && py < CT_SPR_H && !out[py * CT_SPR_W + px]) {
          out[py * CT_SPR_W + px] = v;
          if (half) half[py * CT_SPR_W + px] = hs;
          if (px < bx0) bx0 = px; if (px > bx1) bx1 = px;
          if (py < by0) by0 = py; if (py > by1) by1 = py;
        }
      }
  }
  if (box) { box[0] = bx0; box[1] = by0; box[2] = bx1; box[3] = by1; }
  return 1;
}

// THE FIELD'S VRAM TILEMAPS AS THE ROM WRITES THEM (C0/794E BG2, C0/7CE7 BG3, and BG1's twin), for a host that has to
// hand a translated stage the VRAM the field left: entry 3 ($7E:671F) DMA-reads BG1's tilemap rows at $6140 and keeps
// the field's characters. FD/C0D7 gives every field BG a 64x32 tilemap ($6000 / $6800 / $7000 in words); the two
// 32x32 halves are filled by the law draw_layer already follows (the first with metatiles (0..15, 0..15), the second
// with columns 16..31 when the layer is wider than 16, else rows 16..31 when taller, else the first again). The
// word writer C0/79CF: per metatile `$7E:B000 + id*8` holds the four corner words, TL TR on the even row and BL BR
// on the odd, each `chip | pal << 10 | prio << 13 | fx << 14 | fy << 15` -- the pack's Corner is that word decoded
// (chip, base = pal*16, fx, fy, prio), and the map's +256 prio variants are baked into the tile ids.
static void vram_tilemap_layer(uint16_t* vram, int base, const uint16_t* tiles, int lw, int lh, const uint8_t* asm8, int nmeta, int halves) {
  if (!tiles || !asm8) return;
  // The builders walk all 64 rows into $7E:C800 (C0/794E: rows 0-31, then C0/7976 / 7991 / 79AE for the second
  // half); the ENTRY uploads one half (C0/7F62, $0800 bytes) or both (C0/7F77, $1000) -- BG1 always one (C0/0A9F),
  // BG2 both with $1D35 bit1 (C0/0AB4), BG3 both with bit2 (C0/0AE1). A half not uploaded keeps what VRAM held (the
  // credits' text tilemap on the attract's 06B) until the streamer's rows reach it.
  for (int half = 0; half < halves; half++)
    for (int my = 0; my < 16; my++)
      for (int mx = 0; mx < 16; mx++) {
        int ti;
        if (half == 0)          ti = tiles[(my % lh) * lw + (mx % lw)];
        else if (lw != 16)      ti = tiles[(my % lh) * lw + ((mx + 16) % lw)];              // C0/7991
        else if (lh != 16)      ti = tiles[((my + 16) % lh) * lw + mx];                     // C0/79AE
        else                    ti = tiles[(my % lh) * lw + mx];                            // C0/7976
        if (ti >= nmeta) continue;
        for (int k = 0; k < 4; k++) {                                    // C0/79FC B000,X / B002,X (even row) and
          const uint8_t* e = asm8 + (size_t)ti * 8 + (size_t)k * 2;      // C0/7AE5 B004,X / B006,X (odd row)
          int row = my * 2 + (k / 2), col = mx * 2 + (k % 2);
          vram[base + half * 0x400 + row * 32 + col] = (uint16_t)(e[0] | (e[1] << 8));
        }
      }
}
// `asm12` = the tileset's assembly as C0/09DD put it at $7E:B000 (ct_lz_load_assembly), `asm3` = the BG3 assembly
// at $7E:C000 (C0/0A14) or NULL. The ids in the pack's tilemaps carry the ROM's +256 prio variant already.
// `d35` = $1D35 (the detached-layer bits, A4A5/A4F5), `bg3` = C0/0ACC-0AD6's gate (`$0BCA & 1` and `$01BB == 0`).
int CtFieldSceneVramTilemaps(void* h, uint16_t* vram, const uint8_t* asm12, const uint8_t* asm3, int d35, int bg3) {
  SceneData* sd = (SceneData*)h;
  if (!sd || !vram || !asm12) return 0;
  vram_tilemap_layer(vram, 0x6000, sd->l1t, sd->w1, sd->h1, asm12, 512, 1);                       // C0/0A9F: 7F62
  vram_tilemap_layer(vram, 0x6800, sd->l2t, sd->w2, sd->h2, asm12, 512, (d35 & 0x02) ? 2 : 1);   // C0/0AB4-0AC0
  if (bg3 && sd->has_l3 && asm3)
    vram_tilemap_layer(vram, 0x7000, sd->l3t, sd->w3, sd->h3, asm3, 256, (d35 & 0x04) ? 2 : 1);   // C0/0AE1-0AED
  return 1;
}
