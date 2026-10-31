// hdma_table.c -- the field's HDMA table builder, FD/C2C1 (disasm/notes/field_hdma_tables.md).
//
// Called once a frame (C0/00C7, C0/00DE, C0/2824, C0/0AFF), it writes the eight channels' tables of the set in bank $7F
// the NMI's FD/C1EE is not using -- set B ($7F:1238.., table FD:C2E5) when `$0153` bit 0 is clear, set A ($7F:0F80..,
// FD:C2DF) when set -- from the textbox window (`$0126` mode, `$0127` height), the window wave (`$1DF9` / `$1DFD`) and
// the scroll words at $7E:1D87.. (the battle's exit: $7F:3467..), then flips the bit.
//
// The stores follow the ROM's order, which is part of the result: two writers can run past the next channel's table (a
// variable-length list at $1085,Y / $133D,Y). Every handler runs with DB = $7F and D = $0100. The routine's own stack
// pushes are not written; no caller reads them back.
//
// Note: the carry is state in one place: the window wave's sixteen `ADC $001D8F` (C5F7.., C747..) have no CLC -- the first
// takes the carry the channel-7 builder's `LSR A` left (bit 0 of $7F:0351, C43C / C47A), each next one the previous
// one's. Everywhere else the ROM sets the carry (CLC / SEC / CMP / ASL) before it reads it.
#include "hdma_table.h"
#include "romview.h"
#include <stddef.h>

typedef struct {
  uint8_t* w;            // 128 KB: $7E at w[0], $7F at w[0x10000]
  const uint8_t* rom; long romLen;
  int c;                 // the carry (see above)
  unsigned y;            // Y in the variable-length writer CC5E / CCB8
} H;

// ---- The memory the routine touches ----
static void s8(H* h, unsigned a, unsigned v) { h->w[0x10000 + (a & 0xFFFF)] = (uint8_t)v; }                // STA abs, M = 1
static void s16(H* h, unsigned a, unsigned v) { s8(h, a, v); s8(h, a + 1, v >> 8); }                      // STX abs / STA abs, M = 0
static unsigned l7f16(H* h, unsigned a) { return h->w[0x10000 + (a & 0xFFFF)] | (h->w[0x10000 + ((a + 1) & 0xFFFF)] << 8); }
static unsigned l7e8(H* h, unsigned a) { return h->w[a & 0x1FFF]; }                                       // LDA $00xxxx (xxxx < $2000)
static unsigned l7e16(H* h, unsigned a) { return h->w[a & 0x1FFF] | (h->w[(a + 1) & 0x1FFF] << 8); }
static void dp8(H* h, unsigned d, unsigned v) { h->w[0x0100 + d] = (uint8_t)v; }                         // STA dp (D = $0100)
static void dp16(H* h, unsigned d, unsigned v) { dp8(h, d, v); dp8(h, d + 1, v >> 8); }
static unsigned ldp8(H* h, unsigned d) { return h->w[0x0100 + d]; }
static unsigned ldp16(H* h, unsigned d) { return ldp8(h, d) | (ldp8(h, d + 1) << 8); }
static unsigned romC0(H* h, unsigned a) {                                                                 // LDA $C0xxxx,X (16-bit)
  return (unsigned)(CtRomByte(h->rom, h->romLen, (long)a) | (CtRomByte(h->rom, h->romLen, (long)a + 1) << 8));   // (romview.h)
}
static unsigned adc16(H* h, unsigned a, unsigned v) { unsigned s = (a & 0xFFFF) + (v & 0xFFFF) + (unsigned)h->c; h->c = s > 0xFFFF; return s & 0xFFFF; }

// An HDMA entry: the line-count byte, then (for these indirect channels) the data's address, written STA then STX.
static void ent(H* h, unsigned a, unsigned count, unsigned ptr) { s8(h, a, count); s16(h, a + 1, ptr); }

// ---- The two sets ----
typedef struct {
  unsigned base;                     // set A 0, set B $2B8: the channel tables, $57 apart (ch0 $0F80 / $1238)
  unsigned p1, p2, p3;               // ch1 / ch2 / ch3 data (set A $1508 / $1510 / $1518, set B $150C / $1514 / $151C)
  unsigned wave;                     // the window wave's data ($7F:1D27 / $1DA7) -- 16 dwords, then their copy +$40
  unsigned p5a, p5b, p5c, p5e;       // ch5: its first run ($17C8 / $1AC8), mode 1's second ($17D6), mode 2's third ($18CA), the 2n run's $1534 / $1536
  unsigned p5m0;                     // ch5 mode 0's second run ($1890 / $1B90)
  unsigned p6, p6b, p6t;             // ch6 ($152C / $152E), its second ($1530 / $1532), the window table ($1538 / $1600)
  unsigned t;                        // the scroll words' place: ch1..3 data + t ($1508.. / $150C..) -- 0 / 4
  unsigned b;                        // ch6's byte data + b: $152C / $152E (and $152D / $152F, $1530 / $1532) -- 0 / 2
  int ch7odd;                        // ch7 (`E4 .. F0 ..` at $11E1 / $1499): which $7F:0351 parity draws $18A8
} Set;
static const Set SET_A = { 0x000, 0x1508, 0x1510, 0x1518, 0x1D27, 0x17C8, 0x17D6, 0x18CA, 0x1534, 0x1890,
                           0x152C, 0x1530, 0x1538, 0, 0, 0 };
static const Set SET_B = { 0x2B8, 0x150C, 0x1514, 0x151C, 0x1DA7, 0x1AC8, 0x1AD6, 0x1BCA, 0x1536, 0x1B90,
                           0x152E, 0x1532, 0x1600, 4, 2, 1 };
#define CH(s, k) ((s)->base + 0x0F80 + 0x57 * (k))

// `64 p 70 p 00` (C2F2..): the whole screen from one place, 100 lines then 112.
static void full(H* h, unsigned a, unsigned p) { ent(h, a, 0x64, p); ent(h, a + 3, 0x70, p); s8(h, a + 6, 0x00); }

// C34E / C4D7 / C550 / C8AA: the window wave -- thirteen `90 <wave>` entries (16 lines each, repeat mode: one dword a line
// from $7F:wave..), the terminator, then the thirteen addresses.
static void wave13(H* h, unsigned a, unsigned wave) {
  for (int k = 0; k < 13; k++) s8(h, a + 3 * k, 0x90);
  s8(h, a + 0x27, 0x00);                                          // STZ
  for (int k = 0; k < 13; k++) s16(h, a + 1 + 3 * k, wave);
}

// C4B5 (set A) / C52E (set B): channel 2, by the 8-bit `$1DFD`.
static void ch2(H* h, const Set* s) {
  if (!l7e8(h, 0x1DFD)) full(h, CH(s, 2), s->p2);
  else wave13(h, CH(s, 2), s->wave);
}

// C439 (set A) / C477 (set B): channel 7, by $7F:0351's parity (`LDA $0351 / LSR A` -- the carry the wave reads).
static void ch7(H* h, const Set* s) {
  unsigned v = h->w[0x10000 + 0x0351];
  h->c = (int)(v & 1);
  int hi = (int)(v & 1) == s->ch7odd;                             // set A: even -> $18A8; set B: odd -> $18A8
  unsigned a = s->base + 0x11E1;
  ent(h, a, 0xE4, hi ? 0x18A8 : 0x17C8);
  ent(h, a + 3, 0xF0, hi ? 0x190C : 0x182C);
  s8(h, a + 6, 0x00);
}

// CC5E (set A, $1085,Y) / CCB8 (set B, $133D,Y): a run of `n` lines of the window wave as repeat-mode entries of at most
// 16 lines, each pointed at the wave's dword `$EE & 0F`; `$EE` (DP $01EE) counts the lines handed out.
static void waveRun(H* h, const Set* s, unsigned n) {
  unsigned a = CH(s, 3);
  for (;;) {
    n &= 0xFF;
    if (n < 0x10) {                                               // CC5E CMP #10 / BCS
      dp8(h, 0xD9, n);
      s8(h, a + h->y, n | 0x80);
      s16(h, a + 1 + h->y, ((ldp8(h, 0xEE) & 0x0F) << 2) + s->wave);
      h->y = (h->y + 3) & 0xFFFF;
      dp8(h, 0xEE, ldp8(h, 0xEE) + ldp8(h, 0xD9));               // CC7E CLC / ADC $D9
      return;
    }
    n -= 0x10;                                                    // CC86 SEC / SBC #10
    dp8(h, 0xD9, n);
    s8(h, a + h->y, 0x90);
    s16(h, a + 1 + h->y, ((ldp8(h, 0xEE) & 0x0F) << 2) + s->wave);
    h->y = (h->y + 3) & 0xFFFF;
    dp8(h, 0xEE, ldp8(h, 0xEE) + 0x10);
    n = ldp8(h, 0xD9);
    if (!n) return;                                               // CCAC LDA $D9 / BEQ RTS, else BRA CC5E
  }
}
static void waveDirect(H* h, const Set* s, unsigned n, unsigned p) {   // CAA1 / CE18 / D0E7 / D396: a plain entry in the run
  unsigned a = CH(s, 3);
  s8(h, a + h->y, n);
  s16(h, a + 1 + h->y, p);
  h->y = (h->y + 3) & 0xFFFF;
  dp8(h, 0xEE, ldp8(h, 0xEE) + n);                                // CCB2 CLC / ADC $EE (A = the same 2n)
}

// ---- The tail: the scroll words into ch1..3's data, then the window wave or not ----
// C5A7 (set A) / C6F7 (set B).
static void tailField(H* h, const Set* s) {
  unsigned t = s->t;
  s16(h, 0x1508 + t, l7e16(h, 0x1D87));
  s16(h, 0x150A + t, l7e16(h, 0x1D89));
  s16(h, 0x1510 + t, l7e16(h, 0x1D8B));
  s16(h, 0x1512 + t, l7e16(h, 0x1D8D));
  if (!l7e16(h, 0x1DF9) && !l7e16(h, 0x1DFD)) {                   // the 16-bit tests (C5C5 / C5CB)
    s16(h, 0x1518 + t, l7e16(h, 0x1D8F));
    s16(h, 0x151A + t, l7e16(h, 0x1D91));
  } else {                                                        // C5E2 / C732: the wave advances one step a call
    unsigned st = (l7e8(h, 0x1DFB) + 1) & 0xFF;
    h->w[0x1DFB] = (uint8_t)st;
    unsigned x = st & 0x3E;
    for (int k = 0; k < 16; k++)                                  // LDA $C0FF30+4k,X / ADC $001D8F / STA wave+4k
      s16(h, s->wave + 4 * k, adc16(h, romC0(h, 0xFF30 + 4 * k + x), l7e16(h, 0x1D8F)));
    unsigned v = l7e16(h, 0x1D91);
    for (int k = 0; k < 16; k++) s16(h, s->wave + 2 + 4 * k, v);
    for (int k = 0; k < 0x40; k++)                                // C6E1 / C831 MVN 7F,7F: the 64 bytes again, +$40
      h->w[0x10000 + s->wave + 0x40 + k] = h->w[0x10000 + s->wave + k];
  }
  s8(h, 0x152D + s->b, l7e8(h, 0x0121));                          // C6E5 SEP #30 / LDA $21
  unsigned v = l7e8(h, 0x0BE0);
  s8(h, 0x152C + s->b, v);
  s8(h, 0x1530 + s->b, v);
}
// C401 (set A) / C95D (set B): mode 0 with `$53` bit 7 (the battle's exit tail) -- the battle's words at $7F:3467.
static void tailBattle(H* h, const Set* s) {
  unsigned t = s->t;
  s16(h, 0x1508 + t, l7f16(h, 0x3467));
  s16(h, 0x150A + t, l7f16(h, 0x3469));
  s16(h, 0x1510 + t, l7f16(h, 0x346B));
  s16(h, 0x1512 + t, l7f16(h, 0x346D));
  s16(h, 0x1518 + t, l7f16(h, 0x346F));
  s16(h, 0x151A + t, l7f16(h, 0x3471));
  s8(h, 0x152D + s->b, l7e8(h, 0x0121));
  unsigned v = h->w[0x10000 + 0x22C0];
  s8(h, 0x152C + s->b, v);
  s8(h, 0x1530 + s->b, v);
}

// ---- Mode 0: no window (C2EB / C847) ----
static void mode0(H* h, const Set* s) {
  full(h, CH(s, 0), 0x14F0);
  full(h, CH(s, 1), s->p1);
  ch2(h, s);
  if (!l7e8(h, 0x1DF9)) full(h, CH(s, 3), s->p3);                 // C32B / C887: the 8-bit `$1DF9`
  else wave13(h, CH(s, 3), s->wave);
  full(h, CH(s, 4), 0x1520);
  ent(h, CH(s, 5), 0xE4, s->p5a);                                 // C3BF / C91B
  ent(h, CH(s, 5) + 3, 0xF0, s->p5m0);
  s8(h, CH(s, 5) + 6, 0x00);
  full(h, CH(s, 6), s->p6);
  ch7(h, s);
  if (ldp8(h, 0x53) & 0x80) tailBattle(h, s);                     // C3F8 LDA $53 / BMI
  else tailField(h, s);
}

// ---- Mode 1: the window at the top (C995 / CD0C), n = `$0127` ----
// The ROM's arithmetic is 8-bit throughout (`LDA #29 / SEC / SBC $000127`, `ASL A`, `DEC A`): every count is & $FF.
static void band1(H* h, unsigned a, unsigned n, unsigned p, unsigned p2) {   // C9A4: 07 / 29-n / 2n (p2) / 64-n / 1E
  ent(h, a, 0x07, p);
  ent(h, a + 3, (0x29 - n) & 0xFF, p);
  ent(h, a + 6, (n << 1) & 0xFF, p2);
  ent(h, a + 9, (0x64 - n) & 0xFF, p);
  ent(h, a + 12, 0x1E, p);
  s8(h, a + 15, 0x00);
}
// CACD / CE44 (mode 1) and D10F / D3BE (mode 2): channel 4's middle -- `n - 1` lines from $1528, at most 10 (CMP #0A /
// BEQ / BCC: n <= 10 -> n - 1, else 10), then $1524 for 2 lines (n <= 10) or 2n - 20, then $1528 again.
static unsigned c4a(unsigned n) { return n <= 0x0A ? ((n - 1) & 0xFF) : 0x0A; }
static unsigned c4b(unsigned n) { return n <= 0x0A ? 0x02 : (((n << 1) & 0xFF) - 0x14) & 0xFF; }
// CB6B / CEE2 (mode 1) and D19F / D44E (mode 2): channel 5's running address -- each run's data follows the last's,
// two bytes a line, kept in DP $01D9 (a word) and the count in $01DB.
static void d9add(H* h, unsigned lines) { dp16(h, 0xD9, (((lines & 0xFF) << 1) + ldp16(h, 0xD9)) & 0xFFFF); }

static void mode1(H* h, const Set* s, unsigned n) {
  band1(h, CH(s, 0), n, 0x14F0, 0x14F4);
  band1(h, CH(s, 1), n, s->p1, 0x14F8);
  ch2(h, s);
  if (!l7e8(h, 0x1DF9)) band1(h, CH(s, 3), n, s->p3, 0x14FC);
  else {                                                          // CA8A / CE01: the same five runs over the wave
    h->y = 0; dp16(h, 0xEE, 0);                                   // CC58 LDY #0000 / STY $EE
    waveRun(h, s, 0x07);
    waveRun(h, s, (0x29 - n) & 0xFF);
    waveDirect(h, s, (n << 1) & 0xFF, 0x14FC);
    waveRun(h, s, (0x82 - n) & 0xFF);
    waveRun(h, s, 0x1E);
    s8(h, CH(s, 3) + h->y, 0x00);
  }
  unsigned a = CH(s, 4);                                          // CACD / CE44
  ent(h, a, 0x07, 0x1520);
  ent(h, a + 3, (0x29 - n) & 0xFF, 0x1520);
  ent(h, a + 6, c4a(n), 0x1528);
  ent(h, a + 9, c4b(n), 0x1524);
  ent(h, a + 12, c4a(n), 0x1528);
  ent(h, a + 15, (0x29 - n) & 0xFF, 0x1520);
  ent(h, a + 18, (0x64 - n) & 0xFF, 0x1520);
  ent(h, a + 21, 0x1E, 0x1520);
  s8(h, a + 24, 0x00);
  a = CH(s, 5);                                                   // CB6B / CEE2
  ent(h, a, 0x87, s->p5a);
  unsigned db = (0x29 - n) & 0xFF;
  dp8(h, 0xDB, db);
  s8(h, a + 3, db | 0x80);
  dp16(h, 0xD9, s->p5b);
  s16(h, a + 4, s->p5b);
  d9add(h, ldp8(h, 0xDB));
  s8(h, a + 6, (n << 1) & 0xFF);
  d9add(h, (n << 1) & 0xFF);
  s16(h, a + 7, s->p5e);
  db = (0x64 - n) & 0xFF;
  dp8(h, 0xDB, db);
  s8(h, a + 9, db | 0x80);
  s16(h, a + 10, ldp16(h, 0xD9));
  d9add(h, ldp8(h, 0xDB));
  s8(h, a + 12, 0x47 | 0x80);
  s16(h, a + 13, ldp16(h, 0xD9));
  s8(h, a + 15, 0x00);
  a = CH(s, 6);                                                   // CBE9 / CF60
  ent(h, a, 0x07, s->p6);
  ent(h, a + 3, (0x29 - n) & 0xFF, s->p6);
  ent(h, a + 6, ((((n << 1) & 0xFF) - 1) & 0xFF) | 0x80, ((((0x28 - n) & 0xFF) & 0x3F) << 1) + s->p6t);
  ent(h, a + 9, 0x01, s->p6b);
  ent(h, a + 12, (0x64 - n) & 0xFF, s->p6);
  ent(h, a + 15, 0x1E, s->p6);
  s8(h, a + 18, 0x00);
  ch7(h, s);
  tailField(h, s);
}

// ---- Mode 2: the window at the bottom (CFCF / D27E), n = `$0127` ----
static void band2(H* h, unsigned a, unsigned n, unsigned p, unsigned p2) {   // CFDE: 64 / 1D / 29-n+1 / 2n (p2) / 29-n-1
  ent(h, a, 0x64, p);
  ent(h, a + 3, 0x1D, p);
  ent(h, a + 6, (0x29 - n + 1) & 0xFF, p);
  ent(h, a + 9, (n << 1) & 0xFF, p2);
  ent(h, a + 12, (0x29 - n - 1) & 0xFF, p);
  s8(h, a + 15, 0x00);
}
static void mode2(H* h, const Set* s, unsigned n) {
  band2(h, CH(s, 0), n, 0x14F0, 0x14F4);
  band2(h, CH(s, 1), n, s->p1, 0x1500);
  ch2(h, s);
  if (!l7e8(h, 0x1DF9)) band2(h, CH(s, 3), n, s->p3, 0x1504);
  else {                                                          // D0CA / D379
    h->y = 0; dp16(h, 0xEE, 0);
    waveRun(h, s, 0x58);
    waveRun(h, s, 0x29);
    waveRun(h, s, (0x29 - n + 1) & 0xFF);
    waveDirect(h, s, (n << 1) & 0xFF, 0x1504);
    waveRun(h, s, (0x29 - n - 1) & 0xFF);
    s8(h, CH(s, 3) + h->y, 0x00);
  }
  unsigned a = CH(s, 4);                                          // D10F / D3BE
  ent(h, a, 0x64, 0x1520);
  ent(h, a + 3, 0x1D, 0x1520);
  ent(h, a + 6, (0x29 - n + 1) & 0xFF, 0x1520);
  ent(h, a + 9, c4a(n), 0x1528);
  ent(h, a + 12, c4b(n), 0x1524);
  ent(h, a + 15, c4a(n), 0x1528);
  ent(h, a + 18, (0x29 - n - 1) & 0xFF, 0x1520);
  s8(h, a + 21, 0x00);
  a = CH(s, 5);                                                   // D19F / D44E
  ent(h, a, 0xE4, s->p5a);
  ent(h, a + 3, 0x9D, s->p5m0);
  unsigned db = (0x29 - n + 1) & 0xFF;
  dp8(h, 0xDB, db);
  s8(h, a + 6, db | 0x80);
  s16(h, a + 7, s->p5c);
  dp16(h, 0xD9, s->p5c);
  d9add(h, ldp8(h, 0xDB));
  s8(h, a + 9, (n << 1) & 0xFF);
  d9add(h, (n << 1) & 0xFF);
  s16(h, a + 10, s->p5e);
  s8(h, a + 12, ((0x29 - n - 1) & 0xFF) | 0x80);
  s16(h, a + 13, ldp16(h, 0xD9));
  s8(h, a + 15, 0x00);
  a = CH(s, 6);                                                   // D20D / D4BC
  ent(h, a, 0x64, s->p6);
  ent(h, a + 3, 0x1D, s->p6);
  ent(h, a + 6, (0x29 - n + 1) & 0xFF, s->p6);
  ent(h, a + 9, ((((n << 1) & 0xFF) - 1) & 0xFF) | 0x80, ((((0x28 - n) & 0xFF) & 0x3F) << 1) + s->p6t);
  ent(h, a + 12, 0x01, s->p6b);
  ent(h, a + 15, (0x29 - n - 1) & 0xFF, s->p6);
  s8(h, a + 18, 0x00);
  ch7(h, s);
  tailField(h, s);
}

// The handler a mode-table entry names. Set A's table FD:C2DF holds C2EB C995 CFCF and runs on into set B's (FD:C2E5:
// C847 CD0C D27E), so `$0126` = 3..5 under bit 0 set reaches set B's handlers -- kept, though no writer of `$0126` stores
// more than 2 (C0/1985 `EOR #03` only flips 1 <-> 2). Past those the ROM jumps into code bytes: nothing to port.
static int run(H* h, int setB, unsigned mode) {
  const Set* s = setB ? &SET_B : &SET_A;
  unsigned n = l7e8(h, 0x0127);
  switch (mode) {
    case 0: mode0(h, s); return 0;
    case 1: if (n) mode1(h, s, n); else mode0(h, s); return 0;    // C995 LDA $0127 / BNE, else BRL C2EB
    case 2: if (n) mode2(h, s, n); else mode0(h, s); return 0;
  }
  return -1;
}

int CtHdmaBuildNative(uint8_t* wram128, const uint8_t* rom, long romLen) {
  if (!wram128 || !rom) return -1;
  H h = { wram128, rom, romLen, 0, 0 };
  unsigned mode = wram128[0x0126];                                // C2C7 LDA $26 / ASL A / TAX
  h.c = (int)((mode >> 7) & 1);
  if (!(wram128[0x0153] & 0x01)) {                                // C2C1 LDA $53 / BIT #01 / BNE
    if (mode > 2 || run(&h, 1, mode)) return -1;                  // JSR ($C2E5,X)
    wram128[0x0153] |= 0x01;                                      // TSB $53
  } else {
    if (mode > 5) return -1;                                      // JSR ($C2DF,X)
    if (run(&h, mode >= 3, mode >= 3 ? mode - 3 : mode)) return -1;
    wram128[0x0153] &= (uint8_t)~0x01;                            // TRB $53
  }
  return 0;
}
