// seq_entry5.c -- sequence entry 5 (`$7E:7092`): the vortex that throws the party through time after Magus's fight.
//
// Scene 0AC (Magus's castle) obj9 after the fight: `DD 0F 03 08 08` (C0/3B1B, $0100 = 10F), `E8 C3`, `FF 9B` (C0/41BC:
// `$0139 = 0B`); C0/21E1's entry 0B is C0/260E, which ends `LDA #85 / JML C3:0000` (disasm/notes/sequence_table.md sec. 17).
// The stage ends on the fade $34E3 and the teardown reboots into 10F.
// This file is the logic over the WRAM image: the two scripts and their call $7286, the tasks the logic reads ($7193 the
// spin's counters, $737E the RNG C3/02DD -- what it leaves in `$0386` is the field's afterwards --, the fades, and the party's
// animation records of $74F5 / $76A5 / $7721 / $77AE, whose scratch the RNG folds in), and the setup's writes those read. The
// picture (the main loop's renderer $7441, the records' positions, tiles and OAM, the window circle $7325, the wave $73AA,
// the matrix $73C6, the colours $72FF, the hooks 783C 7868 78AB 388B 380B 3843, the setup's HDMA $36CC, VRAM and PPU) is not
// ported yet: those parts run as no-ops here, and the host draws black (field.c).
#include "seq_entry5.h"
#include "seq_entry4.h"   // CtSeqRng02DD8: C3/02DD (seq_stagelib.c)
#include "romview.h"
#include <string.h>
#include "seq_wram.h"   // CT_SEQ_W16 / CT_SEQ_W16_PUT: the WRAM word helpers

static int s_songHold;   // the song post's handshake frames left (page B's `$038A = 3A`: C3/09A4's JSL C7/0004)

static unsigned rd(const uint8_t* w, unsigned a) { return CT_SEQ_W16(w, a); }
static void wr(uint8_t* w, unsigned a, unsigned v) { CT_SEQ_W16_PUT(w, a, v); }

int CtEntry5StageHas(uint8_t operand) { return (operand & 0x1F) == 5 && !(operand & 0x40); }

// The setup's cycles, from `CT_CALLROM_NONMI=1 ct.exe --callrom 7E7092 --pokewram 0384=85 --regdb 00 --m8 --x16` on a state with
// the overlay in WRAM (build/_pt77/s95_85100): the two MVNs, the scripts' first visits, $74F5, the HDMA and VRAM setup, $31B0.
long CtEntry5StageSetupCycles(void) { return 732452L; }

// $7E:3448 -- the overlay's RNG: `LDA $039B / INC A / STA $039B / TAX / LDA $C0FE00,X` (`TDC / XBA` first: B = 0).
static uint8_t rng3448(uint8_t* w, const CtRaceIo* io) {
  uint8_t x = ++w[0x039B];
  return io ? CtRomByte(io->rom, io->romLen, 0xFE00 + x) : 0;
}

// The scripts' `F0` call: $7286 (page A's every frame, D = $0A00): with `$004B` set, `JSR $3448 / AND #0006 / TAX / LDA
// $729C,X / STA $0A20` -- page A jumps to one of four scripts (the call's own `INC $20` x2 then steps past their first word).
static int entry5Call(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  (void)page;
  if (tgt != 0x7286) return -1;
  if (!w[0x004B]) return 0;                                           // 7286 LDA $004B / AND #00FF / BEQ -> RTS
  unsigned x = rng3448(w, io) & 0x06;                                 // 728E JSR $3448 / AND #0006 / TAX
  wr(w, 0x0A20, rd(w, 0x729C + x));                                   // 7295 LDA $729C,X / STA $0A20
  return 0;
}

// $7E:7193 (8-bit A): `DEC $4A / BNE -> RTS`, then `$4A = $49` and `$4C = ($4C + 1) & 7`.
static void entry5Spin(uint8_t* w) {
  if (--w[0x004A]) return;                                            // 7195 DEC $4A / BNE
  w[0x004A] = w[0x0049];                                              // 7199 LDA $49 / STA $4A
  w[0x004C] = (uint8_t)((w[0x004C] + 1) & 7);                         // 719D LDA $4C / INC A / AND #07 / STA $4C
}

// $7E:737E (8-bit A, X = the task's slot): `$4C` != FF -> the mosaic `$2106 = $4C << 4 | 1` (the picture's); else every
// fourth frame (`LDA $08 / AND #03 / BNE`) a new `$4D` from the RNG: `JSR $3443` (C3/02DD), `AND #30 / ADC #20`, drawn
// again while it equals `$4D`; then the mosaic `$4D | 1`. The carry: `CMP #FF` (equal: set) into the first draw, the
// `CMP $4D` (equal: set) into each redraw; A into each redraw is the value just compared.
static void entry5Flash(uint8_t* w, unsigned slot) {
  if (w[0x004C] != 0xFF) return;                                      // 7380 LDA $4C / CMP #FF / BEQ -> 7391
  if (w[0x0008] & 0x03) return;                                       // 7391 LDA $08 / AND #03 / BNE -> RTS
  uint8_t a = 0; int c = 1;                                           // (A = $08 & 3 = 0, the carry from CMP #FF)
  for (int guard = 0; guard < 256; guard++) {
    a = CtSeqRng02DD8(w, a, &c, slot);                                // 7397 JSR $3443
    a &= 0x30;                                                        // 739A AND #30
    unsigned t = (unsigned)a + 0x20 + (unsigned)c; a = (uint8_t)t; c = t > 0xFF;   // 739C ADC #20
    c = a >= w[0x004D];                                               // 739E CMP $4D
    if (a != w[0x004D]) break;                                        //   BEQ -> 7397
  }
  w[0x004D] = a;                                                      // 73A2 STA $4D (ORA #01 / STA $2106: the picture's)
}

// ---- The party's four sprite records (D = $0000, X = 0 / 16 / 2C / 42): what the RNG's scratch reads ----
// C3/02DD folds `$00F0` / `$00F2` / `$00F4` in, and the task $7721 (before $737E in the list) leaves them: `$F0` at 0 (its OAM
// builder $701E counts `$F0` down from 4 for every record), `$F2` / `$F4` from the animation step's $77E7. So the records'
// animation is the logic's: their setup ($74F5), their facing ($76A5's `$65`) and the step ($77AE). Their positions (`$54` /
// `$55` from the radius `$5D` and the hardware multiply), the tiles ($6E42) and the OAM ($7787 / $701E) are the picture's.
static uint8_t e4b(const CtRaceIo* io, unsigned a) { return io ? CtRomByte(io->rom, io->romLen, 0x240000L + (a & 0xFFFF)) : 0; }
static unsigned e4w(const CtRaceIo* io, unsigned a) { return e4b(io, a) | ((unsigned)e4b(io, a + 1) << 8); }

// $7E:5872 -- the angle of (X, Y) (16-bit): `$F0` / `$F2` = their signs (STZ / INC, 16-bit), the quotient (|Y| & FF) << 8 over
// (|X| - 1) (`TXA / DEC A / ... / STA $4206` 8-bit: the divisor is |X| - 1), found in the table $7E:58F8 (`CMP $58F8,Y / BCC`,
// Y from 2 by 2 to 80, then `DEY / DEY`), turned by the quadrant. |X| = 0 or 1: 0100 with Y negative, else 0300.
static unsigned entry5Angle(uint8_t* w, unsigned x, unsigned y) {
  x &= 0xFFFF; y &= 0xFFFF;
  unsigned f0 = 0, f2 = 0;
  if (x & 0x8000) { x = ((x ^ 0xFFFF) + 1) & 0xFFFF; f0 = 1; }        // 5876-587E
  if (y & 0x8000) { y = ((y ^ 0xFFFF) + 1) & 0xFFFF; f2 = 1; }        // 5882-588A
  wr(w, 0x00F0, f0); wr(w, 0x00F2, f2);
  unsigned num = (y & 0x00FF) << 8;                                   // 588C AND #00FF / XBA / STA $4204
  unsigned d = (x - 1) & 0xFFFF;                                      // 5894 TXA / DEC A
  if ((d & 0x8000) || d == 0) return f2 ? 0x0100 : 0x0300;            // 5896 BMI / BNE -> 589A
  unsigned q = num / (d & 0xFF);                                      // 58A8 STA $4206 (8-bit) / RDDIV
  unsigned k = 2;
  for (; k != 0x80; k += 2) if (q < rd(w, 0x58F8 + k)) break;         // 58BB CMP $58F8,Y / BCC (data bank $7E)
  k -= 2;                                                             // 58C8 DEY / DEY
  unsigned a = (k << 1) & 0xFFFF;
  if (f0 ^ f2) return (((f2 ? a - 0x0200 : a - 0x0400) ^ 0xFFFF) + 1) & 0xFFFF;   // 58D0-58E7
  return (f0 & f2) ? a : (a + 0x0200) & 0xFFFF;                       // 58E8-58F7
}

// $7E:74F5 -- the setup's records: the frame `$51` = `$5A`, the character `$52` (a word: the party slot and the next one), the
// animation set by `$52 & 7` ($762E: `$5E` = $7E:7642[i], `$5F` = it + 1), `$56`, `$60` = 3000, `$58` (the OAM word), the
// position `$54` / `$55` from the nibbles $29B0.. and $7649's angle `$5B` = $5872($54 - 80, $55 - 70) (`$F4` / `$F6` its
// operands; its radius `$5D`, $34FB, is the picture's).
static void entry5Records(uint8_t* w) {
  static const struct { uint16_t frame, chr; uint16_t v56, v58; uint16_t pos; } R[4] = {
    { 0x29B4, 0x2980, 0x0080, 0x3A08, 0x29B0 }, { 0x29B9, 0x2981, 0x0200, 0x3C20, 0x29B5 },
    { 0x29BE, 0x2982, 0x0280, 0x3E28, 0x29BA }, { 0x29C3, 0x0000, 0x1880, 0x3388, 0x29BF } };
  for (unsigned k = 0; k < 4; k++) {
    unsigned x = k * 0x16;
    w[0x51 + x] = w[R[k].frame]; w[0x5A + x] = w[R[k].frame];                       // 7500 / 754D / 759A / 75E7
    wr(w, 0x52 + x, R[k].chr ? rd(w, R[k].chr) : 0x0006);                         // 750A (16-bit) / 75F1 LDA #0006
    w[0x5E + x] = w[0x7642 + (rd(w, 0x52 + x) & 7)];                              // $762E
    w[0x5F + x] = (uint8_t)(w[0x5E + x] + 1);
    wr(w, 0x56 + x, R[k].v56); wr(w, 0x60 + x, 0x3000); wr(w, 0x58 + x, R[k].v58);
    w[0x54 + x] = (uint8_t)((w[R[k].pos] << 4) | w[R[k].pos + 2]);                // 7524-7530
    w[0x55 + x] = (uint8_t)((w[R[k].pos + 1] << 4) | w[R[k].pos + 3]);            // 7532-753E
    unsigned f4 = (w[0x55 + x] - 0x70) & 0xFFFF, f6 = (w[0x54 + x] - 0x80) & 0xFFFF;   // $7649: 764A-765F
    wr(w, 0x00F4, f4); wr(w, 0x00F6, f6);
    wr(w, 0x5B + x, entry5Angle(w, f6, f4));                                      // 7662 JSR $5872 / STA $5B,X
  }
}

// $7E:76A5 -- the facing (for the records 0, 16, 2C): `$A9 += $0B04` (page B's r2), `$65` = $7E:771D[((A9 + $5B) + 80) >> 8
// & 3] & 3; the position from the sine and the radius (M7A x M7B) is the picture's.
static void entry5Facing(uint8_t* w) {
  wr(w, 0x00A9, rd(w, 0x00A9) + rd(w, 0x0B04));                       // 76A5 LDA $A9 / CLC / ADC $0B04 / STA $A9
  for (unsigned x = 0; x <= 0x2C; x += 0x16) {
    unsigned v = (rd(w, 0x00A9) + rd(w, 0x5B + x) + 0x0080) & 0xFFFF; // 76BC-76C4
    w[0x65 + x] = (uint8_t)(w[0x771D + ((v >> 8) & 3)] & 3);          // 76C7-76D5
  }
}

// $7E:77E7 (X = the record, data bank $E4): `$F6` = E4:2600[c], `$F2` = `$5E` x 4, `$63` = `$F4` = `$F2` + E4:2800[c], `$FA` =
// (E4:2602[c] - `$F6`) / 2, `$F8` = `$FA` / 2, `$60` = `$F6` + the facing's share (0, `$F8`, `$FA`, `$FA` + `$F8`) + `$F2`.
static void entry5Anim77E7(uint8_t* w, unsigned x, const CtRaceIo* io) {
  unsigned y = (rd(w, 0x52 + x) & 0xFF) << 1;                         // 77E9-77EF
  unsigned f6 = e4w(io, 0x2600 + y); wr(w, 0x00F6, f6);               // 77F0-77F3
  unsigned f2 = ((rd(w, 0x5E + x) & 0xFF) << 2) & 0xFFFF; wr(w, 0x00F2, f2);   // 77F5-77FC
  unsigned f4 = (f2 + e4w(io, 0x2800 + y)) & 0xFFFF;                  // 77FE CLC / ADC $2800,Y
  wr(w, 0x63 + x, f4); wr(w, 0x00F4, f4);                             // 7802 / 7804
  unsigned fa = ((e4w(io, 0x2602 + y) - f6) & 0xFFFF) >> 1; wr(w, 0x00FA, fa);   // 7806-780D
  unsigned f8 = fa >> 1; wr(w, 0x00F8, f8);                           // 780F-7810
  unsigned a = f6, face = w[0x65 + x];                                // 7812-7818
  if (face == 3) a += fa + f8; else if (face == 2) a += fa; else if (face == 1) a += f8;   // 781A-7832
  wr(w, 0x60 + x, (a + f2) & 0xFFFF);                                 // 7834 CLC / ADC $F2 / STA $60,X
}

// $7E:77AE (8-bit A, data bank $E4): a new set (`$5E` != `$5F`) restarts the list; a new facing (`$65` != `$66`) takes the
// next entry at once; else the dwell `$62` counts down. An entry FF restarts it ($77E7). The entry: `$51` = E4[`$60`], `$62` =
// E4[`$63`], both pointers + 1 (16-bit INC).
static void entry5Anim(uint8_t* w, unsigned x, const CtRaceIo* io) {
  int restart = 0;
  if (w[0x5E + x] != w[0x5F + x]) restart = 1;                        // 77B4 LDA $5E,X / CMP $5F,X / BNE -> 77CA
  else if (w[0x65 + x] == w[0x66 + x]) {                              // 77BA LDA $65,X / CMP $66,X / BNE -> 77C4
    if (--w[0x62 + x]) return;                                        // 77C0 DEC $62,X / BNE -> 77E3
  }
  uint8_t a = 0;
  if (!restart) { a = e4b(io, rd(w, 0x60 + x)); if (a == 0xFF) restart = 1; }   // 77C4 LDA ($60,X) / CMP #FF
  if (restart) {                                                      // 77CA
    w[0x66 + x] = w[0x65 + x]; w[0x5F + x] = w[0x5E + x];
    entry5Anim77E7(w, x, io);                                         // 77D2 JSR $77E7
    a = e4b(io, rd(w, 0x60 + x));                                     // 77D5 LDA ($60,X)
  }
  w[0x51 + x] = a;                                                    // 77D7 STA $51,X
  w[0x62 + x] = e4b(io, rd(w, 0x63 + x));                             // 77D9 LDA ($63,X) / STA $62,X
  wr(w, 0x60 + x, rd(w, 0x60 + x) + 1); wr(w, 0x63 + x, rd(w, 0x63 + x) + 1);   // 77DF INC $60,X / INC $63,X (16-bit)
}

// $7E:7721 -- the records' frame: `$41` = FFFF, `$AB` = 4; from `$AD` the first record whose frame changed (`$51` != `$5A`)
// takes it (`$5A` = `$51`, `$41` = its `$56`, the tiles $6E42 -- the picture's), `$AD` steps to the next record ($7774); then
// every record's OAM ($7787 -> $701E, the picture's; `$F0` = `$52 & FF` from $74EB, then $701E's count leaves it 0) and its
// animation step ($77AE).
static void entry5Party(uint8_t* w, const CtRaceIo* io) {
  wr(w, 0x0041, 0xFFFF); wr(w, 0x00AB, 0x0004);                       // 7721-7729
  for (;;) {
    unsigned x = rd(w, 0x00AD);                                       // 772B LDX $AD
    int changed = w[0x51 + x] != w[0x5A + x];                         // 772F LDA $51,X / CMP $5A,X
    if (changed) { w[0x5A + x] = w[0x51 + x]; wr(w, 0x0041, rd(w, 0x56 + x)); }   // 7735 / 773A-773C
    unsigned nx = (x + 0x16) & 0xFFFF; if (nx == 0x58) nx = 0;        // $7774
    wr(w, 0x00AD, nx);
    if (changed) break;                                               // 7754 BRA -> 775F
    wr(w, 0x00AB, rd(w, 0x00AB) - 1);                                 // 775B DEC $AB (16-bit)
    if (!rd(w, 0x00AB)) break;                                        //   BNE -> 772D
  }
  for (unsigned x = 0; x != 0x58; x += 0x16) {                        // 775F-7771
    wr(w, 0x00F0, rd(w, 0x52 + x) & 0xFF);                            // $7787 -> $74EB: `STA $F0` (16-bit)
    w[0x00F0] = 0;                                                    //   -> $701E: `LDA #04 / STA $F0` .. `DEC $F0` to 0
    entry5Anim(w, x, io);                                             // 7765 JSR $77AE
  }
}

static unsigned entry5Task(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x76A5: entry5Facing(w); return t;
    case 0x7721: entry5Party(w, io); return t;
    case 0x3749: CtSeqScript(w, 0x0A00, io, entry5Call); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, entry5Call); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x7193: entry5Spin(w); return t;
    case 0x737E: entry5Flash(w, slot); return t;
    default: return t;                                                // the picture's tasks (see the head of the file)
  }
}

static unsigned entry5Hook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) {
  (void)w; (void)slot; (void)io;
  return h;                                                           // the picture's hooks
}

int CtEntry5StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEntry5StageHas(operand)) return -1;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  // $7E:7092 -- the setup (8-bit A, 16-bit X)
  w[0x0350] = 0x80; w[0x0352] = 0x70; w[0x0358] = 0x7E;               // 7092-709E
  memmove(w + 0x0C00, w + 0xC800, 0x800);                             // 70A3 LDX #C800 / LDY #0C00 / LDA #07FF / MVN $00,$7E
  memmove(w + 0x1400, w + 0xC800, 0x800);                             // 70AF LDX #C800 / LDY #1400 / MVN
  wr(w, 0x038E, 0x0202);                                              // 70BB LDX #0202 / STX $038E
  wr(w, 0x0B20, 0x71A5); wr(w, 0x0A20, 0x7157);                       // 70C1-70CA
  if (CtSeqScript(w, 0x0A00, io, entry5Call) < 0) return -1;          // 70CE JSR $3749
  if (CtSeqScript(w, 0x0B00, io, entry5Call) < 0) return -1;          // 70D1 JSR $3754
  wr(w, 0x3024, 0x7441);                                              // 70D4 LDA #7441 / STA $3024: the main loop's JSR
  // 70DD JSR $36CC (Y = 7354: the HDMA channels) -- the picture's
  w[0x038D] = 0x0F;                                                   // 70E3 LDA #0F / STA $038D
  entry5Records(w);                                                   // 70E8 JSR $74F5 (their logic; see entry5Records)
  // 70EB-7112 the VRAM DMA, 7117-7139 the PPU -- the picture's
  w[0x003D] = 0x01; w[0x003F] = 0x01;                                 // 713D LDA #01 / STA $05 / STA $003D / STA $003F
  w[0x038C] = 0x0F;                                                   // 7147 LDA #0F / STA $038C
  // $7E:300D .. 301E: $388B, $31B0, JSL C3/08D0 (one NMI, run here), NMI on, STZ $00, STZ $0392
  s_songHold = 0;
  g_ctRaceOpenNmi = 1;
  CtSeqStageNmiHooks(w, 0, io, entry5Hook, entry5Task);
  g_ctRaceOpenNmi = 0;
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  return 0;
}

int CtEntry5StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, entry5Hook, entry5Task);
  if (st != 2) s_songHold = CtSeqStageSongTick(w, io, s_songHold);    // page B's song change (`$038A = 3A`)
  return st;
}

void CtEntry5StageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
