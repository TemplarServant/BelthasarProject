// seq_racepic.c -- THE MOTORCYCLE RACE's picture, native (stage 2 of the race's port; stage 1, its logic, is seq_race.c).
//
// What the race's code puts in the PPU, done by the same code natively over the stage's WRAM and its CtSeqStagePic: the setup's
// uploads ($7E:3D8C and $7E:3B5D-3C4E: the packets, the copies, the reformatting streams, the VRAM loads, the HDMA channels;
// $7E:3CEA-3D44: the layers' registers, the VRAM read back), the CGRAM upload $7E:388B, the vblank hooks and the picture's tasks.
// The overlay's code is read from the WRAM image it was decompressed into ($7E:3000.., `tools/wdis.py`).
// disasm/notes/seq_picture.md sec. 4.
#include "seq_racepic.h"
#include <string.h>
#include <stdio.h>
#include "lz.h"

static unsigned rw(const uint8_t* w, unsigned a) { return (unsigned)(w[a & 0x1FFFF] | (w[(a + 1) & 0x1FFFF] << 8)); }
static void ww(uint8_t* w, unsigned a, unsigned v) { w[a & 0x1FFFF] = (uint8_t)v; w[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }

// $7E:3691 -- a packet of the overlay's table $FE:0000,X (three bytes: its address) into the destination `$0303-$0305`, through
// C3/0557: `$0300-$0302` = the source, the output streamed through WMADD (`$0303`, bank `$0305` bit 0), `$0306` = its length.
static void unpack(uint8_t* w, const CtRaceIo* io, unsigned x) {
  static uint8_t out[0x10000];
  if (!io || !io->rom) return;
  long t = 0x3E0000L + (long)x;                                       // $FE:0000 + X (HiROM: file 0x3E0000)
  if (t + 3 > io->romLen) return;
  w[0x0300] = io->rom[t]; w[0x0301] = io->rom[t + 1]; w[0x0302] = io->rom[t + 2];
  unsigned src = (unsigned)(io->rom[t] | (io->rom[t + 1] << 8) | (io->rom[t + 2] << 16));
  size_t n = ct_lz_decompress_bounded(io->rom, (size_t)io->romLen, out, sizeof out, ct_lz_hirom_offset(src));
  unsigned d = rw(w, 0x0303), b = (unsigned)(w[0x0305] & 1) << 16;
  for (size_t i = 0; i < n; i++) w[b | ((d + (unsigned)i) & 0xFFFF)] = out[i];   // (WMADD's 17 bits: a bank's end runs on)
  ww(w, 0x0306, (unsigned)n);                                         // 08A6 TYA / SEC / SBC $03 / STA $06
}
// $7E:38E7 -- a DMA with `$F0` / `$F2` as DMAP / BBAD from `$0303-$0305`, `$0306` bytes.
static void dma38E7(CtSeqStagePic* sp, const uint8_t* w) {
  unsigned src = ((unsigned)(w[0x0305] & 1) << 16) | rw(w, 0x0303);
  CtSeqStagePicDma(sp, w, w[0x00F0], w[0x00F2], src, rw(w, 0x0306) ? rw(w, 0x0306) : 0x10000);
}
// The multiplier's product low byte ($2134), what a `B -> A` DMA from it fills WRAM with: M7A x the high byte of M7B.
static uint8_t mpyl(const CtSeqStagePic* sp) { return (uint8_t)(((int)sp->p.m7[0] * (int8_t)(sp->p.m7[1] >> 8)) & 0xFF); }
static void fill(uint8_t* w, unsigned dst, unsigned n, uint8_t v) { for (unsigned i = 0; i < n; i++) w[(dst + i) & 0x1FFFF] = v; }
static void mvn(uint8_t* w, unsigned dst, unsigned src, unsigned n) {   // MVN within one bank pair: X / Y wrap in their banks
  for (unsigned i = 0; i < n; i++)
    w[(dst & 0x10000) | ((dst + i) & 0xFFFF)] = w[(src & 0x10000) | ((src + i) & 0xFFFF)];
}
// $7E:37A6 -- the mode-7 map's rows doubled: 64 rows of 64 bytes from $7F:0200, each written twice at $7F:2200 + row x 128.
static void rows37A6(uint8_t* w) {
  unsigned x = 0x10200, y = 0x12200;
  for (int r = 0; r < 0x40; r++) { mvn(w, y, x, 0x40); mvn(w, y + 0x40, x, 0x40); x += 0x40; y += 0x80; }
}
// $7E:8752 -- `STZ $2115`, the low bytes of VRAM from $7F:2200, $2000 bytes (DMAP 00 to $2118).
static void dma8752(CtSeqStagePic* sp, const uint8_t* w) { sp->vmain = 0; CtSeqStagePicDma(sp, w, 0x00, 0x18, 0x12200, 0x2000); }
// $7E:3EB7 -- eight rows of 8 bytes from X into a 64-byte stride at Y (MVN $7F,$7F); X runs on, returned.
static unsigned rows3EB7(uint8_t* w, unsigned x, unsigned y) {
  for (int k = 0; k < 8; k++) { mvn(w, 0x10000 | y, 0x10000 | x, 8); x = (x + 8) & 0xFFFF; y = (y + 8 + 0x38) & 0xFFFF; }
  return x;
}
// $7E:3ED3 -- eight rows through WMDATA: each the 32 bytes from $7F:(Y + $0200) down (a mirror), then 32 zeros; Y += $40.
static unsigned stream3ED3(uint8_t* w, unsigned* wm, unsigned y) {
  for (int k = 0; k < 8; k++) {
    unsigned x = (y + rw(w, 0x0200)) & 0xFFFF;
    for (int i = 0; i < 0x20; i++) { w[*wm & 0x1FFFF] = w[0x10000 | x]; *wm = (*wm + 1) & 0x1FFFF; x = (x - 1) & 0xFFFF; }
    for (int i = 0; i < 0x20; i++) { w[*wm & 0x1FFFF] = 0; *wm = (*wm + 1) & 0x1FFFF; }
    y = (y + 0x40) & 0xFFFF;
  }
  return y;
}
static unsigned stream3ECD(uint8_t* w, unsigned* wm, unsigned y) { y = stream3ED3(w, wm, y); y = stream3ED3(w, wm, y); return stream3ED3(w, wm, y); }

// $7E:3D8C -- the race's VRAM: OBJ $4000 cleared to the byte at $7E:350B, the palette packet 0F at $7E:2000 copied to $2200, the
// mode-7 characters (packet 15, the high bytes of words 0..) and its map (packets 18 and 1B, rows doubled, the low bytes), the
// sky's characters and its HUD (packets 27, 2A, 12 at words $7000, $7100, $5000), $7E:E100 cleared, $7F:0300 filled from the
// multiplier, the three BG maps (packets 1E, 24, 21 at $6000, $6800, $6400).
static void setup3D8C(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  sp->vmain = 0x80; sp->vaddr = 0x4000;
  CtSeqStagePicDma(sp, w, 0x09, 0x18, 0x350B, 0x1000);
  w[0x0305] = 0; ww(w, 0x0303, 0x2000); unpack(w, io, 0x0F);
  mvn(w, 0x2200, 0x2000, 0x200);                                       // 38AF
  ww(w, 0x0304, 0x7F00); ww(w, 0x0303, 0x0200);                       // 89ED: $7F:0200
  sp->vaddr = 0x0000; unpack(w, io, 0x15);
  w[0x00F0] = 0; w[0x00F2] = 0x19; dma38E7(sp, w);
  sp->vaddr = 0x0000; unpack(w, io, 0x18); rows37A6(w); dma8752(sp, w);
  unpack(w, io, 0x1B); rows37A6(w); dma8752(sp, w);
  sp->vmain = 0x80; w[0x00F0] = 0x01; w[0x00F2] = 0x18;
  sp->vaddr = 0x7000; unpack(w, io, 0x27); dma38E7(sp, w);
  sp->vaddr = 0x7100; unpack(w, io, 0x2A); dma38E7(sp, w);
  sp->vaddr = 0x5000; unpack(w, io, 0x12); dma38E7(sp, w);
  fill(w, 0xE100, 0x40, 0);                                            // 3E40-3E55
  fill(w, 0x10300, 0x700, mpyl(sp));                                   // 3E58 from $2134
  sp->vaddr = 0x6000; unpack(w, io, 0x1E); ww(w, 0x0306, 0x0800); dma38E7(sp, w);
  sp->vaddr = 0x6800; unpack(w, io, 0x24); ww(w, 0x0306, 0x0800); dma38E7(sp, w);
  sp->vaddr = 0x6400; unpack(w, io, 0x21); ww(w, 0x0306, 0x0800); dma38E7(sp, w);
}

void CtRacePicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  setup3D8C(sp, w, io);
  ww(w, 0x0303, 0x0A00); unpack(w, io, 0x06);                          // 3B60: the bikes' packets into $7F:0A00 and $7F:8A00
  ww(w, 0x0303, 0x8A00); unpack(w, io, 0x09);
  mvn(w, 0x13800, 0x10A00, 0x2E00);                                    // 3B78 MVN $7F,$7F
  unsigned x = 0x8A00;                                                 // 3B87-3BA7: five 8 x 8 blocks out of $7F:8A00
  x = rows3EB7(w, x, 0x3808);
  { unsigned keep = x; x = rows3EB7(w, x, 0x3E08); x = keep; }
  x = rows3EB7(w, x, 0x4408); x = rows3EB7(w, x, 0x4C0C); x = rows3EB7(w, x, 0x520C);
  unsigned wm = 0x15A00, y;                                            // 3BAC: WMADD $7F:5A00, stride $0200 = $20
  ww(w, 0x0200, 0x0020);
  y = 0x0A00; y = stream3ECD(w, &wm, y); y = stream3ECD(w, &wm, y); y = stream3ECD(w, &wm, y);
  y = 0x2C00; y = stream3ECD(w, &wm, y); y = stream3ECD(w, &wm, y);
  wm = 0x17800; y = 0x3800; y = stream3ECD(w, &wm, y); y = stream3ECD(w, &wm, y); y = stream3ECD(w, &wm, y);
  ww(w, 0x0200, 0x0018);
  unpack(w, io, 0x0C);                                                 // 3BEC: packet 0C into $7F:8A00
  wm = 0x1AE00; y = 0x8A00;
  for (int k = 0; k < 0x0C; k++) y = stream3ED3(w, &wm, y);            // 3BFA: twelve
  w[0x0202] = 0;
  fill(w, 0xF000, 0x1000, mpyl(sp)); fill(w, 0x0C00, 0x1200, mpyl(sp)); // 3C09 / 3C25 from $2134
  ww(w, 0x37, 0x0A00);                                                 // 3C41
  for (unsigned c = 0, t = 0x3D45; c < 8 && w[t] != 0xFF; c++, t += 5) {   // 3C46 JSR $36CC: the HDMA channels
    sp->ch[c].dmap = w[t]; sp->ch[c].bAdr = w[t + 1];
    sp->ch[c].aAdr = (uint16_t)rw(w, t + 2); sp->ch[c].aBank = w[t + 4]; sp->ch[c].indBank = w[t + 4];
  }
}
// $7E:4309 (4640 JMP): `$1446-$1545` cleared -- the mode-7 table builder's request bytes.
void CtRacePicTables(uint8_t* w) { fill(w, 0x1446, 0x100, 0); }

// $7E:3CEA-3D44 -- after the script: C3/09E9 writes the bike frames' reformatting code at $7E:B574 (not drawn: its semantics are
// the main thread's, CtRacePicFrameBike), the layers ($2107 = 60 / 64, $2109 = 68, $210B = $210C = 77, CGWSEL 02, TM 17, TS 04),
// and VRAM words $0B00.. read back (VMAIN 02: every 128th) into $7E:C995, 20 bytes.
void CtRacePicSetupB(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  (void)io;
  CtSeqPicWrite(&sp->p, 0x07, 0x60, 0); CtSeqPicWrite(&sp->p, 0x08, 0x64, 0); CtSeqPicWrite(&sp->p, 0x09, 0x68, 0);
  CtSeqPicWrite(&sp->p, 0x0B, 0x77, 0); CtSeqPicWrite(&sp->p, 0x0C, 0x77, 0);
  CtSeqPicWrite(&sp->p, 0x30, 0x02, 0);
  CtSeqPicWrite(&sp->p, 0x2C, 0x17, 0); CtSeqPicWrite(&sp->p, 0x2D, 0x04, 0);
  sp->vmain = 0x02; sp->vaddr = 0x0B00;
  CtSeqStagePicVramRead(sp, w, 0xC995, 0x14);
}
// $7E:388B -- CGRAM from $7E:2200, 512 bytes.
void CtRacePicCgram(CtSeqStagePic* sp, const uint8_t* w) { sp->cgadd = 0; CtSeqStagePicDma(sp, w, 0x00, 0x22, 0x2200, 0x200); }

// ---- the vblank hooks ($0940) ----
// $7E:3870 -- a byte of the OAM's high table: `($13)` shifted down to the size bits of the next sprite (`$11` / 4).
static void hook3870(uint8_t* w) {
  unsigned x = (w[0x11] >> 2) & 3;
  unsigned a = rw(w, 0x13);
  uint8_t v = w[a];
  while (x != 4) { x++; v = (uint8_t)(((v >> 1) | 0x80) >> 1); }      // 3882 INX / SEC / ROR A / LSR A
  w[a] = v;
}
static void hook380B(CtSeqStagePic* sp, uint8_t* w) {
  hook3870(w);
  CtSeqStagePicDma(sp, w, 0x00, 0x04, 0x0700, 0x220);                // $2102 = 0, DMAP 00 to $2104
  CtSeqPicWrite(&sp->p, 0x03, 0x80, 0); CtSeqPicWrite(&sp->p, 0x02, w[0x10], 0);   // 382E: priority rotation from `$10`
  ww(w, 0x11, 0); ww(w, 0x13, 0x0900);
}
static void hook3843(CtSeqStagePic* sp, uint8_t* w) {
  CtSeqPicWrite(&sp->p, 0x1B, 0x01, 0); CtSeqPicWrite(&sp->p, 0x1B, 0x00, 0); CtSeqPicWrite(&sp->p, 0x1C, 0xE0, 0);
  fill(w, 0x0700, 0x200, mpyl(sp));                                    // the OAM shadow: every sprite at y = E0
  CtSeqPicWrite(&sp->p, 0x1C, 0x00, 0);
}
static void hook5221(CtSeqStagePic* sp, uint8_t* w) {
  unsigned v;
  if (w[0xAA]) { w[0xAA] = 0; v = 0x4000; }
  else if (w[0xAC]) { w[0xAC] = 0; v = 0x4400; }
  else return;
  sp->vaddr = (uint16_t)v; sp->vmain = 0x80;
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0x0C00, 0x800);
  w[0x39] = 0;
}
static void hook43F2(CtSeqStagePic* sp, uint8_t* w) {
  static const uint16_t from[7] = { 0x142C, 0x142E, 0x1430, 0x1434, 0x1436, 0, 0 };
  static const uint8_t reg[5] = { 0x1F, 0x20, 0x0F, 0x11, 0x12 };
  for (int k = 0; k < 5; k++) { CtSeqPicWrite(&sp->p, reg[k], w[from[k]], 0); CtSeqPicWrite(&sp->p, reg[k], w[from[k] + 1], 0); }
  ww(w, 0x1424, rw(w, 0x141C)); ww(w, 0x1428, rw(w, 0x1420)); ww(w, 0x1426, rw(w, 0x141E)); ww(w, 0x142A, rw(w, 0x1422));
}
// $7E:44FE -- `$0A0C / 2` (and its carry) into three bytes of each of two HDMA tables, then $7E:5261: $7E:E100 to VRAM $5180.
static void hook44FE(CtSeqStagePic* sp, uint8_t* w) {
  unsigned a = w[0x0A0C], c = a & 1; a >>= 1;
  w[0x451F] = (uint8_t)a; w[0x452B] = (uint8_t)a; w[0x45A6] = (uint8_t)a;
  a = (a + c) & 0xFF;                                                 // 450E ADC #00 (the LSR's carry)
  w[0x4522] = (uint8_t)a; w[0x452E] = (uint8_t)a; w[0x45A9] = (uint8_t)a;
  sp->vaddr = 0x5180; sp->vmain = 0x80;
  CtSeqStagePicDma(sp, w, 0x01, 0x18, 0xE100, 0x40);
}
// $7E:4643 / 464D -- the start line's column of the road map: 19 low bytes, a map row apart (VMAIN 02), into VRAM $0B00 -- from
// $D2:0000 (4643: the line drawn over) or from $7E:C995 (464D: the bytes the setup read back); then off the list ($39DE).
static void hook4643(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io, int fromRom) {
  sp->vmain = 0x02; sp->vaddr = 0x0B00;
  for (unsigned i = 0; i < 0x13; i++) {
    uint8_t v = fromRom ? ((io && io->rom && io->romLen > 0x120000L + (long)i) ? io->rom[0x120000L + i] : 0) : w[0xC995 + i];
    unsigned a = sp->vaddr & 0x7FFF;
    sp->p.vram[a] = (uint16_t)((sp->p.vram[a] & 0xFF00) | v);
    sp->vaddr = (uint16_t)(sp->vaddr + 128);                            // (VMAIN 02: the step on the low byte, 128 words)
  }
}
unsigned CtRacePicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook, const CtRaceIo* io) {
  switch (hook) {
    case 0x4643: hook4643(sp, w, io, 1); return 0;
    case 0x464D: hook4643(sp, w, io, 0); return 0;
    case 0x380B: hook380B(sp, w); return hook;
    case 0x3843: hook3843(sp, w); return hook;
    case 0x5221: hook5221(sp, w); return hook;
    case 0x43F2: hook43F2(sp, w); return hook;
    case 0x44FE: hook44FE(sp, w); return hook;
    default: {
      static unsigned said = 0;
      if (said != hook) { said = hook; fprintf(stderr, "racepic: UNDERIVED vblank hook $%04X\n", hook); }
      return hook;
    }
  }
}

// ---- the picture's tasks ----
static unsigned div16x8(unsigned a, unsigned b) { b &= 0xFF; return b ? (a & 0xFFFF) / b : 0xFFFF; }   // $4204-$4206 -> $4214

// $7E:357B -- THE ROAD's per-line mode-7 matrix (D = $0A00, page A's registers): the cosine and the sine of reg 5 scaled by reg 0's
// low byte (C2-style 16 x 16, $7E:31BF), then for each line from reg 6 down the screen the hardware divider gives cos / X and
// sin / X with X the line's distance (reg 8 on, a step a line): M7A = M7D = cos / X, M7B = sin / X, M7C = -(sin / X) (the signs
// as the angle's), into $7E:F100 + 4n (A, B) and $7E:F488 + 4n (C, D). The routine patches its own loop with what it computed --
// the products as the loop's immediates ($3623, $360B), the negation as a branch ($3616: `80 00` falls into it, `80 04` skips),
// the two sine tables' order ($362B / $3632) and, with reg 11 set, the loop's tail ($3634 = $368F's `BRA` to $366C: every line
// computed is copied to the next and the distance steps 2). Those bytes are written here too: they are the overlay's WRAM.
static void build357B(uint8_t* w, const CtRaceIo* io) {
  const unsigned D = 0x0A00;
  unsigned reg0 = rw(w, D + 0x00), reg5 = rw(w, D + 0x0A);
  unsigned c = CtSeqSine(io, reg5 + 0x100); int cneg = (c & 0x8000) != 0;   // 357F JSR $3461
  ww(w, D + 0xF0, cneg ? ((c ^ 0xFFFF) + 1) & 0xFFFF : c); ww(w, D + 0xF2, reg0 & 0xFF);
  CtSeqMul31BF(w, D);
  unsigned cosP = rw(w, D + 0xF4);
  ww(w, 0x3623, cosP); ww(w, 0x3616, cneg ? 0x0080 : 0x0480);
  unsigned s = CtSeqSine(io, reg5); int sneg = (s & 0x8000) != 0;      // 35B7 JSR $3465
  ww(w, D + 0xF0, sneg ? ((s ^ 0xFFFF) + 1) & 0xFFFF : s); ww(w, D + 0xF2, reg0 & 0xFF);
  CtSeqMul31BF(w, D);
  unsigned sinP = rw(w, D + 0xF4);
  ww(w, 0x360B, sinP);
  unsigned p1 = sneg ? 0xF484 : 0xF0FE, p2 = sneg ? 0xF0FE : 0xF484;
  ww(w, 0x362B, p1); ww(w, 0x3632, p2);
  // $7E:45B0 (the JSR $35FD names): Y = reg 6 x 4 (reg 6 at least 1), X = reg 8, the tail by reg 11
  if (rw(w, D + 0x0C) < 1) ww(w, D + 0x0C, 1);
  unsigned y = (rw(w, D + 0x0C) << 2) & 0xFFFF, x = rw(w, D + 0x10);
  int alt = rw(w, D + 0x16) != 0;
  ww(w, 0x3634, alt ? rw(w, 0x368F) : 0xE8EA);
  unsigned dividend = cosP, guard = 0;                                 // 3603 LDA $3623 / STA $4204
  do {
    unsigned q1 = div16x8(dividend, x);                                // 3608 STX $4206
    dividend = sinP;                                                   // 360A
    y = (y + 3) & 0xFFFF;
    unsigned a = q1;
    unsigned q2 = div16x8(dividend, x);                                // 3614 STX $4206
    if (cneg) a = ((a ^ 0xFFFF) + 1) & 0xFFFF;                         // 3616: the patched branch
    ww(w, 0xF0FD + y, a); ww(w, 0xF487 + y, a);
    dividend = cosP;                                                   // 3622
    y = (y + 1) & 0xFFFF;
    ww(w, p1 + y, q2); ww(w, p2 + y, ((q2 ^ 0xFFFF) + 1) & 0xFFFF);
    if (alt) {                                                         // 366C: the line again, the distance 2 on
      ww(w, 0xF100 + y, rw(w, 0xF0FC + y)); ww(w, 0xF48A + y, rw(w, 0xF0FC + y));
      ww(w, 0xF102 + y, rw(w, 0xF0FE + y)); ww(w, 0xF488 + y, rw(w, 0xF484 + y));
      x = (x + 2) & 0xFFFF; y = (y + 4) & 0xFFFF;
    } else x = (x + 1) & 0xFFFF;                                       // 3634 NOP / INX
  } while (y != 0x0380 && ++guard < 0x4000);                           // 3636 CPY #0380 / BNE
}
// $7E:42CE -- the matrix is rebuilt when page A's reg 0, 5, 6, 8 or 11 changed since the last build ($0A30-$0A38 keep them); a
// change of reg 0 also clears the requests $1446-$1545 ($4309). The ROM's branches enter its store chain at the changed one.
static void task42CE(uint8_t* w, const CtRaceIo* io) {
  static const uint8_t r[5] = { 0x00, 0x0A, 0x0C, 0x10, 0x16 };
  int k;
  for (k = 0; k < 5; k++) if (rw(w, 0x0A00 + r[k]) != rw(w, 0x0A30 + 2 * k)) break;
  if (k == 5) return;
  if (k == 0) { ww(w, 0x0A30, rw(w, 0x0A00)); CtRacePicTables(w); k = 1; }
  for (; k < 5; k++) ww(w, 0x0A30 + 2 * k, rw(w, 0x0A00 + r[k]));
  build357B(w, io);
}
// $7E:428B -- page A's reg 1 += reg 9 (10 bits), then the mode-7 scroll and centre the hook $43F2 sends: $1420 = reg 1 + $1440,
// $142C = it + $7F; $1422 = page B's reg 2 + $1442, $142E = it + $E0.
static void task428B(uint8_t* w) {
  ww(w, 0x0A02, (rw(w, 0x0A02) + rw(w, 0x0A12)) & 0x03FF);
  unsigned a = (rw(w, 0x0A02) + rw(w, 0x1440)) & 0xFFFF;
  ww(w, 0x1420, a); ww(w, 0x142C, (a + 0x7F) & 0xFFFF);
  a = (rw(w, 0x0B04) + rw(w, 0x1442)) & 0xFFFF;
  ww(w, 0x1422, a); ww(w, 0x142E, (a + 0xE0) & 0xFFFF);
}
// $7E:33D6 -- the signed 16 x 16 product: the signs taken off, $7E:31BF, and the 32 bits negated when they differed.
static void mul33D6(uint8_t* w, unsigned D) {
  unsigned f0 = rw(w, D + 0xF0), f2 = rw(w, D + 0xF2);
  int neg = ((f0 ^ f2) & 0x8000) != 0;
  if (f0 & 0x8000) f0 = ((f0 ^ 0xFFFF) + 1) & 0xFFFF;
  if (f2 & 0x8000) f2 = ((f2 ^ 0xFFFF) + 1) & 0xFFFF;
  ww(w, D + 0xF0, f0); ww(w, D + 0xF2, f2);
  CtSeqMul31BF(w, D);
  if (neg) {                                                           // 33F6 CLC / EOR #FFFF / ADC #0001, then ADC #0000
    unsigned lo = (rw(w, D + 0xF4) ^ 0xFFFF) + 1, c = lo > 0xFFFF;
    ww(w, D + 0xF4, lo & 0xFFFF); ww(w, D + 0xF6, ((rw(w, D + 0xF6) ^ 0xFFFF) + c) & 0xFFFF);
  }
}
// $7E:4447 -- the road's offsets and the sky's scrolls (D = $0A00): $1442 / $1440 = cos / sin of reg 5 times reg 2 (bits 8-23 of
// the signed product), $141E = $17 - reg 6 and $1436 = it - 2, $141C = -reg 5, $1434 = it / 2, $1430 = it / 4 (logical shifts).
static void task4447(uint8_t* w, const CtRaceIo* io) {
  const unsigned D = 0x0A00;
  ww(w, D + 0xF0, CtSeqSine(io, rw(w, D + 0x0A) + 0x100)); ww(w, D + 0xF2, rw(w, D + 0x04)); mul33D6(w, D);
  ww(w, 0x1442, rw(w, D + 0xF5));
  ww(w, D + 0xF0, CtSeqSine(io, rw(w, D + 0x0A))); ww(w, D + 0xF2, rw(w, D + 0x04)); mul33D6(w, D);
  ww(w, 0x1440, rw(w, D + 0xF5));
  unsigned a = (((rw(w, D + 0x0C) ^ 0xFFFF) + 1) + 0x17) & 0xFFFF;
  ww(w, 0x141E, a); ww(w, 0x1436, (a - 2) & 0xFFFF);
  a = ((rw(w, 0x0A0A) ^ 0xFFFF) + 1) & 0xFFFF;
  ww(w, 0x141C, a); ww(w, 0x1434, a >> 1); ww(w, 0x1430, a >> 2);
}
int CtRacePicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t, const CtRaceIo* io) {
  (void)sp;
  switch (t) {
    case 0x42CE: task42CE(w, io); return 1;
    case 0x428B: task428B(w); return 1;
    case 0x4447: task4447(w, io); return 1;
    default: return 0;
  }
}
