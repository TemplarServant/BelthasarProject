// oam.c — the field sprite -> OAM build, ported from Chrono Trigger and validated byte-for-byte
// against the LakeSnes oracle (ct.exe --checkoam: 0 mismatches, 100% coverage over many frames).
//
// The pipeline (per frame), reproduced here:
//   C0/B276  init the per-group OAM write pointers to constants (main: default=spr28, B=spr104,
//            C=spr4; high: $0907/$091A/$0901).
//   C0/B2A8  iterate the Y-sorted sprite list $0E00 (chain heads) and the $0E81 next-links.
//   C0/B701  stage 1 (B788): assemble each object's OBJ tiles into the $7F4Bxx scratch -
//            finalX = (objX $0A00 + Xoff) & 0xFF, finalY = (objY $0A80 + Yoff) & 0xFF, plus a
//            high byte 0xAA | (each tile's X-bit-8).
//   C0/B330  stage 2: group by priority ($0F80 & 0x0C) and copy the 4 tiles' {X,Y,tile,attr}
//            to the group's main pointer (+0x10) and the high byte to its high pointer (+1).
#include "oam.h"

// C0/B309's STAGE 2 -- and it is a COPY, not a computation. That correction is the whole of this
// routine's 2026-08-08 rewrite, and it fixed two defects that no scenario check could see.
//
//   B369  LDA #04 / STA $01C9        <- the PIECE COUNT: 04 (class 0) / 08 (B42D) / 0C (B510)
//   B34F  LDX $01DB / STX $81        <- $2181 = the group's main cursor; the 4-byte bursts below then
//                                       stream sequentially through the $2180 WRAM port
//   B36E  LDA $7F4BC0,X / STA $80        the piece's X   (+0)
//   B374  LDA $7F4BC1,X / STA $80        its Y           (+1)
//   B37A  LDA $7F4BC6,X / STA $80        its tile        (+6)
//   B380  LDA $7F4BC7,X / STA $80        its attr        (+7)
//   B386  TXA / ADC #0008 / TAX          the next 8-byte piece record
//   B390  DEC $01C9 / BNE ->B36E
//
// DEFECT 1 -- IT EMITTED ONE OAM ENTRY PER OBJECT AND THE ROM EMITS ONE PER GROUP OF FOUR PIECES.
// C0/B309's FIRST dispatch is on the sprite CLASS, not the priority group (B316 BEQ ->B329 /
// B31C BRL ->B3DF / B323 BRL ->B4B8 / B326 BRL ->B5AF), and each class body carries its own advance:
// `B35D ADC #0010` for 4 pieces, `B421 ADC #0020` for 8, `B504 ADC #0030` for 12 and `B619 ADC #0060`
// for 24, with the high-table byte count matching (ONE at B341, TWO at B3FB, THREE at B4D4, SIX at
// B5CB -- and class 2's third destination is $7F:4B40, BELOW the record window, not $4F02, with
// class 3's fourth at $4B41 and its last two out at $4F80/$4F81). An 8-, 12- or 24-piece sprite
// therefore needs 2, 3 or 6 entries, and the cursors must advance by that much or the next object
// lands on top of this one.
// ⚠ AND THE COUNT IS NOT `(class & 3) + 1`: that arithmetic happens to fit classes 0-2 and is WRONG
// for class 3, which is SIX groups (24 pieces), not four. The table below is read off the four arms.
//
// DEFECT 2 -- IT RE-DERIVED THE PLACEMENT THAT C0/B788 HAD ALREADY DONE. This used to compute
// `finalX = objX + xoff` and `finalY = objY + yoff` and to rebuild the high byte from the X bit-8s.
// The ROM does none of that here: C0/B788 (ctOamEmit in move.c, graded by --sweepdrain) writes the
// placed X into the record's +0 and the CLAMPED Y into +1, and stage 2 copies them out. The Y is the
// difference that matters -- ctOamEmit's Y half is seven hand-written clamp rules, and a plain add
// agrees with it only while the clamp does not bind. Two models of one computation, and they disagree
// exactly where the ROM bothered to have rules.
//
// WHAT THIS COSTS --checkoam, said plainly: with the values now passed through, that check no longer
// discriminates the placement MATH (which moved to where the ROM keeps it, and is graded there). It
// still discriminates the ADDRESSING -- which piece lands at which OAM byte, which high byte at which
// slot, how many entries per object -- because a mis-addressed copy puts the wrong record byte at an
// address the oracle filled differently. --sweepstage2 grades that addressing on SYNTHETIC records,
// where every byte is distinguishable, precisely so it cannot become a memcpy grade.
// ⭐⚠ THE TWO WALKS DISAGREE WHEN B701 REFUSES AN OBJECT, located 2026-09-11.
//
// ⚠ SCOPE FIRST, because the commit that found this overstated it as "live code": THIS FUNCTION
// IS A VALIDATION MODEL, not the renderer. Nothing outside src/main.c calls it -- the production
// field renderer rasterises from the actor list through field.c's drawSprite(), and outside
// boot.c's clear nothing else even reads $0700. So what the defect below corrupts is what
// --checkoam, --verifyframe and --sweepstage2 believe, not what the game draws. That makes it a
// grading defect rather than a rendering one -- and --checkoam carries the same blindness it
// describes: it is green because no object is ever refused in the scenarios it runs. C0/B309 opens `JSR C0/B701 / BCC ->B30F / RTS`: an object whose
// arena release is REFUSED emits nothing at all. CtAnimDrawPass models that exactly -- it advances the
// cursors only on the CLC arm and counts the rest in gCtDrawSkipped. THIS function does not: it walks
// the same $0E00/$0E81 list a SECOND time and emits bytes for every object in it. So the moment any
// object is refused, the two walks are out of step and every later object lands at the wrong cursor.
//
// MEASURED, on --sweepstage2 with three class-3 objects seeded: the chain links, $1700, $1B00 and the
// whole $7F:4BC0/$7F:4F00 record windows are BYTE-IDENTICAL to the oracle's, and the ROM still emits
// obj02 then obj06 while this emits obj02, obj04, obj06 -- the ROM refused obj04. The bytes were never
// wrong; the object list was.
//
// ⚠ AND THE OBVIOUS FIX IS WRONG. Having CtAnimDrawPass publish a per-object accept flag for this
// function to filter on would break every caller that does NOT run the draw pass first -- which is the
// normal case: in production CtAnimDrawPass runs only under legsOn(), and main.c's three call sites
// here run none. Calling C0/B701 again from this function is worse: it is the engine's only arena
// RELEASE and would double-free. The ROM has ONE walk that releases and emits per object; this port
// split it in two, and rejoining them is the fix -- a structural change to the render path with its
// own design and its own grading, not a patch. Until then class 3 stays out of --sweepstage2's seeds
// (the note there has the reproduction) because it is the class large enough to make the arena refuse.
void CtBuildFieldOAM(const uint8_t* r, uint8_t* oamMain, uint8_t* oamHigh,
                     uint8_t* wroteMain, uint8_t* wroteHigh) {
  // The high-table DESTINATIONS per piece-group -- ctOamEmit's HIDST read from the consumer side.
  // Class 3's six come from C0/B5CB, which PUSHES $4F81/$4F80/$4B41/$4B40/$4F01 and then loads
  // $4F00, so the stores come back out in this order; they are the same six C0/C73A writes.
  static const int GRP[4] = { 1, 2, 3, 6 };   // groups of four per class -- B341/B3FB/B4D4/B5CB
  static const int HIDST[4][6] = { { 0x4F00 },
                                   { 0x4F00, 0x4F01 },
                                   { 0x4F00, 0x4F01, 0x4B40 },
                                   { 0x4F00, 0x4F01, 0x4B40, 0x4B41, 0x4F80, 0x4F81 } };
  // per-group OAM write cursors (byte offsets into the 512-byte main / 32-byte high), the
  // init constants from C0/B276 — groups [default, B, C].
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
      int cls = r[0x1201 + idx] & 3;                 // the PIECE CLASS: 0 = 4 pieces, 1 = 8, 2 = 12,
      int ng = GRP[cls];                             //   and 3 = TWENTY-FOUR, not 16 (B5AF's `LDA #18`)
      for (int e = 0; e < 4 * ng; e++) {             // B369/B42D/B510/B625's count, one 4-byte burst each
        int o = 0x10000 + ((0x4BC0 + base + e * 8) & 0xFFFF);
        int m = mainOff[g] + e * 4;                  // the port streams them straight from the cursor
        if (m + 3 < 512) {
          oamMain[m]     = r[o + 0];                 // B36E LDA $7F4BC0,X -- the PLACED X
          oamMain[m + 1] = r[o + 1];                 // B374 LDA $7F4BC1,X -- the CLAMPED Y
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
    out[t].xoff = (off + 1 < romLen) ? (int8_t)rom[off] : 0;
    out[t].yoff = (off + 1 < romLen) ? (int8_t)rom[off + 1] : 0;
    out[t].tile = (uint8_t)(r[0x0D00 + idx] + t * 2);      // sequential even tiles
    out[t].attr = (uint8_t)(d9 | r[(t < 2) ? 0x0C00 + idx : 0x0C01 + idx]);
  }
}

// =================================================================================================
// C0/ABA2 -- THE FIELD HEADING BUILDER, byte-exact. Eleven in-bank callers (4D46 4FAB 503D 50EA
// 516D 51F1 5281 5326 53CA 54B6 55E6), no long calls.
//
// IT TAKES NO ARGUMENTS. The ROM reads four bytes its callers stage first -- $01F0 = current X,
// $01F1 = current Y, $01F2 = target X, $01F3 = target Y (see C0/5129 at 5140/5147/514C, and op
// 0x96 at 4F71/4F78/4F7F/4F84) -- and writes the answer to $01EE.
//
// ⚠ THE QUADRANT IS CHOSEN BY AN UNSIGNED BORROW, NOT A SIGNED COMPARE, and that was the whole
// defect. `LDA $F3 / SEC / SBC $F1 / BCC` branches on the CARRY, i.e. on "target < current" as
// UNSIGNED BYTES. This function used to take signed int8 deltas the caller had already computed,
// and at the map wrap the two models disagree: target $00 minus current $FF BORROWS here, but
// `(int8_t)($00 - $FF)` truncates to +1, so the two aimed 180 degrees apart. Measured in --sweepops
// as `$01EE C=00 RO=80` at pos=(FF,7E), which negated the velocity exactly (`$01DD C=2C RO=D4`).
//
// It also WRITES five bytes the old version did not touch at all: $01EF, $01F6, $01F7, the 16-bit
// pair $01F4/$01F5, and $01EE itself. Two of the four quadrant arms SWAP $01F6/$01F7 through the
// accumulator's hidden B half (XBA), which is what folds the circle down to one octant of table.
// Graded by --sweepaba2.
// -------------------------------------------------------------------------------------------------
int CtMoveDirAngle(uint8_t* r, const uint8_t* rom) {
  // ABA2 STZ $EF, and it is LOAD-BEARING rather than housekeeping: callers re-read $EE as a 16-BIT
  // word, so the heading's high byte has to be the zero this store puts there.
  r[0x01EF] = 0x00;
  int dy = r[0x01F3] - r[0x01F1];                          // ABA4/ABA6/ABA7 LDA $F3 / SEC / SBC $F1
  if (dy >= 0) {                                           // ABA9 BCC ->ABCB  (carry set = no borrow)
    r[0x01F7] = (uint8_t)dy;                               // ABAB STA $F7
    int dx = r[0x01F2] - r[0x01F0];                        // ABAD/ABAF/ABB0
    if (dx >= 0) {                                         // ABB2 BCC ->ABBA
      r[0x01F6] = (uint8_t)dx;                             // ABB4 STA $F6
      r[0x01EE] = 0x00;                                    // ABB6 STZ $EE
    } else {
      // ABBA-ABC7. `EOR #FF / INC A` negates, `EB XBA` parks it in the hidden B half, $F6 takes the
      // Y delta and then XBA brings |dX| back for $F7 -- the axes are SWAPPED, not just negated.
      r[0x01F6] = (uint8_t)dy;                             // ABBE LDA $F7 / ABC0 STA $F6
      r[0x01F7] = (uint8_t)((-dx) & 0xFF);                 // ABBA/ABBC then ABC2 XBA / ABC3 STA $F7
      r[0x01EE] = 0x40;                                    // ABC5/ABC7
    }
  } else {
    int ady = (-dy) & 0xFF;                                // ABCB EOR #FF / ABCD INC A
    r[0x01F7] = (uint8_t)ady;                              // ABCE STA $F7
    // ABD0/ABD2/ABD3. This is dX's FIRST evaluation on this path, not a recomputation: ABA9's BCC
    // jumped clean over ABAD..ABB2, so the +Y branch's subtraction never ran.
    int dx = r[0x01F2] - r[0x01F0];
    if (dx < 0) {                                          // ABD5 BCS ->ABE2
      r[0x01F6] = (uint8_t)((-dx) & 0xFF);                 // ABD7/ABD9/ABDA
      r[0x01EE] = 0x80;                                    // ABDC/ABDE
    } else {
      // ABE2-ABEC, the other swap. NOTE $01F7 IS WRITTEN TWICE ON THIS PATH: once at ABCE with
      // |dY|, then again here with dX. A write list that records only the final value misses it.
      r[0x01F6] = (uint8_t)ady;                            // ABE3 LDA $F7 / ABE5 STA $F6
      r[0x01F7] = (uint8_t)dx;                             // ABE2 XBA / ABE7 XBA / ABE8 STA $F7
      r[0x01EE] = 0xC0;                                    // ABEA/ABEC
    }
  }
  // ABEE-AC04: the SCALE selector. `89 40 BIT #$40` is an IMMEDIATE BIT, which on the 65816 sets
  // ONLY Z -- it does not touch N or V -- so each `BNE` is a plain "this bit is set" test.
  int f6 = r[0x01F6], f7 = r[0x01F7], ix;
  if ((f6 & 0x40) || (f7 & 0x40)) {                        // ABEE/ABF0/ABF2 and ABF4/ABF6/ABF8
    r[0x01F4] = (uint8_t)((f6 & 0x7F) >> 2); r[0x01F5] = 0x00;   // AC30 STA $F4 -- a 16-BIT store
    ix = (((f7 & 0x7C) << 3) | ((f6 & 0x7F) >> 2)) & 0xFFFF;     // AC34/AC37 ASL x3 / AC3A ORA $F4
  } else if ((f6 & 0x20) || (f7 & 0x20)) {                 // ABFA..AC04
    r[0x01F4] = (uint8_t)((f6 & 0x3F) >> 1); r[0x01F5] = 0x00;   // AC50
    ix = (((f7 & 0x3E) << 4) | ((f6 & 0x3F) >> 1)) & 0xFFFF;     // AC54/AC57 ASL x4 / AC5B
  } else {
    r[0x01F4] = (uint8_t)(f6 & 0x1F); r[0x01F5] = 0x00;    // AC0D
    ix = (((f7 & 0x1F) << 5) | (f6 & 0x1F)) & 0xFFFF;      // AC11/AC14 ASL x5 / AC19
  }
  // `AA TAX` is governed by the INDEX width, and the callers leave X 16-BIT, so the full index
  // moves; taking only its low byte would collapse three quarters of the table onto one row. All
  // three arms cap the index at $3E0 | $1F = $3FF, so the 1 KB table at $C0:F300 is never overrun.
  r[0x01EE] = (uint8_t)((r[0x01EE] + rom[0xF300 + ix]) & 0xFF);  // AC1E/AC21/AC22/AC24 (and AC3F.., AC60..)
  return r[0x01EE];
}

