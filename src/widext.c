// widext.c - ct_wide.dat, the Steam release's widescreen margins (widext.h; written by tools/steam_wide.py).
#include "widext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDEXT_LOCATIONS 0x200

static int g_loaded;
static CtWideExt g_ext[WIDEXT_LOCATIONS];

// ct_wide.dat: "CTWD", u32 version 1, u32 count, then per location u16 location, u8 w, u8 h, 4*w*h u16 (all LE).
static void widextLoad(void) {
  g_loaded = 1;
  const char* path = getenv("CT_WIDE_DAT");
  FILE* f = fopen(path && *path ? path : "ct_wide.dat", "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* d = len > 12 ? (uint8_t*)malloc((size_t)len) : NULL;
  if (!d || fread(d, 1, (size_t)len, f) != (size_t)len || memcmp(d, "CTWD", 4) || (d[4] | d[5] << 8) != 1) {
    free(d); fclose(f); return;
  }
  fclose(f);
  int count = d[8] | d[9] << 8;
  long o = 12;
  for (int i = 0; i < count && o + 4 <= len; i++) {
    int loc = d[o] | d[o + 1] << 8, w = d[o + 2], h = d[o + 3], n = 4 * w * h;
    o += 4;
    if (o + 2L * n > len) break;
    uint16_t* p = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)n);
    if (!p) break;
    for (int k = 0; k < n; k++) p[k] = (uint16_t)(d[o + 2 * k] | d[o + 2 * k + 1] << 8);
    o += 2L * n;
    if (loc < WIDEXT_LOCATIONS && !g_ext[loc].planes) { g_ext[loc].w = w; g_ext[loc].h = h; g_ext[loc].planes = p; }
    else free(p);
  }
  free(d);
}

int CtWideExtGet(int location, CtWideExt* out) {
  if (!g_loaded) widextLoad();
  if (location < 0 || location >= WIDEXT_LOCATIONS || !g_ext[location].planes) return 0;
  *out = g_ext[location];
  return 1;
}
