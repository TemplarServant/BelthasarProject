// tables_proof.c -- the table pack's byte-for-byte proof (tools/gen/tables_proof.py drives it).
//
//   ct_tables_proof <pack>
//
// Fills the data-free storage (src/tables/tables_storage.c) from the pack exactly as a data-free Belthasar does
// (src/datapack.c), then compares every table with the same table compiled from the real header
// (src/tables/tables_real.c): every byte the same, padding included, except the pointer members, whose strings must be
// equal (both NULL or the same text); and every #define with the real header's value (a run-time one as the loader
// filled its variable). Prints one line per difference and a summary; exit 0 only if all are identical.
#include <stdio.h>
#include <string.h>
#include "datapack.h"

typedef struct { const char* name; const void* data; size_t size; } CtTableReal;
extern const CtTableReal ctTablesReal[];
extern const int ctTablesRealCount;
typedef struct { const char* name; long long value; } CtDefineReal;
extern const CtDefineReal ctDefinesReal[];
extern const int ctDefinesRealCount;

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: ct_tables_proof <pack>\n"); return 2; }
  char err[512];
  if (!CtDataPackLoad(argv[1], err, sizeof err)) { printf("tables-proof: FAIL: the pack did not load: %s\n", err); return 1; }
  if (ctTablesRealCount != ctTableDescCount) {
    printf("tables-proof: FAIL: %d tables compiled from the headers, %d in the data-free build\n", ctTablesRealCount, ctTableDescCount);
    return 1;
  }
  int bad = 0; size_t bytes = 0, nptr = 0, nonzero = 0;
  for (int t = 0; t < ctTableDescCount; t++) {
    const CtTableDesc* d = &ctTableDescs[t];
    const CtTableReal* r = NULL;
    for (int k = 0; k < ctTablesRealCount && !r; k++) if (!strcmp(ctTablesReal[k].name, d->name)) r = &ctTablesReal[k];
    if (!r) { printf("tables-proof: %s: not compiled from the headers\n", d->name); bad++; continue; }
    if (r->size != d->size) { printf("tables-proof: %s: %zu bytes from the header, %zu loaded\n", d->name, r->size, d->size); bad++; continue; }
    const unsigned char* a = (const unsigned char*)r->data; const unsigned char* b = (const unsigned char*)d->data;
    size_t nElem = d->elemSize ? d->size / d->elemSize : 0, firstDiff = (size_t)-1, diffs = 0;
    for (size_t i = 0; i < d->size; i++) {
      size_t in = d->elemSize ? i % d->elemSize : i;
      int isPtr = 0;
      for (int p = 0; p < d->nPtr; p++) if (in >= d->ptrOffs[p] && in < d->ptrOffs[p] + sizeof(void*)) isPtr = 1;
      if (isPtr) continue;
      nonzero += b[i] != 0;
      if (a[i] != b[i]) { if (firstDiff == (size_t)-1) firstDiff = i; diffs++; }
    }
    for (size_t e = 0; e < nElem; e++)
      for (int p = 0; p < d->nPtr; p++) {
        const char *sa, *sb;
        memcpy(&sa, a + e * d->elemSize + d->ptrOffs[p], sizeof sa);
        memcpy(&sb, b + e * d->elemSize + d->ptrOffs[p], sizeof sb);
        nptr++;
        if ((sa == NULL) != (sb == NULL) || (sa && strcmp(sa, sb))) {
          printf("tables-proof: %s: element %zu pointer +%u: \"%s\" vs \"%s\"\n", d->name, e, d->ptrOffs[p],
                 sa ? sa : "(null)", sb ? sb : "(null)");
          bad++;
        }
      }
    if (diffs) { printf("tables-proof: %s: %zu bytes differ (first at +%zu)\n", d->name, diffs, firstDiff); bad++; }
    bytes += d->size;
  }
  // the #defines: a run-time one as the loader filled it, a compiled-in one as compiled, against the real headers'
  int nRun = 0, badDef = 0;
  if (ctDefinesRealCount != ctTableDefineCount) {
    printf("tables-proof: %d #defines in the headers, %d in the data-free build\n", ctDefinesRealCount, ctTableDefineCount);
    badDef++;
  }
  for (int i = 0; i < ctTableDefineCount; i++) {
    const CtTableDefine* d = &ctTableDefines[i];
    const CtDefineReal* r = NULL;
    for (int k = 0; k < ctDefinesRealCount && !r; k++) if (!strcmp(ctDefinesReal[k].name, d->name)) r = &ctDefinesReal[k];
    long long v = !d->var ? d->value : d->kind == CT_TBL_DEF_INT ? (long long)*(const int*)d->var
                                                                    : (long long)*(const unsigned*)d->var;
    nRun += d->var != NULL;
    if (!r) { printf("tables-proof: #define %s: not in the headers\n", d->name); badDef++; }
    else if (r->value != v)
      { printf("tables-proof: #define %s: %lld in the headers, %lld %s\n", d->name, r->value, v, d->var ? "loaded" : "compiled in"); badDef++; }
  }
  printf("tables-proof: %d #defines (%d run-time, %d compiled in): %s\n", ctTableDefineCount, nRun, ctTableDefineCount - nRun,
         badDef ? "DIFFER" : "IDENTICAL");
  bad += badDef;
  printf("tables-proof: %d tables, %zu bytes (%zu non-zero), %zu pointers: %s\n", ctTableDescCount, bytes, nonzero, nptr,
         bad ? "DIFFER" : "IDENTICAL");
  return bad ? 1 : 0;
}
