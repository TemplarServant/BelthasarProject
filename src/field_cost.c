// The field's reload after a menu close, priced cycle for cycle -- see field_cost.h. Every number below is a block of
// LakeSnes's cost read off the cartridge's beam traces (the names and values tools/pass_cost_consts.py prints; the
// ranges are in bank C0), and the walk is the ROM's control flow over the WRAM it reads. The Python twin that grades
// this layout block by block is tools/pass_cost_proto.py.
#include <stdio.h>
#include "romview.h"
#include <stdlib.h>
#include <string.h>
#include "field_cost.h"
#include "lz.h"

#define BR_T  18      // a conditional branch taken (FastROM: opcode + operand + one idle)
#define BR_NT 12      //   ... not taken

typedef struct { uint8_t* w; const uint8_t* rom; long romLen; const char* why; int drawn; } Walk;

static int  b8(Walk* c, long a) { return c->w[a & 0x1FFFF]; }
static int  w16(Walk* c, long a) { return b8(c, a) | (b8(c, a + 1) << 8); }
static void sb(Walk* c, long a, int v) { c->w[a & 0x1FFFF] = (uint8_t)v; }
static void sw(Walk* c, long a, int v) { sb(c, a, v); sb(c, a + 1, v >> 8); }
static int  rom8(Walk* c, int bank, int addr) {
  long o = ((long)(bank & 0x3F) << 16) | (addr & 0xFFFF);
  return CtRomByte(c->rom, c->romLen, o);                  // (the container's named table when one covers it: romview.h)
}
static int  mem8(Walk* c, int bank, int addr) {            // a byte as the CPU reads it at bank:addr
  if (bank == 0x7E || bank == 0x7F) return c->w[((bank - 0x7E) << 16) | (addr & 0xFFFF)];
  if ((bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && (addr & 0xFFFF) < 0x2000) return c->w[addr & 0x1FFF];
  return rom8(c, bank, addr);
}
// `LDA [dp],Y` with D's low byte 0 reading bytes >= $8000 of `bank`: opcode + operand (12), the 3-byte pointer from
// the direct page (24), the data -- 6 a byte from FastROM, 8 from WRAM or a slow bank (snes_getAccessTime)
static long longRead(int bank, int nbytes) { return 36 + (long)nbytes * (bank >= 0xC0 ? 6 : 8); }
static void unpriced(Walk* c, const char* why) { if (!c->why) c->why = why; }

// =================================================================================================================
// C0/286C's pass
// =================================================================================================================

// ---- C0/7506 + C0/7553: the camera from the arrival tile ($0102/$0103) against the map's bounds ($1D1A-$1D1D)
static long camera(Walk* c) {
  long k = 110;                                            // CAMX_HEAD  7506-7511
  int lo = b8(c, 0x1D1A), hi = b8(c, 0x1D1B), t = b8(c, 0x0102), s = (lo + 8) & 0xFF;
  if (t == s) { k += 132; sb(c, 0x1D0A, lo << 1); sb(c, 0x1D0C, (lo << 1) + 0x20); }          // CAMX_EQ
  else if (t > s) {
    k += 156;                                              // CAMX_GT  752E-753B
    int a = ((t - 8) << 1) & 0xFF; sb(c, 0x1D0A, a);
    if (((hi - 8) & 0xFF) >= t) { k += 82; sb(c, 0x1D0C, a + 0x20); }                          // CAMX_GT_IN
    else { k += 138; int e = ((hi + 1) << 1) & 0xFF; sb(c, 0x1D0C, e); sb(c, 0x1D0A, e - 0x20); } // CAMX_GT_CLAMP
  } else { k += 138; sb(c, 0x1D0A, lo << 1); sb(c, 0x1D0C, (lo << 1) + 0x20); }                // CAMX_LT
  k += 150;                                                // CAMY_HEAD  RTS + 7553-755E
  lo = b8(c, 0x1D1C); hi = b8(c, 0x1D1D); t = b8(c, 0x0103); s = (lo + 8) & 0xFF;
  if (t == s) { k += 132; sb(c, 0x1D0E, lo << 1); sb(c, 0x1D10, (lo << 1) + 0x1C); }          // CAMY_EQ
  else if (t > s) {
    k += 156;
    int a = ((t - 8) << 1) & 0xFF; sb(c, 0x1D0E, a);
    if (((hi - 6) & 0xFF) >= t) { k += 82; sb(c, 0x1D10, a + 0x1C); }
    else { k += 138; int e = ((hi + 1) << 1) & 0xFF; sb(c, 0x1D10, e); sb(c, 0x1D0E, e - 0x1C); }
  } else { k += 138; sb(c, 0x1D0E, lo << 1); sb(c, 0x1D10, (lo << 1) + 0x1C); }
  return k + 40;                                           // CAMY_RTS
}

// ---- C0/28E1: re-apply the opened-door list ($7F:1CC8, packed, ends at the first negative word) to the map
static long doors28E1(Walk* c) {
  long k = 36;                                             // D28E1_HEAD
  for (int i = 0; i < 16; i++) {
    int v = w16(c, 0x11CC8 + 2 * i);
    k += 40;                                               // D28E1_ENTRY  LDA $7F1CC8,X
    if (v & 0x8000) return k + 76;                         // D28E1_END
    k += 142;                                              // D28E1_CALL   JSR 28F9 .. LDA $7E3000,X
    int tile = b8(c, 0x3000 + v);
    static const int tv[4] = { 0xE6, 0xEC, 0xEE, 0xFA }, hit[4] = { 214, 326, 326, 522 };
    int j;
    for (j = 0; j < 4; j++) { k += 12; if (tile == tv[j]) { k += hit[j]; break; } k += 12; }   // CMP / BEQ arms
    if (j == 4) { if (tile == 0xFC) { unpriced(c, "door list: an FC tile (C0/29BA)"); return k; } k += 12 + 18; }
    k += 92 + 42;                                          // D28F9_RET, D28E1_NEXT
    k += (i < 15) ? 18 : 70;                               // D28E1_LOOP / D28E1_FULL
    if (i == 15) return k;
  }
  return k;
}

// ---- C0/A712: the $7E:70C0 page, then the location's chests ($F5:F000 ranges; C0/A7BC tests $7F:0001+)
static long chestsA712(Walk* c, int loc) {
  long k = 201750;                                         // A712_FILL
  int base = rom8(c, 0xF5, 0xF000) | (rom8(c, 0xF5, 0xF001) << 8);
  int lo = rom8(c, 0xF5, 0xF000 + 2 * loc) | (rom8(c, 0xF5, 0xF001 + 2 * loc) << 8);
  int hi = rom8(c, 0xF5, 0xF002 + 2 * loc) | (rom8(c, 0xF5, 0xF003 + 2 * loc) << 8);
  if (lo == hi) return k + 76;                             // A712_NONE
  if ((rom8(c, 0xF5, lo) | (rom8(c, 0xF5, lo + 1) << 8)) == 0) { unpriced(c, "chests: C0/A763"); return k; }
  k += 190;                                                // A712_SOME
  for (int x = lo; ; x += 4) {
    k += 28;                                               // A712_TEST
    if (x >= hi) return k + 76;                            // A712_DONE
    int n = ((x - base) >> 2) & 0xFFFF, bit = rom8(c, 0xC0, 0xFF20 + (n & 7));
    if (b8(c, 0x10001 + ((n >> 3) & 0x3F)) & bit) { unpriced(c, "chests: an opened chest (C0/A7E6)"); return k; }
    k += 898;                                              // A712_CLOSED
  }
}

// ---- C0/7170 -> ($7181,X): the OBJ palette allocator (the port's is ct_vm.c palAlloc7170)
static void palClaim(Walk* c, int x, int slot) {
  sb(c, 0x0B00 + x, 0x80); sb(c, 0x0B01 + x, 0x80); sb(c, 0x0B80 + slot, x); sb(c, 0x0F81 + x, (slot << 1) & 0x0E);
}
static long palScan(Walk* c, int x, int lo, int end, int is721C) {
  long tail = 2772;                                        // P721C_TAIL / P72B4_TAIL (two 24-byte MVNs from $E4)
  long k = 12;                                             // _INI
  int s;
  for (s = lo; s < end; s++) {                             // pass 1: share an in-use slot with the same $1400 key
    int own = b8(c, 0x0B80 + s);
    k += 26;                                               // _TOP
    if (!(own & 0x80)) {
      if (!is721C) { unpriced(c, "palette: C0/72B4 share scan"); return k; }
      k += 176;                                            // P721C_USED
      if (w16(c, 0x1400 + own) == w16(c, 0x1400 + x)) {
        k += 208 + tail;                                   // P721C_SHARE
        sb(c, 0x0B00 + x, own); sb(c, 0x0B01 + x, 0x80); sb(c, 0x0B01 + own, x);
        sb(c, 0x0B80 + s, x); sb(c, 0x0F81 + x, (s << 1) & 0x0E);
        return k;
      }
      k += 80;                                             // P721C_NEXT
      if (s + 1 < end) { k += BR_T; continue; }
      k += BR_NT; unpriced(c, "palette: C0/7283"); return k;
    }
    k += 36;                                               // _FREE
    k += (s + 1 < end) ? BR_T : BR_NT;
  }
  k += 12;                                                 // _INI2
  for (s = lo; s < end; s++) {                             // pass 2: claim the first free slot
    int own = b8(c, 0x0B80 + s);
    k += 26;                                               // _TOP2
    if (own & 0x80) { k += 148 + tail; palClaim(c, x, s); return k; }        // _CLAIM
    if (!is721C) { unpriced(c, "palette: C0/7342"); return k; }
    k += 42;                                               // P721C_USED2
    if (s + 1 >= end) { unpriced(c, "palette table full"); return k; }
    k += 36;                                               // P721C_BRL2
  }
  return k;
}
static long pal7170(Walk* c, int x) {
  long k = 20;                                             // P7170_HEAD
  if (b8(c, 0x0054) & 0x80) return k + BR_T + 40;
  k += BR_NT + 46;                                         // P7170_CLS
  int t = b8(c, 0x1100 + x);
  if (t & 0x80) return k + BR_T + 40;
  k += BR_NT + 76 + 40;                                    // P7170_JMP + P7170_RTS
  if (t <= 2) {
    k += (t == 0) ? 12 : 36;                               // P7199 / P7212 / P7217
    int slot = 5 + t;
    k += 26;                                               // P719B
    if (!(b8(c, 0x0B80 + slot) & 0x80)) { unpriced(c, "palette: C0/7199 share"); return k; }
    k += BR_NT + 2908;                                     // P71A0_CLAIM
    palClaim(c, x, slot);
    return k;
  }
  if (t == 5 || t == 6) return k + palScan(c, x, 1, 4, 0);
  if (t < 12) return k + palScan(c, x, 0, 5, 1);
  unpriced(c, "palette: class >= 12");
  return k;
}

// ---- C0/6F9A -> ($6FA7,X): classes 3/4 take a VRAM slot from $0B88 (C0/6FBA)
static long vram6F9A(Walk* c, int x) {
  long k = 174;                                            // V6F9A
  int t = b8(c, 0x1100 + x);
  if (t != 3 && t != 4) return k + 40;                     // V6FB9
  if (b8(c, 0x1201 + x) & 3) { unpriced(c, "VRAM slot: C0/7008"); return k; }
  k += 88;                                                 // V6FBA
  for (int s = 0x19; s > 0; s--) {
    k += 26;                                               // V6FC7
    if (b8(c, 0x0B88 + s) & 0x80) {
      k += 618;                                            // V6FCC
      sb(c, 0x0B88 + s, x); sw(c, 0x0D00 + x, ((s & 0xFE) << 4) + ((s & 1) << 3)); sw(c, 0x1700 + x, s << 5);
      return k;
    }
    k += 48;                                               // V7003
  }
  unpriced(c, "VRAM slots exhausted");
  return k;
}

// ---- C0/B127: the camera window; a visible object gets its draw state reset, C0/7170 and C0/6F9A
static long b127(Walk* c, int x, int* vis) {
  *vis = 0;
  long k = 26;                                             // B127_HEAD
  int a = b8(c, 0x1A81 + x);
  if (a == 0) return k + BR_T + 52;
  k += BR_NT;
  if (a & 0x80) return k + BR_T + 52;
  k += BR_NT + 50;                                         // B127_CAM
  int c3 = (b8(c, 0x1D0A) - 4) & 0xFF;
  if (c3 & 0x80) { k += BR_NT + 12; c3 = 0; } else k += BR_T;
  k += 116;                                                // B127_CAM2
  int c4 = (b8(c, 0x1D0C) + 4) & 0xFF, c5 = b8(c, 0x1D0E);
  if (c5 & 0x80) { k += BR_NT + 12; c5 = 0; } else k += BR_T;
  k += 168;                                                // B127_CAM3
  int c6 = (b8(c, 0x1D10) + 5) & 0xFF;
  int px = (b8(c, 0x1801 + x) << 1) & 0xFF, py = (b8(c, 0x1881 + x) << 1) & 0xFF;
  if (px < c3) return k + BR_T + 52;
  k += BR_NT + 20;
  if (px >= c4) return k + BR_T + 52;
  k += BR_NT + 58;
  if (py < c5) return k + BR_T + 52;
  k += BR_NT + 20;
  if (py >= c6) return k + BR_T + 52;
  k += BR_NT + 392;                                        // B127_VIS
  sb(c, 0x0E80 + x, 0x80); sb(c, 0x0E81 + x, 0x80); sb(c, 0x0F00 + x, 0x80);
  sb(c, 0x1681 + x, 0); sb(c, 0x1601 + x, 0); sb(c, 0x1B00 + x, 0); sb(c, 0x0F01 + x, 0xFF);
  k += pal7170(c, x) + vram6F9A(c, x);
  *vis = 1;
  return k;
}

// ---- C0/AB45 (the screen position) and C0/A9CD (the draw-list bucket by screen Y / 2)
static long ab45(Walk* c, int x) {
  int dd = ((b8(c, 0x1D0A) << 3) + (w16(c, 0x1D87) & 7)) & 0xFFFF;
  int df = ((b8(c, 0x1D0E) << 3) + (w16(c, 0x1D89) & 7)) & 0xFFFF;
  long k = 632;                                            // AB45_HEAD
  int sx = (w16(c, 0x1800 + x) >> 4) - dd;
  if (sx < 0) { k += BR_NT + 18; sx = (sx & 0xFFFF) | 0x100; } else k += BR_T;
  sw(c, 0x0A00 + x, sx & 0x1FF);
  k += 180;                                                // AB45_MID
  int sy = (w16(c, 0x1880 + x) >> 4) - df;
  if (sy < 0) { k += BR_NT + 18; sy = (sy & 0xFFFF) | 0x100; } else k += BR_T;
  sw(c, 0x0A80 + x, sy & 0x1FF);
  return k + 116;                                          // AB45_END
}
static long a9cd(Walk* c, int x) {
  long k = 64;                                             // A9CD_HEAD
  if (!(b8(c, 0x0E01 + x) & 0x80)) return k + BR_T + 40;
  k += BR_NT + 88;                                         // A9CD_MID
  int y = (b8(c, 0x0A80 + x) >> 1) & 0xFE, head = b8(c, 0x0E00 + y);
  if (head & 0x80) {
    sb(c, 0x0E00 + y, x); sb(c, 0x0E01 + x, 0); sb(c, 0x0E80 + x, 0x80); sb(c, 0x0E81 + x, 0x80);
    return k + BR_NT + 192;                                // A9CD_EMPTY
  }
  sb(c, 0x0E80 + head, x); sb(c, 0x0E81 + x, head); sb(c, 0x0E00 + y, x); sb(c, 0x0E01 + x, 0);
  return k + BR_T + 256;                                   // A9CD_INSERT
}

// ---- C0/CAD9, class 0 (C0/CBDC): the frame lookup, the arena grant, 16 pieces, the OAM template
static long cb3a(Walk* c, int x, int* frame) {
  long k = 26;                                             // CB3A_HEAD
  int m = b8(c, 0x1600 + x), b1480 = w16(c, 0x1480 + x), b1500 = w16(c, 0x1500 + x), d6;
  if (m == 0) { k += 116; d6 = b1500; }                    // CB3A_M0
  else {
    k += 24;                                               // CB3A_NZ
    if (m == 2) { k += 156; d6 = b1480 * 2 + b1500; }      // CB3A_M2
    else if (!(m & 0x80) && m > 2) { k += 258; d6 = b1480 * 3 + b1500; }   // CB3A_M3
    else { k += 168; d6 = b1480 + b1500; }                 // CB3A_M1
  }
  if (b8(c, 0x1780 + x) == 2) { unpriced(c, "CB3A: $1780 = 2 (C0/CB8D)"); return k; }
  d6 = (d6 + b8(c, 0x1680 + x) * 4 + b8(c, 0x1681 + x)) & 0xFFFF;
  *frame = rom8(c, 0xE4, d6);
  if (*frame == 0xFF) { unpriced(c, "CB3A: the sequence terminator (C0/CBC8)"); return k; }
  return k + 482;                                          // CB3A_TAIL
}
static long e534(int cf) { return 2800 + 32 * longRead(cf, 1); }               // E534_FIXED + 32 [$CD],Y reads
static long e687(Walk* c, int cf) {
  switch (cf) {
    case 0x7F: return 236 + 1578;                          // E687_HEAD + E687_7F
    case 0xD2: return 236 + 1556;
    case 0xD3: return 236 + 1586;
    case 0xD4: return 236 + 1616;
    default: unpriced(c, "E687: a graphics bank no trace ran (C0/E6B6)"); return 236;
  }
}
static long cad9(Walk* c, int x) {
  long k = 232;                                            // CAD9_GATE (class 0)
  int a = b8(c, 0x1A81 + x);
  if ((b8(c, 0x1100 + x) & 0x80) || a == 0 || (a & 0x80) || b8(c, 0x0F00 + x) == 0) {
    unpriced(c, "CAD9 gate"); return k;
  }
  if (b8(c, 0x1201 + x) & 3) { unpriced(c, "CAD9: class 1/2 (C0/CEF5, C0/D4F7)"); return k; }
  if (b8(c, 0x1780 + x) == 3) { unpriced(c, "CAD9: $1780 = 3 (C0/CBF9)"); return k; }
  k += 304;                                                // CBDC_HEAD
  int frame = 0;
  k += cb3a(c, x, &frame);
  if (c->why) return k;
  if (frame == b8(c, 0x0F01 + x)) { unpriced(c, "CAD9: frame unchanged (C0/CC09)"); return k; }
  k += 122;                                                // CC01_CC10
  if (!(b8(c, 0x0BC0) & 0x80)) { unpriced(c, "E952: slot 0 taken"); return k; }
  k += 340;                                                // E952
  sb(c, 0x0BC0, x); sw(c, 0x0D80 + x, 0x3800);
  sb(c, 0x0F01 + x, frame);
  int cf = b8(c, 0x1200 + x), fbank = b8(c, 0x1300 + x);
  int d3 = (frame * 0x28 + w16(c, 0x1380 + x)) & 0xFFFF;
  k += 648;                                                // CC10_CC5D
  for (int p = 0; p < 16; p++) {
    if (p) k += 86;                                        // PIECE_NEXT
    int word = mem8(c, fbank, d3 + 2 * p) | (mem8(c, fbank, d3 + 2 * p + 1) << 8);
    k += longRead(fbank, 2) + 18;                          // LDA [$D3],Y + PIECE_READ_OP
    int last = (p == 15);
    if (word & 0x4000) k += 132 + e534(cf) + (last ? BR_NT : BR_T);             // PIECE_534
    else k += 126 + e687(c, cf) + (last ? BR_NT + 18 : BR_T);                    // PIECE_687 (+ PIECE_687_LAST)
  }
  int tb[8];
  for (int i = 0; i < 8; i++) tb[i] = mem8(c, fbank, d3 + 0x20 + i);
  k += 866 + longRead(fbank, 1);                           // CC78_TEMPL + its read
  static const long rest[4] = { 120, 120, 120, 76 };       // TPLn_REST (each with two [$D3],Y reads but the last)
  for (int i = 0; i < 4; i++) {
    k += (tb[2 * i] & 0x80) ? 42 : 30;                     // TPLn_NEG / TPLn_POS
    k += rest[i] + (i < 3 ? 2 : 1) * longRead(fbank, 1);
  }
  int t = w16(c, 0x1700 + x);
  for (int i = 0; i < 8; i++) sb(c, 0x14802 + t + (i / 2) * 8 + (i & 1) * 2, tb[i]);
  k += 388;                                                // CD2C
  for (int i = 0; i < 4; i++) {
    k += (tb[2 * i + 1] < 0xE8) ? 102 : 114;               // ATTRn_LO / ATTRn_HI
    if (i < 3) k += 44;                                    // ATTRn_JOIN
  }
  k += 144;                                                // CDBF
  sb(c, 0x1B00 + x, b8(c, 0x1B00 + x) + 1);
  int q = b8(c, 0x0079);                                   // C0/CCAC-CCB4: the upload queue's entry
  sb(c, 0x09A0 + q, b8(c, 0x09A0 + q) + 1); sb(c, 0x09A2 + q, 0); sb(c, 0x0079, q + 2);
  return k;
}

// ---- C0/B1B2 (two $0100-byte DMAs per queued object), C0/EA42 (the arena release)
static long b1b2(Walk* c) {
  long k = 228;                                            // B1B2_HEAD
  for (int q = 0; q < 16; q += 2) {
    k += 26;                                               // B1B2_TEST
    if (b8(c, 0x09A0 + q) == 0) { sb(c, 0x09A0, 0); return k + 78; }         // B1B2_END
    k += 4654;                                             // B1B2_ENTRY: the DMAs with the traces' mean sync
  }
  unpriced(c, "B1B2 queue overflow");
  return k;
}
static long ea42(Walk* c, int x) {
  if (b8(c, 0x0BC0) != x || b8(c, 0x0BC1) == x) { unpriced(c, "EA42: arena slots"); return 0; }
  sb(c, 0x0BC0, 0x80);
  return 254;                                              // EA42
}

// ---- C0/B0E6: the per-object sweep
static long b0e6(Walk* c) {
  int n = b8(c, 0x12000) * 2;
  long k = 94;                                             // B0E6_PRO
  for (int x = 0; ; ) {
    k += 46;                                               // B0E6_OBJ
    int t = b8(c, 0x1100 + x);
    if (t & 0x80) k += 18;                                 // B0E6_DEAD
    else {
      k += 24;                                             // B0E6_CLS
      if (t == 7) k += 18;                                 // B0E6_CLS7
      else {
        int vis;
        k += 52 + b127(c, x, &vis);                        // B0E6_TEST
        if (!vis) k += 18;                                 // B0E6_HIDDEN
        else {
          sb(c, 0x0079, 0);                                // C0/B107 `STZ $79`
          k += 342 + ab45(c, x) + a9cd(c, x) + cad9(c, x) + b1b2(c) + ea42(c, x);   // B0E6_DRAW
          c->drawn++;
        }
      }
    }
    if (c->why) return k;
    k += 64;                                               // B0E6_TAIL
    x += 2;
    if (x < n) k += BR_T; else { k += BR_NT; break; }
  }
  return k + 42;                                           // B0E6_END (the BRL C0/B271)
}

// ---- C0/B271: the draw pass; C0/B309 per object (C0/B701 -> C0/B8CA + C0/E9E2, then stage 2)
static long b8ca(Walk* c, int x) {
  long k = 2140;                                           // B8CA_X (the prologue and the X half)
  int t = w16(c, 0x1700 + x), sy = w16(c, 0x0A80 + x) & 0x1FF, c5 = sy & 0xFF, c6 = sy >> 8;
  long rec = 0x14800 + t;
  int disp[5];
  for (int i = 0; i < 4; i++) disp[i] = b8(c, rec + 4 + 8 * i);
  disp[4] = b8(c, 0x14BE4 + t);
  if (c6) {                                                // arm A (Y >= $100): five checks, the fifth $4BE4,X
    k += 12;                                               // B8CA_A_ENTRY
    for (int i = 0; i < 5; i++) {
      k += (i < 4) ? 96 : 64;                              // B8CA_An_HEAD
      int s = disp[i] + c5;
      if (s < 0x100) k += 30;                              // B8CA_An_NC (BCC -> LDA #E0)
      else { k += 24; k += ((s & 0xFF) >= 0xE0) ? 18 : 24; }   // _CMP, then _HI / _CLAMP
      k += 32;                                             // B8CA_An_ST
    }
    return k + 422;                                        // B8CA_A_END
  }
  k += 38;                                                 // B8CA_BC
  if (c5 & 0x80) {                                         // arm B ($80-$FF): carry, then < E0
    k += 12;
    for (int i = 0; i < 4; i++) {
      k += 96;
      int s = disp[i] + c5;
      if (s < 0x100) k += 18;
      else { k += 24; k += ((s & 0xFF) < 0xE0) ? 18 : 24; }
      k += 32;
    }
    k += 18;                                               // B8CA_B_BRA
  } else {                                                 // arm C (< $80): sign, then >= E0
    k += 18;
    for (int i = 0; i < 4; i++) {
      k += 96;
      int s = (disp[i] + c5) & 0xFF;
      if (!(s & 0x80)) k += 18;
      else { k += 24; k += (s >= 0xE0) ? 18 : 24; }
      k += 32;
    }
  }
  return k + 422;                                          // B8CA_BA47
}
static long b309(Walk* c, int x) {
  if (b8(c, 0x1201 + x) & 3) { unpriced(c, "B701: class 1/2"); return 0; }
  int s = b8(c, 0x1B00 + x);
  if (s == 0 || (s & 0x80)) { unpriced(c, "B701: $1B00"); return 0; }
  long k = 232 + b8ca(c, x);                               // B309_HEAD
  for (int i = 0; i < 4; i++) if (b8(c, 0x0BC0 + i) == x) { unpriced(c, "E9E2 finds a slot"); return k; }
  k += 502 + 438;                                          // E9E2, B309_MID
  sb(c, 0x1B00 + x, 0x80);
  int g = b8(c, 0x0F80 + x) & 0x0C, cur;
  if (g == 0) { k += 370; cur = 0x01DB; }                  // B309_G0
  else if (g & 4) { k += 412; cur = 0x01DD; }              // B309_G1
  else { k += 424; cur = 0x01DF; }                         // B309_G2
  sw(c, cur, w16(c, cur) + 0x10);
  return k + 1568;                                         // B309_OAM
}
static long b271(Walk* c) {
  long k = 488;                                            // B271_HEAD
  sw(c, 0x01DB, 0x0770); sw(c, 0x01DD, 0x08A0); sw(c, 0x01DF, 0x0710);
  for (int y = 0x7E; y >= 0; y -= 2) {
    k += 32;                                               // B271_BUCKET
    int head = b8(c, 0x0E00 + y);
    if (head & 0x80) k += 18;                              // B271_EMPTY
    else {
      k += 72 + b309(c, head);                             // B271_HEADOBJ
      for (int o = head, guard = 0; guard < 128; guard++) {
        k += 60;                                           // B271_CHAIN
        int nx = b8(c, 0x0E81 + o);
        if (nx & 0x80) { k += 18; break; }                 // B271_CHAIN_END
        k += 72 + b309(c, nx) + 18;                        // B271_CHAIN_OBJ + B271_CHAIN_BRA
        o = nx;
      }
    }
    if (c->why) return k;
    k += 24 + (y > 0 ? BR_T : BR_NT);                      // B271_DEY
  }
  k += 40;                                                 // B271_FILLS
  static const int cur[3] = { 0x01DB, 0x01DD, 0x01DF }, mark[3] = { 0x017B, 0x017D, 0x017F },
                   end[3] = { 0x08A0, 0x0900, 0x0770 }, tailc[3] = { 84, 84, 96 };
  for (int g = 0; g < 3; g++) {                            // hide the stale tail up to last frame's mark
    int p = w16(c, cur[g]), prev = w16(c, mark[g]);
    for (;;) {
      k += 28;                                             // Fn_TOP
      if (p >= prev) { k += BR_T; break; }
      k += 110;                                            // Fn_BODY
      p += 4;
      if (p < end[g]) k += BR_T; else { k += BR_NT; break; }
    }
    sw(c, mark[g], w16(c, cur[g]));
    k += tailc[g];                                         // Fn_TAIL
  }
  return k;
}

// ---- C0/78EC: BG2's tilemap build (C0/0A50, `JSR C0/78EC` at C0/0AAB), from that JSR to its return. Rows of C0/79CF
// (sixteen cells, four WMDATA bytes each); the ARM by $1D35 bit 1 and $0BC9 bit 7:
//   standard   32 rows from ($1D0A, $1D0E): the entry camera is always even (C0/7506/7553's `<< 1`), so it has ONE cost,
//              and PASS_REST carries it
//   C0/794E    ($1D35 bit 1) 32 rows at column 0, then by $0BCF / $0BD1: 7976 (both $10) the same 32 again, 7991 ($0BCF
//              != $10) 32 at column $20, 79AE ($0BD1 != $10) rows 32-63 and `$1D85 := 2`
//   C0/792F    ($0BC9 bit 7) rows $1D16..$1D18 at column $1D12, which C0/74B1 set from the camera
// A row costs by its parity and its first column's (C0/79CF's two tests): 10010 / 10028 from an even column, 10220 /
// 10394 from an odd one (a half cell, fifteen, a half: 7A2A / 7B12). MEASURED on --cputrace beam traces, non-refresh:
// the hub's standard arm 327,358 and loc 0AD's 794E + 7976 652,856, both to the cycle; the arms no trace ran are priced
// instruction by instruction on the key of an executed one (tools/ctlib/costtab.py lin_rom).
#define B78_STD 327358L
static long row79CF(int row, int col) {
  return (col & 1) ? ((row & 1) ? 10394 : 10220) : ((row & 1) ? 10028 : 10010);
}
static long b78EC(Walk* c) {
  if (b8(c, 0x1D35) & 0x02) {                              // 7900 BEQ not taken -> BRL C0/794E
    long k = 284 + 326194;                                 // the head and the 32 rows at column 0 (rows 0-31)
    if (b8(c, 0x0BCF) != 0x10) k += 326718;                // 7991: 32 rows at column $20
    else if (b8(c, 0x0BD1) != 0x10) { k += 326800; sb(c, 0x1D85, 2); }   // 79AE: rows 32-63, $85 := 2
    else k += 326378;                                      // 7976: rows 0-31 again
    return k;
  }
  if (b8(c, 0x0BC9) & 0x80) {                              // 7908 BMI -> BRL C0/792F: rows $16..$18 at column $12
    int col = b8(c, 0x1D12), r = b8(c, 0x1D16), r1 = b8(c, 0x1D18);
    long k = 380 + 40;                                     // the caller's JSR .. 7937, and the RTS
    for (int guard = 0; guard < 256; guard++, r = (r + 1) & 0xFF) {
      int a = (r + 1) & 0xFF;                              // 7944 LDA $5A / INC A / CMP $18
      k += 184 + row79CF(r, col) + (a < r1 ? 18 : a == r1 ? 30 : 24);   // BCC taken / BEQ taken / both not
      if (a > r1) return k;
    }
    unpriced(c, "C0/792F: a row range that never ends");
    return k;
  }
  return B78_STD;
}

// ---- A7E9 / B204: resets over the object count n = $7F2000
static long a7e9(int n) { return 240 + (long)n * 48 + (n - 1) * BR_T + BR_NT + 92; }
static long b204(int n) { return 11898 + (long)n * 60 + (n - 1) * BR_T + BR_NT + 74; }

// The part of the pass the walk does not branch through: C0/286C's own instructions, C0/0A50 without its BG3 arm
// (C0/75E9 and C0/78EC -- the builders, whose parity tests run on the camera's even coordinates -- two C0/7F62
// uploads, C0/87F1, C0/74A6's C0/74AC-74D1 and C0/7F9A, whose C0/813D family reads the $1D99-$1D9E C0/74C7 zeroes),
// C0/6F79, C0/7155, C0/E935, and after C0/B0E6: C0/6E5C, 70E9, 29F7, 6EB0, 2B78 (its counts are immediates), 6EF1,
// 6F0C, B262, 6F5E, and C0/0B28's FD:C124 / FD:C2C1 / FD:C1EE up to the wait. MEASURED as the traces' remainder
// once the walked legs are taken out: 874,914 / 874,876 / 874,902 on loc $082 / $005 / $078 -- the spread is the
// DMA legs' sync to the master clock; the mean.
#define PASS_REST 874897L
#define PASS_BG3  (298554L + 17000 + 248)   // C0/7CB5 (the BG3 builder), C0/7F62 (its upload), C0/0AD8-0AF3's arm

static long pass286C(Walk* c, int loc) {
  // The configuration the traces cover: every arm below is the ROM's, taken on the flags it reads.
  if (b8(c, 0x1D35) & 0x04) { unpriced(c, "pass: $1D35 bit 2 (the BG3 builder's C0/8385 arm)"); return -1; }
  if (b8(c, 0x0126) != 0)   { unpriced(c, "pass: $0126 != 0 (the FD:C2DF/C2E5 handlers 1-2)"); return -1; }
  if (b8(c, 0x1DF9) || b8(c, 0x1DFD)) { unpriced(c, "pass: $1DF9 / $1DFD (the HDMA handlers' effect arms)"); return -1; }
  if ((b8(c, 0x0153) & 0x80) || b8(c, 0x0BE9) || (b8(c, 0x0351) & 1)) {
    unpriced(c, "pass: $0153 bit 7 / $0BE9 / $0351 bit 0"); return -1;
  }
  int n = b8(c, 0x12000);
  long k = PASS_REST, d0, d1, d2, d3;
  k += (d0 = doors28E1(c));
  k += (d1 = chestsA712(c, w16(c, 0x0100) & 0x1FF));   // C0/A742 `LDA $0100 / AND #01FF` (the loader's own location)
  k += (d2 = camera(c));
  // C0/74AC `LDA $0BC9 / BPL`: with bit 7, 74B1-74C5 derive the BG2 window C0/792F builds from the camera just set
  // (+198: the BPL not taken and the block)
  if (b8(c, 0x0BC9) & 0x80) {
    int x = (b8(c, 0x1D0A) >> 1) & 0xFE, y = b8(c, 0x1D0E) >> 1;
    sb(c, 0x1D12, x); sb(c, 0x1D14, x + 0x20); sb(c, 0x1D16, y); sb(c, 0x1D18, y + 0x1C);
    k += 198;
  }
  k += (d3 = b78EC(c)) - B78_STD;                          // C0/78EC's arm, in place of the standard one PASS_REST holds
  // FD:C1EE's `LDA $013C / BIT #01 / BNE`: the $2129 arm is `LDA #29` alone where $2128's is `LDA #28 / BRA` (-12)
  if (b8(c, 0x013C) & 1) k -= 12;
  if (getenv("CT_PASSCOST_DEBUG")) printf("passcost: doors %ld chests %ld camera %ld 78EC %ld n %d\n", d0, d1, d2, d3, n);
  if ((b8(c, 0x0BCA) & 1) && !b8(c, 0x01BB)) {
    if (b8(c, 0x1D35) & 0x04) { unpriced(c, "pass: the BG3 upload C0/7F77"); return -1; }
    k += PASS_BG3;
  }
  k += a7e9(n) + b204(n);
  // the tables C0/B0E6 finds: C0/6F79 ($0B88), C0/7155 ($0B80), C0/E935 ($0BC0), C0/B204 (the $0E00 page, then
  // $0F01/$0F02 per object), C0/A7E9 ($0F00/$0F01 per object); C0/01B4-01C1 set the three fill marks
  for (int i = 0; i < 0x1E; i++) sb(c, 0x0B88 + i, 0x80);
  for (int i = 0; i < 8; i++) { sb(c, 0x0B80 + i, 0x80); sb(c, 0x0BC0 + i, 0x80); }
  for (int i = 0; i < 0x100; i++) sb(c, 0x0E00 + i, 0x80);
  for (int i = 0; i < n; i++) { sb(c, 0x0F00 + 2 * i, 0x00); sb(c, 0x0F01 + 2 * i, 0xFF); sb(c, 0x0F02 + 2 * i, 0x00); }
  sw(c, 0x017B, 0x08A0); sw(c, 0x017D, 0x0900); sw(c, 0x017F, 0x0770);
  { long e = b0e6(c), f = c->why ? 0 : b271(c); k += e + f;
    if (getenv("CT_PASSCOST_DEBUG")) printf("passcost: a7e9+b204 %ld b0e6 %ld b271 %ld\n", a7e9(n) + b204(n), e, f); }
  // C0/0B28's FD:C1EE picks the HDMA table by `$0153 & 0F`, which FD:C2C1 flipped just before (bit 0)
  k += (((b8(c, 0x0153) ^ 1) & 0x0F) == 0) ? BR_T : BR_NT + 18;
  return k;
}

// =================================================================================================================
// The reload's C0/0B5D -> C0/286C (C0/01A5 and the caller's legs)
// =================================================================================================================

// ---- C0/595C: object 0's slot 1 re-run (ops 00, 12, 16; any other op is not walked)
static int vmCmp(int op, int d9, int db, long* k) {        // the $6467 comparators: 1 = true (CLC, fall through)
  switch (op) {
    case 0: *k += (d9 == db) ? 110 : 104; return d9 == db;
    case 1: *k += (d9 != db) ? 110 : 104; return d9 != db;
    case 2: *k += (d9 == db) ? 110 : (d9 > db) ? 122 : 116; return d9 > db;
    case 3: *k += (d9 < db) ? 110 : 104; return d9 < db;
    case 4: *k += (d9 == db) ? 110 : (d9 > db) ? 122 : 116; return d9 >= db;
    case 5: *k += (d9 == db) ? 110 : (d9 < db) ? 122 : 116; return d9 <= db;
    case 6: *k += (d9 & db) ? 110 : 104; return (d9 & db) != 0;
    default: *k += (d9 | db) ? 110 : 104; return (d9 | db) != 0;
  }
}
static long vm595C(Walk* c) {
  long k = 116;                                            // VM_HEAD
  int pos = w16(c, 0x12003);
  for (int guard = 0; guard < 64; guard++) {
    int op = b8(c, 0x12001 + pos);
    k += 32;                                               // VM_TOP
    if (op == 0) return k + 58;                            // VM_END
    k += 154;                                              // VM_DISPATCH
    int c3, truth;
    if (op == 0x12) {
      int d9 = b8(c, 0x10200 + b8(c, 0x12001 + pos + 1) * 2), db = b8(c, 0x12001 + pos + 2);
      int cmp = b8(c, 0x12001 + pos + 3);
      if ((cmp & 0x7F) >= 8) { unpriced(c, "slot-1 op 12: comparator >= 8"); return k; }
      k += 440;                                            // OP12_HEAD
      truth = vmCmp(cmp & 7, d9, db, &k);
      c3 = pos + 4;
      k += truth ? 122 : 294;                              // OP12_FALL / OP12_JUMP
    } else if (op == 0x16) {
      int idx = b8(c, 0x12001 + pos + 1), db = b8(c, 0x12001 + pos + 2), cmp = b8(c, 0x12001 + pos + 3), d9;
      k += 184;                                            // OP16_HEAD
      if (cmp & 0x80) { k += 258; d9 = b8(c, 0x10000 + (idx | 0x100)); cmp &= 0x7F; }   // OP16_HI
      else { k += 330; d9 = b8(c, 0x10000 + idx); }                                   // OP16_LO
      if (cmp >= 8) { unpriced(c, "slot-1 op 16: comparator >= 8"); return k; }
      truth = vmCmp(cmp, d9, db, &k);
      c3 = pos + 4;
      k += truth ? 128 : 312;                              // OP16_FALL / OP16_JUMP
    } else {
      static char msg[48];
      snprintf(msg, sizeof msg, "slot-1 op %02X (C0/595C)", op);
      unpriced(c, msg);
      return k;
    }
    pos = truth ? c3 + 1 : c3 + b8(c, 0x12001 + c3);
    k += 18;                                               // VM_BRA
  }
  unpriced(c, "slot-1 run too long");
  return k;
}

// the DMA C0/2DC8 runs for n bytes: 436 of setup and sync (the traces' mean) + 8 a byte
static long dma2DC8(long n) { return 436 + 8 * n; }

static long pack(Walk* c, long addr, int to7F) {           // one `JSL C3:0002`: the vector's JMP + the decompression
  long o = ((addr >> 16) & 0x3F) << 16 | (addr & 0xFFFF);
  return 18 + ct_lz_rom_cycles(c->rom, (size_t)c->romLen, (size_t)o, to7F);
}
static long ptr24(Walk* c, long fileOff) {
  return CtRomByte(c->rom, c->romLen, (long)(fileOff)) | ((long)CtRomByte(c->rom, c->romLen, (long)(fileOff + 1)) << 8) | ((long)CtRomByte(c->rom, c->romLen, (long)(fileOff + 2)) << 16);
}

static long prePass(Walk* c, int loc, int caller, long* vmOut) {
  const long REC = 0x360000L + (long)loc * 14;
  long k = 610;                                            // C0/0B5D -> C0/01A5 (0B4E's tail, 0B64, 0B75, D = $0100)
  // C0/01A5 -> C0/0960: C0/0905 (the $7F:2000 -> $7E:0920 MVN, 246,000), FD:C2C1, FD:C1EE, the three fill marks;
  // FD:C1EE's table by `$0153 & 0F` after FD:C2C1's flip of bit 0 (the port's $0153 is the menu's, as restored)
  k += 251060 + ((((b8(c, 0x0153) ^ 1) & 0x0F) == 0) ? BR_T : BR_NT + 18);
  sb(c, 0x0153, b8(c, 0x0153) ^ 1);
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "0960", k);
  // C0/0960: the tileset's list ($F6:1C00 + t*8), six VRAM packets (C0/6D2F, the sixth $0F80 bytes) and two for
  // bank $7F (C0/6D65)
  int ts = CtRomByte(c->rom, c->romLen, (long)(REC + 1));
  if (ts == 0xFF) k += 122 + 40;                           // 0960's FF arm, JSR 6DCF
  else {
    k += 1286 + 40;                                        // 0960's body (its six JSRs, the BRL), JSR 6DCF
    for (int i = 0; i < 8; i++) {
      int idx = CtRomByte(c->rom, c->romLen, (long)(0x361C00L + (long)ts * 8 + i));
      long src = (idx == 0xFF) ? 0 : ptr24(c, 0x362220L + (long)idx * 3);
      if (i < 6) k += (idx == 0xFF) ? 112 : 578 + pack(c, src, 0) + dma2DC8(i == 5 ? 0x0F80 : 0x1000);
      else if (i == 6) k += ((idx == 0xFF) ? 72 : 498 + pack(c, src, 1)) + 122;
      else k += (idx == 0xFF) ? 58 : 484 + pack(c, src, 1);
    }
  }
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "6DCF", k);
  // C0/6DCF: the BG3 tileset (record byte 2) -> $7F:5080 + a $1000 DMA; FF sets $BB
  int b3 = CtRomByte(c->rom, c->romLen, (long)(REC + 2));
  if (b3 == 0xFF) { k += 160; sb(c, 0x01BB, 1); }
  else k += 824 + pack(c, ptr24(c, 0x362220L + (long)b3 * 3), 1) + dma2DC8(0x1000);
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "7084", k);
  k += 40 + 10152 + 40 + 2820 + 40;                        // JSR 7084, C0/7084, JSR 7F7E, C0/7F7E, JSR A33B
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "A33B", k);
  // C0/A33B: the map, decompressed (lz.c) and built (lz.c ct_map_load_cycles: walked, exact on the traces)
  {
    int mapId = CtRomByte(c->rom, c->romLen, (long)(REC + 4)) | (CtRomByte(c->rom, c->romLen, (long)(REC + 5)) << 8);
    long me = ptr24(c, 0x361E00L + (long)mapId * 3), mo = ((me >> 16) & 0x3F) << 16 | (me & 0xFFFF);
    k += ct_lz_rom_cycles(c->rom, (size_t)c->romLen, (size_t)mo, 0);
    uint8_t* img = (uint8_t*)malloc(0x20000);
    if (img) {
      size_t len = ct_lz_decompress_bounded(c->rom, (size_t)c->romLen, img, 0x20000, (size_t)mo);
      long wk = ct_map_load_cycles(c->rom, (size_t)c->romLen, loc);
      if (wk >= 0) k += wk; else if (len) k += ct_map_build_cycles(img, len); else unpriced(c, "map build");
      if (wk < 0) unpriced(c, "map build: header bytes 2-3 (C0/A463 / C0/A4B3)");
      free(img);
    }
    // ...and the cells it builds at $7E:3000 (C0/A521): what C0/28E1 finds when it re-opens the listed doors -- the
    // map as the ROM stores it, not the one the menu left (an opened door is closed again until C0/28E1)
    if (!ct_lz_map_to_wram(c->rom, (size_t)c->romLen, loc, c->w)) unpriced(c, "map cells");
    k += 40;                                               // JSR 09DD
  }
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "09DD", k);
  // C0/09DD: the properties (record byte 1 again) -> $7E:B000
  if (ts == 0xFF) k += 128; else k += 554 + pack(c, ptr24(c, 0x362100L + (long)ts * 3), 0);
  k += 40;                                                 // JSR 0A14
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "0A14", k);
  // C0/0A14: the BG3 assembly -> $7E:C000, unless $BB
  if (b8(c, 0x01BB)) k += 78;
  else if (b3 == 0xFF) k += 160;
  else k += 586 + pack(c, ptr24(c, 0x3621C0L + (long)b3 * 3), 0);
  k += 40;                                                 // JSR 56D4
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "56D4", k);
  // C0/56D4: the script (record word 8) -> $7F:2000, over the menu's stash (C0/011B's $0920 copy), then C0/5CC7 /
  // C0/5CEC. What the rest of the walk reads there -- the object count $7F:2000, object 0's slot-1 run -- is THIS.
  {
    int sid = CtRomByte(c->rom, c->romLen, (long)(REC + 8)) | (CtRomByte(c->rom, c->romLen, (long)(REC + 9)) << 8);
    long sp = ptr24(c, 0x3CF9F0L + (long)sid * 3);
    k += 7186 + pack(c, sp, 1);
    size_t so = (size_t)(((sp >> 16) & 0x3F) << 16 | (sp & 0xFFFF));
    if (!ct_lz_decompress_bounded(c->rom, (size_t)c->romLen, c->w + 0x12000, 0xE000, so)) unpriced(c, "script packet");
  }
  sb(c, 0x1DF9, b8(c, 0x11D26));                           // C0/01FF `LDA $7F1D26 / STA $1DF9`
  k += 454;                                                // C0/01DB-0208: the four copies, $1DF9, JSR 595C
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "595C", k);
  long vm = vm595C(c);
  *vmOut = vm;
  k += vm;
  if (getenv("CT_PASSCOST_DEBUG")) printf("pre: %-6s %ld\n", "tail", k);
  // C0/0209-024B: $AE, $7F:03FE
  k += 100;                                                // TDC XBA LDA $AE / BMI taken / LDA $7F03FE
  if (!(b8(c, 0x01AE) & 0x80)) unpriced(c, "C0/020F: $01AE >= 0");
  int fe = b8(c, 0x103FE);
  if (fe == 0) k += 18; else if (fe >= 3) k += 42; else unpriced(c, "C0/021F: $7F03FE 1-2");
  k += 40;                                                 // RTS
  int n = b8(c, 0x12000);
  long b192 = 168 + (long)n * 62 + (n - 1) * BR_T + BR_NT + 76 + 340;   // C0/B192 (+ C0/E935)
  if (caller == CT_RELOAD_CALLER_X) {
    if (b8(c, 0x2980) != b8(c, 0x0194) || b8(c, 0x2981) != b8(c, 0x0195) || b8(c, 0x2982) != b8(c, 0x0196))
      unpriced(c, "C0/1A1C: the party changed in the menu");
    k += 272 + 40 + b192 + 40;                             // JSR 1A03 + C0/1A03, JSR B192, B192, JSR 2824
  } else {
    k += 40 + b192 + 116;                                  // JSR B192, B192, C0/0D33-0D3D (the flags, BRL 2824)
  }
  return k + 40;                                           // C0/2824 `JSR C0/286C`
}

long ct_field_pass_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, CtReloadCost* out) {
  CtReloadCost r = { 0, 0, 0, 0, NULL };
  if (out) *out = r;
  if (!rom || !wram || loc < 0 || loc > 0x1FF || romLen < 0x400000) return -1;
  Walk c = { (uint8_t*)malloc(0x20000), rom, romLen, NULL, 0 };
  if (!c.w) return -1;
  memcpy(c.w, wram, 0x20000);
  r.pass = pass286C(&c, loc);
  r.objects = c.drawn;
  r.why = c.why;
  free(c.w);
  if (out) *out = r;
  return r.why ? -1 : r.pass;
}

long ct_field_reload_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, int caller,
                            CtReloadCost* out) {
  CtReloadCost r = { 0, 0, 0, 0, NULL };
  if (out) *out = r;
  if (!rom || !wram || loc < 0 || loc > 0x1FF || romLen < 0x400000) return -1;
  Walk c = { (uint8_t*)malloc(0x20000), rom, romLen, NULL, 0 };
  if (!c.w) return -1;
  memcpy(c.w, wram, 0x20000);
  r.pre = prePass(&c, loc, caller, &r.vm);
  r.pass = c.why ? 0 : pass286C(&c, loc);
  r.objects = c.drawn;
  r.why = c.why;
  free(c.w);
  if (out) *out = r;
  return r.why ? -1 : r.pre + r.pass;
}
