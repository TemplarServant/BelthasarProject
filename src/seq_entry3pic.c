// seq_entry3pic.c -- sequence entry 3's picture (its logic is seq_entry3.c): the night panorama.
//
// The stage draws with what the field left in VRAM -- the tiles at $1000, BG2's maps at $6800 / $6A00, the field's BG1 rows at
// $6140 -- so its picture starts from the host's VRAM (a CtSeqStagePic seeded with it). Its own: the setup's two reads (the
// multiplier's product into $7E:1400, the field's BG1 rows back into $7E:1680) and their upload as BG1's map at $0000 ($783C),
// the HDMA channels ($7E:685E), the layers' registers; the hook $66DD (BG3's offset-per-tile row from $0043, BG3 H / BG1 V
// scrolls); the tasks $73C6 (the multiplier) and $6A23 (BG1's per-line scroll table at $7E:F000). The overlay's code is read
// from the WRAM it was decompressed into ($7E:3000.., tools/wdis.py). disasm/notes/seq_picture.md sec. 5.
#include "seq_entry3pic.h"
#include "seq_racepic.h"
#include <stdio.h>

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

// $7E:671F-6772 (8-bit A) -- before the script: M7A = $0100, M7B = 1 and a `B -> A` DMA of $280 bytes from $2134 / $2135
// in turn (mode 1: the product's low and middle bytes, 00 01) fills $7E:1400; $2116 = $6140, a dummy `LDA $213A` (VMAIN 0:
// no step), VMAIN $80 and $580 bytes from $2139 / $213A into $7E:1680 -- the field's BG1 rows; BG12NBA = $11.
void CtEntry3PicSetupA(CtSeqStagePic* sp, uint8_t* w) {
  CtSeqPicWrite(&sp->p, 0x1B, 0x00, 0); CtSeqPicWrite(&sp->p, 0x1B, 0x01, 0); CtSeqPicWrite(&sp->p, 0x1C, 0x01, 0);
  { long m = CtSeqPicMpy(sp); for (unsigned i = 0; i < 0x280; i++) w[0x1400 + i] = (uint8_t)(m >> (8 * (i & 1))); }   // DMAP 81: $2134 / $2135 in turn
  sp->vaddr = 0x6140;                                                 // 674A STX $2116 (the read buffer loaded)
  sp->vmain = 0x80;                                                   // 6768 STA $2115
  CtSeqStagePicVramReadWords(sp, w, 0x1680, 0x580);                   // 6753-676D DMAP 81, BBAD 39
  CtSeqPicWrite(&sp->p, 0x0B, 0x11, 0);
}
// $7E:679F-6805 -- after the script: $783C (with `$3B` raised) sends $7E:1400, $800 bytes, to VRAM $0000 (VMAIN $80): BG1's map;
// $36CC installs the HDMA channels from $7E:685E; W12SEL $33 (the attract's arm: $FF), BGMODE 2, BG1SC 00, BG3SC 70, TM 13,
// TS 12, CGWSEL 02, CGADSUB 01, TMW 1F, WH2 / WH3 = 08 / F8, WOBJSEL... $2129 = FF.
void CtEntry3PicSetupB(CtSeqStagePic* sp, uint8_t* w, uint8_t operand) {
  sp->vaddr = 0x0000; sp->vmain = 0x80;
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x1400, 0x800);
  CtSeqPicHdma36CC(sp, w, 0x685E);
  CtSeqPicWrite(&sp->p, 0x23, (operand & 0x40) ? 0xFF : 0x33, 0);
  CtSeqPicWrite(&sp->p, 0x05, 0x02, 0);
  CtSeqPicWrite(&sp->p, 0x07, 0x00, 0);
  CtSeqPicWrite(&sp->p, 0x09, 0x70, 0);
  CtSeqPicWrite(&sp->p, 0x2C, 0x13, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x12, 0);
  CtSeqPicWrite(&sp->p, 0x30, 0x02, 0); CtSeqPicWrite(&sp->p, 0x31, 0x01, 0);
  CtSeqPicWrite(&sp->p, 0x2E, 0x1F, 0);
  CtSeqPicWrite(&sp->p, 0x26, 0x08, 0); CtSeqPicWrite(&sp->p, 0x27, 0xF8, 0);
  CtSeqPicWrite(&sp->p, 0x29, 0xFF, 0);
}
// $7E:66DD -- BG3's offset-per-tile row: $0043, $80 bytes, to VRAM $7000 (VMAIN $80); BG3HOFS = `$3D`, BG1VOFS = page B's r1.
static void hook66DD(CtSeqStagePic* sp, uint8_t* w) {
  sp->vaddr = 0x7000; sp->vmain = 0x80;
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x0043, 0x80);
  CtSeqPicWrite(&sp->p, 0x11, w[0x003D], 0); CtSeqPicWrite(&sp->p, 0x11, w[0x003E], 0);
  CtSeqPicWrite(&sp->p, 0x0E, w[0x0B02], 0); CtSeqPicWrite(&sp->p, 0x0E, w[0x0B03], 0);
}
unsigned CtEntry3PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  if (CtSeqPicCommonHook(sp, w, hook)) return hook;
  if (hook == 0x66DD) { hook66DD(sp, w); return hook; }
  { static unsigned said = 0; if (said != hook) { said = hook; fprintf(stderr, "entry3pic: UNDERIVED vblank hook $%04X\n", hook); } }
  return hook;
}
// $7E:73C6 -- the sine and cosine of page A's r5 through the PPU's multiplier: |cos| into M7A (two bytes), r0's low byte / 4 into
// M7B (twice), the product's low word / 8 with the cosine's sign = $1F and $25; the same for the sine = $21, its negation $23;
// $2B = $27 = r1, $2D = $29 = r2.
static unsigned mul73C6(CtSeqStagePic* sp, const uint8_t* w, unsigned v) {
  int neg = (v & 0x8000) != 0;                                        // PHP after the sine: N = its sign
  unsigned a = neg ? ((v ^ 0xFFFF) + 1) & 0xFFFF : v;
  CtSeqPicWrite(&sp->p, 0x1B, (uint8_t)a, 0); CtSeqPicWrite(&sp->p, 0x1B, (uint8_t)(a >> 8), 0);
  uint8_t b = (uint8_t)(w[0x0A00] >> 2);
  CtSeqPicWrite(&sp->p, 0x1C, b, 0); CtSeqPicWrite(&sp->p, 0x1C, b, 0);
  unsigned p = ((unsigned)CtSeqPicMpy(sp) & 0xFFFF) >> 3;             // 73E9 LDA $2134 / LSR x3
  return neg ? ((p ^ 0xFFFF) + 1) & 0xFFFF : p;
}
static void task73C6(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  unsigned c = mul73C6(sp, w, CtSeqSine(io, rw(w, 0x0A0A) + 0x100));
  ww(w, 0x1F, c); ww(w, 0x25, c);
  unsigned s = mul73C6(sp, w, CtSeqSine(io, rw(w, 0x0A0A)));
  ww(w, 0x21, s); ww(w, 0x23, ((s ^ 0xFFFF) + 1) & 0xFFFF);
  ww(w, 0x2B, rw(w, 0x0A02)); ww(w, 0x27, rw(w, 0x0A02));
  ww(w, 0x2D, rw(w, 0x0A04)); ww(w, 0x29, rw(w, 0x0A04));
}
// $7E:6A23 -- BG1's per-line H-scroll table at $7E:F000 ($1C0 bytes, HDMA channel 1's): page B's r0 and 0 in turn, the order
// swapped every frame (`$08` bit 0).
static void task6A23(uint8_t* w) {
  unsigned r0 = rw(w, 0x0B00), odd = w[0x08] & 1;
  for (unsigned x = 0; x < 0x1C0; x += 4) { ww(w, 0xF000 + x, odd ? r0 : 0); ww(w, 0xF002 + x, odd ? 0 : r0); }
}
int CtEntry3PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x73C6: task73C6(sp, w, io); return 1;
    case 0x6A23: task6A23(w); return 1;
    default: return 0;
  }
}
