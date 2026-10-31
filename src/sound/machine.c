// sound/machine.c -- SndMachine (machine.h): the sound program on its clock, the timers, the ports, the S-DSP.
#include "sound/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound/coro.h"

// The debug traces snes/dsp.c keeps (CT_DSP_KONTRACE, CT_DSP_RAWPCM, CT_DSP_VOICEMASK), the same for the machine's S-DSP:
// one sample count a process, the same file, the same line.
extern unsigned long ctDspSamples;
void dsp_ctRawPcm(int16_t l, int16_t r);
static int envVoiceMask(void) {
  static int vm = -2;
  if (vm == -2) { const char* e = getenv("CT_DSP_VOICEMASK"); vm = e ? (int)(strtol(e, NULL, 16) & 0xFF) : -1; }
  return vm;
}
static int envKonTrace(void) { static int k = -1; if (k < 0) k = getenv("CT_DSP_KONTRACE") ? 1 : 0; return k; }

// ---- the S-DSP: brought up to an access's cycle ----
// (apu.c: the S-DSP steps at each cycle divisible by 32, before the access made on the next cycle)
static void mixTo(SndMachine* m, uint64_t count) {
  int16_t buf[256 * 2];
  while (m->mixed < count) {
    int n = (int)((count - m->mixed) > 256 ? 256 : (count - m->mixed));
    if (m->sinkPerSample) n = 1;
    ct_mixer_render(&m->mx, buf, n);
    for (int i = 0; i < n; i++) {
      uint32_t r = m->newestPos++ & 0x3FF;
      m->newest[r * 2] = buf[i * 2]; m->newest[r * 2 + 1] = buf[i * 2 + 1];
      if (m->traces && !m->replaying) { ctDspSamples++; dsp_ctRawPcm(buf[i * 2], buf[i * 2 + 1]); }
    }
    if (m->sink) m->sink(m->sinkCtx, buf, n);
    m->mixed += (uint64_t)n;
  }
}
static void mixToIo(SndMachine* m) { mixTo(m, (m->d.clk.io + 31) / 32); }

// ---- the timers: the prescaler steps at the cycles before `upto` (apu.c apu_cycle) ----
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

// ---- the program's outside ----
#define OBS(k, a, v) do { if (m->obs) m->obs(m->obsCtx, (k), (uint8_t)(a), (uint8_t)(v), m->d.clk.io); } while (0)
static void hDspWrite(void* c, uint8_t reg, uint8_t val) {
  SndMachine* m = (SndMachine*)c;
  mixToIo(m);
  ct_mixer_write(&m->mx, reg, val);
  if (reg == 0x4C && val && m->traces && envKonTrace() && !m->replaying) {   // snes/dsp.c's line
    fprintf(stderr, "dspkon s=%lu f=%lu val=%02X", ctDspSamples, ctDspSamples / 534, val);
    for (int i = 0; i < 8; i++) if (val & (1 << i)) fprintf(stderr, " v%d:srcn=%02X", i, m->mx.v[i].srcn);
    fputc(10, stderr);
  }
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

// ---- the savestate's restore point and record (machine.h SndRestore) ----
static void logPush(SndMachine* m, uint8_t kind, uint64_t t, uint8_t k, uint8_t v) {
  if (m->logN == m->logCap) {
    long cap = m->logCap ? m->logCap * 2 : 256;
    SndLogEntry* l = (SndLogEntry*)realloc(m->log, (size_t)cap * sizeof *l);
    if (!l) return;
    m->log = l; m->logCap = cap;
  }
  SndLogEntry e = { t, kind, k, v };
  m->log[m->logN++] = e;
}
static void restoreTake(SndMachine* m, int kind) {
  SndRestore* r = m->restore;
  if (!r) return;
  r->kind = kind;
  r->cycles = m->d.clk.cyc; r->mixed = m->mixed;
  memcpy(r->ram, m->d.m, sizeof r->ram);
  r->mx = m->mx;
  memcpy(r->t, m->t, sizeof r->t);
  memcpy(r->in, m->in, 4); memcpy(r->out, m->out, 4); r->romReadable = m->romReadable;
  memcpy(r->newest, m->newest, sizeof r->newest); r->newestPos = m->newestPos;
}
static void onLoopTop(void* c) {   // (inside the run: the record restarts with the run under way)
  SndMachine* m = (SndMachine*)c;
  if (m->logN < 64 || m->replaying) return;
  mixTo(m, (m->d.clk.cyc + 31) / 32);
  restoreTake(m, 1);
  m->log[0] = m->log[m->logN - 1];
  m->logN = 1;
}

static void startProgram(SndMachine* m, int entry, uint64_t cyc, uint16_t ip) {
  if (m->co) SndCoroFree(m->co);
  SndClock* c = &m->d.clk;
  c->cyc = cyc; c->ip = ip; c->want = 0; c->io = 0; c->err = 0;
  c->boundary = onBoundary; c->boundaryCtx = m;
  c->loopTop = onLoopTop; c->loopTopCtx = m;
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
  m->restore = (SndRestore*)calloc(1, sizeof *m->restore);
  SndMachinePowerOn(m);
  return m;
}
void SndMachineFree(SndMachine* m) { if (!m) return; SndCoroFree(m->co); free(m->restore); free(m->log); free(m); }

void SndMachinePowerOn(SndMachine* m) {
  memset(m->d.m, 0, sizeof m->d.m);
  ct_mixer_init(&m->mx, m->d.m);
  if (envVoiceMask() >= 0) m->mx.voiceMask = (uint8_t)envVoiceMask();
  memset(m->t, 0, sizeof m->t);
  memset(m->in, 0, sizeof m->in); memset(m->out, 0, sizeof m->out);
  m->romReadable = 1;
  m->mixed = 0;
  memset(m->newest, 0, sizeof m->newest); m->newestPos = 0;
  startProgram(m, SND_ENTRY_RESET, 0, 0xFFC0);   // spc.c's reset (8 cycles), then the boot ROM
  restoreTake(m, 0);
  m->logN = 0;
}

void SndMachineAdopt(SndMachine* m, const uint8_t* ram, const CtMixer* mx, const SndMachineState* st) {
  memcpy(m->d.m, ram, sizeof m->d.m);
  m->mx = *mx; m->mx.aram = m->d.m;
  if (envVoiceMask() >= 0) m->mx.voiceMask = (uint8_t)envVoiceMask();
  memcpy(m->t, st->t, sizeof m->t);
  memcpy(m->in, st->in, 4); memcpy(m->out, st->out, 4);
  m->romReadable = st->romReadable;
  m->mixed = (st->cycles + 31) / 32;
  startProgram(m, SND_ENTRY_LOOP, st->cycles, 0x0299);
  restoreTake(m, 1);
  m->logN = 0;
}

void SndMachineSetSink(SndMachine* m, void (*sink)(void*, const int16_t*, int), void* ctx) { m->sink = sink; m->sinkCtx = ctx; }

int SndMachineRun(SndMachine* m, int cycles) {
  if (!m || cycles <= 0) return 0;
  SndClock* c = &m->d.clk;
  uint64_t start = c->cyc;
  c->want = start + (uint64_t)cycles;
  logPush(m, 'R', c->want, 0, 0);
  SndCoroResume(m->co);
  mixTo(m, (c->cyc + 31) / 32);
  return (int)(c->cyc - start);
}

void SndMachinePortWrite(SndMachine* m, int k, uint8_t v) { m->in[k & 3] = v; logPush(m, 'W', 0, (uint8_t)(k & 3), v); }

// The savestate: "CTSM", version, the restore point, the record, the position it leads to.
#define SND_SAVE_MAGIC 0x4D535443u
int SndMachineSave(const SndMachine* m, uint8_t** buf, long* len) {
  if (!m || !m->restore) return 0;
  long n = 8 + (long)sizeof(SndRestore) + 4 + m->logN * (long)sizeof(SndLogEntry) + 8;
  uint8_t* b = (uint8_t*)malloc((size_t)n);
  if (!b) return 0;
  uint8_t* p = b;
  uint32_t hdr[2] = { SND_SAVE_MAGIC, 1 };
  memcpy(p, hdr, 8); p += 8;
  memcpy(p, m->restore, sizeof(SndRestore)); p += sizeof(SndRestore);
  uint32_t ln = (uint32_t)m->logN; memcpy(p, &ln, 4); p += 4;
  memcpy(p, m->log, (size_t)m->logN * sizeof(SndLogEntry)); p += m->logN * (long)sizeof(SndLogEntry);
  uint64_t at = m->d.clk.cyc; memcpy(p, &at, 8);
  *buf = b; *len = n;
  return 1;
}
int SndMachineLoad(SndMachine* m, const uint8_t* buf, long len) {
  uint32_t hdr[2], ln;
  if (!m || !buf || len < 8 + (long)sizeof(SndRestore) + 12) return 0;
  memcpy(hdr, buf, 8);
  if (hdr[0] != SND_SAVE_MAGIC || hdr[1] != 1) return 0;
  const uint8_t* p = buf + 8;
  SndRestore* r = (SndRestore*)malloc(sizeof *r);
  if (!r) return 0;
  memcpy(r, p, sizeof *r); p += sizeof *r;
  memcpy(&ln, p, 4); p += 4;
  if (len != 8 + (long)sizeof(SndRestore) + 4 + (long)ln * (long)sizeof(SndLogEntry) + 8) { free(r); return 0; }
  const SndLogEntry* lg = (const SndLogEntry*)p;
  uint64_t at; memcpy(&at, p + ln * sizeof(SndLogEntry), 8);
  // the restore point
  if (r->kind == 0) SndMachinePowerOn(m);
  else {
    memcpy(m->d.m, r->ram, sizeof m->d.m);
    m->mx = r->mx; m->mx.aram = m->d.m;
    memcpy(m->t, r->t, sizeof m->t);
    memcpy(m->in, r->in, 4); memcpy(m->out, r->out, 4); m->romReadable = r->romReadable;
    m->mixed = r->mixed;
    memcpy(m->newest, r->newest, sizeof m->newest); m->newestPos = r->newestPos;
    startProgram(m, SND_ENTRY_LOOP, r->cycles, 0x0299);
    restoreTake(m, 1);
    m->logN = 0;
  }
  free(r);
  // the record, replayed silent and unobserved
  void (*sink)(void*, const int16_t*, int) = m->sink;
  void (*tr)(void*, uint16_t, uint64_t) = m->d.clk.trace;
  void (*ob)(void*, char, uint8_t, uint8_t, uint64_t) = m->obs;
  void (*ro)(void*, uint16_t, uint8_t, uint64_t) = m->ramObs;
  m->sink = NULL; m->d.clk.trace = NULL; m->obs = NULL; m->ramObs = NULL;
  m->replaying = 1;
  for (uint32_t i = 0; i < ln; i++) {
    if (lg[i].kind == 'W') SndMachinePortWrite(m, lg[i].k, lg[i].v);
    else if (lg[i].kind == 'R' && lg[i].t > m->d.clk.cyc) SndMachineRun(m, (int)(lg[i].t - m->d.clk.cyc));
  }
  m->replaying = 0;
  m->sink = sink; m->d.clk.trace = tr; m->obs = ob; m->ramObs = ro;
  return m->d.clk.cyc == at;
}
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
