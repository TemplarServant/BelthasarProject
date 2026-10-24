// widext.c - ct_wide.dat, the Steam release's widescreen margins (widext.h; written by tools/steam_wide.py).
#include "widext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDEXT_LOCATIONS 0x200
#define WIDEXT_SET_BYTES (128 * 64)
#define WIDEXT_SHEET_BYTES (256 * 256)

static int g_loaded;
static CtWideExt g_ext[WIDEXT_LOCATIONS];
static uint8_t* g_file;                       // the whole file, kept: the chip sets and corners point into it
static const uint8_t* g_set[256];

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// ct_wide.dat (LE): "CTWD", u32 version 3, u32 locations, u32 sets, u32 sheets; per set u8 set + 128*64 pixels; per sheet
// 256*256 CGRAM indices; per location u16 location, u8 w, u8 h, 5*w*h u16, u8 slot set[6], u16 metatiles, per metatile
// u16 id + 4 x (u16 chip, u8 pal, u8 flags), u8 pages, per page u8 page + u16 sheet. (A version-2 file, four planes and no
// sheets, is refused: tools/steam_wide.py writes it again.)
static void widextLoad(void) {
  g_loaded = 1;
  const char* path = getenv("CT_WIDE_DAT");
  FILE* f = fopen(path && *path ? path : "ct_wide.dat", "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* d = len > 16 ? (uint8_t*)malloc((size_t)len) : NULL;
  if (!d || fread(d, 1, (size_t)len, f) != (size_t)len || memcmp(d, "CTWD", 4) || rd32(d + 4) != 3) {
    free(d); fclose(f); return;
  }
  fclose(f);
  g_file = d;
  uint32_t nloc = rd32(d + 8), nset = rd32(d + 12), nsheet = rd32(d + 16);
  long o = 20;
  for (uint32_t i = 0; i < nset && o + 1 + WIDEXT_SET_BYTES <= len; i++) { g_set[d[o]] = d + o + 1; o += 1 + WIDEXT_SET_BYTES; }
  const uint8_t* sheets = d + o;
  if (o + (long)nsheet * WIDEXT_SHEET_BYTES > len) return;
  o += (long)nsheet * WIDEXT_SHEET_BYTES;
  for (uint32_t i = 0; i < nloc && o + 4 <= len; i++) {
    int loc = d[o] | d[o + 1] << 8, w = d[o + 2], h = d[o + 3], n = 5 * w * h;
    o += 4;
    if (o + 2L * n + 8 > len) break;
    uint16_t* p = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)n);
    if (!p) break;
    for (int k = 0; k < n; k++) p[k] = (uint16_t)(d[o + 2 * k] | d[o + 2 * k + 1] << 8);
    o += 2L * n;
    CtWideExt e; memset(&e, 0, sizeof e);
    e.w = w; e.h = h; e.planes = p;
    for (int s = 0; s < 6; s++) e.slotChips[s] = d[o + s] != 0xFF ? g_set[d[o + s]] : NULL;
    o += 6;
    e.nMeta = d[o] | d[o + 1] << 8; o += 2;
    if (o + 18L * e.nMeta > len) { free(p); break; }
    uint16_t* ids = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)(e.nMeta ? e.nMeta : 1));
    uint8_t* corners = (uint8_t*)malloc((size_t)(e.nMeta ? e.nMeta : 1) * 16);
    if (!ids || !corners) { free(p); free(ids); free(corners); break; }
    for (int m = 0; m < e.nMeta; m++) { ids[m] = (uint16_t)(d[o] | d[o + 1] << 8); memcpy(corners + m * 16, d + o + 2, 16); o += 18; }
    e.metaIds = ids; e.metaCorners = corners;
    int np = o < len ? d[o++] : 0;
    for (int k = 0; k < np && o + 3 <= len; k++, o += 3) {
      int pg = d[o], s = d[o + 1] | d[o + 2] << 8;
      if (pg >= 1 && pg < 8 && (uint32_t)s < nsheet) e.pageSheet[pg] = sheets + (size_t)s * WIDEXT_SHEET_BYTES;
    }
    if (loc < WIDEXT_LOCATIONS && !g_ext[loc].planes) g_ext[loc] = e;
    else { free(p); free(ids); free(corners); }
  }
}

int CtWideExtAvailable(void) {
  if (!g_loaded) widextLoad();
  return g_file != NULL;
}
int CtWideExtGet(int location, CtWideExt* out) {
  if (!g_loaded) widextLoad();
  if (location < 0 || location >= WIDEXT_LOCATIONS || !g_ext[location].planes) return 0;
  *out = g_ext[location];
  return 1;
}
