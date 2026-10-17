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

// $7E:3CEA-3D44 -- after the script: C3/09E9 writes the bike frames' reformatting code at $7E:B574 (CtRacePicGenB574), the layers ($2107 = 60 / 64, $2109 = 68, $210B = $210C = 77, CGWSEL 02, TM 17, TS 04),
// and VRAM words $0B00.. read back (VMAIN 02: every 128th) into $7E:C995, 20 bytes.
void CtRacePicSetupB(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io) {
  (void)io;
  CtRacePicGenB574(w);
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
static void hook449C(uint8_t* w);
// ---- shared by the stages' pictures (the overlay's common code: every entry runs the same $7E:3000-39FF) ----
long CtSeqPicMpy(const CtSeqStagePic* sp) { return (long)sp->p.m7[0] * (int8_t)((uint16_t)sp->p.m7[1] >> 8); }
void CtSeqPicHdma36CC(CtSeqStagePic* sp, const uint8_t* w, unsigned t) {
  for (unsigned c = 0; c < 8 && w[t & 0x1FFFF] != 0xFF; c++, t += 5) {   // 5-byte records, $FF ends; the bank to $43x4 and $43x7
    sp->ch[c].dmap = w[t & 0x1FFFF]; sp->ch[c].bAdr = w[(t + 1) & 0x1FFFF];
    sp->ch[c].aAdr = (uint16_t)rw(w, t + 2); sp->ch[c].aBank = w[(t + 4) & 0x1FFFF]; sp->ch[c].indBank = w[(t + 4) & 0x1FFFF];
  }
}
int CtSeqPicCommonHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook) {
  switch (hook) {
    case 0x380B: hook380B(sp, w); return 1;
    case 0x3843: hook3843(sp, w); return 1;
    case 0x388B: CtRacePicCgram(sp, w); return 1;
    default: return 0;
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
    case 0x449C: hook449C(w); return hook;
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
// ---- the bikes' projection, $7E:4D0D (D = $0000) ----
// ⚠ THE CARRY IS STATE here as in seq_race.c: `ADC` with no `CLC` (4DCA, 4DD4, 4DF7, 4E01, 4E0D, 4ED0, 4EE0, 4EE6, 4F00), `SBC`
// with no `SEC` (4E73, 4EDC, 4F34, 4F46), the restoring divide's `ROL $F0` taking the caller's carry. It is one variable, threaded
// in program order; so is X, which 4EEB reads back when it skips its search (4F06 BEQ: `TXA` takes what X held).
typedef struct { uint8_t* w; int c; unsigned x; const CtRaceIo* io; unsigned dvd, quo, mpa, prod; } K;   // the divider and the multiplier too
static unsigned kadc(K* k, unsigned a, unsigned v) { unsigned s = (a & 0xFFFF) + (v & 0xFFFF) + (unsigned)k->c; k->c = s > 0xFFFF; return s & 0xFFFF; }
static unsigned ksbc(K* k, unsigned a, unsigned v) { int s = (int)(a & 0xFFFF) - (int)(v & 0xFFFF) - (1 - k->c); k->c = s >= 0; return (unsigned)s & 0xFFFF; }
static unsigned kneg(unsigned v) { return ((v ^ 0xFFFF) + 1) & 0xFFFF; }
// $4206: the quotient of the dividend $4204 by the byte written (LakeSnes: at once; by 0, FFFF).
static void kdiv(K* k, unsigned b) { b &= 0xFF; k->quo = b ? k->dvd / b : 0xFFFF; }
// $7E:3461 / $7E:3465 -- the sine of A (3461: of A + $100, the cosine), from C0:F900; X = the index, the carry = X >= $200.
static unsigned ksin(K* k, unsigned a) { k->x = a & 0x03FF; k->c = k->x >= 0x0200; return CtSeqSine(k->io, a); }
// C3/094C -- the signed 16 x 8 product $F0 x $F2 (low byte) into $F4..$F7, through $4202 / $4203; the flags survive (PHP / PLP).
// ⚠ $F6 IS NOT CLEARED: `STZ $F7` is an 8-bit store and the 16-bit `ADC $F5` reads $F6 as the last product left it.
static void mul094C(K* k) {
  uint8_t* w = k->w;
  unsigned f0 = rw(w, 0xF0); uint8_t f2 = w[0xF2];
  int neg = ((f0 >> 8) ^ f2) & 0x80;                                  // 094F LDA $F1 / EOR $F2 / PHP
  unsigned m = (f2 & 0x80) ? (unsigned)(uint8_t)(0x100 - f2) : f2;    // 0954 -> $4202
  unsigned a = (f0 & 0x8000) ? kneg(f0) : f0;                         // 0960
  w[0xF7] = 0;                                                        // 096D STZ $F7
  unsigned p1 = (a & 0xFF) * m, p2 = (a >> 8) * m;
  k->x = p1; ww(w, 0xF4, p1);                                         // 0970 LDX $4216 / 0976 STX $F4
  ww(w, 0xF5, (p2 + rw(w, 0xF5)) & 0xFFFF);                           // 0978 REP #21 / LDA $4216 / ADC $F5 / STA $F5
  if (neg) {                                                          // 0984 the 32 bits negated
    unsigned lo = (rw(w, 0xF4) ^ 0xFFFF) + 1, c = lo > 0xFFFF;
    ww(w, 0xF4, lo & 0xFFFF); ww(w, 0xF6, ((rw(w, 0xF6) ^ 0xFFFF) + c) & 0xFFFF);
  }
}
// $7E:32E2 -- the restoring divide of A by $F2, unrolled: 8 steps take the dividend's high byte from `ROL $F0` (the first takes
// the caller's carry into $F0's bit 0), 8 more shift zeros (`ASL A`); $F8 = $F2 - 1 (the subtrahend); the quotient in $F4.
static void div32E2(K* k, unsigned a) {
  uint8_t* w = k->w;
  ww(w, 0xF0, a);
  unsigned f2 = rw(w, 0xF2), f8 = (f2 - 1) & 0xFFFF;
  ww(w, 0xF8, f8); a = 0; ww(w, 0xF4, 0);
  for (int i = 0; i < 16; i++) {
    int nc;
    if (i < 8) {
      unsigned f0 = rw(w, 0xF0); nc = (int)(f0 >> 15); ww(w, 0xF0, ((f0 << 1) | (unsigned)k->c) & 0xFFFF); k->c = nc;   // ROL $F0
      nc = (int)(a >> 15); a = ((a << 1) | (unsigned)k->c) & 0xFFFF; k->c = nc;                                        // ROL A
    } else { k->c = (int)(a >> 15); a = (a << 1) & 0xFFFF; }                                                          // ASL A
    a = ksbc(k, a, f8);                                               // SBC $F8
    if (!k->c) { a = kadc(k, a, f2); k->c = 0; }                      // BCS / ADC $F2 / CLC
    unsigned f4 = rw(w, 0xF4); nc = (int)(f4 >> 15); ww(w, 0xF4, ((f4 << 1) | (unsigned)k->c) & 0xFFFF); k->c = nc;     // ROL $F4
  }
}
// $7E:4D4E -- the first bike's turn: $46 = sin x ($8E or, with $B0, $B2) + cos x ($4F - $40); $48 = cos x it + -sin x ($4F - $40).
static void bike4D4E(K* k) {
  uint8_t* w = k->w;
  unsigned m = rw(w, rw(w, 0xB0) ? 0xB2 : 0x8E);
  ww(w, 0xF0, rw(w, 0x90)); ww(w, 0xF2, m); mul094C(k);
  unsigned t = rw(w, 0xF4);
  ww(w, 0xF0, rw(w, 0x92)); k->c = 0; ww(w, 0xF2, kadc(k, rw(w, 0x4F), 0xFFC0)); mul094C(k);
  k->c = 0; ww(w, 0x46, kadc(k, t, rw(w, 0xF4)));
  ww(w, 0xF0, rw(w, 0x92)); ww(w, 0xF2, m); mul094C(k);
  t = rw(w, 0xF4);
  ww(w, 0xF0, rw(w, 0x94)); k->c = 0; ww(w, 0xF2, kadc(k, rw(w, 0x4F), 0xFFC0)); mul094C(k);
  k->c = 0; ww(w, 0x48, kadc(k, t, rw(w, 0xF4)));
}
// $7E:4DA5 -- the second bike's (none with $1444): the same over $74, its `$B2` only with $44 too, every ADC on the carry as it stands.
static void bike4DA5(K* k) {
  uint8_t* w = k->w;
  if (rw(w, 0x1444)) return;
  unsigned m = rw(w, (rw(w, 0xB0) && rw(w, 0x44)) ? 0xB2 : 0x8E);
  ww(w, 0xF0, rw(w, 0x94)); ww(w, 0xF2, m); mul094C(k);
  unsigned t = rw(w, 0xF4);
  ww(w, 0xF0, rw(w, 0x92)); ww(w, 0xF2, kadc(k, rw(w, 0x74), 0xFFC0)); mul094C(k);
  ww(w, 0x6B, kadc(k, t, rw(w, 0xF4)));
  ww(w, 0xF0, rw(w, 0x96)); ww(w, 0xF2, m); mul094C(k);
  t = rw(w, 0xF4);
  ww(w, 0xF0, rw(w, 0x94)); ww(w, 0xF2, kadc(k, rw(w, 0x74), 0xFFC0)); mul094C(k);
  ww(w, 0x6D, kadc(k, t, rw(w, 0xF4)));
}
// $7E:4EEB -- a bike's screen line from its depth $F0: the row $F4 = (-$F0)'s high byte + reg 2 + reg 0 (low bytes); its divisor
// cached in $1446,row, else searched (D = $4200, C3/0E29: from $B4 up, the first X whose (reg 0 x 256) / X falls under $F4, plus
// one); then $F8 = reg 6 - reg 8, $B4 = X - 1, and A = (X x 256 x ($E0 - $F8)) >> 16 through $7E:31BF. A row of 0 skips both.
static unsigned bike4EEB(K* k) {
  uint8_t* w = k->w;
  unsigned r0 = w[0x0A00];
  ww(w, 0xF2, r0);                                                    // 4EEB LDA $0A00 / AND #00FF / STA $F2
  k->dvd = (r0 << 8) & 0xFFFF;                                        // 4EF3 XBA / STA $4204
  k->x &= 0xFF;                                                       // 4EF7 SEP #10
  unsigned a = kneg(rw(w, 0xF0)); a = ((a >> 8) | (a << 8)) & 0xFFFF;  // 4EF9 .. 4EFF XBA
  a = kadc(k, a, rw(w, 0x0A04)) & 0xFF;                               // 4F00 ADC $0A04 / AND #00FF
  if (a) {
    a = kadc(k, a, rw(w, 0xF2)); ww(w, 0xF4, a);                      // 4F08 ADC $F2 / STA $F4
    k->x = w[0x1446 + (a & 0xFF)];                                    // 4F0C TAY / LDX $1446,Y
    if (!k->x) {
      k->x = w[0xB4]; kdiv(k, k->x);                                  // 4F16 LDX $00B4 / STX $4206
      unsigned guard = 0;
      do {                                                            // C3/0E29 INX / LDA $4214 / STX $4206 / CMP $00F4 / BCS
        k->x = (k->x + 1) & 0xFF;
        unsigned q = k->quo; kdiv(k, k->x);
        k->c = q >= rw(w, 0xF4);
      } while (k->c && ++guard < 0x1000);
      w[0x1446 + w[0xF4]] = (uint8_t)k->x;                            // 4F20 LDY $00F4 / TXA / STA $1446,Y
    }
  }
  unsigned f8 = ksbc(k, rw(w, 0x0A0C), rw(w, 0x0A10));                // 4F31 LDA $0A0C / SBC $0A10
  ww(w, 0xF8, f8); ww(w, 0xFE, f8);
  ww(w, 0xB4, (k->x - 1) & 0xFFFF);                                   // 4F3B TXA / STA $B4 / DEC $B4
  ww(w, 0xF0, (k->x << 8) & 0xFFFF);                                  // 4F40 XBA / STA $F0
  ww(w, 0xF2, ksbc(k, 0x00E0, f8));                                   // 4F43 LDA #00E0 / SBC $F8
  CtSeqMul31BF(w, 0);                                                 // 4F4A (its flags survive)
  ww(w, 0xF8, rw(w, 0xFE));
  return rw(w, 0xF6);
}
// $7E:4E06 -- the first bike on the screen: $48 = the line + $F8 - $5D, $0B00 = (reg 0 x 256) / that (12 bits), the size
// $FC = max($0B00 / 2, $40): $5D = $0C00 / $FC, $61 = $5D + $48, $5B = $1000 / $FC; $46 = ($8000 / $0B00 x 2) x $46 (bits 16-31,
// signed) + $80 - $5B / 2, $5F = $46 + $5B.
static void bike4E06(K* k) {
  uint8_t* w = k->w;
  ww(w, 0xF0, rw(w, 0x48));
  unsigned a = bike4EEB(k);
  kdiv(k, a);                                                         // 4E0D STA $4206
  a = kadc(k, a, rw(w, 0xF8)); k->c = 1; ww(w, 0x48, ksbc(k, a, rw(w, 0x5D)));
  a = k->quo & 0x0FFF; ww(w, 0x0B00, a);
  a >>= 1;                                                            // 4E20 LSR A
  k->c = a >= 0x40; if ((a - 0x40) & 0x8000) a = 0x40;                // 4E21 CMP #0040 / BPL
  ww(w, 0xFC, a); ww(w, 0xF2, a);
  div32E2(k, 0x0C00);
  ww(w, 0x5D, rw(w, 0xF4)); k->c = 0; ww(w, 0x61, kadc(k, rw(w, 0x5D), rw(w, 0x48)));
  ww(w, 0xF2, rw(w, 0xFC)); div32E2(k, 0x1000);
  ww(w, 0x5B, rw(w, 0xF4) & 0xFF);
  ww(w, 0xF2, rw(w, 0x0B00)); div32E2(k, 0x8000);
  a = rw(w, 0xF4); k->c = (int)(a >> 15); ww(w, 0xF0, (a << 1) & 0xFFFF);   // 4E58 ASL A
  ww(w, 0xF2, rw(w, 0x46)); mul33D6(w, 0);
  k->c = 0; ww(w, 0xF2, kadc(k, rw(w, 0xF6), 0x0080));
  a = rw(w, 0x5B); k->c = (int)(a & 1); ww(w, 0xF0, a >> 1);          // 4E6C LSR A
  ww(w, 0x46, ksbc(k, rw(w, 0xF2), rw(w, 0xF0)));
  k->c = 0; ww(w, 0x5F, kadc(k, rw(w, 0x46), rw(w, 0x5B)));
}
// $7E:4E7D -- the second bike's (none with $1444): $6D, $0B0C, $80 = $82 = $0C00 / size, $6B, $84 = $6B + $80, $86 = $6D + $82.
static void bike4E7D(K* k) {
  uint8_t* w = k->w;
  if (rw(w, 0x1444)) return;
  ww(w, 0xF0, rw(w, 0x6D));
  unsigned a = bike4EEB(k);
  kdiv(k, a);
  a = kadc(k, a, rw(w, 0xF8)); k->c = 1; ww(w, 0x6D, ksbc(k, a, rw(w, 0x82)));
  a = k->quo & 0x0FFF; ww(w, 0x0B0C, a);
  a >>= 1;
  k->c = a >= 0x40; if ((a - 0x40) & 0x8000) a = 0x40;
  ww(w, 0xF2, a);
  div32E2(k, 0x0C00);
  ww(w, 0x80, rw(w, 0xF4) & 0xFF); ww(w, 0x82, rw(w, 0xF4) & 0xFF);
  ww(w, 0xF2, rw(w, 0x0B0C)); div32E2(k, 0x8000);
  a = rw(w, 0xF4); k->c = (int)(a >> 15); ww(w, 0xF0, (a << 1) & 0xFFFF);
  ww(w, 0xF2, rw(w, 0x6B)); mul33D6(w, 0);
  ww(w, 0xF2, kadc(k, rw(w, 0xF6), 0x0080));                          // 4ECE ADC #0080 -- the ASL's carry
  a = rw(w, 0x80); k->c = (int)(a & 1); ww(w, 0xF0, a >> 1);
  ww(w, 0x6B, ksbc(k, rw(w, 0xF2), rw(w, 0xF0)));
  ww(w, 0x84, kadc(k, rw(w, 0x6B), rw(w, 0x80)));
  ww(w, 0x86, kadc(k, rw(w, 0x6D), rw(w, 0x82)));
}
// $7E:4D0D -- the angle $F0 = $100 - reg 5: $92 / $96 = its cosine and the negation, $90 / $94 its sine; the two bikes turned
// (4D4E, 4DA5), $B4 = reg 6, then put on the screen, the nearer one last ($48 against $6D: BMI). `c` = the carry the task
// enters with: the dispatcher $7E:398A's `PLP` gives back the flags of its `CPX #0020` (clear) for every slot but the first.
static void task4D0D(uint8_t* w, const CtRaceIo* io, int c) {
  K kk = { w, c, 0, io, 0, 0 }, *k = &kk;
  unsigned f0 = kadc(k, kneg(rw(w, 0x0A0A)), 0x0100); ww(w, 0xF0, f0);
  unsigned a = ksin(k, f0 + 0x100); ww(w, 0x92, a); ww(w, 0x96, kneg(a));
  a = ksin(k, rw(w, 0xF0)); ww(w, 0x90, a); ww(w, 0x94, kneg(a));
  bike4D4E(k); bike4DA5(k);
  ww(w, 0xB4, rw(w, 0x0A0C));
  unsigned x48 = rw(w, 0x48), x6D = rw(w, 0x6D);
  k->c = x48 >= x6D;                                                  // 4D3C CMP $6D / BMI
  if ((x48 - x6D) & 0x8000) { bike4E06(k); bike4E7D(k); } else { bike4E7D(k); bike4E06(k); }
}
// $7E:449C -- after the finish, the sky's scrolls from the camera: $F0 = -(($009D / 4) + reg 1 / 2 + $88 (both ADCs on the
// carry as it stands)), $141C = $F0 + $143E, $1430 = it / 4, $1434 = it / 4 + it / 8 (the LSR's carry) -- what $43F2 sends.
static void hook449C(uint8_t* w) {
  K kk = { w, 0, 0, NULL, 0, 0, 0, 0 }, *k = &kk;
  ww(w, 0xF0, rw(w, 0x9D) >> 2);
  unsigned a = rw(w, 0x0A02); k->c = (int)(a & 1); a >>= 1;          // 44A5 LDA $0A02 / LSR A
  a = kadc(k, a, rw(w, 0xF0)); a = kadc(k, a, 0x0088);
  a = kneg(a); ww(w, 0xF0, a);
  k->c = 0; a = kadc(k, a, rw(w, 0x143E)); ww(w, 0x141C, a);
  ww(w, 0x1430, a >> 2);
  a = rw(w, 0x141C) >> 1; k->c = (int)(a & 1); a >>= 1; ww(w, 0xF0, a);   // 44C0 LSR / LSR / STA $F0
  k->c = (int)(a & 1); a >>= 1;                                       // 44C7 LSR A
  ww(w, 0x1434, kadc(k, a, rw(w, 0xF0)));
}
// ---- the sprites: C3's OAM library over the shadow $0700 (low table, 4 bytes a sprite at `$11`) and $0900 (the high table,
// its byte at `$13`); the data bank is $00, so the shadow is the low WRAM ----
static unsigned lo(unsigned a) { return a & 0x1FFF; }
// C3/0D0E -- a sprite list read through WMDATA ($2181 = A, bank $7E): a count n + 1, then n x (tile, attr, x, y), x and y offset
// by the bytes $15 / $17; the high table's byte shifted down 2 a sprite (small, x < 256; a 0 is left), the next byte every 4.
static void oam0D0E(uint8_t* w, unsigned a) {
  unsigned y = rw(w, 0x11), x = rw(w, 0x13), n = w[a & 0x1FFFF]; a++;
  for (;;) {
    n = (n - 1) & 0xFF; if (!n) break;                                // 0D1F DEC $19 / BEQ
    w[lo(0x0702 + y)] = w[a++ & 0x1FFFF]; w[lo(0x0703 + y)] = w[a++ & 0x1FFFF];
    w[lo(0x0700 + y)] = (uint8_t)(w[a++ & 0x1FFFF] + w[0x15]);
    w[lo(0x0701 + y)] = (uint8_t)(w[a++ & 0x1FFFF] + w[0x17]);
    if (w[lo(x)]) w[lo(x)] >>= 2;                                     // 0D41 LDA $0000,X / BEQ / LSR / LSR
    y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) x = (x + 1) & 0xFFFF;
  }
  ww(w, 0x13, x); ww(w, 0x11, y);
}
// C3/0D5E -- the same, large sprites with a 9-bit x: x = the byte + $15 (16 bits); from $100 up (`CMP #0100 / BMI`) the word
// $9801 is stored instead (x = 1, its bit 8 set: off the screen); the high table takes x's bit 15 and the size bit (`ROR`, `SEC /
// ROR`), then y overwrites the word's high byte.
static void oam0D5E(uint8_t* w, unsigned a) {
  unsigned y = rw(w, 0x11), x = rw(w, 0x13), n = w[a & 0x1FFFF]; a++;
  for (;;) {
    n = (n - 1) & 0xFF; if (!n) break;
    w[lo(0x0702 + y)] = w[a++ & 0x1FFFF]; w[lo(0x0703 + y)] = w[a++ & 0x1FFFF];
    unsigned v = (w[a++ & 0x1FFFF] + rw(w, 0x15)) & 0xFFFF;          // 0D82 REP #21 / AND #00FF / ADC $15
    if (!((v - 0x0100) & 0x8000)) v = 0x9801;                         // 0D89 CMP #0100 / BMI
    w[lo(0x0700 + y)] = (uint8_t)v; w[lo(0x0701 + y)] = (uint8_t)(v >> 8);
    unsigned h = w[lo(x)];
    h = (h >> 1) | ((v >> 15) << 7); h = (h >> 1) | 0x80;             // 0D94 ASL A / ROR A / SEC / ROR A
    w[lo(x)] = (uint8_t)h;
    w[lo(0x0701 + y)] = (uint8_t)(w[a++ & 0x1FFFF] + w[0x17]);
    y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) x = (x + 1) & 0xFFFF;
  }
  ww(w, 0x13, x); ww(w, 0x11, y);
}
// C3/0DBC -- one small sprite from the record $7E:t (tile, attr, x, y); the high table through `($13)`, its low byte stepped
// (`INC $13` with an 8-bit A).
static void oam0DBC(uint8_t* w, unsigned t) {
  unsigned y = rw(w, 0x11), p = rw(w, 0x13);
  w[lo(0x0702 + y)] = w[t & 0x1FFFF]; w[lo(0x0703 + y)] = w[(t + 1) & 0x1FFFF];
  w[lo(0x0700 + y)] = (uint8_t)(w[(t + 2) & 0x1FFFF] + w[0x15]);
  w[lo(0x0701 + y)] = (uint8_t)(w[(t + 3) & 0x1FFFF] + w[0x17]);
  if (w[lo(p)]) w[lo(p)] >>= 2;
  y = (y + 4) & 0xFFFF; if (!(y & 0x0F)) w[0x13]++;
  ww(w, 0x11, y);
}
// $7E:39F5 -- a number ($F0) as digit sprites: the ten-thousands, thousands and hundreds by subtraction into $7E:3A8E / 3A8A /
// 3A86 (tile $20 + the digit), the tens and ones through the divider into $3A82 / $3A7E; leading zeros cut from the count
// $3A7D (the ones always drawn); the list $3A7D through C3/0D0E.
static void num39F5(uint8_t* w, unsigned a) {
  static const unsigned pw[3] = { 10000, 1000, 100 }, at[3] = { 0x3A8E, 0x3A8A, 0x3A86 };
  for (int i = 0; i < 3; i++) {
    unsigned y = 0x1F; int c;
    do { c = a >= pw[i]; a = (a - pw[i]) & 0xFFFF; y++; } while (c);  // SEC / SBC / INY / BCS
    a = (a + pw[i]) & 0xFFFF;                                         // ADC (the carry clear)
    w[at[i]] = (uint8_t)y;
  }
  w[0x3A82] = (uint8_t)(0x20 + a / 10); w[0x3A7E] = (uint8_t)(a % 10 + 0x20);   // 3A2A $4204 = A, $4206 = 10
  unsigned x = 6;
  if (w[0x3A8E] == 0x20) { x--; if (w[0x3A8A] == 0x20) { x--; if (w[0x3A86] == 0x20) { x--; if (w[0x3A82] == 0x20) x--; } } }
  w[0x3A7D] = (uint8_t)x;
  oam0D0E(w, 0x3A7D);
}
// $7E:50D3 / 50E7 -- a minimap marker's place: y = $BE - the product's high byte, x = $C0 + A / 8. ⚠ `SBC $4217` IS 16-BIT:
// its high byte is $4218, the pad's auto-read (JOY1L) -- what lands in $16, the word's high byte (the sprite reads only $15).
static void hud50D3(K* k, const CtSeqStagePic* sp, unsigned a) {
  uint8_t* w = k->w;
  a = (a & 0xFFFF) >> 3;
  k->prod = k->mpa * (a & 0xFF);                                      // 50D6 STA $4203
  unsigned v = ((k->prod >> 8) & 0xFF) | ((sp->joy & 0xFF) << 8);
  unsigned y0 = (rw(w, 0x50DA) + rw(w, 0x50DF)) & 0xFFFF;            // 50D9 LDA #imm / SEC / CLC / ADC #imm: the overlay's bytes
  k->c = 0; ww(w, 0x15, ksbc(k, y0, v));                             // SBC $4217 ($B8 + 6; FF 09's setup writes $D8 at $50DA)
}
static void hud50E7(K* k, unsigned a) { ww(k->w, 0x17, (((a & 0xFFFF) >> 3) + 0xC0) & 0xFFFF); k->c = 0; }
// $7E:50F1 / 5100 -- the two bikes' markers on the minimap (second $77 / $74, first $52 / $4F), the sprite $7E:538D / 5391
// unless `$B0` -- or the shared tail 510D is an `RTS`: the finish writes $60 over its `LDA $B0` (seq_race.c, 487C), and the
// markers are gone from then on.
static void hud510D(K* k, unsigned x) {
  if (k->w[0x510D] == 0x60) return;                                   // 510D: the overlay's own byte
  if (!rw(k->w, 0xB0)) oam0DBC(k->w, x);
}
static void hud50F1(K* k, const CtSeqStagePic* sp) { hud50D3(k, sp, rw(k->w, 0x77)); hud50E7(k, rw(k->w, 0x74)); hud510D(k, 0x538D); }
static void hud5100(K* k, const CtSeqStagePic* sp) { hud50D3(k, sp, rw(k->w, 0x52)); hud50E7(k, rw(k->w, 0x4F)); hud510D(k, 0x5391); }
// $7E:5116 -- the boosts left ($9A icons from x $EA leftwards, the last one $5385 unless `$BA`), the panel $7E:5395 and the
// labels $7E:53AE at 0 / 0, REST (the distance to the end less the immediate of 5170's `SBC`; 0 with `$B0` or from 1001 up)
// at $10 / $0E, that immediate ($7E:5171, the routine's own code) stepped down every 64 frames of `$BC` while not 0, the
// score `$69` (floored at 0) / 2 at $10 / $18.
static void hud5116(K* k) {
  uint8_t* w = k->w;
  ww(w, 0x17, 0x00D0); ww(w, 0x15, 0x00EA);
  if (rw(w, 0x9A)) {
    ww(w, 0xF0, rw(w, 0x9A));
    do {
      unsigned x = 0x5389;
      if (rw(w, 0xF0) == 1 && !rw(w, 0xBA)) x = 0x5385;
      oam0DBC(w, x);
      ww(w, 0x15, (rw(w, 0x15) - 8) & 0xFFFF);
      ww(w, 0xF0, (rw(w, 0xF0) - 1) & 0xFFFF);
    } while (rw(w, 0xF0));
  }
  ww(w, 0x17, 0); ww(w, 0x15, 0);
  oam0D5E(w, 0x5395); oam0D0E(w, 0x53AE);
  unsigned a = 0;
  if (!rw(w, 0xB0)) {
    k->c = 1; ww(w, 0xF0, ksbc(k, 0, rw(w, 0x51)));                   // 515D LDA #0000 / SEC / SBC $51
    ww(w, 0xF2, ksbc(k, 0x0008, rw(w, 0x53)));                        // 5165 LDA #0008 / SBC $53
    a = rw(w, 0xF1) >> 1;
    k->c = 1; a = ksbc(k, a, rw(w, 0x5171));                          // 516F SEC / SBC #imm -- the immediate is $7E:5171, stepped below
    if (!((a - 0x03E9) & 0x8000)) a = 0;                              // 5173 CMP #03E9 / BMI
  }
  ww(w, 0xF0, a);
  ww(w, 0x15, 0x0010); ww(w, 0x17, 0x000E);
  num39F5(w, rw(w, 0xF0));
  if (!((rw(w, 0xBC) + 1) & 0x3F) && rw(w, 0x5171)) ww(w, 0x5171, rw(w, 0x5171) - 1);
  ww(w, 0x17, 0x0018);
  if (w[0x6A] & 0x80) ww(w, 0x69, 0);                                 // 51A2: the score's floor
  ww(w, 0xF0, rw(w, 0x69) >> 1);
  num39F5(w, rw(w, 0xF0));
}
// $7E:4FAD -- the first bike's sprite: unless a frame is on its way ($1402 = 1; 2 = sent: cleared, `$AA` raised for the hook
// $5221), the frame's request -- $140C / $1406 from $7E:52EB / 52FB by the turn $FE, the zoom's level into the list's count
// $7E:531B ($1408 under $80: $7E:530B; under $100: 5; else 2), $1408 = max($0B00, $80) / 2, every other frame ($0A14) 16 on,
// $1404 from $7E:528B by $AE x 32 + that, $1400 = 4, $1402 = 1 (the main thread's $51D1 decompresses it); then the list $7E:531B
// at $46 / $48 unless `$63`.
static void bike4FAD(K* k) {
  uint8_t* w = k->w;
  unsigned a = rw(w, 0x1402);
  if (a != 1) {
    if (a == 2) { ww(w, 0x1402, 0); ww(w, 0xAA, rw(w, 0xAA) + 1); }
    unsigned x = rw(w, 0xFE); ww(w, 0xF0, x);
    ww(w, 0x140C, rw(w, 0x52EB + x)); ww(w, 0x1406, rw(w, 0x52FB + x));
    a = rw(w, 0x1408);
    a = ((a - 0x80) & 0x8000) ? rw(w, 0x530B + x) : ((a - 0x100) & 0x8000) ? 5 : 2;
    w[0x531B] = (uint8_t)a;
    a = rw(w, 0x0B00);
    if ((a - 0x80) & 0x8000) { a = 0x80; ww(w, 0x0B00, a); }
    ww(w, 0x1408, a >> 1);
    ww(w, 0x0A14, rw(w, 0x0A14) + 1);
    if (rw(w, 0x0A14) & 1) ww(w, 0xF0, rw(w, 0xF0) + 0x10);          // 500C LSR / BCC / ADC #000F (the carry set)
    a = rw(w, 0xAE);
    for (int i = 0; i < 5; i++) { k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF; }
    x = kadc(k, a, rw(w, 0xF0));                                      // 501D ADC $F0 (the last ASL's carry)
    ww(w, 0x1404, rw(w, (0x528B + x) & 0x1FFFF));
    ww(w, 0x1400, 4); ww(w, 0x1402, 1);
  }
  if (!rw(w, 0x63)) { ww(w, 0x15, rw(w, 0x46)); ww(w, 0x17, rw(w, 0x48)); oam0D5E(w, 0x531B); }
}
// $7E:5047 -- the second's: $1410 / `$AC`; $1416 = max($0B0C / 2 (12 bits), $40) (the floor also written to $0B0C), the count
// $7E:535C ($0A / 5 / 2), $1412 from $7E:51B1 by $FE (16 on on the even frames of $0A14), $141A = $600, $1414 = $30, $140E = 3,
// $1410 = 1; the list $7E:535C at $6B / $6D unless `$88`.
static void bike5047(K* k) {
  uint8_t* w = k->w;
  unsigned a = rw(w, 0x1410);
  if (a != 1) {
    if (a == 2) { ww(w, 0x1410, 0); ww(w, 0xAC, rw(w, 0xAC) + 1); }
    a = (rw(w, 0x0B0C) >> 1) & 0x0FFF;
    if ((a - 0x40) & 0x8000) { a = 0x40; ww(w, 0x0B0C, a); }
    ww(w, 0x1416, a);
    a = ((a - 0x80) & 0x8000) ? 0x0A : ((a - 0x100) & 0x8000) ? 5 : 2;
    w[0x535C] = (uint8_t)a;
    unsigned x = rw(w, 0xFE);
    if (!(rw(w, 0x0A14) & 1)) x = (x + 0x10) & 0xFFFF;                // 5098 LSR / BCS / ADC #0010 (the carry clear)
    ww(w, 0x1412, rw(w, 0x51B1 + x));
    ww(w, 0x141A, 0x0600); ww(w, 0x1414, 0x0030); ww(w, 0x140E, 3); ww(w, 0x1410, 1);
  }
  if (!rw(w, 0x88)) { ww(w, 0x15, rw(w, 0x6B)); ww(w, 0x17, rw(w, 0x6D)); oam0D5E(w, 0x535C); }
}
// $7E:4F54 -- THE HUD AND THE BIKES' SPRITES: the turn $FE = ((reg 5 + $40) / 128 + reg 12) x 2 (3 bits), $0A14 cleared while
// page B's reg 13 is 0, the multiplier's $4202 = $B8 - $21 (the ASL's borrow); then, unless the attract's `$0384` bit 6, the
// minimap markers and the panel, and the two bikes, the one further down the screen ($61 against $86) first.
static void task4F54(CtSeqStagePic* sp, uint8_t* w, int c) {
  K kk = { w, c, 0, NULL, 0, 0, 0, 0 }, *k = &kk;
  unsigned a = kadc(k, rw(w, 0x0A0A), 0x0040);                        // 4F57 ADC #0040 (the carry the task enters with)
  a >>= 7;
  k->c = 0; a = kadc(k, a, rw(w, 0x0A18));
  k->c = (int)((a >> 15) & 1); a = (a << 1) & 0xFFFF;                 // 4F65 ASL A
  ww(w, 0xFE, a & 0x000E);
  if (!rw(w, 0x0B1A)) ww(w, 0x0A14, 0);
  k->mpa = ksbc(k, rw(w, 0x4F74), rw(w, 0x4F77)) & 0xFF; k->prod = 0;   // 4F73 LDA #imm / SBC #imm / STA $4202 (16-bit: $4203 = 0
                                                                      // too); the immediates are the overlay's ($B8, $21; FF 09: $D8)
  unsigned x61 = rw(w, 0x61), x86 = rw(w, 0x86);
  int hud = !(w[0x0384] & 0x40);
  k->c = x61 >= x86;
  if (!((x61 - x86) & 0x8000)) { if (hud) { hud5100(k, sp); hud50F1(k, sp); hud5116(k); } bike4FAD(k); bike5047(k); }
  else { if (hud) { hud50F1(k, sp); hud5100(k, sp); hud5116(k); } bike5047(k); bike4FAD(k); }
}
// ---- the bike frames: the main thread $7E:51D1 ----
#define B7F(a) w[0x10000 + ((a) & 0xFFFF)]
// C3/09E9 -- THE REFORMATTING CODE's generator (X = the first page written, Y = the source base, through WMDATA from `wm`): for
// each word, in the order (the table C3:09D9's eight offsets, then 8 x 4, 2 x 2, 8 x $20 innermost), `LDA $src / STA $dp` with
// src = the four offsets + Y and dp the destination's low byte, a `LDA #page00 / TCD` before each page, `RTS` at the end; it
// leaves $F0 = $0700, $F2 = $20, $F4 = 4, $F6 = 0, $F8 = Y, $FA = X with its page byte stepped once a page. With `run`, the walk
// does what the code it writes does (the main thread's `JSR $B574` with the data bank $7F): every word of the 4-planes rows at
// $7F:Y into its place in the tiles at X.
static const uint16_t tab09D9[8] = { 0x0000, 0x0200, 0x0100, 0x0300, 0x0400, 0x0600, 0x0500, 0x0700 };
static void walk09E9(uint8_t* w, unsigned wm, unsigned x, unsigned y, int run) {
  #define EMIT(v) do { if (!run) w[wm++ & 0x1FFFF] = (uint8_t)(v); } while (0)
  unsigned page = x >> 8, d = 0;
  for (int yo = 0; yo < 8; yo++)
    for (unsigned f2 = 0; f2 < 0x20; f2 += 4)
      for (unsigned f4 = 0; f4 < 4; f4 += 2)
        for (unsigned f6 = 0; f6 < 0x100; f6 += 0x20) {
          if (!(x & 0xFF)) { EMIT(0xA9); EMIT(0x00); EMIT(page); EMIT(0x5B); d = page << 8; page++; }   // 0A1F TXA / BNE
          unsigned src = (tab09D9[yo] + f2 + f4 + f6 + y) & 0xFFFF;
          EMIT(0xAD); EMIT(src); EMIT(src >> 8); EMIT(0x85); EMIT(x);
          if (run) { unsigned a = (d + (x & 0xFF)) & 0x1FFF; w[a] = B7F(src); w[(a + 1) & 0x1FFF] = B7F(src + 1); }
          x = (x + 2) & 0xFFFF;
        }
  EMIT(0x60);
  if (!run) {
    ww(w, 0xF0, tab09D9[7]); ww(w, 0xF2, 0x20); ww(w, 0xF4, 4); ww(w, 0xF6, 0); ww(w, 0xF8, y); ww(w, 0xFA, page << 8);
  }
  #undef EMIT
}
void CtRacePicGenB574(uint8_t* w) { walk09E9(w, 0xB574, 0x0C00, 0x0200, 0); }   // $7E:3CEA: $2181 = $7E:B574, X = $0C00, Y = $0200
// C3/0307 -- THE BIKE's FRAME, SCALED (D = 0, the data bank $7F; the request in $1F..$2C, the out pointer $2D / $2F = $0200):
// rows from $33 = 0 stepped by $27 until `$25` runs out or the row (($33 / 2) & ~$3F, 64 bytes a row of the source, one byte a
// pixel) reaches `$2B`; a row equal to the last one copies the 32 bytes above it, else `$1F` x 16 pixels sampled at X += $29
// (8.8: the column X >> 8, every ADC on the carry as it stands) and turned into 4 bit planes (8 pixels -> 4 bytes, the first
// pixel in bit 7); the rows below, down to where the last frame ended (`$37`), cleared.
static void scale0307(K* k) {
  uint8_t* w = k->w;
  unsigned a;
  ww(w, 0x33, 0);
  for (;;) {
    unsigned n = (rw(w, 0x25) - 1) & 0xFFFF; ww(w, 0x25, n);          // 0309 DEC $25 / BEQ
    if (!n) break;
    a = kadc(k, rw(w, 0x33), rw(w, 0x27)); ww(w, 0x33, a);            // 030D LDA $33 / ADC $27
    a = (a >> 1) & 0xFFC0;
    k->c = a >= rw(w, 0x2B);
    if (!((a - rw(w, 0x2B)) & 0x8000)) break;                         // 0317 CMP $2B / BMI
    k->c = 0; a = kadc(k, a, rw(w, 0x23));
    k->c = a >= rw(w, 0x31);
    if (a == rw(w, 0x31)) {                                           // 0335 CMP $31 / BNE: the row again
      unsigned dst = rw(w, 0x2D), src = (dst - 0x20) & 0xFFFF;
      for (unsigned i = 0; i < 0x20; i++) B7F(dst + i) = B7F(src + i);   // 0344 MVN $7F,$7F
    } else {
      ww(w, 0x31, a);
      unsigned x = 0;
      ww(w, 0x35, rw(w, 0x1F));
      do {
        for (int g = 0; g < 2; g++) {
          for (int i = 0; i < 8; i++) {
            x = kadc(k, x, rw(w, 0x29));                              // TXA / ADC $29 / TAX
            unsigned p = (rw(w, 0x31) + (x >> 8)) & 0xFFFF;           // XBA / AND #00FF / TAY / LDA ($31),Y
            ww(w, 0x3B + i, B7F(p) | (B7F(p + 1) << 8));
          }
          unsigned y = rw(w, 0x2F);
          for (int b = 0; b < 4; b++) {                               // 03BD SEP #20: LSR $3B..$42 / ROL A, x 4
            unsigned acc = 0;
            for (int i = 0; i < 8; i++) { unsigned v = w[0x3B + i]; w[0x3B + i] = (uint8_t)(v >> 1); acc = ((acc << 1) | (v & 1)) & 0xFF; }
            B7F(y + b) = (uint8_t)acc;
          }
          k->c = 0; ww(w, 0x2F, kadc(k, y, 4));                       // TYA / CLC / ADC #0004 / STA $2F
        }
        n = (rw(w, 0x35) - 1) & 0xFFFF; ww(w, 0x35, n);
      } while (n);
    }
    k->c = 0; a = kadc(k, rw(w, 0x2D), 0x20); ww(w, 0x2D, a); ww(w, 0x2F, a);   // 051C
  }
  a = ksbc(k, rw(w, 0x37), rw(w, 0x2D));                              // 031B LDA $37 / SBC $2D / BMI / BEQ
  if (!(a & 0x8000) && a) {
    unsigned x = rw(w, 0x2D);
    B7F(x) = 0; B7F(x + 1) = 0;                                       // STZ $0000,X (16-bit)
    for (unsigned i = 0; i <= a; i++) B7F(x + 1 + i) = B7F(x + i);    // TXY / INY / MVN $7F,$7F
  }
  ww(w, 0x37, rw(w, 0x2D));
}
// $7E:51F4 -- one request: its 14 bytes to $1F (MVN from X), the out pointer $0200, the column step $29 = $27 x 2 (the ASL's carry
// into C3/0307's first ADC), the frame scaled.
static void frame51F4(uint8_t* w, unsigned req) {
  memmove(w + 0x1F, w + req, 14);
  ww(w, 0x2D, 0x0200); ww(w, 0x2F, 0x0200);
  K kk = { w, 0, 0, NULL, 0, 0, 0, 0 }, *k = &kk;
  unsigned a = rw(w, 0x27); k->c = (int)((a >> 15) & 1); ww(w, 0x29, (a << 1) & 0xFFFF);
  scale0307(k);
}
// $7E:51D1 -- THE MAIN THREAD's pass (the loop $7E:3023 `JSR $51D1 / LDA $00 / BEQ`, run after each NMI: the oracle's CPU is
// infinitely fast, so the pass is done before the beam moves on): for the first bike's request ($1402 = 1, from $1400) and then
// the second's ($1410 = 1, from $140E), the frame scaled, then -- once the hook $5221 has sent the last one (`$39` = 0; the pass
// waits at 5211 `LDA $39 / BNE` until then, across NMIs: sp->thread keeps where) -- `$39` raised, the rows reformatted into the
// tiles at $0C00 ($B574), and the request marked sent (2).
// The loop calls the pass again as long as the last one did something and `$00` is 0: a request the NMI's tasks made while the
// pass waited is taken in the same frame (the oracle measured: the second bike sent, then the first one's next frame scaled).
static int pass51D1(CtSeqStagePic* sp, uint8_t* w) {
  static const uint16_t flag[2] = { 0x1402, 0x1410 }, req[2] = { 0x1400, 0x140E };
  int did = 0;
  for (int b = 0; b < 2; b++) {
    if (sp->thread == b + 1) sp->thread = 0;                          // resumed at 5211
    else if (sp->thread || rw(w, flag[b]) != 1) continue;
    else frame51F4(w, req[b]);
    did = 1;
    if (rw(w, 0x39)) { sp->thread = (uint8_t)(b + 1); return 0; }     // 5211 the wait
    ww(w, 0x39, rw(w, 0x39) + 1);
    walk09E9(w, 0, 0x0C00, 0x0200, 1);                                // 5218 JSR $B574
    ww(w, flag[b], 2);
  }
  return did;
}
void CtRacePicThread(CtSeqStagePic* sp, uint8_t* w) {
  for (int guard = 0; guard < 16; guard++)
    if (!pass51D1(sp, w) || rw(w, 0x00)) return;                      // 3026 LDA $00 / BEQ $3023
}
#undef B7F
int CtRacePicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned t, const CtRaceIo* io) {
  switch (t) {
    case 0x42CE: task42CE(w, io); return 1;
    case 0x428B: task428B(w); return 1;
    case 0x4447: task4447(w, io); return 1;
    case 0x4F54:
      if (slot == 0) { static int said; if (!said++) fprintf(stderr, "racepic: UNDERIVED -- task $4F54 in slot 0 (the carry the NMI leaves)\n"); }
      task4F54(sp, w, 0); return 1;
    case 0x4D0D:
      if (slot == 0) { static int said; if (!said++) fprintf(stderr, "racepic: UNDERIVED -- task $4D0D in slot 0 (the carry the NMI leaves)\n"); }
      task4D0D(w, io, 0); return 1;
    default: return 0;
  }
}
