// vwf.c -- bank C2's proportional-text engine, native (see vwf.h; disasm/notes/vwf_engine.md).
//
// The ROM's own routines, one for one: every direct-page byte they write is written here too (the scratch included),
// so the WRAM after a call is the cartridge's. D = $0200 throughout (57DF / 5823 `PHD / LDA #0200 / TCD`); the
// absolutes $0231-$023A are the same bytes. Long pointers read through the HiROM bus (rd).
#include "vwf.h"
#include <stdio.h>

typedef struct { uint8_t* w; const uint8_t* rom; long romLen; int bad; } Vwf;

#define DP(o) v->w[0x0200 + (o)]

static uint8_t rd(Vwf* v, uint32_t a) {
  uint8_t b = (uint8_t)(a >> 16); uint16_t o = (uint16_t)a;
  if (b == 0x7E || b == 0x7F) return v->w[((uint32_t)(b - 0x7E) << 16) | o];
  if (!(b & 0x40) && o < 0x2000) return v->w[o];                      // banks 00-3F / 80-BF: the low WRAM
  if ((b & 0x40) || o >= 0x8000) {                                     // HiROM
    long r = ((long)(b & 0x3F) << 16) | o;
    return r < v->romLen ? v->rom[r] : 0;
  }
  return 0;
}
static void wr(Vwf* v, uint32_t a, uint8_t x) {
  uint8_t b = (uint8_t)(a >> 16); uint16_t o = (uint16_t)a;
  if (b == 0x7E || b == 0x7F) v->w[((uint32_t)(b - 0x7E) << 16) | o] = x;
  else if (!(b & 0x40) && o < 0x2000) v->w[o] = x;
}
static unsigned rdW(Vwf* v, uint32_t a) { return rd(v, a) | (rd(v, (a + 1) & 0xFFFFFF) << 8); }
static unsigned c2W(Vwf* v, unsigned o) { return rdW(v, 0xC20000u | o); }   // DB = C2 (5E36 / 5F07 `LDA #C2 / PHA / PLB`)
static unsigned dpW(Vwf* v, unsigned o) { return DP(o) | (DP(o + 1) << 8); }
static void dpPutW(Vwf* v, unsigned o, unsigned x) { DP(o) = (uint8_t)x; DP(o + 1) = (uint8_t)(x >> 8); }
static uint32_t dpLong(Vwf* v, unsigned o) { return DP(o) | (DP(o + 1) << 8) | ((uint32_t)DP(o + 2) << 16); }
static void inc16(Vwf* v, unsigned o) { dpPutW(v, o, dpW(v, o) + 1); }                 // `REP #20 / INC dp`
static void cpW(Vwf* v, unsigned dst, unsigned src) { dpPutW(v, dst, dpW(v, src)); }  // `LDX src / STX dst`
static uint8_t next31(Vwf* v) { uint8_t a = rd(v, dpLong(v, 0x31)); inc16(v, 0x31); return a; }   // `LDA [$31] / INC $31`
static uint8_t next37(Vwf* v) { uint8_t a = rd(v, dpLong(v, 0x37)); inc16(v, 0x37); return a; }
static uint8_t next3D(Vwf* v) { uint8_t a = rd(v, dpLong(v, 0x3D)); inc16(v, 0x3D); return a; }

// ---- C2/5DC4: one glyph ---------------------------------------------------------------------------------------------
// C2/5E89 / C2/5F5A, one plane-row: the font's left eight pixels ([$76],Y: FF:2060 + code x 24) over its right four
// ([$79],Y: FF:3860 + (code >> 1) x 24, the even code's high nibble, the odd code's low), a 12-bit row left-justified in
// a word; shifted to the pen's column ($6A = pen & 7, C2:5EAF / 5F80) and stored at the strip's column offsets ($6E = 2 x
// (pen >> 3) into C2:5FE6 for 2 bpp, C2:6066 for 4 bpp). Shift 0 stores both bytes; 1-4 shift right, the low byte
// stored, the high OR-ed; 5-7 shift left (8 - s) through $63: the low byte two columns on, the high one on (both
// stored), $63 OR-ed into the first.
static void glyphRow(Vwf* v, unsigned tbl) {
  unsigned y = dpW(v, 0x60);
  uint8_t hi = rd(v, (dpLong(v, 0x76) + y) & 0xFFFFFF);
  uint8_t b = rd(v, (dpLong(v, 0x79) + y) & 0xFFFFFF);
  uint8_t lo = (DP(0x62) & 1) ? (uint8_t)(b << 4) : (uint8_t)(b & 0xF0);
  unsigned w = (unsigned)hi << 8 | lo;
  dpPutW(v, 0x63, 0);
  unsigned s = DP(0x6A), x = dpW(v, 0x6E);
  uint32_t d = dpLong(v, 0x73);
  if (s == 0) {
    wr(v, (d + c2W(v, tbl + 2 + x)) & 0xFFFFFF, lo);
    wr(v, (d + c2W(v, tbl + x)) & 0xFFFFFF, hi);
  } else if (s <= 4) {
    w >>= s;
    wr(v, (d + c2W(v, tbl + 2 + x)) & 0xFFFFFF, (uint8_t)w);
    uint32_t a = (d + c2W(v, tbl + x)) & 0xFFFFFF;
    wr(v, a, (uint8_t)(rd(v, a) | (w >> 8)));
  } else {
    unsigned r = dpW(v, 0x63);
    for (unsigned k = s; k < 8; k++) { unsigned c = (w >> 15) & 1; w = (w << 1) & 0xFFFF; r = ((r << 1) | c) & 0xFFFF; }
    dpPutW(v, 0x63, r);                                                // `ROL $63` (16-bit)
    wr(v, (d + c2W(v, tbl + 4 + x)) & 0xFFFFFF, (uint8_t)w);
    wr(v, (d + c2W(v, tbl + 2 + x)) & 0xFFFFFF, (uint8_t)(w >> 8));
    uint32_t a = (d + c2W(v, tbl + x)) & 0xFFFFFF;
    wr(v, a, (uint8_t)(rd(v, a) | DP(0x63)));
  }
}
// C2/5E36 (2 bpp: the tile below is $F0 on) / C2/5F07 (4 bpp: $1F0 on): 12 rows from the strip's base + 8 -- rows 4-7
// of the upper tile (8 bytes), then all 8 rows of the one below (16 bytes); planes 0 and 1 only.
static void glyphRows(Vwf* v, unsigned step, unsigned tbl) {
  DP(0x68) = 4; DP(0x60) = 0; DP(0x61) = 0;
  do {
    glyphRow(v, tbl); inc16(v, 0x60); inc16(v, 0x73);
    glyphRow(v, tbl); inc16(v, 0x60); inc16(v, 0x73);
  } while (--DP(0x68));
  dpPutW(v, 0x73, dpW(v, 0x73) + step);
  DP(0x68) = 8;
  do {
    glyphRow(v, tbl); inc16(v, 0x60); inc16(v, 0x73);
    glyphRow(v, tbl); inc16(v, 0x60); inc16(v, 0x73);
  } while (--DP(0x68));
}
// `REP #20 / STA $35 / SEP #20 / JSR C2/5DC4`: the glyph `code` at the pen, the pen on by its advance (C2:60E6[code - A0],
// 8-bit), $17 + 1.
static void glyph(Vwf* v, unsigned code) {
  dpPutW(v, 0x35, code);
  unsigned c = dpW(v, 0x35), a, k;
  a = (c << 3) & 0xFFFF; dpPutW(v, 0x6C, a);                           // 5DC6-5DCB
  k = (a >> 15) & 1; a = ((a << 1) & 0xFFFF) + dpW(v, 0x6C) + k;       // `ASL A / ADC $6C` (the ASL's carry)
  dpPutW(v, 0x76, (a + 0x2060) & 0xFFFF);                              // `CLC / ADC #2060`
  a = ((c >> 1) << 3) & 0xFFFF; dpPutW(v, 0x6C, a);                    // 5DD6-5DDC
  k = (a >> 15) & 1; a = ((a << 1) & 0xFFFF) + dpW(v, 0x6C) + k;
  dpPutW(v, 0x79, (a + 0x3860) & 0xFFFF);
  dpPutW(v, 0x6A, dpW(v, 0x34) & 0x0007);
  dpPutW(v, 0x73, (dpW(v, 0x10) + 8) & 0xFFFF);
  DP(0x75) = DP(0x12); DP(0x78) = 0xFF; DP(0x7B) = 0xFF;
  DP(0x6E) = (uint8_t)((DP(0x34) & 0xF8) >> 2); DP(0x6F) = 0;
  DP(0x62) = DP(0x35);
  if (DP(0x14) & 0x0F) glyphRows(v, 0x01F0, 0x6066); else glyphRows(v, 0x00F0, 0x5FE6);
  DP(0x34) = (uint8_t)(DP(0x34) + rd(v, 0xC260E6u + (uint8_t)(DP(0x35) - 0xA0)));
  DP(0x17)++;
}

// ---- The numbers ----------------------------------------------------------------------------------------------------
// C2/614B: Y rounds of compare-and-subtract against $6A, halving it, the quotient's bits into $65; Y = 4 after.
static unsigned div8(Vwf* v, unsigned a, int y) {                      // m 8-bit (C2/615E)
  DP(0x65) = 0;
  do {
    unsigned c = a >= DP(0x6A); if (c) a -= DP(0x6A);
    DP(0x65) = (uint8_t)(DP(0x65) << 1 | c); DP(0x6A) >>= 1;
  } while (--y);
  return a;
}
static unsigned div16(Vwf* v, unsigned a, int y) {                     // m 16-bit (C2/6180)
  dpPutW(v, 0x65, 0);
  do {
    unsigned c = a >= dpW(v, 0x6A); if (c) a -= dpW(v, 0x6A);
    dpPutW(v, 0x65, (dpW(v, 0x65) << 1 | c) & 0xFFFF); dpPutW(v, 0x6A, dpW(v, 0x6A) >> 1);
  } while (--y);
  return a;
}
// C2/6231: the same on the 24-bit $68-$6A against $6C-$6E, the bits into $6B.
static void div24(Vwf* v, int y) {
  DP(0x6B) = 0;
  do {
    uint32_t n = DP(0x68) | DP(0x69) << 8 | (uint32_t)DP(0x6A) << 16;
    uint32_t d = DP(0x6C) | DP(0x6D) << 8 | (uint32_t)DP(0x6E) << 16;
    unsigned c = n >= d;
    if (c) { n -= d; DP(0x68) = (uint8_t)n; DP(0x69) = (uint8_t)(n >> 8); DP(0x6A) = (uint8_t)(n >> 16); }
    DP(0x6B) = (uint8_t)(DP(0x6B) << 1 | c);
    d >>= 1; DP(0x6C) = (uint8_t)d; DP(0x6D) = (uint8_t)(d >> 8); DP(0x6E) = (uint8_t)(d >> 16);
  } while (--y);
}
// C2/615E: $68 (8-bit) -> three digits at $60-$62.
static void digits3(Vwf* v) {
  dpPutW(v, 0x6A, 0x00C8); unsigned a = div8(v, DP(0x68), 2); cpW(v, 0x60, 0x65);
  dpPutW(v, 0x6A, 0x0050); a = div8(v, a, 4); cpW(v, 0x61, 0x65);
  DP(0x62) = (uint8_t)a;
}
// C2/6180: $68-$69 -> five digits at $60-$64 (`STA $64` 16-bit writes $65 too).
static void digits5(Vwf* v) {
  dpPutW(v, 0x6A, 0x9C40); unsigned a = div16(v, dpW(v, 0x68), 3); cpW(v, 0x60, 0x65);
  dpPutW(v, 0x6A, 0x1F40); a = div16(v, a, 4); cpW(v, 0x61, 0x65);
  dpPutW(v, 0x6A, 0x0320); a = div16(v, a, 4); cpW(v, 0x62, 0x65);
  dpPutW(v, 0x6A, 0x0050); a = div16(v, a, 4); cpW(v, 0x63, 0x65);
  dpPutW(v, 0x64, a);
}
// C2/61BD: $68-$6A -> eight digits at $60-$67.
static void digits8(Vwf* v) {
  static const uint32_t dv[7] = { 0x989680, 0x7A1200, 0x0C3500, 0x013880, 0x001F40, 0x000320, 0x000050 };
  for (int i = 0; i < 7; i++) {
    dpPutW(v, 0x6C, dv[i] & 0xFFFF); DP(0x6E) = (uint8_t)(dv[i] >> 16);
    div24(v, i ? 4 : 1); cpW(v, 0x60 + i, 0x6B);
  }
  DP(0x67) = DP(0x68);
}
// C2/5D56 (8 digits) / 5D91 (5) / 5DAD (3): the leading zeros dropped from $40.., one digit kept; A = the digits left.
static unsigned stripZeros(Vwf* v, int from) {
  if (from == 8) {
    if (DP(0x40)) return 8;
    DP(0x40) = DP(0x41); cpW(v, 0x41, 0x42); cpW(v, 0x43, 0x44); cpW(v, 0x45, 0x46);
    if (DP(0x40)) return 7;
    cpW(v, 0x40, 0x41); cpW(v, 0x42, 0x43); cpW(v, 0x44, 0x45);
    if (DP(0x40)) return 6;
    DP(0x40) = DP(0x41); cpW(v, 0x41, 0x42); cpW(v, 0x43, 0x44);
    from = 5;
  }
  if (from == 5) {
    if (DP(0x40)) return 5;
    cpW(v, 0x40, 0x41); cpW(v, 0x42, 0x43);
    if (DP(0x40)) return 4;
    DP(0x40) = DP(0x41); cpW(v, 0x41, 0x42);
  }
  if (DP(0x40)) return 3;
  cpW(v, 0x40, 0x41);
  if (DP(0x40)) return 2;
  DP(0x40) = DP(0x41);
  return 1;
}
// C2/5CC0: the byte at $60 as two hex glyphs (C2:5D03[nibble], a word: >= 100 a prefixed pair, stored high byte first)
// into $40 + $6A.
static void hexByte(Vwf* v) {
  for (int half = 0; half < 2; half++) {
    unsigned a = c2W(v, 0x5D03 + (half ? (DP(0x60) & 0x0F) << 1 : (DP(0x60) & 0xF0) >> 3));
    unsigned x = dpW(v, 0x6A);
    if (a >= 0x0100) { dpPutW(v, 0x40 + x, (a >> 8) | ((a & 0xFF) << 8)); dpPutW(v, 0x6A, x + 2); }
    else { dpPutW(v, 0x40 + x, a); dpPutW(v, 0x6A, x + 1); }
  }
}

// ---- The string loop and its states ---------------------------------------------------------------------------------
// C2/5D23 / 5D34 / 5D45: the bytes at [$37] before `stop`, at most `max`.
static uint8_t countTo(Vwf* v, uint8_t stop, unsigned max) {
  unsigned y = 0;
  do { if (rd(v, (dpLong(v, 0x37) + y) & 0xFFFFFF) == stop) break; y++; } while (y < max);
  return (uint8_t)y;
}
// A dictionary entry (58D0 / 5A4C): [$37] = DE:FA00[x], $3A its length byte.
static void dictEntry(Vwf* v, unsigned x) {
  dpPutW(v, 0x37, rdW(v, 0xDEFA00u + x)); DP(0x39) = 0xDE;
  DP(0x3A) = next37(v);
  DP(0x30) = 1;
}
// A name in bank 7E (5A26 / 5B15 / 5B70): [$37] = C2:5FD8[x] (the party's names from $7E:2C23, 6 bytes apart), up to
// five bytes before a 00.
static void nameAt(Vwf* v, unsigned x) {
  dpPutW(v, 0x37, c2W(v, 0x5FD8 + x)); DP(0x39) = 0x7E;
  DP(0x30) = 1;
  DP(0x3A) = countTo(v, 0x00, 5);
}
// The state after a glyph of states 1-3 (5C15 / 5C4E / 5C97): its run ($3A) and the call's quota ($13), each counted down.
// 1 = go on in the state; 0 = back to the string (state 0); -1 = the call is over.
static int stateTail(Vwf* v) {
  int more = --DP(0x3A) != 0, quota = --DP(0x13) != 0;
  if (more && quota) return 1;
  if (!more) DP(0x30) = 0;
  if (quota) return 0;                                                 // the run ended: 5C37 `STZ $30 / JMP C2/58B2`
  DP(0x15) = 0x10;
  return -1;
}

static int run(Vwf* v) {
  int st = DP(0x30);
  for (;;) {
    if (st == 0) {                                                     // C2/58B2
      unsigned a = next31(v);
      if (a >= 0xA0 || a == 0x01 || a == 0x02) {
        if (a < 0xA0) a = a << 8 | next31(v);                          // 5B06: a prefixed glyph (`INC $0231`)
        glyph(v, a);                                                   // 58BE
        if (--DP(0x13)) continue;
        DP(0x15) = 0x10;
        return 0;
      }
      if (a >= 0x21) { DP(0x3B) = (uint8_t)a; dictEntry(v, (a - 0x21) << 1); st = 1; continue; }   // 58D0
      switch (a) {                                                     // C2:5903[a]
      case 0x03:                                                       // 5945: an argument byte into $16
        DP(0x16) = next31(v); DP(0x15) = 0x03; return 0;
      case 0x10: return 0;                                             // 5B14: the call ends, $15 as it was
      case 0x0D:                                                       // 5953: a byte from [$3D]
        DP(0x68) = next3D(v);
        if (!DP(0xA1)) {
          digits3(v); cpW(v, 0x40, 0x60); DP(0x42) = DP(0x62);
          DP(0x3A) = (uint8_t)stripZeros(v, 3); st = 2;
        } else { dpPutW(v, 0x6A, 0); cpW(v, 0x60, 0x68); hexByte(v); DP(0x3A) = 2; st = 3; }
        break;
      case 0x0E:                                                       // 5985: a word from [$3D]
        dpPutW(v, 0x68, rdW(v, dpLong(v, 0x3D))); inc16(v, 0x3D); inc16(v, 0x3D);
        if (!DP(0xA1)) {
          digits5(v); cpW(v, 0x40, 0x60); cpW(v, 0x42, 0x62); DP(0x44) = DP(0x64);
          DP(0x3A) = (uint8_t)stripZeros(v, 5); st = 2;
        } else {
          dpPutW(v, 0x6A, 0);
          DP(0x60) = DP(0x69); DP(0x61) = DP(0x68); hexByte(v);
          cpW(v, 0x60, 0x68); hexByte(v);
          DP(0x3A) = 4; st = 3;
        }
        break;
      case 0x0F:                                                       // 59C8: three bytes from [$3D]
        dpPutW(v, 0x68, rdW(v, dpLong(v, 0x3D)));
        DP(0x6A) = rd(v, (dpLong(v, 0x3D) + 2) & 0xFFFFFF);
        dpPutW(v, 0x3D, dpW(v, 0x3D) + 3);
        digits8(v); cpW(v, 0x40, 0x60); cpW(v, 0x42, 0x62); cpW(v, 0x44, 0x64); cpW(v, 0x46, 0x66);
        DP(0x3A) = (uint8_t)stripZeros(v, 8); st = 2;
        break;
      case 0x11:                                                       // 5A26: the name [$3D] picks
        nameAt(v, (unsigned)next3D(v) << 1); st = 1; break;
      case 0x12: {                                                     // 5A4C
        unsigned b = next31(v);
        if (b >= 0x08) { DP(0x3B) = (uint8_t)b; dictEntry(v, (b - 0x08 + 0x1F) << 1); st = 1; break; }
        if (b >= 0x02) {
          static int said; if (!said) { said = 1; fprintf(stderr, "vwf: code 12 %02X -- C2/5A87's table has no entry\n", b); }
          v->bad = 1; return -1;
        }
        unsigned id = next3D(v);                                       // 5A8E / 5AD0: x 0B through $4202 / $4203
        DP(0x39) = 0xCC;
        if (b == 0) {                                                  // an item's name: its icon byte 2F skipped
          dpPutW(v, 0x37, id * 0x0B + 0x15C4);
          DP(0x3A) = countTo(v, 0xEF, 0x0B);
          if (rd(v, dpLong(v, 0x37)) == 0x2F) { inc16(v, 0x37); DP(0x3A) = countTo(v, 0xEF, 0x0A); }
        } else { dpPutW(v, 0x37, id * 0x0B + 0x6500); DP(0x3A) = 0x0B; }
        DP(0x30) = 1; st = 1;
        break;
      }
      case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:   // 5B15: member a - 13's name
        nameAt(v, ((a - 0x13) << 1) & 0xFF); st = 1; break;
      case 0x1A:                                                       // 5B3B: $7E:2C23
        dpPutW(v, 0x37, 0x2C23); DP(0x39) = 0x7E; DP(0x3A) = countTo(v, 0x00, 5); DP(0x30) = 1; st = 1; break;
      case 0x1B: case 0x1C: case 0x1D:                                 // 5B70: the party's slot a - 1B ($7E:2980)
        nameAt(v, (rd(v, 0x7E2980u + (a - 0x1B)) << 1) & 0xFF); st = 1; break;
      case 0x1E:                                                       // 5B56: C2:6146, five glyphs
        dpPutW(v, 0x37, 0x6146); DP(0x39) = 0xC2; DP(0x3A) = 5; DP(0x30) = 1; st = 1; break;
      case 0x1F:                                                       // 5B9B: CC:0B5F + $7F:0200 x 0B
        dpPutW(v, 0x37, rd(v, 0x7F0200u) * 0x0B + 0x0B5E + 1); DP(0x39) = 0xCC;
        DP(0x3A) = countTo(v, 0xEF, 0x0A); DP(0x30) = 1; st = 1; break;
      case 0x20:                                                       // 5BD6: $7E:2C4D
        dpPutW(v, 0x37, 0x2C4D); DP(0x39) = 0x7E; DP(0x30) = 1; DP(0x3A) = countTo(v, 0x00, 5); st = 1; break;
      default:                                                         // 594F (00, 04-0C): the code into $15
        DP(0x15) = (uint8_t)a; return 0;
      }
      if (st == 2 || st == 3) { dpPutW(v, 0x37, 0x0240); DP(0x39) = 0x00; DP(0x30) = (uint8_t)st; }   // 59FC / 5A11
      continue;
    }
    if (st == 2) glyph(v, (uint8_t)(next37(v) + 0xD4));               // C2/5C3E: digits from $0240
    else {                                                             // C2/5BF5 / 5C77
      unsigned a = next37(v);
      if (a == 0x01 || a == 0x02) a = a << 8 | next37(v);
      glyph(v, a);
    }
    int t = stateTail(v);
    if (t < 0) return 0;
    if (t == 0) st = 0;
  }
}

void CtVwfBegin(uint8_t* wram, const uint8_t* rom, long romLen) {
  Vwf vv = { wram, rom, romLen, 0 }, *v = &vv;
  uint32_t p = (dpLong(v, 0x0D) + ((unsigned)DP(0x0C) << 1)) & 0xFFFFFF;   // `LDA [$0D],Y` (16-bit)
  DP(0x31) = rd(v, p); DP(0x32) = rd(v, (p + 1) & 0xFFFFFF);
  DP(0x33) = DP(0x0F); DP(0x30) = 0;
  DP(0x34) = DP(0x14) == 0x02 ? 0x00 : 0x08;
  if (DP(0x14) & 0x80) DP(0x34) = 0;
  DP(0x15) = 0x04;
  dpPutW(v, 0x3D, 0x0200); DP(0x3F) = 0x00;
  DP(0x17) = 0;
}

int CtVwfRun(uint8_t* wram, const uint8_t* rom, long romLen) {
  Vwf vv = { wram, rom, romLen, 0 }, *v = &vv;
  switch (DP(0x15)) {                                                  // C2/584A (C2:5851[$15])
  case 0x00: return 0;                                                 // 5893: the string has ended
  case 0x05: case 0x07: case 0x09: case 0x0B:                          // 5897 / 589B: a new line's pen
  case 0x06: case 0x08: case 0x0A: case 0x0C:
    DP(0x34) = (DP(0x15) & 1) ? 0x08 : 0x14;
    DP(0x17) = 0;
    if ((DP(0x14) & 0x0F) == 0x02) DP(0x34) -= 8;
    break;
  default:
    if (DP(0x15) > 0x20) { fprintf(stderr, "vwf: $15 = %02X past C2:5851's table\n", DP(0x15)); return -1; }
    break;                                                             // 5895
  }
  return run(v) < 0 || v->bad ? -1 : 0;
}
