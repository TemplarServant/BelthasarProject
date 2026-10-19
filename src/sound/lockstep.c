// ============================================================================
// sound/lockstep.c -- the sound machine against the original on the vendored SPC700 and S-DSP, instruction by
// instruction (lockstep.h).
//
// THE ORIGINAL'S SIDE: four hooks on the vendored core (snes/apu.c) queue what it does as it does it -- each instruction
// about to run (its address and cycle), each S-DSP write, each read of an S-DSP register, a port or a timer, each write of
// a port or a control register (the SPC700's dummy read before a store is dropped: the store is the access). The host
// runs the original first, then the machine to the same time (src/audio.c); the machine's trace and observer take the
// queue's events in order and every field must be equal. At the run's end the queue is empty, both stopped on the same
// cycle, the ports equal.
//
// A mismatch is reported (the events before it on both sides) and the machine waits for the original's next $0299 to
// join again with its state, so a run counts every stretch.
// ============================================================================
#include "sound/lockstep.h"
#include "sound/machine.h"
#include "sound/mixer_dsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "apu.h"
#include "dsp.h"
#include "spc.h"

typedef struct { char k; uint8_t a, v; uint16_t pc; uint32_t cyc; } LsEv;   // 'O' an instruction (pc), else an access
typedef struct { LsEv* v; long head, n, cap; } LsQ;
typedef struct { int16_t* v; uint8_t* t; long head, n, cap; uint64_t first; } LsS;   // samples (`first` = head's index), t: tainted

typedef struct {
  Apu* apu;
  SndMachine* m;
  int active, pending;
  uint16_t curPc;       // the original's instruction running now
  LsQ q;                // the original's events not yet matched
  LsS vs, ms;           // the samples: the original's, the machine's
  LsEv histV[24], histM[24]; int hn;
  uint64_t joinedAt;
  uint16_t prevPc;
  int activeAtStart;    // the machine ran from the run's start
  // the RAM at the main loop's top after each tick, the song start and the boot: the original's, waiting for the
  // machine to reach the same instruction (both programs are then between two of their statements)
  struct { uint32_t cyc; uint8_t* ram; } snap[8];
  int snapHead, snapN;
} LsSession;

static LsSession g_s[8];
static long g_rwAdr = -2;
static uint32_t* g_rwV; static long g_rwVn, g_rwVcap; static uint64_t g_rwVfirst;
static int g_on;
static long g_mismatch, g_ops, g_acc, g_samples, g_ramChecks, g_runs, g_joins, g_details, g_drvRam;
static FILE* g_out;
static int g_reported;
static void (*g_prevOp)(Apu*, uint16_t);
static void (*g_prevDspW)(Apu*, uint8_t, uint8_t);
static void (*g_prevRd)(Apu*, uint16_t, uint8_t);
static void (*g_prevWr)(Apu*, uint16_t, uint8_t);

static LsSession* find(const Apu* apu) {
  for (int i = 0; i < 8; i++) if (g_s[i].apu == apu && apu) return &g_s[i];
  return NULL;
}

static void say(const char* fmt, const char* a) {
  printf(fmt, a); if (g_out) { fprintf(g_out, fmt, a); fflush(g_out); }
}
static void evStr(char* b, size_t n, const LsEv* e) {
  if (e->k == 'O') snprintf(b, n, "%04X@%u", e->pc, e->cyc);
  else if (e->k == 'R') snprintf(b, n, "R%04X=%02X@%u", e->pc, e->v, e->cyc);
  else snprintf(b, n, "%c%02X=%02X@%u", e->k, e->a, e->v, e->cyc);
}

static void qPush(LsQ* q, LsEv e) {
  if (q->head + q->n == q->cap) {
    if (q->head) { memmove(q->v, q->v + q->head, (size_t)q->n * sizeof *q->v); q->head = 0; }
    if (q->n == q->cap) {
      long cap = q->cap ? q->cap * 2 : 65536;
      LsEv* v = (LsEv*)realloc(q->v, (size_t)cap * sizeof *v);
      if (!v) return;
      q->v = v; q->cap = cap;
    }
  }
  q->v[q->head + q->n++] = e;
}
static void sPush(LsS* s, const int16_t* lr, int n, int taint) {
  if (s->head + s->n + n > s->cap) {
    if (s->head) { memmove(s->v, s->v + 2 * s->head, (size_t)s->n * 4); memmove(s->t, s->t + s->head, (size_t)s->n); s->head = 0; }
    if (s->n + n > s->cap) {
      long cap = s->cap ? s->cap : 65536;
      while (cap < s->n + n) cap *= 2;
      int16_t* v = (int16_t*)realloc(s->v, (size_t)cap * 4);
      uint8_t* t = (uint8_t*)realloc(s->t, (size_t)cap);
      if (v) s->v = v;
      if (t) s->t = t;
      if (!v || !t) return;
      s->cap = cap;
    }
  }
  memcpy(s->v + 2 * (s->head + s->n), lr, (size_t)n * 4);
  memset(s->t + s->head + s->n, taint, (size_t)n);
  s->n += n;
}
static void sDrop(LsS* s, long n) { if (n > s->n) n = s->n; s->head += n; s->n -= n; s->first += (uint64_t)n; if (!s->n) s->head = 0; }

// --- the mismatch ----------------------------------------------------------------------------------------------------
static void mismatch(LsSession* s, const char* what, const LsEv* v, const LsEv* m) {
  g_mismatch++;
  if (g_details++ < 4) {
    char a[64] = "-", b[64] = "-", line[600];
    if (v) evStr(a, sizeof a, v);
    if (m) evStr(b, sizeof b, m);
    snprintf(line, sizeof line, "snddrv-lockstep: MISMATCH %s: original %s, machine %s (machine joined at %llu; err %d at %04X->%04X)\n",
             what, a, b, (unsigned long long)s->joinedAt, s->m->d.clk.err, s->m->d.clk.errAt, s->m->d.clk.errTo);
    say("%s", line);
    char h[1600]; size_t o = 0;
    o += (size_t)snprintf(h + o, sizeof h - o, "  before it (original | machine):");
    for (int i = 0; i < 24 && i < s->hn; i++) {
      int r = (s->hn - 24 + i + 24 * 100) % 24;
      if (s->hn < 24) r = i;
      char x[48], y[48];
      evStr(x, sizeof x, &s->histV[r]); evStr(y, sizeof y, &s->histM[r]);
      if (o < sizeof h - 64) o += (size_t)snprintf(h + o, sizeof h - o, " %s|%s", x, y);
    }
    snprintf(h + o, sizeof h - o, "\n");
    say("%s", h);
  }
  s->active = 0; s->pending = 1;   // join again at the original's next $0299
  s->q.n = 0; s->q.head = 0; s->snapN = 0;
  s->vs.n = s->vs.head = 0; s->ms.n = s->ms.head = 0;
  SndMachineSetTrace(s->m, NULL, NULL);
  SndMachineSetObserver(s->m, NULL, NULL); s->m->ramObs = NULL;
}

static void cmp(LsSession* s, const LsEv* m) {
  if (!s->active) return;
  if (!s->q.n) { mismatch(s, "the machine made one more", NULL, m); return; }
  LsEv v = s->q.v[s->q.head];
  s->q.head++; s->q.n--;
  if (!s->q.n) s->q.head = 0;
  s->histV[s->hn % 24] = v; s->histM[s->hn % 24] = *m; s->hn++;
  if (v.k != m->k || v.cyc != m->cyc || (v.k == 'O' ? v.pc != m->pc : v.k == 'R' ? (v.pc != m->pc || v.v != m->v) : (v.a != m->a || v.v != m->v))) {
    mismatch(s, v.k != m->k ? "a different event" : v.cyc != m->cyc ? "a different cycle" : "a different value", &v, m);
    return;
  }
  if (v.k == 'O') g_ops++; else g_acc++;
}

// --- the machine's side ----------------------------------------------------------------------------------------------
static void ramCmp(LsSession* s, const uint8_t* a, uint32_t cyc);
static void mTrace(void* c, uint16_t pc, uint64_t cyc) {
  LsSession* s = (LsSession*)c;
  LsEv e = { 'O', 0, 0, pc, (uint32_t)cyc };
  cmp(s, &e);
  if (pc == 0x0299 && s->active && s->snapN && s->snap[s->snapHead].cyc == (uint32_t)cyc) {
    SndMachineSync(s->m);   // (its S-DSP up to this cycle: the echo buffer)
    ramCmp(s, s->snap[s->snapHead].ram, (uint32_t)cyc);
    s->snapHead = (s->snapHead + 1) % 8; s->snapN--;
  }
}
static void mObs(void* c, char k, uint8_t a, uint8_t v, uint64_t io) {
  LsEv e = { k, a, v, 0, (uint32_t)io };
  cmp((LsSession*)c, &e);
}
// The S-DSP reading the driver's own working RAM (the direct page and page 1, the fades, the queue, the state arrays and
// the save area at $F15F-$FFFF): a voice's directory entry or BRR block there, or the echo buffer over it. The original
// driver writes that RAM at its cycles while the S-DSP reads it; the C program writes its state between two marks --
// the one place the machine is not exact. No game data does it: a voice keyed on an instrument whose sample is not
// loaded (its directory entry zero: the direct page played as a sample), an echo buffer set over the driver's RAM by a
// raw S-DSP write (command FD) -- the fuzz's constructed songs and random commands.
static int drvRam(unsigned a) { a &= 0xFFFF; return a < 0x200 || a >= 0xF15F; }
static int dspRam(unsigned a) { return a >= 0x1E00 && a < 0xF15F; }   // the stores compared: the samples' and the directory's
static void mRamObs(void* c, uint16_t adr, uint8_t v, uint64_t io) {
  if (!dspRam(adr)) return;
  LsEv e = { 'R', 0, v, adr, (uint32_t)io };
  cmp((LsSession*)c, &e);
}
static void mSink(void* c, const int16_t* lr, int n) {
  LsSession* s = (LsSession*)c;
  if (!s->active) return;
  const CtMixer* x = &s->m->mx;
  int taint = 0;
  for (int k = 0; k < 8; k++) {
    const CtMixerVoice* v = &x->v[k];   // (a silent one too: the header it reads there decides whether it sounds)
    unsigned dir = (unsigned)(x->dirAdr + v->srcn * 4);
    if (drvRam(v->blockAdr) || drvRam(v->blockAdr + 8u) || drvRam(dir) || drvRam(dir + 3u)) taint = 1;
  }
  unsigned elen = x->echoLength > 4 ? x->echoLength : 4;
  if (drvRam(x->echoAdr) || drvRam(x->echoAdr + elen - 1u) || (unsigned)x->echoAdr + elen > 0x10000u) taint = 1;
  { static long long lo = -2, hi = 0;   // CT_SNDLS_SAMPDUMP=<first>:<count>: the machine's voices after each of those samples
    if (lo == -2) { const char* e = getenv("CT_SNDLS_SAMPDUMP"); lo = -1; if (e) { char* q; lo = strtoll(e, &q, 10); hi = lo + (*q == ':' ? strtoll(q + 1, NULL, 10) : 1); } }
    long long idx = (long long)s->m->mixed - n;
    if (lo >= 0 && idx >= lo && idx < hi) {
      printf("samp %lld %d,%d taint %d dir %04X echo %04X:", idx, lr[0], lr[1], taint, x->dirAdr, x->echoAdr);
      for (int k = 0; k < 8; k++) printf(" v%d e%d k%d b%04X s%02X o%d", k, x->v[k].env, x->v[k].keyOn, x->v[k].blockAdr, x->v[k].srcn, x->v[k].out);
      printf("%c", 10);
    } }
  if (g_rwAdr >= 0) {   // (the machine's side of CT_SNDLS_RAMWATCH)
    uint64_t idx = s->m->mixed;
    const uint8_t* mm = s->m->d.m + g_rwAdr;
    uint32_t mv = (uint32_t)(mm[0] | mm[1] << 8 | mm[2] << 16 | (uint32_t)mm[3] << 24);
    if (idx >= g_rwVfirst && idx < g_rwVfirst + (uint64_t)g_rwVn / 9) {
      const uint32_t* vv = g_rwV + 9 * (idx - g_rwVfirst);
      if (vv[0] != mv) printf("ramwatch: sample %llu $%04lX original %08X machine %08X%c", (unsigned long long)idx, g_rwAdr, vv[0], mv, 10);
      for (int k = 0; k < 8; k++)
        if ((uint16_t)vv[1 + k] != (uint16_t)s->m->mx.v[k].out)
          printf("ramwatch: sample %llu voice %d out original %d machine %d (env %d block %04X)%c", (unsigned long long)idx, k,
                 (int16_t)vv[1 + k], s->m->mx.v[k].out, s->m->mx.v[k].env, s->m->mx.v[k].blockAdr, 10);
    }
  }
  sPush(&s->ms, lr, n, taint);
}

// The machine takes the original's state at an instruction's start (its main loop's top).
void CtSndMachineAdoptApu(SndMachine* m, const Apu* apu) {
  SndMachineState st;
  memset(&st, 0, sizeof st);
  st.cycles = apu->cycles;
  for (int k = 0; k < 4; k++) { st.in[k] = apu->inPorts[k]; st.out[k] = apu->outPorts[k]; }
  st.romReadable = apu->romReadable ? 1 : 0;
  for (int i = 0; i < 3; i++) {
    st.t[i].enabled = apu->timer[i].enabled ? 1 : 0;
    st.t[i].divider = apu->timer[i].divider; st.t[i].target = apu->timer[i].target; st.t[i].counter = apu->timer[i].counter;
    st.t[i].next = (uint64_t)apu->cycles + apu->timer[i].cycles;
  }
  CtMixer mx;
  ct_mixer_init(&mx, NULL);
  ct_mixer_from_dsp(&mx, apu->dsp);
  SndMachineAdopt(m, apu->ram, &mx, &st);
  for (int i = 0; i < 1024; i++) {   // the S-DSP's ring (dsp_getSamples' view), as it stands
    uint32_t r = (uint32_t)(apu->dsp->sampleOffset - 1024 + i) & 0x3FF;
    m->newest[(uint32_t)i * 2] = apu->dsp->sampleBuffer[r * 2]; m->newest[(uint32_t)i * 2 + 1] = apu->dsp->sampleBuffer[r * 2 + 1];
  }
  m->newestPos = 1024;
}

static void join(LsSession* s) {
  CtSndMachineAdoptApu(s->m, s->apu);
  s->pending = 0; s->active = 1;
  s->q.n = s->q.head = 0; s->snapN = 0;
  s->vs.n = s->vs.head = 0; s->ms.n = s->ms.head = 0;
  s->vs.first = s->ms.first = s->m->mixed;
  s->hn = 0;
  s->joinedAt = s->apu->cycles;
  g_rwVn = 0;
  SndMachineSetTrace(s->m, mTrace, s);
  SndMachineSetObserver(s->m, mObs, s); s->m->ramObs = mRamObs;
  SndMachineSetSink(s->m, mSink, s);
  s->m->sinkPerSample = 1;
  g_joins++;
}

// --- the original's side ---------------------------------------------------------------------------------------------
static void push(LsSession* s, char k, uint8_t a, uint8_t v, uint16_t pc, uint32_t cyc) {
  LsEv e = { k, a, v, pc, cyc };
  qPush(&s->q, e);
}
static void dropDummy(LsSession* s, char k, uint8_t a) {   // the store's own read just before it, in the same instruction
  if (!s->q.n) return;
  LsEv* e = &s->q.v[s->q.head + s->q.n - 1];
  if (e->k == k && e->a == a) s->q.n--;   // (each instruction's 'O' comes first: the last event is this instruction's)
}
// CT_SNDLS_RAMWATCH=<hex adr>: four bytes of RAM after each sample, compared between the two (a debugging aid)
extern void (*g_dspSampleHook)(Dsp* dsp, int16_t l, int16_t r);
static void (*g_prevSample)(Dsp*, int16_t, int16_t);
static void sampleHook(Dsp* dsp, int16_t l, int16_t r) {
  if (g_prevSample) g_prevSample(dsp, l, r);
  LsSession* s = find(dsp->apu);
  if (!s || !s->active || g_rwAdr < 0) return;
  if (g_rwVn + 9 > g_rwVcap) { g_rwVcap = g_rwVcap ? g_rwVcap * 2 : 1 << 16; g_rwV = (uint32_t*)realloc(g_rwV, (size_t)g_rwVcap * 4); }
  const uint8_t* m = dsp->apu->ram + g_rwAdr;
  if (!g_rwVn) g_rwVfirst = (uint64_t)dsp->apu->cycles / 32;   // (the hook runs inside apu_cycle at a multiple of 32)
  g_rwV[g_rwVn++] = (uint32_t)(m[0] | m[1] << 8 | m[2] << 16 | (uint32_t)m[3] << 24);
  for (int k = 0; k < 8; k++) g_rwV[g_rwVn++] = (uint32_t)(uint16_t)dsp->channel[k].sampleOut;
}
static void opHook(Apu* apu, uint16_t pc) {
  if (g_prevOp) g_prevOp(apu, pc);
  LsSession* s = find(apu);
  if (!s) return;
  if (s->pending && pc == 0x0299) join(s);
  if (s->active && pc == 0x0299 && (s->prevPc == 0x0374 || s->prevPc == 0x0CF3 || s->prevPc == 0x0296 || s->prevPc == 0x02E2 ||
                                    s->prevPc == 0x02F5) && s->snapN < 8) {
    int i = (s->snapHead + s->snapN) % 8;
    if (!s->snap[i].ram) s->snap[i].ram = (uint8_t*)malloc(0x10000);
    if (s->snap[i].ram) { memcpy(s->snap[i].ram, apu->ram, 0x10000); s->snap[i].cyc = apu->cycles; s->snapN++; }
  }
  s->curPc = pc;
  s->prevPc = pc;
  if (s->active) push(s, 'O', 0, 0, pc, apu->cycles);
}
static void dspWriteHook(Apu* apu, uint8_t adr, uint8_t val) {
  if (g_prevDspW) g_prevDspW(apu, adr, val);
  LsSession* s = find(apu);
  if (s && s->active) push(s, 'D', adr, val, s->curPc, apu->cycles);
}
static void readHook(Apu* apu, uint16_t adr, uint8_t val) {
  if (g_prevRd) g_prevRd(apu, adr, val);
  LsSession* s = find(apu);
  if (!s || !s->active) return;
  if (adr == 0xF3) push(s, 'd', (uint8_t)(apu->dspAdr & 0x7F), val, s->curPc, apu->cycles);
  else if (adr >= 0xF4 && adr <= 0xF7) push(s, 'p', (uint8_t)(adr - 0xF4), val, s->curPc, apu->cycles);
  else if (adr >= 0xFD && adr <= 0xFF) push(s, 't', (uint8_t)(adr - 0xFD), val, s->curPc, apu->cycles);
}
static void writeHook(Apu* apu, uint16_t adr, uint8_t val) {
  if (g_prevWr) g_prevWr(apu, adr, val);
  { static long lo = -2, hi = 0;   // CT_SNDLS_WATCH=<hex adr>:<len>: the original's SPC writes there (pc, cycle)
    if (lo == -2) { const char* e = getenv("CT_SNDLS_WATCH"); lo = -1; if (e) { char* q; lo = strtol(e, &q, 16); hi = lo + (*q == ':' ? strtol(q + 1, NULL, 16) : 1); } }
    if (lo >= 0 && adr >= lo && adr < hi) { LsSession* w = find(apu); printf("watch %04X=%02X pc %04X cyc %u\n", adr, val, w ? w->curPc : 0, apu->cycles); } }
  LsSession* s = find(apu);
  if (!s || !s->active) return;
  if (dspRam(adr)) push(s, 'R', 0, val, adr, apu->cycles);
  else if (adr == 0xF3) dropDummy(s, 'd', (uint8_t)(apu->dspAdr & 0x7F));
  else if (adr >= 0xF4 && adr <= 0xF7) { dropDummy(s, 'p', (uint8_t)(adr - 0xF4)); push(s, 'P', (uint8_t)(adr - 0xF4), val, s->curPc, apu->cycles); }
  else if (adr == 0xF1 || (adr >= 0xFA && adr <= 0xFC)) push(s, 'C', (uint8_t)adr, val, s->curPc, apu->cycles);
}

// --- the host's points -----------------------------------------------------------------------------------------------
static void atExit(void) {
  if (!g_reported) CtSndLockstepReport();
  if (g_out) fclose(g_out);
}

void CtSndLockstepAttach(Apu* apu, SndMachine* m, int atPowerOn) {
  if (!apu || !m) return;
  if (!g_on) {
    g_on = 1;
    const char* p = getenv("CT_SNDDRV_LOCKSTEP_OUT");
    char path[1024];
    if (p && strstr(p, "%d")) { snprintf(path, sizeof path, p, (int)getpid()); p = path; }
    if (p && *p && (g_out = fopen(p, "a")) != NULL) {
#ifdef _WIN32
      fprintf(g_out, "snddrv-lockstep: run %s\n", GetCommandLineA());
#endif
    }
    g_prevOp = g_apuOpHook; g_apuOpHook = opHook;
    g_prevDspW = g_apuDspWriteHook; g_apuDspWriteHook = dspWriteHook;
    g_prevRd = g_apuReadHook; g_apuReadHook = readHook;
    g_prevWr = g_apuWriteHook; g_apuWriteHook = writeHook;
    { const char* e = getenv("CT_SNDLS_RAMWATCH"); g_rwAdr = e ? strtol(e, NULL, 16) : -1; }
    if (g_rwAdr >= 0) { g_prevSample = g_dspSampleHook; g_dspSampleHook = sampleHook; }
    atexit(atExit);
  }
  LsSession* s = find(apu);
  if (!s) for (int i = 0; i < 8 && !s; i++) if (!g_s[i].apu) s = &g_s[i];
  if (!s) return;
  LsSession keep = *s;
  memset(s, 0, sizeof *s);
  s->q = keep.q; s->vs = keep.vs; s->ms = keep.ms;
  s->q.n = s->q.head = 0; s->vs.n = s->vs.head = 0; s->ms.n = s->ms.head = 0;
  s->apu = apu; s->m = m;
  g_rwVn = 0;
  if (atPowerOn) {   // both at power-on: the machine joins now
    s->active = 1; s->joinedAt = 0;
    s->vs.first = s->ms.first = 0;
    SndMachineSetTrace(m, mTrace, s);
    SndMachineSetObserver(m, mObs, s); m->ramObs = mRamObs;
    SndMachineSetSink(m, mSink, s);
    m->sinkPerSample = 1;
    g_joins++;
  } else s->pending = 1;
}
void CtSndLockstepDetach(Apu* apu) {
  LsSession* s = find(apu);
  if (!s) return;
  free(s->q.v); free(s->vs.v); free(s->ms.v);
  memset(s, 0, sizeof *s);
}
void CtSndLockstepAdoptNext(Apu* apu) {
  LsSession* s = find(apu);
  if (!s) return;
  s->active = 0; s->pending = 1;
  s->q.n = s->q.head = 0; s->vs.n = s->vs.head = 0; s->ms.n = s->ms.head = 0;
  SndMachineSetTrace(s->m, NULL, NULL);
  SndMachineSetObserver(s->m, NULL, NULL); s->m->ramObs = NULL;
}
int CtSndLockstepActive(const Apu* apu) { const LsSession* s = find(apu); return s && s->active; }

static void cmpSamples(LsSession* s) {
  // align the two on the index
  if (s->vs.first < s->ms.first) sDrop(&s->vs, (long)(s->ms.first - s->vs.first));
  if (s->ms.first < s->vs.first) sDrop(&s->ms, (long)(s->vs.first - s->ms.first));
  long n = s->vs.n < s->ms.n ? s->vs.n : s->ms.n;
  if (s->vs.first != s->ms.first) n = 0;
  const int16_t* a = s->vs.v + 2 * s->vs.head; const int16_t* b = s->ms.v + 2 * s->ms.head;
  for (long i = 0; i < n; i++) {
    if (a[2 * i] != b[2 * i] || a[2 * i + 1] != b[2 * i + 1]) {
      if (s->ms.t[s->ms.head + i]) {   // a voice on the driver's working RAM (drvRam): counted apart, both levelled at $0299
        g_drvRam++;
        s->active = 0; s->pending = 1;
        s->q.n = s->q.head = 0; s->snapN = 0; s->vs.n = s->vs.head = 0; s->ms.n = s->ms.head = 0;
        SndMachineSetTrace(s->m, NULL, NULL); SndMachineSetObserver(s->m, NULL, NULL); s->m->ramObs = NULL;
        return;
      }
      char w[160];
      snprintf(w, sizeof w, "sample %llu (%d,%d vs %d,%d)", (unsigned long long)(s->vs.first + (uint64_t)i), a[2 * i], a[2 * i + 1], b[2 * i], b[2 * i + 1]);
      LsEv e = { 'S', 0, 0, 0, (uint32_t)((s->vs.first + (uint64_t)i) * 32) };
      if (g_details < 4) {   // the two S-DSPs now (both at the run's end), field by field
        CtMixer v; ct_mixer_init(&v, NULL); ct_mixer_from_dsp(&v, s->apu->dsp);
        const CtMixer* m = &s->m->mx;
        char d[1200]; size_t o = 0;
        o += (size_t)snprintf(d + o, sizeof d - o, "  S-DSP now (original/machine):");
        for (int k = 0; k < 8; k++) {
          const CtMixerVoice* a = &v.v[k]; const CtMixerVoice* b = &m->v[k];
          if (memcmp(a, b, sizeof *a) && o < sizeof d - 200)
            o += (size_t)snprintf(d + o, sizeof d - o, " v%d[env %d/%d blk %04X/%04X pc %04X/%04X out %d/%d mode %d/%d srcn %02X/%02X]", k,
                                  a->env, b->env, a->blockAdr, b->blockAdr, a->pitchCounter, b->pitchCounter, a->out, b->out,
                                  a->envMode, b->envMode, a->srcn, b->srcn);
        }
        if (memcmp(v.regs, m->regs, sizeof v.regs) && o < sizeof d - 100)
          for (int r = 0; r < 128; r++) if (v.regs[r] != m->regs[r] && o < sizeof d - 40) o += (size_t)snprintf(d + o, sizeof d - o, " r%02X %02X/%02X", r, v.regs[r], m->regs[r]);
        if ((v.echoPos != m->echoPos || v.firPos != m->firPos || memcmp(v.firL, m->firL, sizeof v.firL)) && o < sizeof d - 100)
          o += (size_t)snprintf(d + o, sizeof d - o, " echo pos %d/%d fir %d/%d", v.echoPos, m->echoPos, v.firPos, m->firPos);
        if (v.noise != m->noise || v.counter != m->counter) o += (size_t)snprintf(d + o, sizeof d - o, " noise %d/%d counter %d/%d", v.noise, m->noise, v.counter, m->counter);
        o += (size_t)snprintf(d + o, sizeof d - o, " | echo %04X+%u writes %d vol %d/%d efb %d | voices (env>0) block/dir:",
                              m->echoAdr, m->echoLength, m->echoWrites, m->echoVolL, m->echoVolR, m->feedback);
        for (int k = 0; k < 8 && o < sizeof d - 40; k++)
          if (m->v[k].env) o += (size_t)snprintf(d + o, sizeof d - o, " v%d %04X/%04X", k, m->v[k].blockAdr,
                                                 (unsigned)(m->dirAdr + m->v[k].srcn * 4));
        o += (size_t)snprintf(d + o, sizeof d - o, " | RAM now differs at:");
        for (int r = 0x200; r < 0x10000 && o < sizeof d - 24; r++)
          if (s->apu->ram[r] != s->m->d.m[r]) o += (size_t)snprintf(d + o, sizeof d - o, " %04X %02X/%02X", r, s->apu->ram[r], s->m->d.m[r]);
        snprintf(d + o, sizeof d - o, "\n");
        say("%s", d);
      }
      mismatch(s, w, &e, &e);
      return;
    }
  }
  g_samples += n;
  sDrop(&s->vs, n); sDrop(&s->ms, n);
}

void CtSndLockstepRunStart(Apu* apu) { LsSession* s = find(apu); if (s) s->activeAtStart = s->active; }
void CtSndLockstepRunEnd(Apu* apu, int ranRef, int ranMachine) {
  LsSession* s = find(apu);
  if (!s || !s->active) return;
  g_runs++;
  if (s->activeAtStart && ranRef != ranMachine) {   // (both were level at the start: the same run)
    LsEv v = { 'B', 0, 0, 0, (uint32_t)ranRef }, m = { 'B', 0, 0, 0, (uint32_t)ranMachine };
    mismatch(s, "a different run length", &v, &m);
    return;
  }
  if (s->q.n) { mismatch(s, "the machine stopped early", &s->q.v[s->q.head], NULL); return; }
  if ((uint32_t)SndMachineCycles(s->m) != apu->cycles) {
    LsEv v = { 'B', 0, 0, 0, apu->cycles }, m = { 'B', 0, 0, 0, (uint32_t)SndMachineCycles(s->m) };
    mismatch(s, "a different stop", &v, &m);
    return;
  }
  CtSndLockstepPorts(apu);
  if (s->active) cmpSamples(s);
  // CT_SNDLS_REJOIN=<n>: the machine takes the original's state again every n runs (the adoption's own check)
  static long every = -1;
  if (every < 0) { const char* e = getenv("CT_SNDLS_REJOIN"); every = e ? atol(e) : 0; }
  if (every > 0 && s->active && g_runs % every == 0) CtSndLockstepAdoptNext(apu);
}
void CtSndLockstepSamples(Apu* apu, const int16_t* lr, int n, uint64_t firstIndex) {
  LsSession* s = find(apu);
  if (!s || !s->active || n <= 0) return;
  if (!s->vs.n) s->vs.first = firstIndex;
  else if (s->vs.first + (uint64_t)s->vs.n != firstIndex) { s->vs.n = 0; s->vs.head = 0; s->vs.first = firstIndex; }
  sPush(&s->vs, lr, n, 0);
}
void CtSndLockstepPorts(Apu* apu) {
  LsSession* s = find(apu);
  if (!s || !s->active) return;
  for (int k = 0; k < 4; k++) {
    if (apu->outPorts[k] != s->m->out[k] || apu->inPorts[k] != s->m->in[k]) {
      LsEv v = { 'Q', (uint8_t)k, apu->outPorts[k], apu->inPorts[k], apu->cycles };
      LsEv m = { 'Q', (uint8_t)k, s->m->out[k], s->m->in[k], (uint32_t)SndMachineCycles(s->m) };
      mismatch(s, "the ports (out, in)", &v, &m);
      return;
    }
  }
}
// The direct page's work bytes the routines write before they read them (their partial results, never state the next
// pass reads): the C program keeps those values in C variables.
static int scratch(int a) {
  return (a >= 0x92 && a <= 0x96) || (a >= 0x9B && a <= 0xA2) || a == 0xA4 || (a >= 0xB5 && a <= 0xB8) || a == 0xBA;
}
static void ramCmp(LsSession* s, const uint8_t* a, uint32_t cyc) {
  g_ramChecks++;
  const uint8_t* b = s->m->d.m;
  for (int i = 0; i < 0x10000; i++) {
    if (i == 0xF0) { i = 0xFF; continue; }       // the SPC's registers (apu.c mirrors their writes into its RAM)
    if (i == 0x190) { i = 0x1FF; continue; }     // the stack: the C program keeps its return addresses in C
    if (scratch(i)) continue;
    if (a[i] != b[i]) {
      LsEv v = { 'M', (uint8_t)(i >> 8), a[i], (uint16_t)i, cyc }, m = { 'M', (uint8_t)(i >> 8), b[i], (uint16_t)i, cyc };
      char w[64]; snprintf(w, sizeof w, "the RAM at $%04X", i);
      mismatch(s, w, &v, &m);
      return;
    }
  }
}
void CtSndLockstepRam(Apu* apu) { (void)apu; }   // (the RAM is compared at the main loop's top, ramCmp)

void CtSndLockstepReport(void) {
  g_reported = 1;
  char line[600];
  snprintf(line, sizeof line, "snddrv-lockstep: %ld instructions, %ld accesses, %ld samples, %ld runs, %ld RAM checks compared; "
           "%ld joins; %ld mismatches; %ld sample differences while a voice played the driver's own RAM\n", g_ops, g_acc,
           g_samples, g_runs, g_ramChecks, g_joins, g_mismatch, g_drvRam);
  say("%s", line);
}
long CtSndLockstepMismatches(void) { return g_mismatch; }
int CtSndLockstepWanted(void) { const char* e = getenv("CT_AUDIO_SPC"); return e && !strcmp(e, "lockstep"); }
long CtSndLockstepCompared(void) { return g_ops; }
