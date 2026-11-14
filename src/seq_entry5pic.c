// seq_entry5pic.c -- sequence entry 5's picture (its logic is seq_entry5.c): the vortex that takes the party after Magus.
//
// The stage is entered with A = 85 (C0/260E): bit 7 skips the common init's PPU reset, so the room stays on the screen as the
// field left it -- its VRAM, its palette, its layers -- and the setup $7E:7092 changes only what the vortex needs: BG1 becomes a
// second map of the room's own tiles (BG1SC 60, on the SUB screen: TM 16 / TS 01, CGWSEL 02, CGADSUB 42 -- BG2 half-added to
// it), windows 1 and 2 two circles cut by HDMA (channels 0 / 1, the tables $7E:F000 / $7E:F280 built by C3/0077), BG3 waved by
// the line (channels 2 / 3, $7E:F500).
// The warp: the room's tilemap (the setup's copy of $7E:C800 at $0C00) read through a rotation matrix into $1400 by the MAIN
// LOOP -- `$3023 JSR $7441 / LDA $00 / BEQ`, a loop that never waits -- and sent to VRAM $6000 by the hook $783C whenever a pass
// has ended (`$3B`). On the cartridge a pass takes more than a frame of CPU; the port runs one pass a frame (the main loop's
// iteration: CLAUDE.md, the zelda3 model -- the CPU's speed is not the game's).
// The party: four records ($51.. by $16), each four large sprites from the field's own frame lists in bank E4 ($701E), its
// sixteen tiles built into $7E:1C00 when its frame changes ($6E42) and sent by the hook $7868, on an orbit of radius `$5D`
// that the task $72FF draws to $80. disasm/notes/sequence_table.md, "17. ENTRY 5".
#include "seq_entry5pic.h"
#include "romview.h"
#include <stdio.h>
#include <string.h>
#include "seq_wram.h"   // CT_SEQ_W16 / CT_SEQ_W16_PUT: the WRAM word helpers

static unsigned rw(const uint8_t* w, unsigned a) { return CT_SEQ_W16(w, a); }
static void ww(uint8_t* w, unsigned a, unsigned v) { CT_SEQ_W16_PUT(w, a, v); }
static uint8_t romb(const CtRaceIo* io, unsigned long a) { return io ? CtRomByte(io->rom, io->romLen, (long)(a & 0x3FFFFF)) : 0; }
static unsigned e4w(const CtRaceIo* io, unsigned a) { return romb(io, 0x240000UL + (a & 0xFFFF)) | ((unsigned)romb(io, 0x240000UL + ((a + 1) & 0xFFFF)) << 8); }

// M7A = the word `a` (two stores of $211B), M7B's high byte = `b` (two stores of $211C): the product $2134-$2136.
static long mpy(CtSeqStagePic* sp, unsigned a, uint8_t b) {
  CtSeqPicWrite(&sp->p, 0x1B, (uint8_t)a, 0); CtSeqPicWrite(&sp->p, 0x1B, (uint8_t)(a >> 8), 0);
  CtSeqPicWrite(&sp->p, 0x1C, b, 0); CtSeqPicWrite(&sp->p, 0x1C, b, 0);
  return CtSeqPicMpy(sp);
}

// $7E:70DD (8-bit A, 16-bit X): the HDMA channels, before the records ($74F5: the radii use the multiplier).
void CtEntry5PicSetupA(CtSeqStagePic* sp, uint8_t* w) {
  CtSeqPicHdma36CC(sp, w, 0x7354);                                    // 70DD LDY #7354 / JSR $36CC
}
// $7E:70EB-7156 (D = $2100): the room's map to VRAM, the layers.
void CtEntry5PicSetupB(CtSeqStagePic* sp, uint8_t* w) {
  sp->vaddr = 0x6800; sp->vmain = 0x80;                               // 70FB LDX #6800 / STX $2116, 7101 LDA #80 / STA $2115
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x0C00, 0x800);                 // 70EF-7114: channel 7, $7E:0C00 -> the words from $6800
  CtSeqPicWrite(&sp->p, 0x09, 0x70, 0);                               // 7117 BG3SC
  CtSeqPicWrite(&sp->p, 0x07, 0x60, 0);                               // 711B BG1SC
  CtSeqPicWrite(&sp->p, 0x30, 0x02, 0); CtSeqPicWrite(&sp->p, 0x31, 0x42, 0);   // 711F LDX #4202 / STX $30
  CtSeqPicWrite(&sp->p, 0x23, 0x8B, 0); CtSeqPicWrite(&sp->p, 0x24, 0x0C, 0);   // 7124 LDX #0C8B / STX $23
  CtSeqPicWrite(&sp->p, 0x25, 0x00, 0);                               // 7129
  for (uint8_t r = 0x0D; r <= 0x10; r++) { CtSeqPicWrite(&sp->p, r, 0x00, 0); CtSeqPicWrite(&sp->p, r, 0x00, 0); }   // 712D-713B
  CtSeqPicWrite(&sp->p, 0x05, 0x01, 0);                               // 713D LDA #01 / STA $05
  CtSeqPicWrite(&sp->p, 0x2C, 0x16, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x01, 0);   // 714C LDX #0116 / STX $2C
  CtSeqPicWrite(&sp->p, 0x2E, 0x17, 0); CtSeqPicWrite(&sp->p, 0x2F, 0x01, 0);   // 7151 LDX #0117 / STX $2E
}

// $7E:7668-76A0 (after 7662 `JSR $5872`, the logic's): |$F4|^2 + |$F6|^2 by the multiplier (M7A the byte, M7B the same byte --
// signed, as the PPU reads it), then $34FB: `STZ $F0`, and `SBC $F0 / BCC / SBC $F0 / BCC / INC $F0` round and round on the
// carry the sum left -- the root, in `$F0`; its low byte is the radius `$5D,X`.
void CtEntry5PicRadius(CtSeqStagePic* sp, uint8_t* w, unsigned x) {
  uint8_t a = w[0x00F4];                                              // 766A LDA $F4 / BPL / EOR #FF / INC A
  if (a & 0x80) a = (uint8_t)((a ^ 0xFF) + 1);
  unsigned y = (unsigned)mpy(sp, a, a) & 0xFFFF;                      // 7671 STA $211B / STZ $211B / STA $211C x2 / LDY $2134
  a = w[0x00F6];                                                      // 7680
  if (a & 0x80) a = (uint8_t)((a ^ 0xFF) + 1);
  unsigned s = y + ((unsigned)mpy(sp, a, a) & 0xFFFF);                // 7693 REP #21 / TYA / ADC $2134
  int c = s > 0xFFFF; s &= 0xFFFF;
  unsigned f0 = 0;                                                    // 34FB STZ $F0
  for (unsigned guard = 0; guard < 0x10000; guard++) {
    int t = (int)s - (int)f0 - (1 - c); c = t >= 0; s = (unsigned)t & 0xFFFF;   // 34FD SBC $F0 / BCC
    if (!c) break;
    t = (int)s - (int)f0 - (1 - c); c = t >= 0; s = (unsigned)t & 0xFFFF;       // 3501 SBC $F0 / BCC
    if (!c) break;
    f0 = (f0 + 1) & 0xFFFF;                                           // 3505 INC $F0
  }
  ww(w, 0x00F0, f0);
  w[(0x5D + x) & 0xFFFF] = (uint8_t)f0;                               // 769C LDA $F0 / SEP #20 / STA $5D,X
}

// $7E:76D9-771A: the cosine ($3461) and the sine ($3465) of `$A9 + $5B,X` times the radius `$5D,X` -- the product's middle
// byte ($2135) + $80 / + $70, each `ADC` on the carry the table's routine returns (`CPX #0200`: the angle's half).
void CtEntry5PicOrbit(CtSeqStagePic* sp, uint8_t* w, unsigned x, const CtRaceIo* io) {
  unsigned ang = (rw(w, 0x00A9) + rw(w, 0x5B + x)) & 0xFFFF;
  unsigned a = (ang + 0x0100) & 0xFFFF;                               // 76DB JSR $3461 (CLC / ADC #0100, then $3465)
  int c = (a & 0x03FF) >= 0x0200;
  long p = mpy(sp, CtSeqSine(io, a), w[0x5D + x]);                    // 76DF-76ED
  w[0x54 + x] = (uint8_t)((((unsigned long)p >> 8) & 0xFF) + 0x80 + c);              // 76F0 LDA $2135 / ADC #80 / STA $54,X
  c = (ang & 0x03FF) >= 0x0200;                                       // 76F7 REP #21 / LDA $A9 / ADC $5B,X / JSR $3465
  p = mpy(sp, CtSeqSine(io, ang), w[0x5D + x]);                       // 7702-7710
  w[0x55 + x] = (uint8_t)((((unsigned long)p >> 8) & 0xFF) + 0x70 + c);              // 7713 LDA $2135 / ADC #70 / STA $55,X
}

// $74EB (A's low byte x 3) and $74DD (A's low byte x 40): the character's and the frame's index.
static unsigned x3(unsigned a) { a &= 0xFF; return (a + (a << 1)) & 0xFFFF; }
static unsigned x40(unsigned a) { a &= 0xFF; return ((a << 3) + (a << 5)) & 0xFFFF; }

// $7E:773E-774D -> $6E42 (A = the frame x 40, X = the character x 3, Y = 1C00): the frame's sixteen tile words from
// E4:2300[X] + A (the bank E4:2302[X]); each tile's 32 bytes from the character's sheet E4:2000[X] (the bank E4:2002[X]) + the
// word << 5 into $7E:1C00.., straight (an MVN) or, with the word's bit 14, every byte through the bit-reversal table C0:FD00
// (WMDATA). The `ADC`s take the carry the shifts left: bit 11 of the word.
void CtEntry5PicTiles(uint8_t* w, unsigned x, const CtRaceIo* io) {
  unsigned ch = x3(rw(w, 0x52 + x)), fr = x40(w[0x51 + x]);           // 773E LDA $52,X / JSR $74EB, 7744 LDA $51,X / JSR $74DD
  unsigned list = (fr + e4w(io, 0x2300 + ch)) & 0xFFFF;               // 6E42 CLC / ADC $E42300,X / STA $1B
  unsigned long lbank = (unsigned long)romb(io, 0x240000UL + 0x2302 + ch) << 16;   // 6E57 LDA $E42302,X / STA $1D
  unsigned base = e4w(io, 0x2000 + ch);                               // 6E49 LDA $E42000,X -> the two `ADC #imm`
  unsigned long sbank = (unsigned long)romb(io, 0x240000UL + 0x2002 + ch) << 16;   // 6E5D LDA $E42002,X -> the MVN's / the PEA's bank
  ww(w, 0x6EAC, base); ww(w, 0x6EBA, base);                           //   STA $7E6EAC / STA $7E6EBA
  w[0x6EB4] = (uint8_t)(sbank >> 16); w[0x6EC3] = (uint8_t)(sbank >> 16);   //   STA $7E6EB4 / STA $7E6EC3
  unsigned y = 0x1C00;
  for (int k = 0; k < 16; k++) {                                      // 6E71: sixteen `JSR $6E9E`
    unsigned v = romb(io, lbank | list) | ((unsigned)romb(io, lbank | ((list + 1) & 0xFFFF)) << 8);   // 6E9E LDA [$1B]
    list = (list + 2) & 0xFFFF;                                       // 6EA0 INC $1B / INC $1B
    int flip = (v >> 14) & 1;                                         // 6EA4 ASL A / ASL A / BCS
    unsigned c = (v >> 11) & 1;                                       //   ... ASL A x3: the carry into the ADC
    unsigned src = (((v << 5) & 0xFFFF) + base + c) & 0xFFFF;         // 6EAB / 6EB9 ADC #imm
    if (flip) ww(w, 0x00F0, y);                                       // 6EBF STY $F0
    for (unsigned i = 0; i < 0x20; i++) {
      uint8_t b = romb(io, sbank | ((src + i) & 0xFFFF));
      if (flip) b = romb(io, 0x00FD00UL + b);                         // 6ECD LDA $00xx,Y / TAX / LDA $C0FD00,X / STA $80
      w[(y + i) & 0xFFFF] = b;                                        // 6EAF LDA #001F / MVN $00,<bank>
    }
    y = (y + 0x20) & 0xFFFF;
  }
  ww(w, 0x001B, list); w[0x001D] = (uint8_t)(lbank >> 16);
}

// $7E:7787 -> $701E (A = the shown frame `$5A,X` x 40, X = the character x 3): four large sprites at the OAM cursor `$11` --
// the words `$58,X`, + 2, + 4, + 6 (the operand of 7034 `LDA #imm`), each at (`$54,X` + dx, `$55,X` + dy), the offsets the four
// byte pairs after the frame's tile words (E4:2300[X] + A + $20); the high table through `($13)`: x's ninth bit (the 16-bit
// `ASL`'s carry) and the size bit.
void CtEntry5PicOam(uint8_t* w, unsigned x, const CtRaceIo* io) {
  unsigned word = rw(w, 0x58 + x);                                    // 7788 LDA $58,X / STA $7E7035
  ww(w, 0x7035, word);
  ww(w, 0x0015, w[0x54 + x]); ww(w, 0x0017, w[0x55 + x]);             // 778E-779A
  unsigned fr = x40(w[0x5A + x]), ch = x3(rw(w, 0x52 + x));           // 779C LDA $5A,X / JSR $74DD, 77A2 LDA $52,X / JSR $74EB
  unsigned list = (fr + e4w(io, 0x2300 + ch) + 0x20) & 0xFFFF;        // 701E CLC / ADC $E42300,X / CLC / ADC #0020 / STA $1B
  unsigned long lbank = (unsigned long)romb(io, 0x240000UL + 0x2302 + ch) << 16;   // 7029 LDA $E42302,X / STA $1D
  ww(w, 0x001B, list); ww(w, 0x001D, romb(io, 0x240000UL + 0x2302 + ch) | ((unsigned)romb(io, 0x240000UL + 0x2303 + ch) << 8));
  unsigned o = rw(w, 0x0011);                                         // 702F LDX $11
  for (unsigned k = 0; k < 4; k++) ww(w, 0x0702 + ((o + k * 4) & 0xFFFF), (word + k * 2) & 0xFFFF);   // 7034-7046
  unsigned yi = 0;
  w[0x00F0] = 4;                                                      // 704B LDA #04 / STA $F0
  do {
    unsigned a = (romb(io, lbank | ((list + yi) & 0xFFFF)) + rw(w, 0x0015)) & 0xFFFF; yi++;   // 704F LDA [$1B],Y / AND #00FF / ADC $15
    ww(w, 0x0700 + (o & 0xFFFF), a);                                  // 7064 STA $0700,X (705A-7061: both branches store A)
    int c = (a >> 15) & 1;                                            // 7067 ASL A
    unsigned hp = rw(w, 0x0013);
    uint8_t h = w[hp];                                                // 706A LDA ($13) / ROR A / SEC / ROR A / STA ($13)
    h = (uint8_t)((h >> 1) | (c << 7));
    h = (uint8_t)((h >> 1) | 0x80);
    w[hp] = h;
    w[0x0701 + (o & 0xFFFF)] = (uint8_t)(romb(io, lbank | ((list + yi) & 0xFFFF)) + w[0x0017]); yi++;   // 7071-7077
    o = (o + 4) & 0xFFFF;                                             // 707A INX x4
    if (!(o & 0x0F)) ww(w, 0x0013, hp + 1);                           // 707E TXA / AND #0F / BNE / INC $13
  } while (--w[0x00F0]);                                              // 7089 DEC $F0 / BNE
  ww(w, 0x0011, o);                                                   // 708F STX $11
}

// $7E:738C / $73A6 `STA $2106`.
void CtEntry5PicMosaic(CtSeqStagePic* sp, uint8_t v) { CtSeqPicWrite(&sp->p, 0x06, v, 0); }

// $7E:7325 (16-bit A, as the task list enters every task: $398A `REP #20`) -- the windows: page B's r6 (`$0B0C`) is window
// 1's radius and its r0 (`$0B00`) window 2's; a new one redraws its table ($7E:F000 / $7E:F280) by C3/0077, mode 0 (`TDC`),
// the centre the setup's (`$0350` / `$0352` = 80 / 70).
static void task7325(CtSeqStagePic* sp, uint8_t* w) {
  if (rw(w, 0x0B0C) != rw(w, 0x003D)) {                               // 7325 LDA $0B0C / CMP $3D / BEQ
    ww(w, 0x003D, rw(w, 0x0B0C)); ww(w, 0x0354, rw(w, 0x0B0C)); ww(w, 0x0356, 0xF000);
    CtSeqPicCircle0077(sp, w, 0x00);                                  // 7337 TDC / JSL C3/0077
  }
  if (rw(w, 0x0B00) != rw(w, 0x003F)) {                               // 733C LDA $0B00 / CMP $3F / BEQ
    ww(w, 0x003F, rw(w, 0x0B00)); ww(w, 0x0354, rw(w, 0x0B00)); ww(w, 0x0356, 0xF280);
    CtSeqPicCircle0077(sp, w, 0x00);
  }
}

// $7E:73C6 (16-bit A; the task list enters it with the carry clear) -- the warp's matrix: |cos| and |sin| of page A's r5
// (`$0A0A`) times its r0's low byte >> 2 (the multiplier), >> 3, the sign put back: `$1F` = `$25` = the cosine's, `$21` the
// sine's, `$23` = -`$21`; the centre `$27` = `$2B` = r1 (`$0A02`), `$29` = `$2D` = r2 (`$0A04`).
static void task73C6(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  unsigned ang = rw(w, 0x0A0A), v[2];
  for (int k = 0; k < 2; k++) {
    unsigned s = CtSeqSine(io, k ? ang : ang + 0x0100);               // 73C9 JSR $3461 / 73FD JSR $3465
    int neg = (s & 0x8000) != 0;                                      // PHP / BPL
    if (neg) s = ((s ^ 0xFFFF) + 1) & 0xFFFF;
    unsigned p = ((unsigned)mpy(sp, s, (uint8_t)(w[0x0A00] >> 2)) & 0xFFFF) >> 3;   // 73D3-73EE
    if (neg) p = ((p ^ 0xFFFF) + 1) & 0xFFFF;                         // PLP / BPL / EOR #FFFF / INC A
    v[k] = p;
  }
  ww(w, 0x001F, v[0]); ww(w, 0x0025, v[0]);                           // 73F6
  ww(w, 0x0021, v[1]); ww(w, 0x0023, ((v[1] ^ 0xFFFF) + 1) & 0xFFFF); // 742A-7430
  ww(w, 0x002B, rw(w, 0x0A02)); ww(w, 0x0027, rw(w, 0x0A02));         // 7432
  ww(w, 0x002D, rw(w, 0x0A04)); ww(w, 0x0029, rw(w, 0x0A04));         // 7439
}

// $7E:73AA (16-bit; the carry clear) -- BG3's wave: the table $7E:F500 moves up a line (the MVN: $1BF bytes from $F502), the
// phase `$4F` += page B's r8 (`$0B10`), and the bottom line takes its sine >> 2.
static void task73AA(uint8_t* w, const CtRaceIo* io) {
  memmove(w + 0xF500, w + 0xF502, 0x1BF);                             // 73AA LDA #01BE / LDX #F502 / LDY #F500 / MVN $7E,$7E
  unsigned ph = (rw(w, 0x0B10) + rw(w, 0x004F)) & 0xFFFF;             // 73B6 LDA $0B10 / ADC $4F / STA $4F
  ww(w, 0x004F, ph);
  ww(w, 0xF6BE, CtSeqSine(io, ph) >> 2);                              // 73BD JSR $3465 / LSR A / LSR A / STA $F6BE
}

// $7E:72FF (`SEP #20`) -- the four radii `$5D,X` (X = 0, 16, 2C, 42): `CMP #80 / BEQ` (at $80 it stays) `/ BMI -> DEC A`,
// else `INC A` -- below $80 a step down (the party is drawn in to the centre), above it a step up.
static void task72FF(uint8_t* w) {
  for (unsigned x = 0; x != 0x58; x += 0x16) {
    uint8_t a = w[0x5D + x];
    if (a == 0x80) continue;                                          // 7318 CMP #80 / BEQ
    w[0x5D + x] = (uint8_t)(((uint8_t)(a - 0x80) & 0x80) ? a - 1 : a + 1);   // 731C BMI -> 7321 DEC A, else 731E INC A
  }
}

int CtEntry5PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x7325: task7325(sp, w); return 1;
    case 0x73C6: task73C6(sp, w, io); return 1;
    case 0x73AA: task73AA(w, io); return 1;
    case 0x72FF: task72FF(w); return 1;
    default: return 0;
  }
}

unsigned CtEntry5PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  switch (hook) {
    case 0x783C:                                                      // a pass of the warp has ended: its map to VRAM $6000
      if (w[0x003B]) {                                                // 783C LDA $3B / BEQ
        sp->vaddr = 0x6000; sp->vmain = 0x80;
        CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x1400, 0x800);
        w[0x003B] = 0;                                                // 7865 STZ $3B
      }
      return hook;
    case 0x7868: {                                                    // a record's new tiles: two rows of eight
      unsigned v = rw(w, 0x0041);                                     // 7868 LDX $41 / CPX #FFFF / BEQ
      if (v != 0xFFFF) {
        sp->vmain = 0x80;
        sp->vaddr = (uint16_t)v; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x1C00, 0x100);
        sp->vaddr = (uint16_t)(v + 0x0100); CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x1D00, 0x100);   // 788F: the DMA goes on from $1D00
      }
      return hook;
    }
    case 0x78AB:                                                      // every eighth frame: colours $15-$17 sent, then turned
      if (!(w[0x0008] & 0x07)) {                                      // 78AB LDA $08 / AND #07 / BNE
        sp->cgadd = 0x15;                                             // 78B1 LDA #15 / STA $2121
        CtSeqStagePicDma(sp, w, 0x00, 0x22, 0x222A, 6);               // 78B6-78C4: six bytes to $2122
        unsigned x = rw(w, 0x222A);                                   // 78CA LDX $222A / ... / STX $222E
        ww(w, 0x222A, rw(w, 0x222C)); ww(w, 0x222C, rw(w, 0x222E)); ww(w, 0x222E, x);
      }
      return hook;
    default: {
      static unsigned said = 0;
      if (said != hook) { said = hook; fprintf(stderr, "entry5pic: UNDERIVED vblank hook $%04X\n", hook); }
      return hook;
    }
  }
}

// The V-IRQ. C3:003B's `$4200 = A1` (A's bit 7) leaves the field's V-IRQ armed -- line 211 -- and the stage's vector ($0504:
// `JML $C3:0529`) acks it, waits for the H-blank and stores INIDISP = 80: every frame is blank from line 212 down (the
// picture's rows 211-223) until the NMI's own store. `rgb` = the frame CtSeqStagePicFrame has just drawn (256 x 224).
void CtEntry5PicIrq(CtSeqStagePic* sp, uint8_t* rgb) {
  if (rgb) memset(rgb + 211 * 256 * 3, 0, 13 * 256 * 3);
  CtSeqPicWrite(&sp->p, 0x00, 0x80, 211);                             // C3/0540 LDA #80 / STA $002100
}

// $7E:7441 (16-bit A, D = 0) -- one pass: 27 rows of 32 map words. The steps are the code's own operands (the `ADC #imm` at
// 749E / 74A7 / 74B0 / 74BB = `$25`, `$21` x 2, `$23`, `$1F` x 2); the origin `$2F` / `$31` from the centre `$27` / `$29`.
// A word: `$0C00 + ((hi($37) + hi($39) x $40 + C) & 07FE)` -- the multiply $4202 x $4203 --; no `CLC` in either loop, so every
// `ADC` takes the carry of the one before it (the first row's from the last `ASL` of the origin).
void CtEntry5PicRender(uint8_t* w) {
  unsigned s1F = rw(w, 0x1F), s21 = rw(w, 0x21), s23 = rw(w, 0x23), s25 = rw(w, 0x25);
  unsigned i74BB = (s1F << 1) & 0xFFFF, i74A7 = (s21 << 1) & 0xFFFF, i749E = s25, i74B0 = s23;
  ww(w, 0x0035, 0x001B);                                              // 7441
  ww(w, 0x74BB, i74BB); ww(w, 0x74A7, i74A7); ww(w, 0x749E, i749E); ww(w, 0x74B0, i74B0);   // 7446-745C
  unsigned a, c;
  a = (rw(w, 0x27) << 4) & 0xFFFF;                                    // 7460 LDA $27 / ASL x4
  a = a + s1F + 1; a &= 0xFFFF;                                       // 7466 SEC / ADC $1F
  a = (a - s23) & 0xFFFF;                                             // 7469 SEC / SBC $23
  a = (a << 5) & 0xFFFF;                                              // 746C ASL x5
  unsigned r31 = ((a ^ 0xFFFF) + 1) & 0xFFFF;                         // 7471 EOR #FFFF / INC A / STA $31
  a = (rw(w, 0x29) << 4) & 0xFFFF;                                    // 7477 LDA $29 / ASL x4
  a = (a + s23) & 0xFFFF;                                             // 747D CLC / ADC $23
  a = (a + s1F + 1) & 0xFFFF;                                         // 7480 SEC / ADC $1F
  c = (a >> 12) & 1;                                                  // 7483 ASL x4: the last shift's carry
  a = (a << 4) & 0xFFFF;
  unsigned r2F = ((a ^ 0xFFFF) + 1) & 0xFFFF;                         // 7487 EOR #FFFF / INC A / STA $2F
  //                                                                     748D LDA #0040 / STA $4202: the multiplicand
  unsigned y = 0x1400, r37 = 0, r39 = 0, t;
  for (unsigned row = 0; row < 0x1B; row++) {                         // 7496 LDA #0020 / STA $33 ... 74D6 DEC $35 / BNE
    t = r2F + i749E + c; c = t > 0xFFFF; r2F = t & 0xFFFF; r39 = r2F;           // 749B LDA $2F / ADC #imm / STA $2F / STA $39
    t = r31 + i74A7 + c; c = t > 0xFFFF; r31 = t & 0xFFFF; r37 = r31;           // 74A4 LDA $31 / ADC #imm / STA $31 / STA $37
    for (unsigned col = 0; col < 0x20; col++) {                       // 74AD ... 74D2 DEC $33 / BNE
      t = r39 + i74B0 + c; c = t > 0xFFFF; r39 = t & 0xFFFF;          // 74AD LDA $39 / ADC #imm / STA $39
      unsigned prod = 0x40 * (r39 >> 8);                              // 74B4 XBA / STA $4203: $4216 = $40 x hi($39)
      t = r37 + i74BB + c; c = t > 0xFFFF; r37 = t & 0xFFFF;          // 74B8 LDA $37 / ADC #imm / STA $37
      t = (r37 >> 8) + prod + c; c = t > 0xFFFF;                      // 74BF XBA / AND #00FF / ADC $4216
      unsigned x = t & 0x07FE;                                        // 74C6 AND #07FE / TAX
      ww(w, y, rw(w, 0x0C00 + x));                                    // 74CA LDA $0C00,X / STA $0000,Y
      y = (y + 2) & 0xFFFF;                                           // 74D0 INY / INY
    }
  }
  ww(w, 0x002F, r2F); ww(w, 0x0031, r31); ww(w, 0x0037, r37); ww(w, 0x0039, r39);
  ww(w, 0x0033, 0); ww(w, 0x0035, 0);
  ww(w, 0x003B, rw(w, 0x003B) + 1);                                   // 74DA INC $3B
}
