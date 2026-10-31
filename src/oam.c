// oam.c -- the field sprite -> OAM build (disasm/notes/oam.md; `ct.exe --checkoam` compares it with the oracle).
//
// The pipeline (per frame):
//   C0/B276  init the per-group OAM write pointers to constants (main: default=spr28, B=spr104,
//            C=spr4; high: $0907/$091A/$0901).
//   C0/B2A8  iterate the Y-sorted sprite list $0E00 (chain heads) and the $0E81 next-links.
//   C0/B701  stage 1 (B788): assemble each object's OBJ tiles into the $7F4Bxx scratch -
//            finalX = (objX $0A00 + Xoff) & 0xFF, finalY = (objY $0A80 + Yoff) & 0xFF, plus a
//            high byte 0xAA | (each tile's X-bit-8).
//   C0/B330  stage 2: group by priority ($0F80 & 0x0C) and copy the 4 tiles' {X,Y,tile,attr}
//            to the group's main pointer (+0x10) and the high byte to its high pointer (+1).
#include "oam.h"
#include "romview.h"
// A byte of the ROM the sprite engine reads (the sprite tables, frames, palettes, tiles): the container's named
// table when one covers it (CT_ASSET_TABLE, romview.h), else the ROM's.
#define MR(o) CtRomByte(rom, 0x400000L, (long)(o))

// C0/B309's stage 2: a copy of the records C0/B788 placed (X in +0, the clamped Y in +1), one OAM entry per group of
// four pieces, the group count by sprite class (1 / 2 / 3 / 6; class 3 is 24 pieces). oam.md "C0/B309's stage 2 is a
// copy" has the listing and the class arms.
// Note: this function is a model for `--checkoam`, `--verifyframe` and `--sweepstage2` (only src/main.c calls it; the
// field renderer draws through field.c's drawSprite()). Not modelled: C0/B701's refusal. The ROM emits nothing for an
// object whose arena release is refused (`JSR C0/B701 / BCC ->B30F / RTS`), while this walk emits every object in the
// $0E00/$0E81 list, so after a refused object every later one lands at the wrong cursor. The fix is to rejoin the
// release and the copy into one walk, as the ROM has it (oam.md); until then class 3 stays out of --sweepstage2's seeds.
void CtBuildFieldOAM(const uint8_t* r, uint8_t* oamMain, uint8_t* oamHigh,
                     uint8_t* wroteMain, uint8_t* wroteHigh) {
  // The high-table destinations per piece group (ctOamEmit's HIDST, read from the consumer side). Class 3's six come
  // from C0/B5CB, which pushes $4F81/$4F80/$4B41/$4B40/$4F01 and then loads $4F00, so the stores come out in this order.
  static const int GRP[4] = { 1, 2, 3, 6 };   // groups of four per class -- B341/B3FB/B4D4/B5CB
  static const int HIDST[4][6] = { { 0x4F00 },
                                   { 0x4F00, 0x4F01 },
                                   { 0x4F00, 0x4F01, 0x4B40 },
                                   { 0x4F00, 0x4F01, 0x4B40, 0x4B41, 0x4F80, 0x4F81 } };
  // per-group OAM write cursors (byte offsets into the 512-byte main / 32-byte high), the
  // init constants from C0/B276 -- groups [default, B, C].
  int mainOff[3] = { 0x70, 0x1A0, 0x10 }, highOff[3] = { 0x07, 0x1A, 0x01 };
  for (int Y = 0x7E; Y >= 0; Y -= 2) {              // the Y-sorted sprite list $7E0E00 (chain heads)
    int obj = r[0x0E00 + Y];
    if (obj & 0x80) continue;                        // negative = empty slot
    for (;; obj = r[0x0E81 + obj]) {                 // follow the chain via $0E81[obj]
      if (obj & 0x80) break;                         // negative = chain end
      int idx = obj;
      int base = r[0x1700 + idx] | (r[0x1700 + idx + 1] << 8);   // OBJ-assembly scratch offset (0x20-aligned; 0 valid)
      if ((base & 0x1F) || base + 0x20 > 0x540) continue;
      int prio = r[0x0F80 + idx] & 0x0C;
      int g = (prio == 0) ? 0 : (prio & 4) ? 1 : 2;  // group default / B / C
      int cls = r[0x1201 + idx] & 3;                 // the piece class: 0 = 4 pieces, 1 = 8, 2 = 12,
      int ng = GRP[cls];                             //   and 3 = 24, not 16 (B5AF's `LDA #18`)
      for (int e = 0; e < 4 * ng; e++) {             // B369/B42D/B510/B625's count, one 4-byte burst each
        int o = 0x10000 + ((0x4BC0 + base + e * 8) & 0xFFFF);
        int m = mainOff[g] + e * 4;                  // the port streams them straight from the cursor
        if (m + 3 < 512) {
          oamMain[m]     = r[o + 0];                 // B36E LDA $7F4BC0,X -- the placed X
          oamMain[m + 1] = r[o + 1];                 // B374 LDA $7F4BC1,X -- the clamped Y
          oamMain[m + 2] = r[o + 6];                 // B37A LDA $7F4BC6,X
          oamMain[m + 3] = r[o + 7];                 // B380 LDA $7F4BC7,X
          if (wroteMain) wroteMain[m] = wroteMain[m + 1] = wroteMain[m + 2] = wroteMain[m + 3] = 1;
        }
      }
      for (int grp = 0; grp < ng; grp++) {           // B341 / B3FB / B4D4 / B5CB: one high byte per group of 4
        int h = highOff[g] + grp;
        if (h < 32) {
          oamHigh[h] = r[0x10000 + ((HIDST[cls][grp] + idx) & 0xFFFF)];
          if (wroteHigh) wroteHigh[h] = 1;
        }
      }
      mainOff[g] += 0x10 * ng; highOff[g] += ng;     // class 3's advance is $60 (B619/B6A1/B6F8)
    }
  }
}

void CtActorScreenCoords(const uint8_t* r, int idx, int* outScreenX, int* outScreenY) {
  int camX = (r[0x1D0A] << 3) + (r[0x1D87] & 7);          // X: tile scroll * 8 + fine scroll
  int camY = (r[0x1D0E] << 3) + (r[0x1D89] & 7);          // Y
  int wX = r[0x1800 + idx] | (r[0x1801 + idx] << 8);       // actor world X (sub-pixel)
  int wY = r[0x1880 + idx] | (r[0x1881 + idx] << 8);
  *outScreenX = ((wX >> 4) - camX) & 0x1FF;               // >> 4 = sub-pixel -> pixel, minus camera
  *outScreenY = ((wY >> 4) - camY) & 0x1FF;
}

void CtDecodeSpriteAssembly(const uint8_t* r, const uint8_t* rom, int romLen, int idx, CtObjTile out[4]) {
  int frame = r[0x0F01 + idx];
  int asmBank = r[0x1300 + idx];                           // bank of the sprite's OBJ-assembly table
  int asmBase = r[0x1380 + idx] | (r[0x1381 + idx] << 8);  // base address of the table
  int addr = (asmBase + frame * 40) & 0xFFFF;              // each frame record is 40 bytes
  int fileoff = ((asmBank & 0x3F) << 16) | addr;           // HiROM bank:addr -> file offset
  int d9 = r[0x0F81 + idx] | r[0x0D01 + idx];              // base attr bits
  for (int t = 0; t < 4; t++) {
    int off = fileoff + 0x20 + t * 2;                      // the 4 tiles' X/Y offsets live at +0x20
    out[t].xoff = (off + 1 < romLen) ? (int8_t)MR(off) : 0;
    out[t].yoff = (off + 1 < romLen) ? (int8_t)MR(off + 1) : 0;
    out[t].tile = (uint8_t)(r[0x0D00 + idx] + t * 2);      // sequential even tiles
    out[t].attr = (uint8_t)(d9 | r[(t < 2) ? 0x0C00 + idx : 0x0C01 + idx]);
  }
}

// ---- C0/ABA2, the field heading builder (oam.md "C0/ABA2") ----
// It reads the four bytes its callers stage ($01F0/$01F1 current X/Y, $01F2/$01F3 target X/Y) and writes $01EE.
// Note: the quadrant is chosen by an unsigned borrow (`LDA $F3 / SEC / SBC $F1 / BCC`), not a signed compare; at the map
// wrap the two differ by 180 degrees. Two of the four arms swap $01F6/$01F7 through the B half (XBA), which folds the
// circle down to one octant of the table. `ct.exe --sweepaba2` checks it.
int CtMoveDirAngle(uint8_t* r, const uint8_t* rom) {
  // ABA2 STZ $EF: callers re-read $EE as a 16-bit word, so the heading's high byte must be this zero.
  r[0x01EF] = 0x00;
  int dy = r[0x01F3] - r[0x01F1];                          // ABA4/ABA6/ABA7 LDA $F3 / SEC / SBC $F1
  if (dy >= 0) {                                           // ABA9 BCC ->ABCB  (carry set = no borrow)
    r[0x01F7] = (uint8_t)dy;                               // ABAB STA $F7
    int dx = r[0x01F2] - r[0x01F0];                        // ABAD/ABAF/ABB0
    if (dx >= 0) {                                         // ABB2 BCC ->ABBA
      r[0x01F6] = (uint8_t)dx;                             // ABB4 STA $F6
      r[0x01EE] = 0x00;                                    // ABB6 STZ $EE
    } else {
      // ABBA-ABC7. `EOR #FF / INC A` negates, `EB XBA` parks it in the B half, $F6 takes the
      // Y delta and then XBA brings |dX| back for $F7 -- the axes are swapped, not just negated.
      r[0x01F6] = (uint8_t)dy;                             // ABBE LDA $F7 / ABC0 STA $F6
      r[0x01F7] = (uint8_t)((-dx) & 0xFF);                 // ABBA/ABBC then ABC2 XBA / ABC3 STA $F7
      r[0x01EE] = 0x40;                                    // ABC5/ABC7
    }
  } else {
    int ady = (-dy) & 0xFF;                                // ABCB EOR #FF / ABCD INC A
    r[0x01F7] = (uint8_t)ady;                              // ABCE STA $F7
    // ABD0/ABD2/ABD3: dX's first evaluation on this path (ABA9's BCC jumped over ABAD..ABB2).
    int dx = r[0x01F2] - r[0x01F0];
    if (dx < 0) {                                          // ABD5 BCS ->ABE2
      r[0x01F6] = (uint8_t)((-dx) & 0xFF);                 // ABD7/ABD9/ABDA
      r[0x01EE] = 0x80;                                    // ABDC/ABDE
    } else {
      // ABE2-ABEC, the other swap. Note: $01F7 is written twice on this path, at ABCE with |dY| and
      // here with dX.
      r[0x01F6] = (uint8_t)ady;                            // ABE3 LDA $F7 / ABE5 STA $F6
      r[0x01F7] = (uint8_t)dx;                             // ABE2 XBA / ABE7 XBA / ABE8 STA $F7
      r[0x01EE] = 0xC0;                                    // ABEA/ABEC
    }
  }
  // ABEE-AC04: the scale selector. `89 40 BIT #$40` is an immediate BIT, which sets only Z (not N
  // or V), so each `BNE` is a plain "this bit is set" test.
  int f6 = r[0x01F6], f7 = r[0x01F7], ix;
  if ((f6 & 0x40) || (f7 & 0x40)) {                        // ABEE/ABF0/ABF2 and ABF4/ABF6/ABF8
    r[0x01F4] = (uint8_t)((f6 & 0x7F) >> 2); r[0x01F5] = 0x00;   // AC30 STA $F4 -- a 16-bit store
    ix = (((f7 & 0x7C) << 3) | ((f6 & 0x7F) >> 2)) & 0xFFFF;     // AC34/AC37 ASL x3 / AC3A ORA $F4
  } else if ((f6 & 0x20) || (f7 & 0x20)) {                 // ABFA..AC04
    r[0x01F4] = (uint8_t)((f6 & 0x3F) >> 1); r[0x01F5] = 0x00;   // AC50
    ix = (((f7 & 0x3E) << 4) | ((f6 & 0x3F) >> 1)) & 0xFFFF;     // AC54/AC57 ASL x4 / AC5B
  } else {
    r[0x01F4] = (uint8_t)(f6 & 0x1F); r[0x01F5] = 0x00;    // AC0D
    ix = (((f7 & 0x1F) << 5) | (f6 & 0x1F)) & 0xFFFF;      // AC11/AC14 ASL x5 / AC19
  }
  // `AA TAX` follows the index width, and the callers leave X 16-bit, so the full index moves. All
  // three arms cap the index at $3E0 | $1F = $3FF, so the 1 KB table at $C0:F300 is never overrun.
  r[0x01EE] = (uint8_t)((r[0x01EE] + MR(0xF300 + ix)) & 0xFF);  // AC1E/AC21/AC22/AC24 (and AC3F.., AC60..)
  return r[0x01EE];
}

