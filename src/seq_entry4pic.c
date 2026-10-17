// seq_entry4pic.c -- SEQUENCE ENTRY 4's picture, native (stage 2; its logic, stage 1, is seq_entry4.c): Lavos falls through
// space onto the planet. Mode 7 (the planet's map and characters), the stars and the planet's sprite.
//
// The setup $7E:53BB uploads the mode-7 characters as VRAM's high bytes (packet $2D's nibbles spread by $350E), the OBJ
// characters by index ($54EA: packet $42's tiles chosen by $FE-table $3F's bytes, through WMDATA) and packet $33, the map
// (packet $30's 64 x 64 rows doubled with zeros by $37CA, $380 bytes of it, the rest filled from one byte by $90A7), the
// palettes ($36AA: $FE-table $3C and $36 into $7E:2200 / 2300) and the layers' registers. Hook $5654 writes the mode-7 matrix,
// centre and BG1 scrolls from page A; task $56EB draws the planet (C3/0D5E, the list $7E:56FD); the stars' OAM is the logic's
// ($579B). The overlay's code is read from the WRAM it was decompressed into. disasm/notes/seq_picture.md sec. 6.
#include "seq_entry4pic.h"
#include "seq_racepic.h"
#include <stdio.h>

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

// $7E:36AA -- `$F0` bytes from the $FE-table's pointer X (bank $FE) to Y, in bank $7E or $7F by `$0305` bit 0 (C3/099C / 09A0).
static void copy36AA(uint8_t* w, const CtRaceIo* io, unsigned x, unsigned y) {
  if (!io || !io->rom) return;
  long t = 0x3E0000L + (long)x;
  if (t + 2 > io->romLen) return;
  unsigned src = (unsigned)(io->rom[t] | (io->rom[t + 1] << 8)), n = rw(w, 0xF0) & 0xFFFF, b = (unsigned)(w[0x0305] & 1) << 16;
  for (unsigned i = 0; i < n; i++) {
    long r = 0x3E0000L + (long)((src + i) & 0xFFFF);
    w[b | ((y + i) & 0xFFFF)] = (r < io->romLen) ? io->rom[r] : 0;
  }
}
// $7E:53BB-54B0 (8-bit A) -- the setup before the script.
void CtEntry4PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  sp->vmain = 0x80; sp->vaddr = 0x0000;                               // 53BB / 53C0
  w[0x0305] = 0; ww(w, 0x0303, 0x0C00);
  CtSeqPicUnpack(w, io, 0x2D);                                        // 53CF: packet $2D -> $7E:0C00
  unsigned n = rw(w, 0x0306);
  ww(w, 0x0306, (n << 1) & 0xFFFF);                                   // 53D5 LDY $0306 / ASL / STA $0306
  // $7E:350E (D = $2100): the stack moved onto $0C00 (S = $0BFF, the old S kept in `$00FE`) and each byte pulled is two nibbles
  // through WMDATA from $7F:0200, the low one first. (S at its TSX: $06FF at $7E:3000, the dispatch's JSR, 350E's JSR and PHD.)
  ww(w, 0x00FE, 0x06F9);
  for (unsigned i = 0, wm = 0x10200; i < n; i++) { uint8_t b = w[(0x0C00 + i) & 0x1FFFF]; w[wm++ & 0x1FFFF] = b & 0x0F; w[wm++ & 0x1FFFF] = b >> 4; }
  w[0x0305] = 0x7F; ww(w, 0x0303, 0x0200); w[0x00F0] = 0; w[0x00F2] = 0x19;
  CtSeqPicDma38E7(sp, w);                                             // 53F7: the mode-7 characters, VRAM's high bytes
  w[0x0305] = 0; w[0x00F1] = 0; w[0x00F0] = 0x60;
  copy36AA(w, io, 0x3F, 0x0C00);                                      // 540A: $60 tile numbers -> $7E:0C00
  w[0x0305] = 0x7F; ww(w, 0x0303, 0x0200);
  CtSeqPicUnpack(w, io, 0x42);                                        // 541B: packet $42 -> $7F:0200
  sp->vaddr = 0x4000;                                                 // 541E
  for (unsigned k = 0; k < 0x60; k++)                                 // 54EA: WMDATA from $7E:0C00 picks each tile; 16 words
    CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x10200 + ((unsigned)w[0x0C00 + k] << 5), 0x20);   //   to $2118 / $2119 (VMAIN $80)
  w[0x1FFFE] = 0; w[0x1FFFF] = 0;                                     // ($7F:FFFE, the count, run down)
  w[0x00F0] = 0x01; w[0x00F2] = 0x18;
  sp->vaddr = 0x4600;
  CtSeqPicUnpack(w, io, 0x33);                                        // 5437: packet $33 -> $7F:0200, then to VRAM $4600
  CtSeqPicDma38E7(sp, w);
  sp->vaddr = 0x0000;
  CtSeqPicUnpack(w, io, 0x30);                                        // 5447: packet $30 -> $7F:0200, the map
  for (unsigned r = 0, x = 0x0200, y = 0x2200; r < 0x40; r++) {     // $7E:37CA: 64 rows of 64, each followed by 64 zeros
    for (unsigned i = 0; i < 0x40; i++) w[0x10000 | ((y + i) & 0xFFFF)] = w[0x10000 | ((x + i) & 0xFFFF)];
    for (unsigned i = 0; i < 0x40; i++) w[0x10000 | ((y + 0x40 + i) & 0xFFFF)] = 0;
    x += 0x40; y += 0x80;
  }
  ww(w, 0x00F0, 0);                                                   // (37EB DEC $F0, 16-bit, to 0)
  sp->vmain = 0x00;                                                   // 5451 TDC / STA $2115
  CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x0380);               // 545D: $380 bytes of the map, VRAM's low bytes
  CtSeqStagePicDma(sp, w, 0x08, 0x18, 0x0350B, 0x3C80);               // 5473 $7E:90A7: the rest from one byte ($7E:350B)
  w[0x0305] = 0; w[0x00F1] = 0; w[0x00F0] = 0x20;
  copy36AA(w, io, 0x3C, 0x2200);                                      // 5488: the palettes
  w[0x00F0] = 0x40;                                                   // 548B ASL $F0
  copy36AA(w, io, 0x36, 0x2300);
  w[0x2200] = 0; w[0x2201] = 0;                                       // 5496-549B
  CtSeqPicWrite(&sp->p, 0x2C, 0x10, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x00, 0);   // 54AD STX $212C: the sprites alone
}
// $7E:54C1-54E9 -- after the script and the stars: mode 7, M7SEL $80, the maps' and characters' bases, CGWSEL 02.
void CtEntry4PicSetupB(CtSeqStagePic* sp) {
  CtSeqPicWrite(&sp->p, 0x05, 0x07, 0);
  CtSeqPicWrite(&sp->p, 0x1A, 0x80, 0);
  CtSeqPicWrite(&sp->p, 0x07, 0x60, 0); CtSeqPicWrite(&sp->p, 0x08, 0x64, 0); CtSeqPicWrite(&sp->p, 0x09, 0x68, 0);
  CtSeqPicWrite(&sp->p, 0x0B, 0x55, 0); CtSeqPicWrite(&sp->p, 0x0C, 0x55, 0);
  CtSeqPicWrite(&sp->p, 0x30, 0x02, 0);
}
// $7E:5654 (D = $0A00) -- page A's registers to the PPU: M7A-M7D = r10-r13, M7X / M7Y = r3 / r4, BG1 H / V = r1 / r2.
static void hook5654(CtSeqStagePic* sp, const uint8_t* w) {
  static const uint8_t reg[8] = { 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x0D, 0x0E };
  static const uint8_t off[8] = { 0x14, 0x16, 0x18, 0x1A, 0x06, 0x08, 0x02, 0x04 };
  for (int k = 0; k < 8; k++) { CtSeqPicWrite(&sp->p, reg[k], w[0x0A00 + off[k]], 0); CtSeqPicWrite(&sp->p, reg[k], w[0x0A01 + off[k]], 0); }
}
unsigned CtEntry4PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  if (hook == 0x5654) { hook5654(sp, w); return hook; }
  { static unsigned said = 0; if (said != hook) { said = hook; fprintf(stderr, "entry4pic: UNDERIVED vblank hook $%04X\n", hook); } }
  return hook;
}
// $7E:56EB -- the planet: C3/0D5E's list $7E:56FD at x 0, y $70.
int CtEntry4PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t) {
  (void)sp;
  if (t != 0x56EB) return 0;
  ww(w, 0x15, 0x0000); ww(w, 0x17, 0x0070);
  CtSeqPicOam0D5E(w, 0x56FD);
  return 1;
}
