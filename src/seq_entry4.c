// seq_entry4.c -- sequence entry 4 (`FF 24`): Lavos falls through space onto the planet (65,000,000 BC).
//
// Scene 301, after the Black Tyranno (disasm/notes/sequence_table.md sec. 15: the scripts $552A / $5568 and their tasks).
// This file is the logic over the WRAM image: the two scripts and their calls, the fades, the tasks that write the pages'
// registers ($56A9, $5855, $55F9), and the starfield ($5777 in the setup, task $579B every frame), which draws from the
// overlay's RNG C3/02DD: what it leaves in `$0386` is the field's afterwards. The picture (the mode-7 registers $5654,
// the planet's sprite $56EB, the setup's VRAM packets and tables) is seq_entry4pic.c.
#include "seq_entry4.h"
#include "seq_entry4pic.h"   // the picture, when the host draws it (io->pic)
#include "seq_racepic.h"
#include <string.h>

static unsigned rd(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

int CtEntry4StageHas(uint8_t operand) { return (operand & 0x1F) == 4 && !(operand & 0x40); }

// The setup's cycles, from `CT_CALLROM_NONMI=1 ct.exe --callrom 7E53BB --pokewram 0384=24 --regdb 00 --m8 --x16` on a
// state with the overlay in WRAM: the VRAM packets, the mode-7 tile conversion $37CA and the 128 stars of $5777.
long CtEntry4StageSetupCycles(void) { return 1938002L; }

// $7E:3443 -> C3/02DD, the overlay's RNG (M = 8): `ROL A / ADC $0386 / STA $0386 / TXA / ADC $0386 / ADC $0008 / CMP $2137 /
// ADC $213C / SBC $213D / ADC $0386 / ADC $00F0 / EOR $00F2 / ADC $00F4 / SBC $213D / STA $0386`, the carry threaded through.
// The beam counters are the timing rule's virtual beam on both sides (CLAUDE.md; snes/snes.c snes_readBBus): $213C = $0351, $213D =
// 00. `CMP $2137` latches the counters and reads the open bus -- the operand's last byte, 21 -- so it only sets the carry.
uint8_t CtSeqRng02DD8(uint8_t* w, uint8_t a, int* c, unsigned x) {
  unsigned t;
  int nc = a >> 7; a = (uint8_t)((a << 1) | *c); *c = nc;             // 02DD ROL A
  t = a + w[0x0386] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02DE ADC $0386
  w[0x0386] = a;                                                      // 02E1 STA $0386
  a = (uint8_t)x;                                                     // 02E4 TXA
  t = a + w[0x0386] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02E5 ADC $0386
  t = a + w[0x0008] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02E8 ADC $0008
  *c = a >= 0x21;                                                     // 02EB CMP $2137
  t = a + w[0x0351] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02EE ADC $213C
  if (!*c) { *c = a != 0; a--; }                                      // 02F1 SBC $213D (00)
  t = a + w[0x0386] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02F4 ADC $0386
  t = a + w[0x00F0] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02F7 ADC $00F0
  a ^= w[0x00F2];                                                     // 02FA EOR $00F2
  t = a + w[0x00F4] + *c; a = (uint8_t)t; *c = t > 0xFF;              // 02FD ADC $00F4
  if (!*c) { *c = a != 0; a--; }                                      // 0300 SBC $213D (00)
  w[0x0386] = a;                                                      // 0303 STA $0386
  return a;
}

// $7E:5777 -- the setup's 128 stars (X = 0..1FC by 4): $0C00,X / $0C01,X = two draws (the star's x / y), $0E00,X = a third
// & 7F (its depth), then `STX $00F2` -- which the next star's draws EOR in. A and the carry run on from draw to draw;
// `CPX #0200` clears the carry between stars. The first draw's A and carry are what page A's first visit left (`a`, `c`).
void CtSeqStars5777(uint8_t* w, uint8_t a, int c) {
  for (unsigned x = 0; x < 0x200; x += 4) {
    a = CtSeqRng02DD8(w, a, &c, x); w[0x0C00 + x] = a;                      // 577A JSR $3443 / STA $0C00,X
    a = CtSeqRng02DD8(w, a, &c, x); w[0x0C01 + x] = a;                      // 5780 JSR $3443 / STA $0C01,X
    a = CtSeqRng02DD8(w, a, &c, x); a &= 0x7F; w[0x0E00 + x] = a;           // 5786 JSR $3443 / AND #7F / STA $0E00,X
    wr(w, 0x00F2, x);                                                 // 578E STX $00F2
    c = 0;                                                            // 5795 CPX #0200 (X < 200)
  }
}

// The hardware divider as LakeSnes has it ($4204 / $4206 -> $4214): a divisor of 0 gives FFFF.
static unsigned div16(unsigned n, unsigned d) { return d ? (n & 0xFFFF) / d : 0xFFFF; }

// One axis of a star on the screen (57DB-5845): the byte `v` (signed) * 256 / the depth, its low byte halved, the sign
// put back, then `ADC #centre` with the carry the PHP before the divide saved (`c`). Returns the coordinate; `*c` its carry.
static uint8_t starAxis(uint8_t v, uint8_t depth, unsigned centre, int* c) {
  unsigned n = (unsigned)v << 8;                                      // REP / XBA / AND #FF00
  if (n & 0x8000) n = ((n ^ 0xFFFF) + 1) & 0xFFFF;                    // BPL / EOR #FFFF / INC A
  uint8_t q = (uint8_t)(div16(n, depth) & 0xFF) >> 1;                 // STA $4204 / $44 -> $4206 / LDA $4214 / LSR A
  if (v & 0x80) q = (uint8_t)((q ^ 0xFF) + 1);                        // PLP / BPL / EOR #FF / INC A
  unsigned t = q + centre + *c;                                       // ADC #80 / ADC #70
  *c = t > 0xFF;
  return (uint8_t)t;
}

// $7E:579B -- the stars' frame (M = 8, Y = $11 .. 1FC by 4): each star's depth $0E00,Y moves by page B's r0 ($0B00, signed);
// one that leaves the field (past 7F forwards, below 0 backwards) is drawn again at a new place (two draws, X = the task's
// slot); its x / y ($0C00 / $0C01) move by page B's r1 / r2 ($0B02 / $0B04), and its sprite is written to the OAM shadow:
// x = $0C00 * 256 / depth + 80, y = $0C01 * 256 / depth + 70, the tile 60 + depth / 8, the attributes 32 for the far tiles
// (< 68) -- 02 once the call $5640 has made the `BMI` at $582E a `BRA` (`$582E = 0080`). The carry runs through the whole loop
// as the ROM threads it; X and the carry are the caller's (entry 4's task: its slot, clear -- the dispatcher's `CPX #0020`).
void CtSeqStars579B(uint8_t* w, unsigned slot, int c) {
  unsigned y = rd(w, 0x0011);                                         // 579D LDY $11
  for (unsigned guard = 0; guard < 0x4000; guard++) {
    uint8_t a = w[0x0B00];                                            // 579F LDA $0B00
    unsigned t = a + w[0x0E00 + y] + c;                               // 57A4 / 57AD ADC $0E00,Y
    int respawn;
    if (!(a & 0x80)) {                                                // 57A2 BMI
      a = (uint8_t)t;
      c = a >= 0x7F; respawn = !(((a - 0x7F) & 0x80));                // 57A7 CMP #7F / BMI -> 57C0
    } else {
      a = (uint8_t)t; c = t > 0xFF; respawn = (a & 0x80) != 0;        // 57B0 BPL -> 57C0
    }
    if (respawn) {                                                    // 57B2 PHA
      uint8_t r = CtSeqRng02DD8(w, a, &c, slot); w[0x0C00 + y] = r;         // 57B3 JSR $3443 / STA $0C00,Y
      r = CtSeqRng02DD8(w, r, &c, slot); w[0x0C01 + y] = r;                 // 57B9 JSR $3443 / STA $0C01,Y
    }                                                                 // 57BF PLA
    a &= 0x7F; w[0x0E00 + y] = a;                                     // 57C0 AND #7F / STA $0E00,Y
    t = a + 0x80 + c; uint8_t depth = (uint8_t)t; c = t > 0xFF;       // 57C5 ADC #80
    w[0x0044] = depth;                                                // 57C7 STA $44
    t = w[0x0B02] + w[0x0C00 + y] + c; w[0x0C00 + y] = (uint8_t)t; c = t > 0xFF;   // 57C9 LDA $0B02 / ADC $0C00,Y
    t = w[0x0B04] + w[0x0C01 + y] + c; w[0x0C01 + y] = (uint8_t)t; c = t > 0xFF;   // 57D2 LDA $0B04 / ADC $0C01,Y
    w[0x0702 + y] = (uint8_t)(0x60 + ((depth >> 3) & 0x0F));          // 57F0 LDA $44 / LSR x3 / AND #0F / ADC #60
    w[0x0700 + y] = starAxis(w[0x0C00 + y], depth, 0x80, &c);         // 57DB .. 580C
    int c2 = c;                                                       // 5812 PHP (the carry of 580A's ADC)
    int far = w[0x582E] == 0x30 && ((w[0x0702 + y] - 0x68) & 0x80);   // 5829 CMP #68 / 582E BMI (or BRA)
    w[0x0703 + y] = far ? 0x32 : 0x02;                                // 5830 / 5834 / 5836 STA $0703,Y
    w[0x0701 + y] = starAxis(w[0x0C01 + y], depth, 0x70, &c2);        // 5839 .. 5845
    c = c2;
    y = (y + 4) & 0xFFFF;                                             // 5848 INY x4
    c = y >= 0x0200;                                                  // 584C CPY #0200 / BEQ -> RTS
    if (y == 0x0200) break;
  }
}

// $7E:56D1 (M = 16, D = $0A00): |v| * page A's r0 through $7E:31BF, the high word's middle ($F5) with v's sign put back.
static unsigned spin56D1(uint8_t* w, unsigned v) {
  const unsigned d = 0x0A00;
  int neg = (v & 0x8000) != 0;                                        // 56D1 PHP / BPL
  if (neg) v = ((v ^ 0xFFFF) + 1) & 0xFFFF;                           // 56D4 EOR #FFFF / INC A
  wr(w, d + 0xF0, v);                                                 // 56D8 STA $F0
  wr(w, d + 0xF2, rd(w, d + 0x00));                                   // 56DA LDA $00 / STA $F2
  CtSeqMul31BF(w, d);                                                 // 56DE JSR $31BF
  unsigned a = rd(w, d + 0xF5);                                       // 56E1 LDA $F5
  return neg ? ((a ^ 0xFFFF) + 1) & 0xFFFF : a;                       // 56E3 PLP / BPL / EOR #FFFF / INC A
}

// $7E:56B4 (D = $0A00) -- the mode-7 matrix from page A's r5 scaled by r0: r10 = r13 = cos r5 * r0, r11 = sin r5 * r0, r12 =
// -r11 (the hook $5654 writes them).
void CtSeqMatrix56B4(uint8_t* w, const CtRaceIo* io) {
  const unsigned d = 0x0A00;
  unsigned a = spin56D1(w, CtSeqSine(io, rd(w, d + 0x0A) + 0x0100));  // 56B4 LDA $0A / JSR $3461 / JSR $56D1
  wr(w, d + 0x14, a); wr(w, d + 0x1A, a);                             // 56BC STA $14 / STA $1A
  a = spin56D1(w, CtSeqSine(io, rd(w, d + 0x0A)));                    // 56C0 LDA $0A / JSR $3465 / JSR $56D1
  wr(w, d + 0x16, a);                                                 // 56C8 STA $16
  wr(w, d + 0x18, ((a ^ 0xFFFF) + 1) & 0xFFFF);                       // 56CA EOR #FFFF / INC A / STA $18
}
// $7E:56A9 -- the planet's turn (D = $0A00): r5 += 16 (`ADC #0016`, the carry clear: the dispatcher's `CPX #0020`), then the
// matrix ($56B4).
static void entry4Spin(uint8_t* w, const CtRaceIo* io) {
  wr(w, 0x0A0A, rd(w, 0x0A0A) + 0x0016);                              // 56AD LDA $0A / ADC #0016 / STA $0A
  CtSeqMatrix56B4(w, io);
}

// $7E:5855 -- the stars' drift (D = $0B00): r1 = sin r5 >> 5, r0 = cos r5 >> 5 (logical shifts of the 16-bit value).
static void entry4Orbit(uint8_t* w, const CtRaceIo* io) {
  wr(w, 0x0B02, CtSeqSine(io, rd(w, 0x0B0A)) >> 5);                   // 5859 LDA $0A / JSR $3465 / LSR x5 / STA $02
  wr(w, 0x0B00, CtSeqSine(io, rd(w, 0x0B0A) + 0x0100) >> 5);          // 5865 LDA $0A / JSR $3461 / LSR x5 / STA $00
}

// The scripts' `F0` calls: $5640 (`LDA #0080 / STA $582E / LDA #5604 / STA $0A20`: the stars' far tiles turn plain, page A
// to its last part) and $564D (`LDA #5635 / STA $0A20`).
static int entry4Call(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  (void)page; (void)io;
  switch (tgt) {
    case 0x5640: wr(w, 0x582E, 0x0080); wr(w, 0x0A20, 0x5604); return 0;
    case 0x564D: wr(w, 0x0A20, 0x5635); return 0;
  }
  return -1;
}

// The vblank hooks: $380B ends the OAM shadow's last high-table byte ($3870: the bits of the sprites past $11's last group of
// four shifted in as `SEC / ROR / LSR`, into ($13)), DMAs it, and resets the cursors `$11 = 0`, `$13 = 0900` -- the stars'
// loop starts from $11. $5654 writes the mode-7 registers from page A (the picture's).
static unsigned entry4Hook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) {
  (void)slot;
  if (io && io->pic) return CtEntry4PicHook(io->pic, w, h);           // the picture's (its $380B does the cursors too)
  if (h == 0x380B) {
    unsigned x = (w[0x0011] >> 2) & 3, p = rd(w, 0x0013);             // 3874 LDA $11 / LSR / LSR / AND #03 / TAX
    uint8_t a = w[p & 0xFFFF];                                        // 387B LDA ($13)
    for (; x != 4; x++) a = (uint8_t)((((a >> 1) | 0x80)) >> 1);      // 387D CPX #4 / INX / SEC / ROR / LSR
    w[p & 0xFFFF] = a;                                                // 3888 STA ($13)
    wr(w, 0x0011, 0); wr(w, 0x0013, 0x0900);                          // 3838-3840
  }
  return h;
}

static unsigned entry4Task(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtSeqScript(w, 0x0A00, io, entry4Call); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, entry4Call); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x579B: CtSeqStars579B(w, slot, 0); return t;
    case 0x56A9: entry4Spin(w, io); return t;
    case 0x5855: entry4Orbit(w, io); return t;
    case 0x55F9: wr(w, 0x0B0A, rd(w, 0x0B0A) + 2); return t;          // 55F9 LDA $0B0A / CLC / ADC #0002 / STA $0B0A
    default:                                                          // 56EB: the planet's sprite (C3/0D5E), the picture's
      if (io && io->pic) CtEntry4PicTask(io->pic, w, t);
      return t;
  }
}

int CtEntry4StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEntry4StageHas(operand)) return -1;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  // $7E:53BB -- the setup. Its VRAM packets ($3691 / C3/0557), the tile conversion ($37CA, bank 7F), the tables ($36AA), the
  // DMAs and the PPU are the picture's (seq_entry4pic.c, when the host draws); what the logic reads, in order. The scratch
  // $00F0-$00F4 the stars' draws fold in:
  if (io && io->pic) CtEntry4PicSetupA(io->pic, w, io);               // 53BB-54B0
  w[0x00F0] = 0x40; w[0x00F1] = 0; w[0x00F2] = 0x18;                  // 53EF .. 548B (STZ $F0 / $F2 = 19 / $F0 = 60 / 1 /
                                                                      //  $F2 = 18 / $37CA's count to 0 / $F0 = 20 / ASL $F0)
  wr(w, 0x0A20, 0x552A); wr(w, 0x0B20, 0x5568);                       // 54A1-54AA
  if (CtSeqScript(w, 0x0A00, io, entry4Call) < 0) return -1;          // 54B4 JSR $3749 -- page A runs once in the setup
  // 54BE JSR $5777 with what the visit left: A = the IP its yield stored (C3/0CB3 `LDA ($20) / STA $20`), the carry clear
  // (C3/0C97's `ASL` of the loop's register number, on the loop's first visit)
  CtSeqStars5777(w, w[0x0A20], 0);
  wr(w, 0x038E, 0x0606);                                              // 54CB LDX #0606 / STX $038E
  if (io && io->pic) { CtEntry4PicSetupB(io->pic); CtRacePicCgram(io->pic, w); }   // 54C1-54E9, then 300D JSR $388B
  // $7E:300D .. 301E: $388B, $31B0, JSL C3/08D0 (one NMI, run here, without C3/09A4), NMI on, STZ $00, STZ $0392
  g_ctRaceOpenNmi = 1;   // (the setup's last NMI: its posts wait for the opening's black frames, seq_race.h)
  CtSeqStageNmiHooks(w, 0, io, entry4Hook, entry4Task);
  g_ctRaceOpenNmi = 0;
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  return 0;
}

int CtEntry4StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, entry4Hook, entry4Task);
  if (st != 2 && CtSeqStageSong(w, io) > 0) w[0x0388] = 0;            // (no song change in this entry)
  return st;
}

void CtEntry4StageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
