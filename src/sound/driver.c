// ============================================================================
// sound/driver.c -- the sound processor's program as native C (see driver.h). Each routine names the address it ports
// (an ARAM address of block 0, or of the boot ROM at $FFC0); disasm/notes/audio_spc_driver.md reads each of them.
//
// Conventions: x is the track index the driver keeps in X, twice the track number (0-$0E the music tracks, $10-$1E
// the effect tracks; an effect track's voice is the one its bit in D_BIT names). The state is d->m in the sound
// processor's RAM layout (driver.h): M() a byte of it, T() a per-track byte (field + x). The arithmetic is the
// driver's own (8-bit wrap, its carries and its 8x8 multiplies), written as C.
//
// The marks: AT(pc) says the program has reached pc; the walker (SndClockAt) charges every instruction from the last
// mark -- straight on, a conditional branch counted taken when the mark is its target, a jump followed, a return
// landing on the mark. So every basic block the original runs has a mark: a branch's fall-through where its target
// is also reached, the return address after each CALL, and inside each loop a mark other than its head. An access the
// outside sees goes through a helper that names its instruction (dspW, portR, timerR, ramW, ...): the access then
// happens at the cycle the original makes it.
// ============================================================================
#include "driver.h"

#include <setjmp.h>
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

// ===================================================================================================================
// The SPC700's time (timing.h): the walk through the cartridge's code
// ===================================================================================================================

#define AT(pc) SndClockAt(d, (uint16_t)(pc))

static void charge(SndDrv* d, uint16_t pc, int taken) {
  SndClock* c = &d->clk;
  const SndIns* in = &c->tim->ins[pc];
  while (c->cyc >= c->want && c->boundary) c->boundary(c->boundaryCtx);   // the host's time: stop before it
  if (pc == 0x0299 && c->loopTop) c->loopTop(c->loopTopCtx);
  if (c->trace) c->trace(c->traceCtx, pc, c->cyc);
  c->cyc += taken ? in->cycTaken : in->cyc;
}

void SndClockAt(SndDrv* d, uint16_t pc) {
  SndClock* c = &d->clk;
  if (!c->tim) { c->ip = pc; return; }
  for (int guard = 0; c->ip != pc; guard++) {
    const SndIns* in = &c->tim->ins[c->ip];
    if (!in->len || guard > 20000) {
      if (!c->err) { c->err = 1; c->errAt = c->ip; c->errTo = pc; }
      c->ip = pc;
      break;
    }
    uint16_t next = (uint16_t)(c->ip + in->len);
    switch (in->kind) {
    case SND_INS_COND:
      if (in->target == pc && in->target != next) { charge(d, c->ip, 1); c->ip = pc; }
      else { charge(d, c->ip, 0); c->ip = next; }
      break;
    case SND_INS_JUMP: charge(d, c->ip, 0); c->ip = in->target; break;
    case SND_INS_RETURN: charge(d, c->ip, 0); c->ip = pc; break;
    default: charge(d, c->ip, 0); c->ip = next; break;
    }
  }
}

// The instruction at pc runs now (a plain one: an access the outside sees); then ioAt gives each access its cycle.
static void insAt(SndDrv* d, uint16_t pc) {
  AT(pc);
  SndClock* c = &d->clk;
  c->insStart = c->cyc;
  c->insPc = pc;
  if (c->tim && c->tim->ins[pc].len) { charge(d, pc, 0); c->ip = (uint16_t)(pc + c->tim->ins[pc].len); }
}
static void ioAt(SndDrv* d, uint8_t reg, int write) {
  SndClock* c = &d->clk;
  if (!c->tim) return;
  const SndIns* in = &c->tim->ins[c->insPc];
  for (int k = 0; k < in->nio; k++)
    if (in->ioReg[k] == reg && in->ioWrite[k] == write) { c->io = c->insStart + in->ioIdx[k] + 1; return; }
  if (!c->err) { c->err = 2; c->errAt = c->insPc; c->errTo = reg; }
}
static void wAt(SndDrv* d, int nth) {   // the instruction's nth RAM write
  SndClock* c = &d->clk;
  if (!c->tim) return;
  const SndIns* in = &c->tim->ins[c->insPc];
  c->io = c->insStart + (nth < in->nw ? in->wIdx[nth] : in->cyc - 1) + 1;
}

// --- the accesses the outside sees --------------------------------------------------------------------------------
// The S-DSP ignores writes to $80-$FF (they mirror the read-only view): the sink sees $00-$7F only.
static void dspW(SndDrv* d, uint16_t pc, uint8_t reg, uint8_t val) {
  insAt(d, pc); ioAt(d, 0xF3, 1);
  if (reg < 0x80) d->host.dspWrite(d->host.ctx, reg, val);
}
static uint8_t dspR(SndDrv* d, uint16_t pc, uint8_t reg) {
  insAt(d, pc); ioAt(d, 0xF3, 0);
  return d->host.dspRead(d->host.ctx, (uint8_t)(reg & 0x7F));
}
static void dsp1(SndDrv* d, uint16_t ret, uint8_t reg, uint8_t val) {   // `CALL $07A3` (Y = reg, A = val)
  AT(0x07A3); dspW(d, 0x07A5, reg, val); AT(ret);
}
static uint8_t portR(SndDrv* d, uint16_t pc, int k) { insAt(d, pc); ioAt(d, (uint8_t)(0xF4 + k), 0); return d->host.portRead(d->host.ctx, k); }
static uint8_t portR2(SndDrv* d, int k) { ioAt(d, (uint8_t)(0xF4 + k), 0); return d->host.portRead(d->host.ctx, k); }   // (the same MOVW)
static void portW(SndDrv* d, uint16_t pc, int k, uint8_t v) { insAt(d, pc); ioAt(d, (uint8_t)(0xF4 + k), 1); d->host.portWrite(d->host.ctx, k, v); }
static void portW2(SndDrv* d, int k, uint8_t v) { ioAt(d, (uint8_t)(0xF4 + k), 1); d->host.portWrite(d->host.ctx, k, v); }
static uint8_t timerR(SndDrv* d, uint16_t pc, int k) { insAt(d, pc); ioAt(d, (uint8_t)(0xFD + k), 0); return d->host.timerRead(d->host.ctx, k); }
static void ctlW(SndDrv* d, uint16_t pc, uint8_t reg, uint8_t v) { insAt(d, pc); ioAt(d, reg, 1); d->host.control(d->host.ctx, reg, v); }
// A store into memory the S-DSP reads (the sample directory, the samples): the S-DSP must see it on its cycle.
static void ramW(SndDrv* d, uint16_t pc, uint16_t adr, uint8_t v) {
  insAt(d, pc); wAt(d, 0);
  if (d->host.ramWrite) d->host.ramWrite(d->host.ctx, adr, v);
  M(adr) = v;
}

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
static void stopMusic(SndDrv* d);
static void stopSfxA(SndDrv* d);
static void stopSfxB(SndDrv* d);
static void echoAlloc(SndDrv* d);
static void pitchOut(SndDrv* d, int x, uint8_t reg);
static void upload(SndDrv* d, uint8_t first);
static void ipl(SndDrv* d);
static void driverBoot(SndDrv* d);
static void mainLoop(SndDrv* d);
static void toLoop(SndDrv* d);
static void toIpl(SndDrv* d);

// ===================================================================================================================
// The ports, the queue
// ===================================================================================================================

static void ackPorts(SndDrv* d, uint16_t ret) {   // `CALL $07A8`: clear ports 0-1 (the ack)
  AT(0x07A8); ctlW(d, 0x07A8, 0xF1, 0x13); ctlW(d, 0x07AB, 0xF1, 0x03); AT(ret);
}

// $0892: a command the CPU posted while the tick runs is queued (four bytes at $F172 + $D7) and echoed; song starts
// ($10-$17) and the upload ($FE) stay on the port for the main loop. `ret` = the caller's address after the CALL.
static void pollPort(SndDrv* d, uint16_t ret) {
  uint8_t a;
  for (;;) {
    a = portR(d, 0x0892, 0);
    if (!a) { AT(0x08CC); AT(ret); return; }
    AT(0x0896);
    if (portR(d, 0x0896, 0) == a) break;
  }
  AT(0x089A);
  if (a == 0xFE) { AT(0x08CC); AT(ret); return; }
  AT(0x089E);
  if (a >= 0x10) {
    AT(0x08A2);
    if (a < 0x18) { AT(0x08CC); AT(ret); return; }
  }
  AT(0x08A6);
  uint8_t q = M(D_QHEAD);
  M(A_QUEUE + q) = a;
  M(A_QUEUE + q + 1) = portR(d, 0x08AD, 1);
  M(A_QUEUE + q + 2) = portR(d, 0x08B2, 2);
  M(A_QUEUE + q + 3) = portR(d, 0x08B7, 3);
  if ((uint8_t)(q + 4) < 0x40) { AT(0x08C4); M(D_QHEAD) = (uint8_t)(q + 4); }   // a full queue overwrites its last entry
  AT(0x08C6);
  ackPorts(d, 0x08CA);
  portW(d, 0x08CA, 0, a);
  AT(0x08CC); AT(ret);
}

// $08CD: run the queued commands, then empty the queue (re-entered by the echo wait inside a command).
static void processQueue(SndDrv* d) {
  int x = 0;
  AT(0x08CD);
  for (;;) {
    AT(0x08CF);
    if (!M(A_QUEUE + x)) break;
    AT(0x08D4);
    M(D_CMD) = M(A_QUEUE + x); M(D_CMD + 1) = M(A_QUEUE + x + 1);
    M(D_CMD + 2) = M(A_QUEUE + x + 2); M(D_CMD + 3) = M(A_QUEUE + x + 3);
    dispatch(d);            // (CALL $0BAC: dispatch marks its own way in and returns to $08E9)
    AT(0x08E9);
    x += 4;
    if (x == 0x40) { AT(0x08F2); goto clear; }
  }
  AT(0x08F2);
clear:
  if (x) {
    AT(0x08F5);
    for (int k = x; k > 0; k -= 4) { AT(0x08F7); M(A_QUEUE - 4 + k) = 0; AT(0x08FE); }   // `$F16E+X` for X = x .. 4
  }
  AT(0x0900);
  M(D_QHEAD) = 0;
}

// $12F9: wait until timer 1 (16 ms) has counted to the echo delay at `at` ($81 or $82) from $83 on -- the S-DSP
// needs EDL x 16 ms to clear the buffer -- answering the port meanwhile; then run what was queued.
static void echoWait(SndDrv* d, uint8_t at) {
  AT(0x12F9);
  M(D_QHEAD) = 0;
  uint8_t target = M(at);
  if (!target) { AT(0x1315); return; }
  AT(0x12FF);
  uint8_t a = M(D_ECHO_WAIT);
  AT(0x130F);
  while (a < target) {
    for (;;) {   // $1303: after a poll the ROM reads the timer again without the compare
      if (timerR(d, 0x1303, 1)) { AT(0x130E); a++; break; }
      AT(0x1307);
      pollPort(d, 0x130B);
    }
    AT(0x130F);
  }
  AT(0x1312);
  processQueue(d);
  AT(0x1315);
}

// ===================================================================================================================
// The S-DSP side helpers
// ===================================================================================================================

static void timer1Restart(SndDrv* d, uint16_t pc) {   // `$F1=1 / MOV Y,$FE / $F1=3`: timer 1 from 0
  ctlW(d, pc, 0xF1, 0x01); timerR(d, (uint16_t)(pc + 3), 1); ctlW(d, (uint16_t)(pc + 5), 0xF1, 0x03);
}

// $1352: stop the echo (writes off, EON/EFB/EVOL 0), note the old delay, set the new EDL and its buffer (ESA = $F1 -
// EDL x 8: the buffer ends at $F100), restart timer 1.
static void echoStop(SndDrv* d) {
  AT(0x1352);
  dspW(d, 0x1359, 0x6C, (uint8_t)(dspR(d, 0x1355, 0x6C) | 0x20));
  dsp1(d, 0x1362, 0x4D, 0); dsp1(d, 0x1367, 0x0D, 0); dsp1(d, 0x136C, 0x2C, 0); dsp1(d, 0x1371, 0x3C, 0);
  M(D_EDL_OLD) = (uint8_t)(dspR(d, 0x1375, 0x7D) & 0x0F);
  dsp1(d, 0x1380, 0x7D, M(D_EDL));
  dsp1(d, 0x138E, 0x6D, (uint8_t)(0xF1 - (uint8_t)(M(D_EDL) << 3)));
  timer1Restart(d, 0x138E);
  AT(0x1396);
}

// $1397: restart timer 1 and let the S-DSP write the echo again.
static void echoStart(SndDrv* d) {
  AT(0x1397);
  timer1Restart(d, 0x1397);
  dspW(d, 0x13A6, 0x6C, (uint8_t)(dspR(d, 0x13A2, 0x6C) & 0xDF));
  AT(0x13A8);
}

// $132E (command FC; the boot): a new echo delay -- stop the echo, wait out the old buffer and the new one.
static void echoAlloc(SndDrv* d) {
  AT(0x132E);
  uint8_t a = (uint8_t)(M(D_CMD + 1) & 0x0F);
  if (a == M(D_EDL)) { AT(0x1351); return; }
  AT(0x1336);
  M(D_ECHO_WAIT) = 0;
  M(D_EDL) = a;
  echoStop(d);
  AT(0x133E);
  echoWait(d, D_EDL_OLD);
  AT(0x1343);
  echoStart(d);
  AT(0x1346);
  echoWait(d, D_EDL);
  AT(0x134B);
  if (!M(D_PLAYING)) { AT(0x134F); M(D_EVOL + 1) = 0; }
  AT(0x1351);
}

// $1316 (a song start with a new delay): stop the echo; with no old buffer to wait for, start it again at once.
static void echoChange(SndDrv* d) {
  AT(0x1316);
  echoStop(d);
  AT(0x1319);
  M(D_ECHO_WAIT) = 0;
  if (M(D_EDL_OLD)) { AT(0x1320); return; }
  AT(0x1321);
  M(D_FLAGS2) &= (uint8_t)~0x02;
  echoStart(d);   // (JMP $1397: its RET is this routine's)
}

// $1AED / $1B0C: the instrument's sample (SRCN) and, unless the effects own the music voice, its ADSR.
static void adsrOut(SndDrv* d, int x) {   // $1B0C (reached by a BRA, ends on `JMP $07A3`'s RET)
  AT(0x1B0C);
  if (x < 0x10) {
    AT(0x1B10);
    if (M(D_SFX_VOICES) & M(D_BIT)) { AT(0x1B16); return; }
  }
  AT(0x1B17);
  uint8_t y = (uint8_t)(voiceReg(x) | 5);
  dsp1(d, 0x1B23, y, T(T_ADSR));
  AT(0x07A3); dspW(d, 0x07A5, (uint8_t)(y + 1), T(T_ADSR + 1)); AT(0x07A7);   // JMP $07A3
}
static void instrOut(SndDrv* d, int x) {   // $1AED
  AT(0x1AED);
  insAt(d, 0x1AF6);   // MOV $F2,A
  insAt(d, 0x1AF8); ioAt(d, 0xF3, 1);
  d->host.dspWrite(d->host.ctx, (uint8_t)(voiceReg(x) | 4), T(T_INSTR));
  adsrOut(d, x);
}

// $1ACF (event DC; percussion; the effects' start): the track's instrument, its tuning and ADSR from the tables.
static void setInstr(SndDrv* d, int x, uint8_t a) {
  AT(0x1ACF);
  T(T_INSTR) = a;
  uint8_t y = (uint8_t)(a << 1);
  T(T_TUNE) = M(A_TUNING + y); T(T_TUNE + 1) = M(A_TUNING + y + 1);
  T(T_ADSR) = M(A_ADSR_TAB + y); T(T_ADSR + 1) = M(A_ADSR_TAB + y + 1);
  AT(0x1AEC);
}

// $1A21 / $1A48 / $1AAA: the EON, FLG (noise clock) + NON and PMON shadows from the music's and the effects' masks.
static void eonShadow(SndDrv* d) { AT(0x1A21); M(D_EON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_EON_M)) | M(D_EON_S)); AT(0x1A2B); }
static void nonShadow(SndDrv* d) {
  AT(0x1A48);
  uint8_t s = (uint8_t)(M(D_NON_S) & ~1);
  if (M(D_NON_S)) { AT(0x1A59); M(D_FLG) = (uint8_t)((M(D_FLG) & 0xE0) | M(D_NCK_S)); }
  else { AT(0x1A55); M(D_FLG) = (uint8_t)((M(D_FLG) & 0xE0) | M(D_NCK_M)); }
  AT(0x1A5B);
  M(D_NON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_NON_M)) | s);
  AT(0x1A67);
}
static void pmonShadow(SndDrv* d) { AT(0x1AAA); M(D_PMON) = (uint8_t)((~M(D_SFX_VOICES) & M(D_PMON_M)) | M(D_PMON_S)); AT(0x1AB4); }

static void echoOff(SndDrv* d, int x) {    // $1A2C
  AT(0x1A2C);
  if (x < 0x10) { AT(0x1A32); M(D_EON_M) &= (uint8_t)~M(D_BIT); } else { AT(0x1A37); M(D_EON_S) &= (uint8_t)~M(D_BIT); }
  eonShadow(d);
}
static void noiseOff(SndDrv* d, int x) {   // $1A68
  AT(0x1A68);
  if (x < 0x10) { AT(0x1A6E); M(D_NON_M) &= (uint8_t)~M(D_BIT); } else { AT(0x1A73); M(D_NON_S) &= (uint8_t)~M(D_BIT); }
  nonShadow(d);
}
static void pmodOff(SndDrv* d, int x) {    // $1AB5
  AT(0x1AB5);
  if (x < 0x10) { AT(0x1ABB); M(D_PMON_M) &= (uint8_t)~M(D_BIT); } else { AT(0x1AC0); M(D_PMON_S) &= (uint8_t)~M(D_BIT); }
  pmonShadow(d);
}

// $1D4C: the track ended -- the step stops; the music's last track ends the song; an effect gives its voice back.
// (`pop` = entered at $1D4A, the event's return address dropped.)
static void endTrack(SndDrv* d, int x) {
  AT(0x1D4C);
  M(D_FLAGS2) |= 0x08;
  uint8_t b = M(D_BIT);
  if (x < 0x10) {
    AT(0x1D54);
    uint8_t old = M(D_PLAYING);
    M(D_PLAYING) = (uint8_t)(old & ~b);
    if (b == old) { AT(0x1D59); M(D_SONG) = 0xFF; }   // TCLR1's Z: the bit was all that was left
  } else {
    AT(0x1D5E);
    M(D_SFX_VOICES) &= (uint8_t)~b;
    M(D_SLURON_M) &= (uint8_t)~b;     // the ROM clears the music's slur mask here ($5E), not the effects' ($5F)
  }
  AT(0x1D64);
  noiseOff(d, x);
  AT(0x1D67);
  pmodOff(d, x);
  AT(0x1D6A);
  echoOff(d, x);   // (JMP $1A2C: its RET is this routine's)
}

// ===================================================================================================================
// Reading a track, pitches
// ===================================================================================================================

static uint8_t trkRead(SndDrv* d, int x) {   // $0753
  AT(0x0753);
  uint16_t p = rw(d, D_TRK_PTR + x);
  uint8_t a = M(p);
  M(D_BYTE) = a;
  ww(d, D_TRK_PTR + x, (uint16_t)(p + 1));
  if (!(uint8_t)(p + 1)) AT(0x075B);
  AT(0x075D);
  return a;
}

// $06BD: a note's pitch -- the semitone's word scaled by the instrument's tuning, shifted by the octave.
static uint16_t pitchOf(SndDrv* d, int x, uint8_t note) {
  AT(0x06BD);
  uint8_t oct = note;
  uint8_t semi = spcDiv(&oct, 0, 12);
  uint8_t lo = M(R_SEMITONE + semi * 2), hi = M(R_SEMITONE + semi * 2 + 1);
  uint8_t mul = (uint8_t)(T(T_TUNE) + T(T_FINE));
  uint16_t t = (uint16_t)(hi * mul + ((lo * mul) >> 8));
  if (T(T_TUNE + 1)) { AT(0x06F1); t = (uint16_t)(t + (((t >> 8) * T(T_TUNE + 1)) >> 8)); }
  else AT(0x06F9);
  AT(0x06FB);
  if (!(mul & 0x80)) { AT(0x06FE); t = (uint16_t)(t + (hi << 8 | lo)); }
  AT(0x0700);
  uint8_t a = 4;
  if (oct & 0x80) {                         // BMI
    do { AT(0x0717); t >>= 1; a--; AT(0x071C); } while (a != oct);
    AT(0x071F);
  } else if (!oct) {                        // BEQ
    AT(0x0708);
    do { AT(0x0717); t >>= 1; a--; AT(0x071C); } while (a != oct);
    AT(0x071F);
  } else if (a >= oct) {                    // CMP A,$A4 / BCS
    AT(0x0708); AT(0x070A); AT(0x071C);
    while (a != oct) { AT(0x0717); t >>= 1; a--; AT(0x071C); }
    AT(0x071F);
  } else {
    AT(0x0708); AT(0x070A);
    do { AT(0x070E); t = (uint16_t)(t << 1); a++; AT(0x0713); } while (a != oct);
    AT(0x0716);
  }
  return t;
}

// $1908: a modulation's start -- a quarter step while its delay runs, the step negated for the depth's $80-$BF
// shape; returns the phase byte: `base` with the phase 7 while delayed, 0 otherwise.
static uint8_t modStart(SndDrv* d, uint16_t* step, uint8_t base, int delayed) {
  AT(0x1908);
  if (delayed) { AT(0x190B); *step >>= 2; }
  AT(0x1913);
  if (M(D_BYTE) >= 0x80) {
    AT(0x1918);
    if (M(D_BYTE) < 0xC0) { AT(0x191D); *step = neg16(*step); }
  }
  AT(0x1925);
  if (delayed) { AT(0x1928); AT(0x192D); return (uint8_t)(base | 7); }
  AT(0x192C); AT(0x192D);
  return base;
}

// $18DE: a modulation's step from its depth (low 6 bits) and its rate: (depth + 1) x 256 / rate, a multiple of 4.
static uint16_t modStep(SndDrv* d, uint8_t depth, uint8_t rate) {
  AT(0x18DE);
  uint8_t a = (uint8_t)((depth & 0x3F) + 1);
  uint16_t v;
  if (!rate) { AT(0x18EF); v = a; }
  else { AT(0x18E9); uint8_t q = a; uint8_t r = spcDiv(&q, 0, rate); uint8_t lo = 0; spcDiv(&lo, r, rate); v = (uint16_t)(q << 8 | lo); }
  AT(0x18EF);
  v >>= 2;
  if (!v) { AT(0x18FD); v = 1; }
  AT(0x18FF);
  AT(0x1907);
  return (uint16_t)(v << 2);
}

// $18A2: the vibrato restarts (each note, and event C9).
static void vibReset(SndDrv* d, int x) {
  AT(0x18A2);
  ww(d, T_VIB_ACC + x, 0);
  T(T_VIB_RATE_N) = T(T_VIB_RATE);
  uint16_t step = rw(d, T_VIB_STEP + x);
  uint8_t delay;
  if (x < 0x10) { AT(0x18C7); delay = M(P_VIB_DELAY + x); }
  else { AT(0x18C3); delay = 0; }
  AT(0x18CA);
  M(P_VIB_DELAY_N + x) = delay;
  T(T_PHASE) = modStart(d, &step, (uint8_t)(T(T_PHASE) & 0x70), delay != 0);
  AT(0x18D0);
  ww(d, T_VIB_CUR + x, step);
  AT(0x18DD);
}

// $1960: the tremolo restarts (each note, and event CB).
static void tremReset(SndDrv* d, int x) {
  AT(0x1960);
  T(T_TREM_RATE_N) = T(T_TREM_RATE);
  uint16_t step = rw(d, T_TREM_STEP + x);
  uint8_t delay = M(P_TREM_DELAY + x);
  M(P_TREM_DELAY_N + x) = delay;
  T(T_PHASE) = xcn(modStart(d, &step, xcn((uint8_t)(T(T_PHASE) & 0x07)), delay != 0));
  AT(0x197F);
  ww(d, T_TREM_CUR + x, step);
  AT(0x198D);
}

// $0720: a percussion note -- instrument, note and (unless negative) pan from the table at $F120.
static uint8_t percNote(SndDrv* d, int x) {
  AT(0x0720);
  uint8_t y = (uint8_t)(M(D_BYTE) * 3);
  uint8_t v = M(A_PERC_TAB + 2 + y);
  if (!(v & 0x80)) { AT(0x072B); T(T_PAN + 1) = (uint8_t)(v << 1); }
  AT(0x072F);
  M(D_BYTE) = M(A_PERC_TAB + 1 + y);
  setInstr(d, x, M(A_PERC_TAB + y));
  AT(0x073A);
  AT(0x073C);
  return M(D_BYTE);
}

// $0E5F: a slide's step -- the difference (its sign in c: 1 = up) times 256 over the slide's ticks (the command's
// second byte, $8E), the sign put back.
static uint16_t slideStep(SndDrv* d, uint8_t a, int c) {
  AT(0x0E5F);
  if (!c) { AT(0x0E62); a = (uint8_t)(~a + 1); }
  AT(0x0E65);
  uint8_t n = M(D_CMD + 1);
  uint8_t q = a, r = spcDiv(&q, 0, n), lo = 0;
  spcDiv(&lo, r, n);
  uint16_t v = (uint16_t)(q << 8 | lo);
  if (!c) { AT(0x0E74); v = neg16(v); }
  AT(0x0E7C);
  AT(0x0E7E);
  return v;
}

static uint16_t sfxPtr(SndDrv* d, uint8_t a, uint8_t y) {   // $1BBC: the effect table's word at a x 4 + y
  AT(0x1BBC);
  uint16_t v = rw(d, (uint16_t)(A_SFX_TAB + y + (a << 2)));
  AT(0x1BD8);
  return v;
}

// ===================================================================================================================
// The sequence events $C4-$FF ($073D through the table at $1554). `a` is the first operand (0 for an event without
// one, whose $A5 then holds the event's index). Returns 1 when the track ended (the step stops).
// ===================================================================================================================

static void setVol(SndDrv* d, int x, uint8_t a) {   // $175F
  AT(0x175F);
  T(T_VOL + 1) = (uint8_t)(a & 0x7F); T(T_VOL) = 0;
  M(D_VOLCHG) |= M(D_BIT);
  T(T_VOL_N) = 0;
}
static void setPan(SndDrv* d, int x, uint8_t a) {   // $17BA (the music's tracks only)
  AT(0x17BA);
  if (x >= 0x10) { AT(0x17CD); return; }
  AT(0x17BE);
  T(T_PAN + 1) = (uint8_t)(a << 1); T(T_PAN) = 0;
  M(D_VOLCHG) |= M(D_BIT);
  T(T_PAN_N) = 0;
}
static void setTempo(SndDrv* d, uint8_t a) {   // $1722: the operand plus 20/256 of it
  AT(0x1722);
  M(D_TEMPO + 1) = (uint8_t)(((a * 20) >> 8) + M(D_BYTE));
  M(D_TEMPO) = 0; M(D_TEMPO_N) = 0;
}
static void setEvol(SndDrv* d, uint8_t a) { AT(0x1791); M(D_EVOL + 1) = a; M(D_EVOL) = 0; M(D_EVOL_N) = 0; }

// The signed slides (F3, F7, the FIR, the modifiers) compare with bit 7 flipped.
static uint8_t sdiff(uint8_t to, uint8_t from, int* c) {
  uint8_t t = (uint8_t)(to ^ 0x80), f = (uint8_t)(from ^ 0x80);
  *c = t >= f;
  return (uint8_t)(t - f);
}

static int event(SndDrv* d, int x, uint8_t ev) {
  AT(0x073D);
  uint8_t idx = (uint8_t)(ev - 0xC4);
  M(D_BYTE) = idx;
  uint8_t a = 0, b, c8;
  int c;
  if (M(R_OPCOUNT + idx)) a = trkRead(d, x);   // BNE $0753: the first operand, and $0753's RET is the dispatch
  else AT(0x0752);
  uint8_t bit = M(D_BIT);
  switch (ev) {
  case 0xC4: setVol(d, x, a); break;
  case 0xC5:   // volume slide: ticks, target
    AT(0x1770);
    T(T_VOL_N) = a; M(D_CMD + 1) = a;
    b = (uint8_t)(trkRead(d, x) & 0x7F);
    AT(0x1778);
    if (!M(D_CMD + 1)) { setVol(d, x, b); break; }
    AT(0x177E);
    c = b >= T(T_VOL + 1); c8 = (uint8_t)(b - T(T_VOL + 1));
    if (!c8) { AT(0x176C); T(T_VOL_N) = 0; break; }
    AT(0x1784);
    ww(d, (uint16_t)(T_VOL_STEP + x), slideStep(d, c8, c));
    AT(0x1787);
    break;
  case 0xC6: setPan(d, x, a); break;
  case 0xC7:   // pan slide: ticks, target
    AT(0x17CE);
    T(T_PAN_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    AT(0x17D6);
    if (x >= 0x10) { AT(0x17F1); break; }
    AT(0x17DA);
    if (!M(D_CMD + 1)) { setPan(d, x, b); break; }
    AT(0x17DE);
    b = (uint8_t)(b << 1);
    c = b >= T(T_PAN + 1); c8 = (uint8_t)(b - T(T_PAN + 1));
    if (!c8) { AT(0x17CA); T(T_PAN_N) = 0; break; }
    AT(0x17E5);
    ww(d, (uint16_t)(T_PAN_STEP + x), slideStep(d, c8, c));
    AT(0x17E8);
    break;
  case 0xC8:   // the next note's pitch slide: ticks, semitones
    AT(0x17F2);
    T(T_PSLIDE_N) = (uint8_t)(a + 1);
    M(P_PSLIDE + x) = trkRead(d, x);
    AT(0x17F9);
    break;
  case 0xC9: {   // vibrato: delay (an effect's: the depth table's index), rate, depth
    AT(0x186E);
    if (x >= 0x10) { AT(0x1872); uint8_t q = a; a = spcDiv(&q, 0, 12); }
    AT(0x187A);
    M(P_VIB_DELAY + x) = a;
    uint8_t rate = trkRead(d, x);
    AT(0x1880);
    T(T_VIB_RATE) = rate;
    uint8_t depth = trkRead(d, x);
    AT(0x1888);
    M(P_VIB_DEPTH + x) = depth;
    uint16_t st = modStep(d, depth, rate);
    AT(0x188E);
    ww(d, (uint16_t)(T_VIB_STEP + x), st);
    ww(d, (uint16_t)(T_VIB_OFS + x), 0);
    vibReset(d, x);
    break;
  }
  case 0xCA: AT(0x192E); M(P_VIB_DEPTH + x) = 0; ww(d, (uint16_t)(T_VIB_OFS + x), 0); break;
  case 0xCB: {   // tremolo: delay, rate, depth
    AT(0x1938);
    M(P_TREM_DELAY + x) = a;
    uint8_t rate = trkRead(d, x);
    AT(0x193E);
    T(T_TREM_RATE) = rate;
    uint8_t depth = trkRead(d, x);
    AT(0x1946);
    M(P_TREM_DEPTH + x) = depth;
    uint16_t st = modStep(d, depth, rate);
    AT(0x194C);
    ww(d, (uint16_t)(T_TREM_STEP + x), st);
    ww(d, (uint16_t)(T_TREM_ACC + x), 0);
    tremReset(d, x);
    break;
  }
  case 0xCC: AT(0x198E); M(P_TREM_DEPTH + x) = 0; ww(d, (uint16_t)(T_TREM_ACC + x), 0); break;
  case 0xCD: {   // pan sweep: period ($FF: none), depth (bit 7: the other way)
    AT(0x1998);
    uint8_t p = (uint8_t)(a + 1);
    int cy;
    if (!p) { AT(0x19A4); cy = 1; }
    else {
      AT(0x199C);
      p &= 0xFE;
      if (!p) { AT(0x19A0); p = 2; }
      cy = 0;
    }
    AT(0x19A5);
    T(T_SWEEP_RATE) = p;
    uint8_t h = (uint8_t)(cy << 7 | p >> 1);
    T(T_SWEEP_RATE_N) = h;
    uint8_t dep = trkRead(d, x);
    AT(0x19B0);
    int neg = dep >> 7;
    uint8_t v = (uint8_t)(dep & 0x7F);
    if (neg) { AT(0x19B7); v ^= 0x7F; }
    AT(0x19B9);
    uint16_t st;
    if (h & 0x80) { AT(0x19BE); st = v; }
    else {
      AT(0x19C3);
      uint8_t q = v, r = spcDiv(&q, 0, h), lo = 0;
      spcDiv(&lo, r, h);
      st = (uint16_t)(q << 8 | lo);
      if (!st) { AT(0x19D4); st = 1; }
    }
    AT(0x19D6);
    if (neg) { AT(0x19D8); st = neg16(st); }
    AT(0x19E0);
    ww(d, (uint16_t)(T_SWEEP_STEP + x), st);
    M(P_SWEEP + x) = M(D_BYTE);
    ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
    break;
  }
  case 0xCE:   // pan sweep off ($19EE: the tail of CD's handler, with A = 0)
    AT(0x19EE);
    M(P_SWEEP + x) = 0;
    ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
    break;
  case 0xCF:   // noise clock: absolute, or (bits above 4 set) relative
    AT(0x1A78);
    b = (uint8_t)(a & 0x1F);
    if (x < 0x10) {
      AT(0x1A7E);
      if (b != M(D_BYTE)) { AT(0x1A82); b = (uint8_t)((b + M(D_NCK_M)) & 0x1F); }
      AT(0x1A87); M(D_NCK_M) = b;
    } else {
      AT(0x1A8B);
      if (b != M(D_BYTE)) { AT(0x1A8F); b = (uint8_t)((b + M(D_NCK_S)) & 0x1F); }
      AT(0x1A94); M(D_NCK_S) = b;
    }
    nonShadow(d);
    break;
  case 0xD0:
    AT(0x1A3C);
    if (x < 0x10) { AT(0x1A40); M(D_NON_M) |= bit; } else { AT(0x1A45); M(D_NON_S) |= bit; }
    nonShadow(d);
    break;
  case 0xD1: noiseOff(d, x); break;
  case 0xD2:
    AT(0x1A98);
    if (x < 0x10) { AT(0x1A9C); M(D_PMON_M) |= bit; }
    else { AT(0x1AA1); if (!(bit & 0x50)) { AT(0x1AA7); M(D_PMON_S) |= bit; } }
    pmonShadow(d);
    break;
  case 0xD3: pmodOff(d, x); break;
  case 0xD4:
    AT(0x1A15);
    if (x < 0x10) { AT(0x1A19); M(D_EON_M) |= bit; } else { AT(0x1A1E); M(D_EON_S) |= bit; }
    eonShadow(d);
    break;
  case 0xD5: echoOff(d, x); break;
  case 0xD6: AT(0x1A0E); T(T_OCT) = (uint8_t)(a * 12); break;
  case 0xD7: AT(0x19FA); T(T_OCT) = (uint8_t)(T(T_OCT) + 12); break;
  case 0xD8: AT(0x1A04); T(T_OCT) = (uint8_t)(T(T_OCT) - 12); break;
  case 0xD9: AT(0x1801); T(T_TRANSPOSE) = a; break;
  case 0xDA: AT(0x17FD); T(T_TRANSPOSE) = (uint8_t)(T(T_TRANSPOSE) + a); break;
  case 0xDB: AT(0x1CF2); T(T_FINE) = a; break;
  case 0xDC: setInstr(d, x, a); break;
  case 0xDD:   // ADSR: attack
    AT(0x1AFC);
    M(D_BYTE) = (uint8_t)(a & 0x0F);
    T(T_ADSR) = (uint8_t)((T(T_ADSR) & 0x70) | M(D_BYTE) | 0x80);
    adsrOut(d, x);
    break;
  case 0xDE:   // decay
    AT(0x1B2A);
    M(D_BYTE) = xcn((uint8_t)(a & 7));
    T(T_ADSR) = (uint8_t)((T(T_ADSR) & 0x0F) | M(D_BYTE) | 0x80);
    adsrOut(d, x);
    break;
  case 0xDF:   // sustain level
    AT(0x1B3D);
    M(D_BYTE) = (uint8_t)(xcn((uint8_t)(a & 7)) << 1);
    T(T_ADSR + 1) = (uint8_t)((T(T_ADSR + 1) & 0x1F) | M(D_BYTE));
    adsrOut(d, x);
    break;
  case 0xE0:   // sustain rate
    AT(0x1B4F);
    M(D_BYTE) = (uint8_t)(a & 0x1F);
    T(T_ADSR + 1) = (uint8_t)((T(T_ADSR + 1) & 0xE0) | M(D_BYTE));
    adsrOut(d, x);
    break;
  case 0xE1: {   // the instrument's own ADSR again
    AT(0x1B5F);
    uint8_t y = (uint8_t)(T(T_INSTR) << 1);
    T(T_ADSR) = M(A_ADSR_TAB + y); T(T_ADSR + 1) = M(A_ADSR_TAB + y + 1);
    adsrOut(d, x);
    break;
  }
  case 0xE2: {   // loop start: count (0 = for ever)
    AT(0x1C64);
    uint8_t lim = (uint8_t)(x * 2 + 4);
    uint8_t sp = (uint8_t)(M(D_LOOP_SP + x) + 1);
    if (sp == lim) { AT(0x1C6E); sp = (uint8_t)(lim - 4); }
    AT(0x1C73);
    M(D_LOOP_SP + x) = sp;
    uint8_t n = M(D_BYTE);
    if (n) { AT(0x1C79); n++; }
    AT(0x1C7A);
    M(T_LOOP_LEFT + sp) = n;
    if (x < 0x10) { AT(0x1C81); M(T_LOOP_CNT + sp) = 1; }
    AT(0x1C86);
    uint8_t y = (uint8_t)(sp << 1);
    M(T_LOOP_PTR + y) = M(D_TRK_PTR + x); M(T_LOOP_PTR + y + 1) = M(D_TRK_PTR + x + 1);
    break;
  }
  case 0xE3: {   // loop end
    AT(0x1C94);
    uint8_t sp = M(D_LOOP_SP + x);
    if (x < 0x10) { AT(0x1C9A); M(T_LOOP_CNT + sp)++; }
    AT(0x1CA1);
    uint8_t n = M(T_LOOP_LEFT + sp);
    if (n) {
      AT(0x1CA6);
      n--;
      if (!n) {
        AT(0x1CA9);
        uint8_t base = (uint8_t)(x * 2 - 1);
        uint8_t s2 = (uint8_t)(M(D_LOOP_SP + x) - 1);
        M(D_LOOP_SP + x) = s2;
        if (s2 == base) { AT(0x1CB1); M(D_LOOP_SP + x) = (uint8_t)(base + 4); }
        AT(0x1CC8);
        break;
      }
      AT(0x1CB8);
      M(T_LOOP_LEFT + sp) = n;
    }
    AT(0x1CBB);
    uint8_t y = (uint8_t)(sp << 1);
    M(D_TRK_PTR + x) = M(T_LOOP_PTR + y); M(D_TRK_PTR + x + 1) = M(T_LOOP_PTR + y + 1);
    break;
  }
  case 0xE4:   // slur on
    AT(0x1B72);
    if (x < 0x10) { AT(0x1B78); M(D_SLUR_M) |= bit; M(D_SLURON_M) &= (uint8_t)~bit; }
    else { AT(0x1B7F); M(D_SLUR_S) |= bit; M(D_SLURON_S) &= (uint8_t)~bit; }
    break;
  case 0xE5: case 0xE7: AT(0x12DD); break;
  case 0xE6:
    AT(0x1B9A);
    if (x < 0x10) { AT(0x1BA0); M(D_ROLL_M) |= bit; } else { AT(0x1BA4); M(D_ROLL_S) |= bit; }
    break;
  case 0xE8: AT(0x1CEF); M(D_CNT + x) = a; break;
  case 0xE9: case 0xEA: {   // go to an effect's track (the high byte 0: the track ends there)
    AT(ev == 0xE9 ? 0x1BA8 : 0x1BAC);
    uint16_t p = sfxPtr(d, a, ev == 0xE9 ? 0 : 2);
    AT(0x1BB1);
    ww(d, (uint16_t)(D_TRK_PTR + x), p);
    if (!(p >> 8)) { AT(0x1BB8); AT(0x1D4A); endTrack(d, x); return 1; }
    AT(0x1BBB);
    break;
  }
  case 0xF0: setTempo(d, a); break;
  case 0xF1:   // tempo slide: ticks, target
    AT(0x1732);
    M(D_TEMPO_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    AT(0x1739);
    if (!M(D_CMD + 1)) { setTempo(d, b); break; }
    AT(0x173D);
    c8 = (uint8_t)(((b * 20) >> 8) + M(D_BYTE));
    c = c8 >= M(D_TEMPO + 1); c8 = (uint8_t)(c8 - M(D_TEMPO + 1));
    if (!c8) { AT(0x172F); M(D_TEMPO_N) = 0; break; }
    AT(0x1749);
    ww(d, D_TEMPO_STEP, slideStep(d, c8, c));
    AT(0x174C);
    break;
  case 0xF2: setEvol(d, a); break;
  case 0xF3:   // echo volume slide
    AT(0x179A);
    M(D_EVOL_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    AT(0x17A1);
    if (!M(D_CMD + 1)) { setEvol(d, b); break; }
    AT(0x17A5);
    c8 = sdiff(b, M(D_EVOL + 1), &c);
    if (!c8) { AT(0x1797); M(D_EVOL_N) = 0; break; }
    AT(0x17B2);
    ww(d, D_EVOL_STEP, slideStep(d, c8, c));
    AT(0x17B5);
    break;
  case 0xF4: AT(0x1751); M(D_MVOL) = a; M(D_VOLCHG) |= M(D_PLAYING); break;
  case 0xF5: {   // leave the loop on iteration a: to the target
    AT(0x1C09);
    uint8_t lo = trkRead(d, x);
    AT(0x1C0E);
    uint8_t hi = trkRead(d, x);
    AT(0x1C13);
    uint8_t sp = M(D_LOOP_SP + x);
    if (M(T_LOOP_CNT + sp) != a) { AT(0x1C38); break; }
    AT(0x1C1D);
    if ((uint8_t)(M(T_LOOP_LEFT + sp) - 1) == 0) {
      AT(0x1C23);
      uint8_t base = (uint8_t)(x * 2 - 1);
      uint8_t s2 = (uint8_t)(M(D_LOOP_SP + x) - 1);
      M(D_LOOP_SP + x) = s2;
      if (s2 == base) { AT(0x1C2B); M(D_LOOP_SP + x) = (uint8_t)(base + 4); }
    }
    AT(0x1C30);
    ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | lo) + rw(d, D_SONG_BASE)));
    break;
  }
  case 0xF6: {   // go to
    AT(0x1BEE);
    uint8_t hi = trkRead(d, x);
    AT(0x1BF2);
    ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | a) + rw(d, D_SONG_BASE)));
    break;
  }
  case 0xF7:   // echo feedback slide (no zero test)
    AT(0x1805);
    M(D_EFB_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    AT(0x180C);
    if (!M(D_CMD + 1)) { AT(0x1823); M(D_EFB + 1) = b; break; }
    AT(0x1810);
    c8 = sdiff(b, M(D_EFB + 1), &c);
    ww(d, D_EFB_STEP, slideStep(d, c8, c));
    AT(0x181E);
    break;
  case 0xF8: {   // the echo FIR: ticks, one of the four sets at $1D93
    AT(0x1826);
    M(D_FIR_N) = a; M(D_CMD + 1) = a;
    b = trkRead(d, x);
    AT(0x182D);
    uint8_t y = (uint8_t)(((b & 3) + 1) << 3);
    if (!M(D_CMD + 1)) {
      for (int k = 0x10; k; k -= 2, y--) { AT(0x1861); M(0x64 + k) = M(R_FIRTAB + y); AT(0x1869); }
    } else {
      for (int k = 0x10; k; k -= 2, y--) {
        AT(0x183A);
        M(0x63 + k) = 0;
        c8 = sdiff(M(R_FIRTAB + y), M(0x64 + k), &c);
        uint16_t st = slideStep(d, c8, c);
        AT(0x1851);
        M(0x00FE + k) = (uint8_t)st; M(0x00FF + k) = (uint8_t)(st >> 8);
      }
      AT(0x185F);
    }
    AT(0x186B);
    break;
  }
  case 0xF9: AT(0x1CF6); M(D_MARK) = (uint8_t)(a & 0x0F); break;
  case 0xFA: {   // jump if command 88's value reaches the operand
    AT(0x1CFB);
    uint8_t v = (uint8_t)(a & 0x0F);
    M(D_BYTE) = v;
    if (v >= M(D_COND_MAX)) { AT(0x1D03); M(D_COND_MAX) = v; }
    AT(0x1D05);
    if (M(D_COND) >= v) {
      AT(0x1D19);
      uint8_t lo = trkRead(d, x);
      AT(0x1D1C);
      uint8_t hi = trkRead(d, x);
      AT(0x1D20);
      ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)((hi << 8 | lo) + rw(d, D_SONG_BASE)));
    } else {
      AT(0x1D0B);
      uint16_t p = rw(d, (uint16_t)(D_TRK_PTR + x));
      if (!(uint8_t)(p + 1)) AT(0x1D0F);
      AT(0x1D11);
      if (!(uint8_t)(p + 2)) AT(0x1D15);
      AT(0x1D29);
      ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)(p + 2));
    }
    break;
  }
  case 0xFB: AT(0x1AC5); M(D_PERC) |= bit; break;
  case 0xFC: AT(0x1AC9); M(D_PERC) &= (uint8_t)~bit; break;
  case 0xFD: AT(0x1757); T(T_EXPR) = (uint8_t)(a << 1); M(D_VOLCHG) |= bit; break;
  default:   // EB-EF, FE, FF: the track's end
    AT(0x1D4A);
    endTrack(d, x);
    return 1;
  }
  return 0;
}

// $075E: the note look-ahead -- the next byte that is a note, a tie or a rest, read past the events (the ones that
// move the pointer followed; the slur's and the instrument's clear the slur), or $EB at the track's end.
static uint8_t lookAhead(SndDrv* d, int x) {
  AT(0x075E);
  uint16_t p = rw(d, (uint16_t)(D_TRK_PTR + x));
  uint8_t sp = M(D_LOOP_SP + x);
  for (;;) {
    AT(0x076A);
    uint8_t a = M(p);
    if (a < 0xC4) { AT(0x07A2); return a; }
    AT(0x0770);
    p++;
    if (a == 0xEB) { AT(0x07A2); return 0xEB; }
    AT(0x0776);
    switch (a) {
    case 0xDC: case 0xE4: case 0xE5: case 0xE6: case 0xE7:   // $1B86: the operand is not skipped
      AT(0x0782); AT(0x1B86);
      { uint8_t b = M(D_BIT);
        if (x < 0x10) { AT(0x1B8C); M(D_SLUR_M) &= (uint8_t)~b; M(D_ROLL_M) &= (uint8_t)~b; }
        else { AT(0x1B93); M(D_SLUR_S) &= (uint8_t)~b; M(D_ROLL_S) &= (uint8_t)~b; } }
      AT(0x0768);
      break;
    case 0xE3: {   // $1CC9
      AT(0x0782); AT(0x1CC9);
      uint8_t n = M(T_LOOP_LEFT + sp);
      if (n) AT(0x1CD0);
      if (!n || (uint8_t)(n - 1)) { AT(0x1CE1); p = rw(d, (uint16_t)(T_LOOP_PTR + (uint8_t)(sp << 1))); }
      else {
        AT(0x1CD3);
        uint8_t base = (uint8_t)(x * 2 - 1);
        sp--;
        if (sp == base) { AT(0x1CDB); sp = (uint8_t)(base + 4); }
      }
      AT(0x1CEE);
      AT(0x0768);
      break;
    }
    case 0xE9: case 0xEA:   // $1BD9 / $1BDD
      AT(0x0782); AT(a == 0xE9 ? 0x1BD9 : 0x1BDD);
      p = sfxPtr(d, M(p), a == 0xE9 ? 0 : 2);
      AT(0x1BE4);
      if (!(p >> 8)) { AT(0x1BE9); AT(0x1BED); return 0xEB; }
      AT(0x1BED);
      AT(0x0768);
      break;
    case 0xF5: {   // $1C39
      AT(0x0782); AT(0x1C39);
      if (M(T_LOOP_CNT + sp) != M(p)) { AT(0x1C5D); p += 3; AT(0x0768); break; }
      AT(0x1C44);
      if ((uint8_t)(M(T_LOOP_LEFT + sp) - 1) == 0) {
        AT(0x1C4C);
        uint8_t base = (uint8_t)(x * 2 - 1);
        sp--;
        if (sp == base) { AT(0x1C54); sp = (uint8_t)(base + 4); }
      }
      AT(0x1C58);
      AT(0x1BFC);
      p = (uint16_t)(rw(d, (uint16_t)(p + 1)) + rw(d, D_SONG_BASE));
      AT(0x0768);
      break;
    }
    case 0xF6:   // $1BFC
      AT(0x0782); AT(0x1BFC);
      p = (uint16_t)(rw(d, p) + rw(d, D_SONG_BASE));
      AT(0x0768);
      break;
    case 0xFA:   // $1D2A
      AT(0x0782); AT(0x1D2A);
      M(D_BYTE) = M(p);
      if (M(D_COND) >= M(D_BYTE)) { AT(0x1D3C); p = (uint16_t)(rw(d, (uint16_t)(p + 1)) + rw(d, D_SONG_BASE)); }
      else { AT(0x1D34); p += 3; }
      AT(0x1D49);
      AT(0x0768);
      break;
    default: {   // no handler: its operands skipped ($15CC)
      AT(0x0794);
      uint8_t n = M(R_OPCOUNT + (uint8_t)(a - 0xC4));
      if (!n) { AT(0x0768); break; }
      AT(0x079B);
      for (uint8_t y = n; y; y--) { AT(0x079C); p++; AT(0x079E); }
      AT(0x07A0);
      break;
    }
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
    AT(0x0507);
    a = trkRead(d, x);
    AT(0x050A);
    if (a < 0xC4) break;
    AT(0x050E);
    if (event(d, x, a)) return;
    AT(0x0511);
  }
  AT(0x0513);
  if (!M(D_CNT + x)) { AT(0x0517); uint8_t q = a; M(D_CNT + x) = M(R_LENGTH + spcDiv(&q, 0, 14)); }
  AT(0x0521);
  a = M(D_BYTE);
  uint8_t bit = M(D_BIT);
  if (a >= 0xA8) {
    AT(0x0527);
    if (a >= 0xB6) { AT(0x052B); return; }   // a rest
    AT(0x052C);
    goto ahead;                              // a tie
  }
  {
    AT(0x052F);
    uint8_t semi = a;
    spcDiv(&semi, 0, 14);
    M(D_BYTE) = semi;
    uint8_t n;
    int perc = 0;
    if (x < 0x10) { AT(0x053C); perc = (bit & M(D_PERC)) != 0; }
    if (perc) { AT(0x0542); n = percNote(d, x); AT(0x0545); }
    else { AT(0x0547); n = (uint8_t)(T(T_OCT) + M(D_BYTE)); }
    AT(0x054D);
    n = (uint8_t)(n + T(T_TRANSPOSE) - 10);
    T(T_NOTE) = n;
    uint16_t pitch = pitchOf(d, x, n);
    AT(0x055A);
    ww(d, (uint16_t)(T_PITCH + x), pitch);
    uint8_t vd = M(P_VIB_DEPTH + x);
    if (vd) {
      AT(0x0569);
      M(D_BYTE) = vd;
      vibReset(d, x);
      AT(0x056E);
      uint8_t y;
      if (x >= 0x10) { AT(0x0576); y = M(P_VIB_DELAY + x); } else { AT(0x0572); y = 0; }
      AT(0x057A);
      uint8_t m = M(R_VIBSCALE + y);
      uint16_t v;
      if (m) { AT(0x0584); v = (uint16_t)(m * (pitch >> 8) + ((m * (pitch & 0xFF)) >> 8)); }
      else { AT(0x0580); v = pitch; }
      AT(0x0595);
      ww(d, (uint16_t)(T_VIB_SCALE + x), v);
    }
    AT(0x059C);
    uint8_t td = M(P_TREM_DEPTH + x);
    if (td) { AT(0x05A1); M(D_BYTE) = td; tremReset(d, x); AT(0x05A6); }
    AT(0x05A8);
    ww(d, (uint16_t)(T_VIB_OFS + x), 0);
    ww(d, (uint16_t)(T_TREM_ACC + x), 0);
    ww(d, (uint16_t)(T_PSLIDE_D + x), 0);
    T(T_PSLIDE_LO) = 0;
    if (x < 0x10) {
      AT(0x05C1);
      M(D_KEYED_M) |= bit;
      if (M(D_SFX_VOICES) & bit) { AT(0x0601); goto ahead; }
      AT(0x05CA);
      if (!(bit & M(D_ROLL_M))) {
        AT(0x05D0);
        if (bit & M(D_SLUR_M)) {
          AT(0x05D6);
          if (bit & M(D_SLURON_M)) { AT(0x05F8); goto volpitch; }
          AT(0x05DA);
          M(D_SLURON_M) |= bit;
        }
      }
    } else {
      AT(0x05DF);
      M(D_KEYED_S) |= bit;
      if (!(bit & M(D_ROLL_S))) {
        AT(0x05E8);
        if (bit & M(D_SLUR_S)) {
          AT(0x05EE);
          if (bit & M(D_SLURON_S)) { AT(0x05F8); goto volpitch; }
          AT(0x05F2);
          M(D_SLURON_S) |= bit;
        }
      }
    }
    AT(0x05F5);
    M(D_KON) |= bit;
  volpitch:
    AT(0x05F8);
    M(D_VOLCHG) |= bit;
    M(D_PITCHCHG) |= bit;
    instrOut(d, x);
  }
ahead: {
    AT(0x0601);
    uint8_t y = lookAhead(d, x);
    AT(0x0604);
    bit = M(D_BIT);
    int s = x >= 0x10;
    uint8_t tie = (uint8_t)(D_TIE_M + s), slur = (uint8_t)(D_SLUR_M + s), roll = (uint8_t)(D_ROLL_M + s);
    uint16_t b0 = s ? 0x062C : 0x060B;   // the effects' copy of the same code at +$21 ($062C-$064A)
    AT(b0);
    if (y >= 0xB6) { AT(b0 + 0x0D); M(slur) &= (uint8_t)~bit; M(roll) &= (uint8_t)~bit; }
    else {
      AT(b0 + 0x04);
      if (y >= 0xA8) { AT(b0 + 0x08); M(tie) |= bit; goto slide; }
    }
    AT(b0 + 0x13);
    M(tie) &= (uint8_t)~bit;
    M(tie) |= (uint8_t)((M(slur) | M(roll)) & bit);
  }
slide: {
    // $064B: event C8 armed a pitch slide: from this note to the one `semitones` away, over its ticks
    AT(0x064B);
    uint8_t ps = M(P_PSLIDE + x);
    if (!ps) { AT(0x06BC); return; }
    AT(0x0650);
    uint8_t n = (uint8_t)(ps + T(T_NOTE));
    T(T_NOTE) = n;
    uint16_t tgt = pitchOf(d, x, n);
    AT(0x065A);
    uint16_t cur = rw(d, (uint16_t)(T_PITCH + x));
    int up = tgt >= cur;
    uint16_t delta = (uint16_t)(tgt - cur);
    if (!up) { AT(0x066D); delta = neg16(delta); }
    AT(0x0675);
    uint8_t len = T(T_PSLIDE_N);
    uint8_t r0 = (uint8_t)delta, r1 = (uint8_t)(delta >> 8), r2 = 0;
    if (len) {
      AT(0x067F);
      uint8_t q = r1, r = spcDiv(&q, 0, len);
      r2 = q;
      q = r0; r = spcDiv(&q, r, len); r1 = q;
      q = 0; spcDiv(&q, r, len); r0 = q;
    } else AT(0x067A);
    AT(0x0691);
    if (!up) {
      AT(0x0694);
      r0 ^= 0xFF; r1 ^= 0xFF; r2 ^= 0xFF;
      uint16_t w = (uint16_t)((r1 << 8 | r0) + 1);
      r0 = (uint8_t)w; r1 = (uint8_t)(w >> 8);
      if (!w) { AT(0x06A1); r2++; }
    }
    AT(0x06A3);
    T(T_PSLIDE_LO) = r0; T(T_PSLIDE_D) = r1; T(T_PSLIDE_D + 1) = r2;
    M(P_PSLIDE + x) = 0;
    T(T_PITCH_LO) = 0;
    AT(0x06BC);
  }
}

// $07AF: the track's slides, each tick it runs -- the modulation delays, volume, pan, the pitch slide, the pan sweep.
static void slides(SndDrv* d, int x) {
  AT(0x07AF);
  uint8_t bit = M(D_BIT);
  if (M(P_VIB_DELAY_N + x)) { AT(0x07B4); M(P_VIB_DELAY_N + x)--; }
  AT(0x07B6);
  if (M(P_TREM_DELAY_N + x)) { AT(0x07BA); M(P_TREM_DELAY_N + x)--; }
  AT(0x07BC);
  if (T(T_VOL_N)) {
    AT(0x07C2);
    T(T_VOL_N)--;
    uint16_t o = rw(d, (uint16_t)(T_VOL + x)), v = (uint16_t)(o + rw(d, (uint16_t)(T_VOL_STEP + x)));
    ww(d, (uint16_t)(T_VOL + x), v);
    if ((v >> 8) != (o >> 8)) { AT(0x07DE); M(D_VOLCHG) |= bit; }
  }
  AT(0x07E1);
  if (T(T_PAN_N)) {
    AT(0x07E6);
    T(T_PAN_N)--;
    uint16_t o = rw(d, (uint16_t)(T_PAN + x)), v = (uint16_t)(o + rw(d, (uint16_t)(T_PAN_STEP + x)));
    ww(d, (uint16_t)(T_PAN + x), v);
    if ((v >> 8) != (o >> 8)) { AT(0x0802); M(D_VOLCHG) |= bit; }
  }
  AT(0x0805);
  uint8_t s0 = T(T_PSLIDE_LO), s1 = T(T_PSLIDE_D), s2 = T(T_PSLIDE_D + 1);
  int on = 1;
  if (!s0 && !s1) { AT(0x0818); on = s2 != 0; }
  if (on) {
    AT(0x081C);
    uint8_t n = (uint8_t)(T(T_PSLIDE_N) - 1);
    if (!n) { AT(0x0822); T(T_PSLIDE_D) = 0; T(T_PSLIDE_D + 1) = 0; T(T_PSLIDE_LO) = 0; }
    AT(0x082B);
    T(T_PSLIDE_N) = n;
    uint32_t acc = (uint32_t)(T(T_PITCH + 1) << 16 | T(T_PITCH) << 8 | T(T_PITCH_LO));
    acc += (uint32_t)(s2 << 16 | s1 << 8 | s0);
    T(T_PITCH_LO) = (uint8_t)acc; T(T_PITCH) = (uint8_t)(acc >> 8); T(T_PITCH + 1) = (uint8_t)(acc >> 16);
    M(D_PITCHCHG) |= bit;
  }
  AT(0x084A);
  if (M(P_SWEEP + x)) {
    AT(0x084F);
    uint16_t st = rw(d, (uint16_t)(T_SWEEP_STEP + x)), o = rw(d, (uint16_t)(T_SWEEP_ACC + x)), v = (uint16_t)(o + st);
    ww(d, (uint16_t)(T_SWEEP_ACC + x), v);
    if ((v >> 8) != (o >> 8)) { AT(0x0870); M(D_VOLCHG) |= bit; }
    AT(0x0873);
    uint8_t n = (uint8_t)(T(T_SWEEP_RATE_N) - 1);
    if (!n) { AT(0x0879); ww(d, (uint16_t)(T_SWEEP_STEP + x), neg16(st)); n = T(T_SWEEP_RATE); }
    AT(0x088E);
    T(T_SWEEP_RATE_N) = n;
  }
  AT(0x0891);
}

// $04C7: the global slides -- tempo, echo volume, echo feedback, the FIR's eight coefficients.
static void globalSlides(SndDrv* d) {
  AT(0x04C7);
  if (M(D_TEMPO_N)) { AT(0x04CB); M(D_TEMPO_N)--; ww(d, D_TEMPO, (uint16_t)(rw(d, D_TEMPO) + rw(d, D_TEMPO_STEP))); }
  AT(0x04D3);
  if (M(D_EVOL_N)) { AT(0x04D7); M(D_EVOL_N)--; ww(d, D_EVOL, (uint16_t)(rw(d, D_EVOL) + rw(d, D_EVOL_STEP))); }
  AT(0x04DF);
  if (M(D_EFB_N)) { AT(0x04E3); M(D_EFB_N)--; ww(d, D_EFB, (uint16_t)(rw(d, D_EFB) + rw(d, D_EFB_STEP))); }
  AT(0x04EB);
  if (M(D_FIR_N)) {
    AT(0x04EF);
    M(D_FIR_N)--;
    for (int k = 0x10; k; k -= 2) {
      AT(0x04F3);
      ww(d, (uint16_t)(0x63 + k), (uint16_t)(rw(d, (uint16_t)(0x63 + k)) + rw(d, (uint16_t)(0x00FE + k))));
      AT(0x0504);
    }
  }
  AT(0x0506);
}

// The music's tick add ($037A, and $0464's copy): the tempo, scaled by the tempo modifier ($AA: below $80 up to x 3,
// from $80 down). `b` = the copy's first address.
static uint8_t tempoAdd(SndDrv* d, uint16_t b) {
  uint8_t t = M(D_TEMPO + 1), m = M(D_TMOD + 1);
  if (!m) { AT(b + 0x18); return t; }
  AT(b + 6);
  uint8_t hi = (uint8_t)((t * m) >> 8);
  if (!(m & 0x80)) {
    AT(b + 0x0B);
    unsigned s = (unsigned)(uint8_t)(hi << 1) + t;
    if (s > 0xFF) { AT(b + 0x11); AT(b + 0x18); return 0xFF; }
    AT(b + 0x18);
    return (uint8_t)s;
  }
  AT(b + 0x15);
  if (hi) { AT(b + 0x18); return hi; }
  AT(b + 0x17); AT(b + 0x18);
  return 1;
}

// One pass of the sequencer over the tracks of $A3 (from bit 0, track x on): step each track whose countdown ran out,
// release the voice two ticks before its next step unless the look-ahead saw a tie. The main tick's pass ($03A3-$03EB:
// the effects' gate and the poll) and the upload tick's ($048D-$04C1). Returns the X it ends at.
static int seqPassMain(SndDrv* d, int x) {
  for (;;) {
    AT(0x03E7);
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) {
      AT(0x03A3);
      M(D_TRK) = (uint8_t)x;
      if (!--M(D_CNT + x)) {
        AT(0x03A9);
        M(D_FLAGS2) &= (uint8_t)~0x08;
        step(d, x);
        AT(0x03AE);
        if (M(D_FLAGS2) & 0x08) { AT(0x03B1); AT(0x03E3); goto adv; }   // the track ended
      } else {
        AT(0x03B3);
        int s = x >= 0x10;
        AT(s ? 0x03BA : 0x03B9);
        AT(0x03BB);
        if (M(D_CNT + x) == 2) {
          AT(0x03C0);
          if (!(M(D_TIE_M + s) & M(D_BIT))) {
            AT(0x03C7);
            M(D_KEYED_M + s) &= (uint8_t)~M(D_BIT);
            int off = 1;
            if (!s) { AT(0x03D4); off = !(M(D_SFX_VOICES) & M(D_BIT)); }
            if (off) { AT(0x03DA); M(D_KOFF) |= M(D_BIT); }
          }
        }
      }
      AT(0x03DD);
      slides(d, x);
      AT(0x03E0);
      pollPort(d, 0x03E3);
    } else if (!M(D_PASS)) { AT(0x03ED); return x; }
    AT(0x03E3);
  adv:
    x += 2;
    M(D_BIT) <<= 1;
  }
}
static void seqPassUpload(SndDrv* d, int x) {
  for (;;) {
    AT(0x04BD);
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) {
      AT(0x048D);
      M(D_TRK) = (uint8_t)x;
      if (!--M(D_CNT + x)) {
        AT(0x0493);
        M(D_FLAGS2) &= (uint8_t)~0x08;
        step(d, x);
        AT(0x0498);
        if (M(D_FLAGS2) & 0x08) { AT(0x049B); AT(0x04B9); goto adv; }
      } else {
        AT(0x049D);
        if (M(D_CNT + x) == 2) {
          AT(0x04A2);
          if (!(M(D_TIE_M) & M(D_BIT))) {
            AT(0x04A8);
            M(D_KEYED_M) &= (uint8_t)~M(D_BIT);
            if (!(M(D_SFX_VOICES) & M(D_BIT))) { AT(0x04B3); M(D_KOFF) |= M(D_BIT); }
          }
        }
      }
      AT(0x04B6);
      slides(d, x);
    } else if (!M(D_PASS)) { AT(0x04C3); return; }
    AT(0x04B9);
  adv:
    x += 2;
    M(D_BIT) <<= 1;
  }
}

// $0377: the sequencer -- the music on its tempo accumulator, then the effects on theirs ($81 a tick).
static void sequencer(SndDrv* d) {
  AT(0x0377);
  if (!(M(D_FLAGS2) & 0x20)) {
    AT(0x037A);
    unsigned acc = M(D_TEMPO_ACC) + tempoAdd(d, 0x037A);
    M(D_TEMPO_ACC) = (uint8_t)acc;
    if (acc <= 0xFF) { AT(0x03FC); goto sfx; }
  }
  AT(0x0399);
  M(D_BIT) = 0x01;
  M(D_PASS) = M(D_PLAYING);
  if (seqPassMain(d, 0) >= 0x18) { AT(0x0414); return; }
  AT(0x03F1);
  globalSlides(d);
  AT(0x03F4);
  M(D_VOLCHG) &= (uint8_t)~M(D_SFX_VOICES);
  M(D_PITCHCHG) &= (uint8_t)~M(D_SFX_VOICES);
sfx:
  AT(0x03FC);
  if (!(M(D_FLAGS2) & 0x20)) {
    AT(0x03FF);
    unsigned acc = M(D_SFX_ACC) + 0x81;
    M(D_SFX_ACC) = (uint8_t)acc;
    if (acc <= 0xFF) { AT(0x0414); return; }
  }
  AT(0x0408);
  M(D_BIT) = 0x10;
  M(D_PASS) = xcn(M(D_SFX_VOICES));
  seqPassMain(d, 0x18);
  AT(0x0414);
}

// ===================================================================================================================
// The voices
// ===================================================================================================================

// $0ADE: a modulation's waveform step at the end of its period: the new current delta from the old one (cur), the
// step and the phase (3 bits); the depth's top bits (in $A5) pick the shape. *nph = the new phase.
static uint16_t waveStep(SndDrv* d, uint8_t ph, uint16_t step, uint16_t cur, uint8_t* nph) {
  AT(0x0ADE);
  uint8_t was = (uint8_t)(cur >> 8);
  uint8_t mode = M(D_BYTE);
  *nph = 0;
  if (mode < 0xC0) {
    AT(0x0AE6);
    if (mode < 0x80) {
      AT(0x0AEB);
      cur = neg16(cur);
      if (!ph) { AT(0x0B59); AT(0x0B5B); return cur; }
      AT(0x0AF6);
      *nph = (uint8_t)(ph - 1);
      if (cur & 0x8000) { AT(0x0B59); AT(0x0B5B); return cur; }
      AT(0x0AFC); AT(0x0B08);
      return (uint16_t)((step >> 2) + cur);
    }
    AT(0x0B09);
    cur = neg16(cur);
    if (!ph) { AT(0x0B59); AT(0x0B5B); return cur; }
    AT(0x0B14);
    *nph = (uint8_t)(ph - 1);
    if (!(cur & 0x8000)) { AT(0x0B59); AT(0x0B5B); return cur; }
    AT(0x0B1A); AT(0x0B2E);
    return (uint16_t)(neg16((uint16_t)(step >> 2)) + cur);
  }
  AT(0x0B2F);
  if (!ph) { AT(0x0B51); AT(0x0B59); AT(0x0B5B); return neg16(cur); }
  AT(0x0B32);
  *nph = (uint8_t)(ph - 1);
  if (cur & 0x8000) { AT(0x0B38); cur = neg16(cur); }
  AT(0x0B40);
  cur = (uint16_t)((step >> 2) + cur);
  if (was & 0x80) { AT(0x0B59); AT(0x0B5B); return cur; }
  AT(0x0B51); AT(0x0B59); AT(0x0B5B);
  return neg16(cur);
}

// $12DE: pan + the pan sweep's signed offset, held to 0-255.
static uint8_t panAdd(SndDrv* d, uint8_t pan, uint8_t sw) {
  AT(0x12DE);
  unsigned s = (unsigned)pan + sw;
  if (sw & 0x80) {
    AT(0x12EB);
    if (s > 0xFF) { AT(0x12F1); return (uint8_t)s; }
    AT(0x12EF); AT(0x12F1);
    return 0;
  }
  AT(0x12E4);
  if (s > 0xFF) { AT(0x12E8); AT(0x12EA); return 0xFF; }
  AT(0x12F1);
  return (uint8_t)s;
}

// $0A78: the voice's pitch registers -- the pitch plus the vibrato's offset, the music scaled by the pitch modifier
// ($AF: below $80 up, from $80 down), the effects' voices 6-7 by command 89's value.
static void pitchOut(SndDrv* d, int x, uint8_t reg) {
  AT(0x0A78);
  uint16_t p = (uint16_t)(rw(d, (uint16_t)(T_PITCH + x)) + rw(d, (uint16_t)(T_VIB_OFS + x)));
  uint8_t lo = (uint8_t)p, hi = (uint8_t)(p >> 8);
  uint16_t out = p;
  if (x < 0x10) {
    AT(0x0AB2);
    uint8_t m = M(D_PMOD + 1);
    uint16_t t = (uint16_t)(m * hi + ((m * lo) >> 8));
    if (m & 0x80) out = t;
    else { AT(0x0AC4); out = (uint16_t)((t << 1) + p); }
  } else {
    AT(0x0A8D);
    if (x >= 0x1C) {
      AT(0x0A95);
      if (M(D_SFXPITCH)) {
        AT(0x0A99);
        uint8_t m = M(D_SFXPITCH);
        uint16_t t = (uint16_t)(m * hi + ((m * lo) >> 8));
        out = (uint16_t)((t << 1) + p);
      } else AT(0x0A91);
    } else AT(0x0A91);
  }
  AT(0x0ACE);
  dspW(d, 0x0AD4, reg, (uint8_t)out);
  dspW(d, 0x0AD9, (uint8_t)(reg + 1), (uint8_t)(out >> 8));
  AT(0x0ADD);
}

// $09F6: the voice's volume (when D_VOLCHG names it) and pitch (D_PITCHCHG) to the S-DSP.
static void voiceOut(SndDrv* d, int x) {
  AT(0x09F6);
  if (!M(D_VOLCHG) && !M(D_PITCHCHG)) { AT(0x09FA); return; }
  AT(0x09FB);
  uint8_t vv = (uint8_t)(x & 0x0F);
  uint8_t reg = voiceReg(x);
  uint8_t bit = M(D_BIT);
  if (bit & M(D_VOLCHG)) {
    AT(0x0A0A);
    uint8_t pan = 0x80;
    if (!(M(D_FLAGS) & 1)) { AT(0x0A10); pan = (uint8_t)~panAdd(d, T(T_PAN + 1), T(T_SWEEP_ACC + 1)); AT(0x0A1A); }
    AT(0x0A1E);
    uint8_t v = T(T_VOL + 1), y = v;
    uint8_t tr = T(T_TREM_ACC + 1);
    if (tr) {
      AT(0x0A29);
      uint16_t pr = (uint16_t)(v * (uint8_t)(tr << 1));
      if (tr & 0x80) y = (uint8_t)(pr >> 8);
      else {
        AT(0x0A2D);
        unsigned s = (pr >> 8) + v;
        y = (uint8_t)s;
        if (y & 0x80) {
          AT(0x0A32);
          if (s > 0xFF) { AT(0x0A34); y = 0x7F; }
        }
        AT(0x0A36);
      }
    }
    AT(0x0A37);
    y = (uint8_t)((y * M(D_PASSVOL)) >> 8);
    if (x < 0x10) {
      AT(0x0A3E);
      if (bit & M(D_MUTE)) { AT(0x0A44); y = 0; }
      else { AT(0x0A48); y = (uint8_t)((y * T(T_EXPR)) >> 8); y = (uint8_t)((y * M(D_MVOL)) >> 8); }
    }
    AT(0x0A4F);
    uint8_t l = (uint8_t)((y * pan) >> 8);
    M(D_VOLOUT + vv) = l;
    dspW(d, 0x0A5E, reg, l);
    AT(0x0A68);
    uint8_t r = (uint8_t)((y * (uint8_t)~pan) >> 8);
    M(D_VOLOUT + (vv ^ 1)) = r;
    dspW(d, 0x0A5E, (uint8_t)(reg + 1), r);
  }
  AT(0x0A70);
  if (bit & M(D_PITCHCHG)) { AT(0x0A76); pitchOut(d, x, (uint8_t)(reg | 2)); return; }
  AT(0x0ADD);
}

// $0903: a voice's tick -- the vibrato (unless delayed), the tremolo (unless delayed), then the output.
static void voice(SndDrv* d, int x) {
  AT(0x0903);
  uint8_t bit = M(D_BIT);
  uint8_t vd = M(P_VIB_DEPTH + x);
  int vib = 0;
  if (vd) {
    AT(0x0908);
    M(D_BYTE) = vd;   // (taken before the delay's test)
    if (x >= 0x10) vib = 1;
    else { AT(0x090E); vib = !M(P_VIB_DELAY_N + x); }
  }
  if (!vib) AT(0x0913);
  else {
    AT(0x0916);
    uint16_t cur = rw(d, (uint16_t)(T_VIB_CUR + x)), old = rw(d, (uint16_t)(T_VIB_ACC + x));
    uint16_t acc = (uint16_t)(old + cur);
    T(T_VIB_ACC) = (uint8_t)acc;
    if ((acc >> 8) != (old >> 8)) {
      AT(0x0933);
      T(T_VIB_ACC + 1) = (uint8_t)(acc >> 8);
      uint8_t h = (uint8_t)((uint16_t)(acc << 1) >> 8);
      uint16_t sc = rw(d, (uint16_t)(T_VIB_SCALE + x));
      uint16_t ofs = (uint16_t)((sc >> 8) * h + (((sc & 0xFF) * h) >> 8));
      if (h & 0x80) { AT(0x0955); ofs = (uint16_t)(ofs - sc); }
      AT(0x0957);
      ww(d, (uint16_t)(T_VIB_OFS + x), ofs);
      M(D_PITCHCHG) |= bit;
    }
    AT(0x0961);
    uint8_t n = (uint8_t)(T(T_VIB_RATE_N) - 1);
    if (!n) {
      AT(0x0967);
      uint8_t ph = T(T_PHASE), nph;
      uint16_t nv = waveStep(d, (uint8_t)(ph & 7), rw(d, (uint16_t)(T_VIB_STEP + x)), cur, &nph);
      AT(0x0981);
      ww(d, (uint16_t)(T_VIB_CUR + x), nv);
      T(T_PHASE) = (uint8_t)(nph | (ph & 0x70));
      n = T(T_VIB_RATE);
    }
    AT(0x0992);
    T(T_VIB_RATE_N) = n;
  }
  AT(0x0995);
  uint8_t td = M(P_TREM_DEPTH + x);
  if (td) {
    AT(0x099A);
    M(D_BYTE) = td;
    if (!M(P_TREM_DELAY_N + x)) {
      AT(0x09A1);
      uint16_t cur = rw(d, (uint16_t)(T_TREM_CUR + x)), old = rw(d, (uint16_t)(T_TREM_ACC + x));
      uint16_t acc = (uint16_t)(old + cur);
      T(T_TREM_ACC) = (uint8_t)acc;
      if ((acc >> 8) != (old >> 8)) { AT(0x09BE); T(T_TREM_ACC + 1) = (uint8_t)(acc >> 8); M(D_VOLCHG) |= bit; }
      AT(0x09C4);
      uint8_t n = (uint8_t)(T(T_TREM_RATE_N) - 1);
      if (!n) {
        AT(0x09CA);
        uint8_t ph = T(T_PHASE), nph;
        uint16_t nv = waveStep(d, (uint8_t)(xcn(ph) & 7), rw(d, (uint16_t)(T_TREM_STEP + x)), cur, &nph);
        AT(0x09E1);
        ww(d, (uint16_t)(T_TREM_CUR + x), nv);
        T(T_PHASE) = (uint8_t)(xcn(nph) | (ph & 0x07));
        n = T(T_TREM_RATE);
      }
      AT(0x09F3);
      T(T_TREM_RATE_N) = n;
    }
  }
  voiceOut(d, x);
}

// ===================================================================================================================
// The fades ($1492, every 7th tick)
// ===================================================================================================================

typedef struct { uint16_t entry, nz, chg, j, zx, stop, next; } FadePcs;
static const FadePcs kFade[3] = {
  { 0x1492, 0x1497, 0x14B4, 0x14B7, 0x14BA, 0x14BD, 0x14C8 },
  { 0x14C8, 0x14CD, 0x14EA, 0x14ED, 0x14F0, 0x14F3, 0x14F6 },
  { 0x14F6, 0x14FB, 0x1518, 0x151B, 0x151E, 0x1521, 0x1524 },
};
static int fadeStep(SndDrv* d, int i, uint8_t chg) {   // 1: the fade ended at volume 0 (marked at its stop call)
  const FadePcs* f = &kFade[i];
  int k = 2 * i;
  AT(f->entry);
  uint8_t n = M(A_FADE_N + k);
  if (!n) { AT(f->next); return 0; }
  AT(f->nz);
  n--;
  M(A_FADE_N + k) = n;
  uint16_t o = rw(d, (uint16_t)(A_FADE + k)), v = (uint16_t)(o + rw(d, (uint16_t)(A_FADE_STEP + k)));
  ww(d, (uint16_t)(A_FADE + k), v);
  if ((v >> 8) != (o >> 8)) { AT(f->chg); M(D_VOLCHG) |= chg; }
  AT(f->j);
  if (v >> 8) { AT(f->next); return 0; }
  AT(f->zx);
  if (n) { AT(f->next); return 0; }
  AT(f->stop);
  return 1;
}

static void fades(SndDrv* d) {
  AT(0x1492);
  if (fadeStep(d, 0, M(D_PLAYING))) {   // the music faded out: it stops, the saved song kept
    uint8_t s = M(D_SAVED), n = M(D_NEXT);
    stopMusic(d);
    AT(0x14C4);
    M(D_SAVED) = s; M(D_NEXT) = n;
  }
  if (fadeStep(d, 1, 0xC0)) stopSfxA(d);
  if (fadeStep(d, 2, 0x30)) stopSfxB(d);
  AT(0x1524);
  if (M(D_TMOD_N)) { AT(0x1528); M(D_TMOD_N)--; ww(d, D_TMOD, (uint16_t)(rw(d, D_TMOD) + rw(d, D_TMOD_STEP))); }
  AT(0x1530);
  if (M(D_PMOD_N)) {
    AT(0x1534);
    M(D_PMOD_N)--;
    uint8_t o = M(D_PMOD + 1);
    ww(d, D_PMOD, (uint16_t)(rw(d, D_PMOD) + rw(d, D_PMOD_STEP)));
    if (M(D_PMOD + 1) != o) { AT(0x1540); M(D_PITCHCHG) |= M(D_PLAYING); }
  }
  AT(0x1543);
}

// ===================================================================================================================
// Stopping
// ===================================================================================================================

static void stopMusic(SndDrv* d) {   // $0F17 (command F1)
  AT(0x0F17);
  uint8_t a = (uint8_t)~M(D_SFX_VOICES);
  M(D_KOFF) |= a; M(D_KON) &= (uint8_t)~a;
  M(D_EON) &= (uint8_t)~a; M(D_PMON) &= (uint8_t)~a; M(D_NON) &= (uint8_t)~a;
  M(D_PLAYING) = 0; M(D_EON_M) = 0; M(D_PMON_M) = 0; M(D_NON_M) = 0;
  M(D_SONG) = 0xFF; M(D_SAVED) = 0xFF; M(D_NEXT) = 0xFF;
  AT(0x0F3B);
}

// $0F3F / $0F71: an effect group stops -- its voices keyed off, its tracks pointed at an end byte ($1DC1) two ticks on.
static void stopSfx(SndDrv* d, uint16_t entry, uint16_t top, uint16_t set, uint16_t adv, uint16_t ret,
                    uint8_t a, uint8_t m, int x, uint8_t bit, uint8_t until) {
  AT(entry);
  M(D_KOFF) |= a; M(D_KON) &= (uint8_t)~a; M(D_KEYED_S) &= (uint8_t)~a;
  M(D_VOLCHG) &= (uint8_t)~a; M(D_PITCHCHG) &= (uint8_t)~a;
  M(D_BIT) = bit;
  do {
    AT(top);
    int c = m >> 7;
    m = (uint8_t)(m << 1);
    if (c) { AT(set); ww(d, (uint16_t)(D_TRK_PTR + x), R_END_TRACK); M(D_CNT + x) = 2; }
    AT(adv);
    x -= 2;
    M(D_BIT) >>= 1;
  } while (!(M(D_BIT) & until));
  AT(ret);
}
static void stopSfxA(SndDrv* d) {
  uint8_t a = (uint8_t)(M(D_SFX_VOICES) & 0xC0);
  stopSfx(d, 0x0F3F, 0x0F59, 0x0F5D, 0x0F69, 0x0F70, a, a, 0x1E, 0x80, 0x20);
}
static void stopSfxB(SndDrv* d) {
  uint8_t a = (uint8_t)(M(D_SFX_VOICES) & 0x30);
  stopSfx(d, 0x0F71, 0x0F8D, 0x0F91, 0x0F9D, 0x0FA4, a, (uint8_t)(a << 2), 0x1A, 0x20, 0x08);
}

// ===================================================================================================================
// The tick
// ===================================================================================================================

// $02B0 / $041A: the 17 shadowed S-DSP registers (the list at $16FF / $1710: FIR, EFB, EVOL, EON, KOFF, FLG, NON,
// PMON, KON last), then the key shadows cleared.
static void flush(SndDrv* d, uint16_t top) {
  for (int y = 17; y >= 1; y--) {
    AT(top);
    dspW(d, (uint16_t)(top + 0x0A), M(R_FLUSH_REG + y), M(M(R_FLUSH_SRC + y)));
  }
  AT(top + 0x0E);
  M(D_KOFF) = 0;
  M(D_KON) = 0;
}

// $1438 (meter mode, command FF 01): four voices' output levels against their volumes, on the four ports.
static void meter(SndDrv* d) {
  AT(0x1438);
  uint8_t out[4], src = 0xBE, reg = 0x09, flag = 0;
  if (M(D_FLAGS2) & 0x10) { AT(0x144A); src = 0xC6; reg = 0x49; flag = 0x80; }
  AT(0x1450);
  uint8_t end = (uint8_t)(src + 8);
  for (int k = 0; k < 4; k++) {
    AT(0x1458);
    uint8_t ox = dspR(d, 0x145C, reg);
    uint8_t l = M(src++), r = M(src++);
    uint8_t hl = (uint8_t)(((ox * (uint8_t)(l << 1)) >> 8) & 0x70);
    uint8_t hr = (uint8_t)((ox * (uint8_t)(r << 1)) >> 8);
    out[k] = (uint8_t)((xcn(hr) & 7) | hl | flag);
    reg = (uint8_t)(reg + 0x10);
  }
  AT(0x1486);
  M(D_BIT) = reg; M(D_PASS) = end;
  insAt(d, 0x1488); portW2(d, 0, out[0]); portW2(d, 1, out[1]);
  insAt(d, 0x148C); portW2(d, 2, out[2]); portW2(d, 3, out[3]);
  M(D_FLAGS2) ^= 0x10;
  AT(0x1491);
}

// $0B5C: one nibble of the built-in sample's loop ($1DCB) flipped a tick -- a pulse whose width sweeps.
static void brrStep(SndDrv* d) {
  AT(0x0B5C);
  uint8_t a = M(D_BRR_STEP);
  if (a == 0x23) { AT(0x0B8A); ramW(d, 0x0B8F, R_BRR_LOOP + 1, (uint8_t)(M(R_BRR_LOOP + 1) ^ 0x70)); }
  AT(0x0B62);
  if (a & 1) AT(0x0B68);
  AT(0x0B69);
  ramW(d, 0x0B6C, (uint16_t)(R_BRR_LOOP + (a >> 1)), (uint8_t)(M(R_BRR_LOOP + (a >> 1)) ^ ((a & 1) ? 0x07 : 0x70)));
  a = (uint8_t)(M(D_BRR_STEP) + 1);
  if (a == 0x24) { AT(0x0B85); a = 3; }
  else {
    AT(0x0B76);
    uint8_t q = (uint8_t)(a >> 1);
    if (!spcDiv(&q, 0, 9)) { AT(0x0B81); a += 2; }
  }
  AT(0x0B87);
  M(D_BRR_STEP) = a;
}

// $02A0-$0374: one tick of timer 0.
static void tick(SndDrv* d) {
  AT(0x02A0);
  uint8_t ev = M(D_EVOL + 1);
  if (M(D_FLAGS) & 0x08) {   // command 8E: the echo louder
    AT(0x02A7);
    if (ev & 0x80) { AT(0x02AA); ev = 0x7F; } else ev = (uint8_t)(ev << 1);
  }
  AT(0x02AE);
  M(D_EVOLOUT) = ev;
  flush(d, 0x02B0);
  if (!(M(D_FLAGS2) & 0x80)) {
    AT(0x02C5);
    insAt(d, 0x02D4);
    portW2(d, 2, (uint8_t)(xcn(M(D_COND_MAX)) | M(D_MARK)));
    portW2(d, 3, (uint8_t)(((xcn(M(D_PLAYING)) | M(D_PLAYING)) & 0x0F) | M(D_SFX_VOICES)));
    insAt(d, 0x02DB);
    portW2(d, 0, 0);
    portW2(d, 1, M(A_FADE_N));
  } else { AT(0x02DF); meter(d); }
  AT(0x02E2);
  if (M(D_FLAGS) & 0x10) return;   // paused
  AT(0x02E5);
  if (!--M(D_FADE_DIV)) { AT(0x02E9); M(D_FADE_DIV) = 7; fades(d); }
  AT(0x02EF);
  sequencer(d);
  AT(0x02F2);
  processQueue(d);
  AT(0x02F5);
  if (M(D_FLAGS) & 0x10) return;
  AT(0x02F8);
  // the music's voices (the effects' own left out), with the music fade
  int x = 0;
  M(D_BIT) = 0x01;
  M(D_PASS) = (uint8_t)(~M(D_SFX_VOICES) & M(D_PLAYING) & M(D_KEYED_M));
  M(D_PASSVOL) = M(A_FADE + 1);
  for (;;) {
    AT(0x0323);
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) {
      AT(0x030E);
      M(D_TRK) = (uint8_t)x;
      if (!M(D_SFX_VOICES)) { AT(0x0314); voice(d, x); AT(0x0317); }   // while effects play, no music modulation
      else { AT(0x0319); voiceOut(d, x); }
      AT(0x031C);
      pollPort(d, 0x031F);
    } else if (!M(D_PASS)) break;
    AT(0x031F);
    x += 2;
    M(D_BIT) <<= 1;
  }
  AT(0x0329);
  // the effects' voices 7-6 with fade A, then 5-4 with fade B
  x = 0x1E;
  M(D_BIT) = 0x80;
  M(D_PASS) = (uint8_t)(M(D_SFX_VOICES) & M(D_KEYED_S));
  M(D_PASSVOL) = M(A_FADE + 3);
  for (;;) {
    AT(0x034A);
    uint8_t c = (uint8_t)(M(D_PASS) >> 7);
    M(D_PASS) <<= 1;
    if (c) { AT(0x033B); M(D_TRK) = (uint8_t)x; voice(d, x); AT(0x0340); pollPort(d, 0x0343); }
    AT(0x0343);
    x -= 2;
    M(D_BIT) >>= 1;
    if (M(D_BIT) & 0x20) break;
  }
  AT(0x0350);
  M(D_PASSVOL) = M(A_FADE + 5);
  for (;;) {
    AT(0x0363);
    uint8_t c = (uint8_t)(M(D_PASS) >> 7);
    M(D_PASS) <<= 1;
    if (c) { AT(0x0357); M(D_TRK) = (uint8_t)x; voice(d, x); AT(0x035C); pollPort(d, 0x035F); }
    else if (!M(D_PASS)) break;
    AT(0x035F);
    x -= 2;
    M(D_BIT) >>= 1;
  }
  AT(0x0369);
  M(D_VOLCHG) = 0;
  M(D_PITCHCHG) = 0;
  processQueue(d);
  AT(0x0371);
  brrStep(d);
  AT(0x0374);
}

// $0415: the tick an upload's kind-5 blocks run between their bytes -- the flush, the fades, the music's step and
// its voices; no status, no queue, no effects.
static void uploadTick(SndDrv* d) {
  AT(0x0415);
  if (M(D_FLAGS) & 0x10) { AT(0x0463); return; }
  AT(0x0418);
  flush(d, 0x041A);
  if (!--M(D_FADE_DIV)) { AT(0x0430); M(D_FADE_DIV) = 7; fades(d); }
  AT(0x0436);
  AT(0x0464);   // CALL $0464
  unsigned acc = M(D_TEMPO_ACC) + tempoAdd(d, 0x0464);
  M(D_TEMPO_ACC) = (uint8_t)acc;
  if (acc > 0xFF) {
    AT(0x0483);
    M(D_BIT) = 0x01;
    M(D_PASS) = M(D_PLAYING);
    seqPassUpload(d, 0);
    globalSlides(d);
  }
  AT(0x04C6);
  AT(0x0439);
  int x = 0;
  M(D_BIT) = 0x01;
  M(D_PASS) = (uint8_t)(~M(D_SFX_VOICES) & M(D_PLAYING) & M(D_KEYED_M));
  M(D_PASSVOL) = M(A_FADE + 1);
  for (;;) {
    AT(0x0458);
    uint8_t c = (uint8_t)(M(D_PASS) & 1);
    M(D_PASS) >>= 1;
    if (c) { AT(0x044F); M(D_TRK) = (uint8_t)x; voice(d, x); }
    else if (!M(D_PASS)) break;
    AT(0x0454);
    x += 2;
    M(D_BIT) <<= 1;
  }
  AT(0x045E);
  M(D_VOLCHG) = 0;
  M(D_PITCHCHG) = 0;
  AT(0x0463);
}

// ===================================================================================================================
// The commands
// ===================================================================================================================

static void trackInit(SndDrv* d, int x) {   // $0CF6 (straight-line: its RET is the caller's next mark)
  AT(0x0CF6);
  M(D_LOOP_SP + x) = (uint8_t)(x << 1);
  M(P_PSLIDE + x) = 0; ww(d, (uint16_t)(T_PSLIDE_D + x), 0); T(T_PSLIDE_LO) = 0;
  M(P_VIB_DEPTH + x) = 0; M(P_TREM_DEPTH + x) = 0; M(P_SWEEP + x) = 0;
  ww(d, (uint16_t)(T_SWEEP_ACC + x), 0);
  T(T_FINE) = 0; T(T_TRANSPOSE) = 0; T(T_VIB_SCALE) = 0;
  M(D_CNT + x) = 1;
  T(T_VIB_SCALE + 1) = 0x10;
}

// $13A9 / $13E5: the playing song's state kept aside ($F200-$F7FF to $F800, the direct page's $00-$7F to $FE00,
// page 1's $0100-$018F to $FF00) and brought back, the music then fading in. Byte by byte, as the driver copies.
static void songSave(SndDrv* d) {
  AT(0x13A9);
  M(D_SAVED) = M(D_NEXT);
  ww(d, 0x93, 0xF200); ww(d, 0x95, 0xF800);
  do {
    uint8_t y = 0;
    do { AT(0x13BE); M(rw(d, 0x95) + y) = M(rw(d, 0x93) + y); y++; AT(0x13C3); } while (y);
    AT(0x13C5);
    M(0x94)++; M(0x96)++;
  } while (M(0x94) != 0xF8);
  AT(0x13CE);
  ww(d, 0x95, (uint16_t)(rw(d, 0x95) - 1));
  for (uint8_t y = 0x80; y; y--) { AT(0x13D2); M(rw(d, 0x95) + y) = M(0xFFFF + y); AT(0x13D7); }
  AT(0x13D9);
  M(0x96)++;
  for (uint8_t y = 0x90; y; y--) { AT(0x13DD); M(rw(d, 0x95) + y) = M(0x00FF + y); AT(0x13E2); }
  AT(0x13E4);
}
static void songRestore(SndDrv* d) {
  AT(0x13E5);
  M(D_SAVED) = 0xFF;
  ww(d, 0x93, 0xF200); ww(d, 0x95, 0xF800);
  do {
    uint8_t y = 0;
    do { AT(0x13FA); M(rw(d, 0x93) + y) = M(rw(d, 0x95) + y); y++; AT(0x13FF); } while (y);
    AT(0x1401);
    M(0x94)++; M(0x96)++;
  } while (M(0x94) != 0xF8);
  AT(0x140A);
  ww(d, 0x95, (uint16_t)(rw(d, 0x95) - 1));
  for (uint8_t y = 0x80; y; y--) { AT(0x140E); M(0xFFFF + y) = M(rw(d, 0x95) + y); AT(0x1413); }
  AT(0x1415);
  M(0x96)++;
  for (uint8_t y = 0x90; y; y--) { AT(0x1419); M(0x00FF + y) = M(rw(d, 0x95) + y); AT(0x141E); }
  AT(0x1420);
  M(A_FADE_N) = 0x10; M(A_FADE) = 0; ww(d, A_FADE_STEP, 0x0EF0); M(A_FADE + 1) = 0x10;
  AT(0x1437);
}

// The tail of `CALL $07A8` reached by a JMP: its RET is the caller's caller's next mark.
static void ackPortsTail(SndDrv* d) { AT(0x07A8); ctlW(d, 0x07A8, 0xF1, 0x13); ctlW(d, 0x07AB, 0xF1, 0x03); }

// $0D29 (before commands 10, 11, 14, 15): the song already playing is refused (port 1 = $FF; the driver then waits for
// the CPU's next post, and acks it, the handler's address dropped), another is answered with its id. Returns 1 when
// the song starts (the pushed handler runs).
static int songPre(SndDrv* d, uint8_t cmd) {
  AT(0x0D29);
  uint8_t a = M(D_CMD + 1);
  int same = a == M(D_SONG);
  M(D_SONG) = a;
  if (!same) { AT(0x0D31); portW(d, 0x0D31, 1, a); portW(d, 0x0D33, 0, cmd); return 1; }
  AT(0x0D36);
  portW(d, 0x0D3A, 1, 0xFF); portW(d, 0x0D3C, 0, cmd);
  AT(0x0D3E);
  while (!portR(d, 0x0D3E, 0)) AT(0x0D3E);
  AT(0x0D42);
  ackPortsTail(d);
  return 0;
}

// $0BD8-$0BE6 + $0BE6: a song starts -- all keyed off, its master volume and echo delay taken, the upload session (the
// song's data), then either the saved song resumed or the new one's tracks set up from its header at $2000. Ends in
// the main loop (`MOV SP,#$FF / JMP $0299`).
static void songStart(SndDrv* d, uint16_t entry, uint8_t cmd) {
  AT(entry);
  if (entry == 0x0BD8 || entry == 0x0BE0) { songSave(d); AT(entry + 3); }
  M(D_VARIANT) = (entry == 0x0BD8 || entry == 0x0BDB) ? 0x10 : 0x20;
  AT(0x0BE6);
  dsp1(d, 0x0BED, 0x5C, 0xFF);
  M(D_NEXT) = M(D_CMD + 1);
  M(D_MVOL) = M(D_CMD + 2);
  uint8_t e = (uint8_t)(M(D_CMD + 3) & 0x0F);
  if (e != M(D_EDL)) { AT(0x0BFC); M(D_EDL) = e; M(D_FLAGS2) |= 0x03; echoChange(d); }
  AT(0x0C05);
  upload(d, cmd);
  AT(0x0C08);
  M(D_SFX_VOICES) = 0;
  M(D_FLAGS2) &= (uint8_t)~0x20;
  M(D_COND) = 0; M(D_COND_MAX) = 0;
  M(D_PITCHCHG) = 0;
  int resumed = 0;
  if (M(D_SAVED) == M(D_NEXT)) {
    AT(0x0C18);
    songRestore(d);
    AT(0x0C1B);
    if (M(D_PLAYING)) {
      AT(0x0C1F);
      resumed = 1;
      M(D_FLG) = (uint8_t)((M(D_FLG) & 0xE0) | M(D_NCK_M));
      int x = 0;
      M(D_BIT) = 0x01;
      do {
        AT(0x0C2A);
        M(D_CNT + x)++;
        M(D_TRK) = (uint8_t)x;
        instrOut(d, x);
        AT(0x0C31);
        uint8_t r = voiceReg(x);
        dsp1(d, 0x0C3A, r, 0);
        dsp1(d, 0x0C3E, (uint8_t)(r + 1), 0);
        M(0x9C) = (uint8_t)(r + 2);
        pitchOut(d, x, (uint8_t)(r + 2));
        AT(0x0C44);
        x += 2;
        M(D_BIT) <<= 1;
      } while (M(D_BIT));
      AT(0x0C4A);
      M(D_EON_S) = 0; M(D_NON_S) = 0; M(D_PMON_S) = 0;
      M(D_KON) = M(D_KEYED_M); M(D_KOFF) = 0; M(D_KEYED_S) = 0;
      M(D_VOLCHG) = M(D_PLAYING);
    }
  }
  if (!resumed) {
    AT(0x0C5E);
    M(D_PLAYING) = 0;
    ww(d, D_EON_M, 0); ww(d, D_NON_M, 0); ww(d, D_PMON_M, 0);
    M(D_KON) = 0; M(D_KOFF) = 0; ww(d, D_KEYED_M, 0);
    M(D_TIE_M) = 0; M(D_SLUR_M) = 0; M(D_ROLL_M) = 0; M(D_PERC) = 0; M(D_MARK) = 0;
    M(D_TEMPO + 1) = 1; M(D_TEMPO_ACC) = 0xFF;
    uint8_t y = M(D_VARIANT);
    for (int k = 0x10; k; k--, y--) { AT(0x0C83); M(0x01 + k) = M(0x2003 + y); AT(0x0C8A); }
    AT(0x0C8C);
    ww(d, D_SONG_BASE, (uint16_t)(0x2024 - rw(d, A_SONG)));
    uint16_t empty = rw(d, A_SONG + 2);
    ww(d, 0x9B, empty);
    M(D_BIT) = 0x80;
    for (int x = 0x0E;; x -= 2) {
      AT(0x0CAB);
      uint16_t p = rw(d, (uint16_t)(D_TRK_PTR + x));
      if (p != empty) {
        AT(0x0CB3);
        M(D_PLAYING) |= M(D_BIT);
        ww(d, (uint16_t)(D_TRK_PTR + x), (uint16_t)(p + rw(d, D_SONG_BASE)));
        trackInit(d, x);
        AT(0x0CBF);
        T(T_EXPR) = 0xFF;
      }
      AT(0x0CC4);
      M(D_BIT) >>= 1;
      if (!M(D_BIT)) break;
    }
    AT(0x0CCA);
    M(D_VOLCHG) = 0;
  }
  AT(0x0CCD);
  if (M(D_FLAGS2) & 0x01) {   // a new echo delay: the old buffer's wait, then the new one's
    AT(0x0CD0);
    if (M(D_FLAGS2) & 0x02) {
      AT(0x0CD3);
      echoWait(d, D_EDL_OLD);
      AT(0x0CD8);
      M(D_ECHO_WAIT) = 0;
      echoStart(d);
    }
    AT(0x0CDE);
    echoWait(d, D_EDL);
    AT(0x0CE3);
    M(D_FLAGS2) &= (uint8_t)~0x01;
  }
  AT(0x0CE5);
  M(D_EON) = M(D_EON_M); M(D_NON) = M(D_NON_M); M(D_PMON) = M(D_PMON_M);
  timerR(d, 0x0CF1, 0);   // `MOV A,$FD`: timer 0's count dropped
  toLoop(d);
}

// $0D45 / $0D51 (commands 18 / 19): an effect starts on voices 7-6 or 5-4 -- its one or two tracks from the effect
// table at $2F00 (command byte 1 = the effect, byte 2 = the pan), whatever ran on those voices ended.
static void sfxStart(SndDrv* d, uint16_t entry, uint8_t cmd) {
  AT(entry);
  portW(d, entry, 0, cmd);
  int a7 = entry == 0x0D45;
  int x = a7 ? 0x1E : 0x1A;
  M(D_BIT) = a7 ? 0x80 : 0x20;
  M(D_CMD + 3) = a7 ? 0xC0 : 0x30;
  AT(0x0D5B);
  if (M(D_FLAGS) & 0x10) { AT(0x0D5E); return; }
  AT(0x0D5F);
  uint16_t tab = (uint16_t)(A_SFX_TAB + (M(D_CMD + 1) << 2));
  ww(d, 0x93, tab);
  M(0xA4) = 0;
  int y = 3;
  for (;;) {
    AT(0x0D7A);
    uint8_t hi = M(tab + y);
    if (hi) {
      AT(0x0D7E);
      M(D_TRK_PTR + x + 1) = hi;
      y--;
      M(D_TRK_PTR + x) = M(tab + y);
      M(0xA4) |= M(D_BIT);
      trackInit(d, x);
      AT(0x0D8B);
      M(D_CNT + x)++;
      T(T_VOL + 1) = 0x46; T(T_PAN + 1) = M(D_CMD + 2); T(T_OCT) = 0x48;
      T(T_VOL_N) = 0; T(T_PAN_N) = 0;
      M(0x9B) = (uint8_t)y;
      setInstr(d, x, 2);
      AT(0x0DAB);
      y = M(0x9B);
      x -= 2;
      M(D_BIT) >>= 1;
      y--;   // ($0DB3; $0DB1's `MOV A,#$DC` hides $0DB2's DEC Y)
    } else {
      AT(0x0DB2);
      y -= 2;
    }
    if (y < 0) break;
  }
  AT(0x0DB6);
  uint8_t got = M(0xA4);
  uint8_t a = (uint8_t)((M(D_SFX_VOICES) & M(D_CMD + 3)) | got);
  M(D_KON) &= (uint8_t)~a; M(D_TIE_S) &= (uint8_t)~a; M(D_SLUR_S) &= (uint8_t)~a; M(D_ROLL_S) &= (uint8_t)~a;
  M(D_VOLCHG) &= (uint8_t)~a; M(D_PITCHCHG) &= (uint8_t)~a;
  M(D_KOFF) |= a;
  M(D_KEYED_S) &= (uint8_t)~a;
  M(D_BYTE) = a;
  x = 0x1E;
  M(D_BIT) = 0x80;
  do {
    AT(0x0DDB);
    int c = M(D_BYTE) >> 7;
    M(D_BYTE) <<= 1;
    if (c) { AT(0x0DDF); endTrack(d, x); }
    AT(0x0DE2);
    x -= 2;
    M(D_BIT) >>= 1;
  } while (!(M(D_BIT) & 0x08));
  AT(0x0DE9);
  M(D_SFX_VOICES) |= got;
  M(D_EON) &= (uint8_t)~got; M(D_PMON) &= (uint8_t)~got; M(D_NON) &= (uint8_t)~got;
  M(D_KEYED_M) &= (uint8_t)~got;
  AT(0x0DFA);
}

// $0E2F (commands 80-83, entered at $0E1A / $0E22 / $0E2A): fade k (0 music, 2 effects A, 4 effects B) to byte 2 over
// byte 1 x 7 ticks.
static void fadeCmd(SndDrv* d, uint16_t entry, int k, uint8_t chg) {
  AT(entry);
  M(D_VOLCHG) |= chg;
  AT(0x0E2F);
  uint8_t tgt = M(D_CMD + 2), n = M(D_CMD + 1);
  for (;;) {
    AT(0x0E33);
    M(A_FADE_N + k) = n;
    if (!n) {
      AT(0x0E38);
      M(A_FADE + k) = 0; M(A_FADE_STEP + k) = 0; M(A_FADE_STEP + k + 1) = 0; M(A_FADE + k + 1) = tgt;
      AT(0x0E45);
      return;
    }
    AT(0x0E46);
    int c = tgt >= M(A_FADE + k + 1);
    uint8_t diff = (uint8_t)(tgt - M(A_FADE + k + 1));
    if (!diff) { n = 0; continue; }
    AT(0x0E4D);
    uint16_t v = slideStep(d, diff, c);
    AT(0x0E51);
    ww(d, (uint16_t)(A_FADE_STEP + k), v);
    M(A_FADE + k) = 0;
    AT(0x0E5E);
    return;
  }
}

// $0E82 / $0EAA (commands 86 / 87, both 85): the tempo or the pitch modifier to byte 2 over byte 1 x 7 ticks.
static void tempoModCmd(SndDrv* d) {
  AT(0x0E82);
  uint8_t tgt = M(D_CMD + 2), n = M(D_CMD + 1);
  for (;;) {
    AT(0x0E86);
    M(D_TMOD_N) = n;
    if (!n) { AT(0x0E8A); M(D_TMOD + 1) = tgt; M(D_TMOD) = 0; M(D_TMOD_STEP + 1) = 0; M(D_TMOD_STEP) = 0; AT(0x0E92); return; }
    AT(0x0E93);
    M(D_BYTE) = (uint8_t)(M(D_TMOD + 1) ^ 0x80);
    int c;
    uint8_t diff = sdiff(tgt, M(D_TMOD + 1), &c);
    if (!diff) { n = 0; continue; }
    AT(0x0EA1);
    uint16_t v = slideStep(d, diff, c);
    AT(0x0EA4);
    ww(d, D_TMOD_STEP, v);
    M(D_TMOD) = 0;
    AT(0x0EA9);
    return;
  }
}
static void pitchModCmd(SndDrv* d) {
  AT(0x0EAA);
  uint8_t tgt = M(D_CMD + 2), n = M(D_CMD + 1);
  for (;;) {
    AT(0x0EAE);
    M(D_PMOD_N) = n;
    if (!n) { AT(0x0EB2); M(D_PMOD + 1) = tgt; M(D_PMOD) = 0; M(D_PMOD_STEP + 1) = 0; M(D_PMOD_STEP) = 0; break; }
    AT(0x0EBC);
    M(D_BYTE) = (uint8_t)(M(D_PMOD + 1) ^ 0x80);
    int c;
    uint8_t diff = sdiff(tgt, M(D_PMOD + 1), &c);
    if (!diff) { n = 0; continue; }
    AT(0x0ECA);
    uint16_t v = slideStep(d, diff, c);
    AT(0x0ECD);
    ww(d, D_PMOD_STEP, v);
    M(D_PMOD) = 0;
    break;
  }
  AT(0x0ED2);
  M(D_PITCHCHG) = 0xFF;
  AT(0x0ED5);
}

// $0F5x: commands $80-$8F and $F0-$FF through the table at $16C0 (k = the command & $1F).
static void cmdHi(SndDrv* d, int k) {
  uint8_t p1 = M(D_CMD + 1);
  switch (k) {
  case 0x00:
    AT(0x0E11);
    fadeCmd(d, 0x0E1A, 0, 0x7F); AT(0x0E14);
    fadeCmd(d, 0x0E22, 2, 0xC0); AT(0x0E17);
    fadeCmd(d, 0x0E2A, 4, 0x30);
    break;
  case 0x01: fadeCmd(d, 0x0E1A, 0, 0x7F); break;
  case 0x02: fadeCmd(d, 0x0E22, 2, 0xC0); break;
  case 0x03: fadeCmd(d, 0x0E2A, 4, 0x30); break;
  case 0x05: AT(0x0E7F); pitchModCmd(d); AT(0x0E82); tempoModCmd(d); break;
  case 0x06: tempoModCmd(d); break;
  case 0x07: pitchModCmd(d); break;
  case 0x08: AT(0x12B6); M(D_COND) = (uint8_t)(p1 & 0x0F); AT(0x12BC); break;
  case 0x09: {
    AT(0x0ED6);
    int ch = p1 != M(D_SFXPITCH);
    M(D_SFXPITCH) = p1;
    if (ch) { AT(0x0EDE); M(D_PITCHCHG) |= (uint8_t)(M(D_SFX_VOICES) & 0xC0); }
    AT(0x0EE5);
    break;
  }
  case 0x0E: {   // 8E: loud -- the main volume $7F (else $40) and the echo doubled
    AT(0x0EE6);
    uint8_t v;
    if (p1) { M(D_FLAGS) |= 0x08; AT(0x0EF4); v = 0x7F; }
    else { M(D_FLAGS) &= (uint8_t)~0x08; AT(0x0EF0); v = 0x40; }
    AT(0x0EF6);
    dsp1(d, 0x0EFB, 0x0C, v);
    dspW(d, 0x07A5, 0x1C, v);   // (JMP $07A3)
    break;
  }
  case 0x10: AT(0x0F0E); stopMusic(d); AT(0x0F11); stopSfxA(d); AT(0x0F14); stopSfxB(d); break;
  case 0x11: stopMusic(d); break;
  case 0x12: AT(0x0F3C); if (p1 & 1) stopSfxB(d); else stopSfxA(d); break;
  case 0x13:
    AT(0x0F00);
    if (p1) { AT(0x0F08); M(D_FLAGS) |= 0x01; } else { AT(0x0F04); M(D_FLAGS) &= (uint8_t)~0x01; }
    AT(0x0F0A);
    M(D_VOLCHG) = 0xFF;
    AT(0x0F0D);
    break;
  case 0x14: AT(0x0FA5); M(D_MUTE) = p1; M(D_VOLCHG) = 0xFF; AT(0x0FAB); break;
  case 0x15:   // F5: pause (the voices' ADSR off and silenced) / resume
    AT(0x0FBE);
    if (p1) {
      AT(0x0FC2);
      for (int y = 0x05; y < 0x80; y += 0x10) {
        AT(0x0FC4);
        uint8_t v = dspR(d, 0x0FC6, (uint8_t)y);
        dspW(d, 0x0FCA, (uint8_t)y, (uint8_t)(v & 0x7F));
      }
      AT(0x0FD3);
      for (int y = 0x00; y < 0x80; y += 0x10) {
        AT(0x0FD7);
        dspW(d, 0x0FD9, (uint8_t)y, 0);
        dspW(d, 0x0FDE, (uint8_t)(y + 1), 0);
      }
      AT(0x0FE7);
      M(D_FLAGS) |= 0x10;
      M(D_VOLCHG) = 0; M(D_KON) = 0;
      for (int y = 0x10; y; y--) { AT(0x0FF1); M(0x00BD + y) = 0; AT(0x0FF4); }
      AT(0x0FF6);
    } else {
      AT(0x0FF7);
      for (int y = 0x05; y < 0x80; y += 0x10) {
        AT(0x0FF9);
        uint8_t v = dspR(d, 0x0FFB, (uint8_t)y);
        dspW(d, 0x0FFF, (uint8_t)y, (uint8_t)(v | 0x80));
      }
      AT(0x1008);
      M(D_FLAGS) &= (uint8_t)~0x10;
      M(D_VOLCHG) = 0xFF;
      AT(0x100D);
    }
    break;
  case 0x16: {   // F6: fast -- a step every tick, and timer 0 at 8 kHz
    AT(0x129D);
    uint8_t a;
    if (p1) { M(D_FLAGS2) |= 0x20; AT(0x12AB); a = 0x01; }
    else { M(D_FLAGS2) &= (uint8_t)~0x20; AT(0x12A7); a = 0x2A; }
    AT(0x12AD);
    ctlW(d, 0x12AD, 0xF1, 0x02); ctlW(d, 0x12B0, 0xFA, a); ctlW(d, 0x12B2, 0xF1, 0x03);
    break;
  }
  case 0x18: AT(0x0FAC); M(D_MUTE) &= (uint8_t)~p1; M(D_VOLCHG) |= p1; AT(0x0FB4); break;
  case 0x19: AT(0x0FB5); M(D_MUTE) |= p1; M(D_VOLCHG) |= p1; AT(0x0FBD); break;
  case 0x1C: echoAlloc(d); break;
  case 0x1D: AT(0x12F2); dspW(d, 0x07A5, p1, M(D_CMD + 2)); break;   // (JMP $07A3)
  case 0x1E: upload(d, 0xFE); break;
  case 0x1F:   // FF: 01 = the meter mode on/off, F0 = back to the IPL
    AT(0x12BD);
    if (p1 == 0x01) {
      AT(0x12C3);
      if (M(D_CMD + 2)) M(D_FLAGS2) |= 0x80; else M(D_FLAGS2) &= (uint8_t)~0x80;
      AT(0x12CB);
      break;
    }
    AT(0x12CC);
    if (p1 != 0xF0) { AT(0x12CB); break; }
    AT(0x12D0);
    dsp1(d, 0x12D7, 0x6C, 0xE0);
    ctlW(d, 0x12D7, 0xF1, 0x80);
    toIpl(d);
    break;
  default: AT(0x12DD); break;   // RET
  }
}

// $0BAC: run the command at $8D-$90 (from the main loop's poll, or the queue).
static void dispatch(SndDrv* d) {
  AT(0x0BAC);
  uint8_t c = M(D_CMD);
  if (c & 0x80) {
    AT(0x0BB1);
    portW(d, 0x0BB1, 0, c);
    AT(0x0DFB);
    if (c < 0xF0) {
      AT(0x0DFF);
      if (c >= 0x90) { AT(0x0E10); return; }
    }
    AT(0x0E03);
    cmdHi(d, c & 0x1F);
    return;
  }
  AT(0x0BB6);
  if (c >= 0x10) {
    AT(0x0BBA);
    if (c < 0x20) {
      AT(0x0BBE);
      int k = c & 0x0F;
      switch (k) {   // the table at $1680, each song start preceded by $0D29 (the table at $16A0)
      case 0x0: case 0x1: case 0x4: case 0x5: {
        AT(0x0BCF);
        if (!songPre(d, c)) return;
        static const uint16_t kEntry[6] = { 0x0BDB, 0x0BE3, 0, 0, 0x0BD8, 0x0BE0 };
        songStart(d, kEntry[k], c);
        return;
      }
      case 0x8: AT(0x0BD4); sfxStart(d, 0x0D45, c); return;
      case 0x9: AT(0x0BD4); sfxStart(d, 0x0D51, c); return;
      default: AT(0x0BD4); AT(0x12DD); return;
      }
    }
  }
  AT(0x0BD5);
  portW(d, 0x0BD5, 0, c);
}

// ===================================================================================================================
// The upload session ($100E, command FE and every song start): the CPU's blocks, each a header (kind, destination)
// and its data, through the receivers. The two-processor protocol as the driver runs it: the tokens on port 0, each
// echoed; data on ports 1-3. The receivers' self-modified store addresses are kept where the driver keeps them.
// ===================================================================================================================

// `CMP X,#$FF / BNE / MOV A,#$F0 / CBNE $F5 / MOV $8E,A / JMP $12BD`: a token $FF with $F0 on port 1 is the reset.
static void uploadReset(SndDrv* d) {
  M(D_CMD + 1) = 0xF0;
  AT(0x12BD); AT(0x12CC); AT(0x12D0);
  dsp1(d, 0x12D7, 0x6C, 0xE0);
  ctlW(d, 0x12D7, 0xF1, 0x80);
  toIpl(d);
}

// $1275: the defragmenter's wait for the next token -- returns 1 (carry) for the end marker $E0, 0 for data. `ret` =
// the caller's mark after the CALL.
static int waitTok(SndDrv* d, uint8_t* px, uint16_t ret) {
  uint8_t x = *px;
wait:
  AT(0x1275);
  if (portR(d, 0x1275, 0) == x) goto wait;
newTok:
  AT(0x1279);
  x = portR(d, 0x1279, 0);
  if (!(x & 0x80)) {
    AT(0x1293);
    if (portR(d, 0x1293, 0) == x) { AT(0x129B); *px = x; AT(ret); return 0; }
    AT(0x1297);
    goto wait;
  }
  AT(0x127D);
  if (portR(d, 0x127D, 0) != x) goto newTok;
  AT(0x1281);
  if (x == 0xE0) { AT(0x1299); *px = x; AT(ret); return 1; }   // ($1299 SETC; $129A's MOV Y,#$60 hides $129B's CLRC)
  AT(0x1285);
  if (x != 0xFF) goto wait;
  AT(0x1289);
  if (portR(d, 0x128B, 1) != 0xF0) goto wait;
  AT(0x128E);
  uploadReset(d);
  return 0;
}

// Kind 3 ($107C) and kind 5 ($10EC): three bytes a token (ports 1-3), the store address self-modified in three
// places and moved on by $80 as Y passes $80; kind 5 runs the upload tick ($0415) while it waits, each time timer 0
// has counted.
typedef struct {
  uint16_t entry, ops[3], data, st[3], echo, rd[2], adv, inc, setLo;   // the data
  uint16_t wait, tickCall, tickRet, cmpWait;                           // the wait (kind 5: wait = its timer read)
  uint16_t newTok, loop, stable, e0, ff, f0, done;                     // a new token
  int ticks;
} Recv3;
static const Recv3 kRecv3 = { 0x107C, { 0x109B, 0x10A3, 0x10A9 }, 0x1098, { 0x109A, 0x10A2, 0x10A8 }, 0x109E,
                              { 0x10A0, 0x10A6 }, 0x10CC, 0x10D7, 0x10E0,
                              0x10AE, 0, 0, 0x10AE,
                              0x10B2, 0x1094, 0x10B6, 0x10BA, 0x10BE, 0x10C2, 0x10EB, 0 };
static const Recv3 kRecv5 = { 0x10EC, { 0x110B, 0x1113, 0x1119 }, 0x1108, { 0x110A, 0x1112, 0x1118 }, 0x110E,
                              { 0x1110, 0x1116 }, 0x1147, 0x1152, 0x115B,
                              0x111E, 0x1122, 0x1127, 0x1129,
                              0x112D, 0x1104, 0x1131, 0x1135, 0x1139, 0x113D, 0x1166, 1 };
static void recv3(SndDrv* d, const Recv3* r, uint8_t* px) {
  uint8_t x = *px, y = 0, v;
  AT(r->entry);
  uint16_t b = rw(d, 0x97);
  for (int k = 0; k < 3; k++) ww(d, r->ops[k], b);
  AT(r->data);
data:
  v = portR(d, r->data, 1);
  ramW(d, r->st[0], (uint16_t)(rw(d, r->ops[0]) + y), v);
  y++;
  portW(d, r->echo, 0, x);
  v = portR(d, r->rd[0], 2);
  ramW(d, r->st[1], (uint16_t)(rw(d, r->ops[1]) + y), v);
  y++;
  v = portR(d, r->rd[1], 3);
  ramW(d, r->st[2], (uint16_t)(rw(d, r->ops[2]) + y), v);
  y++;
  if (y & 0x80) {
    AT(r->adv);
    y &= 0x7F;
    uint8_t lo = (uint8_t)(M(r->ops[0]) ^ 0x80);
    if (!(lo & 0x80)) { AT(r->inc); M(r->ops[0] + 1)++; M(r->ops[1] + 1)++; M(r->ops[2] + 1)++; }
    AT(r->setLo);
    M(r->ops[0]) = lo; M(r->ops[1]) = lo; M(r->ops[2]) = lo;
  }
wait:
  AT(r->wait);
  if (r->ticks) {
    if (timerR(d, r->wait, 0)) { AT(r->tickCall); uploadTick(d); AT(r->tickRet); }
    AT(r->cmpWait);
  }
  if (portR(d, r->cmpWait, 0) == x) goto wait;
newTok:
  AT(r->newTok);
  x = portR(d, r->newTok, 0);
  if (!(x & 0x80)) {
    AT(r->loop);
    if (portR(d, r->loop, 0) != x) goto newTok;
    AT(r->data);
    goto data;
  }
  AT(r->stable);
  if (portR(d, r->stable, 0) != x) goto newTok;
  AT(r->e0);
  if (x == 0xE0) { AT(r->done); *px = x; return; }
  AT(r->ff);
  if (x != 0xFF) goto wait;
  AT(r->f0);
  if (portR(d, (uint16_t)(r->f0 + 2), 1) != 0xF0) goto wait;
  AT(r->f0 + 5);
  uploadReset(d);
}

// Kind 2 ($1167): two bytes a token (ports 1-2), the base's high byte stepped when Y wraps.
static void recv2(SndDrv* d, uint8_t* px) {
  uint8_t x = *px, y = 0, v;
  AT(0x1167);
  uint16_t b = rw(d, 0x97);
  ww(d, 0x1180, b); ww(d, 0x1188, b);
  AT(0x117D);
data:
  v = portR(d, 0x117D, 1);
  ramW(d, 0x117F, (uint16_t)(rw(d, 0x1180) + y), v);
  y++;
  v = portR(d, 0x1183, 2);
  portW(d, 0x1185, 0, x);
  ramW(d, 0x1187, (uint16_t)(rw(d, 0x1188) + y), v);
  y++;
  if (!y) { AT(0x118D); M(0x1181)++; M(0x1189)++; }
wait:
  AT(0x1193);
  if (portR(d, 0x1193, 0) == x) goto wait;
newTok:
  AT(0x1197);
  x = portR(d, 0x1197, 0);
  if (!(x & 0x80)) {
    AT(0x1179);
    if (portR(d, 0x1179, 0) != x) goto newTok;
    AT(0x117D);
    goto data;
  }
  AT(0x119B);
  if (portR(d, 0x119B, 0) != x) goto newTok;
  AT(0x119F);
  if (x == 0xE0) { AT(0x11B1); *px = x; return; }
  AT(0x11A3);
  if (x != 0xFF) goto wait;
  AT(0x11A7);
  if (portR(d, 0x11A9, 1) != 0xF0) goto wait;
  AT(0x11AC);
  uploadReset(d);
}

// Kind 1 ($11B2): one byte a token (port 1), stored through the pointer at $97.
static void recv1(SndDrv* d, uint8_t* px) {
  uint8_t x = *px, y = 0, v;
  AT(0x11B2);
  AT(0x11BA);
data:
  portW(d, 0x11BA, 0, x);
  v = portR(d, 0x11BC, 1);
  ramW(d, 0x11BE, (uint16_t)(rw(d, 0x97) + y), v);
  y++;
  if (!y) { AT(0x11C3); M(0x98)++; }
wait:
  AT(0x11C5);
  if (portR(d, 0x11C5, 0) == x) goto wait;
newTok:
  AT(0x11C9);
  x = portR(d, 0x11C9, 0);
  if (!(x & 0x80)) {
    AT(0x11B6);
    if (portR(d, 0x11B6, 0) != x) goto newTok;
    AT(0x11BA);
    goto data;
  }
  AT(0x11CD);
  if (portR(d, 0x11CD, 0) != x) goto newTok;
  AT(0x11D1);
  if (x == 0xE0) { AT(0x11E3); *px = x; return; }
  AT(0x11D5);
  if (x != 0xFF) goto wait;
  AT(0x11D9);
  if (portR(d, 0x11DB, 1) != 0xF0) goto wait;
  AT(0x11DE);
  uploadReset(d);
}

// Kinds 0, 4, 6 ($11E4): the data discarded, each token echoed.
static void recv0(SndDrv* d, uint8_t* px) {
  uint8_t x = *px;
  for (;;) {
    AT(0x11E4);
    portW(d, 0x11E4, 0, x);
    AT(0x11E6);
    while (portR(d, 0x11E6, 0) == x) AT(0x11E6);
    AT(0x11EA);
    for (;;) { x = portR(d, 0x11EA, 0); if (portR(d, 0x11EC, 0) == x) break; AT(0x11EA); }
    AT(0x11F0);
    if (!(x & 0x80)) continue;
    AT(0x11F3);
    if (x == 0xE0) { AT(0x1205); *px = x; return; }
    AT(0x11F7);
    if (x != 0xFF) continue;
    AT(0x11FB);
    if (portR(d, 0x11FD, 1) != 0xF0) continue;
    AT(0x1200);
    uploadReset(d);
  }
}

// Kind 7 ($1206): the defragmenter -- each move a destination ($99), a length ($9C pages + $9B bytes) and, after the
// first, a source ($97: the header's address for the first); pages copied up, the rest down, through self-modified
// operands.
static void recv7(SndDrv* d, uint8_t* px) {
  uint8_t x = *px;
  for (;;) {
    AT(0x1206);
    uint8_t lo = portR(d, 0x1206, 2), hi = portR2(d, 3);
    ww(d, 0x99, (uint16_t)(lo | hi << 8));
    portW(d, 0x120A, 0, x);
    if (waitTok(d, &x, 0x120F)) { AT(0x1274); *px = x; return; }
    AT(0x1211);
    lo = portR(d, 0x1211, 2); hi = portR2(d, 3);
    ww(d, 0x9B, (uint16_t)(lo | hi << 8));
    portW(d, 0x1215, 0, x);
    AT(0x1217);
    if (hi) {
      AT(0x121A);
      ww(d, 0x122D, rw(d, 0x97));
      ww(d, 0x1230, rw(d, 0x99));
      do {
        uint8_t y = 0;
        do {
          AT(0x122C);
          uint8_t v = M(rw(d, 0x122D) + y);
          ramW(d, 0x122F, (uint16_t)(rw(d, 0x1230) + y), v);
          y++;
        } while (y);
        AT(0x1235);
        M(0x98)++; M(0x9A)++; M(0x122E)++; M(0x1231)++;
      } while (--M(0x9C));
    }
    AT(0x1242);
    ww(d, 0x97, (uint16_t)(rw(d, 0x97) - 1));
    ww(d, 0x99, (uint16_t)(rw(d, 0x99) - 1));
    ww(d, 0x125B, rw(d, 0x97));
    ww(d, 0x125E, rw(d, 0x99));
    uint8_t y = M(0x9B);
    if (y) {
      do {
        AT(0x125A);
        uint8_t v = M(rw(d, 0x125B) + y);
        ramW(d, 0x125D, (uint16_t)(rw(d, 0x125E) + y), v);
      } while (--y);
    }
    AT(0x1262);
    if (waitTok(d, &x, 0x1265)) { AT(0x1274); *px = x; return; }
    AT(0x1267);
    lo = portR(d, 0x1267, 2); hi = portR2(d, 3);
    ww(d, 0x97, (uint16_t)(lo | hi << 8));
    portW(d, 0x126B, 0, x);
    if (waitTok(d, &x, 0x1270)) { AT(0x1274); *px = x; return; }
    AT(0x1272);
  }
}

// $100E: the session. `x` = the command that opened it (its echo goes on port 0 until the CPU's first token, x + 1).
static void upload(SndDrv* d, uint8_t x) {
  AT(0x100E);
  uint8_t want = (uint8_t)((x + 1) & 0x7F);
  M(0x9B) = want;
  uint8_t a;
  for (;;) {
    AT(0x1014);
    a = portR(d, 0x1014, 0);
    if (!a) continue;
    AT(0x1018);
    if (portR(d, 0x1018, 0) != a) continue;
    AT(0x101C);
    if (portR(d, 0x101C, 0) == x) { AT(0x1020); portW(d, 0x1020, 0, x); continue; }
    AT(0x1024);
    if (a == want) break;
  }
  AT(0x1028);
  x = a;
  for (;;) {   // $1029: a block's header
    AT(0x1029);
    if (M(D_FLAGS2) & 0x01) {   // a new echo buffer settling: timer 1's count kept
      AT(0x102C); AT(0x1326);
      M(D_ECHO_WAIT) = (uint8_t)(M(D_ECHO_WAIT) + timerR(d, 0x1326, 1));
      AT(0x102F);
    }
    AT(0x102F);
    uint8_t lo = portR(d, 0x102F, 2), hi = portR2(d, 3);
    ww(d, 0x97, (uint16_t)(lo | hi << 8));
    uint8_t kind = portR(d, 0x1033, 1);
    if (kind == 0xE0) {   // the session's end
      AT(0x1074);
      portW(d, 0x1074, 1, 0); portW(d, 0x1077, 0, x);
      ackPortsTail(d);
      return;
    }
    AT(0x1039);
    portW(d, 0x1039, 1, kind); portW(d, 0x103B, 0, x);
    int k = kind & 7;
    if (hi < 0x1E) { AT(0x1054); k = 0; }   // the driver's own code is never overwritten
    else {
      AT(0x1044);
      if (hi == 0x2F) {   // a new effect table: the effects stop first
        AT(0x1048);
        stopSfxA(d); AT(0x104D);
        stopSfxB(d); AT(0x1050);
      }
    }
    AT(0x1056);
    AT(0x1065);
    while (portR(d, 0x1065, 0) == x) AT(0x1065);
    AT(0x1069);
    for (;;) { x = portR(d, 0x1069, 0); if (portR(d, 0x106B, 0) == x) break; AT(0x1069); }
    AT(0x106F);
    portW(d, 0x1071, 1, 0xEE);
    switch (k) {   // the table at $1544 (RET to the receiver, its RET to $1029)
    case 3: recv3(d, &kRecv3, &x); break;
    case 5: recv3(d, &kRecv5, &x); break;
    case 2: recv2(d, &x); break;
    case 1: recv1(d, &x); break;
    case 7: recv7(d, &x); break;
    default: recv0(d, &x); break;
    }
  }
}

// ===================================================================================================================
// The main loop, the boot, the IPL
// ===================================================================================================================

// $0299: the main loop -- the poll ($0B96: a command the CPU posted, port 0 nonzero and read twice the same, taken
// whole, acked and run), then a tick each time timer 0 has counted. Never returns.
static void mainLoop(SndDrv* d) {
  for (;;) {
    AT(0x0299);
    AT(0x0B96);
    uint8_t x = portR(d, 0x0B96, 0);
    if (!x) AT(0x0B9A);
    else {
      AT(0x0B9D);
      for (;;) { if (portR(d, 0x0B9D, 0) == x) break; AT(0x0B9B); x = portR(d, 0x0B9B, 0); AT(0x0B9D); }
      AT(0x0BA1);
      M(D_CMD + 2) = portR(d, 0x0BA1, 2); M(D_CMD + 3) = portR2(d, 3);
      M(D_CMD) = portR(d, 0x0BA5, 0); M(D_CMD + 1) = portR2(d, 1);
      ackPorts(d, 0x0BAC);
      dispatch(d);
    }
    AT(0x029C);
    if (!timerR(d, 0x029C, 0)) continue;
    AT(0x02A0);
    tick(d);
  }
}

// $0200: the driver starts -- its RAM cleared, the S-DSP set up, the timers, the echo, the built-in sample.
static void driverBoot(SndDrv* d) {
  AT(0x0200);
  for (int x = 0; x != 0xF0; x++) { AT(0x0208); M(x) = 0; AT(0x020B); }
  AT(0x020D);
  M(D_SONG)--;
  ww(d, D_SAVED, (uint16_t)(rw(d, D_SAVED) - 1));
  dsp1(d, 0x0216, 0x2C, 0);
  dsp1(d, 0x021B, 0x3C, 0);
  for (int x = 0x40; x; x -= 4) { AT(0x021D); M(0xF16E + x) = 0; AT(0x0224); }
  AT(0x0226);
  dsp1(d, 0x022D, 0x5D, 0x1E);
  for (int y = 0x07; y < 0x80; y += 0x10) { AT(0x0231); dspW(d, 0x0233, (uint8_t)y, 0xA0); }
  AT(0x023C);
  ctlW(d, 0x023C, 0xF1, 0x30); ctlW(d, 0x023F, 0xFA, 0x2A); ctlW(d, 0x0242, 0xFB, 0x80); ctlW(d, 0x0245, 0xF1, 0x03);
  M(D_CMD + 1) = 0x05;
  echoAlloc(d);
  AT(0x024E);
  M(D_KOFF)--;
  M(D_FADE_DIV) = 7;
  for (int x = 6; x; x -= 2) { AT(0x0255); M(0xF15F + x) = 0xFF; M(0xF164 + x) = 0; AT(0x0260); }
  AT(0x0262);
  ramW(d, 0x0264, 0x1E40, 0xC2); ramW(d, 0x0269, 0x1E41, 0x1D);   // sample $10: the built-in pulse
  ramW(d, 0x026E, 0x1E42, 0xCB); ramW(d, 0x0273, 0x1E43, 0x1D);
  ramW(d, 0x0278, 0x1FA0, 0xFF); ramW(d, 0x027D, 0x1FA1, 0xE0);   // instrument $10's ADSR and tuning
  ramW(d, 0x0282, 0x1F20, 0xE1); ramW(d, 0x0287, 0x1F21, 0x00);
  AT(0x028A);
  dsp1(d, 0x0291, 0x0C, 0x40);
  dsp1(d, 0x0296, 0x1C, 0x40);
  M(D_BRR_STEP) = 2;
}

// $FFC0: the sound processor's boot ROM -- its RAM's direct page cleared, $BBAA on the ports, then the CPU's blocks
// (each a destination on ports 2-3, its bytes with a running index on port 0), and the jump to the address of the
// last header (port 1 = 0).
static void ipl(SndDrv* d) {
  AT(0xFFC0);
  for (int x = 0xEF; x; x--) { AT(0xFFC5); M(x) = 0; AT(0xFFC7); }
  AT(0xFFC9);
  portW(d, 0xFFC9, 0, 0xAA); portW(d, 0xFFCC, 1, 0xBB);
  AT(0xFFCF);
  while (portR(d, 0xFFCF, 0) != 0xCC) AT(0xFFCF);
  AT(0xFFD4);
  for (;;) {
    AT(0xFFEF);
    uint8_t lo = portR(d, 0xFFEF, 2), hi = portR2(d, 3);
    M(0x00) = lo; M(0x01) = hi;
    uint8_t p0 = portR(d, 0xFFF3, 0), p1 = portR2(d, 1);
    portW(d, 0xFFF5, 0, p0);
    if (!p1) break;
    AT(0xFFD6);
    uint8_t y;
    while ((y = portR(d, 0xFFD6, 0)) != 0) AT(0xFFD6);
    for (;;) {
      AT(0xFFDA);
      uint8_t p = portR(d, 0xFFDA, 0);
      if (p == y) {
        AT(0xFFDE);
        uint8_t a = portR(d, 0xFFDE, 1);
        portW(d, 0xFFE0, 0, y);
        ramW(d, 0xFFE2, (uint16_t)(rw(d, 0x00) + y), a);
        y++;
        if (y) continue;
        AT(0xFFE7);
        M(0x01)++;
        if (!(M(0x01) & 0x80)) continue;
      } else {
        AT(0xFFE9);
        if (!((uint8_t)(y - p) & 0x80)) continue;
      }
      AT(0xFFEB);
      p = portR(d, 0xFFEB, 0);
      if (!((uint8_t)(y - p) & 0x80)) continue;
      break;
    }
  }
  AT(0xFFFB);
  uint16_t to = rw(d, 0x00);
  if (to != 0x0200) {   // only the driver's own entry is a program here
    SndClock* c = &d->clk;
    if (!c->err) { c->err = 3; c->errAt = 0xFFFB; c->errTo = to; }
    for (;;) d->host.stuck(d->host.ctx);
  }
  driverBoot(d);
  mainLoop(d);
}

// ===================================================================================================================
// The program and its entries
// ===================================================================================================================

static void toLoop(SndDrv* d) { longjmp(d->jb, SND_ENTRY_LOOP); }
static void toIpl(SndDrv* d) { longjmp(d->jb, SND_ENTRY_IPL); }

void SndDrvProgram(SndDrv* d) {
  int e = setjmp(d->jb);
  if (!e) e = d->entry;
  if (e == SND_ENTRY_RESET) {   // spc.c's reset: one step of 8 cycles before the boot ROM's first instruction
    SndClock* c = &d->clk;
    while (c->cyc >= c->want && c->boundary) c->boundary(c->boundaryCtx);
    c->cyc += 8;
    c->ip = 0xFFC0;
    e = SND_ENTRY_IPL;
  }
  if (e == SND_ENTRY_IPL) ipl(d);
  mainLoop(d);
}

void SndDrvInit(SndDrv* d, const SndDrvHost* host) { d->host = *host; }
uint8_t* SndDrvMem(SndDrv* d) { return d->m; }
