// seq_entry7pic.c -- sequence entry 7's picture (its logic is seq_entry7.c): the Day of Lavos after
// the lost final battle -- the planet in mode 3, the sparkles and the text "BUT... THE FUTURE REFUSED TO CHANGE.".
//
// The setup $7E:6248: packet $63's 2bpp characters widened to 4bpp through WMDATA ($66B0: two bytes a row, then eight zero words
// a tile) at VRAM $4000, packets $5D / $CF / $B1 to $4800 / $5000 / $7000, the palettes into WRAM (packets $54 / $57 / $5A / $60
// at $2020 / $B574 / $B774 / $21C0, three colours by $8772) and $38AF's copy of $7E:2000 into the CGRAM shadow $2200; after the
// script, the layers' registers. The hooks are the common three ($380B OAM, $3843, $388B CGRAM every frame). Task $64BF draws
// the text from the string $7E:6644 (its pen kept in two operands of its own code, $64D6 / $64DA); the sparkles are the logic's
// records drawn through C3/0DBC (seq_entry7.c calls CtEntry7PicSparkle). disasm/notes/seq_picture.md sec. 7.
#include "seq_entry7pic.h"
#include "seq_racepic.h"
#include <stdio.h>

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

// $7E:66B0 (D = $2100, X = the tiles): WMADD = `$0303` in the bank C3/0557 left ($7F); each tile's eight rows: two bytes read
// to $2118 / $2119, then eight zero words -- 2bpp widened to 4bpp.
void CtSeqPicWiden66B0(CtSeqStagePic* sp, const uint8_t* w, unsigned tiles) {
  for (unsigned t = 0, r = 0x10000 | rw(w, 0x0303); t < tiles; t++) {
    for (int k = 0; k < 8; k++) {                                     // 66BD LDA $80 / STA $18 / LDA $80 / STA $19
      sp->p.vram[sp->vaddr & 0x7FFF] = (uint16_t)(w[r & 0x1FFFF] | (w[(r + 1) & 0x1FFFF] << 8)); r += 2; sp->vaddr = (uint16_t)(sp->vaddr + 1); }
    for (int k = 0; k < 8; k++) { sp->p.vram[sp->vaddr & 0x7FFF] = 0; sp->vaddr = (uint16_t)(sp->vaddr + 1); }
  }
}
// $7E:6248-62EB (8-bit A) -- the setup before the script.
void CtEntry7PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  sp->vmain = 0x80;                                                   // 6248
  ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                       // 624D $7E:89ED: the destination $7F:0200
  sp->vaddr = 0x4000;
  CtSeqPicUnpack(w, io, 0x63);                                        // 6256: packet $63 -> $7F:0200
  CtSeqPicWiden66B0(sp, w, 0x80);                                     // 6262 LDX #0080 / JSR $66B0
  sp->vaddr = 0x4800;
  CtSeqPicUnpack(w, io, 0x5D);                                        // 6268
  w[0x00F0] = 0x01; w[0x00F2] = 0x18;
  CtSeqPicDma38E7(sp, w);
  CtSeqPicUnpack(w, io, 0xCF);                                        // 627C
  sp->vaddr = 0x5000; CtSeqPicDma38E7(sp, w);
  sp->vaddr = 0x7000;
  CtSeqPicUnpack(w, io, 0xB1); CtSeqPicDma38E7(sp, w);                // 628C
  w[0x0305] = 0; w[0x00F1] = 0;                                       // 629C
  ww(w, 0x0303, 0x2020); CtSeqPicUnpack(w, io, 0x54);                 // 62A1: the palettes, into WRAM
  ww(w, 0x0303, 0xB574); CtSeqPicUnpack(w, io, 0x57);
  ww(w, 0x0303, 0xB774); CtSeqPicUnpack(w, io, 0x5A);
  ww(w, 0x21E2, 0x77BD); ww(w, 0x21E4, 0x4631); ww(w, 0x21E6, 0x1CE7); ww(w, 0x2000, 0x0000);   // 62C5 $7E:8772
  ww(w, 0x0303, 0x21C0); CtSeqPicUnpack(w, io, 0x60);                 // 62C8
  for (unsigned i = 0; i < 0x200; i++) w[0x2200 + i] = w[0x2000 + i];   // 62EB $7E:38AF: MVN $2000 -> $2200
}
// $7E:6301-6333 -- after the script: mode 3, BG1SC $70, BG12NBA $05, BG1 H / V = $FFB8 / $FFCC, CGWSEL / CGADSUB 02 / 01,
// TM / TS 01 / 10, TMW / TSW 1F / 1F.
void CtEntry7PicSetupB(CtSeqStagePic* sp) {
  CtSeqPicWrite(&sp->p, 0x05, 0x03, 0);
  CtSeqPicWrite(&sp->p, 0x07, 0x70, 0);
  CtSeqPicWrite(&sp->p, 0x0B, 0x05, 0);
  CtSeqPicWrite(&sp->p, 0x0D, 0xB8, 0); CtSeqPicWrite(&sp->p, 0x0D, 0xFF, 0);
  CtSeqPicWrite(&sp->p, 0x0E, 0xCC, 0); CtSeqPicWrite(&sp->p, 0x0E, 0xFF, 0);
  CtSeqPicWrite(&sp->p, 0x30, 0x02, 0); CtSeqPicWrite(&sp->p, 0x31, 0x01, 0);
  CtSeqPicWrite(&sp->p, 0x2C, 0x01, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x10, 0);
  CtSeqPicWrite(&sp->p, 0x2E, 0x1F, 0); CtSeqPicWrite(&sp->p, 0x2F, 0x1F, 0);
}
unsigned CtEntry7PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  { static unsigned said = 0; if (said != hook) { said = hook; fprintf(stderr, "entry7pic: UNDERIVED vblank hook $%04X\n", hook); } }
  return hook;
}
// $7E:6629 -- a live sparkle: C3/0DBC from the record $7E:6640 (its first byte the frame seq_entry7.c just wrote).
void CtEntry7PicSparkle(uint8_t* w) { CtSeqPicOam0DBC(w, 0x6640); }
// $7E:64BF -> 64CA -- the text (8-bit A, the data bank $7E): from the string at X, the pen ($15 / $17) starting where the code's
// own operands at $64D6 / $64DA say; `40` ends it (the OAM cursor back to `$11`), `00 x y` moves the pen (and rewrites both
// operands), `2F` a new line ($15 = the line's x, $17 += $10), `20` a space ($15 += 8). A character c: its tile from $7E:664F + c,
// its attribute the operand at $6525 (6524 `LDA #imm`), at the pen; its width w from $7E:662F + c -- from 9 up a large sprite
// (16 x 16: the high table's `SEC / ROR`), else two small ones, the second 8 lower with the tile + $10 (`CLC / ROR`); the pen
// moves w + 1 (`SEC / ADC $15`).
void CtSeqPicText64CA(uint8_t* w, unsigned x) {
  unsigned y = rw(w, 0x11);
  w[0x15] = w[0x64D6]; w[0x17] = w[0x64DA];                           // 64D5 LDA #imm / STA $15, 64D9 LDA #imm / STA $17
  for (unsigned guard = 0; guard < 0x400; guard++) {
    uint8_t c = w[x & 0x1FFFF]; x++;
    if (c == 0x40) { ww(w, 0x11, y); return; }                         // 64E1 CMP #40 / BEQ -> 64C5 STY $11
    if (c == 0x00) {                                                   // 64E5: the pen moved, the operands rewritten
      w[0x64D6] = w[0x15] = w[x & 0x1FFFF]; x++;
      w[0x64DA] = w[0x17] = w[x & 0x1FFFF]; x++;
      continue;
    }
    if (c == 0x2F) { w[0x15] = w[0x64D6]; w[0x17] = (uint8_t)(w[0x17] + 0x10); continue; }
    if (c == 0x20) { w[0x15] = (uint8_t)(w[0x15] + 8); continue; }
    unsigned o = 0x0700 + y;
    w[(o + 2) & 0x1FFF] = w[(0x664F + c) & 0x1FFFF]; w[(o + 3) & 0x1FFF] = w[0x6525];   // 6524 LDA #imm (the operand: entry 7 $3E, the ending $3F)
    w[o & 0x1FFF] = w[0x15]; w[(o + 1) & 0x1FFF] = w[0x17];
    uint8_t wd = w[(0x662F + c) & 0x1FFFF];
    unsigned p = rw(w, 0x13);
    if (!((uint8_t)(wd - 9) & 0x80)) {                                // 6536 CMP #09 / BMI: a large sprite
      w[0x15] = (uint8_t)(w[0x15] + wd + 1);                          // 653A SEC / ADC $15
      w[p & 0x1FFF] = (uint8_t)(((w[p & 0x1FFF] >> 1) >> 1) | 0x80);  // 653F LSR / SEC / ROR
      y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) ww(w, 0x13, p + 1);
      continue;
    }
    w[p & 0x1FFF] = (uint8_t)(w[p & 0x1FFF] >> 2);                    // 6559 LSR / LSR: small
    y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) ww(w, 0x13, p + 1);
    o = 0x0700 + y;                                                   // 656E: the lower half
    w[o & 0x1FFF] = w[0x15]; w[(o + 1) & 0x1FFF] = (uint8_t)(w[0x17] + 8);
    w[(o + 2) & 0x1FFF] = (uint8_t)(w[(o - 2) & 0x1FFF] + 0x10); w[(o + 3) & 0x1FFF] = w[(o - 1) & 0x1FFF];
    w[0x15] = (uint8_t)(w[0x15] + wd + 1);                            // 658D SEC / ADC $15
    p = rw(w, 0x13);
    w[p & 0x1FFF] = (uint8_t)(w[p & 0x1FFF] >> 2);                    // 6592 LSR / CLC / ROR
    y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) ww(w, 0x13, p + 1);
  }
}
int CtEntry7PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t) {
  (void)sp;
  if (t != 0x64BF) return 0;
  CtSeqPicText64CA(w, 0x6644);
  return 1;
}
