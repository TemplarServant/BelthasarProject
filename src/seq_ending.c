// seq_ending.c -- THE ENDING's $C3 stage (sequence entries 10, 11, 14, 15: `FF 2A` / `FF 2B` / `FF 2E` / `FF 2F`), native.
//
// $7E:3043's table points all four entries at one setup, $7E:8538 (disasm/notes/sequence_table.md sec. 13). It loads the
// world's picture and the staff credits, hands the two script pages their programs (by `$0384 & 0F`) and lets them run: the
// story's `FF 2B` (Leene's Square, scene 81) plays the credits over the world and ENDs on its own fade ($7E:34E3); `FF 2E` /
// `FF 2F` (scene 90) end on "The End" for good -- page A stops on a jump-to-self with $0396 = 0, and only the soft reset leaves.
// The scripts keep TIME WITH THE SONG: `F5 n` (C3/0C2A) waits until the sound processor's port 2 shows marker n, so the
// stage's length is the song's (the cartridge: `FF 2B` at f131 from saves/ending_re/ending_pre_ff2b.state, the markers pass
// at f349 / f829 / f2029 / f3950, the fade at f4910, the END at f4925).
//
// Stage 1 of the native port, as the race's (seq_race.c): the LOGIC -- the scripts, the waits, the brightness tasks that
// decide the END, the setup's WRAM that the logic reads -- over the WRAM image, byte for byte with the cartridge. The picture
// (the mode-7 world, the credits text, the HDMA, the hooks, the render tasks and the main thread's $8930) is stage 2: its
// setup packets and DMAs are not run here, and its inline calls (`F0 xxxx`) are accepted and left to it.
#include "seq_ending.h"
#include <string.h>
#include <stdio.h>

// The frames a song's post is still inside C3/09A4's JSL C7/0004 (`$0388` set; the NMIs run on over it: CtSeqStageSongTick).
static int s_songHold;

static unsigned rd(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

int CtEndingStageHas(uint8_t operand) {
  unsigned e = operand & 0x1F;
  return !(operand & 0x40) && (e == 10 || e == 11 || e == 14 || e == 15);
}

// The scripts' inline calls (op `F0`, C3/0C51 -> JSL $7E:37F3 -> JSR ($00F0)). Most only draw: 8790 / 87CC load the world
// (87CC is page A's first op in `FF 2B`), 9919 copies $0064 into the HDMA table ($64D6 / $64DA), 9FC8 / 9FEA set two colour
// animations ($052C.. / $0538..), 98F6 / 98FB / 9900 a palette transfer ($0360-$0366), 8DF3 / 96FB / 9702 / 9F8A the last
// screens' palettes and layers. The backdrop reloads of the last screens also reset the stage's lists, and that is logic:
// $7E:7EF8 copies 32 bytes of $D2:0000 (zeros) over the task list $0920 and $7E:7F05 16 over the hooks $0940 -- the script
// re-adds what it wants right after (`gosub $9325`) -- and they set $038C. 8CD0 is where entry 15 leaves entry 14's path.
static void endingClearTasks(uint8_t* w) { memset(w + 0x0920, 0, 0x20); }   // $7E:7EF8 `MVN $7E,$D2` (LDA #001F)
static void endingClearHooks(uint8_t* w) { memset(w + 0x0940, 0, 0x10); }   // $7E:7F05 `MVN $7E,$D2` (LDA #000F)

static int endingCall(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  (void)page; (void)io;
  switch (tgt) {
    case 0x8A01: endingClearTasks(w); endingClearHooks(w); w[0x038C] = 0x01; return 0;   // 8A02 / 8A05, 8AD0 LDA #01 / STA $038C
    case 0x8AF5: endingClearTasks(w); endingClearHooks(w); w[0x038C] = 0x0F; return 0;   // 8AF6 / 8AF9, 8B15 LDA #0F / STA $038C
    case 0x8BAF: endingClearTasks(w); endingClearHooks(w); w[0x038C] = 0x00; return 0;   // 8BB0 / 8BB3, 8BC3 (A = $0000, TCD)
    case 0x8C14: endingClearTasks(w); endingClearHooks(w); w[0x038C] = 0x00; return 0;   // 8C15 / 8C18, 8C28 (A = $0000, TCD)
    case 0x8CD0:                                                                         // 8CD1 JSR $7EF8 (the tasks only)
      endingClearTasks(w);
      if ((w[0x0384] & 0x0F) == 0x0F) wr(w, 0x0A20, 0x9315);   // 8CE0 LDA $0384 / AND #0F / CMP #0F / BNE: entry 15's end
      return 0;                                                //   (8CE9 LDX #9315 / STX $0A20 -- the IP the call returns to)
    case 0x8DF3:                                                                         // the last screen's planet:
      memset(w + 0x23C0, 0, 0x20);                                                       //   8E26 MVN $7E,$D2 (a palette row)
      wr(w, 0x71, 0x00E0); wr(w, 0x73, 0x0080); wr(w, 0x75, 0x0080);                    //   8E2C .. 8E3E: the pan that
      wr(w, 0x77, 0x0080); wr(w, 0x79, 0x0080);                                          //   task $9A0C runs out
      return 0;
    case 0x8790: case 0x87CC: case 0x96FB: case 0x9702:
    case 0x98F6: case 0x98FB: case 0x9900: case 0x9919: case 0x9F8A: case 0x9FC8: case 0x9FEA:
      return 0;
    default:
      return -1;
  }
}

// ---- the two tasks that write the script pages' registers ----
// (Everything else the scripts' registers hold comes from the scripts themselves; these two tasks are why page A's r2 and
// page B's r1 / r2 move on frames no script op runs.)

// $7E:90C1 -- page A's r2 turns by page B's r8: `LDA $0A04 / CLC / ADC $0B10 / AND #03FF / STA $0A04` (an angle, 0-$3FF).
static void task90C1(uint8_t* w) { wr(w, 0x0A04, (rd(w, 0x0A04) + rd(w, 0x0B10)) & 0x03FF); }

// $7E:3465 / $7E:3461 -- the overlay's sine and cosine (seq_race.h CtSeqSine).
static unsigned sine3465(const CtRaceIo* io, unsigned a) { return CtSeqSine(io, a); }

// $7E:33D6 -- the signed product $F0 * $F2 into $F4-$F7 (D = the page): the sign of $F0 ^ $F2 kept, both made positive, then
// $7E:31BF's unsigned 16 x 16 (seq_race.h CtSeqMul31BF), and the 32-bit result negated if the sign was set.
static void mul33D6(uint8_t* w, unsigned d) {
  unsigned f0 = rd(w, d + 0xF0), f2 = rd(w, d + 0xF2);
  int negr = ((f0 ^ f2) & 0x8000) != 0;
  if (f0 & 0x8000) f0 = ((f0 ^ 0xFFFF) + 1) & 0xFFFF;
  if (f2 & 0x8000) f2 = ((f2 ^ 0xFFFF) + 1) & 0xFFFF;
  wr(w, d + 0xF0, f0); wr(w, d + 0xF2, f2);
  CtSeqMul31BF(w, d);                                                 // 31BF
  if (negr) {                                                         // 33F6 CLC / EOR #FFFF / ADC #0001 ...
    unsigned lo = (rd(w, d + 0xF4) ^ 0xFFFF) + 1, c = lo > 0xFFFF;
    wr(w, d + 0xF4, lo);
    wr(w, d + 0xF6, (rd(w, d + 0xF6) ^ 0xFFFF) + c);                  // ... EOR #FFFF / ADC #0000
  }
}

// $7E:992A -- page B's orbit (D = $0B00): r1 = (sin r5 * r8)'s bits 12-27 + $80, r2 = (cos r5 * r8)'s bits 13-28 + $70, and
// the main thread's $004E from -r5 (8921: the world's angle, the picture's). The carries are the ROM's: `SBC #0300` takes the
// one `ADC #0070` left, 8921's `ADC #1200` the last `ASL`'s.
static void task992A(uint8_t* w, const CtRaceIo* io) {
  const unsigned d = 0x0B00;
  wr(w, d + 0xF0, sine3465(io, rd(w, d + 0x0A)));                     // 992E LDA $0A / JSR $3465 / STA $F0
  wr(w, d + 0xF2, rd(w, d + 0x10));                                   // 9935 LDA $10 / STA $F2
  mul33D6(w, d);
  wr(w, d + 0x02, ((rd(w, d + 0xF5) >> 4) + 0x0080) & 0xFFFF);        // 993C LDA $F5 / LSR x4 / CLC / ADC #0080 / STA $02
  wr(w, d + 0xF0, sine3465(io, rd(w, d + 0x0A) + 0x0100));            // 9948 LDA $0A / JSR $3461 / STA $F0
  wr(w, d + 0xF2, rd(w, d + 0x10));
  mul33D6(w, d);
  unsigned s = (rd(w, d + 0xF5) >> 5) + 0x0070;                       // 9956 LDA $F5 / LSR x5 / CLC / ADC #0070
  unsigned c = s > 0xFFFF;
  wr(w, d + 0x04, s & 0xFFFF);                                        // 9961 STA $04
  unsigned a = ((rd(w, d + 0x0A) ^ 0xFFFF) + 1) & 0xFFFF;             // 9963 LDA $0A / EOR #FFFF / INC A
  a = (a - 0x0300 - (1 - c)) & 0xFFFF;                                // 9969 SBC #0300
  a &= 0x03E0;                                                        // 8921 AND #03E0
  for (int k = 0; k < 5; k++) { c = (a >> 15) & 1; a = (a << 1) & 0xFFFF; }   // ASL x5
  wr(w, 0x004E, (a + 0x1200 + c) & 0xFFFF);                           // 8929 ADC #1200 / STA $004E
}

// $7E:997D -- the last screens' page B (D = $0B00): r2 = sin r5 >> 6, r0 = cos r5 >> 6 (logical shifts of the 16-bit value).
static void task997D(uint8_t* w, const CtRaceIo* io) {
  wr(w, 0x0B04, sine3465(io, rd(w, 0x0B0A)) >> 6);                   // 9981 LDA $0A / JSR $3465 / LSR x6 / STA $04
  wr(w, 0x0B00, sine3465(io, rd(w, 0x0B0A) + 0x0100) >> 6);          // 998E LDA $0A / JSR $3461 / LSR x6 / STA $00
}

// $7E:64CA's walk over one credits page (D = 0, DB = $7E): from X, a byte at a time -- `00 x y` places the pen (two bytes
// more), `2F` is a new line, `20` a space, anything else a glyph (drawn: the picture's) -- to the `40` that ends the page.
// Returns X past the `40`, as 64C5 leaves it.
static unsigned creditsPageEnd(const uint8_t* w, unsigned x) {
  for (int guard = 0; guard < 0x10000; guard++) {
    uint8_t b = w[x & 0xFFFF]; x = (x + 1) & 0xFFFF;                  // 64DD LDA $0000,X / INX
    if (b == 0x40) return x;                                          // 64E1 CMP #40 / BEQ -> 64C5
    if (b == 0x00) x = (x + 2) & 0xFFFF;                              // 64E5 .. 64F5: the pen's x and y
  }
  return x;
}

// $7E:9B4F -- THE CREDITS' PAGER (M = 16): $85 pages left, $83 the phase (odd: a page up; even: the gap), $81 the frames
// left in it, $7D / $7F the two lengths. On an odd phase it draws the page at $0382 (64CA) every frame and, once the screen is
// fully lit ($038D = $0F), counts $81 down (at $20 the page's fade-out colours, 9B99); at 0 the next phase, and the page's
// end becomes $0382 -- or, past the last page ($AB1A), the task removes itself. On an even phase it counts the gap and
// starts the next page's fade-in colours (9B94). The colour animation slot it writes is $0520-$0528.
static void creditsColours(uint8_t* w, unsigned x) {                 // 9B9C .. 9BB4 (16-bit stores, in this order)
  wr(w, 0x0527, x); wr(w, 0x0520, 0x0050); wr(w, 0x0521, 0x04F0); wr(w, 0x0523, 0x0008); wr(w, 0x0525, 0x0001);
}
static unsigned task9B4F(uint8_t* w, unsigned t) {
  if (!rd(w, 0x85)) return t;                                         // 9B4F LDA $85 / BNE / RTS
  if (rd(w, 0x83) & 1) {                                              // 9B54 LDA $83 / LSR A / BCC
    unsigned x = creditsPageEnd(w, rd(w, 0x0382));                    // 9B59 LDX $0382 / JSR $64CA
    if ((rd(w, 0x038D) & 0x000F) != 0x000F) return t;                 // 9B5F AND #000F / CMP #000F / BNE
    wr(w, 0x81, rd(w, 0x81) - 1);                                     // 9B6A DEC $81
    if (rd(w, 0x81)) {                                                // 9B6C BNE -> 9B82
      if (rd(w, 0x81) == 0x0020) creditsColours(w, 0x00FF);          // 9B84 CMP #0020 / BEQ -> 9B99
      return t;
    }
    wr(w, 0x85, rd(w, 0x85) - 1);                                     // 9B6E DEC $85
    wr(w, 0x83, rd(w, 0x83) + 1);                                     // 9B70 INC $83
    wr(w, 0x81, rd(w, 0x7D));                                         // 9B72 LDA $7D / STA $81
    if (!(((x - 0xAB1A) & 0xFFFF) & 0x8000)) return 0;                // 9B76 CPX #AB1A / BMI -> 9B7E, else JMP $39A7
    wr(w, 0x0382, x);                                                 // 9B7E STX $0382
    return t;
  }
  wr(w, 0x81, rd(w, 0x81) - 1);                                       // 9B8A DEC $81 / BNE -> RTS
  if (rd(w, 0x81)) return t;
  wr(w, 0x81, rd(w, 0x7F));                                           // 9B8E LDA $7F / STA $81
  wr(w, 0x83, rd(w, 0x83) + 1);                                       // 9B92 INC $83
  creditsColours(w, 0xFF00);                                          // 9B94 LDX #FF00 / BRA 9B9C
  return t;
}

// $7E:9E7B -- "The End"'s rolling text (M = 16, D = 0): every 16th frame (or every frame while $AD is set) it clears the line
// buffer $1414-$1433 and copies the next line of the text at $4A into $141C.. through the WRAM port (`20` a blank, `00 a b`
// skipped, each code less $40), to the `/` that ends it -- $4A past it, $48 the next tilemap row (+$20, in $4800-$4BE0). At
// the `\` that ends the text: `INC $6A`, the hook $9EFC out ($3949), the task out ($39A7). The scroll $46 (9ED6: +8 with
// $AD, else +1 on frames the PPU's field bit `$213F` bit 7 is set) is the picture's (stage 2).
static unsigned task9E7B(uint8_t* w, unsigned t) {
  if (!rd(w, 0xAD) && (rd(w, 0x08) & 0x000F)) return t;              // 9E7B LDA $AD / BNE; LDA $08 / AND #000F / BNE -> 9ED6
  memset(w + 0x1414, 0, 0x20);                                        // 9E86 MVN $00,$D2: 32 zeros into $1414
  unsigned x = rd(w, 0x4A), o = 0x141C;                               // 9E99 $2181 = $141C, 9E9F LDX $4A
  for (int guard = 0; guard < 0x10000; guard++) {
    uint8_t b = w[x & 0xFFFF]; x = (x + 1) & 0xFFFF;                  // 9EA1 LDA $7E0000,X / INX
    if (b == 0x2F) {                                                  // 9EA6 CMP #2F -> 9EC4
      wr(w, 0x4A, x);                                                 // 9EC4 STX $4A
      wr(w, 0x48, (((rd(w, 0x48) + 0x0020) & 0x03E0) + 0x4800) & 0xFFFF);   // 9EC6 REP #21 / ADC #0020 / AND #03E0 / ADC #4800
      return t;
    }
    if (b == 0x5C) {                                                  // 9EAA CMP #5C -> 9EF1
      w[0x6A]++;                                                      // 9EF1 INC $6A
      for (unsigned i = 0; i < 0x1000; i += 2) if (rd(w, 0x0940 + i) == 0x9EFC) { wr(w, 0x0940 + i, 0); break; }   // 3949
      return 0;                                                       // 9EF9 JMP $39A7
    }
    if (b == 0x00) { x = (x + 2) & 0xFFFF; continue; }                // 9EB2 INX / INX
    if (b == 0x20) b = 0x40;                                          // 9EB6 CMP #20 / LDA #40
    w[o & 0x1FFFF] = (uint8_t)(b - 0x40); o++;                        // 9EBC SEC / SBC #40 / STA $2180
  }
  return t;
}

// $7E:9A0C -- the last screen's planet (M = 16, D = 0). Its palette window ($0350-$0367, C3/0EFA) and the sprite it draws
// (7E:3A97) are the picture; what runs it out: $71 falls by 2 a frame from $E0 (with $73 = $79 += 4), then $73 climbs by 1
// and $79 by 3 up to $390; every frame $75 climbs by 1 and $77 by 3, and once $75 reaches $390 the task ends (9A9B `STZ
// $2132 / JMP $39A7`). And on the frames the PPU's field bit is set (9AB1 `SEP #20 / LDA $213F / ASL A`) it turns the planet,
// `DEC $0A04` -- eight bits, page A's r2.
static unsigned task9A0C(uint8_t* w, unsigned t, const CtRaceIo* io) {
  unsigned a;
  if (rd(w, 0x71)) {                                                  // 9A55 LDA $71 / BEQ -> 9A7E
    wr(w, 0x71, rd(w, 0x71) - 2);                                     // 9A6E DEC $71 / DEC $71
    a = (rd(w, 0x73) + 4) & 0xFFFF; wr(w, 0x73, a); wr(w, 0x79, a);  // 9A72 .. 9A7A, BRA 9A94
  } else {
    a = rd(w, 0x73); if (((a - 0x0390) & 0xFFFF) & 0x8000) wr(w, 0x73, a + 1);   // 9A7E CMP #0390 / BPL
    a = rd(w, 0x79); if (((a - 0x0390) & 0xFFFF) & 0x8000) wr(w, 0x79, a + 3);   // 9A88 CMP #0390 / BPL
  }
  a = rd(w, 0x75);
  if (!(((a - 0x0390) & 0xFFFF) & 0x8000)) return 0;                  // 9A96 CMP #0390 / BMI, else STZ $2132 / JMP $39A7
  wr(w, 0x75, a + 1);                                                 // 9AA2 INC A / STA $75
  a = rd(w, 0x77); if (((a - 0x0390) & 0xFFFF) & 0x8000) wr(w, 0x77, a + 3);     // 9AA5 CMP #0390 / BPL
  if (io && io->field && io->field(io->ctx)) w[0x0A04]--;             // 9AB3 LDA $213F / ASL A / BCC / DEC $0A04
  return t;
}

// The vblank hooks' logic. They draw (the DMAs, the PPU, the HDMA tables -- stage 2); what the stage's logic reads from them:
// $7E:8D56 (the last screen's text, added by page A at `F5 08`) points the rolling text at its start -- 8DB7 `$4A = $B099`,
// `$48 = $FFE0` -- sets $038C = 1 (8DB2) and removes itself (8DF0 JMP $39DE).
static unsigned endingHook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) {
  (void)slot; (void)io;
  switch (h) {
    case 0x8D56: w[0x038C] = 0x01; wr(w, 0x4A, 0xB099); wr(w, 0x48, 0xFFE0); return 0;
    default: return h;
  }
}

// The ending's tasks (the logic ones, and the ones above): the two script pages and the overlay's brightness fades. The rest
// draw (stage 2): 37FA, 99B5, 9ABD, 9D48, 9BF9 (the shooting
// stars, on the RNG $039B), 891B, 6439, 8850, ...
static unsigned endingTask(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtSeqScript(w, 0x0A00, io, endingCall); return t;
    case 0x3754: CtSeqScript(w, 0x0B00, io, endingCall); return t;
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);
    case 0x90C1: task90C1(w); return t;
    case 0x992A: task992A(w, io); return t;
    case 0x997D: task997D(w, io); return t;
    case 0x9B4F: return task9B4F(w, t);
    case 0x9E7B: return task9E7B(w, t);
    case 0x9A0C: return task9A0C(w, t, io);
    default: return t;
  }
}

long CtEndingStageSetupCycles(uint8_t operand) {
  if (!CtEndingStageHas(operand)) return 0;
  switch (operand & 0x0F) {
    case 0x0A: return 9736808L;                                       // entry 10: its own backdrop, page A's `F0 8A01`..
    case 0x0B: return 10986252L;                                      // entry 11: page A's `F0 87CC` loads the world
    default:   return 12460664L;                                      // entries 14 / 15: page A's `F0 8790`, `F0 9702`
  }
}

int CtEndingStageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  if (!CtEndingStageHas(operand)) return -1;
  s_songHold = 0;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  unsigned e = operand & 0x0F;                                        // 85D4 / 86B5 LDA $0384 / AND #0F
  // $7E:8538 -- the setup. Its picture (the VRAM packets $3691, the WRAM tables $36AA / $38E7 / C3/09E9, the DMAs, the PPU) is
  // stage 2; what the logic reads follows, in the setup's order.
  w[0x6525] = 0x3F;                                                   // 8552 LDA #3F / STA $7E6525 (a byte of the overlay)
  if (e == 0x0A) {                                                    // 864F LDX #9CE2 / JSR $3974: entry 10's own hook, in
    unsigned x = 0; while (rd(w, 0x0940 + x)) x += 2;                 //   the first free slot of $0940
    wr(w, 0x0940 + x, 0x9CE2);
  }
  memset(w + 0x2200, 0, 0xEF + 1);                                    // 86A1 STZ $2200 / MVN: the CGRAM shadow's first 240 bytes
  unsigned a = 0x9613, b = 0x970F;                                    // 86AF entries 14 and 15
  if (e == 0x0A) { wr(w, 0x0382, 0xA651); a = 0x90CF; b = 0x9341; }   // 86BF entry 10
  else if (e == 0x0B) { wr(w, 0x0382, 0xA651); a = 0x9450; b = 0x97A6; }   // 86D1 entry 11
  wr(w, 0x0A20, a); wr(w, 0x0B20, b);                                 // 86DD STX $0A20 / STY $0B20
  if (CtSeqScript(w, 0x0A00, io, endingCall) < 0) return -1;          // 86E6 JSR $3749 -- page A runs once in the setup
  wr(w, 0x35FD, 0x9B46);                                              // 86E9 LDA #9B46 / STA $35FD (a word of the overlay)
  for (unsigned y = 0; y < 0x90; y += 0x0C) w[0x0520 + y] = 0;        // 86F2 JSL $FD:FFEE -> FD/E275: the colour animations off
  wr(w, 0x81, 0x0001); wr(w, 0x7D, 0x0040); wr(w, 0x7F, 0x0140);     // 86FA .. 8707 (16-bit stores: $81-$82, $7D-$7E, $7F-$80)
  w[0x0396] = 1;                                                      // 8709 LDA #01 / STA $0396: no soft reset while it plays
  w[0x038C] = 0x1F;                                                   // 8717
  wr(w, 0x038E, 0x0101);                                              // 871C LDX #0101 / STX $038E: a fade step every frame
  // $7E:300D .. 301E: $388B (CGRAM), $31B0 (the vblank), JSL C3/08D0 (one NMI, run here), NMI on, STZ $00, STZ $0392
  CtSeqStageNmiHooks(w, 0, io, endingHook, endingTask);
  wr(w, 0x0000, 0); wr(w, 0x0392, 0);
  return 0;
}

int CtEndingStageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, endingHook, endingTask);   // (the NMIs run on over a song's post: CtSeqStageSongTick)
  if (st != 2) s_songHold = CtSeqStageSongTick(w, io, s_songHold);
  return st;
}

void CtEndingStageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }
