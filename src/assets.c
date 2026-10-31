// assets.c -- see assets.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"

static uint32_t rd32(const uint8_t* d) {
  return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

int CtAssetsOpen(CtAssets* a, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  if (n < 12) { fclose(f); return 0; }
  uint8_t* d = (uint8_t*)malloc((size_t)n);
  if (fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return 0; }
  fclose(f);
  if (memcmp(d, "CTAS", 4) != 0) { free(d); return 0; }
  // A cut-short or damaged container is refused here, not read past its end by CtAssetGet: the directory and every
  // blob it names must lie inside the file.
  uint32_t count = rd32(d + 8);
  if ((uint64_t)12 + (uint64_t)count * 16 > (uint64_t)n) { free(d); return 0; }
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* e = d + 12 + (size_t)i * 16;
    if ((uint64_t)rd32(e + 8) + rd32(e + 12) > (uint64_t)n) { free(d); return 0; }
  }
  a->data = d;
  a->len = (int)n;
  a->version = rd32(d + 4);
  a->count = count;
  return 1;
}

const uint8_t* CtAssetGet(const CtAssets* a, uint32_t type, uint32_t id, uint32_t* outLen) {
  const uint8_t* dir = a->data + 12;
  for (uint32_t i = 0; i < a->count; i++) {
    const uint8_t* e = dir + (size_t)i * 16;
    if (rd32(e) == type && rd32(e + 4) == id) {
      uint32_t off = rd32(e + 8), len = rd32(e + 12);
      if (outLen) *outLen = len;
      return a->data + off;
    }
  }
  return NULL;
}

const char* CtAssetString(const CtAssets* a, uint32_t type, uint32_t id, int index, int* outLen) {
  uint32_t blen;
  const uint8_t* b = CtAssetGet(a, type, id, &blen);
  if (!b || blen < 2) return NULL;
  const uint8_t* end = b + blen;
  int count = b[0] | (b[1] << 8);
  if (index < 0 || index >= count) return NULL;
  const uint8_t* p = b + 2;
  for (int i = 0; i < index; i++) {               // skip earlier strings, never past the blob
    if (p + 2 > end) return NULL;
    p += 2 + (p[0] | (p[1] << 8));
  }
  if (p + 2 > end) return NULL;
  int l = p[0] | (p[1] << 8);
  if (p + 2 + l > end) return NULL;
  if (outLen) *outLen = l;
  return (const char*)(p + 2);
}

void CtAssetsClose(CtAssets* a) {
  free((void*)a->data);
  a->data = NULL;
}

int CtAssetsRomFree(const CtAssets* a) {
  for (uint32_t k = 0; a && a->data && k < a->count; k++) {
    const uint8_t* e = a->data + 12 + (size_t)k * 16;
    if ((uint32_t)(e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24) == CT_ASSET_TABLE) return 1;
  }
  return 0;
}
int CtAssetsFileRomFree(const char* path) {
  CtAssets a;
  if (!CtAssetsOpen(&a, path)) return 0;
  int r = CtAssetsRomFree(&a);
  CtAssetsClose(&a);
  return r;
}
