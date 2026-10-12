// ct_shadow.c — see ct_shadow.h and docs/SHADOW_PROPOSAL.md.
#include <stdlib.h>
#include <string.h>
#include "ct_shadow.h"
#include "cpu.h"
#include "cart.h"
#include "apu.h"
#include "battle.h"
#include "move.h"
#include "menu.h"

#define SH_WRAM  0x20000
#define SH_DEPTH 16
#define SH_DETAILS 8

// ---- The registry ----------------------------------------------------------------------------------------------

// C1/AF22, THE BATTLE RNG (battle.c battleRngBase; --sweeprng grades it offline). Entered `JSR C1/AF22` with A 8-bit
// = the count and X = the base (AF22 SEP #10 / STX $25: only X's low byte is read). Its direct-page bytes are the
// battle's, D = 0, and its absolutes ($B31E, $2989) are bank $7E's: the C function's convention.
//   AF73 REP #10 / AF75 PLX / AF76 STX $2C / AF78 RTS -- X comes back 16-bit holding the $2C AF28 saved, and A's low
//   byte holds the draw.
static int rngGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) { (void)e; (void)w; return in->dp == 0x0000 && in->db == 0x7E; }
static int rngRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)romLen;
  int r = CtBattleRngBaseProbe(w, rom, (in->a & 0xFF) - 1, in->x & 0xFF);   // its `maxv` is the count less one
  out->a = (uint16_t)((in->a & 0xFF00) | (r & 0xFF));
  out->x = (uint16_t)(w[0x2C] | (w[0x2D] << 8));
  return CT_SH_RETURNS;
}

// THE FIELD-SCRIPT OPCODE HANDLERS (move.c g_ctVmOps, CtFieldRunnerOne; --sweepops grades them offline). The runner
// C0/5A93 latches the object index (5A9C `STX $6D`, DP = $0100 -> $016D), then reaches the handler through
// `5AAA AND #00FF / 5AAD ASL A / 5AAE TAX / 5AB1 JSR ($5D6E,X)`: D = $0100, X = the opcode doubled, Y = the IP of
// the opcode byte at $7F:2001+Y. The handler hands back the new IP in X (committed at 5ABF) and the carry (1 = keep
// spending the visit, 0 = yield, tested at 5AB4). The guard asks that X carry THIS handler's opcode and that the byte
// at the IP be it: some handlers are also subroutines of others with the same X and Y (op B6 at C0/354F runs op 95's
// body at C0/5410, op B5 op 94's at C0/5215), and such a call is part of the caller's handler, graded at its entry,
// not this one's -- it is skipped rather than graded under the wrong convention.
static const CtShadowSkip k_vmSkip[] = {
  { 0x0308, 0x0316, "C3/0002's workspace: ct_lz_decompress is a rewrite, graded on its output (as --sweepops)" },
  { 0, 0, NULL }
};
static int vmGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  return in->dp == 0x0100 && in->x == e->arg * 2 && in->y < 0xDFFF && w[0x12001 + in->y] == e->arg;
}
static int vmRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                 CtShadowRegs* out) {
  (void)e;
  g_ctApuPort1 = in->apu[1]; g_ctApuPort3 = in->apu[3];   // ops ED and the C0/3CC6 poll read the SPC's mailbox
  CtHRet h;
  if (CtFieldRunnerOne(w, rom, (int)romLen, w[0x016D], in->y, &h) >= 0) return CT_SH_UNPORTED;
  out->x = (uint16_t)h.ip;
  out->p = (uint8_t)((in->p & ~1) | (h.carry & 1));
  return CT_SH_RETURNS;
}

// C0/ACFD, THE VELOCITY + PRIORITY BUILDER (move.c CtBuildVelocityPriority; --sweepacfd). Entered with X = the object
// index and D = $0100 (the VM handlers' `LDX $6D / ... / JSR C0/ACFD`); it reads the heading its caller staged at
// $01EE. Graded on WRAM alone: its callers reload what they need.
static int acfdGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x0100 && in->db == 0x00 && in->x < 0x80 && !(in->x & 1);
}
static int acfdRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)romLen; (void)out;
  CtBuildVelocityPriority(w, rom, in->x);
  return CT_SH_RETURNS;
}

// C1/DC64, THE PHYSICAL TO-HIT ROLL (battle.c battleHitCore; --sweephitroll). The caller stages hit in $16 and evade
// in $18; the routine hands back its verdict in $16, the roll in $18 and zero in $AE4F -- all WRAM. Its draw is a
// JSR C1/AF22, which the shadow grades on its own as well.
static int battleGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x0000 && in->db == 0x7E;
}
static int hitRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out;
  int pct, roll; CtBattleHitCoreProbe(w, rom, &pct, &roll);
  return CT_SH_RETURNS;
}

// THE ENEMY AI (battle.c aiWalk / aiCond / aiCommit; --sweepaieval). The walk C1/AFD2 is entered with A = the enemy's
// AI id (C1/8C0C's TDC leaves B = 0); each condition handler is reached by the walker's `ASL A / TAX / JSR ($B80D,X)`
// (B006 / B021) with X = the op doubled and the cursor $B1D2 on its op byte; the commit C1/8C3E is a condition's JSR
// (condition 00 is `JSR C1/8C3E / RTS`). Every one of them returns: the sweep's stop at C1/8C53 is where IT cuts the
// comparison, inside the commit, not where the cartridge's call ends -- so the shadow runs the C whole (the
// *Whole entry points), commit included. What the C does not port is flagged and counted `unported`.
static int aiWalkRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)e; (void)romLen; (void)out;
  int unp = 0;
  return CtBattleAiWalkWhole(w, rom, in->a & 0xFF, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}
static int aiCondGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)w; return in->dp == 0x0000 && in->db == 0x7E && in->x == e->arg * 2;
}
static int aiCondRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)romLen; (void)in; (void)out;
  int unp = 0;
  return CtBattleAiCondWhole(w, rom, e->arg, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}
static int aiCommitRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                       CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out;
  int unp = 0;
  return CtBattleAiCommitSweep(w, rom, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}

// C2/8791 AND C2/87D5, THE INVENTORY'S ADD AND REMOVE (menu.c CtMenuInvAdd / CtMenuInvRemove). Both take the item in
// $04C9 and the count in $04CB, `PHX / PHP / SEP #30` on entry and `PLP / PLX / RTS` on the way out, so any width
// enters and X comes back as it was; A's low byte is the verdict (0 done, $FF not held / bag full, $FE not enough).
// Their absolutes ($2400,X / $2500,X) are bank $7E's.
static int invGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->db == 0x7E;
}
static int invRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)rom; (void)romLen;
  int r = e->arg ? CtMenuInvRemove(w, w[0x04C9], w[0x04CB]) : CtMenuInvAdd(w, w[0x04C9], w[0x04CB]);
  out->a = (uint16_t)((in->a & 0xFF00) | (r & 0xFF));
  return CT_SH_RETURNS;
}

static const CtShadowEntry k_fixed[] = {
  { 0xC28791, "inventory add (CtMenuInvAdd)", 0x00, 0x00, 0, 0, invGuard, invRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC287D5, "inventory remove (CtMenuInvRemove)", 0x00, 0x00, 0, 1, invGuard, invRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC1AF22, "battle RNG (battleRngBase)", 0x20, 0x20, 0, 0, rngGuard, rngRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC0ACFD, "velocity+priority (ACFD)", 0x30, 0x20, 0, 0, acfdGuard, acfdRun, 0, NULL },
  { 0xC1DC64, "battle hit roll (battleHitCore)", 0x30, 0x20, 0, 0, battleGuard, hitRun, 0, NULL },
  { 0xC1AFD2, "battle AI walk (aiWalk)", 0x30, 0x20, 0, 0, battleGuard, aiWalkRun, 0, NULL },
  { 0xC18C3E, "battle AI commit (aiCommit)", 0x30, 0x20, 0, 0, battleGuard, aiCommitRun, 0, NULL },
};
#define AI_NCOND 0x29   // the condition ops 00-28 (--sweepaieval's range; op 0F, C1/2C02's path test, is unported)
#define SH_NFIXED ((int)(sizeof k_fixed / sizeof k_fixed[0]))
#define SH_MAX (SH_NFIXED + CT_VM_NOPS + AI_NCOND)

static CtShadowEntry s_entries[SH_MAX]; static int s_n;
static char s_names[CT_VM_NOPS + AI_NCOND][40];

static void buildRegistry(const uint8_t* rom) {
  s_n = 0;
  for (int i = 0; i < SH_NFIXED; i++) s_entries[s_n++] = k_fixed[i];
  for (int i = 0; i < CT_VM_NOPS; i++) {
    snprintf(s_names[i], sizeof s_names[i], "vm op %02X %s", g_ctVmOps[i].op, g_ctVmOps[i].name);
    // m 8-bit, x 16-bit: the runner's 5A9E REP #10 and its SEP #20
    CtShadowEntry e = { 0xC00000u | g_ctVmOps[i].addr, s_names[i], 0x30, 0x20, 0, g_ctVmOps[i].op, vmGuard, vmRun,
                        CT_SH_X16 | CT_SH_C, k_vmSkip };
    s_entries[s_n++] = e;
  }
  for (int op = 0; op < AI_NCOND; op++) {        // the handler addresses, from the ROM's own table at C1/B80D
    char* nm = s_names[CT_VM_NOPS + op];
    snprintf(nm, 40, "battle AI condition %02X", op);
    uint16_t hand = (uint16_t)(rom[0x01B80D + op * 2] | (rom[0x01B80D + op * 2 + 1] << 8));
    CtShadowEntry e = { 0xC10000u | hand, nm, 0x30, 0x20, 0, op, aiCondGuard, aiCondRun, 0, NULL };
    s_entries[s_n++] = e;
  }
}

// ---- State -----------------------------------------------------------------------------------------------------

typedef struct { long calls, matched, mismatched, interrupted, exits, skipped, unported, skipDiffer; } ShStats;
typedef struct {
  const CtShadowEntry* e; int idx;
  uint16_t entrySp, minSp; uint32_t retPc;
  int interrupted; long callNo; uint32_t frame;
  CtShadowRegs in, out;
} ShActive;

static Snes* s_snes;
static const uint8_t* s_rom; static long s_romLen;
static const char* s_dump; static int s_dumped;
static ShStats s_stats[SH_MAX];
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
static int skipped(const CtShadowSkip* sk, int k) {
  for (; sk && sk->why; sk++) if ((uint32_t)k >= sk->lo && (uint32_t)k <= sk->hi) return 1;
  return 0;
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
  int nDiff = 0, first = -1, skipDiff = 0;
  for (int k = 0; k < SH_WRAM; k++) {
    if (!(k & 0xFF) && !memcmp(w + k, s->ram + k, 0x100)) { k += 0xFF; continue; }   // an equal page, at once
    if (k > a->minSp && k <= a->entrySp) continue;   // the bytes the call pushed and popped
    if (w[k] == s->ram[k]) continue;
    if (skipped(a->e->skip, k)) { skipDiff = 1; continue; }
    if (first < 0) first = k;
    nDiff++;
  }
  if (skipDiff) st->skipDiffer++;
  const Cpu* c = s->cpu; unsigned o = a->e->outRegs;
  int regBad = 0; char regs[128] = "";
  if ((o & CT_SH_A8) && (c->a & 0xFF) != (a->out.a & 0xFF)) {
    regBad = 1; snprintf(regs, sizeof regs, " A oracle %02X port %02X", c->a & 0xFF, a->out.a & 0xFF); }
  if ((o & CT_SH_A16) && c->a != a->out.a) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " A oracle %04X port %04X", c->a, a->out.a); }
  if ((o & CT_SH_X16) && c->x != a->out.x) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " X oracle %04X port %04X", c->x, a->out.x); }
  if ((o & CT_SH_Y16) && c->y != a->out.y) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " Y oracle %04X port %04X", c->y, a->out.y); }
  if ((o & CT_SH_C) && (int)c->c != (a->out.p & 1)) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " carry oracle %d port %d", c->c, a->out.p & 1); }
  if (!nDiff && !regBad) { st->matched++; return; }
  st->mismatched++;
  if (nDiff)
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X Y=%04X S=%04X: %d WRAM byte(s), "
             "first %02X:%04X oracle %02X port %02X;%s", a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x,
             a->in.y, a->entrySp, nDiff, 0x7E + (first >> 16), first & 0xFFFF, s->ram[first], w[first], regs);
  else
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X Y=%04X S=%04X:%s",
             a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x, a->in.y, a->entrySp, regs);
  addDetail(buf);
  if (s_dump && !s_dumped) {
    s_dumped = 1;
    writeFile(s_dump, "pre", s_pre[a - s_act]); writeFile(s_dump, "oracle", s->ram); writeFile(s_dump, "port", w);
  }
}

static int findEntry(uint32_t pc) {
  for (int i = 0; i < s_n; i++) if (s_entries[i].addr == pc) return i;
  return -1;
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
  int first = findEntry(pc);
  if (first < 0) return;
  CtShadowRegs in = { c->a, c->x, c->y, c->dp, c->db, cpuP(c),
                      { s->apu->outPorts[0], s->apu->outPorts[1], s->apu->outPorts[2], s->apu->outPorts[3] } };
  // Several entries may share an address (two AI condition ops on one handler): the first whose convention holds.
  int idx = -1;
  for (int i = first; i < s_n && idx < 0; i++)
    if (s_entries[i].addr == pc && !c->e && (in.p & s_entries[i].pMask) == s_entries[i].pValue &&
        (!s_entries[i].guard || s_entries[i].guard(&s_entries[i], s->ram, &in))) idx = i;
  if (idx < 0) { s_stats[first].calls++; s_stats[first].skipped++; return; }
  const CtShadowEntry* e = &s_entries[idx];
  ShStats* st = &s_stats[idx];
  st->calls++;
  if (s_depth == SH_DEPTH) { s_overflow++; return; }
  int d = s_depth;
  memcpy(s_work[d], s->ram, SH_WRAM);
  CtShadowRegs out = in;
  int how = e->run(e, s_work[d], s_rom, s_romLen, &in, &out);
  if (how == CT_SH_UNPORTED) { st->unported++; return; }
  if (s_dump && !s_dumped) memcpy(s_pre[d], s->ram, SH_WRAM);   // only --dump reads it
  s_depth++;
  ShActive* a = &s_act[d];
  a->e = e; a->idx = idx; a->entrySp = c->sp; a->minSp = c->sp; a->interrupted = 0;
  a->callNo = st->calls; a->frame = s->frames; a->in = in; a->out = out;
  uint16_t ret = (uint16_t)(stackByte(s, c->sp + 1) | (stackByte(s, c->sp + 2) << 8));
  uint8_t bank = e->isLong ? stackByte(s, c->sp + 3) : c->k;
  a->retPc = ((uint32_t)bank << 16) | (uint16_t)(ret + 1);
}
static void shInt(void* mem) {
  if ((Snes*)mem != s_snes) return;
  for (int i = 0; i < s_depth; i++) s_act[i].interrupted = 1;
}

void CtShadowInstall(Snes* s, const char* dumpPrefix) {
  buildRegistry(s->cart->rom);
  s_snes = s; s_rom = s->cart->rom; s_romLen = (long)s->cart->romSize;
  s_dump = dumpPrefix; s_dumped = 0; s_depth = 0; s_overflow = 0; s_nDetail = 0;
  memset(s_stats, 0, sizeof s_stats); memset(s_bits, 0, sizeof s_bits);
  for (int i = 0; i < s_n; i++) s_bits[s_entries[i].addr >> 3] |= (uint8_t)(1u << (s_entries[i].addr & 7));
  g_cpuShadowOpHook = shOp; g_cpuShadowIntHook = shInt;
}
void CtShadowUninstall(void) { g_cpuShadowOpHook = NULL; g_cpuShadowIntHook = NULL; s_snes = NULL; }

long CtShadowMismatches(void) { long n = 0; for (int i = 0; i < s_n; i++) n += s_stats[i].mismatched; return n; }

void CtShadowReport(FILE* f) {
  long calls = 0, matched = 0, mism = 0, intr = 0, exits = 0, skip = 0, unp = 0; int reached = 0;
  for (int i = 0; i < s_n; i++) {
    const ShStats* st = &s_stats[i];
    if (!st->calls) continue;
    reached++;
    fprintf(f, "%06X %-32s calls %ld  matched %ld  mismatched %ld  interrupted %ld  exits %ld  skipped %ld",
            s_entries[i].addr, s_entries[i].name, st->calls, st->matched, st->mismatched, st->interrupted, st->exits,
            st->skipped);
    if (st->unported) fprintf(f, "  unported %ld", st->unported);
    if (st->skipDiffer) fprintf(f, "  (%ld differed only where not compared)", st->skipDiffer);
    fprintf(f, "\n");
    calls += st->calls; matched += st->matched; mism += st->mismatched; intr += st->interrupted; exits += st->exits;
    skip += st->skipped; unp += st->unported;
  }
  for (int i = 0; i < s_nDetail; i++) fprintf(f, "  %s\n", s_detail[i]);
  if (s_overflow) fprintf(f, "  %ld call(s) past the nesting depth %d, not compared\n", s_overflow, SH_DEPTH);
  // The ranges not compared, said out loud whether or not anything differed there.
  { const CtShadowSkip* seen[8]; int ns = 0;
    for (int i = 0; i < s_n; i++)
      for (const CtShadowSkip* sk = s_entries[i].skip; sk && sk->why; sk++) {
        int dup = 0; for (int j = 0; j < ns; j++) if (seen[j] == sk) dup = 1;
        if (dup || ns == 8 || !s_stats[i].calls) continue;
        seen[ns++] = sk;
        fprintf(f, "  not compared: $%04X-$%04X, %s\n", sk->lo, sk->hi, sk->why);
      } }
  fprintf(f, "shadow: %d entries, %d reached, %ld calls, matched %ld, mismatched %ld, interrupted %ld, exits %ld, "
          "skipped %ld, unported %ld\n", s_n, reached, calls, matched, mism, intr, exits, skip, unp);
}
