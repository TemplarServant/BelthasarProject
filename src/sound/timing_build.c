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
