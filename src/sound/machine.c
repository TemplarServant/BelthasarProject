// sound/machine.c -- SndMachine (machine.h): the sound program on its clock, the timers, the ports, the S-DSP.
#include "sound/machine.h"

#include <stdlib.h>
#include <string.h>

#include "sound/coro.h"

// --- the S-DSP: brought up to an access's cycle (apu.c: the S-DSP steps at each cycle divisible by 32, before the
// access made on the next cycle) ---------------------------------------------------------------------------------------
static void mixTo(SndMachine* m, uint64_t count) {
  int16_t buf[256 * 2];
  while (m->mixed < count) {
    int n = (int)((count - m->mixed) > 256 ? 256 : (count - m->mixed));
    if (m->sinkPerSample) n = 1;
    ct_mixer_render(&m->mx, buf, n);
    for (int i = 0; i < n; i++) {
      uint32_t r = m->newestPos++ & 0x3FF;
      m->newest[r * 2] = buf[i * 2]; m->newest[r * 2 + 1] = buf[i * 2 + 1];
    }
    if (m->sink) m->sink(m->sinkCtx, buf, n);
    m->mixed += (uint64_t)n;
  }
}
static void mixToIo(SndMachine* m) { mixTo(m, (m->d.clk.io + 31) / 32); }

// --- the timers: the prescaler steps at the cycles before `upto` (apu.c apu_cycle) ----------------------------------
static void tAdv(SndTimer* t, int i, uint64_t upto) {
  uint64_t per = i == 2 ? 16 : 128;
  if (t->next >= upto) return;
  if (!t->enabled) { t->next += (upto - t->next + per - 1) / per * per; return; }
  while (t->next < upto) {
    t->divider++;
    if (t->divider == t->target) { t->divider = 0; t->counter = (uint8_t)((t->counter + 1) & 0x0F); }
    t->next += per;
  }
}

// --- the program's outside --------------------------------------------------------------------------------------------
#define OBS(k, a, v) do { if (m->obs) m->obs(m->obsCtx, (k), (uint8_t)(a), (uint8_t)(v), m->d.clk.io); } while (0)
static void hDspWrite(void* c, uint8_t reg, uint8_t val) {
  SndMachine* m = (SndMachine*)c;
  mixToIo(m);
  ct_mixer_write(&m->mx, reg, val);
  OBS('D', reg, val);
}
static uint8_t hDspRead(void* c, uint8_t reg) {
  SndMachine* m = (SndMachine*)c;
  mixToIo(m);
  uint8_t v = ct_mixer_read(&m->mx, reg);
  OBS('d', reg, v);
  return v;
}
static uint8_t hPortRead(void* c, int k) { SndMachine* m = (SndMachine*)c; uint8_t v = m->in[k & 3]; OBS('p', k, v); return v; }
static void hPortWrite(void* c, int k, uint8_t v) { SndMachine* m = (SndMachine*)c; m->out[k & 3] = v; OBS('P', k, v); }
static uint8_t hTimerRead(void* c, int k) {
  SndMachine* m = (SndMachine*)c;
  SndTimer* t = &m->t[k];
  tAdv(t, k, m->d.clk.io);
  uint8_t v = t->counter;
  t->counter = 0;
  OBS('t', k, v);
  return v;
}
static void hControl(void* c, uint8_t reg, uint8_t val) {
  SndMachine* m = (SndMachine*)c;
  uint64_t io = m->d.clk.io;
  for (int i = 0; i < 3; i++) tAdv(&m->t[i], i, io);
  if (reg == 0xF1) {
    for (int i = 0; i < 3; i++) {
      SndTimer* t = &m->t[i];
      if (!t->enabled && (val & (1 << i))) { t->divider = 0; t->counter = 0; }
      t->enabled = (val >> i) & 1;
    }
    if (val & 0x10) { m->in[0] = 0; m->in[1] = 0; }
    if (val & 0x20) { m->in[2] = 0; m->in[3] = 0; }
    m->romReadable = (uint8_t)(val >> 7);
  } else if (reg >= 0xFA && reg <= 0xFC) m->t[reg - 0xFA].target = val;
  OBS('C', reg, val);
}
static void hRamWrite(void* c, uint16_t adr, uint8_t val) {
  SndMachine* m = (SndMachine*)c;
  mixToIo(m);
  if (m->ramObs) m->ramObs(m->obsCtx, adr, val, m->d.clk.io);
}

static void onBoundary(void* c) { SndCoroYield(((SndMachine*)c)->co); }
static void hStuck(void* c) {   // nothing more runs: the time passes
  SndMachine* m = (SndMachine*)c;
  for (;;) { if (m->d.clk.cyc < m->d.clk.want) m->d.clk.cyc = m->d.clk.want; SndCoroYield(m->co); }
}

static void progMain(void* p) { SndDrvProgram(&((SndMachine*)p)->d); }

static void startProgram(SndMachine* m, int entry, uint64_t cyc, uint16_t ip) {
  if (m->co) SndCoroFree(m->co);
  SndClock* c = &m->d.clk;
  c->cyc = cyc; c->ip = ip; c->want = 0; c->io = 0; c->err = 0;
  c->boundary = onBoundary; c->boundaryCtx = m;
  m->d.entry = entry;
  m->co = SndCoroNew(progMain, m);
  m->started = 1;
}

SndMachine* SndMachineNew(const SndTiming* tim) {
  SndMachine* m = (SndMachine*)calloc(1, sizeof *m);
  if (!m) return NULL;
  SndDrvHost h;
  memset(&h, 0, sizeof h);
  h.ctx = m;
  h.dspWrite = hDspWrite; h.dspRead = hDspRead; h.portRead = hPortRead; h.portWrite = hPortWrite;
  h.timerRead = hTimerRead; h.control = hControl; h.ramWrite = hRamWrite; h.stuck = hStuck;
  SndDrvInit(&m->d, &h);
  m->d.clk.tim = tim;
  SndMachinePowerOn(m);
  return m;
}
void SndMachineFree(SndMachine* m) { if (!m) return; SndCoroFree(m->co); free(m); }

void SndMachinePowerOn(SndMachine* m) {
  memset(m->d.m, 0, sizeof m->d.m);
  ct_mixer_init(&m->mx, m->d.m);
  memset(m->t, 0, sizeof m->t);
  memset(m->in, 0, sizeof m->in); memset(m->out, 0, sizeof m->out);
  m->romReadable = 1;
  m->mixed = 0;
  memset(m->newest, 0, sizeof m->newest); m->newestPos = 0;
  startProgram(m, SND_ENTRY_RESET, 0, 0xFFC0);   // spc.c's reset (8 cycles), then the boot ROM
}

void SndMachineAdopt(SndMachine* m, const uint8_t* ram, const CtMixer* mx, const SndMachineState* st) {
  memcpy(m->d.m, ram, sizeof m->d.m);
  m->mx = *mx; m->mx.aram = m->d.m;
  memcpy(m->t, st->t, sizeof m->t);
  memcpy(m->in, st->in, 4); memcpy(m->out, st->out, 4);
  m->romReadable = st->romReadable;
  m->mixed = (st->cycles + 31) / 32;
  startProgram(m, SND_ENTRY_LOOP, st->cycles, 0x0299);
}

void SndMachineSetSink(SndMachine* m, void (*sink)(void*, const int16_t*, int), void* ctx) { m->sink = sink; m->sinkCtx = ctx; }

int SndMachineRun(SndMachine* m, int cycles) {
  if (!m || cycles <= 0) return 0;
  SndClock* c = &m->d.clk;
  uint64_t start = c->cyc;
  c->want = start + (uint64_t)cycles;
  SndCoroResume(m->co);
  mixTo(m, (c->cyc + 31) / 32);
  return (int)(c->cyc - start);
}

void SndMachinePortWrite(SndMachine* m, int k, uint8_t v) { m->in[k & 3] = v; }
uint8_t SndMachinePortRead(const SndMachine* m, int k) { return m->out[k & 3]; }
uint64_t SndMachineCycles(const SndMachine* m) { return m->d.clk.cyc; }
void SndMachineSync(SndMachine* m) { mixTo(m, (m->d.clk.cyc + 31) / 32); }
void SndMachineNewest(const SndMachine* m, int16_t* out, int n) {
  for (int i = 0; i < n; i++) {
    uint32_t r = (m->newestPos - (uint32_t)n + (uint32_t)i) & 0x3FF;
    out[i * 2] = m->newest[r * 2]; out[i * 2 + 1] = m->newest[r * 2 + 1];
  }
}
int SndMachineVoicesSounding(const SndMachine* m) {
  int n = 0;
  for (int i = 0; i < 8; i++) if (m->mx.v[i].env) n++;
  return n;
}
void SndMachineSetTrace(SndMachine* m, void (*trace)(void*, uint16_t, uint64_t), void* ctx) {
  m->d.clk.trace = trace; m->d.clk.traceCtx = ctx;
}
void SndMachineSetObserver(SndMachine* m, void (*obs)(void*, char, uint8_t, uint8_t, uint64_t), void* ctx) {
  m->obs = obs; m->obsCtx = ctx;
}
