// romview.c - see romview.h.
#include "romview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { long start, len; const uint8_t* bytes; } RomTable;
static RomTable g_tab[64];
static int g_ntab;

static int tabCmp(const void* a, const void* b) {
  long x = ((const RomTable*)a)->start, y = ((const RomTable*)b)->start;
  return x < y ? -1 : x > y;
}

static int collect(const CtAssets* as, RomTable* out, int cap) {
  int n = 0;
  for (uint32_t k = 0; as && k < as->count && n < cap; k++) {
    const uint8_t* e = as->data + 12 + (size_t)k * 16;
    uint32_t type = (uint32_t)(e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24);
    if (type != CT_ASSET_TABLE) continue;
    uint32_t id = (uint32_t)(e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24);
    uint32_t off = (uint32_t)(e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24);
    uint32_t len = (uint32_t)(e[12] | e[13] << 8 | e[14] << 16 | (uint32_t)e[15] << 24);
    out[n].start = (long)id; out[n].len = (long)len; out[n].bytes = as->data + off; n++;
  }
  qsort(out, (size_t)n, sizeof *out, tabCmp);
  return n;
}

void CtRomViewUse(const CtAssets* as) { g_ntab = as ? collect(as, g_tab, 64) : 0; }

static const RomTable* find(long off) {
  int lo = 0, hi = g_ntab - 1;
  while (lo <= hi) {                                  // the last table starting at or before `off`
    int mid = (lo + hi) / 2;
    if (g_tab[mid].start <= off) lo = mid + 1; else hi = mid - 1;
  }
  for (int k = hi; k >= 0 && k > hi - 4; k--)         // (tables may overlap: C0's sine and trig share F900-F93F)
    if (off >= g_tab[k].start && off < g_tab[k].start + g_tab[k].len) return &g_tab[k];
  return NULL;
}

int CtRomViewHas(long off) { return find(off) != NULL; }

const uint8_t* CtRomPtr(const uint8_t* rom, long romLen, long off, long len) {
  const RomTable* t = g_ntab ? find(off) : NULL;
  if (t && off + len <= t->start + t->len) return t->bytes + (off - t->start);
  return (rom && off >= 0 && off + len <= romLen) ? rom + off : NULL;
}

uint8_t CtRomByte(const uint8_t* rom, long romLen, long off) {
  const RomTable* t = g_ntab ? find(off) : NULL;
  if (t) return t->bytes[off - t->start];
  return (rom && off >= 0 && off < romLen) ? rom[off] : 0;
}

long CtRomViewCompare(const uint8_t* rom, long romLen, const CtAssets* as) {
  RomTable t[64];
  int n = collect(as, t, 64);
  long bytes = 0;
  if (!n) { printf("tab-data: the container holds no table\n"); return -1; }
  for (int k = 0; k < n; k++) {
    if (t[k].start + t[k].len > romLen || memcmp(t[k].bytes, rom + t[k].start, (size_t)t[k].len)) {
      long i = 0;
      while (i < t[k].len && t[k].start + i < romLen && t[k].bytes[i] == rom[t[k].start + i]) i++;
      printf("tab-data: the table at %06lX (%ld bytes) differs at +%ld\n", t[k].start, t[k].len, i);
      return -1;
    }
    bytes += t[k].len;
  }
  return bytes;
}
