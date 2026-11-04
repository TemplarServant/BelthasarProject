// seq_entry4.c -- sequence entry 4 (`FF 24`): Lavos falls through space onto the planet (65,000,000 BC).
//
// Scene 301, after the Black Tyranno (disasm/notes/sequence_table.md sec. 15: the scripts $552A / $5568 and their tasks).
// This file is the logic over the WRAM image: the two scripts and their calls, the fades, the tasks that write the pages'
// registers ($56A9, $5855, $55F9), and the starfield ($5777 in the setup, task $579B every frame, seq_stagelib.c), which
// draws from the overlay's RNG C3/02DD: what it leaves in `$0386` is the field's afterwards. The picture (the mode-7
// registers $5654, the planet's sprite $56EB, the setup's VRAM packets and tables) is seq_entry4pic.c.
#include "seq_entry4.h"
#include "seq_entry4pic.h"   // the picture, when the host draws it (io->pic)
#include "seq_racepic.h"
#include <string.h>
#include "seq_wram.h"   // CT_SEQ_W16 / CT_SEQ_W16_PUT: the WRAM word helpers

static unsigned rd(const uint8_t* w, unsigned a) { return CT_SEQ_W16(w, a); }
static void wr(uint8_t* w, unsigned a, unsigned v) { CT_SEQ_W16_PUT(w, a, v); }

int CtEntry4StageHas(uint8_t operand) { return (operand & 0x1F) == 4 && !(operand & 0x40); }

// The setup's cycles, from `CT_CALLROM_NONMI=1 ct.exe --callrom 7E53BB --pokewram 0384=24 --regdb 00 --m8 --x16` on a
// state with the overlay in WRAM: the VRAM packets, the mode-7 tile conversion $37CA and the 128 stars of $5777.
long CtEntry4StageSetupCycles(void) { return 1938002L; }

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
