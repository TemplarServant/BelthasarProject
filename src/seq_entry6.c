// seq_entry6.c -- sequence entry 6 (`FF 26`): the Epoch flies into Lavos's shell.
//
// Scene 432 (loc 1B0); the teardown reboots into 1DA, Lavos's outer shell (disasm/notes/sequence_table.md sec. 17). Page B
// ($7CF3) drives it all: the dive, the task-list swaps ($7EE8 / $7ED1), the hooks that reset page A ($7A35 / $79F8), the
// divide $32E2, the palette records, the SPC's marker (`F5 04`) and the END ($34E3).
// This file is the logic over the WRAM image. The picture is seq_entry6pic.c: the setup's packets and
// tables, mode 7 ($8278 on the main thread, $824A, $81C4, $82D1's $357B), the Epoch's shake, the lightning and the sprites
// ($7F7D, $8079, $835B, $7FCB, $7BF2, $7F58). The shake's and the lightning's draws call C3/02DD with a 16-bit A, which
// reads PPU state ($2138, $213E); nothing the logic reads comes from them (page B's r5 and $0386 are theirs alone).
#include "seq_entry6.h"
#include "romview.h"     // the ROM's tables from ct_assets.dat (CT_ASSET_TABLE)
#include "seq_entry6pic.h"   // the picture, when the host draws it (io->pic)
#include "seq_racepic.h"
#include "objslot.h"
#include <string.h>

static unsigned rd(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

int CtEntry6StageHas(uint8_t operand) { return (operand & 0x1F) == 6 && !(operand & 0x40); }

// The setup's cycles, from `CT_CALLROM_NONMI=1 ct.exe --callrom 7E78DD --pokewram 0384=26 --regdb 00 --m8 --x16` on a
// state inside the opening with the overlay in WRAM: the packets and the mode-7 tables above all.
long CtEntry6StageSetupCycles(void) { return 5216178L; }

static int s_songHold;

// $7E:37FA -- the palette records' frame: FD/E39C over the twelve records at $0520 (seq_entry3.c's).
// `dp` = the direct page it runs on: `LDX $4202` reads a write-only register -- the open bus, $4242 -- and `STX $F0` keeps it
// there (a task's D = 0; a script's call runs on its page, $0A00 / $0B00).
static void entry6Objslot(uint8_t* w, const CtRaceIo* io, unsigned dp) {
  wr(w, dp + 0xF0, 0x4242);                                           // 37FA LDX $4202 / STX $F0
  CtObjSlotIo o; memset(&o, 0, sizeof o);
  o.s = w; o.colourModes = 1; o.rom = io ? io->rom : NULL; o.romLen = io ? io->romLen : 0;
  CtObjSlotService(&o);
}

// $7E:32E2 -- the divide (M = 16, D = 0): $F0 = A, $F8 = $F2 - 1, $F4 = 0; eight steps that shift $F0's top into A (`ROL $F0 /
// ROL A`) and eight that shift zeros (`ASL A`), each `SBC $F8 / BCS / ADC $F2 / CLC` and the carry into the quotient `ROL
// $F4`. Returns A (what is left); $F4 holds the quotient.
static unsigned entry6Div(uint8_t* w, unsigned a) {
  wr(w, 0x00F0, a);                                                   // 32E2 STA $F0
  wr(w, 0x00F8, rd(w, 0x00F2) - 1);                                   // 32E4 LDA $F2 / DEC A / STA $F8
  a = 0; wr(w, 0x00F4, 0);                                            // 32E9 LDA #0000 / STA $F4
  int c = 0;                                                          // (the entry carry goes into $F0's bit 0 only)
  for (int k = 0; k < 16; k++) {
    if (k < 8) {
      unsigned f0 = rd(w, 0x00F0), nc = f0 >> 15;                      // ROL $F0
      wr(w, 0x00F0, ((f0 << 1) | (unsigned)c) & 0xFFFF); c = (int)nc;
      nc = a >> 15; a = ((a << 1) | (unsigned)c) & 0xFFFF; c = (int)nc;   // ROL A
    } else { c = (int)(a >> 15); a = (a << 1) & 0xFFFF; }              // ASL A
    unsigned d = rd(w, 0x00F8), t = a - d - (unsigned)(1 - c);         // SBC $F8
    c = (a + 0x10000u - d - (unsigned)(1 - c)) > 0xFFFF; a = t & 0xFFFF;
    if (!c) { a = (a + rd(w, 0x00F2)) & 0xFFFF; c = 0; }               // BCS / ADC $F2 (C clear: no carry in) / CLC
    wr(w, 0x00F4, ((rd(w, 0x00F4) << 1) | (unsigned)c) & 0xFFFF);     // ROL $F4 (the carry out of $F4 is dropped)
  }
  return a;
}

// $7E:8205 (M = 16, D = 0): `$F2 = $0027 = page B's r0`, $32E2 of 0400 by it; `$17 = $0B04 - $F4` (SEC / SBC), `$0B38 = $F4 +
// 3 + the SBC's carry`, `$15 = $0B02 + 4 + that ADC's carry`; the Epoch's sprite (`$82B0` = 09, or 05 from $0027 >= 80, then
// C3/0D5E: the picture's).
static void entry6Epoch8205(uint8_t* w, const CtRaceIo* io) {
  wr(w, 0x00F2, rd(w, 0x0B00)); wr(w, 0x0027, rd(w, 0x0B00));         // 8205-820A
  entry6Div(w, 0x0400);                                               // 820D LDA #0400 / JSR $32E2
  unsigned b4 = rd(w, 0x0B04), f4 = rd(w, 0x00F4);
  int c = b4 >= f4;                                                   // 8213 LDA $0B04 / SEC / SBC $F4
  wr(w, 0x0017, b4 - f4);
  unsigned t = f4 + 3 + (unsigned)c;                                  // 821B LDA $F4 / ADC #0003 / STA $0B38
  wr(w, 0x0B38, t); c = t > 0xFFFF;
  wr(w, 0x0015, rd(w, 0x0B02) + 4 + (unsigned)c);                     // 8223 LDA $0B02 / ADC #0004 / STA $15
  w[0x82B0] = (w[0x0027] >= 0x80) ? 0x05 : 0x09;                      // 822D-823C (8-bit)
  if (io && io->pic) CtSeqPicOam0D5E(w, 0x82B0);                      // 8242 LDA #82B0 / JSL C3/0D5E: the Epoch (the picture's)
}

// $7E:7F12 / $7F35 (D = $0A00, M = 16, the carry clear: the dispatcher's `CPX #0020`): page A's r1 (r2) += r8, kept in
// 0..3FF -- `CMP #0400 / BMI`: else `SBC #0400` (the CMP's carry set); `CMP #0000 / BPL`: else `ADC #0400` with the CMP's
// carry set, i.e. + 0401 -- then r3 = r1 + 80 (`CLC / ADC`), r4 = r2 - 81 (`CLC / SBC #0080`).
static unsigned entry6Wrap(unsigned a) {
  if (!((a - 0x0400) & 0x8000)) return (a - 0x0400) & 0xFFFF;         // 7F1A CMP #0400 / BMI / SBC #0400
  if (!(a & 0x8000)) return a;                                        // 7F24 CMP #0000 / BPL
  return (a + 0x0401) & 0xFFFF;                                       // 7F29 ADC #0400 (C = 1)
}
static void entry6DiveA(uint8_t* w, int r2) {
  if (!r2) {
    unsigned a = entry6Wrap((rd(w, 0x0A02) + rd(w, 0x0A10)) & 0xFFFF); // 7F16 LDA $02 / ADC $10
    wr(w, 0x0A02, a); wr(w, 0x0A06, a + 0x0080);                      // 7F2C STA $02 / CLC / ADC #0080 / STA $06
  } else {
    unsigned a = entry6Wrap((rd(w, 0x0A04) + rd(w, 0x0A10)) & 0xFFFF); // 7F39 LDA $04 / ADC $10
    wr(w, 0x0A04, a); wr(w, 0x0A08, a - 0x0081);                      // 7F4F STA $04 / CLC / SBC #0080 / STA $08
  }
}

// $7E:82D1 (D = $0A00): page A's r0 / r5 / r7 against their last copies $0A30 / $0A32 / $0A34; from the first that changed,
// the copies are taken and $357B runs: the mode-7 matrix (the picture's), and at 35FC `JSR ($35FD)` -- an operand the hooks
// rewrite: $8042 while the Epoch dives ($7A35), $801F after the turn ($79F8). $8042's logic (still D = $0A00): `$0044` + 1
// (16-bit) and, when it is odd, `INC $0E` -- page A's r7, which the next frame's compare sees changed: the matrix is redrawn
// every frame of the dive. (Its tables, $3634 / $3637, and $801F's are the picture's.)
static void entry6Copies(uint8_t* w, const CtRaceIo* io) {
  if (rd(w, 0x0A00) != rd(w, 0x0A30)) {                               // 82D5 LDA $00 / CMP $30 / BNE -> 82E8 STA $30
    wr(w, 0x0A30, rd(w, 0x0A00)); wr(w, 0x0A32, rd(w, 0x0A0A)); wr(w, 0x0A34, rd(w, 0x0A0E));
  } else if (rd(w, 0x0A0A) != rd(w, 0x0A32)) {                        // 82DB LDA $0A / CMP $32 / BNE -> 82EC
    wr(w, 0x0A32, rd(w, 0x0A0A)); wr(w, 0x0A34, rd(w, 0x0A0E));
  } else if (rd(w, 0x0A0E) != rd(w, 0x0A34)) {                        // 82E1 LDA $0E / CMP $34 / BNE -> 82F0
    wr(w, 0x0A34, rd(w, 0x0A0E));
  } else return;                                                      // 82E7 RTS
  if (io && io->pic) { CtEntry6Pic357B(w, io); return; }              // 82F2 JMP $357B: the build whole (the picture's)
  if (rd(w, 0x35FD) == 0x8042) {                                      // 82F2 JMP $357B -> 35FC JSR ($35FD)
    unsigned n = (rd(w, 0x0044) + 1) & 0xFFFF;                        // 8042 LDA $0044 / INC A / STA $0044
    wr(w, 0x0044, n);
    if (n & 1) wr(w, 0x0A0E, rd(w, 0x0A0E) + 1);                      // 8049 BIT #0001 / BEQ; 8054 INC $0E
  }
}

// $7E:7EE8 / $7ED1 -- the task list replaced: `MVN $7E,$D2` 32 bytes from $D2:0000 into $0920, then 12 ($7EDC) or 10
// ($7EC7) bytes of the overlay's own table over it.
static void entry6Tasks(uint8_t* w, unsigned tbl, unsigned n, const CtRaceIo* io) {
  { const uint8_t* z = io ? CtRomPtr(io->rom, io->romLen, 0x120000, 0x20) : NULL;   // 7EF8-7F01 ($D2:0000: a table)
    if (z) memcpy(w + 0x0920, z, 0x20); }
  memmove(w + 0x0920, w + tbl, n);                                    // 7EEB-7EF4 / 7ED4-7EDA
}

// The scripts' `F0` calls.
static int entry6Call(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  (void)page;
  switch (tgt) {
    case 0x7EE8: entry6Tasks(w, 0x7EDC, 0x0C, io); return 0;
    case 0x7ED1: entry6Tasks(w, 0x7EC7, 0x0A, io); return 0;
    case 0x82F5:                                                      // palette record 0, then one pass of the service
      w[0x0520] = 0x40; wr(w, 0x0521, 0x2020); wr(w, 0x0523, 0x0001); wr(w, 0x0525, 0x0001); wr(w, 0x0527, 0x00FF);
      entry6Objslot(w, io, page); return 0;                           // 82FB-8315, 8319 JSR $37FA
    case 0x831F:                                                      // records 0 and 1, then the service
      w[0x0520] = 0x40; w[0x052C] = 0x40; wr(w, 0x0521, 0x4080); wr(w, 0x052D, 0x3050);
      wr(w, 0x0523, 0x0008); wr(w, 0x052F, 0x0008); wr(w, 0x0525, 0x0001); wr(w, 0x0531, 0x0001);
      wr(w, 0x0527, 0xFF00); wr(w, 0x0533, 0xFF00);                   // 8325-8351
      entry6Objslot(w, io, page); return 0;                           // 8355 JSR $37FA
  }
  return -1;
}

// The vblank hooks that reset page A ($7A35 when the dive starts, $79F8 when it turns), each leaving the list ($39DE); the
// others are the picture's.
static unsigned entry6Hook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) {
  (void)slot;
  if (io && io->pic) {                                                // the picture's hooks, and the PPU half of the two below
    unsigned k = CtEntry6PicHook(io->pic, w, h);
    if (h != 0x7A35 && h != 0x79F8) return k;
  }
  switch (h) {
    case 0x7A35:
      wr(w, 0x35FD, 0x8042); wr(w, 0x0A00, 0x0080);                   // 7A37-7A41 (then the PPU and $36CC: the picture's)
      w[0x038C] = 0x3F;                                               // 7A60
      return 0;                                                       // 7A65 JMP $39DE
    case 0x79F8:
      wr(w, 0x0A00, 0x00FF); wr(w, 0x0A04, 0x0000); wr(w, 0x0A08, 0x0070);   // 79FA-7A09
      wr(w, 0x3634, rd(w, 0x368D)); wr(w, 0x35FD, 0x801F);            // 7A0C-7A17
      w[0x038C] = 0x1F;                                               // 7A2D
      return 0;                                                       // 7A32 JMP $39DE
  }
  return h;
}

static unsigned entry6Task(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtSeqScript(w, 0x0A00, io, entry6Call); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, entry6Call); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x37FA: entry6Objslot(w, io, 0); return t;
    case 0x7F12: entry6DiveA(w, 0); return t;
    case 0x7F35: entry6DiveA(w, 1); return t;
    case 0x8205: entry6Epoch8205(w, io); return t;
    case 0x7FC4: wr(w, 0x0B0C, rd(w, 0x0B38)); return t;              // 7FC4 LDA $0B38 / STA $0B0C
    case 0x81F1: wr(w, 0x0A04, rd(w, 0x0A04) + 3); return t;          // 81F1 LDA $0A04 / ADC #0003 (C clear)
    case 0x81FB: wr(w, 0x0A08, rd(w, 0x0A08) + 0xFFFD); return t;     // 81FB LDA $0A08 / ADC #FFFD
    case 0x82D1: entry6Copies(w, io); return t;
    default:                                                          // 7F7D 7FCB 8079 835B 7BF2: the picture's
      if (io && io->pic) CtEntry6PicTask(io->pic, w, slot, t, io);
      return t;
  }
}

int CtEntry6StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEntry6StageHas(operand)) return -1;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  s_songHold = 0;
  // $7E:78DD -- the setup. Its VRAM packets, the CGRAM and mode-7 tables, $7A68's and C3/09E9's buffers and the PPU are the
  // picture's; what the logic reads, in order:
  if (io && io->pic) CtEntry6PicSetupA(io->pic, w, io);               // 78DD-79A1 (around FD/FFEE: no overlap)
  for (unsigned y = 0; y < 0x90; y += 0x0C) w[0x0520 + y] = 0;        // 7972 JSL $FD:FFEE -> FD/E275
  wr(w, 0x0A20, 0x7C73);                                              // 79A3
  if (CtSeqScript(w, 0x0A00, io, entry6Call) < 0) return -1;          // 79AA JSR $3749 -- page A runs once in the setup
  wr(w, 0x0B20, 0x7CF3);                                              // 79AE
  wr(w, 0x3024, 0x8278);                                              // 79B4 the main thread's call (the picture's tables)
  wr(w, 0x0046, 0x00A8); wr(w, 0x0B0A, 0x00A8);                       // 79BB-79C1
  wr(w, 0x0021, rd(w, 0x0021) + 1);                                   // 79C4 TCD (D = 0) / 79C8 INC $21
  entry6Epoch8205(w, io);                                             // 79CA JSR $8205
  // (79CD JSR $8278: `LDA $21 / BNE -> RTS`; then the PPU)
  if (io && io->pic) { CtEntry6PicSetupB(io->pic); CtRacePicCgram(io->pic, w); }   // 79D0-79F7, then 300D JSR $388B
  // $7E:300D .. 301E: $388B, $31B0, JSL C3/08D0 (one NMI, run here, without C3/09A4), NMI on, STZ $00, STZ $0392
  g_ctRaceOpenNmi = 1;   // (the setup's last NMI: its posts wait for the opening's black frames, seq_race.h)
  CtSeqStageNmiHooks(w, 0, io, entry6Hook, entry6Task);
  g_ctRaceOpenNmi = 0;
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  if (io && io->pic) CtEntry6PicThread(w);                            // 3023: the main loop's first pass ($3024 = $8278)
  return 0;
}

int CtEntry6StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, entry6Hook, entry6Task);
  if (st != 2) s_songHold = CtSeqStageSongTick(w, io, s_songHold);
  if (st != 2 && io && io->pic) CtEntry6PicThread(w);                 // the main loop's pass after the NMI
  return st;
}

void CtEntry6StageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
