// rom_verify.c -- see rom_verify.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rom_verify.h"
#include "rom_census.h"

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1Block(uint32_t h[5], const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void CtSha1Hex(const uint8_t* data, long n, char out[41]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  long i = 0;
  for (; i + 64 <= n; i += 64) sha1Block(h, data + i);
  uint8_t tail[128] = {0};
  long r = n - i;
  memcpy(tail, data + i, (size_t)r);
  tail[r] = 0x80;
  int blocks = r + 9 <= 64 ? 1 : 2;
  uint64_t bits = (uint64_t)n * 8;
  for (int j = 0; j < 8; j++) tail[blocks * 64 - 1 - j] = (uint8_t)(bits >> (8 * j));
  for (int j = 0; j < blocks; j++) sha1Block(h, tail + 64 * j);
  for (int j = 0; j < 5; j++) sprintf(out + 8 * j, "%08x", h[j]);
}

const uint8_t* CtRomVerify(const uint8_t* data, long* len, const char* what) {
  if (!data) return NULL;
  if (*len % 1024 == 512) { data += 512; *len -= 512; }          // a copier header
  char got[41] = "";
  if (*len == CT_ROM_SIZE) CtSha1Hex(data, *len, got);
  if (strcmp(got, CT_ROM_SHA1) != 0) {
    fprintf(stderr, "[rom] %s is not the supported ROM: Chrono Trigger (USA), %ld bytes, SHA-1 %s\n"
                    "      (got %ld bytes%s%s). Dump your own cartridge; see docs/ROM.md.\n",
            what ? what : "the ROM", CT_ROM_SIZE, CT_ROM_SHA1, *len, *got ? ", SHA-1 " : "", got);
    return NULL;
  }
  return data;
}

static const uint8_t* s_rom; static long s_romLen; static char s_romHint[520];
void CtRomSetPath(const char* path) { snprintf(s_romHint, sizeof s_romHint, "%s", path ? path : ""); }

static const uint8_t* romTry(const char* path, long* len) {
  if (!path || !*path) return NULL;
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)n);
  if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
  fclose(f);
  const uint8_t* img = CtRomVerify(buf, &n, path);
  if (!img) { free(buf); return NULL; }
  *len = n;
  return CtRomCensusWrap(img, n, "rom_verify");                 // kept for the process' lifetime (CT_ROM_CENSUS: guarded)
}

const uint8_t* CtRomImage(long* len) {
  if (!s_rom) {
    const char* tries[] = { getenv("CT_ROM"), s_romHint, "Chrono_Trigger_USA_SNES.sfc" };
    for (int i = 0; i < 3 && !s_rom; i++) s_rom = romTry(tries[i], &s_romLen);
    if (!s_rom) {
      fprintf(stderr, "[rom] no ROM: put your dump of Chrono Trigger (USA) at Chrono_Trigger_USA_SNES.sfc or name it "
                      "with CT_ROM=<path> (docs/ROM.md).\n");
      exit(1);
    }
  }
  if (len) *len = s_romLen;
  return s_rom;
}

int CtRomApplyMemPatch(uint8_t* img, long len) {
  const char* p = getenv("CT_ROM_MEMPATCH");
  if (!p || !*p || !img) return 0;
  int n = 0;
  while (*p) {
    char* e; long off = strtol(p, &e, 16);
    if (e == p) break;
    p = e;
    int orMode = (*p == '|');
    if (orMode) p++;
    if (*p != '=') break;
    p++;
    while (*p && *p != ',') {
      int hi = p[0], lo = p[1];
      if (!lo) break;
      char hx[3] = { (char)hi, (char)lo, 0 };
      uint8_t v = (uint8_t)strtol(hx, NULL, 16);
      if (off >= 0 && off < len) { img[off] = orMode ? (uint8_t)(img[off] | v) : v; n++; }
      off++; p += 2;
    }
    if (*p == ',') p++;
  }
  if (n) fprintf(stderr, "[rom] CT_ROM_MEMPATCH: %d byte(s) instrumented in memory\n", n);
  return n;
}
