// seq_race.c -- THE MOTORCYCLE RACE's own logic (sequence entry 0, `FF 00` / `FF 09` / the attract's `FF 60`), native.
//
// The race runs in the $C3 overlay's TASKS (disasm/notes/sequence_table.md sec. 12); its script only waits (`FD 07 40` at
// $7E:4007) until a task points it at the finish. This file is the tasks, transliterated from the overlay as it sits in WRAM
// ($7E:3000.., decompressed from C3:1313; `tools/wdis.py saves/race_re/race_f300.wram <addr> ...` lists them), over the WRAM
// image itself: the race keeps its state in the direct page (D = $0000 for these tasks), and the transliteration reads and
// writes the same bytes the cartridge does, so every frame can be graded against the oracle byte for byte (tools/
// race_step_cmp.py, `ct.exe --callrom 7E485D --writes`).
//
// ⚠ THE CARRY IS STATE. The 65816 code leans on it across instructions -- `ADC` with no `CLC` before it (4AF9, 4CD2, 4A3A),
// `SBC` after an `ADC` (4C76), `SBC $F2` right after a `ROR` (4AE9), a `JSR` whose callee ends on an `SBC` (4B7B -> 4C3F ->
// 4CD0) -- so the carry is one variable here, threaded through in program order, and every helper sets it the way the
// instruction does. N is derived where a branch reads it (BMI/BPL after LDA/CMP/SBC/ADC: bit 15 of the result, M = 16).
#include "seq_race.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lz.h"       // the overlay: C3:0000 decompresses it with C3/0557
#include "seq_racepic.h"   // the picture (stage 2), when the host draws it (io->pic)
#include "seq_entry4.h"    // CtSeqRng02DD8: C3/02DD, the curtain's seed

typedef struct { uint8_t* w; int c; const CtRaceIo* io; } R;

static unsigned rd(const R* r, unsigned a) { return (unsigned)(r->w[a & 0x1FFFF] | (r->w[(a + 1) & 0x1FFFF] << 8)); }
static void wr(R* r, unsigned a, unsigned v) { r->w[a & 0x1FFFF] = (uint8_t)v; r->w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }
static unsigned adc(R* r, unsigned a, unsigned v) { unsigned s = (a & 0xFFFF) + (v & 0xFFFF) + (unsigned)r->c; r->c = s > 0xFFFF; return s & 0xFFFF; }
static unsigned sbc(R* r, unsigned a, unsigned v) { int s = (int)(a & 0xFFFF) - (int)(v & 0xFFFF) - (1 - r->c); r->c = s >= 0; return (unsigned)s & 0xFFFF; }
static int cmpn(R* r, unsigned a, unsigned v) { r->c = (a & 0xFFFF) >= (v & 0xFFFF); return (((a - v) & 0xFFFF) & 0x8000) != 0; }   // -> N
static unsigned lsr(R* r, unsigned a) { r->c = (int)(a & 1); return (a & 0xFFFF) >> 1; }
static unsigned ror(R* r, unsigned a) { int nc = (int)(a & 1); a = ((a & 0xFFFF) >> 1) | ((unsigned)r->c << 15); r->c = nc; return a; }
static unsigned asl(R* r, unsigned a) { r->c = (int)((a >> 15) & 1); return (a << 1) & 0xFFFF; }
static int neg(unsigned v) { return (v & 0x8000) != 0; }

// The task-list library (C3 overlay): $3934 removes X from the task list $0920, $395E adds it at the first free slot,
// $3974 adds a vblank hook at the first free slot of $0940. Each is `PHP ... PLP`: the caller's flags survive the call.
// ⚠ NONE OF THE THREE HAS A BOUND: `LDA $0920,X / BEQ / INX / INX / BRA` walks on past the list's sixteen slots until it meets
// a zero (the task list runs into the hook list at $0940, the hook list into $0960). Kept; the cap only stops a runaway.
static void taskDel(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (rd(r, 0x0920 + i) == x) { wr(r, 0x0920 + i, 0); return; } }
static void taskAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (!rd(r, 0x0920 + i)) { wr(r, 0x0920 + i, x); return; } }
static void vblAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (!rd(r, 0x0940 + i)) { wr(r, 0x0940 + i, x); return; } }

// $4C3F / $4C84: a bump from behind / ahead -- the two positions pushed apart, then $4CC7 swaps the speeds.
static void bumpSwap(R* r) { unsigned x = rd(r, 0x4A); wr(r, 0x4A, rd(r, 0x6F)); wr(r, 0x6F, x); }   // 4CC7 LDX $4A / LDA $6F / STA $4A / STX $6F
static void bump4C3F(R* r) {
  r->c = 1; wr(r, 0xF0, sbc(r, rd(r, 0x51), 0x0100));                 // 4C3F LDA $51 / SEC / SBC #0100 / STA $F0
  wr(r, 0xF2, sbc(r, rd(r, 0x53), 0));                                // 4C47 LDA $53 / SBC #0000 / STA $F2
  r->c = 0; wr(r, 0xF6, adc(r, rd(r, 0x76), 0x00C0));                 // 4C4E LDA $76 / CLC / ADC #00C0 / STA $F6
  wr(r, 0xF8, adc(r, rd(r, 0x78), 0));                                // 4C56
  r->c = 1; wr(r, 0xF6, sbc(r, rd(r, 0xF6), rd(r, 0xF0)));            // 4C5D LDA $F6 / SEC / SBC $F0
  wr(r, 0xF8, sbc(r, rd(r, 0xF8), rd(r, 0xF2)));                      // 4C64 LDA $F8 / SBC $F2
  wr(r, 0x51, adc(r, rd(r, 0x51), rd(r, 0xF6)));                      // 4C6A LDA $51 / ADC $F6 (the SBC's carry)
  wr(r, 0x53, adc(r, rd(r, 0x53), rd(r, 0xF8)));                      // 4C70
  wr(r, 0x76, sbc(r, rd(r, 0x76), rd(r, 0xF8)));                      // 4C76 LDA $76 / SBC $F8 -- $F8, as the ROM has it
  wr(r, 0x78, sbc(r, rd(r, 0x78), rd(r, 0xF8)));                      // 4C7C
  bumpSwap(r);                                                        // 4C82 BRA ->4CC7
}
static void bump4C84(R* r) {
  r->c = 1; wr(r, 0xF0, sbc(r, rd(r, 0x76), 0x00C0));                 // 4C84 LDA $76 / SEC / SBC #00C0
  wr(r, 0xF2, sbc(r, rd(r, 0x78), 0));
  r->c = 0; wr(r, 0xF6, adc(r, rd(r, 0x51), 0x0100));                 // 4C93 LDA $51 / CLC / ADC #0100
  wr(r, 0xF8, adc(r, rd(r, 0x53), 0));
  r->c = 1; wr(r, 0xF6, sbc(r, rd(r, 0xF6), rd(r, 0xF0)));            // 4CA2
  wr(r, 0xF8, sbc(r, rd(r, 0xF8), rd(r, 0xF2)));
  wr(r, 0x76, adc(r, rd(r, 0x76), rd(r, 0xF6)));                      // 4CAF LDA $76 / ADC $F6
  wr(r, 0x78, adc(r, rd(r, 0x78), rd(r, 0xF8)));
  wr(r, 0x51, sbc(r, rd(r, 0x51), rd(r, 0xF8)));                      // 4CBB LDA $51 / SBC $F8
  wr(r, 0x53, sbc(r, rd(r, 0x53), rd(r, 0xF8)));
  bumpSwap(r);
}
// $4CD0 / $4CE7: a bump from the side -- the lateral positions pushed apart, then $4CFC swaps the lateral speeds.
static void sideSwap(R* r) {
  unsigned x = rd(r, 0x67); wr(r, 0x67, rd(r, 0x8C)); wr(r, 0x8C, x);  // 4CFC LDX $67 / LDA $8C / STA $67 / STX $8C
  x = rd(r, 0x65); wr(r, 0x65, rd(r, 0x8A)); wr(r, 0x8A, x);           // 4D04 LDX $65 / LDA $8A / STA $65 / STX $8A
}
static void side4CD0(R* r) {
  unsigned f = adc(r, rd(r, 0x4E), 0x1000); f = sbc(r, f, rd(r, 0x73)); wr(r, 0xF0, f);   // 4CD0 LDA $4E / ADC #1000 / SBC $73
  wr(r, 0x4E, sbc(r, rd(r, 0x4E), rd(r, 0xF0)));                      // 4CD9 LDA $4E / SBC $F0
  wr(r, 0x73, adc(r, rd(r, 0x73), rd(r, 0xF0)));                      // 4CDF LDA $73 / ADC $F0
  sideSwap(r);
}
static void side4CE7(R* r) {
  unsigned f = adc(r, rd(r, 0x73), 0x1000); f = sbc(r, f, rd(r, 0x4E)); wr(r, 0xF0, f);   // 4CE7 LDA $73 / ADC #1000 / SBC $4E
  wr(r, 0x4E, adc(r, rd(r, 0x4E), rd(r, 0xF0)));                      // 4CF0 LDA $4E / ADC $F0
  wr(r, 0x73, sbc(r, rd(r, 0x73), rd(r, 0xF0)));                      // 4CF6 LDA $73 / SBC $F0
  sideSwap(r);
}

// $7E:4788 -- THE PLAYER CROSSED THE LINE FIRST ($53 == 8). Returns the task that replaces this one (39B1).
static unsigned win4788(R* r) {
  wr(r, 0x0380, 1);                                                   // 4788 LDA #0001 / STA $0380
  wr(r, 0xB2, rd(r, 0x8E) & 0x00FF);                                  // 478E
  if ((r->w[0x0384] & 0x0F) == 0) { wr(r, 0x55, 0x7000); wr(r, 0x59, 0x7000); }   // 4795 ... the attract's call
  else { wr(r, 0x7A, 0x4000); wr(r, 0x7E, 0x4000); }                  // 47A6 the story's: Johnny slows
  wr(r, 0x0B0C, 0x0FFF);                                              // 47B0
  wr(r, 0x038E, 0x0505);                                              // 47B6
  unsigned a = lsr(r, rd(r, 0x69));                                   // 47BC LDA $69 / LSR A / STA $0382
  wr(r, 0x0382, a);
  int rank = 0;                                                       // 47C2..: the jackpots
  if (a == 0x0309) rank = 2;
  else if (a == 0x007B || a == 0x04D2) rank = 3;
  else if (a == 0x03E8 || a == 0x07D0 || a == 0x0BB8) rank = 4;
  else {                                                              // 47EF LDX #0016 / CMP $7E4770,X / DEX DEX / BNE
    for (unsigned x = 0x16; x; x -= 2) if (rd(r, 0x4770 + x) == a) { rank = 1; break; }
  }
  if (rank) r->w[0x0381] = (uint8_t)rank;                             // 4801 SEP #20 / STA $0381
  r->w[0x038A] = 0x1B;                                                // 4806 LDA #1B / STA $038A
  unsigned t = rd(r, 0x0382); uint8_t bits = 0;                       // 480D TDC / LDX $0382 / CPX ...
  if (!neg(t - 0x0514)) { bits = 0x20; if (!neg(t - 0x05DC)) { bits |= 0x40; if (!neg(t - 0x07D0)) bits |= 0x80; } }
  r->w[0x0381] |= bits;                                               // 4826 TSB $0381
  taskDel(r, 0x4339);                                                 // 482B the steering task
  taskAdd(r, 0x4755);                                                 // 4831
  wr(r, 0xA2, rd(r, 0xA2) + 1);                                       // 4837 INC $A2
  wr(r, 0xB0, rd(r, 0xB0) + 1);                                       // 4839 INC $00B0
  wr(r, 0x0390, 0); wr(r, 0x65, 0); wr(r, 0x67, 0);                   // 483C-4841
  if (!cmpn(r, rd(r, 0x4E), rd(r, 0x73))) {                           // 4843 LDA $4E / CMP $73 / BMI
    wr(r, 0x46CE, 0x6000); wr(r, 0x46E4, 0x2000);                     // 4849 the follow-up task's own operands
  }
  return 0x4677;                                                      // 4857 LDX #4677 / JMP $39B1
}

unsigned CtRaceStep485D(uint8_t* w, const CtRaceIo* io) {
  R rr = { w, 0, io }, *r = &rr;
  wr(r, 0xBC, rd(r, 0xBC) + 1);                                       // 485D INC $BC -- the race clock
  if (rd(r, 0x53) == 0x0008) { r->c = 1; return win4788(r); }         // 485F LDA $53 / CMP #0008 / BNE / JMP $4788
  if (rd(r, 0x78) == 0x0008) {                                        // 4869 LDA $78 / CMP #0008 -- Johnny first
    r->c = 1;                                                         //   (CMP equal: C = 1, kept across $3934's PHP/PLP)
    wr(r, 0x0380, 2);                                                 // 4870
    taskDel(r, 0x4339);                                               // 4876
    w[0x510D] = 0x60;                                                 // 487C SEP #20 / LDA #60 / STA $7E510D
    unsigned a = adc(r, rd(r, 0x4A), rd(r, 0x6F));                    // 4886 LDA $4A / ADC $6F (C = 1)
    a = ((a >> 8) & 0xFF) >> 3;                                       // XBA / AND #00FF / LSR x3
    wr(r, 0x0A12, a);                                                 // 4891
    vblAdd(r, 0x449C);                                                // 4894
    taskAdd(r, 0x44CE);                                               // 489A
    wr(r, 0x0A20, 0x4211);                                            // 48A0 the script, at the finish
    return 0;                                                         // 48A6 JMP $39A7 -- this task ends
  }
  // 48A9 THE HUD'S TWO MARKERS on the camera's middle point ($9C/$9E)
  if (!rd(r, 0x9E) && !cmpn(r, rd(r, 0x9C), 0x1000) && rd(r, 0xA2)) { wr(r, 0xA2, 0); vblAdd(r, 0x464D); }
  if (rd(r, 0x9E) == 7) {                                             // 48C0 LDA $9E / CMP #0007 / BNE
    r->c = 1;
    sbc(r, rd(r, 0x9C), 0xF200);                                      // 48C7 LDA $9C / SBC #F200 / BCC
    if (r->c && !rd(r, 0xA2)) { wr(r, 0xA2, rd(r, 0xA2) + 1); vblAdd(r, 0x4643); }
  } else cmpn(r, rd(r, 0x9E), 7);
  // 48DA THE LEAD: $63 = $88 = 1 when the player is $1000 or more ahead
  wr(r, 0x88, 0); wr(r, 0x63, 0);
  r->c = 1; wr(r, 0xF0, sbc(r, rd(r, 0x51), rd(r, 0x76)));
  wr(r, 0xF2, sbc(r, rd(r, 0x53), rd(r, 0x78)));
  if (!cmpn(r, rd(r, 0xF2), 0) && !cmpn(r, rd(r, 0xF0), 0x1000)) { wr(r, 0x63, rd(r, 0x63) + 1); wr(r, 0x88, rd(r, 0x88) + 1); }
  // 48FD THE SPEEDS: drag $80 a frame, then the engine
  { r->c = 1; unsigned a = sbc(r, rd(r, 0x4A), 0x80); if (neg(a)) a = 0; wr(r, 0x4A, a); }
  { r->c = 1; unsigned a = sbc(r, rd(r, 0x6F), 0x80); if (neg(a)) a = 0; wr(r, 0x6F, a); }
  r->c = 0; wr(r, 0x4A, adc(r, rd(r, 0x4A), rd(r, 0x4C)));            // 4913 LDA $4A / CLC / ADC $4C
  if (rd(r, 0x98) & 0x8000) { r->c = 0; wr(r, 0x6F, adc(r, rd(r, 0x6F), rd(r, 0x71))); }   // 491A Johnny once started
  wr(r, 0x4C, 0x0100); wr(r, 0x71, 0x00F8);                           // 4928
  if (cmpn(r, rd(r, 0x57), rd(r, 0x4A))) wr(r, 0x4C, 0);              // 4932 LDA $57 / CMP $4A / BPL
  if (cmpn(r, rd(r, 0x7C), rd(r, 0x6F))) wr(r, 0x71, 0);              // 493A
  // 4942 THE BOOST (B), only once the race runs ($B0 == 0)
  if (!rd(r, 0xB0)) {
    if (rd(r, 0xBA)) {                                                // 4946 a boost is burning
      if (rd(r, 0x9A) && !(rd(r, 0xBA) & 7)) {                        // 494A ... every eighth frame, the flash
        uint8_t v = w[0xE111]; r->c = 1; v = (uint8_t)((v >> 1) | 0x80);   // 495B LDA $E111 / SEC / ROR A (8-bit)
        r->c = w[0xE111] & 1; w[0xE111] = v; w[0xE113] = v;
        uint8_t u = w[0xE131]; int nc = u & 1; u = (uint8_t)((u >> 1) | (r->c << 7)); r->c = nc;   // 4966 LDA $E131 / ROR A
        w[0xE131] = u; w[0xE133] = u;
      }
      wr(r, 0xBA, rd(r, 0xBA) - 1);                                   // 4973 DEC $BA
    } else if (rd(r, 0x06) & 0x8000) {                                // 4977 LDA $06 / BIT #8000 -- B
      if (!rd(r, 0xA0)) {                                             // 497E the press's edge
        wr(r, 0xA0, rd(r, 0xA0) + 1);
        if (rd(r, 0x9A)) {                                            // 4984 a boost left
          wr(r, 0x9A, rd(r, 0x9A) - 1);
          wr(r, 0xBA, 0x0080);                                        // 498B 128 frames
          w[0xE111] = 0; w[0xE113] = 0; w[0xE131] = 0; w[0xE133] = 0; // 4996 the flash's start
          unsigned a = adc(r, rd(r, 0x4A), 0x1700);                   // 49A5 LDA $4A / ADC #1700 (the carry as it stands)
          if (neg(a)) a = 0x7FFF;
          wr(r, 0x4A, a);
        }
      }
    } else wr(r, 0xA0, 0);                                            // 49B3 STZ $A0
  }
  // 49B5 THE TOP SPEEDS: whoever is behind ($8E's sign) gets the higher one
  wr(r, 0x57, rd(r, 0x55)); wr(r, 0x7C, rd(r, 0x7A));
  if (!neg(rd(r, 0x8E))) wr(r, 0x7C, rd(r, 0x7E)); else wr(r, 0x57, rd(r, 0x59));
  // 49CB THE CAMERA: the middle of the two positions, and BG's $0A02 from it
  r->c = 0; wr(r, 0x9C, adc(r, rd(r, 0x51), rd(r, 0x76)));
  { unsigned hi = adc(r, rd(r, 0x53), rd(r, 0x78)); hi = lsr(r, hi); wr(r, 0x9E, hi); }
  { unsigned a = ror(r, rd(r, 0x9C)); wr(r, 0x9C, a);
    a = lsr(r, a); a = lsr(r, a); a = lsr(r, a); a = lsr(r, a);
    r->c = 1; a = adc(r, a, 0x0374); wr(r, 0x0A02, a & 0x03FF); }
  // 49EC JOHNNY'S STEERING ($98: bit 15 = he runs, $0100 / $0200 = steer one way / the other) and $69 (the "W.P.")
  wr(r, 0x98, 0);
  if (!rd(r, 0xB0)) {
    wr(r, 0x98, 0x8000);
    unsigned steer = 0;
    unsigned e8 = rd(r, 0x8E);
    if (e8 && !neg(e8)) {                                             // 49F7 the player ahead
      wr(r, 0x69, rd(r, 0x69) + 2);                                   // 49FD INC $69 x2
      r->c = 1; unsigned d = sbc(r, rd(r, 0x4F), rd(r, 0x74));
      if (neg(d)) { r->c = 1; d = sbc(r, rd(r, 0x74), rd(r, 0x4F)); }
      if (cmpn(r, d, 0x0020)) {                                       // 4A0D CMP #0020 / BPL -- close sideways
        steer = cmpn(r, rd(r, 0x4F), 0x0040) ? 0x0200 : 0x0100;       // 4A12
      }
    } else {
      wr(r, 0x69, rd(r, 0x69) - 1);                                   // 4A1E DEC $69
      steer = cmpn(r, rd(r, 0x4E), rd(r, 0x73)) ? 0x0100 : 0x0200;    // 4A20 LDA $4E / CMP $73 / BMI ->4A19
    }
    if (steer) wr(r, 0x98, rd(r, 0x98) | steer);                      // 4A29 TSB $98
  }
  // 4A2B THE PLAYER'S STEERING: the pad against the camera's two masks $B8 / $B6 (task $4339)
  wr(r, 0xAE, 0);
  if (rd(r, 0x06) & rd(r, 0xB8)) {
    wr(r, 0xAE, 1);
    unsigned a = adc(r, rd(r, 0x65), 0x0028); if (!cmpn(r, a, 0x0210)) a = 0x0210; wr(r, 0x65, a);
  }
  { unsigned a = sbc(r, rd(r, 0x65), 0x0010); if (neg(a)) a = 0; wr(r, 0x65, a); }
  if (rd(r, 0x06) & rd(r, 0xB6)) {
    wr(r, 0xAE, 2);
    unsigned a = adc(r, rd(r, 0x67), 0x0028); if (!cmpn(r, a, 0x0210)) a = 0x0210; wr(r, 0x67, a);
  }
  { unsigned a = sbc(r, rd(r, 0x67), 0x0010); if (neg(a)) a = 0; wr(r, 0x67, a); }
  if (rd(r, 0x98) & 0x0200) { unsigned a = adc(r, rd(r, 0x8A), 0x0018); if (!cmpn(r, a, 0x0210)) a = 0x0210; wr(r, 0x8A, a); }
  { unsigned a = sbc(r, rd(r, 0x8A), 0x0010); if (neg(a)) a = 0; wr(r, 0x8A, a); }
  if (rd(r, 0x98) & 0x0100) { unsigned a = adc(r, rd(r, 0x8C), 0x0018); if (!cmpn(r, a, 0x0210)) a = 0x0210; wr(r, 0x8C, a); }
  { unsigned a = sbc(r, rd(r, 0x8C), 0x0010); if (neg(a)) a = 0; wr(r, 0x8C, a); }
  // 4ABD $8E = (player - Johnny) >> 5, through $F0 / $F2
  { wr(r, 0xF0, rd(r, 0x78)); unsigned a = rd(r, 0x76);
    for (int k = 0; k < 5; k++) { wr(r, 0xF0, lsr(r, rd(r, 0xF0))); a = ror(r, a); }
    wr(r, 0xF2, a);
    wr(r, 0xF0, rd(r, 0x53)); a = rd(r, 0x51);
    for (int k = 0; k < 5; k++) { wr(r, 0xF0, lsr(r, rd(r, 0xF0))); a = ror(r, a); }
    wr(r, 0x8E, sbc(r, a, rd(r, 0xF2))); }                           // 4AE9 SBC $F2 -- the last ROR's carry
  if (!rd(r, 0xB0)) {
    // 4AF4 THE COLLISION: $A4 = (sideways relation << 4) | (lengthwise relation); 0 = touching on both axes
    wr(r, 0xA4, 0);
    wr(r, 0xF0, adc(r, 0x0100, rd(r, 0x51)));
    wr(r, 0xF2, adc(r, 0x00C0, rd(r, 0x76)));
    { unsigned a = adc(r, rd(r, 0xF2), 0x0200);
      if (!cmpn(r, a, rd(r, 0xF0))) {
        a = sbc(r, rd(r, 0xF2), 0x0180);
        if (!cmpn(r, a, rd(r, 0xF0))) { wr(r, 0xA4, rd(r, 0xA4) + 1); wr(r, 0xA4, rd(r, 0xA4) + 1); }
      } else wr(r, 0xA4, rd(r, 0xA4) + 1); }
    wr(r, 0xA6, 0);
    { r->c = 1; unsigned a = sbc(r, rd(r, 0x73), 0x1000);
      if (!cmpn(r, a, rd(r, 0x4E))) wr(r, 0xA6, rd(r, 0xA6) + 1);    // 4B22 BPL ->4B31
      else { a = adc(r, rd(r, 0x73), 0x1000); int n = cmpn(r, a, rd(r, 0x4E));
             if (!(a == rd(r, 0x4E)) && n) wr(r, 0xA6, rd(r, 0xA6) + 2); } }   // 4B2B BEQ / BPL -> none, else INC x2
    { unsigned a = rd(r, 0xA6); a = asl(r, a); a = asl(r, a); a = asl(r, a); a = asl(r, a);
      a |= rd(r, 0xA4); wr(r, 0xA4, a);
      int doSnd = 0;
      if (a) { wr(r, 0xA8, a); }                                      // 4B3F the side they came from
      else {                                                          // 4B46 touching: bump by $A8
        unsigned s = rd(r, 0xA8);
        if (s == 0x01) { cmpn(r, s, 1); bump4C3F(r); r->c = 0; wr(r, 0x69, adc(r, rd(r, 0x69), 0x0014)); }
        else if (s == 0x02) { cmpn(r, s, 2); bump4C84(r); r->c = 1; wr(r, 0x69, sbc(r, rd(r, 0x69), 0x000A)); }
        else if (s == 0x10) { cmpn(r, s, 0x10); side4CD0(r); }
        else if (s == 0x11) { cmpn(r, s, 0x11); bump4C3F(r); side4CD0(r); }
        else if (s == 0x12) { cmpn(r, s, 0x12); side4CD0(r); bump4C84(r); }
        else if (s == 0x20) { cmpn(r, s, 0x20); side4CE7(r); }
        else if (s == 0x21) { cmpn(r, s, 0x21); bump4C3F(r); side4CE7(r); }
        else if (s == 0x22) { cmpn(r, s, 0x22); side4CE7(r); bump4C84(r); }
        else cmpn(r, s, 0x22);
        doSnd = 1;
      }
      if (doSnd && !w[0x0388]) {                                      // 4BB2 the bump's sound: $1E02 = 80, $1E01 = 98, $1E00 = 19
        w[0x1E02] = 0x80; w[0x1E01] = 0x98; w[0x1E00] = 0x19;           //   and JSR $377E (the mailbox to $2140-$2143)
        if (r->io && r->io->sfx) r->io->sfx(r->io->ctx, 0x19, 0x98, 0x80);
      }
    }
    if (!w[0x0388] && r->io && r->io->apu) r->io->apu(r->io->ctx, w[0x4B]);    // 4BCB $2141 = $4B, $2140 = $89: the engine
  }
  // 4BDE THE POSITIONS: speed >> 6 a frame (and the lateral ones, clamped to 0..$7000)
  r->c = 0; wr(r, 0x51, adc(r, rd(r, 0x51), (rd(r, 0x4A) >> 6) & 0x01FF));
  wr(r, 0x53, adc(r, rd(r, 0x53), 0));
  r->c = 0; wr(r, 0x76, adc(r, rd(r, 0x76), (rd(r, 0x6F) >> 6) & 0x01FF));
  wr(r, 0x78, adc(r, rd(r, 0x78), 0));
  { r->c = 0; unsigned a = adc(r, rd(r, 0x4E), rd(r, 0x65)); if (!cmpn(r, a, 0x7000)) a = 0x7000; wr(r, 0x4E, a);
    r->c = 1; a = sbc(r, rd(r, 0x4E), rd(r, 0x67)); if (neg(a)) a = 0; wr(r, 0x4E, a); }
  { r->c = 0; unsigned a = adc(r, rd(r, 0x73), rd(r, 0x8A)); if (!cmpn(r, a, 0x7000)) a = 0x7000; wr(r, 0x73, a);
    r->c = 1; a = sbc(r, rd(r, 0x73), rd(r, 0x8C)); if (neg(a)) a = 0; wr(r, 0x73, a); }
  return 0x485D;                                                      // 4C3E RTS: the task stays
}

// $7E:4339 -- THE CAMERA AND THE STEERING MASKS (D = $0A00, set by the task itself: `LDA #0A00 / TCD`).
//   4339  on the story's call (`$0044` != 0, `FF 09`'s INC / the attract leaves it 0): the pad `$0006` through $43B8 (R,
//         bit 4: $0A3A up to 12, else down to 0) and $43D5 (L, bit 5: $0A3C), then the angle `$0A0A += ($3A >> 2) - ($3C >> 2)`
//   4361  the masks: X = (($0A0A + $40) >> 6) & $0E; `$00B6 = $7E:4319,X`, `$00B8 = $7E:4329,X` -- which d-pad bit steers
//         which way for the camera's quarter (the tables are the overlay's own words)
//   4380  every second frame (`$0008` bit 0): X (bit 6) raises `$0A00/$0A04/$0A0C` while `$0A00 < $7F`; else A (bit 7)
//         lowers them while `$0A00 >= $60` -- the camera's height, which nothing in the race reads
void CtRaceTask4339(uint8_t* w) {
  R rr = { w, 0, 0 }, *r = &rr;
  if (rd(r, 0x0044)) {
    unsigned pad = rd(r, 0x0006);
    { unsigned x = rd(r, 0x0A3A);                                     // 43B8
      if (pad & 0x0010) { x++; if (!(x < 0x000C)) x = 0x000C; } else if (x) x--;
      wr(r, 0x0A3A, x); }
    { unsigned x = rd(r, 0x0A3C);                                     // 43D5
      if (pad & 0x0020) { x++; if (!(x < 0x000C)) x = 0x000C; } else if (x) x--;
      wr(r, 0x0A3C, x); }
    unsigned a = lsr(r, rd(r, 0x0A3A)); a = lsr(r, a); wr(r, 0x0AF0, a);   // 434B LDA $3A / LSR / LSR / STA $F0
    a = lsr(r, rd(r, 0x0A3C)); a = lsr(r, a); wr(r, 0x0AF2, a);
    r->c = 0; a = adc(r, rd(r, 0x0A0A), rd(r, 0x0AF0));                // 4357 LDA $0A / CLC / ADC $F0
    r->c = 1; a = sbc(r, a, rd(r, 0x0AF2));                            //      SEC / SBC $F2
    wr(r, 0x0A0A, a);
  }
  { r->c = 0; unsigned a = adc(r, rd(r, 0x0A0A), 0x0040);              // 4361
    for (int k = 0; k < 6; k++) a = lsr(r, a);
    unsigned x = a & 0x000E;
    wr(r, 0x00B6, rd(r, 0x4319 + x)); wr(r, 0x00B8, rd(r, 0x4329 + x)); }
  unsigned pad = rd(r, 0x0006);
  if (pad & 0x0040) {                                                 // 4380 BIT #0040 -- X
    if (rd(r, 0x0008) & 1) {                                          // LDA $0008 / LSR / BCC
      if (!cmpn(r, rd(r, 0x0A00), 0x007F)) return;                    // CMP #007F / BPL -> RTS
      wr(r, 0x0A04, rd(r, 0x0A04) + 1); wr(r, 0x0A00, rd(r, 0x0A00) + 1); wr(r, 0x0A0C, rd(r, 0x0A0C) + 1);
      return;
    }
  }
  if (pad & 0x0080) {                                                 // 439C BIT #0080 -- A
    if (rd(r, 0x0008) & 1) {
      if (cmpn(r, rd(r, 0x0A00), 0x0060)) return;                     // CMP #0060 / BMI -> RTS
      wr(r, 0x0A04, rd(r, 0x0A04) - 1); wr(r, 0x0A00, rd(r, 0x0A00) - 1); wr(r, 0x0A0C, rd(r, 0x0A0C) - 1);
    }
  }
}

// ---- THE $C3 SCRIPT INTERPRETER (C3/0AFF -> C3/0B03), over its register page in WRAM -------------------------------------
// The same machine title_intro.c runs for the title (sequence_table.md sec. 8.1), here over the page itself, because the
// race's tasks read the registers straight from WRAM ($0A0A, $0A00, $0B1A, ...) and the race's own task writes the IP ($0A20).
// Encoding (C3/0B03): bit 7 clear = an assignment (low nibble = the register; high nibble with bit 5 clear = a 16-bit
// immediate, set when it is 0 and added otherwise; bit 5 set = a register named by the next byte, set when $20 and added
// otherwise); $8n / $9n = loop n; anything else dispatches on its low nibble through C3:0B74.
static unsigned pg16(const R* r, unsigned page, unsigned off) { return rd(r, page + off); }
static void pgw16(R* r, unsigned page, unsigned off, unsigned v) { wr(r, page + off, v); }

// $7E:3F59 -- the race's one inline call: `JSR $3448 / AND #0002 / TAX / LDA $3F66,X / STA $20 / RTS`. $3448 is the overlay's
// RNG: `$039B` + 1 (8-bit), then the byte at $C0:FE00 + it. Which camera intro page A plays.
static void call3F59(R* r, unsigned page, const CtRaceIo* io) {
  uint8_t c = (uint8_t)(r->w[0x039B] + 1);
  r->w[0x039B] = c;
  unsigned v = (io && io->rom && io->romLen > 0xFE00 + c) ? io->rom[0xFE00 + c] : 0;
  unsigned x = v & 0x0002;
  pgw16(r, page, 0x20, rd(r, 0x3F66 + x));
}

static int raceCall(uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  R rr = { w, 0, io };
  if (tgt == 0x3F59) { call3F59(&rr, page, io); return 0; }
  return -1;
}

int CtRaceScript(uint8_t* w, unsigned page, const CtRaceIo* io) { return CtSeqScript(w, page, io, raceCall); }

int CtSeqScript(uint8_t* w, unsigned page, const CtRaceIo* io, CtSeqCall call) {
  R rr = { w, 0, io }, *r = &rr;
  for (int guard = 0; guard < 4096; guard++) {
    unsigned ip = pg16(r, page, 0x20);
    unsigned a = rd(r, ip);                                              // 0B03 LDA ($20) -- sixteen bits
    pgw16(r, page, 0x20, ip + 1);                                        // 0B05 INC $20
    if (!(a & 0x0080)) {                                                 // an assignment
      unsigned reg = page + (a & 0x000F) * 2;                            // 0B0C .. 0B16 $F0 = D + n * 2
      pgw16(r, page, 0xF0, reg);
      unsigned hi = a & 0x00F0, v;
      ip = pg16(r, page, 0x20);
      if (!(hi & 0x0020)) {
        v = (hi == 0) ? rd(r, ip) : (rd(r, reg) + rd(r, ip)) & 0xFFFF;   // 0B26 / 0B2A
        pgw16(r, page, 0x20, ip + 2);                                    // 0B4D INC $20 / 0B4F INC $20
      } else {
        unsigned x = rd(r, ip) & 0x001E;                                 // 0B36 / 0B40 `LDA ($20) / AND #001E / TAX`
        v = (hi == 0x20) ? rd(r, page + x) : (rd(r, reg) + rd(r, page + x)) & 0xFFFF;
        pgw16(r, page, 0x20, ip + 1);                                    // 0B4F
      }
      wr(r, reg, v);                                                     // 0B51 STA ($F0)
      continue;
    }
    pgw16(r, page, 0xF0, a);                                             // 0B55 STA $F0
    unsigned hi = a & 0x00F0;
    if (hi == 0x0080 || hi == 0x0090) {                                  // C3/0C92, the loop counters
      unsigned n = (a & 0x000F) * 2, ctr = pg16(r, page, 0x22 + n);
      ip = pg16(r, page, 0x20);
      if (ctr && !neg(ctr - rd(r, ip))) {                                // 0C9B BEQ / CMP ($20) / BMI
        pgw16(r, page, 0x22 + n, 0);
        pgw16(r, page, 0x20, ip + 4);                                    // 0CA3 past the limit and the target
      } else {
        pgw16(r, page, 0x22 + n, ctr + 1);                               // 0CAC INC A / STA $22,X
        pgw16(r, page, 0x20, rd(r, ip + 2));                             // 0CAF the target
      }
      if (hi == 0x0090) return 0;                                        // $9n: C3/0B67 JMP $0C92 -- a yield
      continue;                                                          // $8n: C3/0C8C, on to the next op
    }
    ip = pg16(r, page, 0x20);
    switch (a & 0x000F) {
      case 0xF:                                                          // 0B95 END: DEC $20 / $7E:0000 = 0001
        pgw16(r, page, 0x20, ip - 1); wr(r, 0x0000, 0x0001); return 0;
      case 0xE: pgw16(r, page, 0x20, rd(r, ip)); continue;               // 0B9F JUMP
      case 0xD: pgw16(r, page, 0x20, rd(r, ip)); return 0;               // 0BA6 JUMP and yield
      case 0xC: return 0;                                                // 0BAA yield
      case 0xA: taskAdd(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;   // 0BAB -> C3/0CE2
      case 0x9: taskDel(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;   // 0BBC -> C3/0CB8
      case 0x8: vblAdd(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;    // 0BC4 -> C3/0CF8
      case 0x7: {                                                        // 0BCC -> C3/0CCD: the hook out of $0940
        unsigned x = rd(r, ip);
        for (unsigned i = 0; i < 0x1000; i += 2) if (rd(r, 0x0940 + i) == x) { wr(r, 0x0940 + i, 0); break; }
        pgw16(r, page, 0x20, ip + 2); continue;
      }
      case 0x6:                                                          // 0BD4 the sound command
        wr(r, 0x1E00, rd(r, ip)); wr(r, 0x1E02, rd(r, ip + 2));
        pgw16(r, page, 0x20, ip + 4);
        if (!w[0x0388] && io && io->post) io->post(io->ctx);           // 0BE4 LDA $0388 / BNE / JSL C7/0004
        continue;
      case 0x1:                                                          // 0C19 a byte into bank 0 (the low-RAM mirror)
        w[rd(r, ip) & 0x1FFF] = w[(ip + 2) & 0xFFFF];
        pgw16(r, page, 0x20, ip + 3); continue;
      case 0x2:                                                          // 0C41 a word into bank 0
        wr(r, rd(r, ip) & 0x1FFF, rd(r, ip + 2));
        pgw16(r, page, 0x20, ip + 4); continue;
      case 0x3:                                                          // 0C7C GOSUB: $30 = IP + 2, then JUMP
        pgw16(r, page, 0x30, ip + 2); pgw16(r, page, 0x20, rd(r, ip)); continue;
      case 0x4: pgw16(r, page, 0x20, pg16(r, page, 0x30)); continue;     // 0C85 RETURN
      case 0x5: {                                                        // 0C2A WAIT FOR THE SONG: port 2's low nibble
        if (!io || !io->port) { pgw16(r, page, 0x20, ip - 1); return 0; }   //   (no sound processor: the marker never comes)
        uint8_t v = (uint8_t)(io->port(io->ctx, 2) & 0x0F);             // 0C2C LDA $2142 (read until stable) / AND #0F
        if ((int8_t)(uint8_t)(v - w[ip & 0xFFFF]) >= 0) {                 // 0C38 CMP ($20) / BPL -> 0C28 -> 0BB5
          pgw16(r, page, 0x20, ip + 1); continue;
        }
        pgw16(r, page, 0x20, ip - 1); return 0;                          // 0C3E DEC $20 / RTS: again next frame
      }
      case 0xB:                                                          // 0C5F a byte into the PPU: $21[ip+1] = [ip]
        if (io && io->ppu) io->ppu(io->ctx, w[(ip + 1) & 0xFFFF], w[ip & 0xFFFF]);
        if (io && io->pic) CtSeqPicWrite(&io->pic->p, w[(ip + 1) & 0xFFFF], w[ip & 0xFFFF], 0);
        pgw16(r, page, 0x20, ip + 2); continue;
      case 0x0: {                                                        // 0C51 CALL: $7E:00F0 = the target, JSL $7E:37F3
        unsigned tgt = rd(r, ip);
        wr(r, 0x00F0, tgt);
        if (!call || call(w, page, tgt, io) < 0) { fprintf(stderr, "seqscript: call $%04X is not ported\n", tgt); return -1; }
        pgw16(r, page, 0x20, pg16(r, page, 0x20) + 2);                   // 0BB1 INC $20 / 0BB5 INC $20
        continue;
      }
      default:
        fprintf(stderr, "seqscript: op %02X at $%04X is not ported\n", a & 0xFF, ip - 1);
        pgw16(r, page, 0x20, ip - 1);
        return -1;
    }
  }
  return 0;
}

// $7E:42BD -- THE PAD'S THREE WORDS, rotated once a frame (D = $0000): `LDA $02 / STA $F0 / LDA $04 / STA $02 / LDA $06 /
// STA $04 / LDA $F0 / STA $06`. The NMI (C3/0A8E) puts the pad in $02; after this task, $06 is it -- what $4339 and $485D
// read -- and $02/$04 hold the two before. ($F0 keeps the word, as the ROM leaves it.)
void CtRaceTask42BD(uint8_t* w) {
  R rr = { w, 0, 0 }, *r = &rr;
  unsigned t = rd(r, 0x02);
  wr(r, 0xF0, t); wr(r, 0x02, rd(r, 0x04)); wr(r, 0x04, rd(r, 0x06)); wr(r, 0x06, t);
}

// ---- THE STAGE: C3:0000 -> $7E:3000 -> the NMI ----------------------------------------------------------------------------
// The machine every entry shares (the overlay, the common init, the NMI, the brightness tasks), then the race's own setup and
// tasks. Stage 1 of the native port: the LOGIC end to end -- what decides how a stage plays and when it ends -- over the WRAM
// image, the picture's own work left out (the packets and DMAs of the setups, the render tasks and the vblank hooks; stage 2).
// sequence_table.md sec. 12 (the race) and sec. 13 (the ending).

// The overlay, decompressed where C3:0000 puts it: `LDA $FE0003` (= C3:1313) -> $0300, `$7E:3000` -> $0303, JSL C3/0557.
static int seqOverlay(uint8_t* w, const CtRaceIo* io) {
  if (!io || !io->rom || io->romLen < 0x3E0006) return -1;
  const uint8_t* p = io->rom + 0x3E0003;
  uint32_t a = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  size_t n = ct_lz_decompress_bounded(io->rom, (size_t)io->romLen, w + 0x3000, 0x10000 - 0x3000, ct_lz_hirom_offset(a));
  w[0x0300] = p[0]; w[0x0301] = p[1]; w[0x0302] = p[2];
  w[0x0303] = 0x00; w[0x0304] = 0x30; w[0x0305] = 0x7E;
  return n ? 0 : -1;
}

// $7E:3177 -- the driver reset (five commands through $377E; sequence_table / title_intro.c CtTitleIntroDriverReset), as WRAM
// leaves it: each command's bytes in $1E00-$1E02, the last one `81 00 FF`, posted by the host. `LDA $0384 / BIT #20 / BNE`:
// an entry called with bit 5 keeps the song the field left playing (the ending's `FF 2A`..`FF 2F`).
static void seqDriverReset(R* r, const CtRaceIo* io) {
  static const uint8_t cmd[5][3] = { { 0x85, 0x00, 0x00 }, { 0x89, 0x00, 0x00 }, { 0xF2, 0x00, 0x00 },
                                     { 0xF2, 0x01, 0x00 }, { 0x81, 0x00, 0xFF } };
  if (r->w[0x0384] & 0x20) return;                                    // 3177 LDA $0384 / BIT #20 / BNE -> RTS
  for (int k = 0; k < 5; k++) {
    if (k == 0) { r->w[0x1E02] = 0; r->w[0x1E03] = 0; }              // STZ $1E02 (16-bit)
    r->w[0x1E00] = cmd[k][0];
    if (k == 0 || k == 3 || k == 4) r->w[0x1E01] = cmd[k][1];
    if (k == 4) r->w[0x1E02] = cmd[k][2];
    if (io && io->post) io->post(io->ctx);
    r->w[0x1E00] = cmd[k][0];                                         // $377E never clears the mailbox (title_intro.c)
  }
}

int CtSeqStageBegin(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  R rr = { w, 0, io }, *r = &rr;
  // C3:0014 -- the vectors ($0500-$0507: JML $C3:0548 / $C3:0529), the operand into $0384, the overlay
  w[0x0500] = 0x5C; w[0x0501] = 0x48; w[0x0502] = 0x05; w[0x0503] = 0xC3;
  w[0x0504] = 0x5C; w[0x0505] = 0x29; w[0x0506] = 0x05; w[0x0507] = 0xC3;
  w[0x0384] = operand;
  if (seqOverlay(w, io) < 0) return -1;
  // $7E:3063 -- the common init's WRAM: the shadows, then $0000-$00FF and $0900-$1DFF cleared (a DMA out of MPYL = 0)
  w[0x038E] = 4; w[0x038F] = 4;
  static const uint16_t z[] = { 0x0380, 0x0381, 0x0390, 0x0392, 0x0394, 0x0391, 0x0393, 0x0395, 0x0396, 0x0388, 0x038D, 0x038C, 0x0366 };
  for (unsigned k = 0; k < sizeof z / sizeof z[0]; k++) w[z[k]] = 0;
  memset(w + 0x0000, 0, 0x100);
  if (!(operand & 0x80)) memset(w + 0x0900, 0, 0x1500); else memset(w + 0x0920, 0, 0x14E0);
  if (!(operand & 0x20)) { w[0x038A] = 0; seqDriverReset(r, io); }   // 30E9 BIT #20 / BNE / STZ $038A / JSR $3177
  memset(w + 0x0700, 0xE0, 0x200);                                    // JSR $3843: the OAM shadow, every sprite off screen
  wr(r, 0x13, 0x0900);                                                // 30F9 LDX #0900 / STX $13
  if (io && io->pic) {                                                // 3102-3170: the PPU reset through D = $2100 (8-bit A = 0, X = 0)
    static const uint8_t seq[] = { 0x00, 0x80, 0x01, 0x02, 0x32, 0xE0, 0x02, 0, 0x03, 0, 0x05, 0, 0x06, 0, 0x07, 0, 0x08, 0, 0x09, 0, 0x0A, 0,
      0x0B, 0, 0x0C, 0, 0x0D, 0, 0x0D, 0, 0x0E, 0, 0x0E, 0, 0x0F, 0, 0x0F, 0, 0x10, 0, 0x10, 0, 0x11, 0, 0x11, 0, 0x12, 0, 0x12, 0,
      0x13, 0, 0x13, 0, 0x14, 0, 0x14, 0, 0x15, 0, 0x1A, 0, 0x1B, 0, 0x1B, 0, 0x1C, 0, 0x1C, 0, 0x1D, 0, 0x1D, 0, 0x1E, 0, 0x1E, 0,
      0x1F, 0, 0x1F, 0, 0x20, 0, 0x20, 0, 0x21, 0, 0x23, 0, 0x24, 0, 0x25, 0, 0x26, 0, 0x27, 0, 0x28, 0, 0x29, 0, 0x2A, 0, 0x2B, 0,
      0x2C, 0, 0x2D, 0, 0x2E, 0, 0x2F, 0, 0x30, 0, 0x31, 0, 0x32, 0, 0x33, 0 };
    for (unsigned k = 0; k < sizeof seq; k += 2) CtSeqPicWrite(&io->pic->p, seq[k], seq[k + 1], 0);
    io->pic->vmain = 0; io->pic->cgadd = 0;                           // ($2115 = 0, $2121 = 0)
  }
  return 0;
}

// C3/08D0 -- ONE NMI of the stage, its logic: INC $08, the task list into its snapshot $0950, the pad (C3/0A8E: $02, the
// pause on Start's edge once $0390 allows it, the soft reset on L+R+Select+Start unless $0396), then the tasks of the snapshot
// in order (7E:398A: sixteen slots, `CPX #0020`), each one able to remove or replace itself in the live list at its own index
// ($39A7 / $39B1). Paused ($0392), neither the hooks nor the tasks run.
int CtSeqStageNmi(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn run) { return CtSeqStageNmiHooks(w, joy, io, NULL, run); }

int CtSeqStageNmiHooks(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn hook, CtSeqTaskFn run) {
  R rr = { w, 0, io }, *r = &rr;
  CtSeqStagePic* pic = io ? io->pic : NULL;
  if (pic) { pic->hdmaen = 0; CtSeqPicWrite(&pic->p, 0x00, 0x80, 0); } // 08DC STZ $420C / 08ED LDA #80 / STA $2100
  if (hook && !rd(r, 0x0392))                                         // 08F2 LDA $0392 / BNE / JSL $7E:39BE
    for (unsigned x = 0; x < 0x10; x += 2) {                          // 39C0 .. 39D8 CPX #0010
      unsigned h = rd(r, 0x0940 + x);
      if (!h) continue;
      unsigned k = hook(w, x, h, io);
      if (k != h) wr(r, 0x0940 + x, k);                               // 39E3 STZ / 39F0 STA $0940,X
    }
  if (pic) {                                                          // 08FD LDA $038C / STA $420C, 0903 $038D or $80 -> $2100
    pic->hdmaen = w[0x038C];
    CtSeqPicWrite(&pic->p, 0x00, w[0x038D] ? w[0x038D] : 0x80, 0);
  }
  wr(r, 0x08, rd(r, 0x08) + 1);                                       // 090F INC $08
  memmove(w + 0x0950, w + 0x0920, 0x20);                              // 0911 MVN $0920 -> $0950, 32 bytes
  wr(r, 0x02, joy);                                                   // 0A99 LDA $4218 / STA $02
  if (pic) pic->joy = joy;
  if (joy == 0x3030 && !w[0x0396]) return 2;                          // 0A9E the soft reset (JML $00FF00)
  if (rd(r, 0x0390)) {                                                // 0ABB the pause
    if (w[0x03] & 0x10) {
      if (!rd(r, 0x0394)) {
        wr(r, 0x0394, rd(r, 0x0394) + 1);
        unsigned p = (rd(r, 0x0392) + 1) & 1;
        wr(r, 0x0392, p);
        if (p) { wr(r, 0x1E00, 0x00F2); }                             // 0ADB `F2` -- JSL $377A
        else { wr(r, 0x1E00, 0x4418); wr(r, 0x1E02, 0x0080); }        // 0AE7 `18 44 80 00`
        if (io && io->post) io->post(io->ctx);
      }
    } else wr(r, 0x0394, 0);
  }
  if (w[0x0392] && pic) {                                             // 0927 paused: $038D / 2, or $80
    unsigned b = w[0x038D] >> 1; CtSeqPicWrite(&pic->p, 0x00, (uint8_t)(b ? b : 0x80), 0);
  }
  if (!rd(r, 0x0392)) {                                               // 0922 LDA $0392 / BEQ -> JSL $7E:398A
    for (unsigned x = 0; x < 0x20; x += 2) {
      unsigned t = rd(r, 0x0950 + x);
      if (!t || !run) continue;
      unsigned k = run(w, x, t, io);
      if (k != t) wr(r, 0x0920 + x, k);
    }
  }
  return rd(r, 0x0000) ? 1 : 0;
}

int CtSeqStageSong(uint8_t* w, const CtRaceIo* io) {
  if (w[0x0388]) return 0;                                            // 09A6 LDA $0388 / BNE
  if (w[0x0384] & 0x20) return 0;                                     // 09AB LDA $0384 / BIT #20 / BNE
  if (w[0x038A] == w[0x0389]) return 0;                               // 09B2 LDA $038A / CMP $0389 / BEQ
  w[0x0389] = w[0x038A];                                              // 09BA STA $0389
  w[0x0388]++;                                                        // 09BD INC $0388
  w[0x1E00] = 0x10; w[0x1E01] = w[0x038A]; w[0x1E02] = 0xFF; w[0x1E03] = 0xFF;   // 09C0 .. 09CE
  int n = 0;
  if (io && io->post) { io->post(io->ctx); if (io->postFrames) n = io->postFrames(io->ctx); }   // 09D1 JSL C7/0004
  if (n <= 0) { w[0x0388] = 0; return 0; }                            // 09D5 STZ $0388
  return n;                                                           //   (after the frames the upload holds)
}

int CtSeqStageSongTick(uint8_t* w, const CtRaceIo* io, int hold) {
  if (hold > 0) {                                                     // the post's JSL still in the driver's handshake
    if (--hold == 0) w[0x0388] = 0;                                   // 09D5 STZ $0388, once it returns
    return hold;
  }
  return CtSeqStageSong(w, io);
}

void CtSeqStageEnd(uint8_t* w, const CtRaceIo* io) {
  R rr = { w, 0, io };
  seqDriverReset(&rr, io);
}

unsigned CtSeqTaskFade(uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io) {
  (void)slot; (void)io;
  if (--w[0x038F]) return task;                                       // DEC $038F / BNE -> RTS
  w[0x038F] = w[0x038E];                                              // LDA $038E / STA $038F
  switch (task) {
    case 0x347A: return ++w[0x038D] == 0x0F ? 0 : task;               // 3487 INC $038D / CMP #0F / BEQ -> JMP $39A7
    case 0x3495: return --w[0x038D] == 0 ? 0 : task;                  // 34A2 DEC $038D / BEQ -> JMP $39A7
    case 0x34E3: if (--w[0x038D]) return task;                        // 34F0 DEC $038D / BEQ
                 w[0x0000]++; return 0;                               // 34F6 INC $00 / JMP $39A7: the END
    default: return task;
  }
}

unsigned CtSeqSine(const CtRaceIo* io, unsigned a) {
  unsigned x = a & 0x03FF;
  unsigned v = (io && io->rom && io->romLen > 0xF900 + 0x3FF) ? io->rom[0xF900 + x] : 0;
  return x >= 0x0200 ? v : ((v ^ 0xFFFF) + 1) & 0xFFFF;
}

void CtSeqMul31BF(uint8_t* w, unsigned d) {
  R rr = { w, 0, NULL }, *r = &rr;
  unsigned f0 = rd(r, d + 0xF0), f2 = rd(r, d + 0xF2);
  unsigned a0 = f0 & 0xFF, a1 = f0 >> 8, b0 = f2 & 0xFF, b1 = f2 >> 8;
  unsigned p1 = b0 * a0, p2 = b0 * a1, p3 = b1 * a0, p4 = b1 * a1;
  wr(r, d + 0xF6, 0);                                                 // 31D5 STZ $F6 (16-bit: $F6, $F7)
  wr(r, d + 0xF4, p1);                                                // 31DD STA $F4
  unsigned s = p2 + p3;                                               // 31EE CLC / ADC $4216
  unsigned c = s > 0xFFFF; s &= 0xFFFF;
  { unsigned wd = rd(r, d + 0xF7); unsigned nc = (wd >> 15) & 1;     // 31F2 ROL $F7 (16-bit: $F7, $F8)
    wr(r, d + 0xF7, ((wd << 1) | c) & 0xFFFF); c = nc; }
  s = s + rd(r, d + 0xF5) + c; c = s > 0xFFFF; s &= 0xFFFF;          // 31F7 ADC $F5
  wr(r, d + 0xF5, s);                                                 // 31F9 STA $F5
  s = p4 + rd(r, d + 0xF6) + c; s &= 0xFFFF;                          // 31FB LDA $4216 / ADC $F6
  wr(r, d + 0xF6, s);                                                 // 3200 STA $F6
}

// C3/01E4 (through C3/000E) -- THE CURTAIN: one call a frame while `$0350` (D = $0300 from here: $50) is 1 (open) or 2 (close).
// `$51` counts the calls; the first fills the 224-byte state at bank `$54`:`$52` + $1C0 with 00 (open) or FF (close: the
// 16-bit `LDA $F0 / STA long` then an MVN that drags the byte up the buffer); calls 1-31 seed `$55` by C3/02DD (A = FE, the
// carry the `CMP #20` left, X's low byte: the caller's X, or the fill's MVN end on the first call) and walk the state: every
// `Y` lines (Y = (b & 1F) + 3 + C, the step (b' & 3) + 8 + C, b / b' the next two bytes of the chain `[$55]` over $C0:FE00 --
// each byte read is the next one's index) each line steps by it, saturating at FF (open, `ADC`) or 0 (close, `SBC` with the
// borrow: step + 1), into the state and through WMDATA into the output half the parity of `$51` names ($52 odd, $52 + E0
// even). ⚠ The close's clamp is `TDC`: it leaves B = $03, so the next refresh's `TAY` takes Y = $03xx -- the rest of the
// buffer walks on one step. The 32nd call clears `$50` / `$51`. Every register comes back (C3/01BD's pulls, PLP included).
// The caller's data bank is $00 (the stage's dispatcher, D = 0 / B = 0): C3/02DD's absolute operands are low RAM and the
// rule's virtual beam (CtSeqRng02DD8). The field's window wipe (C0/22FA) and the overworld's window object call the same
// routine; their port is another session's (src/xlat/wipe_c3.c, owwin_c6.c today) -- this is the stages' native one, the one
// body to swap for a shared one later.
void CtSeqCurtain01E4(uint8_t* w, unsigned x, const CtRaceIo* io) {
  uint8_t* d = w + 0x0300;
  if (!d[0x50]) { d[0x51] = 0; d[0x50] = 0; return; }                // 01F2 LDA $50 / BEQ -> 023B
  uint8_t a, b = 0;
  if (d[0x51]) a = d[0x51];                                           // 01F6 LDA $51 / BNE -> 0234
  else {
    d[0xF0] = (d[0x50] == 1) ? 0x00 : 0xFF;                           // 01FA STZ $F0 / CMP #01 / LDA #FF / STA $F0
    unsigned bank = (d[0x54] & 1) ? 0x10000 : 0;                      // 0211 LDA $54 / LSR A / BCC: $7F or $7E
    unsigned s = (unsigned)(d[0x52] | (d[0x53] << 8)) + 0x01C0;       // 0206 REP #21 / LDA $52 / ADC #01C0 / TAX
    for (unsigned i = 0; i <= 0xDF; i++) w[bank | ((s + i) & 0xFFFF)] = d[0xF0];   // STA long (16-bit) / MVN, A = DE
    x = (s + 0xDF) & 0xFFFF;                                          //   X past the MVN's last source byte
    a = 0x00; b = 0x03;                                               // 0233 TDC (D = $0300)
  }
  a++; d[0x51] = a;                                                   // 0234 INC A / STA $51
  int c = a >= 0x20;                                                  // 0237 CMP #20 / BMI
  if (!((uint8_t)(a - 0x20) & 0x80)) { d[0x51] = 0; d[0x50] = 0; return; }   // 023B STZ $51 / STZ $50
  d[0x57] = 0xC0; d[0x56] = 0xFE;                                     // 0242-0248
  { static int st = -1; if (st < 0) st = getenv("CT_XLAT_SEEDTRACE") ? 1 : 0;   // the seed's inputs, as seq_xlat.c traces them
    if (st) fprintf(stderr, "seedtrace stage f0 v=0 h=0 A=00FE X=%04X C=%d D=0300 B=00 $0386=%02X $0351=%02X $0008=%02X "
                    "$00F0=%02X $00F2=%02X $00F4=%02X bus=21\n", x & 0xFFFF, c, w[0x0386], w[0x0351], w[0x0008], w[0x00F0],
                    w[0x00F2], w[0x00F4]); }
  d[0x55] = CtSeqRng02DD8(w, 0xFE, &c, x);                            // 024A JSL C3/02DD / STA $55
  unsigned base = (unsigned)(d[0x52] | (d[0x53] << 8));
  x = (base + 0x01C0) & 0xFFFF;                                       // 0252 LDA $52 / CLC / ADC #01C0 / TAX
  unsigned f4 = (x + 0x00E0) & 0xFFFF;                                // 0259 CLC / ADC #00E0 / STA $F4
  d[0xF4] = (uint8_t)f4; d[0xF5] = (uint8_t)(f4 >> 8);
  unsigned wm = (d[0x51] & 1) ? base : (base + 0x00E0) & 0xFFFF;      // 025F LDA $51 / LSR A / LDA $52 / BCS / ADC #00E0
  unsigned wb = (unsigned)(d[0x54] & 1) << 16;                        // 026E STA $2183 (bank $7E / $7F)
  wm |= wb;
  if ((d[0x54] & 0xFE) != 0x7E) { static int said; if (!said++) fprintf(stderr, "seqcurtain: UNDERIVED -- the state's bank $%02X\n", d[0x54]); }
  unsigned db = wb;                                                   // 0273 PHA / PLB: the walk's data bank is $54
  b = 0x00;                                                           // 0275 TDC / XBA: A = 03, B = 00
  unsigned y = 1;                                                     // 0277 LDY #0001
  int close = d[0x50] == 2;                                           // 027A LDA $50 / CMP #02 / BEQ
  c = d[0x50] >= 2;
  const uint8_t* rom = io ? io->rom : NULL; long romLen = io ? io->romLen : 0;
  #define FE(i) ((rom && romLen > 0xFE00 + (long)(i)) ? rom[0xFE00 + (i)] : 0)
  for (;;) {
    y = (y - 1) & 0xFFFF;                                             // 0280 / 02AE DEY / BNE
    if (!y) {
      uint8_t v = FE(d[0x55]); d[0x55] = v;                           // LDA [$55] / STA $55
      unsigned t = (unsigned)(v & 0x1F) + 3 + (unsigned)c; c = t > 0xFF; y = ((unsigned)b << 8) | (t & 0xFF);   // AND #1F / ADC #03 / TAY
      v = FE(d[0x55]); d[0x55] = v;
      t = (unsigned)(v & 0x03) + 8 + (unsigned)c; c = t > 0xFF; d[0xF2] = (uint8_t)t;   // AND #03 / ADC #08 / STA $F2
    }
    uint8_t m = w[db | x], r;
    if (!close) {
      unsigned t = (unsigned)m + d[0xF2] + (unsigned)c; c = t > 0xFF; r = (uint8_t)t;   // 0296 LDA $0000,X / ADC $F2
      if (c) r = 0xFF;                                                //   BCC / LDA #FF
    } else {
      int t = (int)m - (int)d[0xF2] - (1 - c); c = t >= 0; r = (uint8_t)t;   // 02C4 LDA $0000,X / SBC $F2
      if ((r & 0x80) && !c) { r = 0x00; b = 0x03; }                   //   BPL / BCS / TDC
    }
    w[wm] = r; wm = (wm + 1) & 0x1FFFF;                               // STA $002180
    w[db | x] = r;                                                    // STA $0000,X
    x = (x + 1) & 0xFFFF;                                             // INX / CPX $F4 / BNE
    c = x >= f4;
    if (x == f4) break;
  }
  #undef FE
}

// $7E:424D -- THE ATTRACT's BUTTON (a task the attract's scripts add once their curtain is open; D = 0, A 8-bit): any pad bit
// in `$02` while no curtain runs (`$0350` = 0) starts the closing one -- `$0350` = the operand of 4258's `LDA #imm`, which the
// setup patched ($7E:4259: 02 for the race, 01 for entry 3) --, posts `18 BA 80` ($1E00-$1E02 through $377E, unless `$0388`),
// `$0399` = 32, `$7F:0062` = 0 (the world script then sends loc 1B1 to the title) and hands its slot to $4283, which counts
// `$0399` down and ENDs (`INC $00`). Returns what the slot holds afterwards.
unsigned CtSeqTaskAbort(uint8_t* w, unsigned task, const CtRaceIo* io) {
  if (task == 0x4283) {
    if (--w[0x0399]) return task;                                     // 4283 DEC $0399 / BNE -- 8-bit
    w[0x0000]++; return task;                                         // 4288 INC $00 (the task stays: the END tears down)
  }
  if (!(w[0x02] | w[0x03])) return task;                              // 424F LDX $02 / BEQ (X 16-bit)
  if (w[0x0350]) return task;                                         // 4253 LDA $0350 / BNE
  w[0x0350] = w[0x4259];                                              // 4258 LDA #imm / STA $0350
  if (!w[0x0388]) {                                                   // 425D LDA $0388 / BNE
    w[0x1E01] = 0xBA; w[0x1E02] = 0x80; w[0x1E00] = 0x18;            // 4262 LDX #80BA / STX $1E01 / LDA #18 / STA $1E00
    if (io && io->sfx) io->sfx(io->ctx, 0x18, 0xBA, 0x80);            // 426D JSR $377E (the ports, as the bump's)
  }
  w[0x0399] = 0x20; w[0x039A] = 0x00;                                 // 4270 LDX #0020 / STX $0399
  w[0x10062] = 0x00;                                                  // 4276 LDA #00 / STA $7F0062
  return 0x4283;                                                      // 427C LDX #4283 / JMP $39B1
}

// ---- the race: entries 0 and 9, $7E:3B48 ----

static unsigned raceTask(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io);
static unsigned raceHook(uint8_t* w, unsigned slot, unsigned h, const CtRaceIo* io) { (void)slot; return CtRacePicHook(io->pic, w, h, io); }

// The setup $7E:3B48's own master cycles, NMI off, by its arm: the story's 9521394 (`--callrom 7E3B48`, CT_CALLROM_NONMI=1,
// $0384 = 20 -- the song's post left to the host's SPC; sequence_table.md sec. 12); the attract's (`FF 60`: bit 5, no song)
// 10205558, measured in place on the rule's cartridge (`ct.exe --racepiccmp` RACEPICCMP_SPAN=7E3B48,7E300D from its hand-off,
// power-on f8182; C3:0000 to $7E:3013 is 46 frames there).
long CtRaceStageSetupCycles(uint8_t operand) { return (operand & 0x40) ? 10205558L : 9521394L; }

int CtRaceStageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  R rr = { w, 0, io }, *r = &rr;
  if (CtSeqStageBegin(w, operand, io) < 0) return -1;
  if ((operand & 0x1F) != 0 && (operand & 0x1F) != 9) return -1;      // entries 0 and 9: $7E:3B48
  // $7E:3B48 -- the race's setup, its logic
  if (operand & 0x0F) w[0x44]++;                                      // 3B48 LDA $0384 / AND #0F / BEQ / INC $44
  if (!(operand & 0x20)) {                                            // 3B51 BIT #20 / BNE / LDA #2C / JSR $375F: the song
    w[0x038A] = 0x2C; w[0x0389] = 0x2C;
    w[0x1E01] = 0x2C; w[0x1E00] = 0x10; w[0x1E02] = 0xFF; w[0x1E03] = 0xFF;
    if (io && io->post) io->post(io->ctx);
  }
  if (io && io->pic) CtRacePicSetupA(io->pic, w, io);                 // 3B5D-3C4E: the picture's loads (stage 2)
  // $7E:45D6 (JSR at 3C51): the start values
  if (w[0x44]) {                                                      // the story's call: six bytes of the overlay's own code
    w[0x53A0] = 0xD8; w[0x4F74] = 0xD8; w[0x50DA] = 0xD8; w[0x53A4] = 0xE8; w[0x5395] = 0x06; w[0x53AE] = 0x01;
  } else { w[0xBA] = 0x80; w[0x9A] = 0x03; }
  wr(r, 0x55, 0x4000); wr(r, 0x59, 0x4000); wr(r, 0x7A, 0x3000); wr(r, 0x7E, 0x5000);
  wr(r, 0xAE, 0); wr(r, 0x4E, 0x6000); wr(r, 0x73, 0x2000); wr(r, 0xA8, 0x0020);
  wr(r, 0xA2, 1); wr(r, 0xA0, 1); wr(r, 0x1444, 0);
  wr(r, 0x00B6, rd(r, 0x4319)); wr(r, 0x00B8, rd(r, 0x4329));
  if (io && io->pic) CtRacePicTables(w);                              // 4640 JMP $4309: the picture's tables
  w[0x038C] = 0x3F;                                                   // 3C4C (then 3C70's $3E on the story's branch)
  if (!(operand & 0x40)) {                                            // 3C54 BIT #0040 / BNE: the story's scripts
    wr(r, 0x0A20, 0x3F14); wr(r, 0x0B20, 0x400A);
    wr(r, 0x038E, 0x0808); w[0x038C] = 0x3E; wr(r, 0x0382, 0);
  } else {                                                            // 3C7C the attract's: the curtain and its window
    wr(r, 0x038E, 0x0808);
    wr(r, 0x0354, 0x007E); wr(r, 0x0352, 0xE000); wr(r, 0x0351, 0);   // 3C82-3C8E (`STZ $0351` 16-bit: $0352's low byte too)
    w[0x4259] = 0x02;                                                 // 3C93 STA $7E4259: $424D's `LDA #imm` -- the close
    if (io && io->pic) {                                              // 3C9B-3CB0, 16-bit stores: each writes the next register
      static const uint8_t wr16[][2] = { { 0x23, 0x22 }, { 0x24, 0x00 }, { 0x24, 0x22 }, { 0x25, 0x00 }, { 0x25, 0x22 },
                                         { 0x26, 0x00 }, { 0x27, 0xFF }, { 0x28, 0x00 }, { 0x2E, 0x17 }, { 0x2F, 0x04 } };
      for (unsigned k = 0; k < sizeof wr16 / sizeof wr16[0]; k++) CtSeqPicWrite(&io->pic->p, wr16[k][0], wr16[k][1], 0);
    }
    wr(r, 0x0397, rd(r, 0x0397) + 1);                                 // 3CB3 LDA $0397 / INC A / STA $0397 -- kept across visits
    if (!(rd(r, 0x0397) & 1)) { wr(r, 0x0A20, 0x404D); wr(r, 0x0B20, 0x415E); }   // 3CBA LSR / BCS: the two camera runs in turn
    else { wr(r, 0x0A20, 0x40C3); wr(r, 0x0B20, 0x415E); }
  }
  if (CtRaceScript(w, 0x0A00, io) < 0) return -1;                     // 3CD8 JSR $3749 -- page A runs once in the setup
  wr(r, 0x35FD, 0x45B0); wr(r, 0x3024, 0x51D1);                       // 3CDB / 3CE1: two words of the overlay
  if (io && io->pic) { CtRacePicSetupB(io->pic, w, io); CtRacePicCgram(io->pic, w); }   // 3CEA-3D44, then $7E:300D's $388B
  // $7E:300D .. 301E: $388B (CGRAM), $31B0 (the vblank), JSL C3/08D0 (one NMI, run here), NMI on, STZ $00, STZ $0392
  CtSeqStageNmiHooks(w, 0, io, (io && io->pic) ? raceHook : NULL, raceTask);
  wr(r, 0x0000, 0); wr(r, 0x0392, 0);
  if (io && io->pic) CtRacePicThread(io->pic, w);                    // 3023: the main loop's first pass
  return 0;
}

// The race's tasks (the logic ones; the picture's are stage 2).
static unsigned raceTask(uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x3749: CtRaceScript(w, 0x0A00, io); return t;
    case 0x3754: CtRaceScript(w, 0x0B00, io); return t;
    case 0x42BD: CtRaceTask42BD(w); return t;
    case 0x4F54:                                                      // the HUD and the sprites; its logic, the score's floor
      if (io && io->pic) CtRacePicTask(io->pic, w, slot, t, io); else CtRaceTask4F54(w);
      return t;
    case 0x4339: CtRaceTask4339(w); return t;
    case 0x44CE: return CtRaceTask44CE(w) ? t : 0;
    case 0x485D: return CtRaceStep485D(w, io);
    case 0x347A: case 0x3495: case 0x34E3: return CtSeqTaskFade(w, slot, t, io);   // the brightness (the overlay's own)
    case 0x424D: case 0x4283: return CtSeqTaskAbort(w, t, io);        // the attract's button
    case 0x3D72:                                                      // the attract's curtain: C3/01E4, then HDMA channel 0's
      CtSeqCurtain01E4(w, slot, io);                                  //   table by the call's parity (3D78 LDA $0351 / LSR A)
      if (io && io->pic) io->pic->ch[0].aAdr = (w[0x0351] & 1) ? 0x3D64 : 0x3D6B;   // STX $4302
      return t;
    default:                                                          // the picture's tasks (stage 2)
      if (io && io->pic) CtRacePicTask(io->pic, w, slot, t, io);
      return t;
  }
}

int CtRaceStageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io) {
  int st = CtSeqStageNmiHooks(w, joy, io, (io && io->pic) ? raceHook : NULL, raceTask);
  if (st != 2 && CtSeqStageSong(w, io) > 0) w[0x0388] = 0;           // (the race never changes $038A)
  if (st != 2 && io && io->pic) CtRacePicThread(io->pic, w);         // $7E:3023 the main loop's pass
  // The END is the main loop's: `3023 JSR $51D1 / 3026 LDA $00 / BEQ` reads `$00` only once the pass returns, and a pass
  // parked at 5211 (waiting on `$39` for the hook $5221) returns NMIs later -- the NMIs run on meanwhile. MEASURED on the
  // rule's cartridge, the attract's race: `$0000 = 1` at f441 of its hand-off state, $7E:302A at f443.
  if (st == 1 && io && io->pic && io->pic->thread) return 0;
  return st;
}

// $7E:302A -- the teardown after the END: NMI and HDMA off, forced blank, the driver reset, `JML $C0:0000` (the host's reboot).
void CtRaceStageClose(uint8_t* w, const CtRaceIo* io) { CtSeqStageEnd(w, io); }

// $7E:4F54 -- THE HUD's task, its one piece of logic. Everything else in it draws (the minimap's two markers, the boost
// icons, REST, BEST, the score: stage 2), but on the story's call it reaches `$5116` every frame (4F8A-4F90 or 4FA1-4FA7,
// `$0384` bit 6 clear) and that routine FLOORS THE SCORE before it draws it: 51A2 `LDA $69 / BPL / LDA #0000 / STA $69`.
// The race task $485D runs after it in the list, so a trailing player's `DEC $69` leaves FFFF and the next frame puts it
// back to 0 -- the cartridge's $69 sits at FFFF all the while Johnny leads (measured: f340 on). The win's jackpots read it.
void CtRaceTask4F54(uint8_t* w) {
  if (w[0x0384] & 0x40) return;                                       // 4F85 / 4F9C BIT #0040: the attract's arm draws less
  if (w[0x6A] & 0x80) { w[0x69] = 0; w[0x6A] = 0; }                   // 51A2
}

// $7E:44CE -- THE SCORE COUNTED DOWN after the finish (485D puts it in $4339's slot): every 4th frame (`$08` & 3 = 0) `$69`
// loses 2000, else 200, else 20, else 2 -- the first that fits (`SEC / SBC / BCS`, the `ADC` putting it back); under 2 the
// task removes itself (44F8 JMP $39A7). Returns 0 then.
int CtRaceTask44CE(uint8_t* w) {
  static const unsigned step[4] = { 2000, 200, 20, 2 };
  if (w[0x08] & 3) return 1;                                          // 44CE LDA $08 / AND #0003 / BNE
  unsigned a = (unsigned)(w[0x69] | (w[0x6A] << 8));
  for (int i = 0; i < 4; i++)
    if (a >= step[i]) { a -= step[i]; w[0x69] = (uint8_t)a; w[0x6A] = (uint8_t)(a >> 8); return 1; }
  return 0;
}
