// ct_cpu_infra.c -- see ct_cpu_infra.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ct_cpu_infra.h"
#include "rom_verify.h"

uint8_t g_ram[CT_WRAM_SIZE];
bool    g_run_without_emu = false;

uint8_t* CtReadFile(const char* path, int* outLen) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  uint8_t* buf = (uint8_t*)malloc((size_t)size);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
  fclose(f);
  *outLen = (int)size;
  return buf;
}

Snes* CtOracleCreate(const uint8_t* rom, int len, int* outType) {
  long rl = len;                                               // only the supported dump (rom_verify.h)
  if (!(rom = CtRomVerify(rom, &rl, "the oracle's ROM"))) return NULL;
  len = (int)rl;
  Snes* s = snes_init();
  if (!snes_loadRom(s, rom, len)) { snes_free(s); return NULL; }
  CtRomApplyMemPatch(s->cart->rom, (long)s->cart->romSize);    // a measuring tool's patch, after the check
  if (outType) *outType = s->cart->type;
  // CT_ORACLE_SRAM=<file>: the battery RAM at power-on (e.g. saves/ct_real_save.srm), the oracle's side of CT_SRAM.
  { const char* sp = getenv("CT_ORACLE_SRAM");
    if (sp && *sp && s->cart->ram && s->cart->ramSize > 0) {
      FILE* f = fopen(sp, "rb");
      if (f) { size_t n = fread(s->cart->ram, 1, (size_t)s->cart->ramSize, f); fclose(f);
               fprintf(stderr, "oracle: SRAM %s -> %zu of %d bytes\n", sp, n, s->cart->ramSize); }
    } }
  return s;
}

// CT_ORACLE_PADLOG=<file>: the pad of every frame the oracle runs, run-length, in the `--btnscript` grammar
// (`hexbtn:frames,...`), so a heuristic drive (--navworld, a pathfinder) can become a fixed recipe (tools/recipes.py).
static FILE* s_padLog; static int s_padBtn = -1, s_padRun;
static void padLogFlush(void) {
  if (s_padLog && s_padRun) { fprintf(s_padLog, "%s%X:%d", ftell(s_padLog) ? "," : "", s_padBtn, s_padRun); fflush(s_padLog); }
  s_padRun = 0;
}
void CtOracleDestroy(Snes* s) { padLogFlush(); if (s) snes_free(s); }
static int s_runN;
void CtOracleFrameCountReset(void) { s_runN = 0; }
static void ctTimedPoke(Snes* s, int n) {
  static const char* spec; static int read;
  if (!read) { read = 1; spec = getenv("PEEK_POKE_AT"); }
  for (const char* g = spec; g && *g; ) {
    char* e; long at = strtol(g, &e, 10);
    if (e == g || *e != ':') return;
    const char* q = e + 1;
    while (*q && *q != ';') {
      long adr = strtol(q, &e, 16);
      if (e == q) break;
      if (adr >= 0x7E0000 && adr <= 0x7FFFFF) adr -= 0x7E0000;
      if (*e == '=') { long v = strtol(e + 1, &e, 16);
        if (at == n && adr >= 0 && adr < 0x20000) s->ram[adr] = (uint8_t)v; }
      while (*e && *e != ',' && *e != ';') e++;
      q = (*e == ',') ? e + 1 : e;
    }
    g = (*q == ';') ? q + 1 : q;
  }
}
void CtOracleRunFrame(Snes* s) {
  static int opened;
  if (!opened) { opened = 1; const char* p = getenv("CT_ORACLE_PADLOG"); if (p && *p) s_padLog = fopen(p, "w"); }
  ctTimedPoke(s, s_runN++);
  if (s_padLog) {
    int b = s->input1->currentState;
    if (b != s_padBtn) { padLogFlush(); s_padBtn = b; }
    s_padRun++;
  }
  snes_runFrame(s);
}
void CtOracleStep(Snes* s) { cpu_runOpcode(s->cpu); }

// --- CPU bus-write logging ---
#define CT_WRITELOG_MAX (1 << 20)
static CtWrite s_wlog[CT_WRITELOG_MAX];
static int     s_wlogN = 0;
static bool    s_wlogOn = false;

// Wrapper installed as cpu->write: record (when logging) then delegate to the core.
static void ctTracingCpuWrite(void* mem, uint32_t adr, uint8_t val) {
  if (s_wlogOn && s_wlogN < CT_WRITELOG_MAX) {
    Snes* sn = (Snes*)mem;                  // cpu->mem is the Snes*; capture the writer's PC
    s_wlog[s_wlogN].adr = adr;
    s_wlog[s_wlogN].ramAdr = sn->ramAdr;    // the WRAM-port latch BEFORE this write consumes it
    s_wlog[s_wlogN].val = val;
    s_wlog[s_wlogN].bank = sn->cpu->k;
    s_wlog[s_wlogN].pc = sn->cpu->pc;
    s_wlogN++;
  }
  snes_cpuWrite(mem, adr, val);
}

void CtOracleInstallWriteHook(Snes* s) { s->cpu->write = ctTracingCpuWrite; }
void CtWriteLogStart(void) { s_wlogN = 0; s_wlogOn = true; }
void CtWriteLogStop(void) { s_wlogOn = false; }
int  CtWriteLogCount(void) { return s_wlogN; }
const CtWrite* CtWriteLogData(void) { return s_wlog; }

int CtOracleRunToPC(Snes* s, uint8_t bank, uint16_t pc, int maxOps) {
  for (int i = 0; i < maxOps; i++) {
    if (s->cpu->k == bank && s->cpu->pc == pc) return i;
    cpu_runOpcode(s->cpu);
  }
  return -1;
}

static void packRegs(const Snes* s, CtRegs* r) {
  const Cpu* c = s->cpu;
  r->a = c->a; r->x = c->x; r->y = c->y;
  r->sp = c->sp; r->pc = c->pc; r->dp = c->dp;
  r->k = c->k; r->db = c->db;
  r->p = (uint8_t)((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) |
                   (c->d << 3) | (c->i << 2) | (c->z << 1) | (c->c));
  r->e = c->e ? 1 : 0;
}

void CtSnapshotFromOracle(const Snes* s, CtSnapshot* out) {
  memcpy(out->ram,   s->ram,        CT_WRAM_SIZE);
  memcpy(out->vram,  s->ppu->vram,  CT_VRAM_SIZE);
  memcpy(out->cgram, s->ppu->cgram, CT_CGRAM_SIZE);
  packRegs(s, &out->regs);
}

static int cmpRegion(const char* label, const char* region, const uint8_t* a,
                     const uint8_t* b, int len, int* reported, int maxReport) {
  int diffs = 0;
  for (int i = 0; i < len; i++) {
    if (a[i] != b[i]) {
      diffs++;
      if (*reported < maxReport) {
        printf("  [%s] %s offset 0x%05X: a=%02X b=%02X\n", label, region, i, a[i], b[i]);
        (*reported)++;
      }
    }
  }
  return diffs;
}

int CtVerifyEq(const CtSnapshot* a, const CtSnapshot* b, const char* label, int maxReport) {
  int reported = 0, diffs = 0;
  diffs += cmpRegion(label, "WRAM ", a->ram,   b->ram,   CT_WRAM_SIZE,  &reported, maxReport);
  diffs += cmpRegion(label, "VRAM ", a->vram,  b->vram,  CT_VRAM_SIZE,  &reported, maxReport);
  diffs += cmpRegion(label, "CGRAM", a->cgram, b->cgram, CT_CGRAM_SIZE, &reported, maxReport);
  if (memcmp(&a->regs, &b->regs, sizeof(CtRegs)) != 0) {
    diffs++;
    if (reported < maxReport)
      printf("  [%s] REGS differ: a.pc=%02X:%04X b.pc=%02X:%04X a.p=%02X b.p=%02X\n",
             label, a->regs.k, a->regs.pc, b->regs.k, b->regs.pc, a->regs.p, b->regs.p);
  }
  return diffs;
}

void CtPortCopyWramFromOracle(const Snes* s) {
  memcpy(g_ram, s->ram, CT_WRAM_SIZE);
}

uint32_t* g_ctPcHist = NULL;   // CT_CALLROM_PCHIST: PC -> execution count (see below)

// --- Call one ROM routine (return detection: see the header) ---
#define CT_CALL_SENTINEL 0xFFFDu   // RTS lands PC on SENTINEL+1; never executed

// Mirrors snes/cpu.c cpu_pushByte (stack in bank 0, post-decrement, emulation-mode page wrap). Calls
// snes_cpuWrite, not cpu->write, so the setup pushes stay out of the routine's write log.
static void ctPush8(Snes* s, uint8_t v) {
  snes_cpuWrite(s, s->cpu->sp, v);
  s->cpu->sp--;
  if (s->cpu->e) s->cpu->sp = (uint16_t)((s->cpu->sp & 0xFF) | 0x100);
}

int CtOracleCallRoutine(Snes* s, const CtCallSetup* in, CtCallResult* out) {
  Cpu* c = s->cpu;
  // No interrupt may be serviced mid-routine. Driving cpu_runOpcode raises none, but a loaded state may carry one.
  c->irqWanted = c->nmiWanted = c->intWanted = c->resetWanted = false;
  c->waiting = c->stopped = false;
  // Force native mode. A machine that has only been reset is still in emulation mode, where the stack is
  // pinned to page 1 and m/x are forced 8-bit, so the setup below would be overridden.
  if (c->e) { c->e = false; c->sp = 0x01FF; }

  if (in->dp != CT_CALL_KEEP) c->dp = (uint16_t)in->dp;
  if (in->db != CT_CALL_KEEP) c->db = (uint8_t)in->db;
  c->a = in->a; c->x = in->x; c->y = in->y;
  c->mf = in->m8 ? true : false;          // mf/xf set = 8-bit
  c->xf = in->x8 ? true : false;
  c->c  = in->carryIn ? true : false;
  // The 65816 clears the index high bytes when x is set (SEP #10).
  if (c->xf) { c->x &= 0xFF; c->y &= 0xFF; }

  uint16_t sp0 = c->sp;                    // BEFORE the push: the return-detection anchor
  if (in->isLong) ctPush8(s, (uint8_t)(in->addr >> 16));   // RTL pops bank first
  ctPush8(s, (uint8_t)(CT_CALL_SENTINEL >> 8));
  ctPush8(s, (uint8_t)(CT_CALL_SENTINEL & 0xFF));

  // `addr` is a 24-bit SNES address (ctParseAddr("C0/5535") = 0xC05535), not a flat ROM offset.
  uint8_t bank = (uint8_t)(in->addr >> 16);
  c->k = bank ? bank : 0xC0;                   // bare 16-bit -> bank $C0
  c->pc = (uint16_t)in->addr;

  // CT_CALLROM_PCHIST: count how many times each PC in the routine's bank executes, to check a cost model
  // instruction by instruction.
  int pcHist = getenv("CT_CALLROM_PCHIST") != NULL;
  int noNmi = in->noNmi || getenv("CT_CALLROM_NONMI") != NULL;
  if (pcHist) { if (!g_ctPcHist) g_ctPcHist = (uint32_t*)calloc(0x10000, sizeof(uint32_t));
                else memset(g_ctPcHist, 0, 0x10000 * sizeof(uint32_t)); }
  int maxOps = in->maxOps > 0 ? in->maxOps : 1000000;
  int ops = 0, returned = 0;
  uint64_t cyc0 = s->cycles;      // the oracle's master clock, charged per access
  extern long g_cycTagCycles[8];   // snes.c: bucket 4 = the 40-cycle DRAM refresh a scanline takes
  long ref0 = g_cycTagCycles[4];   // (not CPU work, so a listing model must not be charged them)
  for (; ops < maxOps; ops++) {
    if (pcHist && g_ctPcHist) g_ctPcHist[c->pc]++;
    // CT_CALLROM_NONMI: keep the vblank NMI out of the measurement of a routine longer than a frame.
    if (noNmi) { c->nmiWanted = c->irqWanted = c->intWanted = false; }
    cpu_runOpcode(c);
    // Only the final RTS/RTL brings sp back to sp0; checked after the opcode, so the sentinel never runs.
    if (c->sp == sp0) { ops++; returned = 1; break; }
  }
  packRegs(s, &out->regs);
  out->ops = ops;
  out->cycles = (long)(s->cycles - cyc0);
  out->refresh = g_cycTagCycles[4] - ref0;
  out->returned = returned;
  return returned;
}
