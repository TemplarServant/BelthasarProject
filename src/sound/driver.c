// ============================================================================
// sound/driver.c -- the cartridge's sound driver as native C (see driver.h). Each routine names the cartridge
// routine it ports (an ARAM address of block 0); disasm/notes/audio_spc_driver.md reads each of them.
//
// Conventions: x is the track index the driver keeps in X, twice the track number (0-$0E the music tracks, $10-$1E
// the effect tracks; an effect track's voice is the one its bit in D_BIT names). The state is d->m in the cartridge
// driver's ARAM layout (driver.h): M() a byte of it, T() a per-track byte (field + x). The arithmetic is the
// driver's own (8-bit wrap, its carries and its 8x8 multiplies), written as C.
// ============================================================================
#include "driver.h"

#include <string.h>

#define M(a) d->m[(uint16_t)(a)]
#define T(f) d->m[(uint16_t)((f) + x)]

// --- the direct page -----------------------------------------------------------------------------------------------
enum {
  D_SONG_BASE = 0x00,   // word: added to a song's relative pointers ($2024 - the song header's own base)
  D_TRK_PTR = 0x02,     // word +x: the track's read pointer
  D_KON = 0x22,         // key-on shadow (flushed at the next tick's start)
  D_KOFF = 0x23,        // key-off shadow
  D_KEYED_M = 0x24,     // music voices holding a note
  D_KEYED_S = 0x25,     // effect voices holding a note
  D_CNT = 0x26,         // +x: ticks to the track's next step
  D_LOOP_SP = 0x27,     // +x: the track's loop slot (4 a track, a ring: 2x .. 2x+3)
  D_TEMPO = 0x46,       // word: the music tempo (the high byte is added each tick)
  D_TEMPO_ACC = 0x48,   // the music's tick accumulator
  D_SFX_ACC = 0x49,     // the effects' tick accumulator
  D_TEMPO_N = 0x4A,     // tempo slide: ticks left
  D_TEMPO_STEP = 0x4B,  // word
  D_MVOL = 0x4D,        // the music's master volume
  D_EVOL = 0x4E,        // word: the echo volume (high byte)
  D_EVOL_STEP = 0x50,   // word
  D_EVOL_N = 0x52,
  D_PLAYING = 0x53,     // the music tracks playing
  D_EON_M = 0x54, D_EON_S = 0x55, D_NON_M = 0x56, D_NON_S = 0x57, D_PMON_M = 0x58, D_PMON_S = 0x59,
  D_TIE_M = 0x5A, D_TIE_S = 0x5B,       // the look-ahead saw a tie next: the gate does not release the voice
  D_SLUR_M = 0x5C, D_SLUR_S = 0x5D,     // event E4
  D_SLURON_M = 0x5E, D_SLURON_S = 0x5F, // the slurred voice has been keyed
  D_ROLL_M = 0x60, D_ROLL_S = 0x61,     // event E6
  D_NCK_M = 0x62, D_NCK_S = 0x63,       // noise clock (5 bits)
  D_FIR = 0x65,         // 8 words: coefficient k at $65+2k (its high byte $66+2k is the register's value)
  D_EFB = 0x75,         // word: echo feedback (high byte)
  D_FIR_N = 0x77, D_EFB_N = 0x78, D_EFB_STEP = 0x79,
  D_MARK = 0x7B,        // the song's marker (event F9): port 2's low nibble
  D_PERC = 0x7C,        // voices in percussion mode
  D_SONG = 0x80,        // the song playing ($FF none)
  D_EDL = 0x81, D_EDL_OLD = 0x82, D_ECHO_WAIT = 0x83,
  D_SFX_VOICES = 0x86,  // the voices the effects own
  D_FLAGS = 0x87,       // bit 0 mono, bit 3 loud (cmd 8E), bit 4 paused
  D_FLAGS2 = 0x88,      // bit 0 echo realloc, bit 1 wait the old echo, bit 3 track ended, bit 4 meter half, bit 5 fast, bit 7 meter
  D_EON = 0x89, D_NON = 0x8A, D_PMON = 0x8B, D_FLG = 0x8C,   // shadows of the S-DSP registers
  D_CMD = 0x8D,         // the command being run (port 0-3)
  D_BIT = 0x91,         // the current voice's bit
  D_PASS = 0xA3,        // the voices a pass has left
  D_BYTE = 0xA5,        // the byte read last (an event's operand)
  D_TRK = 0xA6,         // the current track (x)
  D_MUTE = 0xA7,        // muted music voices
  D_PASSVOL = 0xA8,     // the pass's fade volume
  D_TMOD = 0xA9, D_TMOD_STEP = 0xAB, D_TMOD_N = 0xAD,   // the tempo modifier (word; high $AA)
  D_PMOD = 0xAE, D_SFXPITCH = 0xB1, D_PMOD_STEP = 0xB2, D_PMOD_N = 0xB4,   // the pitch modifier (word; high $AF)
  D_VARIANT = 0xB9,     // $10 / $20: which of the song's two track tables
  D_SAVED = 0xBB, D_NEXT = 0xBC,   // the saved song, the requested one
  D_FADE_DIV = 0xBD,    // the fades run every 7th tick
  D_VOLOUT = 0xBE,      // 16: each voice's VOL L/R as written (the meter reads them)
  D_VOLCHG = 0xD0, D_PITCHCHG = 0xD1,   // voices whose volume / pitch must be written this tick
  D_COND = 0xD2, D_COND_MAX = 0xD3,     // command 88's value (event FA's test), the largest FA operand seen
  D_QHEAD = 0xD7, D_BRR_STEP = 0xD8, D_EVOLOUT = 0xD9,
};
// --- page 1 (+x) ---------------------------------------------------------------------------------------------------
enum {
  P_FIR_STEP = 0x0100,  // 8 words: the FIR slide's step per coefficient
  P_VIB_DELAY = 0x0110, P_VIB_DELAY_N = 0x0111, P_TREM_DELAY = 0x0130, P_TREM_DELAY_N = 0x0131,
  P_PSLIDE = 0x0150,    // the next note's pitch slide (semitones)
  P_VIB_DEPTH = 0x0151, P_TREM_DEPTH = 0x0170, P_SWEEP = 0x0171,
};
// --- the per-track arrays (+x) -------------------------------------------------------------------------------------
enum {
  T_OCT = 0xF200, T_INSTR = 0xF201, T_EXPR = 0xF220, T_PHASE = 0xF221,
  T_VOL = 0xF240, T_VOL_STEP = 0xF260, T_PAN = 0xF280, T_PAN_STEP = 0xF2A0, T_VOL_N = 0xF2C0, T_PAN_N = 0xF2C1,
  T_VIB_RATE = 0xF2E0, T_VIB_RATE_N = 0xF2E1, T_VIB_SCALE = 0xF300, T_TREM_RATE = 0xF320, T_TREM_RATE_N = 0xF321,
  T_SWEEP_RATE = 0xF340, T_SWEEP_RATE_N = 0xF341, T_PSLIDE_N = 0xF360, T_TRANSPOSE = 0xF361,
  T_TUNE = 0xF380, T_FINE = 0xF3A0, T_NOTE = 0xF3A1, T_PSLIDE_D = 0xF3C0, T_SWEEP_STEP = 0xF3E0,
  T_VIB_STEP = 0xF400, T_VIB_CUR = 0xF420, T_TREM_STEP = 0xF440, T_TREM_CUR = 0xF460,
  T_VIB_ACC = 0xF480, T_TREM_ACC = 0xF4A0, T_SWEEP_ACC = 0xF4C0, T_PSLIDE_LO = 0xF4E0, T_PITCH_LO = 0xF4E1,
  T_VIB_OFS = 0xF500, T_PITCH = 0xF520, T_ADSR = 0xF540,
  T_LOOP_CNT = 0xF560, T_LOOP_LEFT = 0xF580, T_LOOP_PTR = 0xF5C0,   // + the loop slot (LOOP_PTR + 2 x slot)
};
// --- the rest of the RAM, and the tables the loader put in block 0 -------------------------------------------------
enum {
  A_PERC_TAB = 0xF120,  // 3 bytes a note: instrument, note, volume
  A_FADE = 0xF160,      // 3 words: music, effects A (voices 7-6), effects B (5-4); high bytes $F161/3/5
  A_FADE_N = 0xF166,    // +2k
  A_FADE_STEP = 0xF16C, // +2k (words)
  A_QUEUE = 0xF172,     // 16 entries of 4: the queued commands
  A_SFX_TAB = 0x2F00,   // the effects' track pointers, 4 bytes an effect
  A_SONG = 0x2000,      // the song's header
  A_TUNING = 0x1F00, A_ADSR_TAB = 0x1F80,   // per instrument (words)
  R_OPCOUNT = 0x15CC,   // block 0: each event's operand count
  R_FLUSH_REG = 0x16FF, R_FLUSH_SRC = 0x1710,   // block 0: the 17 shadowed S-DSP registers and their RAM
  R_SEMITONE = 0x1D6D, R_VIBSCALE = 0x1D87, R_FIRTAB = 0x1D92, R_LENGTH = 0x1DB3,
  R_END_TRACK = 0x1DC1, R_BRR_LOOP = 0x1DCB,
};

static uint16_t rw(SndDrv* d, uint16_t a) { return (uint16_t)(M(a) | M(a + 1) << 8); }
static void ww(SndDrv* d, uint16_t a, uint16_t v) { M(a) = (uint8_t)v; M(a + 1) = (uint8_t)(v >> 8); }
static uint8_t xcn(uint8_t a) { return (uint8_t)(a << 4 | a >> 4); }
static uint8_t voiceReg(int x) { return (uint8_t)(xcn((uint8_t)x) >> 1); }   // `MOV A,X / XCN / LSR`: voice x/2 & 7, x 16

// The S-DSP ignores writes to $80-$FF (they mirror the read-only view): the sink sees $00-$7F only.
static void dsp(SndDrv* d, uint8_t reg, uint8_t val) { if (reg < 0x80) d->host.dspWrite(d->host.ctx, reg, val); }
static uint8_t dspIn(SndDrv* d, uint8_t reg) { return d->host.dspRead(d->host.ctx, reg); }
static void port(SndDrv* d, int k, uint8_t v) { d->host.status(d->host.ctx, k, v); }
static void ctl(SndDrv* d, uint8_t reg, uint8_t v) { d->host.control(d->host.ctx, reg, v); }

// The driver's 16/8 divide (the SPC700's own): every use divides by a value the dividend's high byte is below.
static uint8_t spcDiv(uint8_t* a, uint8_t y, uint8_t x) {   // YA / X: returns the remainder, *a the quotient
  int yva = (y << 8) | *a, xx = x << 9;
  for (int i = 0; i < 9; i++) {
    yva <<= 1; yva |= (yva & 0x20000) ? 1 : 0; yva &= 0x1ffff;
    if (yva >= xx) yva ^= 1;
    if (yva & 1) yva -= xx;
    yva &= 0x1ffff;
  }
  *a = (uint8_t)yva;
  return (uint8_t)(yva >> 9);
}
static uint16_t neg16(uint16_t v) { return (uint16_t)(~v + 1); }

static void processQueue(SndDrv* d);
static void dispatch(SndDrv* d);
static void endTrack(SndDrv* d, int x);
static void echoOff(SndDrv* d, int x);
static void noiseOff(SndDrv* d, int x);
static void pmodOff(SndDrv* d, int x);
static void stopMusic(SndDrv* d);
static void stopSfxA(SndDrv* d);
static void stopSfxB(SndDrv* d);
static void echoAlloc(SndDrv* d);
static void voiceOut(SndDrv* d, int x);

// ===================================================================================================================
// The ports
// ===================================================================================================================

static void ackPorts(SndDrv* d) { ctl(d, 0xF1, 0x13); ctl(d, 0xF1, 0x03); }   // $07A8: clear ports 0-1 (the ack)

// $0892: a command the CPU posted while the tick runs is queued (four bytes at $F172 + $D7) and echoed; song starts
// ($10-$17) and the upload ($FE) stay on the port for the main loop.
static void pollPort(SndDrv* d, int site) {
  uint8_t c[4] = {0, 0, 0, 0};
  d->host.poll(d->host.ctx, site, c);
  uint8_t a = c[0];
  if (!a || a == 0xFE || (a >= 0x10 && a < 0x18)) return;
  uint8_t q = M(D_QHEAD);
  M(A_QUEUE + q) = a; M(A_QUEUE + q + 1) = c[1]; M(A_QUEUE + q + 2) = c[2]; M(A_QUEUE + q + 3) = c[3];
  if ((uint8_t)(q + 4) < 0x40) M(D_QHEAD) = (uint8_t)(q + 4);   // a full queue keeps overwriting its last entry
  ackPorts(d);
  port(d, 0, a);
}

// $08CD: run the queued commands, then empty the queue.
static void processQueue(SndDrv* d) {
  int x = 0;
  while (M(A_QUEUE + x)) {
    M(D_CMD) = M(A_QUEUE + x); M(D_CMD + 1) = M(A_QUEUE + x + 1);
    M(D_CMD + 2) = M(A_QUEUE + x + 2); M(D_CMD + 3) = M(A_QUEUE + x + 3);
    dispatch(d);
    x += 4;
    if (x == 0x40) break;
  }
  for (int k = x; k > 0; k -= 4) M(A_QUEUE - 4 + k) = 0;   // `$F16E+X` for X = x .. 4
  M(D_QHEAD) = 0;
}

// $12F9: wait until timer 1 (16 ms) has counted to the echo delay at `at` ($81 or $82) from $83 on -- the S-DSP
// needs EDL x 16 ms to clear the buffer -- answering the port meanwhile; then run what was queued.
static void echoWait(SndDrv* d, uint8_t at) {
  M(D_QHEAD) = 0;
  uint8_t target = M(at);
  if (!target) return;
  uint8_t a = M(D_ECHO_WAIT);
  while (a < target) {
    if (d->host.timer1(d->host.ctx)) a++;
    else pollPort(d, SNDDRV_POLL_ECHOWAIT);
  }
  processQueue(d);
}

// ===================================================================================================================
// The S-DSP side helpers
// ===================================================================================================================

static void timer1Restart(SndDrv* d) { ctl(d, 0xF1, 0x01); ctl(d, 0xF1, 0x03); }   // `$F1=1 / MOV Y,$FE / $F1=3`

// $1352: stop the echo (writes off, EON/EFB/EVOL 0), note the old delay, set the new EDL and its buffer (ESA = $F1 -
// EDL x 8: the buffer ends at $F100), restart timer 1.
static void echoStop(SndDrv* d) {
  dsp(d, 0x6C, (uint8_t)(dspIn(d, 0x6C) | 0x20));
  dsp(d, 0x4D, 0); dsp(d, 0x0D, 0); dsp(d, 0x2C, 0); dsp(d, 0x3C, 0);
  M(D_EDL_OLD) = (uint8_t)(dspIn(d, 0x7D) & 0x0F);
  dsp(d, 0x7D, M(D_EDL));
  dsp(d, 0x6D, (uint8_t)(0xF1 - (uint8_t)(M(D_EDL) << 3)));
  timer1Restart(d);
}

// $1397: restart timer 1 and let the S-DSP write the echo again.
static void echoStart(SndDrv* d) {
  timer1Restart(d);
  dsp(d, 0x6C, (uint8_t)(dspIn(d, 0x6C) & 0xDF));
}

// $132E (command FC; the boot): a new echo delay -- stop the echo, wait out the old buffer and the new one.
static void echoAlloc(SndDrv* d) {
  uint8_t a = (uint8_t)(M(D_CMD + 1) & 0x0F);
  if (a == M(D_EDL)) return;
  M(D_ECHO_WAIT) = 0;
  M(D_EDL) = a;
  echoStop(d);
  echoWait(d, D_EDL_OLD);
  echoStart(d);
  echoWait(d, D_EDL);
  if (!M(D_PLAYING)) M(D_EVOL + 1) = 0;
}

// $1316 (a song start with a new delay): stop the echo; with no old buffer to wait for, start it again at once.
static void echoChange(SndDrv* d) {
  echoStop(d);
  M(D_ECHO_WAIT) = 0;
  if (M(D_EDL_OLD)) return;
  M(D_FLAGS2) &= (uint8_t)~0x02;
  echoStart(d);
}

// $1326 (each upload header while a realloc is pending): the time the upload took counts towards the echo wait.
static void echoWaitAccount(SndDrv* d) { M(D_ECHO_WAIT) = (uint8_t)(M(D_ECHO_WAIT) + d->host.timer1(d->host.ctx)); }

// $1AED: the instrument's sample (SRCN) and, unless the effects own the music voice, its ADSR.
static void adsrOut(SndDrv* d, int x) {   // $1B0C
  if (x < 0x10 && (M(D_SFX_VOICES) & M(D_BIT))) return;
  uint8_t y = (uint8_t)(voiceReg(x) | 5);
  dsp(d, y, T(T_ADSR));
  dsp(d, (uint8_t)(y + 1), T(T_ADSR + 1));
}
static void instrOut(SndDrv* d, int x) {
  dsp(d, (uint8_t)(voiceReg(x) | 4), T(T_INSTR));
  adsrOut(d, x);
}

// $1ACF (event DC; percussion; the effects' start): the track's instrument, its tuning and ADSR from the tables.
static void setInstr(SndDrv* d, int x, uint8_t a) {
  T(T_INSTR) = a;
  uint8_t y = (uint8_t)(a << 1);
  T(T_TUNE) = M(A_TUNING + y); T(T_TUNE + 1) = M(A_TUNING + y + 1);
  T(T_ADSR) = M(A_ADSR_TAB + y); T(T_ADSR + 1) = M(A_ADSR_TAB + y + 1);
}

// $1A21 / $1A48 / $1AAA: the EON, FLG (noise clock) + NON and PMON shadows from the music's and the effects' masks.
static void eonShadow(SndDrv* d) { M(D_EON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_EON_M)) | M(D_EON_S)); }
static void nonShadow(SndDrv* d) {
  uint8_t s = (uint8_t)(M(D_NON_S) & ~1);
  M(D_FLG) = (uint8_t)((M(D_FLG) & 0xE0) | (M(D_NON_S) ? M(D_NCK_S) : M(D_NCK_M)));
  M(D_NON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_NON_M)) | s);
}
static void pmonShadow(SndDrv* d) { M(D_PMON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_PMON_M)) | M(D_PMON_S)); }

static void echoOff(SndDrv* d, int x) {    // $1A2C
  if (x < 0x10) M(D_EON_M) &= (uint8_t)~M(D_BIT); else M(D_EON_S) &= (uint8_t)~M(D_BIT);
  eonShadow(d);
}
static void noiseOff(SndDrv* d, int x) {   // $1A68
  if (x < 0x10) M(D_NON_M) &= (uint8_t)~M(D_BIT); else M(D_NON_S) &= (uint8_t)~M(D_BIT);
  nonShadow(d);
}
static void pmodOff(SndDrv* d, int x) {    // $1AB5
  if (x < 0x10) M(D_PMON_M) &= (uint8_t)~M(D_BIT); else M(D_PMON_S) &= (uint8_t)~M(D_BIT);
  pmonShadow(d);
}

// $1D4C: the track ended -- the step stops; the music's last track ends the song; an effect gives its voice back.
static void endTrack(SndDrv* d, int x) {
  M(D_FLAGS2) |= 0x08;
  uint8_t b = M(D_BIT);
  if (x < 0x10) {
    uint8_t old = M(D_PLAYING);
    M(D_PLAYING) = (uint8_t)(old & ~b);
    if (b == old) M(D_SONG) = 0xFF;   // TCLR1's Z: the bit was all that was left
  } else {
    M(D_SFX_VOICES) &= (uint8_t)~b;
    M(D_SLURON_M) &= (uint8_t)~b;     // the ROM clears the music's slur mask here ($5E), not the effects' ($5F)
  }
  noiseOff(d, x);
  pmodOff(d, x);
  echoOff(d, x);
}

// ===================================================================================================================
// Reading a track, pitches
// ===================================================================================================================

static uint8_t trkRead(SndDrv* d, int x) {   // $0753
  uint16_t p = rw(d, D_TRK_PTR + x);
  uint8_t a = M(p);
  M(D_BYTE) = a;
  ww(d, D_TRK_PTR + x, (uint16_t)(p + 1));
  return a;
}

// $06BD: a note's pitch -- the semitone's word scaled by the instrument's tuning, shifted by the octave.
static uint16_t pitchOf(SndDrv* d, int x, uint8_t note) {
  uint8_t oct = note;
  uint8_t semi = spcDiv(&oct, 0, 12);
  uint8_t lo = M(R_SEMITONE + semi * 2), hi = M(R_SEMITONE + semi * 2 + 1);
  uint8_t mul = (uint8_t)(T(T_TUNE) + T(T_FINE));
  uint16_t t = (uint16_t)(hi * mul + ((lo * mul) >> 8));
  if (T(T_TUNE + 1)) t = (uint16_t)(t + (((t >> 8) * T(T_TUNE + 1)) >> 8));
  if (!(mul & 0x80)) t = (uint16_t)(t + (hi << 8 | lo));
  uint8_t a = 4;
  if (oct == 0 || (oct & 0x80) || a >= oct) { while (a != oct) { t >>= 1; a--; } }
  else { do { t = (uint16_t)(t << 1); a++; } while (a != oct); }
  return t;
}

// $1908: a modulation's start -- a quarter step while its delay runs, the step negated for the depth's $80-$BF
// shape; returns the phase byte: `base` with the phase 7 while delayed, 0 otherwise.
static uint8_t modStart(SndDrv* d, uint16_t* step, uint8_t base, int delayed) {
  if (delayed) *step >>= 2;
  if (M(D_BYTE) >= 0x80 && M(D_BYTE) < 0xC0) *step = neg16(*step);
  return delayed ? (uint8_t)(base | 7) : base;
}

// $18DE: a modulation's step from its depth (low 6 bits) and its rate: (depth + 1) x 256 / rate, a multiple of 4.
static uint16_t modStep(uint8_t depth, uint8_t rate) {
  uint8_t a = (uint8_t)((depth & 0x3F) + 1);
  uint16_t v;
  if (!rate) v = a;
  else { uint8_t q = a; uint8_t r = spcDiv(&q, 0, rate); uint8_t lo = 0; spcDiv(&lo, r, rate); v = (uint16_t)(q << 8 | lo); }
  v >>= 2;
  if (!v) v = 1;
  return (uint16_t)(v << 2);
}

// $18A2: the vibrato restarts (each note, and event C9).
static void vibReset(SndDrv* d, int x) {
  ww(d, T_VIB_ACC + x, 0);
  T(T_VIB_RATE_N) = T(T_VIB_RATE);
  uint16_t step = rw(d, T_VIB_STEP + x);
  uint8_t delay = x < 0x10 ? M(P_VIB_DELAY + x) : 0;
  M(P_VIB_DELAY_N + x) = delay;
  T(T_PHASE) = modStart(d, &step, (uint8_t)(T(T_PHASE) & 0x70), delay != 0);
  ww(d, T_VIB_CUR + x, step);
}

// $1960: the tremolo restarts (each note, and event CB).
static void tremReset(SndDrv* d, int x) {
  T(T_TREM_RATE_N) = T(T_TREM_RATE);
  uint16_t step = rw(d, T_TREM_STEP + x);
  uint8_t delay = M(P_TREM_DELAY + x);
  M(P_TREM_DELAY_N + x) = delay;
  T(T_PHASE) = xcn(modStart(d, &step, xcn((uint8_t)(T(T_PHASE) & 0x07)), delay != 0));
  ww(d, T_TREM_CUR + x, step);
}

// $0720: a percussion note -- instrument, note and (unless negative) pan from the table at $F120.
static uint8_t percNote(SndDrv* d, int x) {
  uint8_t y = (uint8_t)(M(D_BYTE) * 3);
  uint8_t v = M(A_PERC_TAB + 2 + y);
  if (!(v & 0x80)) T(T_PAN + 1) = (uint8_t)(v << 1);
  M(D_BYTE) = M(A_PERC_TAB + 1 + y);
  setInstr(d, x, M(A_PERC_TAB + y));
  return M(D_BYTE);
}

// $0E5F: a slide's step -- the difference (its sign in c: 1 = up) times 256 over the slide's ticks (the command's
// second byte, $8E), the sign put back.
static uint16_t slideStep(SndDrv* d, uint8_t a, int c) {
  if (!c) a = (uint8_t)(~a + 1);
  uint8_t n = M(D_CMD + 1);
  uint8_t q = a, r = spcDiv(&q, 0, n), lo = 0;
  spcDiv(&lo, r, n);
  uint16_t v = (uint16_t)(q << 8 | lo);
  return c ? v : neg16(v);
}

static uint16_t sfxPtr(SndDrv* d, uint8_t a, uint8_t y) {   // $1BBC: the effect table's word at a x 4 + y
  return rw(d, (uint16_t)(A_SFX_TAB + y + (a << 2)));
}

static void slurClear(SndDrv* d, int x) {   // $1B86
  uint8_t b = M(D_BIT);
  if (x < 0x10) { M(D_SLUR_M) &= (uint8_t)~b; M(D_ROLL_M) &= (uint8_t)~b; }
  else { M(D_SLUR_S) &= (uint8_t)~b; M(D_ROLL_S) &= (uint8_t)~b; }
}

static void loopPop(int x, uint8_t* sp) {   // the loop slot steps back; below the track's base it wraps to the top
  uint8_t base = (uint8_t)(x * 2 - 1);
  (*sp)--;
  if (*sp == base) *sp = (uint8_t)(base + 4);
}

// ===================================================================================================================
// The sequence events $C4-$FF ($073D through the table at $1554). `a` is the first operand (0 for an event without
// one, whose $A5 then holds the event's index). Returns 1 when the track ended (the step stops).
// ===================================================================================================================

static void setVol(SndDrv* d, int x, uint8_t a) {   // $175F
  T(T_VOL + 1) = (uint8_t)(a & 0x7F); T(T_VOL) = 0;
  M(D_VOLCHG) |= M(D_BIT);
  T(T_VOL_N) = 0;
}
static void setPan(SndDrv* d, int x, uint8_t a) {   // $17BA (the music's tracks only)
  if (x >= 0x10) return;
  T(T_PAN + 1) = (uint8_t)(a << 1); T(T_PAN) = 0;
  M(D_VOLCHG) |= M(D_BIT);
  T(T_PAN_N) = 0;
}
static void setTempo(SndDrv* d, uint8_t a) {   // $1722: the operand plus 20/256 of it
  M(D_TEMPO + 1) = (uint8_t)(((a * 20) >> 8) + M(D_BYTE));
  M(D_TEMPO) = 0; M(D_TEMPO_N) = 0;
}
static void setEvol(SndDrv* d, uint8_t a) { M(D_EVOL + 1) = a; M(D_EVOL) = 0; M(D_EVOL_N) = 0; }   // $1791

// The signed slides (F3, F7, the FIR, the modifiers) compare with bit 7 flipped.
static uint8_t sdiff(uint8_t to, uint8_t from, int* c) {
  uint8_t t = (uint8_t)(to ^ 0x80), f = (uint8_t)(from ^ 0x80);
  *c = t >= f;
  return (uint8_t)(t - f);
}

static int event(SndDrv* d, int x, uint8_t ev) {
  uint8_t idx = (uint8_t)(ev - 0xC4);
  M(D_BYTE) = idx;
  uint8_t a = 0, b, c8;
  int c;
  if (M(R_OPCOUNT + idx)) a = trkRead(d, x);
  uint8_t bit = M(D_BIT);
  switch (ev) {
  case 0xC4: setVol(d, x, a); break;
  case 0xC5:   // volume slide: ticks, target
    T(T_VOL_N) = a; M(D_CMD + 1) = a;
    b = (uint8_t)(trkRead(d, x) & 0x7F);
    if (!M(D_CMD + 1)) { setVol(d, x, b); break; }
    c = b >= T(T_VOL + 1); c8 = (uint8_t)(b - T(T_VOL + 1));
    if (!c8) { T(T_VOL_N) = 0; break; }
    ww(d, (uint16_t)(T_VOL_STEP + x), slideStep(d, c8, c));
    break;
  case 0xC6: setPan(d, x, a); break;
  case 0xC7:   // pan slide: ticks, target
    T(T_PAN_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    if (x >= 0x10) break;
    if (!M(D_CMD + 1)) { setPan(d, x, b); break; }
    b = (uint8_t)(b << 1);
    c = b >= T(T_PAN + 1); c8 = (uint8_t)(b - T(T_PAN + 1));
    if (!c8) { T(T_PAN_N) = 0; break; }
    ww(d, (uint16_t)(T_PAN_STEP + x), slideStep(d, c8, c));
    break;
  case 0xC8:   // the next note's pitch slide: ticks, semitones
    T(T_PSLIDE_N) = (uint8_t)(a + 1);
    M(P_PSLIDE + x) = trkRead(d, x);
    break;
  case 0xC9: {   // vibrato: delay (an effect's: the depth table's index), rate, depth
    if (x >= 0x10) { uint8_t q = a; a = spcDiv(&q, 0, 12); }
    M(P_VIB_DELAY + x) = a;
    uint8_t rate = trkRead(d, x);
    T(T_VIB_RATE) = rate;
    uint8_t depth = trkRead(d, x);
    M(P_VIB_DEPTH + x) = depth;
    ww(d, (uint16_t)(T_VIB_STEP + x), modStep(depth, rate));
    ww(d, (uint16_t)(T_VIB_OFS + x), 0);
    vibReset(d, x);
    break;
  }
  case 0xCA: M(P_VIB_DEPTH + x) = 0; ww(d, (uint16_t)(T_VIB_OFS + x), 0); break;
  case 0xCB: {   // tremolo: delay, rate, depth
    M(P_TREM_DELAY + x) = a;
    uint8_t rate = trkRead(d, x);
    T(T_TREM_RATE) = rate;
    uint8_t depth = trkRead(d, x);
    M(P_TREM_DEPTH + x) = depth;
    ww(d, (uint16_t)(T_TREM_STEP + x), modStep(depth, rate));
    ww(d, (uint16_t)(T_TREM_ACC + x), 0);
    tremReset(d, x);
    break;
  }
  case 0xCC: M(P_TREM_DEPTH + x) = 0; ww(d, (uint16_t)(T_TREM_ACC + x), 0); break;
  case 0xCD: {   // pan sweep: period ($FF: none), depth (bit 7: the other way)
    uint8_t p = (uint8_t)(a + 1);
    int cy;
    if (!p) cy = 1;
    else { p &= 0xFE; if (!p) p = 2; cy = 0; }
    T(T_SWEEP_RATE) = p;
    uint8_t h = (uint8_t)(cy << 7 | p >> 1);
    T(T_SWEEP_RATE_N) = h;
    uint8_t dep = trkRead(d, x);
    int neg = dep >> 7;
    uint8_t v = (uint8_t)(dep & 0x7F);
    if (neg) v ^= 0x7F;
    uint16_t st;
    if (h & 0x80) st = v;
    else {
      uint8_t q = v, r = spcDiv(&q, 0, h), lo = 0;
      spcDiv(&lo, r, h);
      st = (uint16_t)(q << 8 | lo);
      if (!st) st = 1;
    }
    if (neg) st = neg16(st);
    ww(d, (uint16_t)(T_SWEEP_STEP + x), st);
    M(P_SWEEP + x) = M(D_BYTE);
    ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
    break;
  }
  case 0xCE:   // pan sweep off ($19EE: the tail of CD's handler, with A = 0)
    M(P_SWEEP + x) = 0;
    ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
    break;
  case 0xCF:   // noise clock: absolute, or (bits above 4 set) relative
    b = (uint8_t)(a & 0x1F);
    if (x < 0x10) { if (b != M(D_BYTE)) b = (uint8_t)((b + M(D_NCK_M)) & 0x1F); M(D_NCK_M) = b; }
    else { if (b != M(D_BYTE)) b = (uint8_t)((b + M(D_NCK_S)) & 0x1F); M(D_NCK_S) = b; }
    nonShadow(d);
    break;
  case 0xD0: if (x < 0x10) M(D_NON_M) |= bit; else M(D_NON_S) |= bit; nonShadow(d); break;
  case 0xD1: noiseOff(d, x); break;
  case 0xD2:
    if (x < 0x10) M(D_PMON_M) |= bit; else if (!(bit & 0x50)) M(D_PMON_S) |= bit;
    pmonShadow(d);
    break;
  case 0xD3: pmodOff(d, x); break;
  case 0xD4: if (x < 0x10) M(D_EON_M) |= bit; else M(D_EON_S) |= bit; eonShadow(d); break;
  case 0xD5: echoOff(d, x); break;
  case 0xD6: T(T_OCT) = (uint8_t)(a * 12); break;
  case 0xD7: T(T_OCT) = (uint8_t)(T(T_OCT) + 12); break;
  case 0xD8: T(T_OCT) = (uint8_t)(T(T_OCT) - 12); break;
  case 0xD9: T(T_TRANSPOSE) = a; break;
  case 0xDA: T(T_TRANSPOSE) = (uint8_t)(T(T_TRANSPOSE) + a); break;
  case 0xDB: T(T_FINE) = a; break;
  case 0xDC: setInstr(d, x, a); break;
  case 0xDD:   // ADSR: attack
    M(D_BYTE) = (uint8_t)(a & 0x0F);
    T(T_ADSR) = (uint8_t)((T(T_ADSR) & 0x70) | M(D_BYTE) | 0x80);
    adsrOut(d, x);
    break;
  case 0xDE:   // decay
    M(D_BYTE) = xcn((uint8_t)(a & 7));
    T(T_ADSR) = (uint8_t)((T(T_ADSR) & 0x0F) | M(D_BYTE) | 0x80);
    adsrOut(d, x);
    break;
  case 0xDF:   // sustain level
    M(D_BYTE) = (uint8_t)(xcn((uint8_t)(a & 7)) << 1);
    T(T_ADSR + 1) = (uint8_t)((T(T_ADSR + 1) & 0x1F) | M(D_BYTE));
    adsrOut(d, x);
    break;
  case 0xE0:   // sustain rate
    M(D_BYTE) = (uint8_t)(a & 0x1F);
    T(T_ADSR + 1) = (uint8_t)((T(T_ADSR + 1) & 0xE0) | M(D_BYTE));
    adsrOut(d, x);
    break;
  case 0xE1: {   // the instrument's own ADSR again
    uint8_t y = (uint8_t)(T(T_INSTR) << 1);
    T(T_ADSR) = M(A_ADSR_TAB + y); T(T_ADSR + 1) = M(A_ADSR_TAB + y + 1);
    adsrOut(d, x);
    break;
  }
  case 0xE2: {   // loop start: count (0 = for ever)
    uint8_t lim = (uint8_t)(x * 2 + 4);
    uint8_t sp = (uint8_t)(M(D_LOOP_SP + x) + 1);
    if (sp == lim) sp = (uint8_t)(lim - 4);
    M(D_LOOP_SP + x) = sp;
    uint8_t n = M(D_BYTE);
    if (n) n++;
    M(T_LOOP_LEFT + sp) = n;
    if (x < 0x10) M(T_LOOP_CNT + sp) = 1;
    uint8_t y = (uint8_t)(sp << 1);
    M(T_LOOP_PTR + y) = M(D_TRK_PTR + x); M(T_LOOP_PTR + y + 1) = M(D_TRK_PTR + x + 1);
    break;
  }
  case 0xE3: {   // loop end
    uint8_t sp = M(D_LOOP_SP + x);
    if (x < 0x10) M(T_LOOP_CNT + sp)++;
    uint8_t n = M(T_LOOP_LEFT + sp);
    if (n) {
      n--;
      if (!n) { loopPop(x, &M(D_LOOP_SP + x)); break; }
      M(T_LOOP_LEFT + sp) = n;
    }
    uint8_t y = (uint8_t)(sp << 1);
    M(D_TRK_PTR + x) = M(T_LOOP_PTR + y); M(D_TRK_PTR + x + 1) = M(T_LOOP_PTR + y + 1);
    break;
  }
  case 0xE4:   // slur on
    if (x < 0x10) { M(D_SLUR_M) |= bit; M(D_SLURON_M) &= (uint8_t)~bit; }
    else { M(D_SLUR_S) |= bit; M(D_SLURON_S) &= (uint8_t)~bit; }
    break;
  case 0xE5: case 0xE7: break;   // $12DD
  case 0xE6: if (x < 0x10) M(D_ROLL_M) |= bit; else M(D_ROLL_S) |= bit; break;
  case 0xE8: M(D_CNT + x) = a; break;
  case 0xE9: case 0xEA: {   // go to an effect's track (the high byte 0: the track ends there)
    uint16_t p = sfxPtr(d, a, ev == 0xE9 ? 0 : 2);
    ww(d, (uint16_t)(D_TRK_PTR + x), p);
    if (!(p >> 8)) { endTrack(d, x); return 1; }
    break;
  }
  case 0xF0: setTempo(d, a); break;
  case 0xF1:   // tempo slide: ticks, target
    M(D_TEMPO_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    if (!M(D_CMD + 1)) { setTempo(d, b); break; }
    c8 = (uint8_t)(((b * 20) >> 8) + M(D_BYTE));
    c = c8 >= M(D_TEMPO + 1); c8 = (uint8_t)(c8 - M(D_TEMPO + 1));
    if (!c8) { M(D_TEMPO_N) = 0; break; }
    ww(d, D_TEMPO_STEP, slideStep(d, c8, c));
    break;
  case 0xF2: setEvol(d, a); break;
  case 0xF3:   // echo volume slide
    M(D_EVOL_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    if (!M(D_CMD + 1)) { setEvol(d, b); break; }
    c8 = sdiff(b, M(D_EVOL + 1), &c);
    if (!c8) { M(D_EVOL_N) = 0; break; }
    ww(d, D_EVOL_STEP, slideStep(d, c8, c));
    break;
  case 0xF4: M(D_MVOL) = a; M(D_VOLCHG) |= M(D_PLAYING); break;
  case 0xF5: {   // leave the loop on iteration a: to the target
    uint8_t lo = trkRead(d, x), hi = trkRead(d, x);
    uint8_t sp = M(D_LOOP_SP + x);
    if (M(T_LOOP_CNT + sp) != a) break;
    if ((uint8_t)(M(T_LOOP_LEFT + sp) - 1) == 0) loopPop(x, &M(D_LOOP_SP + x));
    ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | lo) + rw(d, D_SONG_BASE)));
    break;
  }
  case 0xF6: {   // go to
    uint8_t hi = trkRead(d, x);
    ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | a) + rw(d, D_SONG_BASE)));
    break;
  }
  case 0xF7:   // echo feedback slide (no zero test)
    M(D_EFB_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    if (!M(D_CMD + 1)) { M(D_EFB + 1) = b; break; }
    c8 = sdiff(b, M(D_EFB + 1), &c);
    ww(d, D_EFB_STEP, slideStep(d, c8, c));
    break;
  case 0xF8: {   // the echo FIR: ticks, one of the four sets at $1D93
    M(D_FIR_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    uint8_t y = (uint8_t)(((b & 3) + 1) << 3);
    for (int k = 0x10; k; k -= 2, y--) {
      if (!M(D_CMD + 1)) { M(0x64 + k) = M(R_FIRTAB + y); continue; }
      M(0x63 + k) = 0;
      c8 = sdiff(M(R_FIRTAB + y), M(0x64 + k), &c);
      uint16_t st = slideStep(d, c8, c);
      M(0x00FE + k) = (uint8_t)st; M(0x00FF + k) = (uint8_t)(st >> 8);
    }
    break;
  }
  case 0xF9: M(D_MARK) = (uint8_t)(a & 0x0F); break;
  case 0xFA: {   // jump if command 88's value reaches the operand
    uint8_t v = (uint8_t)(a & 0x0F);
    M(D_BYTE) = v;
    if (v >= M(D_COND_MAX)) M(D_COND_MAX) = v;
    if (M(D_COND) >= v) {
      uint8_t lo = trkRead(d, x), hi = trkRead(d, x);
      ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | lo) + rw(d, D_SONG_BASE)));
    } else ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)(rw(d, (uint16_t)(D_TRK_PTR + x)) + 2));
    break;
  }
  case 0xFB: M(D_PERC) |= bit; break;
  case 0xFC: M(D_PERC) &= (uint8_t)~bit; break;
  case 0xFD: T(T_EXPR) = (uint8_t)(a << 1); M(D_VOLCHG) |= bit; break;
  default:   // EB-EF, FE, FF: the track's end
    endTrack(d, x);
    return 1;
  }
  return 0;
}

// $075E: the note look-ahead -- the next byte that is a note, a tie or a rest, read past the events (the ones that
// move the pointer followed; the slur's and the instrument's clear the slur), or $EB at the track's end.
static uint8_t lookAhead(SndDrv* d, int x) {
  uint16_t p = rw(d, (uint16_t)(D_TRK_PTR + x));
  uint8_t sp = M(D_LOOP_SP + x);
  for (;;) {
    uint8_t a = M(p);
    if (a < 0xC4) return a;
    p++;
    if (a == 0xEB) return 0xEB;
    switch (a) {
    case 0xDC: case 0xE4: case 0xE5: case 0xE6: case 0xE7: slurClear(d, x); break;   // the operand is not skipped
    case 0xE3: {
      uint8_t n = M(T_LOOP_LEFT + sp);
      if (!n || (uint8_t)(n - 1)) p = rw(d, (uint16_t)(T_LOOP_PTR + (uint8_t)(sp << 1)));
      else loopPop(x, &sp);
      break;
    }
    case 0xE9: case 0xEA:
      p = sfxPtr(d, M(p), a == 0xE9 ? 0 : 2);
      if (!(p >> 8)) return 0xEB;
      break;
    case 0xF5:
      if (M(T_LOOP_CNT + sp) != M(p)) { p += 3; break; }
      if ((uint8_t)(M(T_LOOP_LEFT + sp) - 1) == 0) loopPop(x, &sp);
      p = (uint16_t)(rw(d, (uint16_t)(p + 1)) + rw(d, D_SONG_BASE));
      break;
    case 0xF6: p = (uint16_t)(rw(d, p) + rw(d, D_SONG_BASE)); break;
    case 0xFA:
      M(D_BYTE) = M(p);
      if (M(D_COND) >= M(D_BYTE)) p = (uint16_t)(rw(d, (uint16_t)(p + 1)) + rw(d, D_SONG_BASE));
      else p += 3;
      break;
    default: p = (uint16_t)(p + M(R_OPCOUNT + (uint8_t)(a - 0xC4))); break;
    }
  }
}

// ===================================================================================================================
// A track's step ($0507): events up to a note; the note's length, pitch, modulations and key-on; the look-ahead's
// tie; the pitch slide toward the next note.
// ===================================================================================================================

static void step(SndDrv* d, int x) {
  uint8_t a;
  for (;;) {
    a = trkRead(d, x);
    if (a < 0xC4) break;
    if (event(d, x, a)) return;
  }
  if (!M(D_CNT + x)) { uint8_t q = a; M(D_CNT + x) = M(R_LENGTH + spcDiv(&q, 0, 14)); }
  a = M(D_BYTE);
  uint8_t bit = M(D_BIT);
  if (a >= 0xA8) {
    if (a >= 0xB6) return;   // a rest
    goto ahead;              // a tie
  }
  {
    uint8_t semi = a;
    spcDiv(&semi, 0, 14);
    M(D_BYTE) = semi;
    uint8_t n;
    if (x < 0x10 && (bit & M(D_PERC))) n = percNote(d, x);
    else n = (uint8_t)(T(T_OCT) + M(D_BYTE));
    n = (uint8_t)(n + T(T_TRANSPOSE) - 10);
    T(T_NOTE) = n;
    uint16_t pitch = pitchOf(d, x, n);
    ww(d, (uint16_t)(T_PITCH + x), pitch);
    uint8_t vd = M(P_VIB_DEPTH + x);
    if (vd) {
      M(D_BYTE) = vd;
      vibReset(d, x);
      uint8_t m = M(R_VIBSCALE + (x >= 0x10 ? M(P_VIB_DELAY + x) : 0));
      uint16_t v = m ? (uint16_t)(m * (pitch >> 8) + ((m * (pitch & 0xFF)) >> 8)) : pitch;
      ww(d, (uint16_t)(T_VIB_SCALE + x), v);
    }
    uint8_t td = M(P_TREM_DEPTH + x);
    if (td) { M(D_BYTE) = td; tremReset(d, x); }
    ww(d, (uint16_t)(T_VIB_OFS + x), 0);
    ww(d, (uint16_t)(T_TREM_ACC + x), 0);
    ww(d, (uint16_t)(T_PSLIDE_D + x), 0);
    T(T_PSLIDE_LO) = 0;
    if (x < 0x10) {
      M(D_KEYED_M) |= bit;
      if (M(D_SFX_VOICES) & bit) goto ahead;
      if (!(bit & M(D_ROLL_M)) && (bit & M(D_SLUR_M))) {
        if (bit & M(D_SLURON_M)) goto volpitch;
        M(D_SLURON_M) |= bit;
      }
    } else {
      M(D_KEYED_S) |= bit;
      if (!(bit & M(D_ROLL_S)) && (bit & M(D_SLUR_S))) {
        if (bit & M(D_SLURON_S)) goto volpitch;
        M(D_SLURON_S) |= bit;
      }
    }
    M(D_KON) |= bit;
  volpitch:
    M(D_VOLCHG) |= bit;
    M(D_PITCHCHG) |= bit;
    instrOut(d, x);
  }
ahead: {
    uint8_t y = lookAhead(d, x);
    bit = M(D_BIT);
    int s = x >= 0x10;
    uint8_t tie = (uint8_t)(D_TIE_M + s), slur = (uint8_t)(D_SLUR_M + s), roll = (uint8_t)(D_ROLL_M + s);
    if (y >= 0xB6) { M(slur) &= (uint8_t)~bit; M(roll) &= (uint8_t)~bit; }
    if (y >= 0xA8 && y < 0xB6) M(tie) |= bit;
    else { M(tie) &= (uint8_t)~bit; M(tie) |= (uint8_t)((M(slur) | M(roll)) & bit); }
  }
  // $064B: event C8 armed a pitch slide: from this note to the one `semitones` away, over its ticks
  uint8_t ps = M(P_PSLIDE + x);
  if (!ps) return;
  uint8_t n = (uint8_t)(ps + T(T_NOTE));
  T(T_NOTE) = n;
  uint16_t tgt = pitchOf(d, x, n), cur = rw(d, (uint16_t)(T_PITCH + x));
  int up = tgt >= cur;
  uint16_t delta = (uint16_t)(tgt - cur);
  if (!up) delta = neg16(delta);
  uint8_t len = T(T_PSLIDE_N);
  uint8_t r0 = (uint8_t)delta, r1 = (uint8_t)(delta >> 8), r2 = 0;
  if (len) {
    uint8_t q = r1, r = spcDiv(&q, 0, len);
    r2 = q;
    q = r0; r = spcDiv(&q, r, len); r1 = q;
    q = 0; spcDiv(&q, r, len); r0 = q;
  }
  if (!up) {
    r0 ^= 0xFF; r1 ^= 0xFF; r2 ^= 0xFF;
    uint16_t w = (uint16_t)((r1 << 8 | r0) + 1);
    r0 = (uint8_t)w; r1 = (uint8_t)(w >> 8);
    if (!w) r2++;
  }
  T(T_PSLIDE_LO) = r0; T(T_PSLIDE_D) = r1; T(T_PSLIDE_D + 1) = r2;
  M(P_PSLIDE + x) = 0;
  T(T_PITCH_LO) = 0;
}

// $07AF: the track's slides, each tick it runs -- the modulation delays, volume, pan, the pitch slide, the pan sweep.
static void slides(SndDrv* d, int x) {
  uint8_t bit = M(D_BIT);
  if (M(P_VIB_DELAY_N + x)) M(P_VIB_DELAY_N + x)--;
  if (M(P_TREM_DELAY_N + x)) M(P_TREM_DELAY_N + x)--;
  if (T(T_VOL_N)) {
    T(T_VOL_N)--;
    uint16_t o = rw(d, (uint16_t)(T_VOL + x)), v = (uint16_t)(o + rw(d, (uint16_t)(T_VOL_STEP + x)));
    ww(d, (uint16_t)(T_VOL + x), v);
    if ((v >> 8) != (o >> 8)) M(D_VOLCHG) |= bit;
  }
  if (T(T_PAN_N)) {
    T(T_PAN_N)--;
    uint16_t o = rw(d, (uint16_t)(T_PAN + x)), v = (uint16_t)(o + rw(d, (uint16_t)(T_PAN_STEP + x)));
    ww(d, (uint16_t)(T_PAN + x), v);
    if ((v >> 8) != (o >> 8)) M(D_VOLCHG) |= bit;
  }
  uint8_t s0 = T(T_PSLIDE_LO), s1 = T(T_PSLIDE_D), s2 = T(T_PSLIDE_D + 1);
  if (s0 || s1 || s2) {
    uint8_t n = (uint8_t)(T(T_PSLIDE_N) - 1);
    if (!n) { T(T_PSLIDE_D) = 0; T(T_PSLIDE_D + 1) = 0; T(T_PSLIDE_LO) = 0; }
    T(T_PSLIDE_N) = n;
    uint32_t acc = (uint32_t)(T(T_PITCH + 1) << 16 | T(T_PITCH) << 8 | T(T_PITCH_LO));
    acc += (uint32_t)(s2 << 16 | s1 << 8 | s0);
    T(T_PITCH_LO) = (uint8_t)acc; T(T_PITCH) = (uint8_t)(acc >> 8); T(T_PITCH + 1) = (uint8_t)(acc >> 16);
    M(D_PITCHCHG) |= bit;
  }
  if (M(P_SWEEP + x)) {
    uint16_t st = rw(d, (uint16_t)(T_SWEEP_STEP + x)), o = rw(d, (uint16_t)(T_SWEEP_ACC + x)), v = (uint16_t)(o + st);
    ww(d, (uint16_t)(T_SWEEP_ACC + x), v);
    if ((v >> 8) != (o >> 8)) M(D_VOLCHG) |= bit;
    uint8_t n = (uint8_t)(T(T_SWEEP_RATE_N) - 1);
    if (!n) { ww(d, (uint16_t)(T_SWEEP_STEP + x), neg16(st)); n = T(T_SWEEP_RATE); }
    T(T_SWEEP_RATE_N) = n;
  }
}

// $04C7: the global slides -- tempo, echo volume, echo feedback, the FIR's eight coefficients.
static void globalSlides(SndDrv* d) {
  if (M(D_TEMPO_N)) { M(D_TEMPO_N)--; ww(d, D_TEMPO, (uint16_t)(rw(d, D_TEMPO) + rw(d, D_TEMPO_STEP))); }
  if (M(D_EVOL_N)) { M(D_EVOL_N)--; ww(d, D_EVOL, (uint16_t)(rw(d, D_EVOL) + rw(d, D_EVOL_STEP))); }
  if (M(D_EFB_N)) { M(D_EFB_N)--; ww(d, D_EFB, (uint16_t)(rw(d, D_EFB) + rw(d, D_EFB_STEP))); }
  if (M(D_FIR_N)) {
    M(D_FIR_N)--;
    for (int k = 0x10; k; k -= 2)
      ww(d, (uint16_t)(0x63 + k), (uint16_t)(rw(d, (uint16_t)(0x63 + k)) + rw(d, (uint16_t)(0x00FE + k))));
  }
}

// The music's tick add: the tempo, scaled by the tempo modifier ($AA: below $80 up to x 3, from $80 down).
static uint8_t tempoAdd(SndDrv* d) {
  uint8_t t = M(D_TEMPO + 1), m = M(D_TMOD + 1);
  if (!m) return t;
  uint8_t hi = (uint8_t)((t * m) >> 8);
  if (!(m & 0x80)) { unsigned s = (unsigned)(uint8_t)(hi << 1) + t; return s > 0xFF ? 0xFF : (uint8_t)s; }
  return hi ? hi : 1;
}

// One pass of the sequencer over the tracks of `mask` (from bit 0, track x on): step each track whose countdown
// ran out, release the voice two ticks before its next step unless the look-ahead saw a tie. `full` = the main
// tick's pass (the effects' gate and the poll); 0 = the upload tick's ($0464). Returns the X it ends at.
static int seqPass(SndDrv* d, int x, uint8_t bit, uint8_t mask, int full) {
  M(D_BIT) = bit;
  M(D_PASS) = mask;
  for (;;) {
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) {
      M(D_TRK) = (uint8_t)x;
      if (!--M(D_CNT + x)) {
        M(D_FLAGS2) &= (uint8_t)~0x08;
        step(d, x);
        if (M(D_FLAGS2) & 0x08) goto next;   // the track ended
      } else {
        int s = full && x >= 0x10;
        if (M(D_CNT + x) == 2 && !(M(D_TIE_M + s) & M(D_BIT))) {
          M(D_KEYED_M + s) &= (uint8_t)~M(D_BIT);
          if (s || !(M(D_SFX_VOICES) & M(D_BIT))) M(D_KOFF) |= M(D_BIT);
        }
      }
      slides(d, x);
      if (full) pollPort(d, SNDDRV_POLL_SEQ);
    } else if (!M(D_PASS)) break;
  next:
    x += 2;
    M(D_BIT) <<= 1;
  }
  return x;
}

// $0377: the sequencer -- the music on its tempo accumulator, then the effects on theirs ($81 a tick).
static void sequencer(SndDrv* d) {
  if (!(M(D_FLAGS2) & 0x20)) {
    unsigned acc = M(D_TEMPO_ACC) + tempoAdd(d);
    M(D_TEMPO_ACC) = (uint8_t)acc;
    if (acc <= 0xFF) goto sfx;
  }
  if (seqPass(d, 0, 0x01, M(D_PLAYING), 1) >= 0x18) return;
  globalSlides(d);
  M(D_VOLCHG) &= (uint8_t)~M(D_SFX_VOICES);
  M(D_PITCHCHG) &= (uint8_t)~M(D_SFX_VOICES);
sfx:
  if (!(M(D_FLAGS2) & 0x20)) {
    unsigned acc = M(D_SFX_ACC) + 0x81;
    M(D_SFX_ACC) = (uint8_t)acc;
    if (acc <= 0xFF) return;
  }
  seqPass(d, 0x18, 0x10, xcn(M(D_SFX_VOICES)), 1);
}

// ===================================================================================================================
// The voices
// ===================================================================================================================

// $0ADE: a modulation's waveform step at the end of its period: the new current delta from the old one (cur), the
// step and the phase (3 bits); the depth's top bits (in $A5) pick the shape. *nph = the new phase.
static uint16_t waveStep(SndDrv* d, uint8_t ph, uint16_t step, uint16_t cur, uint8_t* nph) {
  uint8_t was = (uint8_t)(cur >> 8);
  uint8_t mode = M(D_BYTE);
  *nph = 0;
  if (mode < 0x80) {
    cur = neg16(cur);
    if (!ph) return cur;
    *nph = (uint8_t)(ph - 1);
    if (cur & 0x8000) return cur;
    return (uint16_t)((step >> 2) + cur);
  }
  if (mode < 0xC0) {
    cur = neg16(cur);
    if (!ph) return cur;
    *nph = (uint8_t)(ph - 1);
    if (!(cur & 0x8000)) return cur;
    return (uint16_t)(neg16((uint16_t)(step >> 2)) + cur);
  }
  if (!ph) return neg16(cur);
  *nph = (uint8_t)(ph - 1);
  if (cur & 0x8000) cur = neg16(cur);
  cur = (uint16_t)((step >> 2) + cur);
  return (was & 0x80) ? cur : neg16(cur);
}

// $12DE: pan + the pan sweep's signed offset, held to 0-255.
static uint8_t panAdd(uint8_t pan, uint8_t sw) {
  unsigned s = (unsigned)pan + sw;
  if (sw & 0x80) return s > 0xFF ? (uint8_t)s : 0;
  return s > 0xFF ? 0xFF : (uint8_t)s;
}

// $0A78: the voice's pitch registers -- the pitch plus the vibrato's offset, the music scaled by the pitch modifier
// ($AF: below $80 up, from $80 down), the effects' voices 6-7 by command 89's value.
static void pitchOut(SndDrv* d, int x, uint8_t reg) {
  uint16_t p = (uint16_t)(rw(d, (uint16_t)(T_PITCH + x)) + rw(d, (uint16_t)(T_VIB_OFS + x)));
  uint8_t lo = (uint8_t)p, hi = (uint8_t)(p >> 8);
  uint16_t out = p;
  if (x < 0x10) {
    uint8_t m = M(D_PMOD + 1);
    uint16_t t = (uint16_t)(m * hi + ((m * lo) >> 8));
    out = (m & 0x80) ? t : (uint16_t)((t << 1) + p);
  } else if (x >= 0x1C && M(D_SFXPITCH)) {
    uint8_t m = M(D_SFXPITCH);
    uint16_t t = (uint16_t)(m * hi + ((m * lo) >> 8));
    out = (uint16_t)((t << 1) + p);
  }
  dsp(d, reg, (uint8_t)out);
  dsp(d, (uint8_t)(reg + 1), (uint8_t)(out >> 8));
}

// $09F6: the voice's volume (when D_VOLCHG names it) and pitch (D_PITCHCHG) to the S-DSP.
static void voiceOut(SndDrv* d, int x) {
  if (!M(D_VOLCHG) && !M(D_PITCHCHG)) return;
  uint8_t vv = (uint8_t)(x & 0x0F);
  uint8_t reg = voiceReg(x);
  uint8_t bit = M(D_BIT);
  if (bit & M(D_VOLCHG)) {
    uint8_t pan = 0x80;
    if (!(M(D_FLAGS) & 1)) pan = (uint8_t)~panAdd(T(T_PAN + 1), T(T_SWEEP_ACC + 1));
    uint8_t v = T(T_VOL + 1), y = v;
    uint8_t tr = T(T_TREM_ACC + 1);
    if (tr) {
      uint16_t pr = (uint16_t)(v * (uint8_t)(tr << 1));
      if (tr & 0x80) y = (uint8_t)(pr >> 8);
      else {
        unsigned s = (pr >> 8) + v;
        y = (uint8_t)s;
        if ((y & 0x80) && s > 0xFF) y = 0x7F;
      }
    }
    y = (uint8_t)((y * M(D_PASSVOL)) >> 8);
    if (x < 0x10) {
      if (bit & M(D_MUTE)) y = 0;
      else { y = (uint8_t)((y * T(T_EXPR)) >> 8); y = (uint8_t)((y * M(D_MVOL)) >> 8); }
    }
    uint8_t l = (uint8_t)((y * pan) >> 8);
    M(D_VOLOUT + vv) = l;
    dsp(d, reg, l);
    uint8_t r = (uint8_t)((y * (uint8_t)~pan) >> 8);
    M(D_VOLOUT + (vv ^ 1)) = r;
    dsp(d, (uint8_t)(reg + 1), r);
  }
  if (bit & M(D_PITCHCHG)) pitchOut(d, x, (uint8_t)(reg | 2));
}

// $0903: a voice's tick -- the vibrato (unless delayed), the tremolo (unless delayed), then the output.
static void voice(SndDrv* d, int x) {
  uint8_t bit = M(D_BIT);
  uint8_t vd = M(P_VIB_DEPTH + x);
  if (vd) M(D_BYTE) = vd;   // (taken before the delay's test)
  if (vd && (x >= 0x10 || !M(P_VIB_DELAY_N + x))) {
    uint16_t cur = rw(d, (uint16_t)(T_VIB_CUR + x)), old = rw(d, (uint16_t)(T_VIB_ACC + x));
    uint16_t acc = (uint16_t)(old + cur);
    T(T_VIB_ACC) = (uint8_t)acc;
    if ((acc >> 8) != (old >> 8)) {
      T(T_VIB_ACC + 1) = (uint8_t)(acc >> 8);
      uint8_t h = (uint8_t)((uint16_t)(acc << 1) >> 8);
      uint16_t sc = rw(d, (uint16_t)(T_VIB_SCALE + x));
      uint16_t ofs = (uint16_t)((sc >> 8) * h + (((sc & 0xFF) * h) >> 8));
      if (h & 0x80) ofs = (uint16_t)(ofs - sc);
      ww(d, (uint16_t)(T_VIB_OFS + x), ofs);
      M(D_PITCHCHG) |= bit;
    }
    uint8_t n = (uint8_t)(T(T_VIB_RATE_N) - 1);
    if (!n) {
      uint8_t ph = T(T_PHASE), nph;
      uint16_t nv = waveStep(d, (uint8_t)(ph & 7), rw(d, (uint16_t)(T_VIB_STEP + x)), cur, &nph);
      ww(d, (uint16_t)(T_VIB_CUR + x), nv);
      T(T_PHASE) = (uint8_t)(nph | (ph & 0x70));
      n = T(T_VIB_RATE);
    }
    T(T_VIB_RATE_N) = n;
  }
  uint8_t td = M(P_TREM_DEPTH + x);
  if (td) M(D_BYTE) = td;
  if (td && !M(P_TREM_DELAY_N + x)) {
    uint16_t cur = rw(d, (uint16_t)(T_TREM_CUR + x)), old = rw(d, (uint16_t)(T_TREM_ACC + x));
    uint16_t acc = (uint16_t)(old + cur);
    T(T_TREM_ACC) = (uint8_t)acc;
    if ((acc >> 8) != (old >> 8)) { T(T_TREM_ACC + 1) = (uint8_t)(acc >> 8); M(D_VOLCHG) |= bit; }
    uint8_t n = (uint8_t)(T(T_TREM_RATE_N) - 1);
    if (!n) {
      uint8_t ph = T(T_PHASE), nph;
      uint16_t nv = waveStep(d, (uint8_t)(xcn(ph) & 7), rw(d, (uint16_t)(T_TREM_STEP + x)), cur, &nph);
      ww(d, (uint16_t)(T_TREM_CUR + x), nv);
      T(T_PHASE) = (uint8_t)(xcn(nph) | (ph & 0x07));
      n = T(T_TREM_RATE);
    }
    T(T_TREM_RATE_N) = n;
  }
  voiceOut(d, x);
}

// ===================================================================================================================
// The fades ($1492, every 7th tick)
// ===================================================================================================================

static int fadeStep(SndDrv* d, int k, uint8_t chg) {   // returns 1 when the fade ended at volume 0
  uint8_t n = M(A_FADE_N + k);
  if (!n) return 0;
  n--;
  M(A_FADE_N + k) = n;
  uint16_t o = rw(d, (uint16_t)(A_FADE + k)), v = (uint16_t)(o + rw(d, (uint16_t)(A_FADE_STEP + k)));
  ww(d, (uint16_t)(A_FADE + k), v);
  if ((v >> 8) != (o >> 8)) M(D_VOLCHG) |= chg;
  return !(v >> 8) && !n;
}

static void fades(SndDrv* d) {
  if (fadeStep(d, 0, M(D_PLAYING))) {   // the music faded out: it stops, the saved song kept
    uint8_t s = M(D_SAVED), n = M(D_NEXT);
    stopMusic(d);
    M(D_SAVED) = s; M(D_NEXT) = n;
  }
  if (fadeStep(d, 2, 0xC0)) stopSfxA(d);
  if (fadeStep(d, 4, 0x30)) stopSfxB(d);
  if (M(D_TMOD_N)) { M(D_TMOD_N)--; ww(d, D_TMOD, (uint16_t)(rw(d, D_TMOD) + rw(d, D_TMOD_STEP))); }
  if (M(D_PMOD_N)) {
    M(D_PMOD_N)--;
    uint8_t o = M(D_PMOD + 1);
    ww(d, D_PMOD, (uint16_t)(rw(d, D_PMOD) + rw(d, D_PMOD_STEP)));
    if (M(D_PMOD + 1) != o) M(D_PITCHCHG) |= M(D_PLAYING);
  }
}

// ===================================================================================================================
// Stopping
// ===================================================================================================================

static void stopMusic(SndDrv* d) {   // $0F17 (command F1)
  uint8_t a = (uint8_t)~M(D_SFX_VOICES);
  M(D_KOFF) |= a; M(D_KON) &= (uint8_t)~a;
  M(D_EON) &= (uint8_t)~a; M(D_PMON) &= (uint8_t)~a; M(D_NON) &= (uint8_t)~a;
  M(D_PLAYING) = 0; M(D_EON_M) = 0; M(D_PMON_M) = 0; M(D_NON_M) = 0;
  M(D_SONG) = 0xFF; M(D_SAVED) = 0xFF; M(D_NEXT) = 0xFF;
}

// $0F3F / $0F71: an effect group stops -- its voices keyed off, its tracks pointed at an end byte ($1DC1) two ticks on.
static void stopSfx(SndDrv* d, uint8_t a, uint8_t m, int x, uint8_t bit, uint8_t until) {
  M(D_KOFF) |= a; M(D_KON) &= (uint8_t)~a; M(D_KEYED_S) &= (uint8_t)~a;
  M(D_VOLCHG) &= (uint8_t)~a; M(D_PITCHCHG) &= (uint8_t)~a;
  M(D_BIT) = bit;
  do {
    int c = m >> 7;
    m = (uint8_t)(m << 1);
    if (c) { ww(d, (uint16_t)(D_TRK_PTR + x), R_END_TRACK); M(D_CNT + x) = 2; }
    x -= 2;
    M(D_BIT) >>= 1;
  } while (!(M(D_BIT) & until));
}
static void stopSfxA(SndDrv* d) { uint8_t a = (uint8_t)(M(D_SFX_VOICES) & 0xC0); stopSfx(d, a, a, 0x1E, 0x80, 0x20); }
static void stopSfxB(SndDrv* d) { uint8_t a = (uint8_t)(M(D_SFX_VOICES) & 0x30); stopSfx(d, a, (uint8_t)(a << 2), 0x1A, 0x20, 0x08); }

// ===================================================================================================================
// The tick
// ===================================================================================================================

// $02A0 / $0418: the 17 shadowed S-DSP registers (the list at $16FF / $1710: FIR, EFB, EVOL, EON, KOFF, FLG, NON,
// PMON, KON last), then the key shadows cleared.
static void flush(SndDrv* d) {
  for (int y = 17; y >= 1; y--) dsp(d, M(R_FLUSH_REG + y), M(M(R_FLUSH_SRC + y)));
  M(D_KOFF) = 0;
  M(D_KON) = 0;
}

// $1438 (meter mode, command FF 01): four voices' output levels against their volumes, on the four ports.
static void meter(SndDrv* d) {
  uint8_t out[4], src = 0xBE, reg = 0x09, flag = 0;
  if (M(D_FLAGS2) & 0x10) { src = 0xC6; reg = 0x49; flag = 0x80; }
  uint8_t end = (uint8_t)(src + 8);
  for (int k = 0; k < 4; k++) {
    uint8_t ox = dspIn(d, reg);
    uint8_t l = M(src++), r = M(src++);
    uint8_t hl = (uint8_t)(((ox * (uint8_t)(l << 1)) >> 8) & 0x70);
    uint8_t hr = (uint8_t)((ox * (uint8_t)(r << 1)) >> 8);
    out[k] = (uint8_t)((xcn(hr) & 7) | hl | flag);
    reg = (uint8_t)(reg + 0x10);
  }
  M(D_BIT) = reg; M(D_PASS) = end;
  port(d, 0, out[0]); port(d, 1, out[1]); port(d, 2, out[2]); port(d, 3, out[3]);
  M(D_FLAGS2) ^= 0x10;
}

// $0B5C: one nibble of the built-in sample's loop ($1DCB) flipped a tick -- a pulse whose width sweeps.
static void brrStep(SndDrv* d) {
  uint8_t a = M(D_BRR_STEP);
  if (a == 0x23) M(R_BRR_LOOP + 1) ^= 0x70;
  M(R_BRR_LOOP + (a >> 1)) ^= (a & 1) ? 0x07 : 0x70;
  a = (uint8_t)(M(D_BRR_STEP) + 1);
  if (a == 0x24) a = 3;
  else { uint8_t q = (uint8_t)(a >> 1); if (!spcDiv(&q, 0, 9)) a += 2; }
  M(D_BRR_STEP) = a;
}

void SndDrvTick(SndDrv* d) {
  if (!d->alive) return;
  uint8_t ev = M(D_EVOL + 1);
  if (M(D_FLAGS) & 0x08) ev = (ev & 0x80) ? 0x7F : (uint8_t)(ev << 1);   // command 8E: the echo louder
  M(D_EVOLOUT) = ev;
  flush(d);
  if (!(M(D_FLAGS2) & 0x80)) {
    port(d, 2, (uint8_t)(xcn(M(D_COND_MAX)) | M(D_MARK)));
    port(d, 3, (uint8_t)(((xcn(M(D_PLAYING)) | M(D_PLAYING)) & 0x0F) | M(D_SFX_VOICES)));
    port(d, 0, 0);
    port(d, 1, M(A_FADE_N));
  } else meter(d);
  if (M(D_FLAGS) & 0x10) return;   // paused
  if (!--M(D_FADE_DIV)) { M(D_FADE_DIV) = 7; fades(d); }
  sequencer(d);
  processQueue(d);
  if (M(D_FLAGS) & 0x10) return;
  // the music's voices (the effects' own left out), with the music fade
  int x = 0;
  M(D_BIT) = 0x01;
  M(D_PASS) = (uint8_t)(~M(D_SFX_VOICES) & M(D_PLAYING) & M(D_KEYED_M));
  M(D_PASSVOL) = M(A_FADE + 1);
  for (;;) {
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) {
      M(D_TRK) = (uint8_t)x;
      if (!M(D_SFX_VOICES)) voice(d, x); else voiceOut(d, x);   // while effects play, no music modulation
      pollPort(d, SNDDRV_POLL_VOICE);
    } else if (!M(D_PASS)) break;
    x += 2;
    M(D_BIT) <<= 1;
  }
  // the effects' voices 7-6 with fade A, then 5-4 with fade B
  x = 0x1E;
  M(D_BIT) = 0x80;
  M(D_PASS) = (uint8_t)(M(D_SFX_VOICES) & M(D_KEYED_S));
  M(D_PASSVOL) = M(A_FADE + 3);
  for (;;) {
    uint8_t c = (uint8_t)(M(D_PASS) >> 7);
    M(D_PASS) <<= 1;
    if (c) { M(D_TRK) = (uint8_t)x; voice(d, x); pollPort(d, SNDDRV_POLL_SFXA); }
    x -= 2;
    M(D_BIT) >>= 1;
    if (M(D_BIT) & 0x20) break;
  }
  M(D_PASSVOL) = M(A_FADE + 5);
  for (;;) {
    uint8_t c = (uint8_t)(M(D_PASS) >> 7);
    M(D_PASS) <<= 1;
    if (c) { M(D_TRK) = (uint8_t)x; voice(d, x); pollPort(d, SNDDRV_POLL_SFXB); }
    else if (!M(D_PASS)) break;
    x -= 2;
    M(D_BIT) >>= 1;
  }
  M(D_VOLCHG) = 0;
  M(D_PITCHCHG) = 0;
  processQueue(d);
  brrStep(d);
}

// $0415: the tick an upload's kind-5 blocks run between their bytes -- the flush, the fades, the music's step and
// its voices; no status, no queue, no effects.
void SndDrvUploadTick(SndDrv* d) {
  if (!d->alive || (M(D_FLAGS) & 0x10)) return;
  flush(d);
  if (!--M(D_FADE_DIV)) { M(D_FADE_DIV) = 7; fades(d); }
  unsigned acc = M(D_TEMPO_ACC) + tempoAdd(d);   // $0464
  M(D_TEMPO_ACC) = (uint8_t)acc;
  if (acc > 0xFF) { seqPass(d, 0, 0x01, M(D_PLAYING), 0); globalSlides(d); }
  int x = 0;
  M(D_BIT) = 0x01;
  M(D_PASS) = (uint8_t)(~M(D_SFX_VOICES) & M(D_PLAYING) & M(D_KEYED_M));
  M(D_PASSVOL) = M(A_FADE + 1);
  for (;;) {
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) { M(D_TRK) = (uint8_t)x; voice(d, x); }
    else if (!M(D_PASS)) break;
    x += 2;
    M(D_BIT) <<= 1;
  }
  M(D_VOLCHG) = 0;
  M(D_PITCHCHG) = 0;
}

// ===================================================================================================================
// The commands
// ===================================================================================================================

static void trackInit(SndDrv* d, int x) {   // $0CF6
  M(D_LOOP_SP + x) = (uint8_t)(x << 1);
  M(P_PSLIDE + x) = 0; ww(d, (uint16_t)(T_PSLIDE_D + x), 0); T(T_PSLIDE_LO) = 0;
  M(P_VIB_DEPTH + x) = 0; M(P_TREM_DEPTH + x) = 0; M(P_SWEEP + x) = 0;
  ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
  T(T_FINE) = 0; T(T_TRANSPOSE) = 0; T(T_VIB_SCALE) = 0;
  M(D_CNT + x) = 1;
  T(T_VIB_SCALE + 1) = 0x10;
}

// $13A9 / $13E5: the playing song's state kept aside ($F200-$F7FF to $F800, the direct page's $00-$7F to $FE00,
// page 1's $0100-$018F to $FF00) and brought back, the music then fading in.
static void songSave(SndDrv* d) {
  M(D_SAVED) = M(D_NEXT);
  memmove(&d->m[0xF800], &d->m[0xF200], 0x600);
  memmove(&d->m[0xFE00], &d->m[0x0000], 0x80);
  memmove(&d->m[0xFF00], &d->m[0x0100], 0x90);
}
static void songRestore(SndDrv* d) {
  M(D_SAVED) = 0xFF;
  memmove(&d->m[0xF200], &d->m[0xF800], 0x600);
  memmove(&d->m[0x0000], &d->m[0xFE00], 0x80);
  memmove(&d->m[0x0100], &d->m[0xFF00], 0x90);
  M(A_FADE_N) = 0x10; M(A_FADE) = 0; ww(d, A_FADE_STEP, 0x0EF0); M(A_FADE + 1) = 0x10;
}

// $0D29: before a song start -- the song already playing is refused (port 1 = $FF; the driver then waits for the
// CPU's next post, the protocol, and acks it), another is answered with its id.
static int songPre(SndDrv* d, uint8_t cmd) {
  uint8_t a = M(D_CMD + 1);
  int same = a == M(D_SONG);
  M(D_SONG) = a;
  if (!same) { port(d, 1, a); port(d, 0, cmd); return 1; }
  port(d, 1, 0xFF); port(d, 0, cmd);
  ackPorts(d);
  return 0;
}

// $0BE6: a song starts -- all keyed off, its master volume and echo delay taken, the upload session (the song's data),
// then either the saved song resumed or the new one's tracks set up from its header at $2000.
static void songStart(SndDrv* d, uint8_t variant) {
  M(D_VARIANT) = variant;
  dsp(d, 0x5C, 0xFF);
  M(D_NEXT) = M(D_CMD + 1);
  M(D_MVOL) = M(D_CMD + 2);
  uint8_t e = (uint8_t)(M(D_CMD + 3) & 0x0F);
  if (e != M(D_EDL)) { M(D_EDL) = e; M(D_FLAGS2) |= 0x03; echoChange(d); }
  d->host.upload(d->host.ctx);
  if (!d->alive) return;
  M(D_SFX_VOICES) = 0;
  M(D_FLAGS2) &= (uint8_t)~0x20;
  M(D_COND) = 0; M(D_COND_MAX) = 0;
  M(D_PITCHCHG) = 0;
  int resumed = 0;
  if (M(D_SAVED) == M(D_NEXT)) {
    songRestore(d);
    if (M(D_PLAYING)) {
      resumed = 1;
      M(D_FLG) = (uint8_t)((M(D_FLG) & 0xE0) | M(D_NCK_M));
      int x = 0;
      M(D_BIT) = 0x01;
      do {
        M(D_CNT + x)++;
        M(D_TRK) = (uint8_t)x;
        instrOut(d, x);
        uint8_t r = voiceReg(x);
        dsp(d, r, 0); dsp(d, (uint8_t)(r + 1), 0);
        pitchOut(d, x, (uint8_t)(r + 2));
        x += 2;
        M(D_BIT) <<= 1;
      } while (M(D_BIT));
      M(D_EON_S) = 0; M(D_NON_S) = 0; M(D_PMON_S) = 0;
      M(D_KON) = M(D_KEYED_M); M(D_KOFF) = 0; M(D_KEYED_S) = 0;
      M(D_VOLCHG) = M(D_PLAYING);
    }
  }
  if (!resumed) {
    M(D_PLAYING) = 0;
    ww(d, D_EON_M, 0); ww(d, D_NON_M, 0); ww(d, D_PMON_M, 0);
    M(D_KON) = 0; M(D_KOFF) = 0; ww(d, D_KEYED_M, 0);
    M(D_TIE_M) = 0; M(D_SLUR_M) = 0; M(D_ROLL_M) = 0; M(D_PERC) = 0; M(D_MARK) = 0;
    M(D_TEMPO + 1) = 1; M(D_TEMPO_ACC) = 0xFF;
    uint8_t y = M(D_VARIANT);
    for (int k = 0x10; k; k--, y--) M(0x01 + k) = M(0x2003 + y);
    ww(d, D_SONG_BASE, (uint16_t)(0x2024 - rw(d, A_SONG)));
    uint16_t empty = rw(d, A_SONG + 2);
    M(D_BIT) = 0x80;
    for (int x = 0x0E;; x -= 2) {
      uint16_t p = rw(d, (uint16_t)(D_TRK_PTR + x));
      if (p != empty) {
        M(D_PLAYING) |= M(D_BIT);
        ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)(p + rw(d, D_SONG_BASE)));
        trackInit(d, x);
        T(T_EXPR) = 0xFF;
      }
      M(D_BIT) >>= 1;
      if (!M(D_BIT)) break;
    }
    M(D_VOLCHG) = 0;
  }
  if (M(D_FLAGS2) & 0x01) {   // $0CCD: a new echo delay -- the old buffer's wait, then the new one's
    if (M(D_FLAGS2) & 0x02) { echoWait(d, D_EDL_OLD); M(D_ECHO_WAIT) = 0; echoStart(d); }
    echoWait(d, D_EDL);
    M(D_FLAGS2) &= (uint8_t)~0x01;
  }
  M(D_EON) = M(D_EON_M); M(D_NON) = M(D_NON_M); M(D_PMON) = M(D_PMON_M);
}

// $0D45 / $0D51 (commands 18 / 19): an effect starts on voices 7-6 or 5-4 -- its one or two tracks from the effect
// table at $2F00 (command byte 1 = the effect, byte 2 = the pan), whatever ran on those voices ended.
static void sfxStart(SndDrv* d, uint8_t cmd, int xs, uint8_t bs, uint8_t group) {
  port(d, 0, cmd);
  M(D_BIT) = bs;
  M(D_CMD + 3) = group;
  if (M(D_FLAGS) & 0x10) return;
  uint16_t tab = (uint16_t)(A_SFX_TAB + (M(D_CMD + 1) << 2));
  uint8_t got = 0;
  int x = xs, y = 3;
  while (y >= 0) {
    uint8_t hi = M(tab + y);
    if (hi) {
      M(D_TRK_PTR + x + 1) = hi;
      M(D_TRK_PTR + x) = M(tab + y - 1);
      got |= M(D_BIT);
      trackInit(d, x);
      M(D_CNT + x)++;
      T(T_VOL + 1) = 0x46; T(T_PAN + 1) = M(D_CMD + 2); T(T_OCT) = 0x48;
      T(T_VOL_N) = 0; T(T_PAN_N) = 0;
      setInstr(d, x, 2);
      x -= 2;
      M(D_BIT) >>= 1;
    }
    y -= 2;
  }
  uint8_t a = (uint8_t)((M(D_SFX_VOICES) & group) | got);
  M(D_KON) &= (uint8_t)~a; M(D_TIE_S) &= (uint8_t)~a; M(D_SLUR_S) &= (uint8_t)~a; M(D_ROLL_S) &= (uint8_t)~a;
  M(D_VOLCHG) &= (uint8_t)~a; M(D_PITCHCHG) &= (uint8_t)~a;
  M(D_KOFF) |= a;
  M(D_KEYED_S) &= (uint8_t)~a;
  M(D_BYTE) = a;
  x = 0x1E;
  M(D_BIT) = 0x80;
  do {
    int c = M(D_BYTE) >> 7;
    M(D_BYTE) <<= 1;
    if (c) endTrack(d, x);
    x -= 2;
    M(D_BIT) >>= 1;
  } while (!(M(D_BIT) & 0x08));
  M(D_SFX_VOICES) |= got;
  M(D_EON) &= (uint8_t)~got; M(D_PMON) &= (uint8_t)~got; M(D_NON) &= (uint8_t)~got;
  M(D_KEYED_M) &= (uint8_t)~got;
}

// $0E2F (commands 81-83): fade k (0 music, 2 effects A, 4 effects B) to byte 2 over byte 1 x 7 ticks.
static void fadeCmd(SndDrv* d, int k, uint8_t chg) {
  M(D_VOLCHG) |= chg;
  uint8_t tgt = M(D_CMD + 2), n = M(D_CMD + 1);
  M(A_FADE_N + k) = n;
  if (n) {
    int c = tgt >= M(A_FADE + k + 1);
    uint8_t diff = (uint8_t)(tgt - M(A_FADE + k + 1));
    if (diff) {
      ww(d, (uint16_t)(A_FADE_STEP + k), slideStep(d, diff, c));
      M(A_FADE + k) = 0;
      return;
    }
    M(A_FADE_N + k) = 0;
  }
  M(A_FADE + k) = 0; ww(d, (uint16_t)(A_FADE_STEP + k), 0); M(A_FADE + k + 1) = tgt;
}

// $0E82 / $0EAA (commands 86 / 87, both 85): the tempo or the pitch modifier to byte 2 over byte 1 x 7 ticks.
static void modCmd(SndDrv* d, uint8_t at, uint8_t stepAt, uint8_t nAt) {
  uint8_t tgt = M(D_CMD + 2), n = M(D_CMD + 1);
  M(nAt) = n;
  if (n) {
    M(D_BYTE) = (uint8_t)(M(at + 1) ^ 0x80);
    int c;
    uint8_t diff = sdiff(tgt, M(at + 1), &c);
    if (diff) { ww(d, stepAt, slideStep(d, diff, c)); M(at) = 0; return; }
    M(nAt) = 0;
  }
  M(at + 1) = tgt; M(at) = 0; ww(d, stepAt, 0);
}

static void reset(SndDrv* d) {   // $12D0: hand the SPC back to its IPL
  dsp(d, 0x6C, 0xE0);
  ctl(d, 0xF1, 0x80);
  d->alive = 0;
}

static void cmdHi(SndDrv* d, int k) {   // $0DFB: commands $80-$8F and $F0-$FF through the table at $16C0
  uint8_t p1 = M(D_CMD + 1);
  switch (k) {
  case 0x00: fadeCmd(d, 0, 0x7F); fadeCmd(d, 2, 0xC0); fadeCmd(d, 4, 0x30); break;
  case 0x01: fadeCmd(d, 0, 0x7F); break;
  case 0x02: fadeCmd(d, 2, 0xC0); break;
  case 0x03: fadeCmd(d, 4, 0x30); break;
  case 0x05: modCmd(d, D_PMOD, D_PMOD_STEP, D_PMOD_N); M(D_PITCHCHG) = 0xFF;
             modCmd(d, D_TMOD, D_TMOD_STEP, D_TMOD_N); break;
  case 0x06: modCmd(d, D_TMOD, D_TMOD_STEP, D_TMOD_N); break;
  case 0x07: modCmd(d, D_PMOD, D_PMOD_STEP, D_PMOD_N); M(D_PITCHCHG) = 0xFF; break;
  case 0x08: M(D_COND) = (uint8_t)(p1 & 0x0F); break;
  case 0x09: {
    int ch = p1 != M(D_SFXPITCH);
    M(D_SFXPITCH) = p1;
    if (ch) M(D_PITCHCHG) |= (uint8_t)(M(D_SFX_VOICES) & 0xC0);
    break;
  }
  case 0x0E: {   // 8E: loud -- the main volume $7F (else $40) and the echo doubled
    if (p1) M(D_FLAGS) |= 0x08; else M(D_FLAGS) &= (uint8_t)~0x08;
    uint8_t v = p1 ? 0x7F : 0x40;
    dsp(d, 0x0C, v); dsp(d, 0x1C, v);
    break;
  }
  case 0x10: stopMusic(d); stopSfxA(d); stopSfxB(d); break;
  case 0x11: stopMusic(d); break;
  case 0x12: if (p1 & 1) stopSfxB(d); else stopSfxA(d); break;
  case 0x13: if (p1) M(D_FLAGS) |= 0x01; else M(D_FLAGS) &= (uint8_t)~0x01; M(D_VOLCHG) = 0xFF; break;
  case 0x14: M(D_MUTE) = p1; M(D_VOLCHG) = 0xFF; break;
  case 0x15:   // F5: pause (the voices' ADSR off and silenced) / resume
    if (p1) {
      for (int y = 0x05; y < 0x80; y += 0x10) dsp(d, (uint8_t)y, (uint8_t)(dspIn(d, (uint8_t)y) & 0x7F));
      for (int y = 0x00; y < 0x80; y += 0x10) { dsp(d, (uint8_t)y, 0); dsp(d, (uint8_t)(y + 1), 0); }
      M(D_FLAGS) |= 0x10;
      M(D_VOLCHG) = 0; M(D_KON) = 0;
      memset(&d->m[D_VOLOUT], 0, 16);
    } else {
      for (int y = 0x05; y < 0x80; y += 0x10) dsp(d, (uint8_t)y, (uint8_t)(dspIn(d, (uint8_t)y) | 0x80));
      M(D_FLAGS) &= (uint8_t)~0x10;
      M(D_VOLCHG) = 0xFF;
    }
    break;
  case 0x16:   // F6: fast -- a step every tick, and timer 0 at 8 kHz
    if (p1) M(D_FLAGS2) |= 0x20; else M(D_FLAGS2) &= (uint8_t)~0x20;
    ctl(d, 0xF1, 0x02); ctl(d, 0xFA, p1 ? 0x01 : 0x2A); ctl(d, 0xF1, 0x03);
    break;
  case 0x18: M(D_MUTE) &= (uint8_t)~p1; M(D_VOLCHG) |= p1; break;
  case 0x19: M(D_MUTE) |= p1; M(D_VOLCHG) |= p1; break;
  case 0x1C: echoAlloc(d); break;
  case 0x1D: dsp(d, p1, M(D_CMD + 2)); break;
  case 0x1E: d->host.upload(d->host.ctx); break;
  case 0x1F:   // FF: 01 = the meter mode on/off, F0 = back to the IPL
    if (p1 == 0x01) { if (M(D_CMD + 2)) M(D_FLAGS2) |= 0x80; else M(D_FLAGS2) &= (uint8_t)~0x80; }
    else if (p1 == 0xF0) reset(d);
    break;
  default: break;   // $12DD
  }
}

// $0BAC: run the command at $8D-$90.
static void dispatch(SndDrv* d) {
  uint8_t c = M(D_CMD);
  if (c & 0x80) {
    port(d, 0, c);
    if (c >= 0xF0 || c < 0x90) cmdHi(d, c & 0x1F);
    return;
  }
  if (c < 0x10 || c >= 0x20) { port(d, 0, c); return; }
  switch (c & 0x0F) {   // the table at $1680, each song start preceded by $0D29
  case 0x0: if (songPre(d, c)) songStart(d, 0x10); break;
  case 0x1: if (songPre(d, c)) songStart(d, 0x20); break;
  case 0x4: if (songPre(d, c)) { songSave(d); songStart(d, 0x10); } break;
  case 0x5: if (songPre(d, c)) { songSave(d); songStart(d, 0x20); } break;
  case 0x8: sfxStart(d, c, 0x1E, 0x80, 0xC0); break;
  case 0x9: sfxStart(d, c, 0x1A, 0x20, 0x30); break;
  default: break;
  }
}

// ===================================================================================================================
// The API
// ===================================================================================================================

void SndDrvInit(SndDrv* d, const SndDrvHost* host) { d->host = *host; d->alive = 0; }
uint8_t* SndDrvMem(SndDrv* d) { return d->m; }

void SndDrvBoot(SndDrv* d) {   // $0200
  memset(d->m, 0, 0xF0);
  M(D_SONG) = 0xFF;
  ww(d, D_SAVED, 0xFFFF);
  dsp(d, 0x2C, 0); dsp(d, 0x3C, 0);
  for (int x = 0x40; x; x -= 4) M(0xF16E + x) = 0;
  dsp(d, 0x5D, 0x1E);
  for (int y = 0x07; y < 0x80; y += 0x10) dsp(d, (uint8_t)y, 0xA0);
  ctl(d, 0xF1, 0x30); ctl(d, 0xFA, 0x2A); ctl(d, 0xFB, 0x80); ctl(d, 0xF1, 0x03);
  d->alive = 1;
  M(D_CMD + 1) = 0x05;
  echoAlloc(d);
  M(D_KOFF) = 0xFF;
  M(D_FADE_DIV) = 7;
  for (int x = 6; x; x -= 2) { M(0xF15F + x) = 0xFF; M(0xF164 + x) = 0; }
  M(0x1E40) = 0xC2; M(0x1E41) = 0x1D; M(0x1E42) = 0xCB; M(0x1E43) = 0x1D;   // sample $10: the built-in pulse
  M(0x1FA0) = 0xFF; M(0x1FA1) = 0xE0; M(0x1F20) = 0xE1; M(0x1F21) = 0x00;   // instrument $10's ADSR and tuning
  dsp(d, 0x0C, 0x40); dsp(d, 0x1C, 0x40);
  M(D_BRR_STEP) = 2;
}

void SndDrvCommand(SndDrv* d, const uint8_t cmd[4]) {   // $0B9D-$0BAC
  if (!d->alive || !cmd[0]) return;
  M(D_CMD + 2) = cmd[2]; M(D_CMD + 3) = cmd[3]; M(D_CMD) = cmd[0]; M(D_CMD + 1) = cmd[1];
  ackPorts(d);
  dispatch(d);
}

int SndDrvUploadHeader(SndDrv* d, uint8_t kind, uint16_t dest) {   // $1029-$1056
  if (M(D_FLAGS2) & 0x01) echoWaitAccount(d);
  if (kind == 0xE0) return -1;
  int k = kind & 7;
  if ((dest >> 8) < 0x1E) k = 0;                                  // the driver's own code is never overwritten
  else if ((dest >> 8) == 0x2F) { stopSfxA(d); stopSfxB(d); }      // a new effect table: the effects stop first
  return k;
}

void SndDrvUploadReset(SndDrv* d) { M(D_CMD + 1) = 0xF0; reset(d); }
