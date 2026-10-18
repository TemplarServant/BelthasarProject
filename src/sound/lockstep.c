// ============================================================================
// sound/lockstep.c -- the C sound driver against the original on the vendored SPC700, tick by tick (lockstep.h).
//
// THE ORIGINAL'S SIDE. Four hooks on the SPC (snes/apu.c): the instruction about to run, the bus reads, the bus writes
// and the S-DSP writes. From them an event log is kept of what the original took from outside, at the cartridge
// driver's own points (the addresses are the listing's, disasm/notes/audio_spc_driver.md):
//   BOOT     $0200 reached                       TICK     $02A0 reached (the timer-0 wait left)
//   LOOPCMD  $0BA1/$0BA5's port reads ($0B96)     POLL     $0892 entered .. $08CC: its caller and port reads
//   TIMER1   $FE read at $1303 / $1326           DSPRD    $F3 read at $0FC6 $0FFB $1355 $1375 $13A2 $145C
//   UPHDR    $102F/$1033's header (at $1035)     UPRECV   the receiver the header's RET entered
//   UPEND    $1074                               UPRESET  a receiver's JMP $12BD    UTICK  $0415 entered
//   WRITE    a RAM write by the upload protocol ($100E-$129C) or the IPL: what the loader puts in ARAM
// and the original's outputs: its S-DSP writes, its port writes and its control writes ($F1/$FA/$FB/$FC), the
// protocol's own (PC $100E-$129C, and $07A8's ack after the session's end) left out.
//
// THE C SIDE. At each TICK (and at the boot) the C driver replays the log up to that point: BOOT/TICK/LOOPCMD call
// SndDrvBoot/Tick/Command; inside them its host callbacks take the events in order (a poll takes a POLL, a timer read
// a TIMER1, an upload session its headers, writes and ticks); WRITE events are applied to its memory as the cursor
// passes them. A callback that finds another event than the one it needs is a DIVERGENCE (the two took different
// paths). Then the two are compared: the outputs in order, the S-DSP register file (but ENVX/OUTX/ENDX, which the
// S-DSP writes), and the RAM -- the direct page but the routines' scratch bytes, page 1 below the stack, and $0200-
// $FFFF but the echo buffer (from the lowest ESA seen, [ESA x 256, $F104)).
// ============================================================================
#include "sound/lockstep.h"
#include "sound/driver.h"

#include <stdarg.h>
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

enum { EV_BOOT = 1, EV_TICK, EV_LOOPCMD, EV_POLL, EV_TIMER1, EV_DSPRD, EV_UPHDR, EV_UPRECV, EV_UPEND, EV_UPRESET,
       EV_UTICK, EV_WRITE };
static const char* const kEvName[] = { "?", "BOOT", "TICK", "LOOPCMD", "POLL", "TIMER1", "DSPRD", "UPHDR", "UPRECV",
                                       "UPEND", "UPRESET", "UTICK", "WRITE" };
typedef struct { uint8_t t, a, b, c, d; uint16_t w; } Ev;
enum { OUT_DSP = 1, OUT_PORT, OUT_CTL };
static const char* const kOutName[] = { "?", "dsp", "port", "ctl" };

typedef struct { Ev* v; int n, cap; } EvLog;
typedef struct { uint32_t* v; int n, cap; } OutLog;

static struct {
  int on;
  Apu* apu;
  int synced;
  SndDrv* drv;
  uint8_t dspFile[128];            // the C driver's S-DSP register file (its sink)
  EvLog log;
  int rd;
  OutLog oOut, cOut;
  uint16_t pc;                     // the original's instruction running now
  int pollOpen; uint16_t pollSite; uint8_t poll[4];
  uint8_t loop[4];
  uint8_t hdrKind; uint16_t hdrDest;
  int ackFromSession;
  int err; char errMsg[200];
  long ticks, compared, mismatched, diverged, events;
  int details;
  // what the original ran (the fuzz's proof)
  uint32_t evRun[60], cmdRun[256], kindRun[8], laRun[7];
  void (*prevOp)(Apu*, uint16_t);
  void (*prevDspW)(Apu*, uint8_t, uint8_t);
  void (*prevRd)(Apu*, uint16_t, uint8_t);
  void (*prevWr)(Apu*, uint16_t, uint8_t);
} L;

static void evPush(uint8_t t, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t w) {
  if (!L.synced) return;
  if (L.log.n == L.log.cap) {
    int cap = L.log.cap ? L.log.cap * 2 : 4096;
    Ev* v = (Ev*)realloc(L.log.v, (size_t)cap * sizeof *v);
    if (!v) return;
    L.log.v = v; L.log.cap = cap;
  }
  Ev* e = &L.log.v[L.log.n++];
  e->t = t; e->a = a; e->b = b; e->c = c; e->d = d; e->w = w;
}
static void outPush(OutLog* o, int kind, uint8_t a, uint8_t b) {
  if (!L.synced) return;
  if (o->n == o->cap) {
    int cap = o->cap ? o->cap * 2 : 4096;
    uint32_t* v = (uint32_t*)realloc(o->v, (size_t)cap * sizeof *v);
    if (!v) return;
    o->v = v; o->cap = cap;
  }
  o->v[o->n++] = (uint32_t)(kind << 16 | a << 8 | b);
}

static void diverge(const char* fmt, int a, int b) {
  if (L.err) return;
  L.err = 1;
  snprintf(L.errMsg, sizeof L.errMsg, fmt, a, b);
}

// The cursor: WRITE events are applied as it passes them; then the next event must be of type t.
static void applyWrites(void) {
  while (L.rd < L.log.n && L.log.v[L.rd].t == EV_WRITE) {
    L.drv->m[L.log.v[L.rd].w] = L.log.v[L.rd].a;
    L.rd++;
  }
}
static Ev* take(int t) {
  if (L.err) return NULL;
  applyWrites();
  if (L.rd >= L.log.n) {
    L.err = 1;
    snprintf(L.errMsg, sizeof L.errMsg, "the C driver wanted %s past the tick's events", kEvName[t]);
    return NULL;
  }
  Ev* e = &L.log.v[L.rd];
  if (e->t != t) {
    L.err = 1;
    snprintf(L.errMsg, sizeof L.errMsg, "the C driver wanted %s, the original had %s (%02X %02X %04X)", kEvName[t],
             kEvName[e->t], e->a, e->b, e->w);
    return NULL;
  }
  L.rd++;
  return e;
}

// --- the C driver's host ---------------------------------------------------------------------------------------------
static void hDspWrite(void* ctx, uint8_t reg, uint8_t val) { (void)ctx; L.dspFile[reg & 0x7F] = val; outPush(&L.cOut, OUT_DSP, reg, val); }
static uint8_t hDspRead(void* ctx, uint8_t reg) {
  (void)ctx;
  Ev* e = take(EV_DSPRD);
  if (!e) return L.dspFile[reg & 0x7F];
  if (e->a != reg) diverge("S-DSP read of $%02X, the original read $%02X", reg, e->a);
  return e->b;
}
static void hPoll(void* ctx, int site, uint8_t cmd[4]) {
  (void)ctx;
  Ev* e = take(EV_POLL);
  if (!e) { cmd[0] = 0; return; }
  if (e->w != site) diverge("a poll from $%04X, the original's from $%04X", site, e->w);
  cmd[0] = e->a; cmd[1] = e->b; cmd[2] = e->c; cmd[3] = e->d;
}
static void hStatus(void* ctx, int k, uint8_t v) { (void)ctx; outPush(&L.cOut, OUT_PORT, (uint8_t)k, v); }
static uint8_t hTimer1(void* ctx) { (void)ctx; Ev* e = take(EV_TIMER1); return e ? e->a : 1; }
static void hControl(void* ctx, uint8_t reg, uint8_t v) { (void)ctx; outPush(&L.cOut, OUT_CTL, reg, v); }

static const uint16_t kRecv[8] = { 0x11E4, 0x11B2, 0x1167, 0x107C, 0x11E4, 0x10EC, 0x11E4, 0x1206 };   // the $1544 table
static void hUpload(void* ctx) {
  (void)ctx;
  for (;;) {
    if (L.err) return;
    int i = L.rd;
    while (i < L.log.n && (L.log.v[i].t == EV_WRITE || L.log.v[i].t == EV_TIMER1)) i++;
    if (i >= L.log.n || L.log.v[i].t != EV_UPHDR) {
      diverge("the upload session: no block header where the C driver wanted one (%d/%d)", i, L.log.n);
      return;
    }
    uint8_t kind = L.log.v[i].a;
    uint16_t dest = L.log.v[i].w;
    int k = SndDrvUploadHeader(L.drv, kind, dest);
    if (!take(EV_UPHDR)) return;
    if (kind == 0xE0) { take(EV_UPEND); return; }
    Ev* r = take(EV_UPRECV);
    if (!r) return;
    if (k < 0 || r->w != kRecv[k]) { diverge("upload block: the C driver's receiver %d, the original's $%04X", k, r->w); return; }
    for (;;) {
      applyWrites();
      if (L.rd >= L.log.n) { diverge("the upload session: the log ended inside a block (%d/%d)", L.rd, L.log.n); return; }
      Ev* e = &L.log.v[L.rd];
      if (e->t == EV_UTICK) { L.rd++; SndDrvUploadTick(L.drv); continue; }
      if (e->t == EV_UPRESET) { L.rd++; SndDrvUploadReset(L.drv); return; }
      break;
    }
  }
}

// --- level, replay, compare ------------------------------------------------------------------------------------------
// The bytes the S-DSP's echo may have written since the last level: from its own state (snes/dsp.c -- writes on, the
// buffer's address, the length it runs and the one it takes at its next wrap; 4 bytes at EDL 0), marked at each level
// and compare and before each write that moves it ($6C, $6D, $7D). The RAM compare leaves them out.
static uint8_t g_echoDirty[0x10000];
static void markEcho(Apu* apu, unsigned adr, int writes, unsigned delay) {
  if (!writes) return;
  unsigned len = (unsigned)apu->dsp->echoLength, d4 = delay * 4;
  if (d4 > len) len = d4;
  if (len < 4) len = 4;
  for (unsigned i = 0; i < len; i++) g_echoDirty[(uint16_t)(adr + i)] = 1;
}
static void markEchoNow(Apu* apu) { markEcho(apu, apu->dsp->echoBufferAdr, apu->dsp->echoWrites, apu->dsp->echoDelay); }

static void clearLogs(void) { L.log.n = 0; L.rd = 0; L.oOut.n = 0; L.cOut.n = 0; L.err = 0; }
static void level(void) {   // the C driver takes the original's state
  memcpy(L.drv->m, L.apu->ram, sizeof L.drv->m);
  memcpy(L.dspFile, L.apu->dsp->ram, sizeof L.dspFile);
  L.drv->alive = 1;
  memset(g_echoDirty, 0, sizeof g_echoDirty);
  markEchoNow(L.apu);
  clearLogs();
}

static void replay(void) {
  while (!L.err && L.rd < L.log.n) {
    Ev* e = &L.log.v[L.rd++];
    switch (e->t) {
    case EV_WRITE: L.drv->m[e->w] = e->a; break;
    case EV_BOOT: SndDrvBoot(L.drv); break;
    case EV_TICK: SndDrvTick(L.drv); break;
    case EV_LOOPCMD: { uint8_t c[4] = { e->a, e->b, e->c, e->d }; SndDrvCommand(L.drv, c); break; }
    default:
      L.err = 1;
      snprintf(L.errMsg, sizeof L.errMsg, "the original took %s (%02X %02X %04X) where the C driver took nothing",
               kEvName[e->t], e->a, e->b, e->w);
      break;
    }
  }
}

static int scratch(int a) {   // the routines' work bytes: written before they are read, never state
  return (a >= 0x92 && a <= 0x96) || (a >= 0x9B && a <= 0xA2) || a == 0xA4 || (a >= 0xB5 && a <= 0xB8) || a == 0xBA;
}

static void report(const char* fmt, ...);
static void compare(void) {
  char what[400] = "";
  if (L.err) { snprintf(what, sizeof what, "diverged: %s", L.errMsg); L.diverged++; }
  if (!what[0]) {
    int n = L.oOut.n < L.cOut.n ? L.oOut.n : L.cOut.n, i;
    for (i = 0; i < n && L.oOut.v[i] == L.cOut.v[i]; i++) {}
    if (i < n || L.oOut.n != L.cOut.n) {
      uint32_t o = i < L.oOut.n ? L.oOut.v[i] : 0, c = i < L.cOut.n ? L.cOut.v[i] : 0;
      snprintf(what, sizeof what, "output %d of %d/%d: original %s %02X=%02X, C %s %02X=%02X", i, L.oOut.n, L.cOut.n,
               kOutName[o >> 16 & 3], o >> 8 & 0xFF, o & 0xFF, kOutName[c >> 16 & 3], c >> 8 & 0xFF, c & 0xFF);
    }
  }
  if (!what[0]) {
    for (int r = 0; r < 128; r++) {
      if ((r & 0x0F) == 8 || (r & 0x0F) == 9 || r == 0x7C) continue;
      if (L.dspFile[r] != L.apu->dsp->ram[r]) {
        snprintf(what, sizeof what, "S-DSP register $%02X: original %02X, C %02X", r, L.apu->dsp->ram[r], L.dspFile[r]);
        break;
      }
    }
  }
  if (!what[0]) {
    const uint8_t* o = L.apu->ram;
    const uint8_t* c = L.drv->m;
    markEchoNow(L.apu);
    for (int a = 0; a < 0x10000; a++) {
      if (a >= 0xF0 && a < 0x100) continue;          // the SPC's registers
      if (a >= 0x190 && a < 0x200) continue;         // the stack
      if (g_echoDirty[a]) continue;                   // the echo buffer (the S-DSP writes it)
      if (a < 0x100 && scratch(a)) continue;
      if (o[a] != c[a]) { snprintf(what, sizeof what, "RAM $%04X: original %02X, C %02X", a, o[a], c[a]); break; }
    }
  }
  L.compared++;
  if (what[0]) {
    L.mismatched++;
    if (L.details < 8) { L.details++; report("snddrv-lockstep: MISMATCH at tick %ld: %s\n", L.ticks, what); }
    if (getenv("CT_SNDDRV_TRACE")) {   // the tick's events and both outputs
      for (int i = 0; i < L.log.n; i++) fprintf(stderr, "  ev %s %02X %02X %02X %02X %04X\n", kEvName[L.log.v[i].t], L.log.v[i].a,
                                               L.log.v[i].b, L.log.v[i].c, L.log.v[i].d, L.log.v[i].w);
      for (int i = 0; i < L.oOut.n || i < L.cOut.n; i++) {
        uint32_t o = i < L.oOut.n ? L.oOut.v[i] : 0, c = i < L.cOut.n ? L.cOut.v[i] : 0;
        fprintf(stderr, "  out %3d  %s %02X=%02X   %s %02X=%02X%s\n", i, kOutName[o >> 16 & 3], o >> 8 & 0xFF, o & 0xFF,
                kOutName[c >> 16 & 3], c >> 8 & 0xFF, c & 0xFF, o != c ? "  <--" : "");
      }
      fprintf(stderr, "  C: $86 %02X $B1 %02X $AF %02X | original $86 %02X $B1 %02X $AF %02X\n", L.drv->m[0x86], L.drv->m[0xB1],
              L.drv->m[0xAF], L.apu->ram[0x86], L.apu->ram[0xB1], L.apu->ram[0xAF]);
      for (int x = 0x00; x < 0x20; x += 2) {
        uint16_t p = (uint16_t)(L.apu->ram[2 + x] | L.apu->ram[3 + x] << 8);
        fprintf(stderr, "  track %02X ptr %04X (C %02X%02X):", x, p, L.drv->m[3 + x], L.drv->m[2 + x]);
        for (int k = -6; k < 2; k++) fprintf(stderr, " %02X/%02X", L.apu->ram[(uint16_t)(p + k)], L.drv->m[(uint16_t)(p + k)]);
        fputc(10, stderr);
      }
      for (int a = 0; a < 0x10000; a++)
        if (!g_echoDirty[a] && L.apu->ram[a] != L.drv->m[a] && !(a >= 0xF0 && a < 0x100) && !(a >= 0x190 && a < 0x200) && !(a < 0x100 && scratch(a)))
          fprintf(stderr, "  RAM $%04X: original %02X, C %02X\n", a, L.apu->ram[a], L.drv->m[a]);
    }
    level();
  }
}

// --- the original's hooks --------------------------------------------------------------------------------------------
// A tick starts (or the driver boots): the C driver replays what the original did since the last one, and the two
// are compared. The boot (and the first tick after an attach or a savestate) levels them instead.
static void atSync(int boot) {
  static int tr = -1;
  if (tr < 0) tr = getenv("CT_SNDDRV_TRACE") ? 1 : 0;
  if (tr) fprintf(stderr, "snddrv-lockstep: tick %ld, %d events, pc $%04X\n", L.ticks, L.log.n, L.pc);
  if (L.synced && !boot) { replay(); compare(); clearLogs(); }
  else { level(); L.synced = 1; }
}

static void opHook(Apu* apu, uint16_t pc) {
  if (L.prevOp) L.prevOp(apu, pc);
  if (apu != L.apu) return;
  uint16_t prev = L.pc;
  L.pc = pc;
  { static long n; static int tr = -1; if (tr < 0) tr = getenv("CT_SNDDRV_TRACE") ? 1 : 0;
    if (tr && !(++n & 0xFFFFF)) fprintf(stderr, "snddrv-lockstep: pc $%04X ($87 %02X $88 %02X)\n", pc, apu->ram[0x87], apu->ram[0x88]); }
  switch (pc) {
  case 0x0200: L.ackFromSession = 0; atSync(1); evPush(EV_BOOT, 0, 0, 0, 0, 0); break;
  case 0x02A0:
    if (L.synced) L.ticks++;
    atSync(0);
    evPush(EV_TICK, 0, 0, 0, 0, 0);
    break;
  case 0x0415: evPush(EV_UTICK, 0, 0, 0, 0, 0); break;
  case 0x0892:
    if (!L.pollOpen) {
      uint8_t sp = apu->spc->sp;
      L.pollOpen = 1;
      L.pollSite = (uint16_t)(apu->ram[0x100 | (uint8_t)(sp + 1)] | apu->ram[0x100 | (uint8_t)(sp + 2)] << 8);
      memset(L.poll, 0, sizeof L.poll);
    }
    break;
  case 0x08CC:
    if (L.pollOpen) { evPush(EV_POLL, L.poll[0], L.poll[1], L.poll[2], L.poll[3], L.pollSite); L.pollOpen = 0; }
    break;
  case 0x0BA9: evPush(EV_LOOPCMD, L.loop[0], L.loop[1], L.loop[2], L.loop[3], 0); break;
  case 0x0BAC: L.cmdRun[apu->ram[0x8D]]++; break;
  case 0x1B86: case 0x1BD9: case 0x1BDD: case 0x1BFC: case 0x1C39: case 0x1CC9: case 0x1D2A:   // the look-ahead's ($0793's RET)
    if (prev == 0x0793) { static const uint16_t la[7] = { 0x1B86, 0x1BD9, 0x1BDD, 0x1BFC, 0x1C39, 0x1CC9, 0x1D2A };
                          for (int k = 0; k < 7; k++) if (la[k] == pc) L.laRun[k]++; }
    break;
  case 0x073D: { uint8_t a = apu->spc->a; if (a >= 0xC4) L.evRun[a - 0xC4]++; break; }
  case 0x1035:
    evPush(EV_UPHDR, L.hdrKind, 0, 0, 0, L.hdrDest);
    if (L.hdrKind != 0xE0) L.kindRun[(L.hdrDest >> 8) < 0x1E ? 0 : (L.hdrKind & 7)]++;
    break;
  case 0x1074: evPush(EV_UPEND, 0, 0, 0, 0, 0); break;
  case 0x1079: L.ackFromSession = 1; break;
  case 0x07AE: L.ackFromSession = 0; break;
  case 0x12BD:
    if (prev == 0x10C9 || prev == 0x1144 || prev == 0x11AE || prev == 0x11E0 || prev == 0x1202 || prev == 0x1290)
      evPush(EV_UPRESET, 0, 0, 0, 0, 0);
    break;
  case 0x11E4: case 0x11B2: case 0x1167: case 0x107C: case 0x10EC: case 0x1206:
    if (prev == 0x1073) evPush(EV_UPRECV, 0, 0, 0, 0, pc);
    break;
  default: break;
  }
}

static void readHook(Apu* apu, uint16_t adr, uint8_t v) {
  if (L.prevRd) L.prevRd(apu, adr, v);
  if (apu != L.apu || adr < 0xF3 || adr > 0xFE) return;
  switch (L.pc) {
  case 0x0892: if (adr == 0xF4) L.poll[0] = v; break;
  case 0x08AD: if (adr == 0xF5) L.poll[1] = v; break;
  case 0x08B2: if (adr == 0xF6) L.poll[2] = v; break;
  case 0x08B7: if (adr == 0xF7) L.poll[3] = v; break;
  case 0x0BA1: if (adr == 0xF6 || adr == 0xF7) L.loop[adr - 0xF4] = v; break;
  case 0x0BA5: if (adr == 0xF4 || adr == 0xF5) L.loop[adr - 0xF4] = v; break;
  case 0x102F:
    if (adr == 0xF6) L.hdrDest = (uint16_t)((L.hdrDest & 0xFF00) | v);
    else if (adr == 0xF7) L.hdrDest = (uint16_t)((L.hdrDest & 0x00FF) | v << 8);
    break;
  case 0x1033: if (adr == 0xF5) L.hdrKind = v; break;
  case 0x1303: case 0x1326: if (adr == 0xFE) evPush(EV_TIMER1, v, 0, 0, 0, 0); break;
  case 0x0FC6: case 0x0FFB: case 0x1355: case 0x1375: case 0x13A2: case 0x145C:
    if (adr == 0xF3) evPush(EV_DSPRD, apu->dspAdr, v, 0, 0, 0);
    break;
  default: break;
  }
}

static int protocolPc(uint16_t pc) { return pc >= 0x100E && pc < 0x129D; }

static int g_watch = -1;   // CT_SNDDRV_WATCH=<hex address>: the original's writes there, with the PC (a debugging aid)
static void writeHook(Apu* apu, uint16_t adr, uint8_t v) {
  if (L.prevWr) L.prevWr(apu, adr, v);
  if (apu != L.apu) return;
  if (adr == g_watch) report("snddrv-lockstep: watch tick %ld pc $%04X $%04X = %02X\n", L.ticks, L.pc, adr, v);
  if (adr >= 0xF0 && adr <= 0xFF) {
    if (adr >= 0xF4 && adr <= 0xF7) { if (!protocolPc(L.pc)) outPush(&L.oOut, OUT_PORT, (uint8_t)(adr - 0xF4), v); }
    else if (adr == 0xF1 || adr == 0xFA || adr == 0xFB || adr == 0xFC) {
      if (!protocolPc(L.pc) && !(L.ackFromSession && L.pc >= 0x07A8 && L.pc < 0x07AE)) outPush(&L.oOut, OUT_CTL, (uint8_t)adr, v);
    }
    return;
  }
  if (protocolPc(L.pc) || L.pc >= 0xFFC0) evPush(EV_WRITE, v, 0, 0, 0, adr);
}

static void dspWriteHook(Apu* apu, uint8_t adr, uint8_t v) {
  if (L.prevDspW) L.prevDspW(apu, adr, v);
  if (apu != L.apu) return;
  outPush(&L.oOut, OUT_DSP, adr, v);
  if (adr == 0x6C || adr == 0x6D || adr == 0x7D) {   // the echo's region before and after the write
    Dsp* p = apu->dsp;
    markEchoNow(apu);
    if (adr == 0x6C) markEcho(apu, p->echoBufferAdr, !(v & 0x20), p->echoDelay);
    if (adr == 0x6D) markEcho(apu, (unsigned)v << 8, p->echoWrites, p->echoDelay);
    if (adr == 0x7D) markEcho(apu, p->echoBufferAdr, p->echoWrites, (unsigned)(v & 0x0F) * 512);
  }
}

// --- the report ------------------------------------------------------------------------------------------------------
static FILE* g_out;
static void report(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (g_out) { fputs(buf, g_out); fflush(g_out); }   // (a file: the run's own output stays as it is)
  else { fputs(buf, stdout); fflush(stdout); }
}

static int g_reported;
void CtSndLockstepReport(void) {
  if (!L.on) return;
  g_reported = 1;
  char miss[600]; int m = 0, run = 0;
  miss[0] = 0;
  for (int i = 0; i < 60; i++) { if (L.evRun[i]) run++; else m += snprintf(miss + m, sizeof miss - m, " %02X", 0xC4 + i); }
  report("snddrv-lockstep: ticks %ld compared %ld mismatched %ld diverged %ld\n", L.ticks, L.compared, L.mismatched, L.diverged);
  report("snddrv-lockstep: events run %d/60, not run:%s\n", run, m ? miss : " -");
  m = 0; run = 0; miss[0] = 0;
  static const uint8_t cmds[] = { 0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x80, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88, 0x89,
                                  0x8E, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF8, 0xF9, 0xFC, 0xFD, 0xFE, 0xFF };
  for (size_t i = 0; i < sizeof cmds; i++) { if (L.cmdRun[cmds[i]]) run++; else m += snprintf(miss + m, sizeof miss - m, " %02X", cmds[i]); }
  report("snddrv-lockstep: commands run %d/%d, not run:%s\n", run, (int)sizeof cmds, m ? miss : " -");
  m = 0; run = 0; miss[0] = 0;
  for (int i = 0; i < 8; i++) { if (L.kindRun[i]) run++; else m += snprintf(miss + m, sizeof miss - m, " %02X", i); }
  report("snddrv-lockstep: upload kinds run %d/8, not run:%s\n", run, m ? miss : " -");
  m = 0; run = 0; miss[0] = 0;
  static const char* const laName[7] = { "DC/E4-E7", "E9", "EA", "F6", "F5", "E3", "FA" };
  for (int i = 0; i < 7; i++) { if (L.laRun[i]) run++; else m += snprintf(miss + m, sizeof miss - m, " %s", laName[i]); }
  report("snddrv-lockstep: look-ahead handlers run %d/7, not run:%s\n", run, m ? miss : " -");
}
long CtSndLockstepMismatches(void) { return L.mismatched; }

static void atExit(void) {   // (the tick the run ended in is left open: its events may be partial)
  if (!g_reported) CtSndLockstepReport();
  if (g_out) fclose(g_out);
}

int CtSndLockstepWanted(void) {
  static int w = -1;
  if (w < 0) { const char* e = getenv("CT_SNDDRV_LOCKSTEP"); w = (e && *e && *e != '0') ? 1 : 0; }
  return w;
}

void CtSndLockstepAttach(Apu* apu) {
  if (!apu) return;
  if (!L.on) {
    L.on = 1;
    static SndDrv drv;
    L.drv = &drv;
    SndDrvHost h = { NULL, hDspWrite, hDspRead, hPoll, hStatus, hTimer1, hControl, hUpload };
    SndDrvInit(L.drv, &h);
    const char* wa = getenv("CT_SNDDRV_WATCH");
    if (wa && *wa) g_watch = (int)strtol(wa, NULL, 16);
    const char* p = getenv("CT_SNDDRV_LOCKSTEP_OUT");
    char path[1024];
    if (p && strstr(p, "%d")) { snprintf(path, sizeof path, p, (int)getpid()); p = path; }
    if (p && *p && (g_out = fopen(p, "a")) != NULL) {
#ifdef _WIN32
      fprintf(g_out, "snddrv-lockstep: run %s\n", GetCommandLineA());
#endif
    }
    L.prevOp = g_apuOpHook; g_apuOpHook = opHook;
    L.prevDspW = g_apuDspWriteHook; g_apuDspWriteHook = dspWriteHook;
    L.prevRd = g_apuReadHook; g_apuReadHook = readHook;
    L.prevWr = g_apuWriteHook; g_apuWriteHook = writeHook;
    atexit(atExit);
  } else if (L.synced) {   // a new SPC: the last one's open tick is not compared
    L.synced = 0;
  }
  L.apu = apu;
  L.synced = 0;
  L.pollOpen = 0; L.ackFromSession = 0;
}

void CtSndLockstepResync(Apu* apu) {
  if (!L.on || apu != L.apu) return;
  L.synced = 0;
  L.pollOpen = 0; L.ackFromSession = 0;
  L.log.n = 0; L.rd = 0;
}
