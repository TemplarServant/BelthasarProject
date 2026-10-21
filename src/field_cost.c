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
// C0/72B4 (classes 5 / 6, slots 1-3) is C0/721C (slots 0-4) instruction for instruction -- the same opcodes and modes at
// the same offsets, only `LDX #01` / `CPX #04` for `LDX #00` / `CPX #05` -- so every arm costs the same (7285 = 731D,
// 727A = 7312, 72AA = 7342); loc 087's objects take its share arm (the cartridge's pass to the cycle).
static long palScan(Walk* c, int x, int lo, int end) {
  long tail = 2772;                                        // P721C_TAIL / P72B4_TAIL (two 24-byte MVNs from $E4)
  long k = 12;                                             // _INI
  int s;
  for (s = lo; s < end; s++) {                             // pass 1: share an in-use slot with the same $1400 key
    int own = b8(c, 0x0B80 + s);
    k += 26;                                               // _TOP
    if (!(own & 0x80)) {
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
    k += 42;                                               // P721C_USED2
    if (s + 1 >= end) { unpriced(c, "palette table full"); return k; }
    k += 36;                                               // P721C_BRL2
  }
  return k;
}
static long pal7170(Walk* c, int x) {
  long k = 20;                                             // P7170_HEAD
  if (b8(c, 0x0154) & 0x80) return k + BR_T + 40;             // 7170 `LDA $54` with D = $0100 (the engine's DP)
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
  if (t == 5 || t == 6) return k + palScan(c, x, 1, 4);
  if (t < 12) return k + palScan(c, x, 0, 5);
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
    // any other bank: C0/E6B1's BNE taken falls into C0/E6B6, which copies from $D5 whatever $CF holds -- the D4 arm's
    // body (C0/E739) byte for byte with $D5 for $D4, entered by a taken BNE (18) where D4's takes a not-taken one and the
    // BRL (12 + 24): 18 less
    default: return 236 + 1598;
  }
}
// ---- the sprite arena's grants: C0/E952 one slot of $0BC0-$0BC3 (bit 7 = free), C0/E97A two adjacent ones; -1 = none
static int arenaGrant(Walk* c, int x, int cls, long* k) {
  if (cls == 0) {
    for (int i = 0; i < 4; i++)
      if (b8(c, 0x0BC0 + i) & 0x80) { *k += 340 + 86L * i; sb(c, 0x0BC0 + i, x); sw(c, 0x0D80 + x, 0x3800 + (i << 9)); return i; }
    *k += 402;
    return -1;
  }
  long steps = 0;                                          // C0/E97C-E9A6: a position whose first slot is taken costs 86,
  for (int i = 0; i < 3; i++) {                            //   whose second is 124
    if (!(b8(c, 0x0BC0 + i) & 0x80)) { steps += 86; continue; }
    if (!(b8(c, 0x0BC1 + i) & 0x80)) { steps += 124; continue; }
    *k += 410 + steps;
    sb(c, 0x0BC0 + i, x); sb(c, 0x0BC1 + i, x); sw(c, 0x0D80 + x, 0x3800 + (i << 9));
    return i;
  }
  *k += 58 + steps;
  return -1;
}
// ---- the pieces C0/E687 / C0/E534 copy (class 0's C0/CC5D loop, class 1's C0/D2E9): n tiles from the frame's record
static long pieces(Walk* c, int cf, int fbank, int d3, int n) {
  long k = 0;
  for (int p = 0; p < n; p++) {
    if (p) k += 86;                                        // PIECE_NEXT
    int word = mem8(c, fbank, d3 + 2 * p) | (mem8(c, fbank, d3 + 2 * p + 1) << 8);
    k += longRead(fbank, 2) + 18;                          // LDA [$D3],Y + PIECE_READ_OP
    int last = (p == n - 1);
    if (word & 0x4000) k += 132 + e534(cf) + (last ? BR_NT : BR_T);             // PIECE_534
    else k += 126 + e687(c, cf) + (last ? BR_NT + 18 : BR_T);                    // PIECE_687 (+ PIECE_687_LAST)
  }
  return k;
}
static long cad9(Walk* c, int x) {
  int a = b8(c, 0x1A81 + x);
  if ((b8(c, 0x1100 + x) & 0x80) || a == 0 || (a & 0x80) || b8(c, 0x0F00 + x) == 0) {
    unpriced(c, "CAD9 gate"); return 232;
  }
  const int cls = b8(c, 0x1201 + x) & 3, s3 = b8(c, 0x1780 + x) == 3;
  // class 3: C0/CAFB `CMP #02 / BEQ` not taken -> `CLC / RTS`, nothing drawn (loc 01C's object $1A: 308 to the RTS)
  if (cls == 3) return 308;
  if (cls == 2) { unpriced(c, "CAD9: class 2 (C0/D4F7)"); return 232; }
  long k;
  int frame = 0;
  if (cls == 0) {
    k = 232;                                               // CAD9_GATE (class 0)
    if (s3) { k += 302 - 18; frame = b8(c, 0x1301 + x); }  // C0/CBF9 `LDA $1301,X / BRA ->CC04`: no CB3A, no BCS
    else { k += 304; k += cb3a(c, x, &frame); }            // CBDC_HEAD
  } else {
    // class 1, C0/CEF5: its first half C0/D28A (the second, C0/D30D, is a later visit's: $1B00 & 7F, which C0/B127 zeroed)
    if (s3) { unpriced(c, "CAD9: class 1 with $1780 = 3 (C0/CF21)"); return 268; }
    k = 268 + 344 + 40;                                    // CAD9_GATE1, CEF5_HEAD, D28A JSR C0/CB3A
    k += cb3a(c, x, &frame);
  }
  if (c->why) return k;
  if (frame == b8(c, 0x0F01 + x)) return k + 108;          // C0/CC07 / D293 BNE not taken: drawn already, CLC
  k += 122;                                                // CC01_CC10 / D28D_GRANT: STA $EE, JSR the grant
  if (arenaGrant(c, x, cls, &k) < 0) return k + 70;        // C0/CC10 / D29C BCC: no room, CLC
  sb(c, 0x0F01 + x, frame);
  int cf = b8(c, 0x1200 + x), fbank = b8(c, 0x1300 + x);
  if (cls == 1) {
    int d3 = (frame * 0x50 + w16(c, 0x1380 + x)) & 0xFFFF;
    k += 648 + pieces(c, cf, fbank, d3, 16) + 136;         // D29C_OK, the first 16 tiles, D304_END (INC $1B00, SEC)
    sb(c, 0x1B00 + x, b8(c, 0x1B00 + x) + 1);
    // ...and SEC sends C0/B10C `BCS ->B109` back into C0/CAD9 at once: C0/CF19 now finds $1B00 & 7F set and takes the
    // second half C0/D30D -- the other 16 tiles (from +$20), the upload queue's entry, the 8 template entries (+$40,
    // each sign-tested by C0/D3C2 + $1A n: 12 more when negative) -- and leaves CLC.
    k += 18 + 40 + 268 + 338;                              // BCS taken, JSR CAD9, CAD9_GATE1, CEF5 -> BRL D30D
    k += 3656 + 16 * longRead(fbank, 1) + pieces(c, cf, fbank, d3 + 0x20, 16);
    int t = w16(c, 0x1700 + x);
    for (int i = 0; i < 8; i++) {
      int sg = mem8(c, fbank, d3 + 0x40 + 2 * i), v = mem8(c, fbank, d3 + 0x41 + 2 * i);
      if (sg & 0x80) k += 12;
      sb(c, 0x14802 + t + 8 * i, sg); sb(c, 0x14803 + t + 8 * i, (sg & 0x80) ? 0xFF : 0); sb(c, 0x14804 + t + 8 * i, v);
    }
    sb(c, 0x1B00 + x, b8(c, 0x1B00 + x) + 1);
    int q = b8(c, 0x0079);                                 // C0/D37E-D3AC: the upload queue's entry, two $200-byte rows
    sw(c, 0x0980 + q, 0x0200); sw(c, 0x0990 + q, 0x0200);
    sb(c, 0x09A0 + q, b8(c, 0x09A0 + q) + 1); sb(c, 0x09A2 + q, 0); sb(c, 0x0079, q + 2);
    return k;
  }
  int d3 = (frame * 0x28 + w16(c, 0x1380 + x)) & 0xFFFF;
  k += 648;                                                // CC10_CC5D
  k += pieces(c, cf, fbank, d3, 16);
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
  int q = b8(c, 0x0079);                                   // C0/CCAC-CCB4: the upload queue's entry, two $100-byte rows
  sw(c, 0x0980 + q, 0x0100); sw(c, 0x0990 + q, 0x0100);
  sb(c, 0x09A0 + q, b8(c, 0x09A0 + q) + 1); sb(c, 0x09A2 + q, 0); sb(c, 0x0079, q + 2);
  return k;
}

// ---- C0/B1B2 (two $0100-byte DMAs per queued object), C0/EA42 (the arena release)
static long b1b2(Walk* c) {
  long k = 228;                                            // B1B2_HEAD
  for (int q = 0; q < 16; q += 2) {
    k += 26;                                               // B1B2_TEST
    if (b8(c, 0x09A0 + q) == 0) { sb(c, 0x09A0, 0); return k + 78; }         // B1B2_END
    k += 4654 + 8L * (w16(c, 0x0980 + q) + w16(c, 0x0990 + q) - 0x200);   // B1B2_ENTRY (two $100-byte DMAs, the traces'
                                                           //   mean sync) + 8 a byte past them (class 1's are $200 each)
  }
  unpriced(c, "B1B2 queue overflow");
  return k;
}
// C0/EA42: the first of $0BC0-$0BC3 that is this object's (100 a slot passed), then that run of its slots freed (144 each
// after the first; a run that reaches the last slot leaves by C0/EA59, 52 less); 446 when it owns none
static long ea42(Walk* c, int x) {
  int i = 0;
  while (i < 4 && b8(c, 0x0BC0 + i) != x) i++;
  if (i == 4) return 446;
  int r = 0;
  while (i + r < 4 && b8(c, 0x0BC0 + i + r) == x) { sb(c, 0x0BC0 + i + r, 0x80); r++; }
  return 254 + 100L * i + 144L * (r - 1) - (i + r == 4 ? 52 : 0);
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
          long ka = ab45(c, x), kb = a9cd(c, x), kd = cad9(c, x), ke = b1b2(c), kf = ea42(c, x);
          if (getenv("CT_PASSCOST_DEBUG"))
            printf("passcost: obj %d class %d: ab45 %ld a9cd %ld cad9 %ld b1b2 %ld ea42 %ld\n", x / 2, b8(c, 0x1201 + x) & 3,
                   ka, kb, kd, ke, kf);
          k += 342 + ka + kb + kd + ke + kf;              // B0E6_DRAW
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
// ---- C0/BCDC, class 1's B8CA: the 8 template rows' screen Y ($7F:4804 + t + 8 n, the sum with $C5 = screen Y & FF) --
// arm A (screen Y >= $100, C0/BF1C): no carry -> E0, else >= E0 kept (18 more when it is clamped after a carry); arm B
// ($80-$FF, C0/BD80): >= E0 clamped (6 more); arm C (< $80, C0/BE46): positive kept, else >= E0 kept (24) or clamped (30)
static long bcdc(Walk* c, int x) {
  int t = w16(c, 0x1700 + x), sy = w16(c, 0x0A80 + x) & 0x1FF, c5 = sy & 0xFF, c6 = sy >> 8;
  long k = c6 ? 6080 : (c5 & 0x80) ? 6100 : 6022;
  for (int i = 0; i < 8; i++) {
    int d = b8(c, 0x14804 + t + 8 * i), sum = d + c5, v = sum & 0xFF;
    if (c6) k += (sum >= 0x100 && v < 0xE0) ? 18 : 0;
    else if (c5 & 0x80) k += (v >= 0xE0) ? 6 : 0;
    else k += (v < 0x80) ? 0 : (v >= 0xE0) ? 24 : 30;
  }
  return k;
}
static long b309(Walk* c, int x) {
  // C0/B701: an object whose tiles are not all in VRAM yet is not drawn (SEC, C0/B30E RTS) -- class 0 with $1B00 = 0
  // (C0/B71B), class 1 with fewer than its two halves (C0/B742 -> B73C)
  const int cls = b8(c, 0x1201 + x) & 3, s = b8(c, 0x1B00 + x);
  if (cls == 0 && s == 0) return 278;
  if (cls == 1 && !(s & 0x80) && s < 2) return s ? 362 : 314;
  if (cls == 1 && !(s & 0x80)) {
    // both halves in: C0/B744 JSR C0/BCDC, C0/E9FF (the pair C0/EA42 has already freed: not found), $1B00 = 80, then
    // C0/B3DF's eight OAM entries in the $0F80 group's list (+$20 to its cursor): 4626 / 4698 / 4686 by group
    for (int i = 0; i < 3; i++) if (b8(c, 0x0BC0 + i) == x) { unpriced(c, "E9FF finds a pair"); return 0; }
    int g = b8(c, 0x0F80 + x) & 0x0C, cur = g == 0 ? 0x01DB : (g & 4) ? 0x01DD : 0x01DF;
    long k = (g == 0 ? 4626 : (g & 4) ? 4698 : 4686) + bcdc(c, x);
    sb(c, 0x1B00 + x, 0x80);
    sw(c, cur, w16(c, cur) + 0x20);
    return k;
  }
  if (cls) { unpriced(c, "B701: class 1/2 drawn"); return 0; }
  if (s & 0x80) { unpriced(c, "B701: $1B00"); return 0; }
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
  // THE WIDE LAYERS, $1D35 bits 1 (BG2) and 2 (BG3), in place of the standard arms PASS_REST holds (traced: loc 16E with
  // both, loc 087 with bit 2; the standard on 002 / 070 / 082 / 1B7). C0/0AB4 `BIT #02 / BEQ`: bit 1 uploads BG2 through
  // C0/7F77 -- `LDX #1000 / STX $4E / BRA 7F67`, C0/7F62's 0x800 bytes doubled: 16384 + the BRA -- and branches over the
  // other (BEQ not taken + BRA 0AC3: +12). C0/87F1 then skips, on bit 1, its BG2 row C0/8365 (`BIT #02 / BNE` taken:
  // -9936, 087 against 16E) and, on bit 2, its BG3 row C0/83A5 (-8996, the standard against 087).
  if (b8(c, 0x1D35) & 0x02) k += 16402 + 12 - 9936;
  else if (b8(c, 0x0BC9) & 0x80) {
    // C0/8806 BPL not taken + JSR C0/8385 + BRA: C0/8365 on the half camera ($1D14 / $1D16): +12 when its C0/7BA9 row
    // is odd like the standard's ($0E - 1, the camera's even row less one); the even row's arm no trace ran
    if (((b8(c, 0x1D16) - 1) & 1) == 0) { unpriced(c, "pass: C0/8385 on an even row (C0/7BA9's even arm)"); return -1; }
    k += 12;
  }
  if (b8(c, 0x1D35) & 0x04) k -= 8996;
  // FD:C1EE's `LDA $013C / BIT #01 / BNE`: the $2129 arm is `LDA #29` alone where $2128's is `LDA #28 / BRA` (-12)
  if (b8(c, 0x013C) & 1) k -= 12;
  if (getenv("CT_PASSCOST_DEBUG")) printf("passcost: doors %ld chests %ld camera %ld 78EC %ld n %d\n", d0, d1, d2, d3, n);
  if ((b8(c, 0x0BCA) & 1) && !b8(c, 0x01BB)) {
    if (b8(c, 0x1D35) & 0x04) {
      // C0/7CB5 `BIT #04 / BEQ` not taken -> C0/7CE7: 32 rows of C0/7D66 at column 0, then by $0BD3 / $0BD5 (like
      // C0/794E): 7D0D (both $10) the same 32 again, 7D28 ($0BD3 != $10) 32 at column $20, 7D45 ($0BD5 != $10) rows
      // 32-63 and `$1D86 := 2`. Every row starts on an even column: 9114 an even row, 9132 an odd one (C0/7D66's
      // `BIT #01` arms), 32 of each = 583,872. The loops around them: 11,576 on 7D0D (16E's trace, to the cycle), and
      // from the same instructions 11,916 on 7D28 (its `LDA #20 / STA $58` a row, +12, and the BNE taken) and 11,998 on
      // 7D45 (the same, the second BNE taken, and `LDA #02 / STA $86`). Then C0/0AE1 `BIT #04 / BEQ` not taken: C0/7F77
      // (+16402 over C0/7F62) and the BRA (-6 against the standard's BEQ taken).
      long arm = 11576;
      if (b8(c, 0x0BD3) != 0x10) arm = 11916;
      else if (b8(c, 0x0BD5) != 0x10) { arm = 11998; sb(c, 0x1D86, 2); }
      k += 583872 + arm + 17000 + 16402 + 248 - 6;
    } else k += PASS_BG3;
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

// ---- C0/0960 -> C0/56D4's RTS: the location's packets -- the tileset and its VRAM uploads, the BG3 tileset, C0/7084,
// C0/7F7E, the map (decompressed and built), the properties, the BG3 assembly, the script -- and the JSRs between them.
// The location load (C0/00F4) and the menu's reload (C0/01A5) run the same chain; what each runs around it is theirs.
static long loadPackets(Walk* c, int loc) {
  const long REC = 0x360000L + (long)loc * 14;
  long k = 0;
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
  return k;
}

static long prePass(Walk* c, int loc, int caller, long* vmOut) {
  const long REC = 0x360000L + (long)loc * 14;
  long k = 610;                                            // C0/0B5D -> C0/01A5 (0B4E's tail, 0B64, 0B75, D = $0100)
  // C0/01A5 -> C0/0960: C0/0905 (the $7F:2000 -> $7E:0920 MVN, 246,000), FD:C2C1, FD:C1EE, the three fill marks;
  // FD:C1EE's table by `$0153 & 0F` after FD:C2C1's flip of bit 0 (the port's $0153 is the menu's, as restored)
  k += 251060 + ((((b8(c, 0x0153) ^ 1) & 0x0F) == 0) ? BR_T : BR_NT + 18);
  sb(c, 0x0153, b8(c, 0x0153) ^ 1);
  k += loadPackets(c, loc);
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

// =================================================================================================================
// The location load's object pass: one op of the event VM, as the init runner (C0/5910) or obj0's slot-1 runner
// (C0/5977) dispatches it -- `JSR ($5D6E,X)` to the handler's RTS, the dispatch's JSR included; the runner's own
// fetch / ASL / TAX / BRA around it is CT_VM_DISPATCH. Measured on the four kept load traces (loc 002 / 070 / 082
// / 1B7) and walked from the ROM (tools/load_cost_consts.py prints every number below): a handler with no branch is
// its straight path; a branchy one is its arms, decided here on the WRAM the handler is about to read. The sound
// driver's cycles (a JSL C7/0004) are not in these numbers -- they are the loader clock's (the posts' own pricing);
// a sprite sheet's decompression is lz.c's (ct_lz_rom_cycles), only the vector's `JMP` (18) is here.
// =================================================================================================================

// The handlers with ONE path (the dispatch's JSR to the RTS), 0 = branchy (below) or not walked.
static const short VM_OP_STRAIGHT[256] = {
  [0x08] = 200, [0x09] = 218, [0x0A] = 332, [0x0C] = 320, [0x0D] = 292, [0x0E] = 274, [0x0F] = 250, [0x10] = 306,
  [0x11] = 324, [0x17] = 250, [0x19] = 388, [0x1B] = 250, [0x1C] = 360, [0x1D] = 250, [0x1E] = 320, [0x1F] = 320,
  [0x20] = 326, [0x21] = 702, [0x22] = 758, [0x23] = 464, [0x24] = 520, [0x25] = 320, [0x26] = 320, [0x2A] = 174,
  [0x2B] = 192, [0x2C] = 526, [0x2F] = 268, [0x32] = 192, [0x47] = 222, [0x48] = 654, [0x49] = 670, [0x4A] = 488,
  [0x4F] = 390, [0x50] = 454, [0x51] = 488, [0x52] = 504, [0x53] = 478, [0x54] = 494, [0x55] = 326, [0x56] = 380,
  [0x58] = 470, [0x59] = 486, [0x5A] = 204, [0x5B] = 434, [0x5D] = 532, [0x5E] = 556, [0x5F] = 434, [0x60] = 506,
  [0x61] = 532, [0x63] = 496, [0x64] = 496, [0x65] = 536, [0x66] = 536, [0x67] = 422, [0x69] = 422, [0x6B] = 422,
  [0x71] = 338, [0x72] = 354, [0x73] = 338, [0x75] = 306, [0x76] = 320, [0x77] = 306, [0x7C] = 288, [0x7D] = 288,
  [0x7E] = 218, [0x7F] = 460, [0x81] = 2632, [0x84] = 244, [0x87] = 372, [0x89] = 244, [0x8A] = 354, [0x90] = 200,
  [0x91] = 218, [0xA6] = 276, [0xA7] = 404, [0xAA] = 396, [0xAE] = 370, [0xB1] = 128, [0xB2] = 116, [0xB3] = 370,
  [0xB4] = 370, [0xB8] = 320, [0xDC] = 524, [0xDD] = 524, [0xDE] = 574, [0xE3] = 204, [0xE5] = 582, [0xE8] = 384,
  [0xEA] = 378, [0xEB] = 790, [0xEC] = 448,
};
// ...and, for a branchy handler not walked below, its path with every branch NOT taken: what the load is charged, with
// the op reported (CtVmOpCost.why) -- a number off the ROM, not the arm the WRAM picks.
static const short VM_OP_FALLTHROUGH[256] = {
  [0x02] = 1486, [0x03] = 1512, [0x04] = 798, [0x05] = 1560, [0x06] = 1586, [0x07] = 1686, [0x27] = 282, [0x28] = 602,
  [0x2D] = 234, [0x30] = 208, [0x31] = 208, [0x34] = 208, [0x35] = 208, [0x36] = 208, [0x37] = 190,
  [0x38] = 208, [0x39] = 208, [0x3B] = 236, [0x3C] = 374, [0x3F] = 374, [0x40] = 374, [0x41] = 374, [0x42] = 374,
  [0x43] = 374, [0x44] = 374, [0x4B] = 552, [0x4C] = 654, [0x4D] = 670, [0x4E] = 698, [0x6F] = 460, [0x7A] = 232,
  [0x7B] = 232, [0x8F] = 188, [0x92] = 232, [0x94] = 188, [0x95] = 188, [0x96] = 188, [0x97] = 188, [0x98] = 232,
  [0x99] = 232, [0x9A] = 232, [0x9C] = 232, [0x9D] = 232, [0x9E] = 188, [0x9F] = 188, [0xA0] = 188, [0xA1] = 188,
  [0xA8] = 1880, [0xA9] = 1942, [0xAB] = 358, [0xAD] = 300, [0xAF] = 240, [0xB0] = 148, [0xB5] = 336, [0xB6] = 324,
  [0xB7] = 358, [0xB9] = 274, [0xBA] = 274, [0xBC] = 256, [0xBD] = 274, [0xC8] = 360, [0xD6] = 500, [0xD8] = 440,
  [0xD9] = 212, [0xDA] = 218, [0xE0] = 628, [0xE2] = 1008, [0xE7] = 240, [0xED] = 166, [0xEE] = 178,
  [0xF0] = 402, [0xF1] = 624, [0xF2] = 172, [0xF3] = 172, [0xF4] = 256, [0xFE] = 1244,
};

// ---- the $6467 comparators (C0/6487-653A), `JSR ($6467,X)` to their RTS: lt / eq / gt (the AND / OR pairs, 6-7 and
// 14-15: zero / nonzero / nonzero). 8-15 are the 16-bit twins. CLC (C0/64B3 / C0/651F) = the condition holds.
static const short VM_CMP_C[16][3] = {
  {104, 110, 104}, {110, 104, 110}, {116, 110, 122}, {110, 104, 104}, {116, 110, 122}, {122, 110, 116},
  {104, 110, 110}, {104, 110, 110}, {156, 162, 156}, {162, 156, 162}, {168, 162, 174}, {174, 162, 168},
  {168, 162, 174}, {174, 162, 168}, {156, 162, 162}, {156, 162, 162},
};
static const uint8_t VM_CMP_T[16][3] = {
  {0, 1, 0}, {1, 0, 1}, {0, 0, 1}, {1, 0, 0}, {0, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 1},
  {0, 1, 0}, {1, 0, 1}, {0, 0, 1}, {1, 0, 0}, {0, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 1},
};
// the five comparing ops' own cycles, the comparator's call excluded: { holds (falls through), fails (C0/6349's jump) }
//   12 C0/62DE local vs byte    13 C0/6313 local word vs word ($6477: the 16-bit half)    14 C0/6361 local vs local
//   15 C0/63A4 local word vs local word ($6477)    16 C0/63E6 flag vs byte (+42 for the $7F:0100 page, C0/63FC-6416)
static const short VM_CMP_OP[5][2] = { {614, 786}, {658, 830}, {736, 908}, {746, 930}, {694, 878} };

static int vmRel(int cmp, unsigned a, unsigned b) {   // the comparator's arm: 0 lt / 1 eq / 2 gt; AND/OR: 0 zero / 1 not
  if ((cmp & 7) == 6) return (a & b) ? 1 : 0;
  if ((cmp & 7) == 7) return (a | b) ? 1 : 0;
  return a < b ? 0 : a == b ? 1 : 2;
}
static long vmCmpOp(int op, int cmp, unsigned a, unsigned b, const char** why) {
  if (cmp > 15) { if (why && !*why) *why = "a comparator past the table (C0/6467)"; cmp &= 15; }
  int r = vmRel(cmp, a, b);
  return VM_CMP_C[cmp][r] + VM_CMP_OP[op - 0x12][VM_CMP_T[cmp][r] ? 0 : 1];
}

long ct_vm_op_cycles(const uint8_t* rom, long romLen, const uint8_t* w, const uint8_t* B, int blen, int addr, int obj,
                     const char** why) {
  if (addr < 0 || addr >= blen) return 0;
  const int op = B[addr], X = obj * 2;
#define OPB(k) ((addr + (k)) < blen ? B[addr + (k)] : 0)
#define W8(a) (w[(a) & 0x1FFFF])
  switch (op) {
    // ---- the NPC loader C0/4590 (82): the sheet C0/5C90 shares or decompresses, then `LDA $73 / CMP #01 / BMI / BEQ`
    //   C0/5C90 pass 1 scans $0920[0..15] for the sheet ($E4:F023 + id*5, byte 0): 92 a step (5C95-5C9E) to the match;
    //   a miss scans all 16 and pass 2 the same 92 a step to the first free ($FF) slot, then C0/466B decompresses.
    //   $73 picks one of three stores (C0/4639 / 4686): -6 when it is 1, +6 when it is past 1 (signed: `$73 - 1` < 0
    //   is the 0 arm)
    case 0x82: case 0x83: {
      int id = OPB(1), k, j = -1;
      long base;
      int g = op == 0x82 ? CtRomByte(rom, romLen, 0x24F023L + 5L * id) : CtRomByte(rom, romLen, 0x24F600L + 10L * id);
      if (op == 0x83 && id >= 0xF8) { base = 3812; goto tail83; }   // C0/477E BRL ->4845: the sheet straight from the table
      for (k = 0; k < 16 && W8(0x0920 + k) != g; k++) ;
      if (k == 16) for (j = 0; j < 16 && W8(0x0920 + j) != 0xFF; j++) ;
      if (j == 16) { if (why && !*why) *why = "C0/5CB0: no free sheet slot (the diagnostic halt)"; j = 15; }
      if (op == 0x82) {
        int p = W8(0x0173), arm = (((p - 1) & 0x80) != 0) ? 0 : (p == 1 ? -6 : 6);
        return (k < 16 ? 3064 + 92L * k : 5166 + 92L * j) + arm;
      }
      base = k < 16 ? 3778 + 92L * k : 5916 + 92L * j;   // C0/4789 shares, C0/47FE decompresses (C0/4842 BRL 4799)
    tail83:
      if (OPB(2) & 0x80) base += 58;                      // C0/47EC `LDA $E1 / BPL`: the 06 arm's `LDA #06 / STA $1100,X`
      if ((CtRomByte(rom, romLen, 0x24F600L + 10L * id + 4) & 3) == 3) {
        // C0/47E9 JSR C0/E12A (+74 with the BCC not taken): a size-3 sheet goes to VRAM at once -- frame 0's 96 tiles
        // (C0/E173-E194, the pieces' loop of class 1), a $0C00-byte DMA (C0/E1C0, 8 a byte), the 24 template rows from
        // +$C0 (12 more each negative one) and its stores: 6690 besides. The assembly is the record's (byte 1 -> $E4:FFE2's
        // table, C0/4708-4715), the bank C0/4579's 7F.
        Walk wk = { (uint8_t*)w, rom, romLen, NULL, 0 };
        long at = 0x240000L + (CtRomByte(rom, romLen, 0x24FFE2L) | (CtRomByte(rom, romLen, 0x24FFE3L) << 8))
                + 3L * CtRomByte(rom, romLen, 0x24F600L + 10L * id + 1);
        int d3 = CtRomByte(rom, romLen, at) | (CtRomByte(rom, romLen, at + 1) << 8), fb = CtRomByte(rom, romLen, at + 2);
        long k = 74 + 6690 + 8L * 0x0C00 + pieces(&wk, 0x7F, fb, d3, 96) + 48 * longRead(fb, 1);
        for (int i = 0; i < 24; i++) if (mem8(&wk, fb, d3 + 0xC0 + 2 * i) & 0x80) k += 12;
        if (wk.why && why && !*why) *why = wk.why;
        base += k;
      }
      return base;
    }
    // ---- the placements: with $0F80,X bit 7 (AUTO) the tile's property pair is looked up (C0/4B98 / 4BFC / 4C42)
    case 0x8B: return (W8(0x0F80 + X) & 0x80) ? 852 : 538;
    case 0x8C: return (W8(0x0F80 + X) & 0x80) ? 1028 : 714;
    case 0x8D: return (W8(0x0F80 + X) & 0x80) ? 882 : 502;
    case 0x8E: return (OPB(1) & 0x80) ? 730 : 526;          // C0/4C7F `BIT #80 / BEQ`: the operand's own bit 7
    // ---- the PC loaders C0/41E4-421E (57 5C 62 6A 68 6C 6D: the slot is the op; 80: C0/4226's chain of compares), then
    // C0/426A: which party slot ($7E2980-82) the PC is in -- the lead (C0/429C), slot 1 (C0/4310), slot 2 (C0/43C3) or
    // none (C0/428C, the object is hidden). The lead and a follower whose lead is absent ($97 bit 7, C0/435E / 4411)
    // place themselves from the arrival ($02-$04: bit 3 the Y fraction, bit 2 the X one); a follower copies the lead.
    // $78 set: C0/4332 `LDA #02 / STA $78 / STZ $1601,X` (+58).
    case 0x57: case 0x5C: case 0x62: case 0x68: case 0x6A: case 0x6C: case 0x6D: case 0x80: {
      int id; long k;
      static const short e80[8] = { 210, 240, 270, 300, 330, 360, 372, 338 };
      if (op == 0x80) { id = OPB(1); k = e80[id < 7 ? id : 7]; }
      else { id = op == 0x57 ? 0 : op == 0x5C ? 1 : op == 0x62 ? 2 : op == 0x6A ? 3 : op == 0x68 ? 4 : op == 0x6C ? 5 : 6;
             k = op == 0x6D ? 142 : 160; }
      int slot = id == W8(0x2980) ? 0 : id == W8(0x2981) ? 1 : id == W8(0x2982) ? 2 : 3;
      int a4 = W8(0x0104), own;
      if (slot == 3) return k + 370;
      if (slot == 0) own = 1, k += 3470;
      else {
        own = (W8(0x0197) & 0x80) != 0;
        k += (slot == 1 ? 3662 : 3712) + (own ? 0 : 102) + (W8(0x0178) ? 58 : 0);
      }
      if (own) k += ((a4 & 8) ? -12 : 0) + ((a4 & 4) ? 6 : 0);
      return k;
    }
    // ---- the comparisons (C0/62DE-6440): the op's own cycles + the comparator's arm
    case 0x12: return vmCmpOp(0x12, OPB(3), W8(0x10200 + 2 * OPB(1)), OPB(2), why);
    case 0x13: return vmCmpOp(0x13, 8 + (OPB(4) & 7),
                              (unsigned)(W8(0x10200 + 2 * OPB(1)) | W8(0x10201 + 2 * OPB(1)) << 8),
                              (unsigned)(OPB(2) | OPB(3) << 8), why);
    case 0x14: return vmCmpOp(0x14, OPB(3), W8(0x10200 + 2 * OPB(1)), W8(0x10200 + 2 * OPB(2)), why);
    case 0x15: return vmCmpOp(0x15, 8 + (OPB(3) & 7),
                              (unsigned)(W8(0x10200 + 2 * OPB(1)) | W8(0x10201 + 2 * OPB(1)) << 8),
                              (unsigned)(W8(0x10200 + 2 * OPB(2)) | W8(0x10201 + 2 * OPB(2)) << 8), why);
    case 0x16: {
      int m = OPB(3), hi = (m & 0x80) != 0;
      return vmCmpOp(0x16, m & 0x7F, W8(0x10000 + OPB(1) + (hi ? 0x100 : 0)), OPB(2), why) + (hi ? 42 : 0);
    }
    case 0x18: return (W8(0x10000) >= OPB(1)) ? 400 : 228;      // C0/6448 `CMP / BCS`: the jump
    case 0x0B: return (OPB(1) == X) ? 342 : 348;                // C0/627A `CPX $6D / BNE`: itself ends the visit
    case 0x1A: return (W8(0x10A80 + X) == OPB(1)) ? 256 : 428;  // C0/6570 `CMP / BNE`: the case's jump
    case 0xAC: return W8(0x1780 + X) ? 414 : 452;               // C0/2F85 `BNE`: $1680 = FF only from a zero $1780
    // ---- C0/6846 (4E): the script's own bytes copied out, `4E dst(2) bank len(2) data`: `MVN $7E,$7F` (C0/6897) when the
    // bank is 7E, else `MVN $7F,$7F` (C0/6872), len - 2 bytes at 46 each (the WRAM read, the write, two idles; the
    // traces' MVN from ROM costs 44, C2/8369)
    case 0x4E: { long n = ((OPB(4) | OPB(5) << 8) - 2) & 0xFFFF; return (OPB(3) == 0x7E ? 660 : 654) + 46 * (n ? n : 0x10000); }
    case 0x6F: { int n = OPB(1) ? OPB(1) : 256; return 460 + 64L * (n - 1); }   // C0/6C5B-6C5E: LSR / DEC / BNE, n times
    case 0xF0: return (OPB(1) & 0x0F) ? 402 : 322;              // C0/3CDB `BEQ`: the low nibble zero sets $19 instead
    case 0x33: return 2938;                                     // C0/3740 / 374D: two 24-byte MVNs from bank E4
    // ---- C0/3D97 (E4) -> C0/AF4E: the map copy `E4 x0 y0 x1 y1 dx dy flags`, dx = x1 - x0 and dy = y1 - y0 (8-bit, $DB / $DD).
    // Flags 0 return at once (C0/AF50); each of bits 0-5 copies one layer (C0/B096: a row's dx + 1 bytes by `MVN $7E,$7E`,
    // 46 a byte, then the next row), bits 6 and 7 one property bit of $7E:30C0 cell by cell (C0/AFAB / C0/B023)
    case 0xE4: {
      int fl = OPB(7), dx = (OPB(3) - OPB(1)) & 0xFF, dy = (OPB(4) - OPB(2)) & 0xFF;
      if (!fl) return 672;
      long k = 672 + 266;
      for (int i = 0; i < 6; i++) if (fl >> i & 1) k += 778 + 46L * dx * (dy + 1) + 440L * dy;
      for (int i = 6; i < 8; i++) if (fl >> i & 1) k += 822 + 266L * dx * (dy + 1) + 558L * dy;
      return k;
    }
    // ---- C0/38E8 (D2): `JSL C2/8004` with A = 0 -- leaf 0 (C2/8C75 -> 8CD1) scans $2980-82 for the operand, then the
    // same epilogue C2/834D the load's own C2/8004 runs (ct_c28004_cycles, whose leaf 11 is a bare RTS: -40); C0/38FC
    // `BNE` jumps when the PC is absent. Found in slot 0 / 1 / 2, absent: the handler's head, the scan and its tail.
    case 0xD2: {
      int id = OPB(1), i = 0;
      while (i < 3 && W8(0x2980 + i) != id) i++;
      static const short d2[4] = { 522, 630, 738, 960 };
      return ct_c28004_cycles(rom, romLen, w) + d2[i];
    }
    default: break;
  }
#undef OPB
#undef W8
  if (VM_OP_STRAIGHT[op]) return VM_OP_STRAIGHT[op];
  if (why && !*why) {
    static char msg[48];
    snprintf(msg, sizeof msg, "op %02X: its arms are not walked", op);
    *why = msg;
  }
  return VM_OP_FALLTHROUGH[op];
}

// =================================================================================================================
// The LOCATION LOAD (C0/0066 -> C0/0085 JSR C0/0B86 ... C0/0094 JSR C0/2848): what the CPU spends from the write that
// turns the NMI off (C0/0B5A) to C0/2848's first vblank wait (C0/0B38), the object pass's ops excepted (the VM prices
// those as they run: ct_vm_op_cycles). The legs, in the ROM's order:
//   C0/0B86 the field's variables; C0/00F4: 092B (the record's address), 1B53 (the song), the packets C0/0960-56D4
//   (loadPackets), FD:DE98 (the animated tiles) and FD:E292 (the palette cycles); C0/B192; C0/56A6: 6F79, 7155, E935,
//   5929, B204, A7E9, C0/5709 (the init runner: its fills, the objects' heads, their ops; C0/9DC3 at its end), C0/595C
//   (obj0's slot 1: its ops), C2/8004 (the party's rows and double techs); C0/28AA; C0/2848 -> C0/286C's pass.
// What none of these walk -- the instructions with no data in them, from C0/0B5D to the pass -- is LOAD_FIXED, measured
// as the traces' remainder (tools/load_cost_cmp.py).
// =================================================================================================================

// ---- FD:DE98 (JSL FD:FFFA): the animated BG tiles. The record's byte 1 & 3F picks a list (FD:F290) of 12 entries at
// FD:F310: 80 = none (1 byte), else a kind and two bytes, then the kind's VRAM tables -- 2: 6 bytes (FD:DF02), 4: 12
// (FD:DF69, two passes of four words), any other: 24 (FD:DFCE). 4194 for twelve empty ones; what each kind adds.
long ct_fd_de98_cycles(const uint8_t* rom, long romLen, int loc) {
  int r1 = CtRomByte(rom, romLen, 0x360000L + loc * 14L + 1) & 0x3F;
  long x = CtRomByte(rom, romLen, 0x3DF290L + r1 * 2) | (CtRomByte(rom, romLen, 0x3DF291L + r1 * 2) << 8), k = 4194;
  for (int e = 0; e < 12; e++) {
    int b = CtRomByte(rom, romLen, 0x3DF310L + x); x++;
    if (b == 0x80) continue;
    x += 2;
    if (b == 2) { k += 3258; x += 6; } else if (b == 4) { k += 5096; x += 12; } else { k += 3812; x += 24; }
  }
  return k;
}
// ---- FD:E292 (JSL FD:FFF4): the palette cycles. The record's byte 3 picks a list (FD:F9C7) of 6 entries at FD:FA77,
// by the high nibble: 0 = none (1 byte), 10 = a cycle (7 bytes, FD:E30A), 80 = the same with its colours inline
// (7 + 1 + the low nibble, FD:E350), else a short one (4 bytes, FD:E2DA). 2202 for six empty ones.
long ct_fd_e292_cycles(const uint8_t* rom, long romLen, int loc) {
  int r3 = CtRomByte(rom, romLen, 0x360000L + loc * 14L + 3);
  long x = CtRomByte(rom, romLen, 0x3DF9C7L + r3 * 2) | (CtRomByte(rom, romLen, 0x3DF9C8L + r3 * 2) << 8), k = 2202;
  for (int e = 0; e < 6; e++) {
    int b = CtRomByte(rom, romLen, 0x3DFA77L + x), h = b & 0xF0;
    if (h == 0) { x += 1; continue; }
    if (h == 0x10) { k += 632; x += 7; }
    else if (h == 0x80) { k += 1020; x += 8 + (b & 0x0F); }
    else { k += 370; x += 4; }
  }
  return k;
}

// ---- C2/8004 with A = 0B (C0/56CF): leaf 11 is C2/8C5A's RTS, run for its epilogue C2/834D --
//   the three party slots ($2980-82, bit 7 = empty): each present PC's weapon row, C2/F5ED + a 6-byte MVN (C2/8369);
//   FF:F958, the double techs: $A3 = FE less the bit (FF:F9BB) of every PC in $2980-88, then for each of the five
//     pairs (CC:2983) whose members are all there, its three techs (CC:2953) against the learned bits $2837 -- the
//     first unlearned one ends the pair; three learned `INC $A4,X`;
//   C2/82E1, the seven PCs' equipped weapons ($262A + 0x50 i): one in AE-B2 whose $03F6 flag is set marks $27A9.
// The whole call, the JSL to its RTL (the JSL itself is LOAD_FIXED's); measured on the four kept traces.
long ct_c28004_cycles(const uint8_t* rom, long romLen, const uint8_t* w) {
  long k = 514 + 198;                                      // C2/8004 -> 8C4F (the leaf), C2/834D's head
  for (int s = 0; s < 3; s++)
    k += ((w[0x2980 + s] & 0x80) ? 98 : 1174) + (s < 2 ? 18 : 12);
  k += 232;                                                // C2/837B JSL FF:F958, its head
  int a3 = 0xFE;
  for (int y = 8; y >= 0; y--) {
    int id = w[0x2980 + y];
    if (id & 0x80) k += 56; else { k += 114; a3 &= ~CtRomByte(rom, romLen, 0x3FF9BBL + (id & 0xFF)); }
    k += y ? 18 : 12;
  }
  k += 20;
  for (int p = 0; p < 5; p++) {
    if (CtRomByte(rom, romLen, 0x0C2983L + p) & a3) k += 114;          // FF:F980 BNE: a member is missing
    else {
      k += 204;
      int t = 0;
      for (; t < 3; t++) {
        int a2 = (CtRomByte(rom, romLen, 0x0C2953L + p * 3 + t) - 1) & 0xFF;
        if (!(CtRomByte(rom, romLen, 0x3FF9BBL + (a2 & 7)) & w[0x2837 + (a2 >> 3)])) { k += 274; break; }
        k += 314 + (t < 2 ? 18 : 12);
      }
      if (t == 3) k += 60;                                 // FF:F9AC INC $A4,X
    }
    k += 66 + (p < 4 ? 18 : 12);
  }
  k += 102 + 322;                                          // FF:F9B8's RTL; C2/82E1's head (its 3-byte MVN)
  for (int i = 0; i < 7; i++) {
    int v = w[0x262A + 0x50 * i];
    if (v < 0xAE) k += 98; else if (v >= 0xB3) k += 122; else k += w[(0x03F6 + v) & 0xFFFF] ? 216 : 178;
    k += 90 + (i < 6 ? 18 : 12);
  }
  return k + 376;                                          // C2/831E .. C2/800D's RTL
}

// ---- C0/9DC3, the init runner's end (C0/5926 BRL): the arrival tile's shape (C0/9AD3 on its first property byte $58,
// with $66 = 80 / $68 = FF) and whether its plane $6A is 0 (C0/9DFA). From the BRL to the RTS that returns to C0/56C8.
static const short LOAD_9DC3[64] = {
  1928, 1988, 2452, 2422, 2374, 2404, 2410, 2332, 2332, 2362, 2494, 2464, 2416, 2446, 2410, 2380,
  2380, 2362, 2494, 2464, 2416, 2446, 2452, 2422, 2100, 2124, 2148, 2252, 2234, 2220, 2282, 2200,
  2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200,
  2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200, 2200,
};
long ct_load_9dc3_cycles(int b58, int a6a, int d30) {
  int v = (b58 & 0xFC) >> 2;
  long k = LOAD_9DC3[v] - (a6a ? 6 : 0);
  if (v == 0x1E && d30) k += (d30 & 0x80) ? 44 : 38;     // C0/9BE1 `LDA $30 / BEQ / BMI`: the conveyor's push
  return k;
}

#define LOAD_FIXED 9070L    // the legs' constant instructions, C0/0B5D -> C0/2848 (calibrated: tools/load_cost_cmp.py)

long ct_field_load_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, CtLoadCost* out) {
  CtLoadCost r;
  memset(&r, 0, sizeof r);
  if (out) *out = r;
  if (!rom || !wram || loc < 0 || loc > 0x1EF || romLen < 0x400000) return -1;
  Walk c = { (uint8_t*)malloc(0x20000), rom, romLen, NULL, 0 };
  if (!c.w) return -1;
  memcpy(c.w, wram, 0x20000);
  const long REC = 0x360000L + (long)loc * 14;
  long k = LOAD_FIXED;
  if (loc & 0x100) k += 18;                                // C0/092F `AND #01 / BNE`: the record's second page
  // C0/1B53: `LDA $7F01ED / BEQ` -- set, the song C0/1B59 kept; else the record's byte 0 (FF: no song, C0/1B7D BMI)
  if (b8(&c, 0x101ED)) k += 252;
  else k += (CtRomByte(rom, romLen, REC) & 0x80) ? 372 : 516;
  r.packets = loadPackets(&c, loc);                        // C0/0960 -> C0/56D4 (the map cells and the script land in c)
  r.anim = ct_fd_de98_cycles(rom, romLen, loc) + ct_fd_e292_cycles(rom, romLen, loc);
  const int n = b8(&c, 0x12000);
  r.objects = (578 + 80L * n)                              // C0/B192
            + (11966 + 78L * n) + (326 + 66L * n)          // C0/B204, C0/A7E9
            + (8190 + 1124L * n) + 50886                   // C0/5709's fills (its three C0/2DF1 DMAs: $200 + $100 + $15C0)
            + (460L * n + 12);                             // C0/58ED-5924: every object's head and tail, ops apart
  r.party = ct_c28004_cycles(rom, romLen, c.w);            // C0/56CF JSL C2/8004
  r.pass = 40 + pass286C(&c, loc);                         // C0/2848 JSR C0/286C, the pass to C0/0B38
  r.why = c.why;
  free(c.w);
  r.total = k + r.packets + r.anim + r.objects + r.party + r.pass;
  if (out) *out = r;
  return r.why ? -1 : r.total;
}
