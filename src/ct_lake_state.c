// ct_lake_state.c -- CTLS wrap/unwrap for the raw LakeSnes handleState stream.
#include "ct_lake_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_crcTable[256];
static int s_crcReady = 0;

static void crcInit(void) {
  if (s_crcReady) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    s_crcTable[i] = c;
  }
  s_crcReady = 1;
}

uint32_t CtLakeCrc32(const uint8_t* data, int len) {
  crcInit();
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < len; i++) c = s_crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static int isWrapped(const uint8_t* file, int fileLen) {
  return fileLen >= CT_LAKE_HDR_SIZE
      && file[0] == CT_LAKE_MAGIC0 && file[1] == CT_LAKE_MAGIC1
      && file[2] == CT_LAKE_MAGIC2 && file[3] == CT_LAKE_MAGIC3;
}

static int looksLegacy(const uint8_t* file, int fileLen) {
  if (fileLen < CT_LAKE_MIN_PAYLOAD) return 0;
  /* Reject gzip / snes9x / CTFS so we never treat them as raw LakeSnes. */
  if (file[0] == 0x1f && file[1] == 0x8b) return 0;
  if (fileLen >= 9 && memcmp(file, "#!s9xsnp:", 9) == 0) return 0;
  if (fileLen >= 4 && memcmp(file, "CTFS", 4) == 0) return 0;
  if (fileLen >= 4 && memcmp(file, "LSSF", 4) == 0) return 0; /* upstream LakeSnes LSSF */
  for (int i = 0; i < 11; i++) {
    if (file[i] != 0 && file[i] != 1) return 0;
  }
  return 1;
}

static uint32_t rd32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int CtLakeStateLoadBytes(const uint8_t* file, int fileLen,
                         uint8_t** out, int* outLen, int* fmtOut) {
  if (!file || fileLen <= 0 || !out || !outLen) {
    fprintf(stderr, "ct_lake_state: load: bad args\n");
    return 1;
  }
  *out = NULL;
  *outLen = 0;
  if (fmtOut) *fmtOut = CT_LAKE_FMT_LEGACY;

  if (isWrapped(file, fileLen)) {
    uint32_t ver = rd32le(file + 4);
    uint32_t plen = rd32le(file + 8);
    uint32_t want = rd32le(file + 12);
    if (ver != CT_LAKE_VERSION) {
      fprintf(stderr, "ct_lake_state: CTLS version %u unsupported (need %u)\n",
              (unsigned)ver, (unsigned)CT_LAKE_VERSION);
      return 2;
    }
    if (plen < (uint32_t)CT_LAKE_MIN_PAYLOAD) {
      fprintf(stderr, "ct_lake_state: CTLS payload too small (%u < %d)\n",
              (unsigned)plen, CT_LAKE_MIN_PAYLOAD);
      return 3;
    }
    if ((int)(CT_LAKE_HDR_SIZE + plen) != fileLen &&
        (uint32_t)fileLen < CT_LAKE_HDR_SIZE + plen) {
      fprintf(stderr, "ct_lake_state: CTLS truncated (file %d B, header says payload %u B)\n",
              fileLen, (unsigned)plen);
      return 4;
    }
    if ((uint32_t)fileLen != CT_LAKE_HDR_SIZE + plen) {
      fprintf(stderr, "ct_lake_state: CTLS length mismatch (file %d B, expected header+payload %u B)\n",
              fileLen, (unsigned)(CT_LAKE_HDR_SIZE + plen));
      return 5;
    }
    const uint8_t* payload = file + CT_LAKE_HDR_SIZE;
    uint32_t got = CtLakeCrc32(payload, (int)plen);
    if (got != want) {
      fprintf(stderr, "ct_lake_state: CTLS CRC mismatch (got %08X, header %08X) — refusing silent garbage\n",
              (unsigned)got, (unsigned)want);
      return 6;
    }
    uint8_t* buf = (uint8_t*)malloc(plen);
    if (!buf) { fprintf(stderr, "ct_lake_state: OOM\n"); return 7; }
    memcpy(buf, payload, plen);
    *out = buf;
    *outLen = (int)plen;
    if (fmtOut) *fmtOut = CT_LAKE_FMT_WRAPPED;
    return 0;
  }

  if (looksLegacy(file, fileLen)) {
    uint8_t* buf = (uint8_t*)malloc((size_t)fileLen);
    if (!buf) { fprintf(stderr, "ct_lake_state: OOM\n"); return 7; }
    memcpy(buf, file, (size_t)fileLen);
    *out = buf;
    *outLen = fileLen;
    if (fmtOut) *fmtOut = CT_LAKE_FMT_LEGACY;
    return 0;
  }

  fprintf(stderr,
          "ct_lake_state: not a CTLS or legacy LakeSnes state (size=%d head=%02X%02X%02X%02X) — refusing load\n",
          fileLen,
          fileLen > 0 ? file[0] : 0, fileLen > 1 ? file[1] : 0,
          fileLen > 2 ? file[2] : 0, fileLen > 3 ? file[3] : 0);
  return 8;
}

int CtLakeStateLoadPath(const char* path, uint8_t** out, int* outLen, int* fmtOut) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "ct_lake_state: cannot open '%s'\n", path);
    return 10;
  }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 11; }
  long size = ftell(f);
  if (size <= 0) {
    fprintf(stderr, "ct_lake_state: empty or unreadable '%s'\n", path);
    fclose(f);
    return 12;
  }
  rewind(f);
  uint8_t* file = (uint8_t*)malloc((size_t)size);
  if (!file) { fclose(f); fprintf(stderr, "ct_lake_state: OOM\n"); return 7; }
  if (fread(file, 1, (size_t)size, f) != (size_t)size) {
    fprintf(stderr, "ct_lake_state: short read '%s'\n", path);
    free(file); fclose(f);
    return 13;
  }
  fclose(f);
  int rc = CtLakeStateLoadBytes(file, (int)size, out, outLen, fmtOut);
  free(file);
  return rc;
}

int CtLakeStateWrap(const uint8_t* payload, int payloadLen,
                    uint8_t** outFile, int* outFileLen) {
  if (!payload || payloadLen < CT_LAKE_MIN_PAYLOAD || !outFile || !outFileLen) {
    fprintf(stderr, "ct_lake_state: wrap: bad args (payloadLen=%d)\n", payloadLen);
    return 1;
  }
  int total = CT_LAKE_HDR_SIZE + payloadLen;
  uint8_t* buf = (uint8_t*)malloc((size_t)total);
  if (!buf) { fprintf(stderr, "ct_lake_state: OOM\n"); return 7; }
  buf[0] = CT_LAKE_MAGIC0; buf[1] = CT_LAKE_MAGIC1;
  buf[2] = CT_LAKE_MAGIC2; buf[3] = CT_LAKE_MAGIC3;
  wr32le(buf + 4, CT_LAKE_VERSION);
  wr32le(buf + 8, (uint32_t)payloadLen);
  wr32le(buf + 12, CtLakeCrc32(payload, payloadLen));
  memcpy(buf + CT_LAKE_HDR_SIZE, payload, (size_t)payloadLen);
  *outFile = buf;
  *outFileLen = total;
  return 0;
}

int CtLakeStateSavePath(const char* path, const uint8_t* payload, int payloadLen) {
  uint8_t* file = NULL;
  int fileLen = 0;
  if (CtLakeStateWrap(payload, payloadLen, &file, &fileLen) != 0) return 1;
  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "ct_lake_state: cannot write '%s'\n", path);
    free(file);
    return 2;
  }
  size_t n = fwrite(file, 1, (size_t)fileLen, f);
  fclose(f);
  free(file);
  if (n != (size_t)fileLen) {
    fprintf(stderr, "ct_lake_state: short write '%s'\n", path);
    return 3;
  }
  return 0;
}
