// ct_shadow.c — see ct_shadow.h and docs/SHADOW_PROPOSAL.md.
#include <stdlib.h>
#include <string.h>
#include "ct_shadow.h"
#include "cpu.h"
#include "cart.h"
#include "battle.h"

#define SH_WRAM  0x20000
#define SH_DEPTH 16
#define SH_DETAILS 8

// ---- The registry ----------------------------------------------------------------------------------------------

// C1/AF22, THE BATTLE RNG (battle.c battleRngBase; --sweeprng grades it offline). Entered `JSR C1/AF22` with A 8-bit
// = the count and X = the base (AF22 SEP #10 / STX $25: only X's low byte is read). Its direct-page bytes are the
// battle's, D = 0, and its absolutes ($B31E, $2989) are bank $7E's: the C function's convention.
//   AF73 REP #10 / AF75 PLX / AF76 STX $2C / AF78 RTS -- X comes back 16-bit holding the $2C AF28 saved, and A's low
//   byte holds the draw.
static int rngGuard(const CtShadowRegs* in) { return in->dp == 0x0000 && in->db == 0x7E; }
static void rngRun(uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in, CtShadowRegs* out) {
  (void)romLen;
  int r = CtBattleRngBaseProbe(w, rom, (in->a & 0xFF) - 1, in->x & 0xFF);   // its `maxv` is the count less one
  out->a = (uint16_t)((in->a & 0xFF00) | (r & 0xFF));
  out->x = (uint16_t)(w[0x2C] | (w[0x2D] << 8));
}

static const CtShadowEntry k_entries[] = {
  { 0xC1AF22, "battle RNG (battleRngBase)", 0x20, 0x20, 0, rngGuard, rngRun, CT_SH_A8 | CT_SH_X16 },
};
#define SH_N ((int)(sizeof k_entries / sizeof k_entries[0]))

// ---- State -----------------------------------------------------------------------------------------------------

typedef struct { long calls, matched, mismatched, interrupted, exits, skipped; } ShStats;
typedef struct {
  const CtShadowEntry* e; int idx;
  uint16_t entrySp, minSp; uint32_t retPc;
  int interrupted; long callNo; uint32_t frame;
  CtShadowRegs in, out;
} ShActive;

static Snes* s_snes;
static const uint8_t* s_rom; static long s_romLen;
static const char* s_dump; static int s_dumped;
static ShStats s_stats[SH_N];
static ShActive s_act[SH_DEPTH]; static int s_depth; static long s_overflow;
static uint8_t s_work[SH_DEPTH][SH_WRAM], s_pre[SH_DEPTH][SH_WRAM];
static uint8_t s_bits[1 << 21];                 // one bit per 24-bit address: is a registered entry there
static char s_detail[SH_DETAILS][256]; static int s_nDetail;

static void addDetail(const char* fmtd) {
  if (s_nDetail < SH_DETAILS) { strncpy(s_detail[s_nDetail], fmtd, sizeof s_detail[0] - 1); s_nDetail++; }
}
static uint8_t cpuP(const Cpu* c) {
  return (uint8_t)((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) | (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c);
}
static uint8_t stackByte(const Snes* s, uint16_t a) { return s->ram[a & 0x1FFF]; }   // bank 0's low 8 KB is WRAM

static void writeFile(const char* prefix, const char* what, const uint8_t* data) {
  char path[512]; snprintf(path, sizeof path, "%s.%s.wram", prefix, what);
  FILE* f = fopen(path, "wb"); if (f) { fwrite(data, 1, SH_WRAM, f); fclose(f); }
}

// The oracle is back at its caller: grade the call.
static void finish(Snes* s, ShActive* a, uint32_t pc) {
  ShStats* st = &s_stats[a->idx];
  char buf[256];
  if (a->interrupted) { st->interrupted++; return; }
  if (pc != a->retPc) {
    st->exits++;
    snprintf(buf, sizeof buf, "exit %06X %s: call #%ld (frame %u) left to %06X, not the return %06X",
             a->e->addr, a->e->name, a->callNo, a->frame, pc, a->retPc);
    addDetail(buf);
    return;
  }
  const uint8_t* w = s_work[a - s_act];
  int nDiff = 0, first = -1;
  for (int k = 0; k < SH_WRAM; k++) {
    if (k > a->minSp && k <= a->entrySp) continue;   // the bytes the call pushed and popped
    if (w[k] != s->ram[k]) { if (first < 0) first = k; nDiff++; }
  }
  const Cpu* c = s->cpu; unsigned o = a->e->outRegs;
  int regBad = 0; char regs[96] = "";
  if ((o & CT_SH_A8) && (c->a & 0xFF) != (a->out.a & 0xFF)) {
    regBad = 1; snprintf(regs, sizeof regs, " A oracle %02X port %02X", c->a & 0xFF, a->out.a & 0xFF); }
  if ((o & CT_SH_A16) && c->a != a->out.a) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " A oracle %04X port %04X", c->a, a->out.a); }
  if ((o & CT_SH_X16) && c->x != a->out.x) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " X oracle %04X port %04X", c->x, a->out.x); }
  if ((o & CT_SH_Y16) && c->y != a->out.y) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " Y oracle %04X port %04X", c->y, a->out.y); }
  if (!nDiff && !regBad) { st->matched++; return; }
  st->mismatched++;
  if (nDiff)
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X: %d WRAM byte(s), first %02X:%04X "
             "oracle %02X port %02X;%s", a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x, nDiff,
             0x7E + (first >> 16), first & 0xFFFF, s->ram[first], w[first], regs);
  else
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X:%s",
             a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x, regs);
  addDetail(buf);
  if (s_dump && !s_dumped) {
    s_dumped = 1;
    writeFile(s_dump, "pre", s_pre[a - s_act]); writeFile(s_dump, "oracle", s->ram); writeFile(s_dump, "port", w);
  }
}

static const CtShadowEntry* findEntry(uint32_t pc, int* idx) {
  for (int i = 0; i < SH_N; i++) if (k_entries[i].addr == pc) { *idx = i; return &k_entries[i]; }
  return NULL;
}

static void shOp(void* mem, uint32_t pc) {
  Snes* s = (Snes*)mem; Cpu* c = s->cpu;
  if (s != s_snes) return;
  while (s_depth > 0) {
    ShActive* a = &s_act[s_depth - 1];
    if (c->sp > a->entrySp) {                       // the RTS/RTL (or whatever unwound it) has run
      finish(s, a, pc);
      s_depth--;
      if (s_depth > 0 && a->minSp < s_act[s_depth - 1].minSp) s_act[s_depth - 1].minSp = a->minSp;
      continue;
    }
    if (c->sp < a->minSp) a->minSp = c->sp;
    break;
  }
  if (!(s_bits[pc >> 3] & (1u << (pc & 7)))) return;
  int idx; const CtShadowEntry* e = findEntry(pc, &idx);
  if (!e) return;
  ShStats* st = &s_stats[idx];
  st->calls++;
  CtShadowRegs in = { c->a, c->x, c->y, c->dp, c->db, cpuP(c) };
  if (c->e || (in.p & e->pMask) != e->pValue || (e->guard && !e->guard(&in))) { st->skipped++; return; }
  if (s_depth == SH_DEPTH) { s_overflow++; return; }
  int d = s_depth++;
  ShActive* a = &s_act[d];
  a->e = e; a->idx = idx; a->entrySp = c->sp; a->minSp = c->sp; a->interrupted = 0;
  a->callNo = st->calls; a->frame = s->frames; a->in = in; a->out = in;
  uint16_t ret = (uint16_t)(stackByte(s, c->sp + 1) | (stackByte(s, c->sp + 2) << 8));
  uint8_t bank = e->isLong ? stackByte(s, c->sp + 3) : c->k;
  a->retPc = ((uint32_t)bank << 16) | (uint16_t)(ret + 1);
  memcpy(s_pre[d], s->ram, SH_WRAM);
  memcpy(s_work[d], s->ram, SH_WRAM);
  e->run(s_work[d], s_rom, s_romLen, &in, &a->out);
}
static void shInt(void* mem) {
  if ((Snes*)mem != s_snes) return;
  for (int i = 0; i < s_depth; i++) s_act[i].interrupted = 1;
}

void CtShadowInstall(Snes* s, const char* dumpPrefix) {
  s_snes = s; s_rom = s->cart->rom; s_romLen = (long)s->cart->romSize;
  s_dump = dumpPrefix; s_dumped = 0; s_depth = 0; s_overflow = 0; s_nDetail = 0;
  memset(s_stats, 0, sizeof s_stats); memset(s_bits, 0, sizeof s_bits);
  for (int i = 0; i < SH_N; i++) s_bits[k_entries[i].addr >> 3] |= (uint8_t)(1u << (k_entries[i].addr & 7));
  g_cpuShadowOpHook = shOp; g_cpuShadowIntHook = shInt;
}
void CtShadowUninstall(void) { g_cpuShadowOpHook = NULL; g_cpuShadowIntHook = NULL; s_snes = NULL; }

long CtShadowMismatches(void) { long n = 0; for (int i = 0; i < SH_N; i++) n += s_stats[i].mismatched; return n; }

void CtShadowReport(FILE* f) {
  long calls = 0, matched = 0, mism = 0, intr = 0, exits = 0, skip = 0;
  for (int i = 0; i < SH_N; i++) {
    const ShStats* st = &s_stats[i];
    fprintf(f, "%06X %-32s calls %ld  matched %ld  mismatched %ld  interrupted %ld  exits %ld  skipped %ld\n",
            k_entries[i].addr, k_entries[i].name, st->calls, st->matched, st->mismatched, st->interrupted, st->exits,
            st->skipped);
    calls += st->calls; matched += st->matched; mism += st->mismatched; intr += st->interrupted; exits += st->exits;
    skip += st->skipped;
  }
  for (int i = 0; i < s_nDetail; i++) fprintf(f, "  %s\n", s_detail[i]);
  if (s_overflow) fprintf(f, "  %ld call(s) past the nesting depth %d, not compared\n", s_overflow, SH_DEPTH);
  fprintf(f, "shadow: %d entr%s, %ld calls, matched %ld, mismatched %ld, interrupted %ld, exits %ld, skipped %ld\n",
          SH_N, SH_N == 1 ? "y" : "ies", calls, matched, mism, intr, exits, skip);
}
