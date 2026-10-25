// sound/timing.c -- the cost table's asset, read (timing.h). No SPC700: the game links this, not timing_build.c's core.
#include "sound/timing.h"

#include <string.h>

#define SND_TIMING_MAGIC 0x54444E53u   /* "SNDT" */

static uint32_t rd32(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

static void insFrom(SndIns* in, const uint8_t* p) {
  in->kind = p[0]; in->len = p[1]; in->cyc = p[2]; in->cycTaken = p[3]; in->target = rd16(p + 4); in->nio = p[6];
  memcpy(in->ioIdx, p + 7, 4); memcpy(in->ioReg, p + 11, 4); memcpy(in->ioWrite, p + 15, 4);
  in->nw = p[19]; memcpy(in->wIdx, p + 20, 4);
}

int SndTimingRead(SndTiming* t, const uint8_t* in, long len) {
  memset(t, 0, sizeof *t);
  if (!in || len < 12 || rd32(in) != SND_TIMING_MAGIC || rd32(in + 4) != 1) return 0;
  long blen = rd16(in + 10), map = (blen + 7) / 8, o = 12 + map;
  if (len < o + 4) return 0;
  uint32_t n = rd32(in + o); o += 4;
  if (len != o + (long)n * 26) return 0;
  for (uint32_t i = 0; i < n; i++, o += 26) insFrom(&t->ins[rd16(in + o)], in + o + 2);
  return 1;
}

int SndTimingIsCode(const uint8_t* in, long len, uint16_t adr) {
  if (!in || len < 12 || rd32(in) != SND_TIMING_MAGIC) return 0;
  uint16_t blo = rd16(in + 8), blen = rd16(in + 10);
  unsigned k = (uint16_t)(adr - blo);
  if (k >= blen || 12 + (long)(blen + 7) / 8 > len) return 0;
  return (in[12 + k / 8] >> (k & 7)) & 1;
}
