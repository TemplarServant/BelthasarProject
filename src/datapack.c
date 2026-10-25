// datapack.c -- see datapack.h. The pack's format is tools/gen/pack_tables.py's (its docstring).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "datapack.h"

enum { T_HEADER = 64, T_TABLE = 65, T_POOL = 66, T_DEFINES = 67 };
#define PACK_VERSION 1u

static char g_packPath[1024];

static uint32_t rd32(const uint8_t* d) {
  return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}
static uint64_t rd64(const uint8_t* d) { return (uint64_t)rd32(d) | ((uint64_t)rd32(d + 4) << 32); }

static int fail(char* err, size_t errLen, const char* fmt, const char* a, unsigned long long b) {
  if (err && errLen) snprintf(err, errLen, fmt, a, b);
  return 0;
}

typedef struct { const uint8_t* data; size_t len; uint32_t count; } Pack;

// The blob of entry (type, id), bounds-checked against the file; NULL if absent or out of the file.
static const uint8_t* entry(const Pack* p, uint32_t type, uint32_t id, uint32_t* len) {
  for (uint32_t i = 0; i < p->count; i++) {
    const uint8_t* e = p->data + 12 + (size_t)i * 16;
    if (rd32(e) != type || rd32(e + 4) != id) continue;
    uint32_t off = rd32(e + 8), n = rd32(e + 12);
    if ((uint64_t)off + n > p->len) return NULL;
    *len = n;
    return p->data + off;
  }
  return NULL;
}

int CtDataPackFileHasTables(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  uint8_t h[12];
  int ok = fread(h, 1, 12, f) == 12 && !memcmp(h, "CTAS", 4);
  uint32_t n = ok ? rd32(h + 8) : 0;
  int found = 0;
  for (uint32_t i = 0; ok && i < n && !found; i++) {
    uint8_t e[16];
    if (fread(e, 1, 16, f) != 16) break;
    found = rd32(e) == T_HEADER;
  }
  fclose(f);
  return found;
}

int CtDataPackLoad(const char* path, char* err, size_t errLen) {
  const uint16_t one = 1;
  if (*(const uint8_t*)&one != 1) return fail(err, errLen, "%sthis build is big-endian; packs are little-endian", "", 0);
  FILE* f = fopen(path, "rb");
  if (!f) return fail(err, errLen, "cannot open '%s'", path, 0);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  if (n < 12) { fclose(f); return fail(err, errLen, "'%s' is not a pack", path, 0); }
  uint8_t* d = (uint8_t*)malloc((size_t)n);     // kept for the program's life: the pool's strings live in it
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return fail(err, errLen, "cannot read '%s'", path, 0); }
  fclose(f);
  Pack p = { d, (size_t)n, rd32(d + 8) };
  if (memcmp(d, "CTAS", 4) || 12 + (uint64_t)p.count * 16 > p.len) { free(d); return fail(err, errLen, "'%s' is not a CTAS container", path, 0); }

  uint32_t hl = 0, pl = 0, dl = 0;
  const uint8_t* h = entry(&p, T_HEADER, 0, &hl);
  const uint8_t* pool = entry(&p, T_POOL, 0, &pl);
  const uint8_t* defs = entry(&p, T_DEFINES, 0, &dl);
  if (!h || hl < 32 || memcmp(h, "CTTB", 4)) { free(d); return fail(err, errLen, "'%s' holds no tables", path, 0); }
  const uint32_t hVer = rd32(h + 4), hPtr = rd32(h + 8), hTables = rd32(h + 16);   // (read before d is freed)
  if (hVer != PACK_VERSION) { free(d); return fail(err, errLen, "%spack format %llu, this build reads 1", "", hVer); }
  if (hPtr != sizeof(void*)) { free(d); return fail(err, errLen, "%spack made for %llu-byte pointers", "", hPtr); }
  if (rd32(h + 12) != 1) { free(d); return fail(err, errLen, "%spack is not little-endian", "", 0); }
  if (hTables != (uint32_t)ctTableDescCount)
    { free(d); return fail(err, errLen, "%spack holds %llu tables, this build has others", "", hTables); }
  if (rd64(h + 24) != ctTableSetHash)
    { free(d); return fail(err, errLen, "%sthe pack's tables are laid out for another version of the generators", "", 0); }
  if (!pool || (pl && pool[pl - 1] != 0)) { free(d); return fail(err, errLen, "%sbad string pool", "", 0); }

  // The #defines, each exactly once: one compiled in must be the ROM's; a run-time one takes the ROM's.
  if (!defs || dl < 4 || rd32(defs) != (uint32_t)ctTableDefineCount || rd32(h + 20) != (uint32_t)ctTableDefineCount)
    { free(d); return fail(err, errLen, "%sthe pack's #defines are not this build's", "", 0); }
  {
    unsigned char* seen = (unsigned char*)calloc((size_t)ctTableDefineCount + 1, 1);
    if (!seen) { free(d); return fail(err, errLen, "%sout of memory", "", 0); }
    const uint8_t* q = defs + 4; const uint8_t* end = defs + dl;
    for (int i = 0; i < ctTableDefineCount; i++) {
      if (q >= end || q + 1 + q[0] + 8 > end) { free(seen); free(d); return fail(err, errLen, "%sbad #define list", "", 0); }
      char name[256]; memcpy(name, q + 1, q[0]); name[q[0]] = 0;
      long long v = (long long)rd64(q + 1 + q[0]);
      q += 1 + q[0] + 8;
      int k = 0;
      while (k < ctTableDefineCount && strcmp(ctTableDefines[k].name, name)) k++;
      if (k == ctTableDefineCount) { free(seen); free(d); return fail(err, errLen, "#define %s is not in this build", name, 0); }
      if (seen[k]++) { free(seen); free(d); return fail(err, errLen, "#define %s is twice in the pack", name, 0); }
      const CtTableDefine* td = &ctTableDefines[k];
      if (!td->var) {
        if (td->value != v) { free(seen); free(d); return fail(err, errLen, "#define %s differs from this build (pack %lld)", name, (unsigned long long)v); }
      } else if (td->kind == CT_TBL_DEF_INT && v >= INT_MIN && v <= INT_MAX) {
        *(int*)td->var = (int)v;
      } else if (td->kind == CT_TBL_DEF_UINT && v >= 0 && v <= (long long)UINT_MAX) {
        *(unsigned*)td->var = (unsigned)v;
      } else {
        free(seen); free(d); return fail(err, errLen, "#define %s: the pack's %lld does not fit its type", name, (unsigned long long)v);
      }
    }
    free(seen);
  }

  // The strings the tables point to, in their own block: the file itself is freed after the load (it may be the whole
  // ct_assets.dat when the tables ride inside it).
  char* keep = (char*)malloc(pl ? pl : 1);
  if (!keep) { free(d); return fail(err, errLen, "%sout of memory", "", 0); }
  if (pl) memcpy(keep, pool, pl);

  // The tables: one pack entry per registry entry, by name.
  for (int t = 0; t < ctTableDescCount; t++) {
    const CtTableDesc* td = &ctTableDescs[t];
    const uint8_t* b = NULL; uint32_t bl = 0;
    size_t nl = strlen(td->name);
    for (uint32_t id = 0; id < (uint32_t)ctTableDescCount && !b; id++) {
      uint32_t l = 0; const uint8_t* c = entry(&p, T_TABLE, id, &l);
      if (c && l >= 1 && c[0] == nl && l >= 1 + nl && !memcmp(c + 1, td->name, nl)) { b = c; bl = l; }
    }
    if (!b) { free(d); free(keep); return fail(err, errLen, "table %s is not in the pack", td->name, 0); }
    size_t o = (1 + nl + 3) & ~(size_t)3;
    if (o + 16 > bl) { free(d); free(keep); return fail(err, errLen, "table %s: truncated", td->name, 0); }
    uint32_t size = rd32(b + o), nrel = rd32(b + o + 12);
    uint64_t layout = rd64(b + o + 4);
    if (size != td->size) { free(d); free(keep); return fail(err, errLen, "table %s: the pack's is %llu bytes", td->name, size); }
    if (layout != td->layout) { free(d); free(keep); return fail(err, errLen, "table %s: another layout", td->name, 0); }
    const uint8_t* rel = b + o + 16;
    const uint8_t* src = rel + (size_t)nrel * 4;
    if ((uint64_t)(src - b) + size != bl) { free(d); free(keep); return fail(err, errLen, "table %s: truncated", td->name, 0); }
    // the relocations are exactly this build's pointer members, in order: every element's, NULL ones included
    uint32_t nElem = td->elemSize ? (uint32_t)(td->size / td->elemSize) : 0;
    if (nrel != nElem * (uint32_t)td->nPtr) { free(d); free(keep); return fail(err, errLen, "table %s: its pointers are not this build's", td->name, 0); }
    memcpy(td->data, src, size);
    for (uint32_t r = 0; r < nrel; r++) {
      uint32_t at = rd32(rel + (size_t)r * 4);
      if (at != (r / td->nPtr) * td->elemSize + td->ptrOffs[r % td->nPtr])
        { free(d); free(keep); return fail(err, errLen, "table %s: its pointers are not this build's", td->name, 0); }
      uint64_t so = rd64(src + at);
      const char* s = NULL;
      if (so != UINT64_MAX) {
        if (so >= pl) { free(d); free(keep); return fail(err, errLen, "table %s: string out of the pool", td->name, 0); }
        s = keep + so;
      }
      memcpy((uint8_t*)td->data + at, &s, sizeof s);
    }
  }
  snprintf(g_packPath, sizeof g_packPath, "%s", path);
  free(d);
  return 1;                                        // keep stays allocated: the tables point into it
}

int CtDataPackLoadForArgs(int argc, char** argv, char* err, size_t errLen) {
  const char* explicitPath = NULL; const char* assets = NULL;
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--tables")) explicitPath = argv[i + 1];
    else if (!strcmp(argv[i], "--assets") || !strcmp(argv[i], "--datonly")) assets = argv[i + 1];
  }
  if (!explicitPath) explicitPath = getenv("CT_TABLES");
  if (explicitPath && *explicitPath) return CtDataPackLoad(explicitPath, err, errLen);
  char sib[1024];
  if (assets) {
    if (CtDataPackFileHasTables(assets)) return CtDataPackLoad(assets, err, errLen);
    const char* s1 = strrchr(assets, '/'); const char* s2 = strrchr(assets, '\\');
    const char* sep = s1 > s2 ? s1 : s2;
    size_t dir = sep ? (size_t)(sep - assets) + 1 : 0;
    if (dir + 14 < sizeof sib) {
      memcpy(sib, assets, dir); memcpy(sib + dir, "ct_tables.dat", 14);
      FILE* f = fopen(sib, "rb");
      if (f) { fclose(f); return CtDataPackLoad(sib, err, errLen); }
    }
  }
  FILE* f = fopen("ct_tables.dat", "rb");
  if (f) { fclose(f); return CtDataPackLoad("ct_tables.dat", err, errLen); }
  return fail(err, errLen, "%sno table pack (ct_tables.dat beside the assets, or --tables <path>): run the setup "
              "on your ROM first (docs/ROM.md)", "", 0);
}

const char* CtDataPackPath(void) { return g_packPath[0] ? g_packPath : NULL; }
