// seq_endingpic.c -- THE ENDING's picture, native (stage 2; its logic, stage 1, is seq_ending.c): sequence entries 10 / 11 /
// 14 / 15 (`FF 2A` / `FF 2B` / `FF 2E` / `FF 2F`) -- the world turning in mode 7 under the staff credits, the shooting stars,
// the planet's last screens.
//
// The setup $7E:8538: packet $63's 2bpp tiles widened at VRAM $5000 ($66B0, entry 7's), C3/09E9's reformatting code at
// $7E:B574, the mode-7 characters (packet $7E, the high bytes) and the map (packets $81 / $D2, $37CA's rows of 64 + 64 zeros, the
// low bytes through $8752), $4000-$4FFF's low bytes the byte at $7E:350B ($90A7), the sprites' tiles (packet $87 at $4000),
// the palettes ($36AA's rows, $8772, packet $8A, $38AF); after the script: $9BB8 (the two shooting stars' scripts), $36CC's
// HDMA channels from $8FEA, the layers. Page A's first op `F0 87CC` (or `F0 8790`, the world again) loads the world's sprite
// sheet ($7F:0A00, streamed mirrored into $7F:5600 by $3ED3) and points the main thread at $8930. The hooks: the common three,
// $9DBC (mode 7's centre and the scroll origin), $9080 (the HDMA tables' line counts), $9AF3 (the orbit's centre), $89C0 (the
// world's sprite sheet to VRAM $5800), $8850 (the planet's sprite and window). The tasks: $9ABD ($357B's matrix with the
// pre-routine $9B46, only on a change), $9BF9 (the shooting stars, $A03D), $8850, and the credits' text ($9B4F -> $64CA). The
// main thread $8930 scales the world's sheet by page B's r0 (C3/0307) and reformats it ($B574). disasm/notes/seq_picture.md
// sec. 9.
#include "seq_endingpic.h"
#include "seq_racepic.h"
#include "seq_entry7pic.h"
#include "seq_ending.h"
#include "seq_entry4.h"
#include <stdio.h>
#include <string.h>

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }
static void mvn7F(uint8_t* w, unsigned dst, unsigned src, unsigned n) {   // MVN $7F,$7F: X / Y wrap in the bank
  for (unsigned i = 0; i < n; i++) w[0x10000 | ((dst + i) & 0xFFFF)] = w[0x10000 | ((src + i) & 0xFFFF)];
}
static void reg(CtSeqStagePic* sp, uint8_t r, uint8_t v) { CtSeqPicWrite(&sp->p, r, v, 0); }

// The main thread's carry into the divide ($32E2's first `ROL $F0`): what the last full pass's `CPX` left (8993 / 899A).
static int s_threadC;

// $7E:37CA -- the mode-7 map's rows: 64 rows of 64 bytes from $7F:0200, each followed by 64 zeros, at $7F:2200 + row x 128
// (`STZ $0000,X` 16-bit, then `MVN` 63 bytes forward).
static void rows37CA(uint8_t* w) {
  for (unsigned r = 0; r < 0x40; r++) {
    mvn7F(w, 0x2200 + r * 0x80, 0x0200 + r * 0x40, 0x40);
    for (unsigned i = 0; i < 0x40; i++) w[0x10000 | ((0x2240 + r * 0x80 + i) & 0xFFFF)] = 0;
  }
}
// $7E:8752 -- `STZ $2115`, the low bytes of VRAM from $7F:2200, $2000 bytes (DMAP 00 to $2118).
static void dma8752(CtSeqStagePic* sp, const uint8_t* w) { sp->vmain = 0; CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x2000); }
// $7E:36AA with the caller's direct page (`$F0` the count): `$F0` bytes from the $FE-table's pointer X (bank $FE) to Y, in bank
// $7E or $7F by `$0305` bit 0.
static void copy36AA(uint8_t* w, const CtRaceIo* io, unsigned d, unsigned x, unsigned y) {
  unsigned keep = rw(w, 0xF0);
  ww(w, 0xF0, rw(w, d + 0xF0));
  CtSeqPicCopy36AA(w, io, x, y);
  ww(w, 0xF0, keep);
}
// $7E:3ED3 (the data bank $7F, D = $2100) -- eight rows through WMDATA: each the 32 bytes from $7F:(Y + $7F:0200) down (a
// mirror), then 32 zeros; Y += $40.
static unsigned stream3ED3(uint8_t* w, unsigned* wm, unsigned y) {
  for (int k = 0; k < 8; k++) {
    unsigned x = (y + rw(w, 0x10200)) & 0xFFFF;
    for (int i = 0; i < 0x20; i++) { w[*wm & 0x1FFFF] = w[0x10000 | x]; *wm = (*wm + 1) & 0x1FFFF; x = (x - 1) & 0xFFFF; }
    for (int i = 0; i < 0x20; i++) { w[*wm & 0x1FFFF] = 0; *wm = (*wm + 1) & 0x1FFFF; }
    y = (y + 0x40) & 0xFFFF;
  }
  return y;
}

// $7E:8538-86A1 (8-bit A, D = 0) -- the setup's picture before the script.
void CtEndingPicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  unsigned e = w[0x0384] & 0x0F;
  sp->vmain = 0x80;                                                   // 8538
  ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                       // 853D $89ED: $7F:0200
  sp->vaddr = 0x5000;
  CtSeqPicUnpack(w, io, 0x63);                                        // 8540
  CtSeqPicWiden66B0(sp, w, 0x80);                                     // 854C LDX #0080 / JSR $66B0
  CtSeqPicGen09E9(w, 0x0B574, 0x0C00, 0x0200);                        // 8558 $2181 = $7E:B574, JSL C3/09E9
  sp->vmain = 0x80; sp->vaddr = 0x0000;                               // 856D
  CtSeqPicUnpack(w, io, 0x7E);
  w[0x00F0] = 0x00; w[0x00F2] = 0x19; CtSeqPicDma38E7(sp, w);         // 857E: the characters, the high bytes
  sp->vmain = 0x00; sp->vaddr = 0x0000;                               // 858A
  CtSeqPicUnpack(w, io, 0x81); rows37CA(w); dma8752(sp, w);           // 8595: the map, two halves
  CtSeqPicUnpack(w, io, 0xD2); rows37CA(w); dma8752(sp, w);
  CtSeqStagePicDma(sp, w, 0x08, 0x18, 0x350B, 0x1000);                // 85AF $90A7: the byte at $7E:350B, $1000 times
  sp->vmain = 0x80; sp->vaddr = 0x4000;                               // 85B5
  CtSeqPicUnpack(w, io, 0x87);
  w[0x00F0] = 0x01; w[0x00F2] = 0x18; CtSeqPicDma38E7(sp, w);
  if (e == 0x0A) {                                                    // 85D4 CMP #0A: entry 10's own pictures
    CtSeqPicUnpack(w, io, 0xBD);                                      // 85E1 (to $7F:0200)
    ww(w, 0x0303, 0x2200); CtSeqPicUnpack(w, io, 0xB4);               // 85E7 (to $7F:2200)
    unsigned wm = ((unsigned)(w[0x0305] & 1) << 16) | 0x2200;         // 85F3 $2181 = $2200, the bank packet $B4's unpack left
                                                                      //   in $2183 (C3/0557: `$0305`'s, $7F)
    ww(w, 0x18000, 0x0140);                                           // 85FF LDA #0140 / STA $8000 (the data bank $7F)
    unsigned y = 0xD800;
    do {                                                              // 860C LDA $80 (WMDATA) / AND #00FF / ASL x5 / ADC #0200
      unsigned x = (((unsigned)w[wm & 0x1FFFF] << 5) + 0x0200) & 0xFFFF; wm = (wm + 1) & 0x1FFFF;
      mvn7F(w, y, x, 0x20); y = (y + 0x20) & 0xFFFF;                  // 861E MVN $7F,$7F (32 bytes)
      ww(w, 0x18000, rw(w, 0x18000) - 1);                             // 8621 DEC $8000 / BNE
    } while (rw(w, 0x18000));
    sp->vaddr = 0x5C00;                                               // 862D: $7F:E000, $0800 bytes to $5C00 (DMAP 01)
    CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x1E000, 0x0800);
  }
  w[0x0305] = 0;                                                      // 8655
  ww(w, 0x00F0, 0x0100);
  CtSeqPicCopy36AA(w, io, 0x96, 0x2000); CtSeqPicCopy36AA(w, io, 0x99, 0xC995);
  CtSeqPicCopy36AA(w, io, 0x9C, 0xCA95); CtSeqPicCopy36AA(w, io, 0x9F, 0xCB95);
  ww(w, 0x00F0, 0x0040);                                              // 8681
  CtSeqPicCopy36AA(w, io, 0x90, 0x2100);
  ww(w, 0x21E2, 0x77BD); ww(w, 0x21E4, 0x4631); ww(w, 0x21E6, 0x1CE7); ww(w, 0x2000, 0x0000);   // 868F $8772
  ww(w, 0x0303, 0x2180); CtSeqPicUnpack(w, io, 0x8A);                 // 8692 (to $7E:2180)
  memmove(w + 0x2200, w + 0x2000, 0x200);                             // 869E $38AF
}

// $7E:9BD8 (M = 16, D = 0, the data bank $7E) -- a shooting star's script, star X: `$9F` set -- the one at $A60A; else one of
// the five at $9C4E by the RNG ($3448 & 3: never past the fourth). Its frame count 1.
static uint8_t rng3448(uint8_t* w, const CtRaceIo* io) {             // $7E:3448: $039B + 1, the byte at C0/FE00 + it
  uint8_t x = ++w[0x039B];
  return (io && io->rom && io->romLen > 0xFE00 + 0xFF) ? io->rom[0xFE00 + x] : 0;
}
static void star9BD8(uint8_t* w, unsigned x, const CtRaceIo* io) {
  unsigned a;
  if (rw(w, 0x9F)) a = 0xA60A;                                        // 9BD8 LDA $9F / BEQ; LDA #A60A
  else a = rw(w, 0x9C4E + ((rng3448(w, io) & 0x0003) << 1));          // 9BE1 JSR $3448 / AND #0003 / ASL / LDA $9C4E,Y
  ww(w, 0x8F + x, a);                                                 // 9BF1 STA $8F,X
  ww(w, 0x87 + x, 0x0001);                                            // 9BF3 LDA #0001 / STA $87,X
}
// $7E:8722-874E -- the layers, after $9BB8 and the HDMA channels.
void CtEndingPicSetupB(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  star9BD8(w, 0, io); ww(w, 0x97, 0x7080);                            // 870E $9BB8: star 0 at (x $80, y $70) ...
  star9BD8(w, 2, io); ww(w, 0x99, 0x60A0);                            //   ... star 1 at ($A0, $60)
  CtSeqPicHdma36CC(sp, w, 0x8FEA);                                    // 8711 LDY #8FEA / JSR $36CC
  reg(sp, 0x1A, 0xC0); reg(sp, 0x05, 0x07); reg(sp, 0x25, 0x03); reg(sp, 0x2E, 0x11);   // 8722
  reg(sp, 0x2C, 0x01); reg(sp, 0x2D, 0x10); reg(sp, 0x30, 0x02); reg(sp, 0x31, 0x05);
  reg(sp, 0x07, 0x70); reg(sp, 0x08, 0x74); reg(sp, 0x0B, 0x66);
}

// $7E:87DD-884D (D = the script page's, the data bank $00 then $7F) -- the world's sprite sheet: packet $D8 at $7F:0A00; then
// ($3ED3 twice, fifteen times) its 16-row blocks from $7F:4E00 up, each row mirrored into WMDATA from $7F:5600 with 32 zeros
// after it ($7F:0200 = $20, the source's offset; $7F:0202 the block, less $400 each time; $7F:0204 the count); `$0037` = $0A00;
// the palettes $A5 at $2100 and $2300 ($36AA, `$F0` = $20 on the page).
static void world87DD(uint8_t* w, const CtRaceIo* io, unsigned d) {
  ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                       // 87DD $89ED
  ww(w, 0x0303, 0x0A00); CtSeqPicUnpack(w, io, 0xD8);                 // 87E0
  ww(w, 0x10200, 0x0020);                                             // 87F5 (the data bank $7F, D = $2100)
  unsigned wm = 0x15600;                                              // 87FB $2183 = 01, $2181 = $5600
  ww(w, 0x10202, 0x4E00); ww(w, 0x10204, 0x000F);
  do {
    unsigned y = rw(w, 0x10202);                                      // 8810 LDY $0202
    y = stream3ED3(w, &wm, y); stream3ED3(w, &wm, y);
    ww(w, 0x10202, rw(w, 0x10202) - 0x0400);                          // 8819 REP #21 / SBC #03FF (the carry clear)
    w[0x10204]--;                                                     // 8826 DEC $0204 (8-bit) / BNE
  } while (w[0x10204]);
  ww(w, 0x0037, 0x0A00);                                              // 882D
  w[0x0305] = 0;
  ww(w, d + 0xF0, 0x0020);                                            // 8836 LDX #0020 / STX $F0 (the page's)
  copy36AA(w, io, d, 0xA5, 0x2100); copy36AA(w, io, d, 0xA5, 0x2300);
}
int CtEndingPicHas(uint8_t operand) { return CtEndingStageHas(operand); }
// ---- the last screens (`FF 2E` / `FF 2F`) ----
// $7E:5E54 (4bpp) / $7E:5EF8 (8bpp), the data bank $7F, D = 0 -- one tile's rows from $7F:X into mode 7's characters (the high
// bytes through $2119, VMAIN $80): each pixel's bits rolled out of the planes' bytes ($3B.. as the rows load them), the first
// plane in bit 0. Returns X past the rows (2 a row); the counter `$3F` / `$31` and the planes' bytes end at 0.
static unsigned tile5E54(CtSeqStagePic* sp, uint8_t* w, unsigned x) {
  w[0x3F] = 0x08;
  do {
    ww(w, 0x3B, rw(w, 0x10000 | x)); ww(w, 0x3D, rw(w, 0x10000 | ((x + 0x10) & 0xFFFF))); x = (x + 2) & 0xFFFF;
    for (int px = 0; px < 8; px++) {
      uint8_t v = 0;                                                  // TDC (D = 0)
      for (unsigned b = 0x3E; b >= 0x3B; b--) { v = (uint8_t)((v << 1) | (w[b] >> 7)); w[b] = (uint8_t)(w[b] << 1); }
      CtSeqStagePicVramWrite(sp, 1, v);                               // STA $002119
    }
  } while (--w[0x3F]);                                                // DEC $3F / BEQ
  return x;
}
static unsigned tile5EF8(CtSeqStagePic* sp, uint8_t* w, unsigned x) {
  w[0x31] = 0x08;
  do {
    for (unsigned k = 0; k < 4; k++) ww(w, 0x3B + 2 * k, rw(w, 0x10000 | ((x + 0x10 * k) & 0xFFFF)));
    x = (x + 2) & 0xFFFF;
    for (int px = 0; px < 8; px++) {
      uint8_t v = 0;                                                  // (eight ROLs: A's old bits all out)
      for (unsigned b = 0x42; b >= 0x3B; b--) { v = (uint8_t)((v << 1) | (w[b] >> 7)); w[b] = (uint8_t)(w[b] << 1); }
      CtSeqStagePicVramWrite(sp, 1, v);
    }
  } while (--w[0x31]);
  return x;
}
// $7E:5E3C (Y = the tiles) -- from $7F:0200, each tile through the routine its `JSR` names ($5E44: $5E54, or $5EF8 once $8C14
// patched it) and X + the step its `ADC #imm` holds ($5E4A: $10, or $30), the carry clear.
static void tiles5E3C(CtSeqStagePic* sp, uint8_t* w, unsigned n) {
  unsigned x = 0x0200;
  for (; n; n--) {
    unsigned tgt = rw(w, 0x5E44);
    if (tgt == 0x5EF8) x = tile5EF8(sp, w, x);
    else if (tgt == 0x5E54) x = tile5E54(sp, w, x);
    else { fprintf(stderr, "endingpic: UNDERIVED -- $5E3C's JSR names $%04X\n", tgt); return; }
    x = (x + rw(w, 0x5E4A)) & 0xFFFF;                                 // 5E46 REP #21 / TXA / ADC #imm / TAX
  }
}
// $7E:5AB2 -- the DMA $38E7, then mode 7's map: 32 rows of 32 bytes read through WMDATA from $0200 (in the bank C3/0557 left,
// `$0305`'s: every second byte skipped) into $2118, each row padded with $60 zeros; then $1000 bytes of the byte at $7E:350B.
static void map5AB2(CtSeqStagePic* sp, uint8_t* w) {
  CtSeqPicDma38E7(sp, w);
  unsigned wm = ((unsigned)(w[0x0305] & 1) << 16) | 0x0200;           // 5AB6 $2181 = $0200
  sp->vmain = 0x00; sp->vaddr = 0x0000;
  for (int r = 0; r < 0x20; r++) {
    for (int i = 0; i < 0x20; i++) { CtSeqStagePicVramWrite(sp, 0, w[wm & 0x1FFFF]); wm = (wm + 2) & 0x1FFFF; }
    for (int i = 0; i < 0x60; i++) CtSeqStagePicVramWrite(sp, 0, 0);
  }
  CtSeqStagePicDma(sp, w, 0x08, 0x18, 0x350B, 0x1000);                // 5AE8
}
int CtEndingPicCall(CtSeqStagePic* sp, uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io) {
  uint8_t dl = (uint8_t)page;                                         // TDC: A's low byte is D's
  switch (tgt) {
    case 0x87CC:                                                      // the world, as the setup leaves it
      ww(w, 0x3024, 0x8930);                                          // 87CD the main thread's call
      sp->hdmaen = dl;                                                // 87D9 TDC / STA $420C
      world87DD(w, io, page);
      return 1;
    case 0x8790:                                                      // the world again, its map reloaded
      ww(w, 0x3024, 0x8930);
      sp->hdmaen = dl;
      memmove(w + 0x2200, w + 0x2000, 0x200);                         // 87A1 $38AF
      w[0x4562] = 0x05;                                               // 87A4 LDA #05 / STA $7E4562
      ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                   // 87AA $89ED
      CtSeqPicUnpack(w, io, 0x84); CtSeqPicRows37A6(w);               // 87AD packet $84, $37A6
      sp->vmain = dl; sp->vaddr = (uint16_t)(dl | (dl << 8)); reg(sp, 0x1A, dl);   // 87B7 TDC / $2115 / $2116 / $2117 / $211A
      dma8752(sp, w); dma8752(sp, w);
      world87DD(w, io, page);
      return 1;
    case 0x8BAF:                                                      // THE STARS (after the logic's lists and $5777):
      sp->hdmaen = 0; reg(sp, 0x00, 0x80);                            // 8BC0 STA $420C (A = 0), 8BC6 INIDISP $80
      ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                   // 8BCE $89ED
      w[0x00F0] = 0x01; w[0x00F2] = 0x18;
      sp->vaddr = 0x4600; CtSeqPicUnpack(w, io, 0x33); CtSeqPicDma38E7(sp, w);   // 8BDB: the stars' tiles
      ww(w, 0x0304, 0x0000);                                          // 8BEB LDX #0000 / STX $0304: bank $7E
      ww(w, 0x00F0, 0x0020); CtSeqPicCopy36AA(w, io, 0x39, 0x2120);   // 8BF1 (D = 0)
      memmove(w + 0x2200, w + 0x2000, 0x200);                         // 8BFF $38AF
      return 1;
    case 0x8C14:                                                      // THE PLANET (mode 7, 8bpp):
      sp->hdmaen = 0; reg(sp, 0x00, 0x80); reg(sp, 0x1A, 0x80);       // 8C25 / 8C2D, 8C30 M7SEL
      ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                   // 8C33 $89ED
      w[0x00F0] = 0x01; w[0x00F2] = 0x18; sp->vmain = 0x80;
      CtSeqPicUnpack(w, io, 0xCF); sp->vaddr = 0x6000; CtSeqPicDma38E7(sp, w);   // 8C45
      sp->vaddr = 0x0000;
      w[0x5E44] = 0xF8; w[0x5E45] = 0x5E; w[0x5E4A] = 0x30;           // 8C5B: $5E3C's JSR -> $5EF8, its step $30
      tiles5E3C(sp, w, 0xA4);                                         // 8C6D LDY #00A4 / JSR $5E3C
      CtSeqPicUnpack(w, io, 0xB1); sp->vaddr = 0x4400; map5AB2(sp, w);   // 8C73
      w[0x0305] = 0; ww(w, 0x0303, 0x2020); CtSeqPicUnpack(w, io, 0x54);   // 8C82: the palette at $7E:2020
      ww(w, 0x2000, 0x0000); ww(w, 0x2002, 0x77BD); ww(w, 0x2004, 0x4631); ww(w, 0x2006, 0x1CE7);   // 8C91 $8CB2
      memmove(w + 0x2200, w + 0x2000, 0x200);                         // 8C94 $38AF
      CtRacePicCgram(sp, w);                                          // 8C97 $388B
      reg(sp, 0x2C, 0x01); reg(sp, 0x2D, 0x10); reg(sp, 0x2E, 0x00); reg(sp, 0x2F, 0x00); reg(sp, 0x05, 0x07);   // 8C9E
      return 1;
    case 0x8CD0: {                                                    // "THE END"'s layers (entry 14; 15 left at 8CE9):
      sp->hdmaen = 0;                                                 // 8CD8 LDA #5B00 / SEP / STA $420C
      ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                   // 8CF2 $89ED
      CtSeqPicUnpack(w, io, 0xAE);
      ww(w, 0x0303, 0x0400); CtSeqPicUnpack(w, io, 0x8D);             // 8CFB
      ww(w, 0x0303, 0x0F00); CtSeqPicUnpack(w, io, 0xC0);             // 8D07
      // 8D13 (D = $2100): WMDATA from $1000 in the bank C3/0557 left -- eight times, for X = 0, 4, 8, C, eight times `x, 0, x+1, 0,
      // x+2, 0, x+3, 0` (`STY $7F` stores Y = 0 to $217F, the SPC's port 3, and $2180).
      unsigned wm = ((unsigned)(w[0x0305] & 1) << 16) | 0x1000;
      w[0x00F0] = 0x08;
      do {
        for (unsigned x = 0; x != 0x10; x += 4) {
          w[0x00F2] = 0x08;
          do { for (unsigned i = 0; i < 4; i++) { w[wm & 0x1FFFF] = (uint8_t)(x + i); w[(wm + 1) & 0x1FFFF] = 0; wm = (wm + 2) & 0x1FFFF; } }
          while (--w[0x00F2]);
        }
      } while (--w[0x00F0]);
      return 1;
    }
    case 0x8DF3:                                                      // THE LAST PLANET's palettes (D = the page's):
      ww(w, 0x0304, 0x0000);                                          // 8DFA LDX #0000 / STX $0304
      ww(w, page + 0xF0, 0x0020);
      copy36AA(w, io, page, 0xA2, 0x21E0); copy36AA(w, io, page, 0xAB, 0x2000);
      memmove(w + 0x2200, w + 0x2000, 0x200);                         // 8E17 $38AF
      return 1;
    case 0x8A01:                                                      // ENTRY 10's PLANET (after the logic's lists; D = 0):
      reg(sp, 0x00, 0x80); sp->hdmaen = 0; sp->vmain = 0x80;          // 8A12 INIDISP $80, 8A17 TDC / STA $420C, 8A1B VMAIN
      ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                   // 8A20 $89ED
      w[0x00F0] = 0x01; w[0x00F2] = 0x18;
      sp->vaddr = 0x6000; CtSeqPicUnpack(w, io, 0xC6); CtSeqPicDma38E7(sp, w);   // 8A23
      sp->vaddr = 0x7000; CtSeqPicUnpack(w, io, 0xC9); CtSeqPicDma38E7(sp, w);   // 8A3D
      CtSeqStagePicDma(sp, w, 0x09, 0x18, 0x350B, 0x0700);            // 8A4D $90AA: the fill byte on, both bytes
      sp->vaddr = 0x7400; CtSeqPicUnpack(w, io, 0xCC); CtSeqPicDma38E7(sp, w);   // 8A56
      CtSeqStagePicDma(sp, w, 0x09, 0x18, 0x350B, 0x0680);            // 8A66 $90AA
      sp->vaddr = 0x0000; CtSeqPicUnpack(w, io, 0xB7);                // 8A6F, then $353C: each byte of $7F:0200 through
      { unsigned wm = 0x10200;                                        //   WMDATA, its low nibble then its high to $2119 (two
        for (unsigned n = rw(w, 0x0306); n; n--) {                    //   mode-7 pixels a byte), `$0306` of them
          uint8_t b = w[wm & 0x1FFFF]; wm = (wm + 1) & 0x1FFFF;
          CtSeqStagePicVramWrite(sp, 1, (uint8_t)(b & 0x0F)); CtSeqStagePicVramWrite(sp, 1, (uint8_t)(b >> 4));
        } }
      sp->vaddr = 0x0000; CtSeqPicUnpack(w, io, 0xBA); rows37CA(w);   // 8A81 packet $BA, $37CA
      sp->vmain = 0x00; CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x2000);   // 8A91 (TDC: VMAIN 0, DMAP 0)
      w[0x0305] = 0; ww(w, 0x00F0, 0x0060); CtSeqPicCopy36AA(w, io, 0x93, 0x2000);   // 8AB3
      memmove(w + 0x2200, w + 0x2000, 0x200);                         // 8AC4 $38AF
      CtRacePicCgram(sp, w);                                          // 8AC7 $388B
      CtSeqPicHdma36CC(sp, w, 0x9003);                                // 8ACA
      reg(sp, 0x1A, 0x80); reg(sp, 0x05, 0x07);                       // 8AD5 M7SEL, BGMODE
      reg(sp, 0x2E, 0x11); reg(sp, 0x2F, 0x01); reg(sp, 0x2C, 0x11); reg(sp, 0x2D, 0x10); reg(sp, 0x31, 0x21);   // 8ADF
      return 1;
    case 0x8AF5:                                                      // entry 10's last world (D = 0): the HDMA from $900E,
      reg(sp, 0x00, 0x80); sp->hdmaen = 0;                            //   TM / TS, WOBJSEL, the tables' counts, BG2's scroll
      CtSeqPicHdma36CC(sp, w, 0x900E);
      reg(sp, 0x2C, 0x13); reg(sp, 0x2D, 0x10); reg(sp, 0x25, 0x00);
      w[0x9043] = w[0x9046] = w[0x9039] = w[0x903C] = w[0x9057] = w[0x905A] = 0x64;   // 8B25
      w[0x1406] = 0x70; reg(sp, 0x10, 0x70); w[0x1407] = 0xFF; reg(sp, 0x10, 0xFF);    // 8B3F
      return 1;
    default: return 0;
  }
}

// ---- the vblank hooks (M = 8, X = 16; the dispatcher's `CPX #0010` leaves the carry clear for every slot after the first) ----
// $7E:5654 (D = $0A00) -- page A's matrix ($14-$1B), centre ($06 / $08) and scroll ($02 / $04) to the PPU, low byte first.
static void regs5654(CtSeqStagePic* sp, const uint8_t* w) {
  static const uint8_t r[] = { 0x1B, 0x1B, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F, 0x20, 0x20, 0x0D, 0x0D, 0x0E, 0x0E };
  static const uint8_t s[] = { 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x06, 0x07, 0x08, 0x09, 0x02, 0x03, 0x04, 0x05 };
  for (int i = 0; i < 16; i++) reg(sp, r[i], w[0x0A00 + s[i]]);
}
// $7E:9DBC -- `$7B` set (the last screens): the centre = page A's r1 / r2 + $80 (ADC on the carry) and $5654. Else the centre
// from $1408 / $140A, $1404 / $1406 = $1400 / $1402, $140C = -r5, $140E = $27 - r6.
static void hook9DBC(CtSeqStagePic* sp, uint8_t* w, unsigned slot) {
  if (w[0x7B]) {
    if (!slot) { static int said; if (!said++) fprintf(stderr, "endingpic: UNDERIVED -- hook $9DBC in slot 0 (the carry the NMI leaves)\n"); }
    unsigned a = rw(w, 0x0A02) + 0x0080; int c = a > 0xFFFF;          // 9DC2 ADC #0080 (carry clear)
    ww(w, 0x0A06, a);
    ww(w, 0x0A08, rw(w, 0x0A04) + 0x0080 + (unsigned)c);              // 9DCB ADC #0080
    regs5654(sp, w);
    return;
  }
  reg(sp, 0x1F, w[0x1408]); reg(sp, 0x1F, w[0x1409]);                 // 9DDB
  reg(sp, 0x20, w[0x140A]); reg(sp, 0x20, w[0x140B]);
  ww(w, 0x1404, rw(w, 0x1400)); ww(w, 0x1406, rw(w, 0x1402));         // 9DF5
  ww(w, 0x140C, ((rw(w, 0x0A0A) ^ 0xFFFF) + 1) & 0xFFFF);             // 9E01 EOR #FFFF / INC A
  ww(w, 0x140E, ((rw(w, 0x0A0C) ^ 0xFFFF) + 0x0027 + 1) & 0xFFFF);    // 9E0B EOR #FFFF / SEC / ADC #0027
}
// $7E:9080 -- the HDMA tables' line counts from page A's r6 / 2: $902F / $904D / $9043 / $452B, and + the LSR's carry $9050 /
// $9032 / $9046 / $452E.
static void hook9080(uint8_t* w) {
  uint8_t a = w[0x0A0C]; int c = a & 1; a >>= 1;
  w[0x902F] = w[0x904D] = w[0x9043] = w[0x452B] = a;
  a = (uint8_t)(a + c);                                               // 9094 ADC #00
  w[0x9050] = w[0x9032] = w[0x9046] = w[0x452E] = a;
}
// $7E:9AF3 (M = 16, D = $0A00) -- the orbit's centre: r15 = page B's r6; $1412 = (cos r5 x r15)'s bits 8-23, $1410 the sine's
// ($33D6, signed); $1400 = r1 + $1410, $1408 = it + $80, $1402 = r2 + $1412, $140A = it + $E0.
static void hook9AF3(uint8_t* w, const CtRaceIo* io) {
  const unsigned d = 0x0A00;
  ww(w, 0x0A1E, rw(w, 0x0B0C));
  ww(w, d + 0xF0, CtSeqSine(io, rw(w, d + 0x0A) + 0x0100)); ww(w, d + 0xF2, rw(w, d + 0x1E));   // 9AFF JSR $3461
  CtSeqMul33D6(w, d); ww(w, 0x1412, rw(w, d + 0xF5));
  ww(w, d + 0xF0, CtSeqSine(io, rw(w, d + 0x0A))); ww(w, d + 0xF2, rw(w, d + 0x1E));            // 9B12 JSR $3465
  CtSeqMul33D6(w, d); ww(w, 0x1410, rw(w, d + 0xF5));
  unsigned a = (rw(w, d + 0x02) + rw(w, 0x1410)) & 0xFFFF; ww(w, 0x1400, a); ww(w, 0x1408, a + 0x0080);
  a = (rw(w, d + 0x04) + rw(w, 0x1412)) & 0xFFFF; ww(w, 0x1402, a); ww(w, 0x140A, a + 0x00E0);
}
// $7E:89C0 -- the main thread's frame: once `$21` is set, `$2B` bytes from $00:0C00 to VRAM $5800 (DMAP 01, VMAIN $80); `$21`
// cleared.
static void hook89C0(CtSeqStagePic* sp, uint8_t* w) {
  if (!w[0x21]) return;
  sp->vaddr = 0x5800; sp->vmain = 0x80;
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x0C00, rw(w, 0x2B) ? rw(w, 0x2B) : 0x10000);
  w[0x21] = 0;
}
// $7E:8850 (8-bit) -- the planet: its sprite at ($0B02 - $4C, $0B04 - $4C / 2) through C3/0D5E (list $88B9, or $88DA with
// `$7B`), and the window's HDMA bytes: $9070 = y / 2, $9073 = + the carry; $9077 / $907A = x, $9078 / $907B = x + $40 (or $FF
// past the edge); $9076 / $9079 = $10 (or $20).
static void draw8850(uint8_t* w) {
  w[0xF4] = (uint8_t)(w[0x4C] >> 1);
  uint8_t a = (uint8_t)(w[0x0B04] - w[0xF4]); w[0x17] = a;            // 8857 SEC / SBC $F4
  int c = a & 1; a >>= 1; w[0x9070] = a;                              // 885F LSR A
  w[0x9073] = (uint8_t)(a + c);                                       // 8864 ADC #00
  a = (uint8_t)(w[0x0B02] - w[0x4C]); w[0x15] = a; w[0x9077] = w[0x907A] = a;   // 886A SEC / SBC $4C
  unsigned t = (unsigned)a + 0x40; a = t > 0xFF ? 0xFF : (uint8_t)t;  // 887A CLC / ADC #40 / BCC; LDA #FF
  w[0x9078] = w[0x907B] = a;
  w[0x16] = 0; w[0x18] = 0;
  if (!w[0x7B]) { w[0x9076] = w[0x9079] = 0x10; CtSeqPicOam0D5E(w, 0x88B9); }
  else { w[0x9076] = w[0x9079] = 0x20; CtSeqPicOam0D5E(w, 0x88DA); }
}
unsigned CtEndingPicHook(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned hook, const CtRaceIo* io) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  switch (hook) {
    case 0x3176: return hook;                                         // RTS
    case 0x9DBC: hook9DBC(sp, w, slot); return hook;
    case 0x9080: hook9080(w); return hook;
    case 0x9AF3: hook9AF3(w, io); return hook;
    case 0x89C0: hook89C0(sp, w); return hook;
    case 0x8850: draw8850(w); return hook;
    case 0x9E2B:                                                      // 9E2B (D = $0A00): mode 7 without a turn -- M7A = M7D = r0,
      reg(sp, 0x1C, 0); reg(sp, 0x1C, 0); reg(sp, 0x1D, 0); reg(sp, 0x1D, 0);   //   M7B = M7C = 0, the centre r3 / r4, the scroll
      reg(sp, 0x1B, w[0x0A00]); reg(sp, 0x1B, w[0x0A01]); reg(sp, 0x1E, w[0x0A00]); reg(sp, 0x1E, w[0x0A01]);   //   r1 / r2
      reg(sp, 0x1F, w[0x0A06]); reg(sp, 0x1F, w[0x0A07]); reg(sp, 0x20, w[0x0A08]); reg(sp, 0x20, w[0x0A09]);
      reg(sp, 0x0D, w[0x0A02]); reg(sp, 0x0D, w[0x0A03]); reg(sp, 0x0E, w[0x0A04]); reg(sp, 0x0E, w[0x0A05]);
      return hook;
    case 0x8D56:                                                      // "THE END"'s screen: $7F:0200 to VRAM $4000 ($200 bytes),
      sp->vaddr = 0x4000; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x10200, 0x0200);
      sp->vaddr = 0x4800; sp->vmain = 0x00; CtSeqStagePicDma(sp, w, 0x08, 0x18, 0x350B, 0x0400);   // 8D7D $90A7: the low bytes
      sp->vaddr = 0x4800; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x08, 0x19, 0x9E7A, 0x0400);   // 8D8C: the high, $7E:9E7A's
      CtSeqPicHdma36CC(sp, w, 0x9023);                                // 8DAC
      reg(sp, 0x0F, 0x00); reg(sp, 0x0F, 0x00);                       // 8DC1 (D = $2100, TDC: 0) BG4HOFS
      reg(sp, 0x0B, 0x46); reg(sp, 0x0C, 0x00); reg(sp, 0x07, 0x44); reg(sp, 0x08, 0x48); reg(sp, 0x09, 0x60);
      reg(sp, 0x05, 0x04); reg(sp, 0x31, 0x82); reg(sp, 0x2C, 0x13); reg(sp, 0x2D, 0x00); reg(sp, 0x2E, 0x00); reg(sp, 0x2F, 0x00);
      return 0;
    case 0x9EFC:                                                      // the rolling text's line: $1414 (32 bytes) to `$48`'s row
      sp->vaddr = (uint16_t)rw(w, 0x48); sp->vmain = 0x00;            //   (the low bytes), BG2's scroll `$46`
      CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x1414, 0x0020);
      reg(sp, 0x10, w[0x46]); reg(sp, 0x10, w[0x47]);
      return hook;
    case 0x9F2B: sp->vaddr = 0x5000; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x10400, 0x0A00); return 0;   // $9F59
    case 0x9F39: sp->vaddr = 0x4C00; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x11000, 0x0800); return 0;
    case 0x9F47: sp->vaddr = 0x0000; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x10F00, 0x0100); return 0;
    case 0x9F73: sp->vmain = 0x80; sp->vaddr = 0x20A0; CtSeqStagePicDma(sp, w, 0x09, 0x18, 0x350B, 0x06C0); return 0;   // $90AA
    case 0x8E44:                                                      // the last screen's layers (D = $2100), the HDMA from $9029
      reg(sp, 0x31, 0x21); reg(sp, 0x32, 0xE0); reg(sp, 0x2C, 0x01); reg(sp, 0x2D, 0x12); reg(sp, 0x05, 0x04);
      reg(sp, 0x08, 0x4C); reg(sp, 0x0B, 0x06); reg(sp, 0x09, 0x60); reg(sp, 0x23, 0x30); reg(sp, 0x2F, 0x02);
      reg(sp, 0x26, 0x00); reg(sp, 0x28, 0x00); reg(sp, 0x33, 0x00);
      CtSeqPicHdma36CC(sp, w, 0x9029);
      return 0;
    case 0x8E88: reg(sp, 0x10, w[0x0A04]); reg(sp, 0x10, w[0x0A05]); return hook;   // BG2's scroll = page A's r2
    case 0x9CE2: {                                                    // entry 10's sprite frame (`$A3` & 3): $800 bytes of $7F
      unsigned x = (unsigned)(w[0xA3] & 3) << 3;                      //   from the table $9D25 to VRAM $5800 (the logic keeps
      sp->vaddr = 0x5800; sp->vmain = 0x80;                           //   `$AB` / `$AC` and takes the hook out)
      CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x10000 | rw(w, 0x9D25 + x), 0x0800);
      return 0;
    }
    case 0x8B52: {                                                    // entry 10's tables and scroll from page A's r5:
      unsigned a = rw(w, 0x0A0A);                                     //   8B54 CMP #00E0 / BMI (else $E0)
      if (!((a - 0x00E0) & 0x8000)) a = 0x00E0;
      uint8_t b = (uint8_t)a; int c = b & 1; b >>= 1;                 // 8B61 LSR A (8-bit)
      w[0x9043] = w[0x9046] = w[0x9039] = b;
      b = (uint8_t)(b + c); w[0x903C] = w[0x9057] = w[0x905A] = b;    // 8B6E ADC #00
      unsigned s = rw(w, 0x0A0A) + 0x0031; c = s > 0xFFFF;            // 8B7C REP #21 / ADC #0031
      unsigned v = ((s ^ 0xFFFF) + 1) & 0xFFFF; ww(w, 0x1406, v);     // EOR #FFFF / INC A / STA $1406
      reg(sp, 0x10, (uint8_t)(v + 0x0020 + (unsigned)c)); reg(sp, 0x10, w[0x1407]);   // 8B8B ADC #0020; 8B93 LDA $1407
      ww(w, 0x140E, ((rw(w, 0x0A0A) ^ 0xFFFF) + 0x0080 + 1) & 0xFFFF); // 8B9B EOR / SEC / ADC #0080
      ww(w, 0x140C, 0xFF50);                                          // 8BA8
      return hook;
    }
    case 0x9E19:                                                      // BG4's scroll = page A's r7, `$1404` = r1, then $9E2B
      reg(sp, 0x0F, w[0x0A0E]); reg(sp, 0x0F, w[0x0A0F]); ww(w, 0x1404, rw(w, 0x0A02));
      return CtEndingPicHook(sp, w, slot, 0x9E2B, io) == 0x9E2B ? hook : 0;
    default: {
      static unsigned said = 0;
      if (said != hook) { said = hook; fprintf(stderr, "endingpic: UNDERIVED vblank hook $%04X\n", hook); }
      return hook;
    }
  }
}

// ---- the tasks (M = 16, D = 0) ----
// $7E:9B46 -- $357B's pre-routine: Y = (page A's r6 - 1) x 4, X = r8 (the loop's tail and limit as the overlay holds them).
static void pre9B46(uint8_t* w, unsigned* x, unsigned* y, unsigned* wm) {
  (void)wm;
  *y = ((rw(w, 0x0A0C) - 1) << 2) & 0xFFFF; *x = rw(w, 0x0A10);
}
// $7E:9ABD (D = $0A00) -- `$7B` (16-bit) set: one matrix for the whole screen ($56B4; the hook $9DBC writes it). Else the
// per-line matrix again only when page A's r0, r5, r6 or r8 changed since the last build ($30-$37).
static void task9ABD(uint8_t* w, const CtRaceIo* io) {
  if (rw(w, 0x007B)) { CtSeqMatrix56B4(w, io); return; }             // 9AC1 LDA $007B / BEQ; JMP $56B4
  if (rw(w, 0x0A00) == rw(w, 0x0A30) && rw(w, 0x0A0A) == rw(w, 0x0A32) && rw(w, 0x0A0C) == rw(w, 0x0A34) && rw(w, 0x0A10) == rw(w, 0x0A36))
    return;                                                           // 9AC9 .. 9AE1 RTS
  ww(w, 0x0A30, rw(w, 0x0A00)); ww(w, 0x0A32, rw(w, 0x0A0A)); ww(w, 0x0A34, rw(w, 0x0A0C)); ww(w, 0x0A36, rw(w, 0x0A10));
  if (rw(w, 0x35FD) != 0x9B46) { static unsigned said; if (said != rw(w, 0x35FD)) { said = rw(w, 0x35FD); fprintf(stderr, "endingpic: UNDERIVED $357B pre-routine $%04X\n", said); } return; }
  CtSeqPic357B(w, io, pre9B46);                                       // 9AF0 JMP $357B
}
// $7E:A03D (the data bank $7E) -- a list of large sprites read through WMDATA from A (`n + 1`, then tile, attr, x, y per sprite)
// at `$15` / `$17`: x = byte + `$15` (8-bit), y = byte + `$17` (16-bit). The code at $A074 `BCS` skips a sprite whose y is $E0
// or more -- unless a caller wrote it over with `NOP / NOP` ($9BF9 does) -- and ITS JUMP SKIPS THE `SEP #20`: the loop runs on
// with a 16-bit A (the count `$19` a word, each `LDA $2180` the byte twice, each store two bytes, x + `$15` a word) until a
// sprite is kept. The OAM cursor `$11`, the high table's `$13`.
static void listA03D(uint8_t* w, unsigned p) {
  unsigned y = rw(w, 0x11), x = rw(w, 0x13);                          // A042 LDY $11 / LDX $13
  w[0x19] = w[p & 0xFFFF]; p++;                                       // A049 LDA $2180 / STA $19 (8-bit)
  int m16 = 0;                                                        // M: 8-bit from A040's SEP #20
  #define B(o, v) (w[((o) + y) & 0x1FFFF] = (uint8_t)(v))
  for (unsigned guard = 0; guard < 0x10000; guard++) {
    if (m16) { unsigned n = (rw(w, 0x19) - 1) & 0xFFFF; ww(w, 0x19, n); if (!n) break; }   // A04E DEC $19 / BEQ (16-bit)
    else if (!--w[0x19]) break;
    uint8_t t = w[p & 0xFFFF], at, xb; p++;                           // A052 LDA $2180 (16-bit: the high byte is $2181's
    B(0x0702, t); if (m16) B(0x0703, t);                              //   open bus, the byte just read) / STA $0702,Y
    at = w[p & 0xFFFF]; p++;
    B(0x0703, at); if (m16) B(0x0704, at);                            // A058 LDA $2180 / STA $0703,Y
    xb = w[p & 0xFFFF]; p++;
    if (m16) { unsigned v = ((unsigned)xb | ((unsigned)xb << 8)) + rw(w, 0x15); B(0x0700, v); B(0x0701, v >> 8); }   // CLC / ADC $15
    else B(0x0700, xb + w[0x15]);
    unsigned a = (w[p & 0xFFFF] + rw(w, 0x17)) & 0xFFFF; p++;         // A067 LDA $2180 / REP #21 / AND #00FF / ADC $17
    m16 = 1;
    if (w[0xA074] == 0xB0 && a >= 0x00E0) continue;                   // A071 CMP #00E0 / BCS (the code as it stands): back to
                                                                      //   A04E with A still 16-bit -- the SEP #20 skipped
    m16 = 0;                                                          // A076 SEP #20
    B(0x0701, a);
    w[x & 0x1FFFF] = (uint8_t)((w[x & 0x1FFFF] >> 2) | 0x80);         // A07B LSR / SEC / ROR: large
    y = (y + 4) & 0xFFFF;
    if (!(y & 0x0F)) x = (x + 1) & 0xFFFF;                            // A088 TYA / AND #0F / BNE; INX
  }
  #undef B
  ww(w, 0x13, x); ww(w, 0x11, y);
}
// $7E:9BF9 (the data bank $7E) -- THE SHOOTING STARS, two (X = 0, 2): `$87,X` counts the frame down; at 0 the script `$8F,X`
// steps 2 to the next `n, list` (n = 0: a new script from $9BD8, and step again), `$87,X` = n, `$8F,X` + 1; then the list at
// ($8F,X) drawn at `$97,X` / `$98,X` through $A03D with its `BCS` patched out ($A074 = `NOP / NOP`, the old word kept in `$F0`
// and put back). $A090's two bytes are `CMP #E0` while it runs, `LDA #E0` after.
static void task9BF9(uint8_t* w, const CtRaceIo* io) {
  ww(w, 0xA090, 0xE0C9);                                              // 9BFB
  for (unsigned x = 0; x < 4; x += 2) {
    unsigned n = (rw(w, 0x87 + x) - 1) & 0xFFFF; ww(w, 0x87 + x, n);  // 9C04 DEC $87,X / BNE
    if (!n) {
      for (unsigned guard = 0; guard < 0x100; guard++) {
        ww(w, 0x8F + x, rw(w, 0x8F + x) + 2);                         // 9C08 INC $8F,X x 2
        n = rw(w, rw(w, 0x8F + x)) & 0x00FF;                          // LDA ($8F,X) / AND #00FF
        if (n) break;
        star9BD8(w, x, io);                                           // 9C13 JSR $9BD8 / BRA
      }
      ww(w, 0x87 + x, n); ww(w, 0x8F + x, rw(w, 0x8F + x) + 1);       // 9C18 STA $87,X / INC $8F,X
    }
    ww(w, 0x15, rw(w, 0x97 + x)); ww(w, 0x17, rw(w, 0x98 + x));       // 9C1C
    ww(w, 0xF0, rw(w, 0xA074)); ww(w, 0xA074, 0xEAEA);                // 9C24
    listA03D(w, rw(w, rw(w, 0x8F + x)));                              // 9C2F LDA ($8F,X) / JSR $A03D
    ww(w, 0xA074, rw(w, 0xF0));                                       // 9C3B
  }
  ww(w, 0xA090, 0xE0A9);                                              // 9C47
}
int CtEndingPicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  (void)sp; (void)slot;
  switch (t) {
    case 0x9ABD: task9ABD(w, io); return 1;
    case 0x9BF9: task9BF9(w, io); return 1;
    case 0x8850: draw8850(w); return 1;
    case 0x99FA: ww(w, 0x15, 0x003C); ww(w, 0x17, 0x0090); CtSeqPicOam0D5E(w, 0xA1A1); return 1;   // the last screen's sprite
    default: return 0;
  }
}

// ---- the main thread ----
// $7E:32E2 -- the divide (M = 16, D = 0): $F0 = A, $F8 = $F2 - 1, $F4 = 0; eight steps that shift $F0's top into A (`ROL $F0 /
// ROL A`) and eight that shift zeros (`ASL A`), each `SBC $F8 / BCS / ADC $F2 / CLC` and the carry into the quotient (`ROL
// $F4`, whose top goes on into the next `ROL $F0`). `*c` the carry in and out.
static unsigned div32E2(uint8_t* w, unsigned a, int* cp) {
  int c = *cp;
  ww(w, 0xF0, a); ww(w, 0xF8, rw(w, 0xF2) - 1); a = 0; ww(w, 0xF4, 0);
  for (int k = 0; k < 16; k++) {
    if (k < 8) {
      unsigned f0 = rw(w, 0xF0); int nc = (int)(f0 >> 15);
      ww(w, 0xF0, (f0 << 1) | (unsigned)c); c = nc;                   // ROL $F0
      nc = (int)(a >> 15); a = ((a << 1) | (unsigned)c) & 0xFFFF; c = nc;   // ROL A
    } else { c = (int)(a >> 15); a = (a << 1) & 0xFFFF; }             // ASL A
    long r = (long)a - (long)rw(w, 0xF8) - (long)(1 - c);             // SBC $F8
    c = r >= 0; a = (unsigned)r & 0xFFFF;
    if (!c) { a = (a + rw(w, 0xF2)) & 0xFFFF; c = 0; }                // BCS; ADC $F2 / CLC
    unsigned f4 = rw(w, 0xF4); int nc = (int)(f4 >> 15);
    ww(w, 0xF4, (f4 << 1) | (unsigned)c); c = nc;                     // ROL $F4
  }
  *cp = c;
  return a;
}
// $7E:8930 -- THE MAIN THREAD's pass (the loop $7E:3023 `JSR ($3024)`): unless the last frame is still to send (`$21`), the
// world's sheet scaled by page B's r0 & $1FF (`$27`, `$56`): `$25` rows ($20, or $40 with `$7B`) up to `$2B` ($400 / $800),
// from `$23` = `$4E`, `$4C` = $800 / `$27` ($32E2), `$29` = `$27` x 2, four groups (C3/0307, the data bank $7F), then
// reformatted at $0C00 ($B574); the planet's list counts by the size ($88B9: 9 / 3 / 2, $88DA: $11 / 5 / 2 below $80 / $100);
// `$21` raised.
void CtEndingPicThread(CtSeqStagePic* sp, uint8_t* w) {
  (void)sp;
  if (rw(w, 0x3024) != 0x8930) return;                                // (the call the setup's world load made)
  if (rw(w, 0x21)) return;                                            // 8930 LDA $21 / BNE
  if (rw(w, 0x7B)) { ww(w, 0x2B, 0x0800); ww(w, 0x25, 0x0040); }      // 8944
  else { ww(w, 0x25, 0x0020); ww(w, 0x2B, 0x0400); }                  // 8938
  unsigned a = rw(w, 0x0B00) & 0x01FF; ww(w, 0x27, a); ww(w, 0x56, a);   // 894E
  ww(w, 0x23, rw(w, 0x4E));
  ww(w, 0x2D, 0x0200); ww(w, 0x2F, 0x0200);                           // 8960
  ww(w, 0xF2, rw(w, 0x27));
  div32E2(w, 0x0800, &s_threadC);                                     // 896B LDA #0800 / JSR $32E2
  ww(w, 0x4C, rw(w, 0xF4));
  a = rw(w, 0x27); int c = (int)(a >> 15); ww(w, 0x29, (a << 1) & 0xFFFF);   // 8977 ASL A
  ww(w, 0x1F, 0x0004);
  CtSeqPicScale0307(w, c);                                            // 897F JSL C3/0307
  CtSeqPicRun09E9(w, 0x0C00, 0x0200);                                 // 8984 JSR $B574
  unsigned x = rw(w, 0x56);
  if (!w[0x7B]) w[0x88B9] = x < 0x80 ? 0x09 : x < 0x100 ? 0x03 : 0x02;   // 8991 CPX #0080 / BCC; CPX #0100 / BCC
  else w[0x88DA] = x < 0x80 ? 0x11 : x < 0x100 ? 0x05 : 0x02;
  s_threadC = x >= 0x0100;                                            // the last CPX's carry (below $80 the first one's: clear)
  ww(w, 0x21, rw(w, 0x21) + 1);                                       // 89BB INC $21
}
void CtEndingPicReset(void) { s_threadC = 0; }
void CtEndingPicListA03D(uint8_t* w, unsigned list) { listA03D(w, list); }
