// seq_xlat.c - see seq_xlat.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seq_xlat.h"
#include "apu.h"
#include "ppu.h"

uint8_t ctXlatPeek(const CtXlat* x, uint32_t adr) {
  uint8_t k = (uint8_t)(adr >> 16);
  uint16_t p = (uint16_t)adr;
  if (k == 0x7E || k == 0x7F) return x->snes->ram[((k & 1) << 16) | p];
  if ((k < 0x40 || (k >= 0x80 && k < 0xC0)) && p < 0x2000) return x->snes->ram[p];
  return 0;
}

static uint32_t pcOf(const CtXlat* x) { return ((uint32_t)x->cpu->k << 16) | x->cpu->pc; }

int ctXlatMiss(CtXlat* x) {
  x->failPc = pcOf(x);
  return CT_XLAT_MISS;
}

// One instruction the translation does not have, run by the core itself. `boundary` = 1 when control arrived through
// the dispatch (the host's hook and a pending interrupt come first, as X_BOUNDARY does); 0 when X_BOUNDARY already ran
// for this address and only the bytes differed.
int ctXlatInterp(CtXlat* x, int boundary) {
  if (!x->interp) return ctXlatMiss(x);
  Cpu* c = x->cpu;
  uint32_t p = pcOf(x);
  { enum { N = 256 }; static uint32_t seen[N]; static int n = 0; int i;
    for (i = 0; i < n; i++) if (seen[i] == p) break;
    if (i == n && n < N) { seen[n++] = p;
      fprintf(stderr, "xlat: %02X:%04X is not in the translation (%s) -- run by the core\n", (unsigned)(p >> 16),
              (unsigned)(p & 0xFFFF), boundary ? "no label" : "its RAM bytes differ"); } }
  if (boundary) {
    if (x->hook) { int r = x->hook(x); if (r) return r; }
    if (c->intWanted) { cpu_xlatInterrupt(c); return 0; }
    // the logical frame's detector, as X_BOUNDARY (once per instruction: boundary 0 has already run it here)
    if (g_cpuOpHook) { g_cpuOpHook(c->mem, p); if (pcOf(x) != p) return 0; }
  }
  cpu_xlatExec(c, cpu_xlatFetch(c));
  return 0;
}

int ctXlatBadOp(CtXlat* x, uint8_t want, uint8_t got) {
  x->failPc = (pcOf(x) - 1) & 0xFFFFFF;          // the fetch has already moved the PC past the opcode
  x->failWant = want; x->failGot = got;
  return CT_XLAT_BADOP;
}

int ctXlatBadBytes(CtXlat* x, int len, uint8_t b1, uint8_t b2, uint8_t b3) {
  uint32_t p = pcOf(x);
  static const int idx[3] = { 1, 2, 3 };
  const uint8_t want[3] = { b1, b2, b3 };
  x->failPc = p;
  for (int i = 0; i < 3 && idx[i] < len; i++) {
    uint8_t got = ctXlatPeek(x, p + idx[i]);
    if (got != want[i]) { x->failWant = want[i]; x->failGot = got; break; }
  }
  return CT_XLAT_BADBYTES;
}

// ---- a translated stage inside a host --------------------------------------------------------------------------
struct CtXlatStage {
  Snes* hw;
  struct Apu* ownApu;      // the hardware's own SPC, parked while the host's is borrowed
  CtXlat x;
  uint32_t frame0, stopFrame;   // stopFrame: snes_runFrame's `frame`, taken when the run leaves the vblank
  int inFrame;                  // 0 = still in the vblank the last stop was in (snes_runFrame's first loop)
  int done;
  int rule;                     // CtXlatStageSetRule: run under CLAUDE.md's exception (logical frames, phase 0)
  int (*prog)(CtXlat*);         // the translated program: CtXlat_seq_c3 ($C3 entries) or CtXlat_gwarp_fd (the warp)
};

// src/xlat/seq_c3.c is the sequence engine as the attract's `FF 60` (entry 0, the race) and `FF 63` (entry 3)
// ran it: one program, because both run on the same overlay, NMI, interpreter and library -- only the tasks differ.
// ct.exe alone builds the translations, as its lockstep tools' references (--xlatcmp, --xlatphase): CT_XLAT_SEQ_C3 the
// attract's two stages (the playable host runs them natively: seq_race.c, seq_entry3.c, `$0384` bit 6) and, without
// CT_XLAT_NO_GWARP, the gate warp (the playable host's is src/seq_gwarp.c). Belthasar links no translated program.
int CtXlatStageHas(uint8_t seqId) {
#ifdef CT_XLAT_SEQ_C3
  if (seqId == 0x60 || seqId == 0x63) return 1;
#endif
#ifndef CT_XLAT_NO_GWARP
  if (seqId >= 0x80 && seqId <= 0x8F) return 1;                     // $80-$8F: the gate warp (C0/3FE4)
#endif
  return 0;
}

// The hook of a hosted run: stop exactly where snes_runFrame stops (snes/snes.c) -- its two loops, checked at the same
// instruction boundaries: `while (inVblank)` runs the rest of the vblank the last stop was in, then `frame = frames;
// while (!inVblank && frame == frames)` runs to the next start of vblank (or to a frame a DMA skipped it in).
// ⭐ NOT "inVblank && frames moved": the counter wraps at the end of line 261 and inVblank drops one step (2 master
// cycles) later, at v=0 h=0 -- an instruction boundary on that step read as a new frame, 225 lines early, and the host
// showed the frame before again (the next stop then runs a frame and 225 lines). Measured on the attract: 51 of the
// 562 stops of a run through the race and entry 3 fell there; on entry 3 each one is a whole-picture repeat against
// the cartridge's `--peek`, which runs snes_runFrame (the Blackbird's palace 4 px behind on stage frames 43, 45, 47,
// 81, 121, ...), and WHICH frames hit that step moved with any change of the stage's cycles -- FD/E39C's tick of the
// colour records the field hands over (disasm/notes/sequence_table.md 11.12).
// CT_XLAT_SEEDTRACE=1: every entry into C3/02DD (the seed, CLAUDE.md's one exception) on the port's translated
// hardware -- the stage (the field's and the world's calls are native: wipe.c prints the same line) -- with every input the routine folds into $0386: A and
// the carry (ROL A), X (TXA), $0008, $00F0/$00F2/$00F4, the open bus the CMP $2137 compares, and $0351 (the virtual
// $213C). The cartridge's side is `--cputrace` with CPUTRACE_FROM=C302DD under CT_ORACLE_VIRTUAL_BEAM=1.
static int seedTraceOn(void) { static int on = -1; if (on < 0) on = getenv("CT_XLAT_SEEDTRACE") ? 1 : 0; return on; }
static void seedTrace(const CtXlat* x, const char* who) {
  const Cpu* c = x->cpu;
  if (c->k != 0xC3 || c->pc != 0x02DD) return;
  const uint8_t* m = x->snes->ram;
  fprintf(stderr, "seedtrace %s f%u v=%u h=%u A=%04X X=%04X C=%d D=%04X B=%02X $0386=%02X $0351=%02X $0008=%02X "
          "$00F0=%02X $00F2=%02X $00F4=%02X bus=%02X\n", who, x->snes->frames, x->snes->vPos, x->snes->hPos, c->a, c->x,
          c->c ? 1 : 0, c->dp, c->db, m[0x386], m[0x351], m[0x08], m[0xF0], m[0xF2], m[0xF4], x->snes->openBus);
}

static int stageHook(CtXlat* x) {
  if (seedTraceOn()) seedTrace(x, "stage");
  CtXlatStage* st = (CtXlatStage*)x->user;
  Snes* s = st->hw;
  if (!st->inFrame) {                                 // snes_runFrame's first loop
    if (s->inVblank) return 0;
    st->inFrame = 1; st->stopFrame = s->frames;       // `uint32_t frame = snes->frames;`
  }
  if (!s->inVblank && s->frames == st->stopFrame) return 0;   // its second loop
  st->inFrame = 0;
  return 1;
}

CtXlatStage* CtXlatStageOpen(const uint8_t* rom, long romLen, const uint8_t* wram128, uint8_t seqId,
                             struct Apu* hostApu, uint16_t hostY) {
  if (!rom || !wram128 || !CtXlatStageHas(seqId)) return NULL;
  CtXlatStage* st = (CtXlatStage*)calloc(1, sizeof *st);
  if (!st) return NULL;
  st->hw = snes_init();
  if (!st->hw || !snes_loadRom(st->hw, rom, (int)romLen)) { CtXlatStageClose(st); return NULL; }
  Snes* s = st->hw;
  memcpy(s->ram, wram128, sizeof s->ram);
  if (hostApu) { st->ownApu = s->apu; s->apu = hostApu; }
  Cpu* c = s->cpu;
  // C0/3FD3..3FE0 and C0/0B4E, as seq_xlat.h lists them.
  int warp = seqId >= 0x80;
#if defined(CT_XLAT_SEQ_C3) && !defined(CT_XLAT_NO_GWARP)
  st->prog = warp ? CtXlat_gwarp_fd : CtXlat_seq_c3;
#elif defined(CT_XLAT_SEQ_C3)
  st->prog = CtXlat_seq_c3;
#elif !defined(CT_XLAT_NO_GWARP)
  st->prog = CtXlat_gwarp_fd;
#else
  (void)warp; st->prog = NULL;                                       // never opened: CtXlatStageHas says no
#endif
  c->a = seqId; c->x = 0x06FF; c->y = hostY; c->sp = 0x06FF; c->dp = 0x0100; c->db = 0x00;
  c->k = 0xC3; c->pc = 0x0000;
  c->e = false; c->mf = true; c->xf = false; c->i = true; c->d = false;
  c->n = (seqId & 0x80) != 0; c->z = seqId == 0; c->v = false; c->c = false;
  if (warp) {
    // C0/3FDC `BMI ->3FE4`, then `CMP #80 / BNE`, `CMP #81 / BNE`, `CMP #82 / BNE` and the JML of the one that matched
    // (3FE8 $FD:FFF1, 3FF0 $FD:FFEB, 3FF8 $FD:FFE8), else 3FFC's $FD:FFE5 -- the flags are the last CMP's.
    static const uint16_t ent[4] = { 0xFFF1, 0xFFEB, 0xFFE8, 0xFFE5 };
    int i = seqId <= 0x82 ? seqId - 0x80 : 3;
    c->k = 0xFD; c->pc = ent[i];
    c->n = false; c->z = i < 3; c->c = true;
  }
  c->waiting = false; c->stopped = false; c->irqWanted = false; c->nmiWanted = false; c->intWanted = false;
  c->resetWanted = false;
  s->vPos = warp ? CT_XLAT_GWARP_V : CT_XLAT_HANDOFF_V; s->hPos = warp ? CT_XLAT_GWARP_H : CT_XLAT_HANDOFF_H;
  s->cycles = (s->cycles & ~7ull) + (warp ? CT_XLAT_GWARP_CYC8 : CT_XLAT_HANDOFF_CYC8);
  s->inVblank = true; s->inNmi = false; s->nmiEnabled = false;
  s->autoJoyRead = false; s->autoJoyTimer = 0;
  s->fastMem = true;                                 // $420D = 1, the field boot's
  s->openBus = warp ? CT_XLAT_GWARP_BUS : CT_XLAT_HANDOFF_BUS;
  s->ramAdr = warp ? CT_XLAT_GWARP_WMADD : CT_XLAT_HANDOFF_WMADD;
  s->ppu->evenFrame = warp ? CT_XLAT_GWARP_EVEN : CT_XLAT_HANDOFF_EVEN;
  st->x.snes = s; st->x.cpu = c; st->x.hook = stageHook; st->x.user = st;
  st->x.interp = getenv("CT_XLAT_STRICT") ? 0 : 1;   // a hosted stage keeps running through a path no trace took
  st->frame0 = s->frames; st->stopFrame = s->frames;  // already inside this vblank: the first stop is the next one
  st->inFrame = 0;
  return st;
}

int CtXlatStageFrame(CtXlatStage* st) {
  if (!st || st->done) return st ? st->done : -1;
  // THE RULE (CLAUDE.md's exception, the zelda3 model): the stage's C3/02DD reads the VIRTUAL beam exactly as the
  // oracle's does (g_ctVirtualBeam = 2: virtual only while the CPU stands inside C3/02DD-0306 -- the translation keeps
  // c->pc exact at every instruction, X_BOUNDARY). Every other beam read of the stage stays real.
  // ...and runs by LOGICAL frames with its NMI-off stretches at phase 0 (snes.c snes_logicalResume -- the oracle's
  // CT_ORACLE_LOGICAL): a main-loop iteration that overruns its frame holds the beam at line 224 until the CPU waits
  // (the race's C3/03xx-05xx iterations, released at 7E:5214), rolled back to real time past one frame of CPU work.
  int vbWas = g_ctVirtualBeam; g_ctVirtualBeam = 2;
  int lfWas = g_ctLogicalFrames; if (st->rule) snes_logicalResume(1);
  int r = st->prog(&st->x);
  if (st->rule) snes_logicalResume(lfWas);
  g_ctVirtualBeam = vbWas;
  // CT_XLAT_FRAMETRACE=1: where the program is at each frame's stop, and the few bytes a stuck stage is usually waiting on.
  { static int ft = -1; if (ft < 0) ft = getenv("CT_XLAT_FRAMETRACE") ? 1 : 0;
    if (ft) { const Cpu* c = st->x.cpu; const uint8_t* m = st->hw->ram;
      fprintf(stderr, "xlatframe %ld: %02X:%04X A=%04X X=%04X S=%04X P(i)=%d nmiEn=%d $0000=%02X $0500=%02X%02X%02X%02X $2100sh=%02X r=%d\n",
              (long)(st->hw->frames - st->frame0), c->k, c->pc, c->a, c->x, c->sp, c->i, st->hw->nmiEnabled,
              m[0], m[0x500], m[0x501], m[0x502], m[0x503], m[0x38D], r); } }
  // snes_runFrame ends each frame with the APU caught up; a hosted SPC produces its samples from here.
  { int n = (int)st->hw->apuCatchupCycles; int ran = apu_runCycles(st->hw->apu, n);
    st->hw->apuCatchupCycles -= (double)ran; }
  // CT_XLAT_APUTRACE=<n>: every n calls, the calls so far, the hardware's frames and its APU cycles -- a hosted SPC must
  // get 17088 a frame (534 samples) for its output to stay level with the picture.
  { static int every = -2; static long calls = 0;
    if (every == -2) { const char* e = getenv("CT_XLAT_APUTRACE"); every = e ? atoi(e) : -1; }
    calls++;
    if (every > 0 && calls % every == 0)
      fprintf(stderr, "xlatapu calls=%ld frames=%ld apucycles=%u master=%llu\n", calls, (long)(st->hw->frames - st->frame0),
              (unsigned)st->hw->apu->cycles, (unsigned long long)st->hw->cycles); }
  if (r == 1) return 0;
  st->done = (r == CT_XLAT_EXIT) ? 1 : r;
  return st->done;
}

static Snes* g_callHw;      // one hardware for every call: the ROM, a PPU whose counters the routine latches, no APU use

// ---- the field's HDMA table builder FD/C2C1 (seq_xlat.h: CtXlatHdmaBuild) ---------------------------------------------
// The translation, kept in ct.exe only (CT_XLAT_HDMA_REF) as the reference --hdmafuzz grades the native builder against
// (src/hdma_table.c, the one every caller runs). Belthasar does not link it.
#ifdef CT_XLAT_HDMA_REF
int CtXlatHdmaBuild(const uint8_t* rom, long romLen, uint8_t* wram128) {
  if (!rom || !wram128) return -1;
  if (!g_callHw) {
    g_callHw = snes_init();
    if (!g_callHw || !snes_loadRom(g_callHw, rom, (int)romLen)) { g_callHw = NULL; return -1; }
  }
  Snes* s = g_callHw; Cpu* c = s->cpu;
  memcpy(s->ram, wram128, 0x20000);
  // The main loop's call, C0/00C7 `JSL FD/C2C1` (S 06FD -> 06FA, pushing C0 00 CA): the RTL lands on C0:00CB, an exit. The
  // routine reads no register it does not set first (`LDA $53` / `LDA $26` / `ASL A / TAX`; its handlers load X and Y), so
  // the call site does not change what it builds -- every caller has D = $0100, B = 0, M = X = 1 (--cputrace).
  s->ram[0x06FD] = 0xC0; s->ram[0x06FC] = 0x00; s->ram[0x06FB] = 0xCA;
  c->a = 0x0080; c->x = 0x0000; c->y = 0x0000; c->sp = 0x06FA; c->dp = 0x0100; c->db = 0x00;
  c->k = 0xFD; c->pc = 0xC2C1;
  c->e = false; c->mf = true; c->xf = true; c->i = false; c->d = false;
  c->n = true; c->v = false; c->z = false; c->c = false;
  c->waiting = false; c->stopped = false; c->irqWanted = false; c->nmiWanted = false; c->intWanted = false;
  c->resetWanted = false;
  s->inNmi = false; s->nmiEnabled = false;                          // no interrupt inside the routine
  s->autoJoyRead = false; s->autoJoyTimer = 0;
  s->fastMem = true; s->openBus = 0xFD;
  CtXlat x; memset(&x, 0, sizeof x);
  x.snes = s; x.cpu = c; x.hook = NULL; x.user = NULL;
  x.interp = 1;                     // a path no trace took (a mode handler, a window case) runs on the same core
  int r = CtXlat_hdma_fd(&x);
  if (r != CT_XLAT_EXIT) return r ? r : -2;
  // Everything but the stack page: the call frame above and the routine's own pushes are this call's, not the caller's
  // (the cartridge's stack sits where its caller's S was -- $06D5 in the battle's tail NMI, $06FA in the main loop).
  memcpy(wram128, s->ram, 0x0600); memcpy(wram128 + 0x0700, s->ram + 0x0700, 0x20000 - 0x0700);
  return 0;
}
#endif

Snes* CtXlatStageHardware(CtXlatStage* st) { return st ? st->hw : NULL; }
void CtXlatStageSetRule(CtXlatStage* st, int on) { if (st) { st->rule = on; if (on) snes_logicalFresh(); } }
const uint8_t* CtXlatStageWram(const CtXlatStage* st) { return st ? st->hw->ram : NULL; }
long CtXlatStageFrames(const CtXlatStage* st) { return st ? (long)(st->hw->frames - st->frame0) : 0; }
uint32_t CtXlatStageFailPc(const CtXlatStage* st) { return st ? st->x.failPc : 0; }
void CtXlatStageFailBytes(const CtXlatStage* st, uint8_t* want, uint8_t* got) {
  if (want) *want = st ? st->x.failWant : 0;
  if (got) *got = st ? st->x.failGot : 0;
}

void CtXlatStagePicture(const CtXlatStage* st, unsigned char* rgb, int w, int h) {
  if (!st || !rgb) return;
  const Ppu* p = st->hw->ppu;
  // ppu_runLine writes line L at pixelBuffer row (L - 1) + (evenFrame ? 0 : 239), eight bytes a column, the main
  // screen's B, G, R at +4..+6 past the output-format offset (ppu_putPixels' non-interlaced row choice).
  for (int y = 0; y < h && y < 224; y++) {
    const uint8_t* row = &p->pixelBuffer[(y + (p->evenFrame ? 0 : 239)) * 2048];
    for (int x = 0; x < w && x < 256; x++) {
      const uint8_t* q = row + x * 8 + 4 + p->pixelOutputFormat;
      rgb[(y * w + x) * 3 + 0] = q[2];
      rgb[(y * w + x) * 3 + 1] = q[1];
      rgb[(y * w + x) * 3 + 2] = q[0];
    }
  }
}

void CtXlatStageClose(CtXlatStage* st) {
  if (!st) return;
  if (st->hw) {
    if (st->ownApu) st->hw->apu = st->ownApu;       // the host's SPC goes back untouched by snes_free
    snes_free(st->hw);
  }
  free(st);
}
