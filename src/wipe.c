// wipe.c -- the window wipe C3/01E4 + its seed C3/02DD, and the overworld's window object C6/E241 / C6/E27E, native
// (see wipe.h; disasm/notes/field_window_wipe.md).
//
// The ROM's routines one for one, the carry included: C3/01E4 leaves several ADC / SBC without a CLC / SEC, and the step
// reads the carry the previous line's `CPX $F4` (or the mode's `CMP #02`) left. D = $0300 throughout (C3/01EC `LDA #0300
// / TCD`), so `$50`.. are $0350.. and `$F0`.. are $03F0..; C3/02DD's absolutes go through the caller's data bank.
#include "wipe.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint8_t* w; const uint8_t* rom; long romLen; } WipeBus;

static uint8_t rd(const WipeBus* b, uint32_t a) {
  uint8_t k = (uint8_t)(a >> 16); uint16_t o = (uint16_t)a;
  if (k == 0x7E || k == 0x7F) return b->w[((uint32_t)(k - 0x7E) << 16) | o];
  if (!(k & 0x40) && o < 0x2000) return b->w[o];                      // banks 00-3F / 80-BF: the low WRAM
  if ((k & 0x40) || o >= 0x8000) {                                     // HiROM
    long r = ((long)(k & 0x3F) << 16) | o;
    return (b->rom && r < b->romLen) ? b->rom[r] : 0;
  }
  return 0;
}
static void wr(const WipeBus* b, uint32_t a, uint8_t v) {
  uint8_t k = (uint8_t)(a >> 16); uint16_t o = (uint16_t)a;
  if (k == 0x7E || k == 0x7F) b->w[((uint32_t)(k - 0x7E) << 16) | o] = v;
  else if (!(k & 0x40) && o < 0x2000) b->w[o] = v;
}
#define DP(o) w[0x0300 + (o)]
static uint16_t dpW(const uint8_t* w, unsigned o) { return (uint16_t)(w[0x0300 + o] | (w[0x0301 + o] << 8)); }

// 65816 binary-mode ADC / SBC on 8 bits (the callers run with D clear; C3/01E4's PHP / REP keep the caller's D flag).
static uint8_t adc8(uint8_t a, uint8_t m, int* c) { unsigned s = (unsigned)a + m + (unsigned)*c; *c = s > 0xFF; return (uint8_t)s; }
static uint8_t sbc8(uint8_t a, uint8_t m, int* c) { int d = (int)a - m - (1 - *c); *c = d >= 0; return (uint8_t)d; }

static int seedTraceOn(void) { static int on = -1; if (on < 0) on = getenv("CT_XLAT_SEEDTRACE") ? 1 : 0; return on; }
static int g_quiet;           // CtWipePeek: a look ahead, kept out of the seed trace

// C3/02DD `ROL / ADC $0386 / STA $0386 / TXA / ADC $0386 / ADC $0008 / CMP $2137 / ADC $213C / SBC $213D / ADC $0386 /
// ADC $00F0 / EOR $00F2 / ADC $00F4 / SBC $213D / STA $0386 / RTL` -- M = 8, the absolutes in the caller's data bank.
static uint8_t seed(const WipeBus* b, uint8_t a, int c, uint16_t x, uint8_t db) {
  uint32_t D = (uint32_t)db << 16;
  if (seedTraceOn() && !g_quiet)
    fprintf(stderr, "seedtrace call A=%02X X=%04X C=%d B=%02X $0386=%02X $0351=%02X $0008=%02X $00F0=%02X $00F2=%02X $00F4=%02X\n",
            a, x, c, db, rd(b, D | 0x0386), b->w[0x0351], rd(b, D | 0x0008), rd(b, D | 0x00F0), rd(b, D | 0x00F2), rd(b, D | 0x00F4));
  int nc = a >> 7; a = (uint8_t)((a << 1) | c); c = nc;                 // ROL A
  a = adc8(a, rd(b, D | 0x0386), &c); wr(b, D | 0x0386, a);             // ADC $0386 / STA $0386
  a = (uint8_t)x;                                                      // TXA (M = 8: X's low byte)
  a = adc8(a, rd(b, D | 0x0386), &c);                                  // ADC $0386
  a = adc8(a, rd(b, D | 0x0008), &c);                                  // ADC $0008
  c = a >= 0x21;                                                       // CMP $2137: the open bus, the operand's $21
  a = adc8(a, b->w[0x0351], &c);                                       // ADC $213C: the virtual beam, $0351
  a = sbc8(a, 0x00, &c);                                               // SBC $213D: the virtual beam, 00
  a = adc8(a, rd(b, D | 0x0386), &c);                                  // ADC $0386
  a = adc8(a, rd(b, D | 0x00F0), &c);                                  // ADC $00F0
  a ^= rd(b, D | 0x00F2);                                              // EOR $00F2
  a = adc8(a, rd(b, D | 0x00F4), &c);                                  // ADC $00F4
  a = sbc8(a, 0x00, &c);                                               // SBC $213D: 00
  wr(b, D | 0x0386, a);                                                // STA $0386
  return a;
}

uint8_t CtWipeSeed(uint8_t* wram128, uint8_t a, int carry, uint16_t x, uint8_t db) {
  WipeBus b = { wram128, NULL, 0 };
  return seed(&b, a, carry != 0, x, db);
}

int CtWipeRoutine(const uint8_t* rom, long romLen, uint8_t* w, uint16_t callerX, uint8_t callerDb) {
  if (!rom || !w || romLen < 0x10000) return -1;
  WipeBus b = { w, rom, romLen };
  if (!DP(0x50)) return 0;                                             // 01F2 LDA $50 / BEQ -> 023B: STZ $51 / STZ $50
  uint16_t x = callerX;
  uint8_t cnt;
  if (!DP(0x51)) {                                                     // 01F6 LDA $51 / BNE
    DP(0xF0) = DP(0x50) == 1 ? 0x00 : 0xFF;                            // 01FA STZ $F0 / CMP #01 / BEQ / LDA #FF / STA $F0
    x = (uint16_t)(dpW(w, 0x52) + 0x01C0);                             // 0206 REP #21 / LDA $52 / ADC #01C0 / TAX
    uint32_t k = (DP(0x54) & 1) ? 0x7F0000u : 0x7E0000u;               // 0211 LDA $54 / LSR / BCC
    // `STA $7x0000,X` of the word $F0/$F1, then `LDA #00DE / MVN`: the byte at X carried through X+1 .. X+$DF.
    for (unsigned i = 0; i < 0xE0; i++) wr(&b, k | (uint16_t)(x + i), DP(0xF0));
    x = (uint16_t)(x + 0xDF);                                          // MVN leaves X past its last source byte
    cnt = 0x01;                                                        // 0233 TDC (A = 00) / INC A
  } else cnt = (uint8_t)(DP(0x51) + 1);                                // 0234 INC A
  DP(0x51) = cnt;                                                      // 0235 STA $51
  if (!((uint8_t)(cnt - 0x20) & 0x80)) { DP(0x51) = 0; DP(0x50) = 0; return 0; }   // 0237 CMP #20 / BMI; 023B
  int c = cnt >= 0x20;                                                 //   (the carry CMP #20 left)
  DP(0x57) = 0xC0; DP(0x56) = 0xFE;                                    // 0242 LDA #C0 / STA $57 / LDA #FE / STA $56
  DP(0x55) = seed(&b, 0xFE, c, x, callerDb);                           // 024A JSL C3/02DD (A = FE) / STA $55
  // 0250 REP #20: X = $52 + $1C0 (the working copy), $F4 = X + $E0 its end; WMADD = $54:$52 on an odd $51, else + $E0.
  x = (uint16_t)(dpW(w, 0x52) + 0x01C0);
  uint16_t end = (uint16_t)(x + 0x00E0);
  DP(0xF4) = (uint8_t)end; DP(0xF5) = (uint8_t)(end >> 8);
  uint16_t wm = (dpW(w, 0x51) & 1) ? dpW(w, 0x52) : (uint16_t)(dpW(w, 0x52) + 0x00E0);   // 025F LDA $51 / LSR / ... BCS
  uint32_t wmadd = ((uint32_t)(DP(0x54) & 1) << 16) | wm;              // $2181/$2182, 026E LDA $54 / STA $2183
  uint8_t dbr = DP(0x54);                                              // 0273 PHA / PLB
  if (dbr != 0x7E && dbr != 0x7F) return -1;                           // (every caller: $7E or $7F)
  uint32_t D = (uint32_t)dbr << 16;
  uint8_t bHi = 0x00;                                                  // 0275 TDC / XBA: A = 03, B = 00
  uint16_t y = 0x0001;                                                 // 0277 LDY #0001
  int mode2 = DP(0x50) == 0x02;
  c = DP(0x50) >= 0x02;                                                // 027A LDA $50 / CMP #02 / BEQ
  do {
    if (--y == 0) {                                                    // DEY / BNE
      uint32_t p = ((uint32_t)DP(0x57) << 16) | ((uint32_t)DP(0x56) << 8);
      uint8_t s = rd(&b, p | DP(0x55)); DP(0x55) = s;                  // LDA [$55] / STA $55
      uint8_t a = adc8((uint8_t)(s & 0x1F), 0x03, &c);                 // AND #1F / ADC #03
      y = (uint16_t)((bHi << 8) | a);                                  // TAY (X = 16: B over A)
      s = rd(&b, p | DP(0x55)); DP(0x55) = s;                          // LDA [$55] / STA $55
      DP(0xF2) = adc8((uint8_t)(s & 0x03), 0x08, &c);                  // AND #03 / ADC #08 / STA $F2
    }
    uint8_t v = rd(&b, D | x);                                         // LDA $0000,X
    if (!mode2) {
      v = adc8(v, DP(0xF2), &c);                                       // 0299 ADC $F2 / BCC
      if (c) v = 0xFF;                                                 //   LDA #FF
    } else {
      v = sbc8(v, DP(0xF2), &c);                                       // 02C7 SBC $F2
      if ((v & 0x80) && !c) { v = 0x00; bHi = 0x03; }                  //   BPL / BCS / TDC (C = D = $0300)
    }
    w[wmadd] = v; wmadd = (wmadd + 1) & 0x1FFFF;                       // STA $002180 (WMDATA)
    wr(&b, D | x, v);                                                  // STA $0000,X
    x++;                                                               // INX
    c = x >= end;                                                      // CPX $F4 / BNE
  } while (x != end);
  return 0;                                                            // JMP C3/01BD: every register restored
}

int CtWipePeek(const uint8_t* rom, long romLen, uint8_t* wram128, uint16_t callerX, uint8_t callerDb) {
  g_quiet = 1; int r = CtWipeRoutine(rom, romLen, wram128, callerX, callerDb); g_quiet = 0;
  return r;
}

int CtOwWindowObject(const uint8_t* rom, long romLen, uint8_t* w, uint32_t target) {
  if (!rom || !w || (target != 0xC6E241 && target != 0xC6E27E)) return -1;
  WipeBus b = { w, rom, romLen };
  uint16_t x = 0x009C;                                                 // E27E: C2/181C's X, op 4E << 1
  if (target == 0xC6E241) {                                            // M = 8, X = 16, D = 0, DB = 0
    w[0x8A42] = 0xF0; w[0x8A45] = 0xF0; w[0x8A48] = 0x00;              // LDA #F0 / STA x2 / TDC / STA $7E8A48
    w[0x004A] |= 0x04;                                                 // LDA #04 / TSB $4A
    x = (uint16_t)(w[0x004E] | (w[0x004F] << 8));                      // LDX $4E
    w[0x0350] = rd(&b, (uint16_t)(0x0022 + x));                        // LDA $0022,X / STA $0350
    w[0x0351] = 0x00;                                                  // STZ $0351
    w[0x0352] = rd(&b, (uint16_t)(0x0024 + x));                        // LDY $0024,X / STY $0352
    w[0x0353] = rd(&b, (uint16_t)(0x0025 + x));
    w[0x0354] = 0x7E;                                                  // LDA #7E / STA $0354
  }
  int r = CtWipeRoutine(rom, romLen, w, x, 0x00);                      // E27E JSL C3/000E
  if (r) return r;
  x = (uint16_t)(w[0x004E] | (w[0x004F] << 8));                        // E282 LDX $4E
  uint16_t t = (uint16_t)(rd(&b, (uint16_t)(0x0024 + x)) | (rd(&b, (uint16_t)(0x0025 + x)) << 8));   // LDA $0024,X
  if (!(w[0x0351] & 1)) t = (uint16_t)(t + 0x00E0);                    // LDA $0351 / LSR / BCC -> ADC #00E0
  w[0x8A25] = (uint8_t)t; w[0x8A26] = (uint8_t)(t >> 8);               // STA $7E8A25
  t = (uint16_t)(t + 0x0070);                                          // CLC / ADC #0070
  w[0x8A27] = (uint8_t)t; w[0x8A28] = (uint8_t)(t >> 8);               // STA $7E8A27
  return 0;
}
