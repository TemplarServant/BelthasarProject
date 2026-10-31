// seq_entry7.c -- sequence entry 7 (`FF 07`): the Day of Lavos, after the final battle is lost.
//
// Scene 476 (loc 1DC), reached from the lost final battle (disasm/notes/sequence_table.md sec. 16). The setup $7E:6248
// (entry 8 shares it) puts page A at `$633D`; the script runs to an endless yield, and task `$63D5` waits for a pad press,
// fades out and becomes `$63F5`, which resets the console (`JML $00FF00`) 128 frames later.
// This file is the logic over the WRAM image: the script and its calls, the fades, the palette-record
// service ($37FA = FD/E39C), the sparkles' records ($0C00-$0CBF) and the RNG they draw from ($3448: `$039B` + 1, the byte
// at C0/FE00 + it), the WRAM palette fade $6439 and the end. The picture (the setup's VRAM packets, the CGRAM shadow it
// builds, the text sprites $64BF, the sparkles' sprites C3/0DBC, the PPU) is seq_entry7pic.c.
#include "seq_entry7.h"
#include "romview.h"     // the ROM's tables from ct_assets.dat (CT_ASSET_TABLE)
#include "seq_entry7pic.h"   // the picture, when the host draws it (io->pic)
#include "seq_racepic.h"
#include "objslot.h"
#include <string.h>

static unsigned rd(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

int CtEntry7StageHas(uint8_t operand) { return (operand & 0x1F) == 7 && !(operand & 0x40); }

// The setup's cycles, from `CT_CALLROM_NONMI=1 ct.exe --callrom 7E6248 --pokewram 0384=07 --regdb 00 --m8 --x16` on a
// state inside the opening with the overlay in WRAM: the VRAM packets above all.
long CtEntry7StageSetupCycles(void) { return 2534972L; }

static int s_reset;                                                   // $63F5 reached `JML $00FF00`
static int s_songHold;                                                // the song post's handshake frames left

// $7E:37FA -- the palette records' frame: FD/E39C over the twelve records at $0520 (seq_entry3.c's).
static void entry7Objslot(uint8_t* w, const CtRaceIo* io) {
  CtObjSlotIo o; memset(&o, 0, sizeof o);
  o.s = w; o.colourModes = 1; o.rom = io ? io->rom : NULL; o.romLen = io ? io->romLen : 0;
  CtObjSlotService(&o);
}

// $7E:3448 -- the overlay's RNG: `LDA $039B / INC A / STA $039B / TAX / LDA $C0FE00,X` (PHP / PLP around it: the caller's
// carry stands).
static uint8_t rng3448(uint8_t* w, const CtRaceIo* io) {
  uint8_t x = ++w[0x039B];
  return io ? CtRomByte(io->rom, io->romLen, 0xFE00 + x) : 0;
}

// $7E:65AE -- one sparkle drawn anew, its record at X: x = rnd & 7F + 40, y = E0 while the word `$0044` is set (the text is
// up) else rnd & 7F + 30, the delay rnd & 3F + 1, the frame count 0A, the step 04. Every caller enters with the carry clear
// ($6598 / $65DD: the loop's `CLC / ADC #0008` before each record, and the first record's the script's yield or the
// dispatcher's `CPX #0020`), and no ADC here can carry out (7F + 40 + 0, 7F + 30 + 0).
static void entry7Spark(uint8_t* w, unsigned x, const CtRaceIo* io) {
  w[x + 0] = (uint8_t)((rng3448(w, io) & 0x7F) + 0x40);               // 65AE JSR $3448 / AND #7F / ADC #40 / STA $0000,X
  if (rd(w, 0x0044)) w[x + 1] = 0xE0;                                 // 65B8 LDA #E0 / LDY $0044 / BNE (16-bit Y)
  else w[x + 1] = (uint8_t)((rng3448(w, io) & 0x7F) + 0x30);          // 65BF JSR $3448 / AND #7F / ADC #30
  w[x + 2] = (uint8_t)((rng3448(w, io) & 0x3F) + 1);                  // 65C9 JSR $3448 / AND #3F / INC A
  w[x + 3] = 0x0A;                                                    // 65D2
  w[x + 4] = 0x04;                                                    // 65D7
}

// $7E:65DD -- the sparkles' frame (M = 8): for each of the 24 records at $0C00, a delay counts down first; then the step
// $0004 counts 4 frames a frame of $0003, and a record whose frame count reaches 0 is drawn anew. A record still alive is
// drawn (C3/0DBC from the descriptor at $6640, whose first byte is its frame: (0A - $0003) | 80 -- the picture's).
static void entry7Sparkles(uint8_t* w, const CtRaceIo* io) {
  int draw = io && io->pic;
  wr(w, 0x0015, 0); wr(w, 0x0017, 0);                                 // 65DF LDX #0000 / STX $15 / STX $17
  for (unsigned x = 0x0C00; x != 0x0CC0; x += 8) {                    // 65E6 .. 663D
    if (w[x + 2]) { w[x + 2]--; continue; }                           // 65E9 LDA $0002,X / BEQ / DEC
    if (--w[x + 4] == 0) {                                            // 65F3 DEC $0004,X / BNE
      w[x + 4] = 0x04;                                                // 65FB
      if (--w[x + 3] == 0) { entry7Spark(w, x, io); continue; }       // 6600 DEC $0003,X / BNE / JSR $65AE
    }
    w[0x0015] = w[x + 0]; w[0x0017] = w[x + 1];                       // 660D-6615
    w[0x6640] = (uint8_t)((0x0A - w[x + 3]) | 0x80);                  // 6617 LDA #0A / SEC / SBC $0003,X / ORA #80 / STA
    if (draw) CtEntry7PicSparkle(w);                                  // 6629 JSL C3/0DBC (the picture's)
  }
}

// $7E:6439 -- the WRAM palette fade (D = $0300): every 16 frames (`LDA $08 / AND #0F`; $6422 makes it #1F), while $0366
// counts down, each of the $0364 colours at ($0362) steps one unit per component toward the one at ($0360). The scratch
// $03F0 / $03F2 / $03FE is the routine's own.
void CtSeqPalFade6439(uint8_t* w) {
  if (w[0x0008] & w[0x643E]) return;                                  // 643B LDA $08 / AND #0F (the operand at $643E)
  if (!w[0x0366]) return;                                             // 6445 LDA $66 / BEQ
  w[0x0366]--;                                                        // 6449 DEC $66
  unsigned x = rd(w, 0x0360), y = rd(w, 0x0362);                      // 644F LDX $60 / LDY $62
  wr(w, 0x03FE, rd(w, 0x0364));                                       // 6453 LDA $64 / STA $FE
  do {
    unsigned s = rd(w, x), d = rd(w, y);                              // (B = K = 7E: PHK / PLB)
    unsigned t = s & 0x001F, c = d & 0x001F;                          // 6457-6465
    wr(w, 0x03F0, t);
    if (c != t) c = ((int)c - (int)t) > 0 ? c - 1 : c + 1;            // 6467 BEQ / BPL DEC / INC
    wr(w, 0x03F2, c);
    t = s & 0x03E0; unsigned g = d & 0x03E0;                          // 6471-647C
    wr(w, 0x03F0, t);
    if (g != t) g = ((int)g - (int)t) > 0 ? g - 0x20 : g + 0x20;      // 6481-648C
    wr(w, 0x03F2, g | rd(w, 0x03F2));                                 // 648F ORA $F2 / STA $F2
    t = s & 0x7C00; unsigned b = d & 0x7C00;                          // 6493-649E
    wr(w, 0x03F0, t);
    if (b != t) b = ((int)b - (int)t) > 0 ? b - 0x0400 : b + 0x0400;  // 64A3-64AE
    wr(w, y, b | rd(w, 0x03F2));                                      // 64B1 ORA $F2 / STA $0000,Y
    x = (x + 2) & 0xFFFF; y = (y + 2) & 0xFFFF;                       // 64B6-64B9
    wr(w, 0x03FE, rd(w, 0x03FE) - 1);                                 // 64BA DEC $FE / BNE
  } while (rd(w, 0x03FE));
}

// $7E:63D5 -- the end, waiting for the pad (M = 16): `LDA $02 / BEQ`; then `81 40` through $377E (LDA #0081 / STA $1E00,
// LDX #0040 / STX $1E01), the fade-out $3495 added to the list ($395E: the first empty slot of $0920) and the slot handed to
// $63F5. $63F5: `DEC $0399` (16-bit, the setup's 0080) and at 0 `JML $00FF00`.
static unsigned entry7End(uint8_t* w, unsigned t, const CtRaceIo* io) {
  if (t == 0x63D5) {
    if (!rd(w, 0x0002)) return t;                                     // 63D5 LDA $02 / BEQ -> RTS
    wr(w, 0x1E00, 0x0081); wr(w, 0x1E01, 0x0040);                     // 63D9-63E2
    if (io && io->post) io->post(io->ctx);                            // 63E5 JSR $377E
    for (unsigned x = 0; x < 0x20; x += 2)                            // 63E8 LDX #3495 / JSR $395E
      if (!rd(w, 0x0920 + x)) { wr(w, 0x0920 + x, 0x3495); break; }
    return 0x63F5;                                                    // 63EE LDX #63F5 / JMP $39B1
  }
  wr(w, 0x0399, rd(w, 0x0399) - 1);                                   // 63F5 DEC $0399
  if (!rd(w, 0x0399)) s_reset = 1;                                    // 63F8 BNE / 63FA JML $00FF00
  return t;
}

// The script's `F0` calls ($0AF0 -> JSL $37F3 -> JSR ($00F0,X)).
static int entry7Call(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  (void)page; (void)io;
  switch (tgt) {
    case 0x63FE: case 0x6403:                                         // the WRAM palette fade's arm (6403: entry 8's)
      wr(w, 0x0360, tgt == 0x63FE ? 0xB7B4 : 0xB5B4);                 // 63FE LDA #B7B4 / 6403 LDA #B5B4 / STA $0360
      wr(w, 0x038E, 0x0808);                                          // 6409
      wr(w, 0x0362, 0x2240);                                          // 640F
      wr(w, 0x0366, 0x0020);                                          // 6415
      wr(w, 0x0364, 0x00C0);                                          // 641B
      return 0;
    case 0x6422:                                                      // the fade's second target, at half the rate
      wr(w, 0x0360, 0xB5B4); wr(w, 0x0366, 0x0020);                   // 6422-642B
      w[0x643E] = 0x1F;                                               // 6430 LDA #1F / STA $7E643E (6439's AND #0F)
      return 0;
    case 0x5DB4:                                                      // palette record 0's fade (the title's "(c)" one)
      w[0x0520] = 0x50; w[0x0521] = 0xC0; w[0x0522] = 0x10; w[0x0523] = 0x04; w[0x0524] = 0;
      w[0x0525] = 0x01; w[0x0526] = 0; w[0x0527] = 0; w[0x0528] = 0xFF;   // 5DBA-5DE2
      return 0;
  }
  return -1;
}

static unsigned entry7Task(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtSeqScript(w, 0x0A00, io, entry7Call); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, entry7Call); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x37FA: wr(w, 0x00F0, 0x4242); entry7Objslot(w, io); return t;   // `LDX $4202` reads the open bus ($42), as entry 3
    case 0x65DD: entry7Sparkles(w, io); return t;
    case 0x6439: CtSeqPalFade6439(w); return t;
    case 0x63D5: case 0x63F5: return entry7End(w, t, io);
    default:                                                          // 64BF: the text's sprites (the picture's)
      if (io && io->pic) CtEntry7PicTask(io->pic, w, t);
      return t;
  }
}

static unsigned entry7Hook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) { (void)slot; return CtEntry7PicHook(io->pic, w, h); }

int CtEntry7StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEntry7StageHas(operand)) return -1;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  s_reset = 0; s_songHold = 0;
  // $7E:6248 -- the setup. Its VRAM packets ($3691 / C3/0557), the CGRAM shadow it builds ($8772, the copy $38AF), the DMAs
  // and the PPU are the picture's; what the logic reads, in order:
  if (io && io->pic) CtEntry7PicSetupA(io->pic, w, io);               // 6248-62EB: the picture's loads (seq_entry7pic.c)
  wr(w, 0x0A20, (operand & 0x0F) == 8 ? 0x63A6 : 0x633D);             // 62D7-62E8 (`LDA $0384 / AND #000F / CMP #0008`)
  if (CtSeqScript(w, 0x0A00, io, entry7Call) < 0) return -1;          // 62F0 JSR $3749 -- page A runs once in the setup
  for (unsigned x = 0x0C00; x != 0x0CC0; x += 8) entry7Spark(w, x, io);   // 62FA JSR $6598 (the carry clear: see 65AE)
  for (unsigned y = 0; y < 0x90; y += 0x0C) w[0x0520 + y] = 0;        // 62FD JSL $FD:FFEE -> FD/E275
  if (io && io->pic) { CtEntry7PicSetupB(io->pic); }                  // 6301-6333: the registers
  wr(w, 0x0399, 0x0080);                                              // 6336 LDX #0080 / STX $0399
  // $7E:300D .. 301E: $388B, $31B0, JSL C3/08D0 (one NMI, run here, without C3/09A4), NMI on, STZ $00, STZ $0392
  if (io && io->pic) CtRacePicCgram(io->pic, w);                      // 300D JSR $388B
  g_ctRaceOpenNmi = 1;   // (the setup's last NMI: its posts wait for the opening's black frames, seq_race.h)
  CtSeqStageNmiHooks(w, 0, io, (io && io->pic) ? entry7Hook : NULL, entry7Task);
  g_ctRaceOpenNmi = 0;
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  return 0;
}

int CtEntry7StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, (io && io->pic) ? entry7Hook : NULL, entry7Task);
  if (s_reset) return 2;
  if (st != 2) s_songHold = CtSeqStageSongTick(w, io, s_songHold);
  return st;
}

void CtEntry7StageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
