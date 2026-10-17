// seq_entry6pic.c -- SEQUENCE ENTRY 6's picture, native (stage 2; its logic, stage 1, is seq_entry6.c): the Epoch flies into
// Lavos's shell. Mode 7 (the shell's map, its per-line matrix built by the shared $357B), the Epoch scaled on the main thread
// (C3/0307 and the code C3/09E9 writes at $B574, as the race's bikes), the backdrop of mode 1, the sprites.
//
// Part 1 (2026-10-17): the setup $7E:78DD with $7A68, the hooks ($380B / $3843 / $388B shared, $81C4, $824A, $7F58 and the
// PPU halves of $7A35 / $79F8), the main thread $8278 and the task $82D1 ($357B with $8042 / $801F before its loop). Not yet:
// the tasks $7F7D, $7FCB (C3/0077's circle), $8079, $835B, $7BF2 -- their draws use C3/02DD with a 16-bit A, which reads $2138 and
// $213E. disasm/notes/seq_picture.md sec. 8.
#include "seq_entry6pic.h"
#include "seq_racepic.h"
#include <stdio.h>

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

// $7E:89ED -- the packets' destination: `$0303-$0305` = $7F:0200 (two 16-bit stores: $0304 = $7F00, then $0303 = $0200).
static void dest89ED(uint8_t* w) { ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200); }
// $7E:7A68 -- the window's tables and the backdrop: `$37` = $0A00, the circle's table pointer `$0356-$0358` = $7E:E060, `$4C` = 1,
// `$4E` = $84F6; $E0 bytes of $FF - sin / 2 (C0:F900 by 3) at $7E:E260 through WMDATA; $E0..FF at $E000, $00, FF..E1 at $E030; packet
// $72 to VRAM $4400, $7B to $5000, $75 read through WMDATA as the backdrop map's attributes at $6000 (7 rows of 32: tiles $90 +
// $10 a row, 16 up and 16 back), six tiles patched at $6056 / $6076; then $7F:0200, $800 bytes, from the product ($2134, B -> A).
static void setup7A68(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  ww(w, 0x0037, 0x0A00);
  ww(w, 0x0356, 0xE060); w[0x0358] = 0x7E;
  ww(w, 0x004C, 0x0001); ww(w, 0x004E, 0x84F6);
  uint8_t a = 0;
  for (unsigned k = 0; k < 0xE0; k++) {                               // 7A98 .. 7AAC
    uint8_t s = (io && io->rom && io->romLen > 0xF900 + 3 * k) ? io->rom[0xF900 + 3 * k] : 0;
    a = (uint8_t)(s >> 1); w[0x00F0] = a;
    w[0xE260 + k] = (uint8_t)(0xFF - a);
  }
  w[0x00F4] = 0;                                                      // (7AAA DEC $F4 to 0)
  a = 0xE0;
  for (unsigned k = 0; k < 0x20; k++) { w[0xE000 + k] = a; a++; }     // 7AB6
  for (unsigned k = 0; k < 0x20; k++) { w[0xE030 + k] = a; a--; }     // 7AC6
  dest89ED(w);
  CtSeqPicUnpack(w, io, 0x72);
  sp->vmain = 0x80; sp->vaddr = 0x4400; w[0x00F0] = 0x01; w[0x00F2] = 0x18;
  CtSeqPicDma38E7(sp, w);
  CtSeqPicUnpack(w, io, 0x7B);
  sp->vaddr = 0x5000; CtSeqPicDma38E7(sp, w);
  CtSeqPicUnpack(w, io, 0x75);
  sp->vaddr = 0x6000;
  unsigned wm = 0x10200;                                              // 7B16 STX $2181 (the bank C3/0557 left: $7F)
  uint8_t f0 = 0x90;
  for (int r = 0; r < 7; r++) {
    for (int k = 0; k < 0x10; k++) { sp->p.vram[sp->vaddr & 0x7FFF] = (uint16_t)(f0 | (w[wm++ & 0x1FFFF] << 8)); sp->vaddr++; f0++; }
    for (int k = 0; k < 0x10; k++) { f0--; sp->p.vram[sp->vaddr & 0x7FFF] = (uint16_t)(f0 | (w[wm++ & 0x1FFFF] << 8)); sp->vaddr++; }
    f0 = (uint8_t)(f0 + 0x10);
  }
  w[0x00F0] = f0;
  sp->vmain = 0x00;                                                   // 7B53: six low bytes
  static const uint16_t at[2] = { 0x6056, 0x6076 }; static const uint8_t v0[2] = { 0xB7, 0xC7 };
  for (int k = 0; k < 2; k++) for (int i = 0; i < 3; i++) { unsigned v = at[k] + (unsigned)i; sp->p.vram[v & 0x7FFF] = (uint16_t)((sp->p.vram[v & 0x7FFF] & 0xFF00) | (uint8_t)(v0[k] + i)); }
  sp->vaddr = (uint16_t)(0x6076 + 3);
  { long m = CtSeqPicMpy(sp); for (unsigned i = 0; i < 0x800; i++) w[0x10200 + i] = (uint8_t)m; }   // 7B7E: DMAP $80 from $2134
}
// $7E:78DD-79A1 -- the setup's uploads before the script (the logic's FD/FFEE call sits between them: no overlap).
void CtEntry6PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  sp->vaddr = 0x2000; sp->vmain = 0x80;                               // 78E1
  dest89ED(w);
  CtSeqPicUnpack(w, io, 0x66);                                        // 78EF: the mode-7 characters, VRAM's high bytes
  w[0x00F0] = 0; w[0x00F2] = 0x19;
  CtSeqPicDma38E7(sp, w);
  CtSeqPicUnpack(w, io, 0x69);                                        // 7902: the map, its rows doubled ($37A6)
  CtSeqPicRows37A6(w);
  sp->vaddr = 0x0000; sp->vmain = 0x00;                               // 790C
  w[0x00F0] = 0; w[0x00F2] = 0x18; ww(w, 0x00F4, 0x2200); w[0x00F6] = 0x7F; ww(w, 0x00F8, 0x2000);
  CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x2000);               // 792F JSL $7E:38C3, twice
  CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x2000);
  w[0x0305] = 0x7E; ww(w, 0x0303, 0x2020);
  CtSeqPicUnpack(w, io, 0x6C);                                        // 7942: the palette page
  for (unsigned i = 0; i < 0x40; i++) w[0x2140 + i] = w[0x20A0 + i];  // 794B MVN $7E,$7E
  ww(w, 0x00F0, 0x0020);
  CtSeqPicCopy36AA(w, io, 0x6F, 0x2180);                              // 795D
  ww(w, 0x2000, 0x6F69);                                              // 7966
  for (unsigned i = 0; i < 0x200; i++) w[0x2200 + i] = w[0x2000 + i];   // 796F $38AF
  setup7A68(sp, w, io);                                               // 7976
  w[0x0305] = 0x7F; ww(w, 0x0303, 0x0A00);
  CtSeqPicUnpack(w, io, 0x78);                                        // 7984: the Epoch's frames, $7F:0A00
  CtSeqPicGen09E9(w, 0xB574, 0x0C00, 0x0200);                         // 798C: WMADD $7E:B574, X = $0C00, Y = $0200
}
// $7E:79D0-79F7 (D = $2100) -- TM / TS / TMW 13, COLDATA $E0, SETINI $41, W12SEL / WOBJSEL 22, BG1SC $60, BG12NBA $04.
void CtEntry6PicSetupB(CtSeqStagePic* sp) {
  CtSeqPicWrite(&sp->p, 0x2C, 0x13, 0); CtSeqPicWrite(&sp->p, 0x2E, 0x13, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x13, 0);
  CtSeqPicWrite(&sp->p, 0x32, 0xE0, 0); CtSeqPicWrite(&sp->p, 0x33, 0x41, 0);
  CtSeqPicWrite(&sp->p, 0x23, 0x22, 0); CtSeqPicWrite(&sp->p, 0x25, 0x22, 0);
  CtSeqPicWrite(&sp->p, 0x07, 0x60, 0); CtSeqPicWrite(&sp->p, 0x0B, 0x04, 0);
}
// $7E:8278 -- THE MAIN THREAD's pass (the loop's `JSR ($3024)`, after each NMI): with `$21` clear, the Epoch's frame scaled from
// $7F:0A00 (C3/0307: `$1F` = 4 groups, `$25` = $21 rows, `$2B` = $400, `$29` = `$27` x 2 -- the ASL's carry into C3/0307) and
// reformatted at $0C00 ($B574), `$21` = 1; hook $824A sends it and clears `$21`.
void CtEntry6PicThread(uint8_t* w) {
  if (rw(w, 0x0021)) return;                                          // 8278 LDA $21 / BNE
  ww(w, 0x0023, 0x0A00); ww(w, 0x002D, 0x0200); ww(w, 0x002F, 0x0200);
  unsigned a = rw(w, 0x0027); int c = (int)((a >> 15) & 1); ww(w, 0x0029, (a << 1) & 0xFFFF);
  ww(w, 0x0025, 0x0021); ww(w, 0x001F, 0x0004); ww(w, 0x002B, 0x0400);
  CtSeqPicScale0307(w, c);
  CtSeqPicRun09E9(w, 0x0C00, 0x0200);                                 // 82A5 JSR $B574
  ww(w, 0x0021, 0x0001);
}
// $7E:8042 -- $357B's pre-routine during the dive: `$0044` + 1; odd -- the tail `DEX / DEX`, page A's r7 + 1, the limit $01C0,
// X = $FF, Y = 0; even -- `INX / INX`, the limit `$46` x 4, X = $20, Y = $200 (the table built in two halves, a frame each).
static void pre8042(uint8_t* w, unsigned* x, unsigned* y, unsigned* wm) {
  (void)wm;
  unsigned n = (rw(w, 0x0044) + 1) & 0xFFFF; ww(w, 0x0044, n);
  if (n & 1) { ww(w, 0x3634, 0xCACA); ww(w, 0x0A0E, rw(w, 0x0A0E) + 1); ww(w, 0x3637, 0x01C0); *x = 0x00FF; *y = 0x0000; }
  else { ww(w, 0x3634, 0xE8E8); ww(w, 0x3637, (rw(w, 0x0046) << 2) & 0xFFFF); *x = 0x0020; *y = 0x0200; }
}
// $7E:801F -- after the turn: the limit $0318, Y = $68, X read through WMDATA from $7E:E260 (7A68's table; the tail $3634, which
// $79F8 copied from $368D, reads the next one each line).
static void pre801F(uint8_t* w, unsigned* x, unsigned* y, unsigned* wm) {
  ww(w, 0x3637, 0x0318); *y = 0x0068;
  *x = w[0xE260]; *wm = 0xE261;
}
void CtEntry6Pic357B(uint8_t* w, const CtRaceIo* io) {
  unsigned p = rw(w, 0x35FD);
  if (p == 0x8042) CtSeqPic357B(w, io, pre8042);
  else if (p == 0x801F) CtSeqPic357B(w, io, pre801F);
  else { static unsigned said; if (said != p) { said = p; fprintf(stderr, "entry6pic: UNDERIVED $357B pre-routine $%04X\n", p); } }
}
// $7E:7F58 -- the HDMA tables' bytes from `$46` and page B's r1: $7C5A = $FF - `$46`, $7C44 / $7C52 / $7C36 = `$46` - $A7, $7C58 =
// $0100 - r1 (its SBC on the carry the last one left).
static void hook7F58(uint8_t* w) {
  w[0x7C5A] = (uint8_t)(0xFF - w[0x46]);
  unsigned d = (unsigned)w[0x46] - 0xA7; int c = w[0x46] >= 0xA7;
  w[0x7C44] = w[0x7C52] = w[0x7C36] = (uint8_t)d;
  ww(w, 0x7C58, (0x0100 - rw(w, 0x0B02) - (c ? 0 : 1)) & 0xFFFF);
}
unsigned CtEntry6PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  switch (hook) {
    case 0x81C4: {                                                    // page A's BG1 scrolls and the mode-7 centre
      static const uint8_t reg[4] = { 0x0D, 0x0E, 0x1F, 0x20 };
      for (int k = 0; k < 4; k++) { CtSeqPicWrite(&sp->p, reg[k], w[0x0A02 + 2 * k], 0); CtSeqPicWrite(&sp->p, reg[k], w[0x0A03 + 2 * k], 0); }
      return hook;
    }
    case 0x824A:                                                      // the Epoch's frame: $0C00, $400 bytes, to VRAM $4000
      if (w[0x21]) { sp->vaddr = 0x4000; sp->vmain = 0x80; CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x0C00, 0x400); w[0x21] = 0; }
      return hook;
    case 0x7F58: hook7F58(w); return hook;
    case 0x7A35:                                                      // the dive: COLDATA $E0, TS / TMW 0, mode 1, TM $11, HDMA
      CtSeqPicWrite(&sp->p, 0x32, 0xE0, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x00, 0); CtSeqPicWrite(&sp->p, 0x2E, 0x00, 0);
      CtSeqPicWrite(&sp->p, 0x05, 0x01, 0); CtSeqPicWrite(&sp->p, 0x2C, 0x11, 0);
      CtSeqPicHdma36CC(sp, w, 0x7BB6);
      return hook;
    case 0x79F8:                                                      // the turn: mode 7, COLDATA $E0, HDMA
      CtSeqPicWrite(&sp->p, 0x05, 0x07, 0); CtSeqPicWrite(&sp->p, 0x32, 0xE0, 0);
      CtSeqPicHdma36CC(sp, w, 0x7B9C);
      return hook;
  }
  { static unsigned said = 0; if (said != hook) { said = hook; fprintf(stderr, "entry6pic: UNDERIVED vblank hook $%04X\n", hook); } }
  return hook;
}
int CtEntry6PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t, const CtRaceIo* io) {
  (void)sp; (void)w; (void)io;
  switch (t) {
    default: return 0;                                                // 7F7D 7FCB 8079 835B 7BF2: part 2
  }
}
