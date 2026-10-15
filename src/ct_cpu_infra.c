// ct_cpu_infra.c — see ct_cpu_infra.h.
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
  long rl = len;                                               // the supported dump only (rom_verify.h)
  if (!(rom = CtRomVerify(rom, &rl, "the oracle's ROM"))) return NULL;
  len = (int)rl;
  Snes* s = snes_init();
  if (!snes_loadRom(s, rom, len)) { snes_free(s); return NULL; }
  CtRomApplyMemPatch(s->cart->rom, (long)s->cart->romSize);    // a measuring tool's instrumentation, after the check
  if (outType) *outType = s->cart->type;
  // CT_ORACLE_SRAM=<file>: the cartridge's battery RAM at power-on (e.g. saves/ct_real_save.srm), so a boot drive
  // reaches the load screen with the same saves the port's CT_SRAM gives it.
  { const char* sp = getenv("CT_ORACLE_SRAM");
    if (sp && *sp && s->cart->ram && s->cart->ramSize > 0) {
      FILE* f = fopen(sp, "rb");
      if (f) { size_t n = fread(s->cart->ram, 1, (size_t)s->cart->ramSize, f); fclose(f);
               fprintf(stderr, "oracle: SRAM %s -> %zu of %d bytes\n", sp, n, s->cart->ramSize); }
    } }
  return s;
}

// CT_ORACLE_PADLOG=<file>: the pad of every frame the oracle runs, run-length, in the `--btnscript` grammar
// (`hexbtn:frames,...`) -- how a heuristic drive (--navworld, a pathfinder) becomes a fixed recipe (tools/recipes.py).
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

// --- Call ONE ROM routine (see the header for the return-detection argument) ---
#define CT_CALL_SENTINEL 0xFFFDu   // RTS lands PC on SENTINEL+1; never executed

// Mirrors snes/cpu.c cpu_pushByte (stack in bank 0, post-decrement, emulation-mode page
// wrap). Deliberately calls snes_cpuWrite and NOT cpu->write: our own setup pushes must
// not show up in the routine's write log.
static void ctPush8(Snes* s, uint8_t v) {
  snes_cpuWrite(s, s->cpu->sp, v);
  s->cpu->sp--;
  if (s->cpu->e) s->cpu->sp = (uint16_t)((s->cpu->sp & 0xFF) | 0x100);
}

int CtOracleCallRoutine(Snes* s, const CtCallSetup* in, CtCallResult* out) {
  Cpu* c = s->cpu;
  // No interrupt may be serviced mid-routine: we drive cpu_runOpcode directly rather than
  // snes_runFrame, so nothing NEW is raised, but a loaded savestate can carry one pending.
  c->irqWanted = c->nmiWanted = c->intWanted = c->resetWanted = false;
  c->waiting = c->stopped = false;
  // FORCE NATIVE MODE. Every 65816 routine in CT runs native, but a machine that has only
  // been reset (no --from state, no frames run) is still in EMULATION mode, where the stack
  // is pinned to page 1 and m/x are forced to 8-bit -- the setup below would be silently
  // overridden and the routine would never return. Found by this mode's own self-test:
  // all six handlers reported NORETURN with identical registers.
  if (c->e) { c->e = false; c->sp = 0x01FF; }

  if (in->dp != CT_CALL_KEEP) c->dp = (uint16_t)in->dp;
  if (in->db != CT_CALL_KEEP) c->db = (uint8_t)in->db;
  c->a = in->a; c->x = in->x; c->y = in->y;
  c->mf = in->m8 ? true : false;          // NOTE the SNES sense: mf/xf SET = 8-bit
  c->xf = in->x8 ? true : false;
  c->c  = in->carryIn ? true : false;
  // The 65816 clears the index HIGH BYTES the moment X is set (SEP #10 semantics). Mirror
  // that, else a 16-bit value handed to an 8-bit-index routine would survive illegally.
  if (c->xf) { c->x &= 0xFF; c->y &= 0xFF; }

  uint16_t sp0 = c->sp;                    // BEFORE the push: the return-detection anchor
  if (in->isLong) ctPush8(s, (uint8_t)(in->addr >> 16));   // RTL pops bank first
  ctPush8(s, (uint8_t)(CT_CALL_SENTINEL >> 8));
  ctPush8(s, (uint8_t)(CT_CALL_SENTINEL & 0xFF));

  // ctParseAddr("C0/5535") yields the 24-bit SNES address 0xC05535, NOT a flat ROM offset.
  // (Assuming flat here sent the CPU to bank $80 and cost this mode's first two self-tests.)
  uint8_t bank = (uint8_t)(in->addr >> 16);
  c->k = bank ? bank : 0xC0;                   // bare 16-bit -> bank $C0
  c->pc = (uint16_t)in->addr;

  // CT_CALLROM_PCHIST: count how many times each PC in the routine's own bank executes. A cost
  // model of a ROM loop is a claim about which instructions run how often, and this is the way to
  // check that claim instruction by instruction instead of arguing with a total.
  int pcHist = getenv("CT_CALLROM_PCHIST") != NULL;
  int noNmi = in->noNmi || getenv("CT_CALLROM_NONMI") != NULL;
  if (pcHist) { if (!g_ctPcHist) g_ctPcHist = (uint32_t*)calloc(0x10000, sizeof(uint32_t));
                else memset(g_ctPcHist, 0, 0x10000 * sizeof(uint32_t)); }
  int maxOps = in->maxOps > 0 ? in->maxOps : 1000000;
  int ops = 0, returned = 0;
  uint64_t cyc0 = s->cycles;      // the oracle's master clock, charged per access
  extern long g_cycTagCycles[8];   // snes.c: bucket 4 = the 40-cycle DRAM refresh a scanline takes
  long ref0 = g_cycTagCycles[4];   // (the CPU never gets those, so a listing model must not be charged them)
  for (; ops < maxOps; ops++) {
    if (pcHist && g_ctPcHist) g_ctPcHist[c->pc]++;
    // CT_CALLROM_NONMI: keep the vblank NMI out of the measurement. A routine that runs longer
    // than a frame takes the handler mid-way, and its ~440 opcodes then read as if they were the
    // routine's own -- which is exactly what made a cycle-exact model of C3/0557 look 8% short.
    if (noNmi) { c->nmiWanted = c->irqWanted = c->intWanted = false; }
    cpu_runOpcode(c);
    // The return address is on the stack for the whole routine, so sp < sp0 throughout;
    // only the final RTS/RTL pops it back. Checking AFTER the opcode means the RTS is the
    // last thing executed and the sentinel itself never runs.
    if (c->sp == sp0) { ops++; returned = 1; break; }
  }
  packRegs(s, &out->regs);
  out->ops = ops;
  out->cycles = (long)(s->cycles - cyc0);
  out->refresh = g_cycTagCycles[4] - ref0;
  out->returned = returned;
  return returned;
}
