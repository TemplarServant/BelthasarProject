// seq_entry3.c -- sequence entry 3 (`FF 23` from the story, `FF 63` in the attract): the night panorama.
//
// The red moon over the lit skyline (disasm/notes/sequence_table.md sec. 7.2 / 14). `$0384` bit 6 picks the scripts in
// the setup $7E:671F. This file is the logic over the WRAM image: the two scripts, the fades, the palette-record service
// ($37FA = FD/E39C), the task chain that waits on record 0 ($6A51 -> $6A74 -> $6A97) and the task that writes page B's
// registers ($69AF). The picture (the mode-2 offsets $73C6 / $6A23, the HDMA, the hooks, the setup's DMAs) is
// seq_entry3pic.c.
#include "seq_entry3.h"
#include "seq_entry3pic.h"   // the picture, when the host draws it (io->pic)
#include "seq_racepic.h"
#include "objslot.h"
#include <string.h>

static unsigned rd(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

int CtEntry3StageHas(uint8_t operand) { return (operand & 0x1F) == 3; }   // `FF 23` (the story) and `FF 63` (the attract)

// The setup's master cycles, $7E:671F -> $7E:300D, taken in place on the oracle under the timing rule: the attract's arm
// (`FF 63`) with `ct.exe --racepiccmp` RACEPICCMP_SPAN=7E671F,7E300D, the story's (`FF 23`) with `--cputrace`
// CPUTRACE_BEAM. Note: `--callrom 7E671F` outside the opening reads a different cost. The two arms sit a few hundred
// cycles either side of a frame boundary, so the story's opens in 17 frames, the attract's in 18 (field.c seqOpeningHold).
long CtEntry3StageSetupCycles(uint8_t operand) { return (operand & 0x40) ? 114098L : 113324L; }

// $7E:37FA -- the palette records' frame (`LDX $4202 / STX $F0 / JSL FD/FFF7 / LDX $F0 / STX $4202`): FD/E39C over the
// twelve records at $0520, whose record 0 the task chain below waits on. Note: $4202 / $4203 are write-only, so the `LDX`
// reads the open bus, the instruction's last byte ($42): `$F0` = $4242, as on the oracle.
static void entry3Objslot(uint8_t* w, const CtRaceIo* io) {
  CtObjSlotIo o; memset(&o, 0, sizeof o);
  o.s = w; o.colourModes = 1; o.rom = io ? io->rom : NULL; o.romLen = io ? io->romLen : 0;
  CtObjSlotService(&o);
}

// $7E:6ACD -- records 0 and 1 armed as one fade (A = its length): +0 = A, +1 = 20 / 50, +2 = 20, +4 = 0, +6 = 0, +7 = 0,
// +8 = FF, in the routine's order.
static void entry3Fade6ACD(uint8_t* w, uint8_t a) {
  w[0x0520] = a; w[0x052C] = a;                                       // 6ACD / 6AD0
  w[0x0521] = 0x20; w[0x052D] = 0x50;                                 // 6AD3-6ADA
  w[0x0522] = 0x20; w[0x052E] = 0x20;                                 // 6ADD-6AE2
  w[0x0524] = 0; w[0x0530] = 0;                                       // 6AE5 / 6AE8
  w[0x0526] = 0; w[0x0532] = 0;                                       // 6AEB-6AF0
  w[0x0527] = 0; w[0x0533] = 0;                                       // 6AF3-6AF8
  w[0x0528] = 0xFF; w[0x0534] = 0xFF;                                 // 6AFB-6B00
}

// $7E:6A51 / 6A74 / 6A97 -- three steps of the moon's light, each waiting for record 0 to finish ($0520 = 0) before it arms
// the next fade and hands its slot to the next step ($39B1); the last also writes TM ($212C = 13, the picture's) and
// leaves the list ($39A7).
static unsigned entry3Chain(uint8_t* w, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x6A51:
      if (w[0x0520]) return t;                                        // 6A53 LDA $0520 / BEQ / RTS
      entry3Fade6ACD(w, 0x50);
      w[0x0523] = w[0x6A5F]; w[0x052F] = w[0x6A5F];                   // 6A5E LDA #imm: 01, the attract's setup patched 06
      w[0x0525] = 1; w[0x0531] = 1;                                   // 6A66-6A6B
      return 0x6A74;                                                  // 6A6E LDX #6A74 / JMP $39B1
    case 0x6A74:
      if (w[0x0520]) return t;                                        // 6A76 LDA $0520 / BEQ / RTS
      entry3Fade6ACD(w, 0x40);
      w[0x0523] = w[0x6A82]; w[0x052F] = w[0x6A82];                   // 6A81 LDA #imm: 04 (the attract's: 08)
      w[0x0525] = 1; w[0x0531] = 1;                                   // 6A89-6A8E
      return 0x6A97;
    case 0x6A97:
      if (w[0x0520]) return t;
      if (io && io->pic) { CtSeqPicWrite(&io->pic->p, 0x2C, 0x13, 0); CtSeqPicWrite(&io->pic->p, 0x2D, 0x00, 0); }   // 6A9F STX $212C
      entry3Fade6ACD(w, 0x40);
      w[0x0523] = w[0x6AAB]; w[0x052F] = w[0x6AAB];                   // 6AAA LDA #imm: 03 (the attract's: 08)
      w[0x0525] = 1; w[0x0531] = 1;                                   // 6AB2-6AB7
      w[0x0527] = 0xFF; w[0x0533] = 0xFF; w[0x0528] = 0; w[0x0534] = 0;   // 6ABA-6AC7
      return 0;                                                       // 6ACA JMP $39A7
  }
  return t;
}

// $7E:69AF -- the water (M = 16): page B's r0 kept to 0-7 and turned into the 32 + 32 offset words at $0043 (+r0 each, then
// -r0), $003D += r0, $003F = r2 >> 2, r2 -1 a frame; then (8-bit) `$C3` counts down and, at 0 with `$C4` left, steps the
// mosaic ($2106, the picture's) and reloads $C3 = 14.
static void entry3Water(uint8_t* w, const CtRaceIo* io) {
  unsigned r0 = rd(w, 0x0B00) & 0x0007;                               // 69AF AND #0007 / STA $0B00
  wr(w, 0x0B00, r0);
  unsigned f0 = ((((r0 ^ 0xFFFF) + 1) & 0xFFFF) << 3) & 0xFFFF;       // 69B8 EOR / INC / ASL x3 / STA $F0
  for (unsigned x = 0; x < 0x20; x += 2) {                            // 69C4-69DA
    f0 = (f0 + r0) & 0xFFFF;
    wr(w, 0x0043 + x, (f0 & 0x03FF) | 0x2000);
  }
  for (unsigned x = 0x20; x < 0x40; x += 2) {                         // 69DC-69F2 (SEC / SBC)
    f0 = (f0 - r0) & 0xFFFF;
    wr(w, 0x0043 + x, (f0 & 0x03FF) | 0x2000);
  }
  wr(w, 0xF0, f0);
  wr(w, 0x003D, rd(w, 0x003D) + r0);                                  // 69F4-69FB
  wr(w, 0x003F, rd(w, 0x0B04) >> 2);                                  // 69FE-6A03
  wr(w, 0x0B04, rd(w, 0x0B04) - 1);                                   // 6A06 DEC $0B04
  if (--w[0xC3] == 0 && w[0xC4]) {                                    // 6A0B DEC $C3 / BNE; LDA $C4 / BEQ
    w[0xC4]--;                                                        // 6A13-6A14
    if (io && io->pic) CtSeqPicWrite(&io->pic->p, 0x06, (uint8_t)((w[0xC4] << 4) | 1), 0);   // 6A16 ASL x3 / SEC / ROL / STA $2106
    w[0xC3] = 0x14;                                                   // 6A1E-6A20
  }
}

static unsigned entry3Task(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtSeqScript(w, 0x0A00, io, NULL); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, NULL); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x37FA: wr(w, 0x00F0, 0x4242); entry3Objslot(w, io); return t;   // (the setup's 682B calls FD/FFF7 itself)
    case 0x69AF: entry3Water(w, io); return t;
    case 0x6A51: case 0x6A74: case 0x6A97: return entry3Chain(w, t, io);
    case 0x424D: case 0x4283: return CtSeqTaskAbort(w, t, io);        // the attract's button (seq_race.c)
    case 0x6844:                                                      // the attract's curtain: C3/01E4, then HDMA channel 0's
      CtSeqCurtain01E4(w, slot, io);                                  //   table by the call's parity (684A LDA $0351 / LSR A)
      if (io && io->pic) io->pic->ch[0].aAdr = (w[0x0351] & 1) ? 0x688A : 0x6891;   // STX $4302
      return t;
    default:                                                          // 73C6, 6A23: the picture's
      if (io && io->pic) CtEntry3PicTask(io->pic, w, t, io);
      return t;
  }
}

static unsigned entry3Hook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) { (void)slot; return CtEntry3PicHook(io->pic, w, h); }

int CtEntry3StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEntry3StageHas(operand)) return -1;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  // $7E:671F -- the setup, both arms of its `BIT #0040` (the story's `FF 23`, the attract's `FF 63`). Its DMAs (the field's
  // BG1 rows into $7E:1680, the tilemaps) and the PPU are the picture's (seq_entry3pic.c); what the logic reads, in order:
  if (io && io->pic) CtEntry3PicSetupA(io->pic, w);                   // 671F-6772
  if (!(operand & 0x40)) { wr(w, 0x0A20, 0x6902); wr(w, 0x0B20, 0x693A); }   // 6777-678E: the story's scripts,
  else { wr(w, 0x0A20, 0x6898); wr(w, 0x0B20, 0x68A1); }              //   6785 the attract's (`BIT #0040`)
  if (CtSeqScript(w, 0x0A00, io, NULL) < 0) return -1;                // 6792 JSR $3749 -- page A runs once in the setup
  wr(w, 0x7841, 0);                                                   // 6795 STZ $7841 -- 16-bit (6775 REP #20): $7842 too
  w[0x3B] = 0;                                                        // 679F INC $3B, then $783C's DMA and STZ $3B
  if (io && io->pic) CtEntry3PicSetupB(io->pic, w, operand);          // 67A1-6805: the DMA, the HDMA, the registers
  w[0x7442] = 0x1C;                                                   // 67A4 STA $7E7442
  w[0x038C] = 0x0E;                                                   // 67B5 LDX #000E / 67DA STA $038C (bit 6 clear)
  if (operand & 0x40) {                                               // 67BB BIT #40: the attract's arm -- the chain's three
    w[0x6A5F] = 0x06; w[0x6A82] = 0x08; w[0x6AAB] = 0x08;             //   operands patched (67C4-67D2), HDMA channel 0 (the
    w[0x038C] = 0x0F;                                                 //   curtain's window) on: 67D6 LDX #000F
  }
  for (unsigned y = 0; y < 0x90; y += 0x0C) w[0x0520 + y] = 0;        // 6808 JSL $FD:FFEE -> FD/E275
  entry3Fade6ACD(w, 0x50);                                            // 680C LDA #50 / JSR $6ACD
  w[0x0527] = 0; w[0x0533] = 0; w[0x0528] = 0; w[0x0534] = 0;         // 6811-681B
  w[0x0525] = 1; w[0x0531] = 1; w[0x0523] = 1; w[0x052F] = 1;         // 681E-6828
  entry3Objslot(w, io);                                               // 682B JSL $FD:FFF7
  w[0x0354] = 0x7E; wr(w, 0x0352, 0xF1C4); w[0x0351] = 0;             // 682F-683A
  w[0x4259] = 0x01;                                                   // 683D STA $7E4259
  // $7E:300D .. 301E: $388B, $31B0, JSL C3/08D0 (one NMI, run here, without C3/09A4), NMI on, STZ $00, STZ $0392
  if (io && io->pic) CtRacePicCgram(io->pic, w);                      // 300D JSR $388B
  g_ctRaceOpenNmi = 1;   // (the setup's last NMI: its posts wait for the opening's black frames, seq_race.h)
  CtSeqStageNmiHooks(w, 0, io, (io && io->pic) ? entry3Hook : NULL, entry3Task);
  g_ctRaceOpenNmi = 0;
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  return 0;
}

int CtEntry3StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, (io && io->pic) ? entry3Hook : NULL, entry3Task);
  if (st != 2 && CtSeqStageSong(w, io) > 0) w[0x0388] = 0;            // (no song change in this entry)
  return st;
}

void CtEntry3StageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
