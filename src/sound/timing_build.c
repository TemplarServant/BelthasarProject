// ============================================================================
// sound/timing_build.c -- SndTimingBuild (timing.h): the per-address cost table of the driver's code, measured on the
// vendored SPC700. ct.exe and the tools only (it links snes/spc.c); the game reads the table from the asset container.
//
// Each address is taken as an instruction and run alone: once over the image itself with A = X = Y = 0 (the accesses:
// which cycle touches which register, which cycles write), then 64 times over random registers, flags and memory (the
// instruction's own bytes kept) to see a conditional branch both ways. A cycle is one call of the core's bus (a read, a
// write or an idle), as apu.c clocks it.
// ============================================================================
#include "sound/timing.h"

#include <stdlib.h>
#include <string.h>

#include "spc.h"

static const uint8_t kLen[256] = {
  1,1,2,3,2,3,1,2,2,3,3,2,3,1,3,1, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,3,3, 1,1,2,3,2,3,1,2,2,3,3,2,3,1,3,2, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,2,3,
  1,1,2,3,2,3,1,2,2,3,3,2,3,1,3,2, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,3,3, 1,1,2,3,2,3,1,2,2,3,3,2,3,1,3,1, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,2,1,
  1,1,2,3,2,3,1,2,2,3,3,2,3,2,1,3, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,1,1, 1,1,2,3,2,3,1,2,2,3,3,2,3,2,1,1, 2,1,2,3,2,3,3,2,3,1,2,2,1,1,1,1,
  1,1,2,3,2,3,1,2,2,3,3,2,3,2,1,1, 2,1,2,3,2,3,3,2,2,2,2,2,1,1,3,1, 1,1,2,3,2,3,1,2,2,3,3,2,3,1,1,1, 2,1,2,3,2,3,3,2,2,2,3,2,1,1,2,1,
};

typedef struct {
  const uint8_t* image;
  uint16_t pc, len;
  int random;
  uint32_t seed;
  int n;
  uint16_t adr[64];
  uint8_t  w[64];       // 0 read, 1 write, 2 idle
} Box;

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static void rec(Box* b, uint16_t a, uint8_t w) { if (b->n < 64) { b->adr[b->n] = a; b->w[b->n] = w; } b->n++; }
static uint8_t bRead(void* m, uint16_t a) {
  Box* b = (Box*)m;
  rec(b, a, 0);
  if (!b->random || (uint16_t)(a - b->pc) < b->len) return b->image[a];
  return (uint8_t)hash32(b->seed * 0x10001u + a);
}
static void bWrite(void* m, uint16_t a, uint8_t v) { (void)v; rec((Box*)m, a, 1); }
static void bIdle(void* m, bool waiting) { (void)waiting; rec((Box*)m, 0, 2); }

static int kindOf(uint8_t op) {
  if ((op & 0x1F) == 0x10 || (op & 0x0F) == 0x03 || op == 0x2E || op == 0xDE || op == 0x6E || op == 0xFE) return SND_INS_COND;
  if (op == 0x2F || op == 0x5F || op == 0x3F || op == 0x4F) return SND_INS_JUMP;
  if (op == 0x6F || op == 0x7F || op == 0x1F || op == 0x0F || (op & 0x0F) == 0x01) return SND_INS_RETURN;
  return SND_INS_PLAIN;
}

int SndTimingBuild(SndTiming* t, const uint8_t* image, uint16_t lo, uint16_t hi) {
  Box b;
  memset(&b, 0, sizeof b);
  b.image = image;
  Spc* spc = spc_init(&b, bRead, bWrite, bIdle);
  if (!spc) return 0;
  for (unsigned pc = lo; pc != hi; pc = (pc + 1) & 0xFFFF) {
    SndIns* in = &t->ins[pc];
    memset(in, 0, sizeof *in);
    uint8_t op = image[pc];
    in->kind = (uint8_t)kindOf(op);
    in->len = kLen[op];
    b.pc = (uint16_t)pc; b.len = in->len;
    uint16_t next = (uint16_t)(pc + in->len);
    if (in->kind == SND_INS_COND) in->target = (uint16_t)(next + (int8_t)image[(pc + in->len - 1) & 0xFFFF]);
    else if (op == 0x2F) in->target = (uint16_t)(next + (int8_t)image[(pc + 1) & 0xFFFF]);
    else if (op == 0x5F || op == 0x3F) in->target = (uint16_t)(image[(pc + 1) & 0xFFFF] | image[(pc + 2) & 0xFFFF] << 8);
    else if (op == 0x4F) in->target = (uint16_t)(0xFF00 | image[(pc + 1) & 0xFFFF]);
    // the accesses: over the image, A = X = Y = 0
    b.random = 0; b.n = 0;
    spc_reset(spc, false);
    spc->resetWanted = false; spc->stopped = false;
    spc->a = spc->x = spc->y = 0; spc->sp = 0xEF; spc->pc = (uint16_t)pc;
    spc->c = spc->z = spc->v = spc->n = spc->i = spc->h = spc->p = spc->b = false;
    spc_runOpcode(spc);
    int nt = -1, tk = -1;
    if (spc->pc == next) nt = b.n; else tk = b.n;
    for (int k = 0; k < b.n && k < 64; k++) {
      if (b.w[k] == 1 && in->nw < 4) in->wIdx[in->nw++] = (uint8_t)k;
      if (b.w[k] != 2 && b.adr[k] >= 0xF0 && b.adr[k] <= 0xFF && in->nio < 4 && k > 0) {
        in->ioIdx[in->nio] = (uint8_t)k; in->ioReg[in->nio] = (uint8_t)b.adr[k]; in->ioWrite[in->nio] = b.w[k]; in->nio++;
      }
    }
    // a conditional branch both ways: random registers, flags and memory
    if (in->kind == SND_INS_COND) {
      for (uint32_t s = 1; s <= 64 && (nt < 0 || tk < 0); s++) {
        b.random = 1; b.seed = s * 2654435761u + pc; b.n = 0;
        uint32_t r = hash32(b.seed);
        spc->a = (uint8_t)r; spc->x = (uint8_t)(r >> 8); spc->y = (uint8_t)(r >> 16); spc->sp = (uint8_t)(r >> 24);
        uint32_t f = hash32(r);
        spc->c = f & 1; spc->z = f & 2; spc->v = f & 4; spc->n = f & 8; spc->h = f & 16; spc->p = 0;
        spc->pc = (uint16_t)pc;
        spc_runOpcode(spc);
        if (spc->pc == next) { if (nt < 0) nt = b.n; } else if (tk < 0) tk = b.n;
      }
      if (nt < 0) nt = tk - 2;
      if (tk < 0) tk = nt + 2;
      in->cyc = (uint8_t)nt; in->cycTaken = (uint8_t)tk;
    } else {
      in->cyc = in->cycTaken = (uint8_t)(nt >= 0 ? nt : tk);
    }
  }
  spc_free(spc);
  return 1;
}

// --- the asset (timing.h SndTimingExport) ----------------------------------------------------------------------------
// The program's reach: from block 0's entry, from each word of its six PUSH/RET dispatch tables (tools/spc700dis.py
// DISPATCH: the sequence events, the look-ahead's, commands $10-$1F and the routines before them, commands $80-$8F /
// $F0-$FF, the upload kinds) and from the boot ROM's $FFC0 -- branches, jumps and calls followed, the fall-through but
// after the instructions that have none.
static const struct { uint16_t adr, n; } kDispatch[] = {
  { 0x1554, 60 }, { 0x1608, 60 }, { 0x1680, 16 }, { 0x16A0, 16 }, { 0x16C0, 32 }, { 0x1544, 8 },
};
static int noFallThrough(uint8_t op) { return op == 0x5F || op == 0x1F || op == 0x6F || op == 0x7F || op == 0x2F || op == 0x0F || op == 0xEF || op == 0xFF; }

long SndTimingExport(const uint8_t* image, uint16_t blo, uint16_t blen, uint8_t** out) {
  SndTiming* t = (SndTiming*)calloc(1, sizeof *t);
  uint8_t* seen = (uint8_t*)calloc(0x10000, 1);
  uint8_t* code = (uint8_t*)calloc(0x10000, 1);
  uint16_t* todo = (uint16_t*)malloc(0x10000 * sizeof *todo);
  if (!t || !seen || !code || !todo) { free(t); free(seen); free(code); free(todo); return 0; }
  SndTimingBuild(t, image, blo, (uint16_t)(blo + blen));
  SndTimingBuild(t, image, 0xFFC0, 0x0000);
  long nt = 0;
  todo[nt++] = blo; todo[nt++] = 0xFFC0;
  for (size_t k = 0; k < sizeof kDispatch / sizeof kDispatch[0]; k++)
    for (int i = 0; i < kDispatch[k].n; i++) {
      uint16_t a = (uint16_t)(kDispatch[k].adr + 2 * i);
      uint16_t w = (uint16_t)(image[a] | image[(uint16_t)(a + 1)] << 8);
      if (w && nt < 0x10000) todo[nt++] = w;
    }
#define INSIDE(pc) (((uint16_t)((pc) - blo) < blen) || (pc) >= 0xFFC0)
  while (nt) {
    unsigned pc = todo[--nt];
    while (INSIDE(pc) && !seen[pc]) {
      const SndIns* in = &t->ins[pc];
      seen[pc] = 1;
      for (int b = 0; b < in->len; b++) code[(uint16_t)(pc + b)] = 1;
      if ((in->kind == SND_INS_COND || in->kind == SND_INS_JUMP) && nt < 0x10000) todo[nt++] = in->target;
      if (noFallThrough(image[pc])) break;
      pc = (uint16_t)(pc + in->len);
    }
  }
  long n = 0;
  for (unsigned a = 0; a < 0x10000; a++) n += seen[a];
  long map = (blen + 7) / 8, len = 12 + map + 4 + n * 26;
  uint8_t* b = (uint8_t*)calloc(1, (size_t)len);
  if (b) {
    uint32_t hdr[2] = { 0x54444E53u, 1 };
    memcpy(b, hdr, 8);
    b[8] = (uint8_t)blo; b[9] = (uint8_t)(blo >> 8); b[10] = (uint8_t)blen; b[11] = (uint8_t)(blen >> 8);
    for (unsigned k = 0; k < blen; k++) if (code[(uint16_t)(blo + k)]) b[12 + k / 8] |= (uint8_t)(1u << (k & 7));
    long o = 12 + map;
    b[o] = (uint8_t)n; b[o + 1] = (uint8_t)(n >> 8); b[o + 2] = (uint8_t)(n >> 16); b[o + 3] = (uint8_t)(n >> 24); o += 4;
    for (unsigned a = 0; a < 0x10000; a++) {
      if (!seen[a]) continue;
      const SndIns* in = &t->ins[a];
      uint8_t* p = b + o;
      p[0] = (uint8_t)a; p[1] = (uint8_t)(a >> 8);
      p[2] = in->kind; p[3] = in->len; p[4] = in->cyc; p[5] = in->cycTaken; p[6] = (uint8_t)in->target; p[7] = (uint8_t)(in->target >> 8);
      p[8] = in->nio; memcpy(p + 9, in->ioIdx, 4); memcpy(p + 13, in->ioReg, 4); memcpy(p + 17, in->ioWrite, 4);
      p[21] = in->nw; memcpy(p + 22, in->wIdx, 4);
      o += 26;
    }
  }
  free(t); free(seen); free(code); free(todo);
  *out = b;
  return b ? len : 0;
}
