// objslot.c -- FD/E39C, the palette-effect record service. See objslot.h.
//
// The battle and the field's VM pass run this one body; the battle's one-factor palette view hangs off io->onRange.
#include "objslot.h"
#include "romview.h"
#include <stdio.h>

// The battle's own trace lines, character for character (checks pin them): `restore` != NULL is the copy-back exit
// ("" for mode 5, "mode4 " for mode 4), else the per-step line with `suffix` ("" for mode 5, " (white)" for mode 4).
static void objslotTrace(CtObjSlotIo* io, const uint8_t* r, const char* restore, int k, const char* suffix) {
  if (!io->tag) return;
  int rec = (int)(r - io->s);
  if (restore)
    fprintf(stderr, "%s f%d rec=$%04X %srestore dst=%02X cnt=%02X acc=%02X\n", io->tag, io->frame, rec, restore,
            r[1], r[2], r[7]);
  else
    fprintf(stderr, "%s f%d rec=$%04X mode=%02X dst=%02X cnt=%02X step=%02X acc=%02X lim=%02X k=%d%s\n", io->tag,
            io->frame, rec, r[0], r[1], r[2], r[3], r[7], r[8], k, suffix);
}

// The live CGRAM page $7E:2200 from the base page $7E:2000 over the words [dst, dst+cnt): k < 0 = FD/E624's copy
// back, else FD/E64F's per-word product -- `STA $4202` = k; B = ((k * (hi & 7C)) >> 8) & 7C, R = (k * (lo & 1F))
// >> 8, G = (((k * ((w & 03E0) >> 2)) >> 8) & F8) << 2, i.e. each 5-bit component (k * c) >> 8.
// kind 1 = FD/E8D7 (mode 4, sub-mode 0): each component moves toward white -- B: `AND #7C / STA $0D / EOR #7C /
// STA $4203` then `$0D + $4217` (the high byte), R the same on `AND #1F`, G on `((w & 03E0) >> 2)` with `EOR #F8`
// and `AND #F8`, the three masked back (`AND #7C1F`, `ASL ASL`): per 5-bit component c + ((acc * (31 - c)) >> 8).
int CtObjSlotSubMask(int sub) {
  static const uint8_t m[7] = { 7, 6, 5, 4, 3, 2, 1 };              // FD/E5ED-E61F: E64F E7C0 E73A E6A1 E779 E6D1 E708
  return m[sub < 0 ? 0 : sub > 6 ? 6 : sub];
}
static void objslotPalRange(CtObjSlotIo* io, int dst, int cnt, int k, int kind) {
  uint8_t* s = io->s;
  int mask = (kind & CT_OBJSLOT_KIND_SUB) ? (kind & 7) : 7;        // the mode-5 sub-mode's components (kind 0: all)
  for (int w = dst; w < dst + cnt && w < 256; w++) {
    int b = 0x2000 + w * 2, l = 0x2200 + w * 2;
    if (k < 0) { s[l] = s[b]; s[l + 1] = s[b + 1]; continue; }     // FD/E63B-E63F (FD/E8C3-E8CE the same)
    int word = s[b] | (s[b + 1] << 8);
    if (kind == 1) {                                               // FD/E8DB-E929
      int b4 = (word >> 8) & 0x7C, r5 = word & 0x1F, g8 = ((word & 0x03E0) >> 2) & 0xFF;
      int hi = (b4 + ((k * (b4 ^ 0x7C)) >> 8)) & 0xFF;
      int lo = (r5 + ((k * (r5 ^ 0x1F)) >> 8)) & 0xFF;
      int c0c = ((hi << 8) | lo) & 0x7C1F;
      int g = (((g8 + ((k * (g8 ^ 0xF8)) >> 8)) & 0xF8) << 2) & 0xFFFF;
      int v = g | c0c;
      s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8);
      continue;
    }
    // Each arm scales its components through the multiplier and takes the others from the base word as they are:
    // B = `LDA $7E2001,X / AND #7C / STA $4203 ... LDA $4217 / XBA` (or the raw high byte, E779), R = `AND #1F` the
    // same (or the raw low byte, E7C0), G = `AND #03E0 / LSR / LSR` through the product, `AND #F8 / ASL / ASL` (or the
    // raw `AND #03E0`, E73A); E6A1 / E6D1 / E708 mask the untouched pair in one `AND #03FF` / `#7C1F` / `#7FE0`.
    int hi = (mask & 4) ? ((k * ((word >> 8) & 0x7C)) >> 8) & 0xFF : (word >> 8);  // FD/E653-E65F
    int lo = (mask & 1) ? (k * (word & 0x1F)) >> 8 : (word & 0xFF);    // FD/E663-E66F
    int c0c = ((hi << 8) | lo) & 0x7C1F;                           // FD/E672-E677
    int g = (mask & 2) ? (((k * (((word & 0x03E0) >> 2) & 0xFF)) >> 8) & 0xF8) << 2   // FD/E679-E692
                       : (word & 0x03E0);
    int v = g | c0c;
    s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8);               // FD/E695
  }
  if (io->onRange) io->onRange(io->ctx, dst, cnt, k, kind);
}

// FD/E5A4, mode 5: the palette fade. +3 step, +5 period (bit7 = the acc is the period byte itself), +7 acc, +8
// limit, +1/+2 the CGRAM range, +6 the sub-mode (0 = FD/E64F's per-word multiply; the others are named below).
static void objslotMode5(CtObjSlotIo* io, uint8_t* r) {
  int a;
  if (r[5] & 0x80) a = r[5];                                       // 3DE5A5-5A8 -> 3DE5D8 (literal)
  else {
    int acc = r[7], lim = r[8], step = r[3];
    if (acc >= lim) {                                              // 3DE5AD BCS -> 3DE5C6
      a = acc - step;                                              // 3DE5C6 SBC
      if (a < 0 || a < lim) { r[0] = 0; a = lim; }                 // 3DE5C9 BCC / 3DE5CE BCS -> 3DE5D0: mode off, at the limit
    } else {
      a = acc + step;                                              // 3DE5B2 ADC (C = 0)
      if (a > 0xFF) {                                              // 3DE5B5 BCS -> 3DE624: mode off, the base page
        r[0] = 0; objslotPalRange(io, r[1], r[2], -1, 0);          //   copied back ($7E2000 -> $7E2200), no acc store
        objslotTrace(io, r, "", 0, "");
        return;
      }
      if (a >= lim) { r[0] = 0; a = lim; }                         // 3DE5B7-5BE
    }
  }
  r[7] = (uint8_t)a;                                               // 3DE5D8
  // 3DE5ED `LDA $0526,Y / BNE / JSR FD/E64F`, then DEC A / BNE down the chain: 1 -> E7C0, 2 -> E73A, 3 -> E6A1,
  // 4 -> E779, 5 -> E6D1, anything else -> E708 (CtObjSlotSubMask). Example: Marle's vanish in Guardia Castle 600
  // (scene 122, `2E 56 10 70 0F 08` right after `5A 10`) is sub-mode 6, R alone.
  objslotPalRange(io, r[1], r[2], a, r[6] ? (CT_OBJSLOT_KIND_SUB | CtObjSlotSubMask(r[6])) : 0);
  objslotTrace(io, r, NULL, a, "");
}

// FD/E82C's sub-modes 1-6 (FD/E87F-E8A7: `DEC A / BNE` down the chain), each over `cnt` words from X = dst * 2 with
// `$4202` = the acc k: every arm moves its components toward white as E8D7 does -- `AND` the component, `STA` it, `EOR`
// the component's full mask into `$4203`, `CLC / ADC $4217`: c + ((k * (max - c)) >> 8) -- and writes the live page
// from the base page ($7E:2000), byte for byte as the ROM stores:
//   1 = E962  R: the low byte only, `(base lo & E0) | R'` (`STA $7E2200,X`, 8-bit) -- the high byte is left as it was
//   2 = E98F  G: the word, `(base & 7C1F) | G' << 5`
//   3 = EA5C  R and G: the word `G' << 5 | R'` -- `AND #001F` leaves nothing of the base's blue, so it is 0 (the yellow
//             of Arc Impulse, T68: records 8/9 `40 10 38 .. 03` / `40 48 38 .. 03`)
//   4 = E935  B: the high byte only, `(base hi & 03) | B'` (`STA $7E2201,X`) -- the low byte is left as it was
//   5 = E9CA  R and B: the word, `(base & 03E0) | B' << 10 | R'` (`XBA` carries B' into the high byte)
//   6+ = EA11 B and G: the word `B' << 10 | G' << 5` -- `AND #7C00`: nothing of the base's red, it is 0
// (B' / G' / R' as E8D7's: B on the high byte's bits 2-6, `AND #7C`, `EOR #7C`; G on `(w & 03E0) >> 2`, `EOR #F8`, the
// sum `AND #F8`, `ASL ASL`; R on `lo & 1F`, `EOR #1F`.) Fire Sword 2 (T3E) runs sub-mode 1 over CGRAM $10-$7F (the red
// arena).
static void objslotWhiteSub(CtObjSlotIo* io, int dst, int cnt, int k, int sub) {
  uint8_t* s = io->s;
  for (int w = dst; w < dst + cnt && w < 256; w++) {
    int b = 0x2000 + w * 2, l = 0x2200 + w * 2;
    int lo = s[b], hi = s[b + 1], word = lo | (hi << 8);
    int b4 = hi & 0x7C, r5 = lo & 0x1F, g8 = ((word & 0x03E0) >> 2) & 0xFF;
    int bw = (b4 + ((k * (b4 ^ 0x7C)) >> 8)) & 0xFF;               // E939-E949 / E9CE-E9DE / EA15-EA25
    int rw = (r5 + ((k * (r5 ^ 0x1F)) >> 8)) & 0xFF;               // E966-E976 / E9E2-E9F2 / EA60-EA70
    int gw = (g8 + ((k * (g8 ^ 0xF8)) >> 8)) & 0xF8;               // E995-E9AD / EA30-EA48 / EA7A-EA92 (`AND #F8`)
    switch (sub) {
      case 1: s[l] = (uint8_t)((lo & 0xE0) | (rw & 0x1F)); break;                            // E979-E985
      case 2: { int v = (word & 0x7C1F) | ((gw << 2) & 0xFFFF);                                 // E9AF-E9BE
                s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8); break; }
      case 3: { int v = (rw & 0x1F) | ((gw << 2) & 0xFFFF);                                    // EA73-EA9A
                s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8); break; }
      case 4: s[l + 1] = (uint8_t)((bw & 0x7C) | (hi & 0x03)); break;                          // E94C-E958
      case 5: { int v = (((bw << 8) | rw) & 0x7C1F) | (word & 0x03E0);                         // E9E1-EA05
                s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8); break; }
      default: { int v = ((bw << 8) & 0x7C00) | ((gw << 2) & 0xFFFF);                          // EA28-EA50
                 s[l] = (uint8_t)v; s[l + 1] = (uint8_t)(v >> 8); break; }
    }
  }
  if (io->onRange) io->onRange(io->ctx, dst, cnt, k, CT_OBJSLOT_KIND_WHITE | (sub > 6 ? 6 : sub));
}

// FD/E82C, mode 4: the fade toward white. The acc walks as mode 5's does, with the two exits swapped -- a borrow
// below 0 (FD/E851 BCC -> E8AC) turns the record off and copies the base page back without storing the acc, a
// carry past FF (FD/E83D BCS -> E844) turns it off at the limit -- then `STA $4202` and the sub-mode $0526: 0 =
// FD/E8D7 (every component), 1 = E962 (red), 2 = E98F (green), 3 = EA5C, 4 = E935 (blue), 5 = E9CA, else EA11.
// Sub-mode 0 is the attract's Zombor flash (`80 12 B0`, battle records 8 and 9) and the field's `88 40` (the
// attract's Flea materialising in 0AD, `88 40 1F F0 02`: acc FF down by 8 every second frame); 1-6 are
// objslotWhiteSub's.
static void objslotMode4(CtObjSlotIo* io, uint8_t* r) {
  int a;
  if (r[5] & 0x80) a = r[5];                                       // FD/E82D-E830 -> E860 (literal)
  else {
    int acc = r[7], lim = r[8], step = r[3];
    if (acc >= lim) {                                              // FD/E835 BCS -> E84E
      a = acc - step;                                              // FD/E84E SBC (C = 1)
      if (a < 0) {                                                 // FD/E851 BCC -> E8AC: off, the base page back
        r[0] = 0; objslotPalRange(io, r[1], r[2], -1, 0);
        objslotTrace(io, r, "mode4 ", 0, "");
        return;
      }
      if (a < lim) { r[0] = 0; a = lim; }                          // FD/E853-E85D
    } else {
      a = acc + step;                                              // FD/E83A ADC (C = 0)
      if (a > 0xFF || a >= lim) { r[0] = 0; a = lim; }             // FD/E83D BCS -> E844 / E83F-E84C
    }
  }
  r[7] = (uint8_t)a;                                               // FD/E860
  if (r[6] == 0) objslotPalRange(io, r[1], r[2], a, 1);            // FD/E875-E87A -> FD/E8D7
  else objslotWhiteSub(io, r[1], r[2], a, r[6]);                   // FD/E87F-E8A7 -> E962 E98F EA5C E935 E9CA EA11
  objslotTrace(io, r, NULL, a, " (white)");
}

static int w16(const uint8_t* s, int a) { return s[a & 0xFFFF] | (s[(a + 1) & 0xFFFF] << 8); }
static void p16(uint8_t* s, int a, int v) { s[a & 0xFFFF] = (uint8_t)v; s[(a + 1) & 0xFFFF] = (uint8_t)(v >> 8); }
static uint8_t romByte(const CtObjSlotIo* io, int bank, int addr) {             // HiROM: $C0-$FF (and $40-$7D) -> file offset
  long o = ((long)(bank & 0x3F) << 16) | (addr & 0xFFFF);
  return CtRomByte(io->rom, io->romLen, o);                                    // (romview.h)
}
// The copy both colour-table modes end in (FD/E468-E47C / E4CB-E4DF): `cnt` words from $F6:x to the live page at
// $7E:2200 + ci*2. `LDA $F60000,X` with X stepping by 2 (16-bit), the destination `[$18]` stepping by 2 in bank $7E;
// Y = the count, DEY / BNE (a count of 0 would run 65536 -- no record in the data has one).
static void objslotTableCopy(CtObjSlotIo* io, const uint8_t* r, int x) {
  int dst = 0x2200 + (r[1] << 1), n = r[2] ? r[2] : 0x10000;
  for (; n > 0; n--, dst += 2, x += 2)
    p16(io->s, dst, romByte(io, 0xF6, x & 0xFFFF) | (romByte(io, 0xF6, (x + 1) & 0xFFFF) << 8));
}
// FD/E437, mode 1: frame +3 of a colour table. `$4202` = the frame, `$4203` = the count * 2 (an 8-bit ASL), the product
// added to the table's address +6/+7 (REP #21: carry clear).
static void objslotMode1(CtObjSlotIo* io, const uint8_t* r) {
  objslotTableCopy(io, r, (r[6] | (r[7] << 8)) + r[3] * ((r[2] << 1) & 0xFF));
}
// FD/E485, mode 8 (and every mode above it): the same copy, the frame read out of the record's frame list -- the long
// pointer +9..+11 (the loader's `X + $FA77` in bank $FD) plus the frame +3, one byte.
static void objslotMode8(CtObjSlotIo* io, const uint8_t* r) {
  int sel = romByte(io, r[11], (r[3] + (r[9] | (r[10] << 8))) & 0xFFFF);
  objslotTableCopy(io, r, (r[6] | (r[7] << 8)) + sel * ((r[2] << 1) & 0xFF));
}
// FD/E4E8, mode 3: colours ci..ci+cc of both pages ($7E:2000 base, $7E:2200 live) rotate up one -- each takes the one
// below it, colour ci the old ci+cc. X = ci*2 + (cc*2 & FF) down to ci*2, DEX DEX / CPX $10.
static void objslotMode3(CtObjSlotIo* io, const uint8_t* r) {
  uint8_t* s = io->s;
  int lo = r[1] << 1, x = (lo + ((r[2] << 1) & 0xFF)) & 0xFFFF;
  int c = w16(s, 0x2000 + x), e = w16(s, 0x2200 + x);                             // E503-E50B
  do { p16(s, 0x2000 + x, w16(s, 0x1FFE + x)); p16(s, 0x2200 + x, w16(s, 0x21FE + x)); x = (x - 2) & 0xFFFF; }
  while (x != lo);                                                                 // E50D-E51D
  p16(s, 0x2000 + x, c); p16(s, 0x2200 + x, e);                                    // E51F-E526
}
// FD/E532, mode 2: the other way -- each colour takes the one above it, colour ci+cc the old ci. X = ci*2 up to
// ci*2 + (cc*2 & FF), INX INX / CPX $10.
static void objslotMode2(CtObjSlotIo* io, const uint8_t* r) {
  uint8_t* s = io->s;
  int x = r[1] << 1, hi = (x + ((r[2] << 1) & 0xFF)) & 0xFFFF;
  int c = w16(s, 0x2000 + x), e = w16(s, 0x2200 + x);                             // E54D-E555
  do { p16(s, 0x2000 + x, w16(s, 0x2002 + x)); p16(s, 0x2200 + x, w16(s, 0x2202 + x)); x = (x + 2) & 0xFFFF; }
  while (x != hi);                                                                 // E557-E567
  p16(s, 0x2000 + x, c); p16(s, 0x2200 + x, e);                                    // E569-E570
}

// FD/E292. D = $0500, DB = 0, X/Y 16-bit. The set: `LDA $F60003,X / ASL A / TAX` (8-bit ASL, B = 0) into $FD:F9C7, a
// word offset into the descriptor stream at $FD:FA77. Six records; per descriptor the high nibble picks the shape:
//   0          one byte, the record's mode byte 0 (the rest of the record is left as it was)
//   1x / 8x    mode ci cc period, three table bytes (+6..+8); an 8x also carries (id & 0F) + 1 frame-list bytes,
//              whose address (+9/+10 = X + $FA77, +11 = $FD) the record keeps
//   any other  mode ci cc period
// +3 (the frame) and +4 (the tick) start at 0. Records 6-11 lose only their mode byte (E384-E395).
void CtObjSlotLoad(uint8_t* s, const uint8_t* rom, long romLen, int loc) {
  if (!s || !rom || loc < 0 || loc > 511) return;
  #define D(i) CtRomByte(rom, romLen, 0x3DFA77L + (long)(i))                      // $FD:FA77,X (romview.h)
  int pal = CtRomByte(rom, romLen, 0x360003L + (long)loc * 14);
  int ti = (pal << 1) & 0xFF;
  long X = CtRomByte(rom, romLen, 0x3DF9C7L + ti) | (CtRomByte(rom, romLen, 0x3DF9C8L + ti) << 8);
  for (int y = 0; y < 0x48; y += 0x0C) {
    uint8_t* r = s + 0x0520 + y;
    int m = D(X) & 0xF0;                                                           // E2CA-E2CE
    if (m == 0) { X++; r[0] = 0; continue; }                                       // E304-E308
    r[0] = D(X); X++; r[1] = D(X); X++; r[2] = D(X); X++;                          // E2DA-E2F1 / E30B-E322
    r[3] = 0; r[4] = 0; r[5] = D(X); X++;                                          // E2F2-E301 / E323-E332
    if (m != 0x10 && m != 0x80) continue;
    r[6] = D(X); X++; r[7] = D(X); X++; r[8] = D(X); X++;                          // E333-E34A
    if (m == 0x80) {                                                               // E34C-E376
      int a = (int)((X + 0xFA77) & 0xFFFF);
      r[9] = (uint8_t)a; r[10] = (uint8_t)(a >> 8); r[11] = 0xFD;
      X += (r[0] & 0x0F) + 1;
    }
  }
  for (int k = 6; k < 12; k++) s[0x0520 + k * 0x0C] = 0;                           // E384-E395
  #undef D
}

// FD/E39C (entered through FD/FFF7): the twelve 12-byte records $0520..$05AF. Each ticks +4 against its period +5
// (every record, a free one too -- 3DE3AC-3B6); on the wrap, modes 1x/8x also step a sub-counter in +3, then the
// mode's handler runs: 1 = FD/E437, 2 = E532, 3 = E4E8, 4 = E82C, 5 = E5A4, 6 = E807, 7 = E57C, 8+ = E485
// (3DE3E1-422). Modes 6 and 7 are not ported -- their records tick as the loop's own do; 1, 2, 3 and 8 run where the
// caller asks for them (io->colourModes).
void CtObjSlotService(CtObjSlotIo* io) {
  uint8_t* s = io->s;
  for (int Y = 0; Y < 0x90; Y += 0x0C) {                           // 3DE425-42F
    uint8_t* r = s + 0x0520 + Y;
    r[4]++;                                                        // 3DE3AC-3B0
    if (r[4] != r[5]) continue;                                    // 3DE3B3-3B6
    r[4] = 0;                                                      // 3DE3B8-3BA
    int m = r[0];
    if ((m & 0xF0) == 0x10 || (m & 0xF0) == 0x80) {                // 3DE3BD-3DE
      r[3]++; if (r[3] == (m & 0x0F) + 1) r[3] = 0; }
    int mode = m >> 4;
    if (mode == 4) objslotMode4(io, r);                            // 3DE405
    if (mode == 5) objslotMode5(io, r);                            // 3DE40D
    if (!io->colourModes) continue;
    if (mode == 1) objslotMode1(io, r);                            // 3DE3ED
    if (mode == 2) objslotMode2(io, r);                            // 3DE3F5
    if (mode == 3) objslotMode3(io, r);                            // 3DE3FD
    if (mode >= 8) objslotMode8(io, r);                            // 3DE422
  }
}
