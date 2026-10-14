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
static void taskDel(R* r, unsigned x) { for (unsigned i = 0; i < 0x20; i += 2) if (rd(r, 0x0920 + i) == x) { wr(r, 0x0920 + i, 0); return; } }
static void taskAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x20; i += 2) if (!rd(r, 0x0920 + i)) { wr(r, 0x0920 + i, x); return; } }
static void vblAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x20; i += 2) if (!rd(r, 0x0940 + i)) { wr(r, 0x0940 + i, x); return; } }

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
