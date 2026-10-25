// romview.c - see romview.h.
#include "romview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { long start, len; const uint8_t* bytes; } RomTable;
#define ROMVIEW_MAX 16384                              // (the battle's scripts alone are ~700 runs)
static RomTable g_tab[ROMVIEW_MAX];
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

void CtRomViewUse(const CtAssets* as) { g_ntab = as ? collect(as, g_tab, ROMVIEW_MAX) : 0; }

void CtRomViewPatch(int (*apply)(uint8_t* img, long len)) {
  const char* mp = getenv("CT_ROM_MEMPATCH");
  if (apply && g_ntab && mp && *mp) {
    long top = 0;
    for (int k = 0; k < g_ntab; k++) if (g_tab[k].start + g_tab[k].len > top) top = g_tab[k].start + g_tab[k].len;
    uint8_t* img = (uint8_t*)calloc((size_t)top, 1);
    if (!img) return;
    for (int k = 0; k < g_ntab; k++) memcpy(img + g_tab[k].start, g_tab[k].bytes, (size_t)g_tab[k].len);
    apply(img, top);
    for (int k = 0; k < g_ntab; k++) g_tab[k].bytes = img + g_tab[k].start;
  }
}

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

// CT_ROMVIEW_MISSLOG=<file> (`%d` = the process id): every read of a ROM image no table covered, once per 4 KB page and
// caller: `PAGE CALLER kind exe` (CALLER = the calling instruction relative to the executable; tools/rom_census.py
// --viewmiss names it through build/Belthasar.map). CT_ROM_CENSUS sees these reads inside CtRomByte; this says whose.
#ifdef _MSC_VER
#include <intrin.h>
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* m, char* p, unsigned long n);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
#define VIEW_CALLER() ((uintptr_t)_ReturnAddress() - (uintptr_t)GetModuleHandleA(NULL))
#define VIEW_PID() ((int)GetCurrentProcessId())
#else
#include <unistd.h>
#define VIEW_CALLER() ((uintptr_t)__builtin_return_address(0))
#define VIEW_PID() ((int)getpid())
#endif
static int g_missOn = -1;
static void viewMiss(long off, uintptr_t caller, const char* kind) {
  static FILE* f; static uint64_t seen[8192]; static int nseen; static char exe[64] = "?";
  if (g_missOn < 0) {
    const char* p = getenv("CT_ROMVIEW_MISSLOG"); char path[1024];
    g_missOn = 0;
    if (p && strstr(p, "%d")) { snprintf(path, sizeof path, p, VIEW_PID()); p = path; }
    if (p && *p && (f = fopen(p, "w")) != NULL) g_missOn = 1;
#ifdef _MSC_VER
    { char m[512] = ""; GetModuleFileNameA(NULL, m, sizeof m); const char* b = m;
      for (const char* q = m; *q; q++) if (*q == '\\' || *q == '/') b = q + 1;
      snprintf(exe, sizeof exe, "%s", b); }
#else
    { char m[512]; ssize_t n = readlink("/proc/self/exe", m, sizeof m - 1);
      if (n > 0) { m[n] = 0; const char* b = strrchr(m, '/'); snprintf(exe, sizeof exe, "%.63s", b ? b + 1 : m); } }
#endif
  }
  if (!g_missOn) return;
  uint64_t key = ((uint64_t)(off >> 12) << 40) ^ (uint64_t)caller;
  for (int i = 0; i < nseen; i++) if (seen[i] == key) return;
  if (nseen < 8192) seen[nseen++] = key;
  fprintf(f, "%06lX %llX %s %s\n", (unsigned long)(off & ~0xFFFL), (unsigned long long)caller, kind, exe);
  fflush(f);
}

void CtRomCopy(uint8_t* dst, const uint8_t* rom, long romLen, long off, long n) {
  for (long i = 0; i < n; i++) dst[i] = CtRomByte(rom, romLen, off + i);
}

const uint8_t* CtRomPtr(const uint8_t* rom, long romLen, long off, long len) {
  const RomTable* t = g_ntab ? find(off) : NULL;
  if (t && off + len <= t->start + t->len) return t->bytes + (off - t->start);
  if (g_missOn && g_ntab && rom && romLen > 0x100000) viewMiss(off, VIEW_CALLER(), "ptr");
  return (rom && off >= 0 && off + len <= romLen) ? rom + off : NULL;
}

uint8_t CtRomByte(const uint8_t* rom, long romLen, long off) {
  const RomTable* t = g_ntab ? find(off) : NULL;
  if (t) return t->bytes[off - t->start];
  if (g_missOn && g_ntab && rom && romLen > 0x100000) viewMiss(off, VIEW_CALLER(), "byte");
  return (rom && off >= 0 && off < romLen) ? rom[off] : 0;
}

long CtRomViewCompare(const uint8_t* rom, long romLen, const CtAssets* as) {
  static RomTable t[ROMVIEW_MAX];
  int n = collect(as, t, ROMVIEW_MAX);
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
