// seq_entry6pic.c -- SEQUENCE ENTRY 6's picture, native (stage 2; its logic, stage 1, is seq_entry6.c): the Epoch flies into
// Lavos's shell. Mode 7 (the shell's map, its per-line matrix built by the shared $357B), the Epoch scaled on the main thread
// (C3/0307 and the code C3/09E9 writes at $B574, as the race's bikes), the backdrop of mode 1, the sprites.
//
// Part 1 (2026-10-17): the setup $7E:78DD with $7A68, the hooks ($380B / $3843 / $388B shared, $81C4, $824A, $7F58 and the
// PPU halves of $7A35 / $79F8), the main thread $8278 and the task $82D1 ($357B with $8042 / $801F before its loop). Not yet:
// the tasks $7F7D, $7FCB (C3/0077's circle), $8079, $835B, $7BF2 -- their draws use C3/02DD with a 16-bit A, which reads $2138 and
// $213E. disasm/notes/seq_picture.md sec. 8.
#include "seq_entry6pic.h"
#include "romview.h"     // the ROM's tables from ct_assets.dat (CT_ASSET_TABLE)
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
    uint8_t s = io ? CtRomByte(io->rom, io->romLen, 0xF900 + 3 * k) : 0;
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
// ---- part 2: the sprites ----
typedef struct { CtSeqStagePic* sp; uint8_t* w; unsigned a; int c; unsigned x; int dbr7e; } Cpu6;   // A (16 bits), the carry, X,
                                                                   //   the data bank $7E (else $00)
static unsigned adc16(Cpu6* k, unsigned a, unsigned v) { unsigned s = (a & 0xFFFF) + (v & 0xFFFF) + (unsigned)k->c; k->c = s > 0xFFFF; return s & 0xFFFF; }
static unsigned sbc16(Cpu6* k, unsigned a, unsigned v) { int s = (int)(a & 0xFFFF) - (int)(v & 0xFFFF) - (1 - k->c); k->c = s >= 0; return (unsigned)s & 0xFFFF; }
// $7E:3443 -> C3/02DD WITH A 16 BITS WIDE: every operand a word. `CMP $2137` reads the latch (the CPU's open bus: the operand's
// last byte, $21) and $2138 (OAM data at the OAM address -- the compositor's, which the read advances); `ADC $213C` the rule's
// virtual beam ($0351, then $213D's 0); `SBC $213D` its 0 and $213E (STAT77: 1, the PPU1 open bus' bit 4, last frame's range /
// time over). The carry threaded as the 8-bit routine's (seq_entry4.c rng02DD).
// ⚠ ITS OPERANDS ARE ABSOLUTE, IN THE CALLER's DATA BANK: under $7E (a task that did `PHK / PLB`, as $8079) `$2137`, `$213C`,
// `$213D` are WRAM's bytes $7E:2137.., not the PPU's -- the beam, the latch and OAM untouched.
static unsigned rngW(Cpu6* k, unsigned adr, int word) { return word ? rw(k->w, adr) : k->w[adr & 0x1FFFF]; }
static void rng16(Cpu6* k) {
  uint8_t* w = k->w; unsigned a = k->a;
  int nc = (int)((a >> 15) & 1); a = ((a << 1) | (unsigned)k->c) & 0xFFFF; k->c = nc;   // 02DD ROL A
  a = adc16(k, a, rw(w, 0x0386)); ww(w, 0x0386, a);                   // ADC $0386 / STA $0386
  a = k->x & 0xFFFF;                                                  // TXA
  a = adc16(k, a, rw(w, 0x0386)); a = adc16(k, a, rw(w, 0x0008));
  { unsigned v = k->dbr7e ? rngW(k, 0x2137, 1) : (0x21 | ((unsigned)CtSeqPicReg(&k->sp->p, 0x38) << 8)); k->c = a >= v; }   // CMP $2137
  a = adc16(k, a, k->dbr7e ? rngW(k, 0x213C, 1) : w[0x0351]);         // ADC $213C
  a = sbc16(k, a, k->dbr7e ? rngW(k, 0x213D, 1) : (unsigned)CtSeqPicReg(&k->sp->p, 0x3E) << 8);   // SBC $213D
  a = adc16(k, a, rw(w, 0x0386)); a = adc16(k, a, rw(w, 0x00F0));
  a ^= rw(w, 0x00F2);
  a = adc16(k, a, rw(w, 0x00F4));
  a = sbc16(k, a, k->dbr7e ? rngW(k, 0x213D, 1) : (unsigned)CtSeqPicReg(&k->sp->p, 0x3E) << 8);   // SBC $213D
  ww(w, 0x0386, a); k->a = a;
}
// The same with an 8-bit A (`$7E:3443` from 8-bit code), in the data bank $7E: the three reads are WRAM's bytes.
static uint8_t rng8w(Cpu6* k, uint8_t a) {
  uint8_t* w = k->w; unsigned t;
  int nc = a >> 7; a = (uint8_t)((a << 1) | k->c); k->c = nc;          // ROL A
  t = a + w[0x0386] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF; w[0x0386] = a;
  a = (uint8_t)k->x;                                                  // TXA
  t = a + w[0x0386] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  t = a + w[0x0008] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  k->c = a >= w[0x2137];                                              // CMP $2137 (WRAM)
  t = a + w[0x213C] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  { int s = (int)a - w[0x213D] - (1 - k->c); k->c = s >= 0; a = (uint8_t)s; }
  t = a + w[0x0386] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  t = a + w[0x00F0] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  a ^= w[0x00F2];
  t = a + w[0x00F4] + (unsigned)k->c; a = (uint8_t)t; k->c = t > 0xFF;
  { int s = (int)a - w[0x213D] - (1 - k->c); k->c = s >= 0; a = (uint8_t)s; }
  w[0x0386] = a;
  return a;
}
// $7E:3A97 -- A LIST OF SPRITES through WMDATA from $7E:A: a count n + 1, then n x (tile, attr -- its bit 4 the size, cleared in
// OAM --, x, y), x and y signed bytes added to the words `$15` / `$17`. x < 0: its low byte and bit 8 set in the high table; x
// from $100: the y byte skipped and y = $E0 (x's byte not written); y negative or from $E0: $E0. A and the carry it leaves are
// the next caller's (A = the last word's high byte : the OAM index's low nibble).
static void draw3A97(Cpu6* k, unsigned list) {
  uint8_t* w = k->w;
  unsigned wm = list, y = rw(w, 0x11);
  uint8_t n = w[wm++ & 0x1FFFF]; w[0x19] = n;
  unsigned hi = 0x00, lo = n;                                         // 3A9C LDA #007E (B = 0), LDA $2180
  for (;;) {
    if (--w[0x19] == 0) break;                                        // 3AA9 DEC $19 / BEQ
    w[(0x0702 + y) & 0x1FFF] = w[wm++ & 0x1FFFF];
    uint8_t attr = w[wm++ & 0x1FFFF]; w[0x0A] = attr; w[(0x0703 + y) & 0x1FFF] = attr & 0xEF;
    uint8_t xb = w[wm++ & 0x1FFFF];
    unsigned v = (xb & 0x80) ? (0xFF00u | xb) : xb;                   // ORA #FF00 / BIT #0080 / AND #00FF
    k->c = 0; v = adc16(k, v, rw(w, 0x15));                           // CLC / ADC $15
    unsigned p = rw(w, 0x13) & 0x1FFF; uint8_t h = w[p];
    if (v & 0x8000) {                                                 // 3AD0 BMI: off the left edge, bit 8 set
      w[(0x0700 + y) & 0x1FFF] = (uint8_t)v;
      if (attr & 0x10) { h = (uint8_t)((h >> 1) | 0x80); h = (uint8_t)((h >> 1) | 0x80); k->c = 1; }   // SEC / ROR / SEC / ROR
      else { h = (uint8_t)((h >> 1) | 0x80); k->c = h & 1; h >>= 1; }                               // SEC / ROR / LSR
    } else {
      k->c = v >= 0x0100;                                             // 3AD2 CMP #0100 / BPL
      if (!((v - 0x0100) & 0x8000)) {                                 // 3B39: off the right edge
        k->c = (h >> 1) & 1; w[p] = (uint8_t)(h >> 2);                // LSR / LSR
        wm++;                                                         // 3B41 LDA $2180 (the y byte)
        w[(0x0701 + y) & 0x1FFF] = 0xE0; hi = (v >> 8) & 0xFF;        // 3B44 LDA #E0 -> 3B28
        y = (y + 4) & 0xFFFF; lo = y & 0x0F; if (!(y & 0x0F)) w[0x13]++;
        continue;
      }
      w[(0x0700 + y) & 0x1FFF] = (uint8_t)v;
      if (attr & 0x10) { h >>= 1; k->c = h & 1; h = (uint8_t)((h >> 1) | 0x80); }   // LSR / SEC / ROR
      else { k->c = (h >> 1) & 1; h >>= 2; }                                         // LSR / LSR
    }
    w[p] = h;                                                         // 3B07 STA ($13)
    uint8_t yb = w[wm++ & 0x1FFFF];
    unsigned u = (yb & 0x80) ? (0xFF00u | yb) : yb;
    k->c = 0; u = adc16(k, u, rw(w, 0x17));                           // 3B19 CLC / ADC $17
    if (u & 0x8000) u = 0x00E0;                                       // 3B1C BMI -> LDA #00E0
    else { k->c = u >= 0x00E0; if (!((u - 0x00E0) & 0x8000)) u = 0x00E0; }   // 3B1E CMP #00E0 / BMI
    w[(0x0701 + y) & 0x1FFF] = (uint8_t)u; hi = (u >> 8) & 0xFF;
    y = (y + 4) & 0xFFFF; lo = y & 0x0F; if (!(y & 0x0F)) w[0x13]++;  // 3B2B INY x 4 / TYA / AND #0F / INC $13
  }
  ww(w, 0x11, y);                                                     // 3A92 REP #20 / STY $11
  k->a = (hi << 8) | lo;
  (void)k->sp;
}
void CtSeqPicList3A97(uint8_t* w, unsigned list) { Cpu6 kk = { NULL, w, 0, 0, 0, 0 }; draw3A97(&kk, list); }
// $7E:7F7D -- the Epoch and its glow: x = page B's r1 + $80 (the carry the task enters with), y = $46 = the draw & `$4A` + $A8 (also
// page B's r5), the lists $8109 at y - $28, $819B at $46 + ($12 + the next draw & 1), $8122 at y - $28.
static void task7F7D(Cpu6* k) {
  uint8_t* w = k->w;
  unsigned a = adc16(k, rw(w, 0x0B02), 0x0080); ww(w, 0x15, a); k->a = a;
  rng16(k);
  a = k->a & rw(w, 0x4A); k->c = 0; a = adc16(k, a, 0x00A8);
  ww(w, 0x46, a); ww(w, 0x0B0A, a);
  k->c = 1; a = sbc16(k, a, 0x0028); ww(w, 0x17, a);
  draw3A97(k, 0x8109);
  rng16(k);
  a = k->a & 0x0001; a = adc16(k, a, rw(w, 0x46)); a = adc16(k, a, 0x0012); ww(w, 0x17, a);
  draw3A97(k, 0x819B);
  k->c = 1; a = sbc16(k, rw(w, 0x46), 0x0028); ww(w, 0x17, a);
  draw3A97(k, 0x8122);
}
// C3/0077 -- A CIRCLE INTO A WINDOW TABLE (D = $0300; A's low byte the mode, from the caller): the centre (`$50`, `$52`), the
// radius `$54` (their high bytes cleared), the table at `$56` in bank $7E / $7F by `$58` bit 0, two bytes a line (left, right).
// Radius 0: every line `01 00` (C3/01C6). Else the midpoint walk from (x = r, y = 0), d = 3 - 2r less the ASL's borrow, writes
// the lines cy - x and cy - y (left cx - y / cx - x clamped at 0, right cx + y / cx + x at $FF) while x >= y; then the lower
// half mirrored from the upper (mode 0: every line, r + 1 of them; mode 1: every second, (r + 1) / 2); unless the mode's bit 7,
// the lines below and above filled `01 00`. Every ADC / SBC on the carry as it stands.
static void circle0077(Cpu6* k, uint8_t mode) {
  uint8_t* w = k->w;
  const unsigned D = 0x0300;
  w[D + 0x51] = 0; w[D + 0x53] = 0; w[D + 0x55] = 0;                 // 3008B STZ $51 / $53 / $55
  unsigned bank = (w[D + 0x58] & 1) ? 0x10000 : 0;                    // 3008F LDA $58 / LSR / BCS
  #define TB(a) w[bank | ((a) & 0xFFFF)]
  unsigned flags = mode, a, x, y;
  unsigned cx = rw(w, D + 0x50), cy = rw(w, D + 0x52), r = rw(w, D + 0x54), tbl = rw(w, D + 0x56);
  if (!r) {                                                           // 300A0 BEQ: no circle -- every line empty
    ww(w, D + 0xF0, 0x01C0); x = tbl;
    TB(x) = 0x01; TB(x + 1) = 0x00;                                   // 301CD LDA #0001 / STA $0000,X
    for (unsigned i = 0; i <= 0x01C0; i++) TB(x + 2 + i) = TB(x + i); // MVN: $1C1 bytes forward
    return;
  }
  x = r;
  a = (r << 1) & 0xFFFF; k->c = (int)((r >> 15) & 1);                // 300AD ASL A
  ww(w, D + 0xF2, a); a = sbc16(k, 0x0003, a); ww(w, D + 0xF2, a);   // 300B0 LDA #0003 / SBC $F2
  y = 0;
  a = (cy << 1) & 0xFFFF; k->c = 0; a = adc16(k, a, tbl); ww(w, D + 0xF4, a);   // 300BA LDA $52 / ASL / CLC / ADC $56
  unsigned top = a;                                                   // 300C2 PHA
  k->c = (int)(a & 1); ww(w, D + 0xF6, a >> 1);                       // LSR A / STA $F6
  for (unsigned guard = 0; guard < 0x1000; guard++) {
    ww(w, D + 0xF0, x);                                               // 300C6 STX $F0
    a = sbc16(k, rw(w, D + 0xF6), x);                                 // LDA $F6 / SBC $F0
    k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF;               // ASL A
    unsigned xa = a;                                                  // TAX
    a = adc16(k, ((y ^ 0xFFFF) + 1) & 0xFFFF, cx);                    // TYA / EOR / INC / ADC $50
    if (a & 0x8000) a = 0;                                            // BPL
    TB(xa) = (uint8_t)a; TB(xa + 1) = (uint8_t)(a >> 8);              // STA $0000,X
    a = adc16(k, y, cx);                                              // TYA / ADC $50
    k->c = a >= 0x00FF; if (!((a - 0x00FF) & 0x8000)) a = 0x00FF;     // CMP #00FF / BMI
    TB(xa + 1) = (uint8_t)a;                                          // STA $0001,X
    xa = rw(w, D + 0xF0);                                             // LDX $F0 (x)
    ww(w, D + 0xF0, y);                                               // STY $F0
    unsigned ya = (rw(w, D + 0xF4) - 2) & 0xFFFF; ww(w, D + 0xF4, ya);   // LDY $F4 / DEY / DEY / STY $F4
    a = adc16(k, ((xa ^ 0xFFFF) + 1) & 0xFFFF, cx);                   // TXA / EOR / INC / ADC $50
    if (a & 0x8000) a = 0;
    TB(ya) = (uint8_t)a; TB(ya + 1) = (uint8_t)(a >> 8);
    k->c = 0; a = adc16(k, xa, cx);                                   // TXA / CLC / ADC $50
    k->c = a >= 0x00FF; if (!((a - 0x00FF) & 0x8000)) a = 0x00FF;
    TB(ya + 1) = (uint8_t)a;
    y = rw(w, D + 0xF0); x = xa;                                      // LDY $F0
    if (!(rw(w, D + 0xF2) & 0x8000)) {                                // 3011D LDA $F2 / BPL -> 30129
      ww(w, D + 0xF0, x); x = (x - 1) & 0xFFFF;                       // TYA / STX $F0 / DEX
      a = sbc16(k, y, rw(w, D + 0xF0));                               // SBC $F0
      k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF; k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF;   // ASL / ASL
      a = adc16(k, a, 0x000A);
    } else {
      a = y; k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF; k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF;   // TYA / ASL / ASL
      a = adc16(k, a, 0x0006);
    }
    a = adc16(k, a, rw(w, D + 0xF2)); ww(w, D + 0xF2, a);             // 30134 ADC $F2 / STA $F2
    ww(w, D + 0xF0, y); y = (y + 1) & 0xFFFF;                         // STY $F0 / INY
    k->c = x >= rw(w, D + 0xF0);                                      // 3013B CPX $F0 / BPL
    if ((x - rw(w, D + 0xF0)) & 0x8000) break;
  }
  y = top; x = y;                                                     // 3013F PLY / TYX
  ww(w, D + 0xF0, (r + 1) & 0xFFFF);                                  // LDA $54 / INC A / STA $F0
  ww(w, D + 0xF2, flags);                                             // PLA / STA $F2
  unsigned m = flags & 0x7F;
  if (m == 1) {                                                       // 30153: every second line
    unsigned n = rw(w, D + 0xF0); k->c = (int)(n & 1); n >>= 1; ww(w, D + 0xF0, n);
    if (!n || (n & 0x8000)) return;
    do { x = (x - 4) & 0xFFFF; TB(y) = TB(x); TB(y + 1) = TB(x + 1); y = (y + 2) & 0xFFFF; ww(w, D + 0xF0, (rw(w, D + 0xF0) - 1) & 0xFFFF); } while (rw(w, D + 0xF0));
    if (rw(w, D + 0xF2) & 0x80) return;
    a = r >> 1; k->c = (int)(r & 1);                                  // 30172 LDA $54 / LSR
  } else if (m == 0) {                                                // 30177: every line
    do { x = (x - 2) & 0xFFFF; TB(y) = TB(x); TB(y + 1) = TB(x + 1); y = (y + 2) & 0xFFFF; ww(w, D + 0xF0, (rw(w, D + 0xF0) - 1) & 0xFFFF); } while (rw(w, D + 0xF0));
    if (rw(w, D + 0xF2) & 0x80) return;
    a = r;
  } else return;
  k->c = 0; a = adc16(k, a, cy);                                      // 3018E CLC / ADC $52
  k->c = a >= 0x00DF;
  if ((a - 0x00DF) & 0x8000) {                                        // CMP #00DF / BPL: the lines below, empty
    ww(w, D + 0xF0, a);
    a = (a << 1) & 0xFFFF; k->c = 0; a = adc16(k, a, tbl); x = a;
    k->c = 1; a = sbc16(k, 0x00E0, rw(w, D + 0xF0)); a = (a - 1) & 0xFFFF; a = (a << 1) & 0xFFFF; a = (a - 1) & 0xFFFF;
    ww(w, D + 0xF0, a);
    TB(x) = 0x01; TB(x + 1) = 0x00;                                   // 301CA TXY / INY / INY / LDA #0001 / STA / MVN
    for (unsigned i = 0; i <= a; i++) TB(x + 2 + i) = TB(x + i);
  }
  k->c = 1; a = sbc16(k, cy, r);                                      // 301AB LDA $52 / SEC / SBC $54
  k->c = a >= 1;
  if (!((a - 1) & 0x8000) && a != 1) {                                // CMP #0001 / BMI / BEQ: the lines above, empty
    a = (a - 1) & 0xFFFF; a = (a << 1) & 0xFFFF; a = (a - 1) & 0xFFFF;
    ww(w, D + 0xF0, a); x = tbl;
    TB(x) = 0x01; TB(x + 1) = 0x00;
    for (unsigned i = 0; i <= a; i++) TB(x + 2 + i) = TB(x + i);
  }
  #undef TB
}
// $7E:7FCB (D = $0B00) -- THE WINDOW's CIRCLE: page B's r6 as the centre and the radius (`$0350` / `$0352` / `$0354`), C3/0077 in
// mode $80 (every line mirrored, no fills) into $7E:E060; the HDMA tables' bytes $7C5C / $7C66 = r2 - r6 - 1, $7C69 = r6 x 2,
// $7C5F = it | $80; then the (r6 + 1) x 2 lines from $E060 to $E0E0, each its left byte + r1 (clamped at 0) | $FF00.
static void task7FCB(Cpu6* k) {
  uint8_t* w = k->w;
  uint8_t r6 = w[0x0B0C];
  w[0x0354] = r6; w[0x0350] = r6; w[0x0352] = r6;
  circle0077(k, 0x80);                                                // 7FDE LDA #80 / JSL C3/0077 (its flags restored)
  k->c = 0;                                                           // 7FE6 CLC
  { unsigned t = (unsigned)w[0x0B04] - r6 - 1; w[0x7C5C] = w[0x7C66] = (uint8_t)t; k->c = (int)(t <= 0xFF && w[0x0B04] >= r6 + 1); }   // SBC $0C
  { unsigned t = (unsigned)r6 << 1; k->c = (int)(t >> 8); w[0x7C69] = (uint8_t)t; w[0x7C5F] = (uint8_t)(t | 0x80); }   // ASL / ORA #80
  unsigned a = (rw(w, 0x0B0C) + 1) & 0xFFFF; k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF;   // REP / LDA $0C / INC / ASL
  if (!a) return;
  for (unsigned x = 0, n = a; n; n--, x += 2) {                       // 8006 .. 801C
    unsigned v = adc16(k, w[0xE060 + x], rw(w, 0x0B02));             // LDA $E060,X / AND #00FF / ADC $02
    if (v & 0x8000) v = 0;
    ww(w, 0xE0E0 + x, v | 0xFF00);
  }
}
// $7E:80D3 -- a draw & 6 picks one of four tile / attr words at $80E1 (X = it), into `$F0`.
static void pick80D3(Cpu6* k) {
  uint8_t* w = k->w;
  rng16(k);
  unsigned a = k->a & 0x0006; k->x = a;
  a = rw(w, 0x80E1 + a); ww(w, 0x00F0, a); k->a = a;
}
// $7E:80A6 (8-bit A) -- two sprites of the lightning through WMDATA: an 8-bit draw & $18 picks four bytes at a time from $80E9
// (X), the third + `$F0` (x); called twice ($F0 = $38, then $C0), the four sprites go to $7E:8112 on.
static unsigned spark80A6(Cpu6* k, unsigned wm) {
  uint8_t* w = k->w;
  uint8_t a = rng8w(k, (uint8_t)k->a);                                // (the data bank $7E: 8079's PHK / PLB)
  a &= 0x18; k->x = a; k->a = (k->a & 0xFF00) | a;                    // AND #18 / TAX (B = 0)
  for (int s = 0; s < 2; s++) {                                       // 80AC JSR $80AF, then on into it
    unsigned x = k->x;
    w[wm++ & 0x1FFFF] = w[0x80E9 + x];
    w[wm++ & 0x1FFFF] = w[0x80EA + x];
    { unsigned t = (unsigned)w[0x80EB + x] + w[0x00F0]; k->c = t > 0xFF; w[wm++ & 0x1FFFF] = (uint8_t)t; }   // CLC / ADC $F0
    w[wm++ & 0x1FFFF] = w[0x80EC + x];
    k->x = (x + 4) & 0xFFFF; k->a = (k->a & 0xFF00) | w[0x80EC + x];
  }
  return wm;
}
// $7E:8079 -- THE LIGHTNING: the first two sprites' words of the list $8109 ($810A, $810E) from $80D3, then the next four through
// WMDATA from $8112 ($80A6 at x $38 and $C0). A enters as the task's address (the dispatcher's `LDA $0950,X`).
static void task8079(Cpu6* k) {
  uint8_t* w = k->w;
  k->dbr7e = 1;                                                       // 8079 PHK / PLB
  pick80D3(k); ww(w, 0x810A, k->a);
  pick80D3(k); ww(w, 0x810E, k->a);
  unsigned wm = 0x8112;                                               // 8089 $2181 = $8112, $2183 = $7E
  k->a = 0x0000; k->a = 0x007E;                                       // LDA #0000 / SEP / LDA #7E
  w[0x00F0] = 0x38; k->a = 0x0038; wm = spark80A6(k, wm);
  w[0x00F0] = 0xC0; k->a = (k->a & 0xFF00) | 0xC0; spark80A6(k, wm);
}
// $7E:835B (the data bank $7E) -- THE EXPLOSION's frames: `$4C` counts the current one's frames down; at 0 the pointer `$4E` steps 3
// through the table from $84F6 (a count byte, then the list's address; a 0 count starts it again) -- then the list drawn at
// page B's r1 / r2 through $3A97.
static void task835B(Cpu6* k) {
  uint8_t* w = k->w;
  unsigned c = (rw(w, 0x004C) - 1) & 0xFFFF; ww(w, 0x004C, c);       // 835F DEC $4C
  if (!c) {
    for (unsigned guard = 0; guard < 0x100; guard++) {
      ww(w, 0x004E, rw(w, 0x004E) + 2);                               // 8363 INC $4E / INC $4E
      unsigned n = w[rw(w, 0x004E)];                                  // LDA ($4E) / AND #00FF
      if (n) { ww(w, 0x004C, n); break; }
      ww(w, 0x004E, 0x84F6);                                          // 836E: the table again
    }
    ww(w, 0x004E, rw(w, 0x004E) + 1);                                 // 8377 INC $4E
  }
  ww(w, 0x15, rw(w, 0x0B02)); ww(w, 0x17, rw(w, 0x0B04));
  k->a = rw(w, rw(w, 0x004E));                                        // 8383 LDA ($4E)
  draw3A97(k, k->a);
}
// $7E:7BF2 (8-bit) -- the HDMA table's opening ($7E:7BE2..): while $7BE2 is not 2, it - 2, $7BE8 + 2, $7BEB + 1; then while
// $7BE6 is not $1E, it + 2, $7BE5 - 2, $7BE8 + 2, and channel 0's table address ($4302) = $7BE5.
static void task7BF2(Cpu6* k) {
  uint8_t* w = k->w;
  if (w[0x7BE2] != 0x02) { w[0x7BE2] -= 2; w[0x7BE8] += 2; w[0x7BEB]++; return; }
  if (w[0x7BE6] == 0x1E) return;
  w[0x7BE6] += 2; w[0x7BE5] -= 2; w[0x7BE8] += 2;
  k->sp->ch[0].aAdr = 0x7BE5;                                         // 7C28 LDA #7BE5 / STA $004302
}
int CtEntry6PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  (void)io;
  Cpu6 kk = { sp, w, 0, 0, slot }, *k = &kk;                          // the dispatcher: X = the slot, its CPX #0020 clears the carry
  if (slot == 0) { static int said; if (!said++) fprintf(stderr, "entry6pic: UNDERIVED -- a picture task in slot 0 (the carry the NMI leaves)\n"); }
  switch (t) {
    case 0x7F7D: task7F7D(k); return 1;
    case 0x7FCB: task7FCB(k); return 1;
    case 0x8079: k->a = 0x8079; task8079(k); return 1;
    case 0x835B: task835B(k); return 1;
    case 0x7BF2: task7BF2(k); return 1;
    default: return 0;
  }
}
