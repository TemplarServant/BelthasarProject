// mixer_replay.c -- `ct.exe --mixreplay <capture>`: the native mixer (mixer.c) against the vendored S-DSP (snes/dsp.c),
// sample for sample, over a CT_MIXER_CAPTURE (src/audio.c) of a real run.
//
// Each segment of the capture starts from the S-DSP's whole state and the 64 KB the SPC had. Both mixers get the same
// state, their own copy of the RAM, and the run's register writes and RAM changes at the APU cycle they came at: the
// S-DSP makes a sample at every APU cycle divisible by 32, so an event at cycle W lands after the samples of the cycles
// before W (apu.c: apu_cycle runs before the access). The reference's stream is checked against the live run's own (the
// capture's 'H' hashes), and every sample of the mixer against the reference's.
// Prints `mixer: N samples identical / M` (tools/mixer_replay.py; disasm/notes/audio_mixer.md sec. 9).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu.h"
#include "dsp.h"
#include "statehandler.h"
#include "sound/mixer.h"
#include "sound/mixer_dsp.h"

static int popcount8(uint8_t x) { int n = 0; for (; x; x &= x - 1) n++; return n; }

typedef struct { const uint8_t* p; const uint8_t* end; } Rd;
static int rdByte(Rd* r) { return r->p < r->end ? *r->p++ : -1; }
static uint32_t rd32(Rd* r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)(rdByte(r) & 0xff) << (8 * i); return v; }
static uint64_t rdVar(Rd* r) { uint64_t v = 0; int s = 0, b; do { b = rdByte(r); if (b < 0) return v; v |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80); return v; }

typedef struct {
  // the reference and the mixer
  Apu ref; Dsp* rd; CtMixer mx; uint8_t* aram2;
  // the stream's hash against the live run's 'H' records (a queue: a record is written when its chunk completes, which
  // can be after the last event the replay has read)
  uint32_t* hq; uint32_t* hn; long hqN, hqCap, hqAt;
  uint32_t h; uint32_t hCount;
  // totals
  unsigned long long samples, same, reads, readsRef, readsMix, chunks, chunksSame;
  long segments, ramSame; int firstBad;
} Rp;

static void hashSample(Rp* r, int16_t l, int16_t rr) {
  uint8_t b[4] = { (uint8_t)l, (uint8_t)((uint16_t)l >> 8), (uint8_t)rr, (uint8_t)((uint16_t)rr >> 8) };
  for (int i = 0; i < 4; i++) r->h = (r->h ^ b[i]) * 16777619u;
  r->hCount++;
  if (r->hqAt < r->hqN && r->hCount == r->hn[r->hqAt]) {
    r->chunks++; if (r->h == r->hq[r->hqAt]) r->chunksSame++;
    else if (r->chunks - r->chunksSame == 1) printf("replay: the reference left the live run in segment %ld, chunk %ld\n", r->segments - 1, r->hqAt);
    r->hqAt++; r->h = 2166136261u; r->hCount = 0;
  }
}

// What the run exercised, so a pass says which of the S-DSP's stages it covered (voice-samples, or samples for the
// globals): a voice counts when its envelope is above 0.
enum { C_ECHOIN, C_ECHOOUT, C_FEEDBACK, C_NOISE, C_PMOD, C_ATTACK, C_DECAY, C_SUSTAIN, C_RELEASE, C_GDIRECT, C_GLINDEC,
       C_GEXPDEC, C_GLININC, C_GBENT, C_BRR0, C_BRR1, C_BRR2, C_BRR3, C_BRRBIG, C_LOOP, C_CLAMP, C_N };
static const char* const kCensus[C_N] = { "echo-in", "echo-out", "feedback", "noise", "pmod", "attack", "decay",
  "sustain", "release", "gain-direct", "gain-lindec", "gain-expdec", "gain-lininc", "gain-bent", "brr-filter0",
  "brr-filter1", "brr-filter2", "brr-filter3", "brr-shift13+", "brr-loop", "clamped" };
static unsigned long long g_census[C_N];
static void census(const CtMixer* m, const uint8_t ringBefore[8], uint8_t endxBefore, const int16_t o[2]) {
  int echoIn = 0;
  for (int ch = 0; ch < 8; ch++) {
    const CtMixerVoice* v = &m->v[ch];
    if (v->ringPos != ringBefore[ch]) {
      g_census[C_BRR0 + ((v->header >> 2) & 3)]++;
      if ((v->header >> 4) > 12) g_census[C_BRRBIG]++;
    }
    if (!v->env) continue;
    if (v->echo) echoIn = 1;
    if (v->noise) g_census[C_NOISE]++;
    if (ch > 0 && v->pitchMod) g_census[C_PMOD]++;
    if (v->envMode == 3) g_census[C_RELEASE]++;
    else if (!v->useGain) g_census[C_ATTACK + v->envMode]++;
    else if (v->directGain) g_census[C_GDIRECT]++;
    else g_census[C_GLINDEC + v->gainMode]++;
  }
  g_census[C_LOOP] += popcount8((uint8_t)(m->regs[0x7c] & ~endxBefore));
  if (echoIn && m->echoWrites) g_census[C_ECHOIN]++;
  int fir = 0; for (int i = 0; i < 8; i++) fir |= m->fir[i];
  if (fir && (m->echoVolL || m->echoVolR) && m->echoLength) g_census[C_ECHOOUT]++;
  if (fir && m->feedback && m->echoWrites && m->echoLength) g_census[C_FEEDBACK]++;
  if (o[0] == 32767 || o[0] == -32768 || o[1] == 32767 || o[1] == -32768) g_census[C_CLAMP]++;
}

static void step(Rp* r, uint64_t n, uint64_t at) {
  for (uint64_t i = 0; i < n; i++) {
    dsp_cycle(r->rd);
    uint8_t ring[8]; for (int ch = 0; ch < 8; ch++) ring[ch] = r->mx.v[ch].ringPos;
    uint8_t endx = r->mx.regs[0x7c];
    int16_t o[2]; ct_mixer_render(&r->mx, o, 1);
    census(&r->mx, ring, endx, o);
    int16_t l = r->rd->sampleOutL, rr = r->rd->sampleOutR;
    hashSample(r, l, rr);
    r->samples++;
    if (o[0] == l && o[1] == rr) r->same++;
    else if (!r->firstBad) {
      r->firstBad = 1;
      printf("mixer: first difference at sample %llu (segment %ld, APU cycle %llu): reference %d,%d  mixer %d,%d\n",
             r->samples - 1, r->segments - 1, (unsigned long long)at, l, rr, o[0], o[1]);
    }
  }
}

static void endSegment(Rp* r) {
  if (!r->rd) return;
  uint64_t want = 0, have = 0;   // the samples the live run made after the last event: up to its last 'H'
  for (long i = 0; i < r->hqN; i++) want += r->hn[i];
  for (long i = 0; i < r->hqAt; i++) have += r->hn[i];
  have += r->hCount;
  if (want > have) step(r, want - have, 0);
  if (!memcmp(r->ref.ram, r->aram2, 0x10000)) r->ramSame++;
  else printf("mixer: segment %ld ends with a different sound RAM (the echo writes)\n", r->segments - 1);
  if (r->hqAt != r->hqN || r->hCount) printf("replay: segment %ld: %ld of %ld live chunks reached\n", r->segments - 1, r->hqAt, r->hqN);
  dsp_free(r->rd); r->rd = NULL; free(r->aram2); r->aram2 = NULL;
  r->hqN = r->hqAt = 0; r->hCount = 0; r->h = 2166136261u;
}

int CtMixerReplayMain(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("mixreplay: cannot open %s\n", path); return 2; }
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { printf("mixreplay: cannot read %s\n", path); fclose(f); return 2; }
  fclose(f);
  if (len < 8 || memcmp(buf, "CTMX", 4) || buf[4] != 1) { printf("mixreplay: %s is not a version-1 capture\n", path); return 2; }
  const char* vm = getenv("CT_DSP_VOICEMASK");   // dsp.c's debug mask, given to the mixer too
  Rp* r = (Rp*)calloc(1, sizeof *r);
  r->h = 2166136261u;
  // At each 'S' the segment's 'H' records are queued by a look-ahead to the next 'S'; then its events are replayed.
  Rd rdr = { buf + 8, buf + len };
  uint64_t cur = 0, t = 0;
  for (;;) {
    int tag = rdByte(&rdr);
    if (tag < 0) break;
    if (tag == 'S') {
      endSegment(r);
      uint32_t raw = rd32(&rdr), n = rd32(&rdr);
      if ((long)(rdr.end - rdr.p) < (long)n + 0x10000) { printf("mixreplay: truncated snapshot\n"); return 2; }
      memset(&r->ref, 0, sizeof r->ref);
      r->rd = dsp_init(&r->ref);
      dsp_reset(r->rd);
      StateHandler* sh = sh_init(false, rdr.p, (int)n);
      dsp_handleState(r->rd, sh);
      sh_free(sh);
      rdr.p += n;
      memcpy(r->ref.ram, rdr.p, 0x10000);
      r->aram2 = (uint8_t*)malloc(0x10000); memcpy(r->aram2, rdr.p, 0x10000);
      rdr.p += 0x10000;
      ct_mixer_init(&r->mx, r->aram2);
      ct_mixer_from_dsp(&r->mx, r->rd);
      if (vm) r->mx.voiceMask = (uint8_t)strtol(vm, NULL, 16);
      cur = t = raw;
      r->segments++;
      // queue this segment's live hashes
      Rd q = rdr;
      for (;;) {
        int g = rdByte(&q);
        if (g < 0 || g == 'S') break;
        if (g == 'H') {
          if (r->hqN == r->hqCap) { r->hqCap = r->hqCap ? r->hqCap * 2 : 1024;
            r->hq = (uint32_t*)realloc(r->hq, r->hqCap * sizeof *r->hq); r->hn = (uint32_t*)realloc(r->hn, r->hqCap * sizeof *r->hn); }
          r->hn[r->hqN] = rd32(&q); r->hq[r->hqN] = rd32(&q); r->hqN++;
        } else if (g == 'W' || g == 'R') { rdVar(&q); q.p += 2; }
        else if (g == 'A') { rdVar(&q); q.p += 3; }
        else { printf("mixreplay: bad record %02X\n", g); return 2; }
      }
      continue;
    }
    if (!r->rd) { printf("mixreplay: a record before the first snapshot\n"); return 2; }
    if (tag == 'H') { rdr.p += 8; continue; }
    t += rdVar(&rdr);
    step(r, ((t + 31) >> 5) - ((cur + 31) >> 5), t);   // the samples of the cycles in [cur, t)
    cur = t;
    if (tag == 'W') {
      uint8_t reg = (uint8_t)rdByte(&rdr), val = (uint8_t)rdByte(&rdr);
      dsp_write(r->rd, reg, val);
      ct_mixer_write(&r->mx, reg, val);
    } else if (tag == 'A') {
      int lo = rdByte(&rdr), hi = rdByte(&rdr), val = rdByte(&rdr);
      r->ref.ram[lo | hi << 8] = (uint8_t)val; r->aram2[lo | hi << 8] = (uint8_t)val;
    } else if (tag == 'R') {
      uint8_t reg = (uint8_t)rdByte(&rdr), val = (uint8_t)rdByte(&rdr);
      r->reads++;
      if (dsp_read(r->rd, reg) == val) r->readsRef++;
      if (ct_mixer_read(&r->mx, reg) == val) r->readsMix++;
    } else { printf("mixreplay: bad record %02X\n", tag); return 2; }
  }
  endSegment(r);
  printf("replay: %ld segment(s); the reference's stream matches the live run in %llu / %llu chunks of %d samples; "
         "reads %llu / %llu\n", r->segments, r->chunksSame, r->chunks, 4096, r->readsRef, r->reads);
  printf("mixer: %llu samples identical / %llu\n", r->same, r->samples);
  printf("mixer: reads %llu identical / %llu\n", r->readsMix, r->reads);
  printf("census:");
  for (int i = 0; i < C_N; i++) printf(" %s=%llu", kCensus[i], g_census[i]);
  printf("%c", 10);
  printf("mixer: sound RAM identical at %ld / %ld segment ends\n", r->ramSame, r->segments);
  int ok = r->same == r->samples && r->readsMix == r->reads && r->chunksSame == r->chunks && r->readsRef == r->reads
           && r->ramSame == r->segments;
  free(buf);
  return ok ? 0 : 1;
}

// ---- --mixfuzz <cases> [seed]: the stages no game run reaches ----
// The game's driver never selects GAIN (it parks every $x7 at $A0 and keys ADSR) and no run saturates the output, so
// random cases drive both mixers from power-on over random sound RAM (any byte stream is BRR: every shift, filter, end
// and loop flag), random registers (GAIN and ADSR, loud volumes, FIR taps, EDL, noise rates) and random writes, key-ons
// and RAM changes between samples. Every sample, a random register read and the RAM after each case are compared.
static uint32_t g_fz;
static uint32_t fzRand(void) { g_fz ^= g_fz << 13; g_fz ^= g_fz >> 17; g_fz ^= g_fz << 5; return g_fz; }
static uint8_t fzReg(void) {
  uint32_t k = fzRand() % 100;
  if (k < 12) return 0x4c;                                   // KON
  if (k < 18) return 0x5c;                                   // KOFF
  if (k < 40) return (uint8_t)((fzRand() & 7) << 4 | (5 + fzRand() % 3));   // ADSR1 / ADSR2 / GAIN
  return (uint8_t)(fzRand() & 0x7f);
}
int CtMixerFuzzMain(int cases, uint32_t seed) {
  g_fz = seed ? seed : 1;
  static uint8_t ramA[0x10000], ramB[0x10000];
  Apu* ref = (Apu*)calloc(1, sizeof *ref);
  Dsp* rd = dsp_init(ref);
  CtMixer mx;
  unsigned long long samples = 0, same = 0, reads = 0, rsame = 0;
  int ramSame = 0, first = 1;
  for (int c = 0; c < cases; c++) {
    for (int i = 0; i < 0x10000; i++) ramA[i] = (uint8_t)fzRand();
    memcpy(ref->ram, ramA, 0x10000); memcpy(ramB, ramA, 0x10000);
    dsp_reset(rd);
    ct_mixer_init(&mx, ramB);
    for (int k = 0; k < 160; k++) {                          // the registers in a random order, twice over
      uint8_t reg = (uint8_t)(fzRand() & 0x7f), val = (uint8_t)fzRand();
      if (reg == 0x6c) val &= (fzRand() % 4) ? 0x3f : 0xff;   // mostly out of reset and unmuted
      if ((reg & 0x0f) <= 1 && (reg >> 4) < 8 && (fzRand() & 1)) val = (fzRand() & 1) ? 0x7f : 0x80;   // loud
      dsp_write(rd, reg, val); ct_mixer_write(&mx, reg, val);
    }
    { uint8_t flg = (uint8_t)(fzRand() & 0x3f); dsp_write(rd, 0x6c, flg); ct_mixer_write(&mx, 0x6c, flg); }
    int n = 2000 + (int)(fzRand() % 14000);
    for (int s = 0; s < n; s++) {
      if (fzRand() % 64 == 0) {
        int w = 1 + (int)(fzRand() % 3);
        for (int k = 0; k < w; k++) {
          uint8_t reg = fzReg(), val = (uint8_t)fzRand();
          if (reg == 0x6c && (fzRand() % 8)) val &= 0x3f;
          dsp_write(rd, reg, val); ct_mixer_write(&mx, reg, val);
        }
      }
      if (fzRand() % 32 == 0) { uint16_t a = (uint16_t)fzRand(); uint8_t v = (uint8_t)fzRand(); ref->ram[a] = v; ramB[a] = v; }
      dsp_cycle(rd);
      uint8_t ring[8]; for (int ch = 0; ch < 8; ch++) ring[ch] = mx.v[ch].ringPos;
      uint8_t endx = mx.regs[0x7c];
      int16_t o[2]; ct_mixer_render(&mx, o, 1);
      census(&mx, ring, endx, o);
      samples++;
      if (o[0] == rd->sampleOutL && o[1] == rd->sampleOutR) same++;
      else if (first) { first = 0; printf("mixfuzz: first difference in case %d at sample %d: reference %d,%d  mixer %d,%d\n",
                                          c, s, rd->sampleOutL, rd->sampleOutR, o[0], o[1]); }
      uint8_t reg = (uint8_t)(fzRand() & 0x7f);
      reads++; if (dsp_read(rd, reg) == ct_mixer_read(&mx, reg)) rsame++;
    }
    if (!memcmp(ref->ram, ramB, 0x10000)) ramSame++;
  }
  printf("mixfuzz: %llu samples identical / %llu (%d cases, seed %u)\n", same, samples, cases, seed);
  printf("mixfuzz: reads %llu identical / %llu, sound RAM identical after %d / %d cases\n", rsame, reads, ramSame, cases);
  printf("census:");
  for (int i = 0; i < C_N; i++) printf(" %s=%llu", kCensus[i], g_census[i]);
  printf("%c", 10);
  dsp_free(rd); free(ref);
  return (same == samples && rsame == reads && ramSame == cases) ? 0 : 1;
}
