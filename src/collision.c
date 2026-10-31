// collision.c -- field tile collision and the player's collision handlers (disasm/notes/collision.md).
// DP = $1D00 throughout, so the ROM's "$XX" is ram[0x1D00 + XX]; absolute operands ($0155 etc.) are ram[addr].
#include "collision.h"
// Set when the commit (C0/9D54-9D67) writes the lead's plane and OAM priority pair; the host copies them into
// its scene WRAM (field.c fieldPlayerCollide).
int g_collPairCommit = 0;
#include <stdio.h>
#include <stdlib.h>

// ROM slope tables at C0/9BF5 / 9C0B / 9C21 (HiROM file offsets 0x9BF5 / 0x9C0B / 0x9C21), generated from the ROM
// (tools/gen/coll_slope.py; a data-free build loads them from the player's pack). 22 entries, indexed by (shape - 2)
// for slope shapes (orig shape 2..0x17).
//   bf5: bit7 = negate-X-after-shift (EOR #$FF); bits0-6 = LSR count (1..3).
//   c0b: 8-bit constant ADDed after the shift.    c21: EOR sign (0x00 keep / 0xFF invert).
// Final bit7 == 0 -> WALKABLE tail, == 1 -> SOLID tail.
#include "tables/coll_slope.h"

// The walk-vs-solid decision of C0/9AD3. `shapeFull` = (byte0 & 0xFC) >> 2; `subX`/`subY` = $1D66/$1D68, the
// sub-tile position (256 subpixels per 16 px tile). Returns 1 = solid (the 9B62 tail), 0 = walkable (9B41).
// The conveyor (shape 0x1E) is walkable here; its $1D30/$1D5E side effects are in CtCollisionShapeDecode.
int CtCollisionShapeSolid(int shapeFull, int subX, int subY) {
  uint8_t s56 = (uint8_t)((subY & 0xFF) >> 2);                       // 9ADD: $56 = $68 >> 2 (Y proxy)
  int shape = shapeFull & 0x3F;
  if (shape == 0) return 0;                                          // none -> walk
  shape--;
  if (shape == 0) return 1;                                          // orig 1 = full solid
  shape--;                                                           // shape := orig - 2
  if (shape >= 0x16) {                                               // one-way / conveyor (orig 0x18..)
    switch (shape) {
      case 0x16: return ((int8_t)subX >= 0) ? 1 : 0;                // 9BA2 solid iff X >= 0
      case 0x17: return ((int8_t)subY >= 0) ? 1 : 0;                // 9BA9 solid iff Y >= 0
      case 0x18: return ((int8_t)subX < 0) ? 0 : (((int8_t)subY < 0) ? 1 : 0);   // 9BC8
      case 0x19: return ((int8_t)subX < 0) ? (((int8_t)subY < 0) ? 1 : 0) : 0;   // 9BD6
      case 0x1A: return ((int8_t)subX < 0) ? (((int8_t)subY >= 0) ? 1 : 0) : 0;  // 9BC1
      case 0x1B: return ((int8_t)subX < 0) ? 0 : (((int8_t)subY >= 0) ? 1 : 0);  // 9BB0
      default:   return 0;                                          // 0x1C conveyor + >=0x1D -> walk
    }
  }
  uint8_t sel = ctCollSlopeBF5[shape], cnt = (uint8_t)(sel & 0x7F), a = (uint8_t)subX;   // SLOPE eval (9AF8-9B40)
  do { a >>= 1; } while (--cnt);
  if (sel & 0x80) a ^= 0xFF;
  a = (uint8_t)(a + ctCollSlopeC0B[shape]);
  a = (uint8_t)(a - s56);
  a ^= ctCollSlopeC21[shape];
  return (a & 0x80) ? 1 : 0;                                        // BMI -> solid
}

void CtCollisionShapeDecode(uint8_t* ram) {
  uint8_t* d = ram + 0x1D00;                 // direct page
  d[0x5E] = 0; d[0x60] = 0; d[0x6A] = 0; d[0x6C] = 0; d[0x6E] = 0;   // 9AD3-9ADB clear outputs
  d[0x56] = (uint8_t)(d[0x68] >> 2);                                 // 9ADD: $56 = $68 >> 2 (Y proxy)
  uint8_t byte1 = d[0x5A], byte2 = d[0x5C];
  int shape = (d[0x58] & 0xFC) >> 2;                                 // 9AE3
  if (shape >= 2) d[0x60] = (uint8_t)(shape - 2);                    // 9AEF $60 = orig - 2 (else stays 0)
  if (shape - 2 == 0x1C) {                                           // 9BDD conveyor side effects (orig 0x1E)
    d[0x5E] = 0x04;
    if (d[0x30] != 0) d[0x30] = ((int8_t)d[0x30] < 0) ? 0xF0 : 0x10;
  }
  int walk = !CtCollisionShapeSolid((d[0x58] & 0xFC) >> 2, d[0x66], d[0x68]);
  if (walk) { d[0x6A] = (uint8_t)(byte2 & 0x03); d[0x6C] = (uint8_t)(byte2 & 0x04); }       // 9B41 walk tail
  else { d[0x6A] = (uint8_t)((byte2 & 0x18) >> 3); d[0x6C] = (uint8_t)(byte2 & 0x20); }      // 9B62 solid tail
  d[0x6E] = (uint8_t)(((byte1 >> 2) & 0x10) | 0x20);                 // both tails: prio from byte1
  d[0x6F] = (uint8_t)(((byte2 >> 2) & 0x10) | 0x20);                 //            + prio from byte2
}

// C0/5B95: actor-vs-actor test for the NPC move step (C0/5129 -> 5170). Returns 1 = blocked (a solid actor in
// the one-tile box ahead), 0 = movable. Gated by $1C80[mover] & 0x02. The four box handlers (5BFA up, 5C20
// down, 5C47 left, 5C6D right) keep the ROM's carry quirks: the primary SBC takes its carry from `CPX $6D`
// (candIdx >= moverIdx), the lateral SBC from `CMP #$0100` (clear), and abs is one's complement (EOR, no INC).
static int collActorBox(int facing, int candX, int candY, int moverX, int moverY, int carryIn) {
  int b = (1 - carryIn);                                  // SBC borrow on the primary axis (carry from CPX $6D)
  if (facing == 0) {                                      // 5BFA up: candidate ABOVE
    int dY = (candY - moverY - b) & 0xFFFF;
    if (!(dY & 0x8000)) return 0;                         // BPL (dY>=0) reject
    if ((dY ^ 0xFFFF) >= 0x100) return 0;
    int dX = (candX - moverX - 1) & 0xFFFF;
    if (((dX & 0x8000) ? (dX ^ 0xFFFF) : dX) >= 0x100) return 0;
    return 1;
  }
  if (facing == 1) {                                      // 5C20 down: candidate BELOW
    int dY = (candY - moverY - b) & 0xFFFF;
    if (dY & 0x8000) return 0;                            // BMI (dY<0) reject
    if (dY >= 0x100) return 0;
    int dX = (candX - moverX - 1) & 0xFFFF;
    if (((dX & 0x8000) ? (dX ^ 0xFFFF) : dX) >= 0x100) return 0;
    return 1;
  }
  if (facing == 2) {                                      // 5C47 left: candidate LEFT
    int dX = (candX - moverX - b) & 0xFFFF;
    if (!(dX & 0x8000)) return 0;                         // BPL (dX>=0) reject
    if ((dX ^ 0xFFFF) >= 0x100) return 0;
    int dY = (candY - moverY - 1) & 0xFFFF;
    if (((dY & 0x8000) ? (dY ^ 0xFFFF) : dY) >= 0x100) return 0;
    return 1;
  }
  // facing 3: 5C6D right: candidate RIGHT
  int dX = (candX - moverX - b) & 0xFFFF;
  if (dX & 0x8000) return 0;                              // BMI (dX<0) reject
  if (dX >= 0x100) return 0;
  int dY = (candY - moverY - 1) & 0xFFFF;
  if (((dY & 0x8000) ? (dY ^ 0xFFFF) : dY) >= 0x100) return 0;
  return 1;
}
static int rd16(const uint8_t* p, int off);
int CtCollisionActorMovable(uint8_t* r, int moverIdx) {   // C0/5B95; 1 = blocked
  r[0x01EA] = 0x00;                                       // 5B95 STZ $EA (DP = $0100), on every path
  if (r[0x0F00 + moverIdx] == 0) return 0;                // 5B99 mover inactive -> movable
  if (!(r[0x1C80 + moverIdx] & 0x02)) return 0;           // 5BA0/5BA3 GATE clear -> movable
  // 5BA7-5BB9 spill the facing and position to DP. 5BA7 `STY $C7` is omitted: every caller writes
  // $01C7 = its own ip right after, so the spill is never observed.
  // Note: 5BAC's `ASL A` runs on the unmasked facing byte, so the spill keeps the unmasked product.
  r[0x01D9] = (uint8_t)((r[0x1600 + moverIdx] << 1) & 0xFF);            // 5BAC / 5BAD STA $D9
  int facing = r[0x1600 + moverIdx] & 3;                  // 5BA9 mover facing
  int moverX = rd16(r, 0x1800 + moverIdx), moverY = rd16(r, 0x1880 + moverIdx);   // 5BB1/5BB6
  r[0x01DB] = (uint8_t)moverX; r[0x01DC] = (uint8_t)(moverX >> 8);      // 5BB4 STA $DB (16-bit)
  r[0x01DD] = (uint8_t)moverY; r[0x01DE] = (uint8_t)(moverY >> 8);      // 5BB9 STA $DD (16-bit)
  int X = (r[0x12000] * 2) & 0xFF;                        // 5BBD $7F2000*2 (8-bit scan index)
  while (X != 0) {                                        // 5BC8 DEX DEX; BNE 5BC3
    if (r[0x0F00 + X] != 0                                // 5BC3 candidate active
        && !(r[0x1100 + X] & 0x80)                        // 5BD2 not inactive
        && X != moverIdx                                  // 5BD7 CPX $6D: skip self
        && (r[0x1B01 + X] & 0x01)) {                      // 5BDB solid
      r[0x01E9] = (uint8_t)X;                             // 5BE2 STX $E9 (the dispatch at 5BE6 clobbers X)
      int carryIn = (X >= moverIdx) ? 1 : 0;              // carry from CPX $6D into the box handler's 1st SBC
      if (collActorBox(facing, rd16(r, 0x1800 + X), rd16(r, 0x1880 + X), moverX, moverY, carryIn)) return 1;
    }
    X = (X - 2) & 0xFF;
  }
  return 0;                                               // 5BD0 fall-through -> movable
}

// C0/8A6D player tile collision: the directional feeler/slide handlers around the probe (8A9E) and the commit
// (9C5C). ADC/SBC carry an explicit carry so the ROM's carry-chained feeler math is kept (cy=1 SEC, cy=0 CLC).
// Each handler leaves the clamped velocity in $1D2E/$1D30.
static int rd16(const uint8_t* p, int off) { return p[off] | (p[off + 1] << 8); }
static void wr16(uint8_t* p, int off, int v) { p[off] = (uint8_t)v; p[off + 1] = (uint8_t)(v >> 8); }
static int adc16(int a, int b, int* cy) { int r = (a & 0xFFFF) + (b & 0xFFFF) + *cy; *cy = (r >> 16) & 1; return r & 0xFFFF; }
static int sbc16(int a, int b, int* cy) { int r = (a & 0xFFFF) - (b & 0xFFFF) - (1 - *cy); *cy = (r >= 0) ? 1 : 0; return r & 0xFFFF; }
// 8A9E probe tri-state: 0 = walkable (carry clear), 1 = z-solid (carry set, A==0), 2 = off-map (A!=0).
static int probe8A9E(uint8_t* ram) { int off = 0; int solid = CtCollisionSolidAt(ram, &off); return off ? 2 : (solid ? 1 : 0); }

// EAST(8AB5)/WEST(8BC4) shared mid-section (8AE4): X movement, probe vertical feelers, slide on $30.
static void coll_eastMid(uint8_t* ram) {                  // C0/8AE4
  uint8_t* d = ram + 0x1D00; int cy, s;
  wr16(d, 0x68, rd16(d, 0x64));                            // 8AE6
  s = probe8A9E(ram);                                      // 8AEA
  if (s == 0) {                                            // 8AED BCC
    cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x64), 0x70, &cy));// 8AF1
    if (probe8A9E(ram)) goto L8B4D;                        // 8AFC BCS 8B4D
    CtCollisionCommit(ram); return;                        // 8AFE
  }
  if (s == 2) { wr16(d, 0x2E, 0); return; }                // 8B02 BNE 8B13
  cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x64), 0x70, &cy));  // 8B06
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); return; }        // 8B11 BCC 8B1A; else 8B13
  wr16(d, 0x30, 0xFFF0);                                   // 8B1C
  cy = 0; wr16(d, 0x68, adc16(0xFFF0, rd16(d, 0x64), &cy));// 8B21
  s = probe8A9E(ram);                                      // 8B26
  if (s) goto L8B7E;                                       // 8B29 BCC 8B2D else BRA 8B7E
  cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); }  // 8B2F
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }  // 8B3F BCC 8B4A; else 8B41
  CtCollisionCommit(ram); return;                          // 8B4A
L8B4D:
  wr16(d, 0x30, 0x0010);                                   // 8B4F
  cy = 0; wr16(d, 0x68, adc16(0x0010, rd16(d, 0x64), &cy));// 8B54
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }  // 8B5C BCC 8B6A; else 8B5E
  cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); }  // 8B6C
  if (probe8A9E(ram) == 0) { CtCollisionCommit(ram); return; }  // 8B7A BCC 8B67; else BRA 8BA3
  cy = 1; wr16(d, 0x66, sbc16(rd16(d, 0x66), rd16(d, 0x2E), &cy));  // 8BA5
  wr16(d, 0x2E, 0);                                        // 8BAC
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));  // 8BAE
  if (probe8A9E(ram)) { wr16(d, 0x30, 0); return; }        // 8BB8 BCC 8BC1; else 8BBA
  CtCollisionCommit(ram); return;                          // 8BC1
L8B7E:
  cy = 1; wr16(d, 0x66, sbc16(rd16(d, 0x66), rd16(d, 0x2E), &cy));  // 8B80
  wr16(d, 0x2E, 0);                                        // 8B87
  cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); }  // 8B89
  if (probe8A9E(ram)) { wr16(d, 0x30, 0); return; }        // 8B97 BCC 8BA0; else 8B99
  CtCollisionCommit(ram); return;                          // 8BA0
}
static void coll_east(uint8_t* ram) {                     // C0/8AB5
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x2F] = 0; d[0x31] = 0;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }  // CLC;ADC#70;ADC$2E
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x64), 0x40, &cy)); // SEC;SBC#40
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x2E, 0); return; }
  coll_eastMid(ram);
}
static void coll_west(uint8_t* ram) {                     // C0/8BC4
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x2F] = 0xFF; d[0x31] = 0;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 0; { int a = adc16(rd16(d, 0x62), rd16(d, 0x2E), &cy); cy = 1; wr16(d, 0x66, sbc16(a, 0x70, &cy)); }  // CLC;ADC$2E;SEC;SBC#70
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x64), 0x40, &cy));
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x2E, 0); return; }
  coll_eastMid(ram);                                       // 8BF6 BRL 8AE4
}
// NORTH(8BF9)/SOUTH(8D11): Y movement, probe horizontal feelers, slide on $2E. Identical body; differ
// only in the initial $68 build (N: $64-$70+$30, S: $64+$30) and $31 (N=$FF, S=0).
static void coll_northSouth(uint8_t* ram, int north) {    // C0/8BF9 / C0/8D11
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy, sL, sR;
  d[0x31] = north ? 0xFF : 0; d[0x2F] = 0;
  wr16(d, 0x62, rd16(ram, 0x1800 + X)); wr16(d, 0x66, rd16(d, 0x62));
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  if (north) { cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); } }
  else { cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy)); }
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x30, 0); if (north) d[0x31] = 0; return; }
  cy = 1; wr16(d, 0x66, sbc16(rd16(d, 0x62), 0x70, &cy)); // left feeler $62-$70
  sL = probe8A9E(ram);
  if (sL == 0) {
    cy = 0; wr16(d, 0x66, adc16(rd16(d, 0x62), 0x70, &cy));  // right feeler $62+$70
    sR = probe8A9E(ram);
    if (sR) goto Lrblk;
    CtCollisionCommit(ram); return;
  }
  if (sL == 2) { wr16(d, 0x30, 0); return; }
  cy = 0; wr16(d, 0x66, adc16(rd16(d, 0x62), 0x70, &cy));
  if (probe8A9E(ram)) { wr16(d, 0x30, 0); return; }
  wr16(d, 0x2E, 0x0010);                                  // slide +16
  cy = 0; { int a = adc16(0x10, rd16(d, 0x62), &cy); cy = 1; wr16(d, 0x66, sbc16(a, 0x70, &cy)); }
  if (probe8A9E(ram)) goto Lcorner;
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }
  CtCollisionCommit(ram); return;
Lrblk:
  if (sR == 2) { wr16(d, 0x30, 0); return; }
  wr16(d, 0x2E, 0xFFF0);                                  // slide -16
  cy = 0; { int a = adc16(0xFFF0, rd16(d, 0x62), &cy); wr16(d, 0x66, adc16(a, 0x70, &cy)); }
  if (probe8A9E(ram)) goto Lcorner2;
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }
  CtCollisionCommit(ram); return;
Lcorner:
  cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x68), rd16(d, 0x30), &cy));
  wr16(d, 0x30, 0);
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); return; }
  CtCollisionCommit(ram); return;
Lcorner2:
  cy = 1; wr16(d, 0x68, sbc16(rd16(d, 0x68), rd16(d, 0x30), &cy));
  wr16(d, 0x30, 0);
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) { wr16(d, 0x2E, 0); return; }
  CtCollisionCommit(ram); return;
}

// Diagonal handlers: corner probes with cardinal fallbacks. Some sites chain the SBC carry into the ADC
// (`SEC;SBC#70;ADC$2E`), others reset it (`...;CLC;ADC$2E`); each is transcribed as the ROM has it.
static void coll_ne(uint8_t* ram) {                       // C0/8E21 (X>0,Y<0)
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x31] = 0xFF; d[0x2F] = 0;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }     // $62+$70+$2E
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); }  // $64-$70+$30
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return; }
  if (probe8A9E(ram)) goto L8E8B;                          // 8E5C BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }  // $62-$70+$2E (CLC)
  if (probe8A9E(ram)) goto L8EC7;                          // 8E6E BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }     // $62+$70+$2E
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));                                       // $64+$30
  if (probe8A9E(ram)) goto L8EBE;                          // 8E86 BCS
  CtCollisionCommit(ram); return;
L8E8B:
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) goto L8EC7;                          // 8E9B BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));
  if (probe8A9E(ram)) goto L8EBE;                          // 8EB3 BCS
  wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return;
L8EBE:
  wr16(d, 0x2E, 0); coll_northSouth(ram, 1); return;       // STZ$2E; BRL 8BF9 (NORTH)
L8EC7:
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));
  if (probe8A9E(ram) == 0) { wr16(d, 0x30, 0); coll_east(ram); return; }   // 8EDD BCC 8EE8 (STZ$30; EAST)
  wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return;              // 8EDF
}
static void coll_nw(uint8_t* ram) {                       // C0/8EF1 (X<0,Y<0)
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x31] = 0xFF; d[0x2F] = 0xFF;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }  // $62-$70+$2E (CLC)
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 1; { int t = sbc16(rd16(d, 0x64), 0x70, &cy); cy = 0; wr16(d, 0x68, adc16(t, rd16(d, 0x30), &cy)); }  // $64-$70+$30
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return; }
  if (probe8A9E(ram)) goto L8F5B;                          // 8F2D BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }     // $62+$70+$2E
  if (probe8A9E(ram)) goto L8F96;                          // 8F3E BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC: chains)
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));                                       // $64+$30
  if (probe8A9E(ram)) goto L8F8D;                          // 8F56 BCS
  CtCollisionCommit(ram); return;
L8F5B:
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) goto L8F96;                          // 8F6A BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC)
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));
  if (probe8A9E(ram)) goto L8F8D;                          // 8F82 BCS
  wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return;
L8F8D:
  wr16(d, 0x2E, 0); coll_northSouth(ram, 1); return;       // STZ$2E; BRL 8BF9 (NORTH)
L8F96:
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }  // $62-$70+$2E (CLC)
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));
  if (probe8A9E(ram) == 0) { wr16(d, 0x30, 0); coll_west(ram); return; }   // 8FAD BCC 8FB8 (STZ$30; WEST)
  wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return;              // 8FAF
}
static void coll_sw(uint8_t* ram) {                       // C0/8FC1 (X<0,Y>0)
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x31] = 0; d[0x2F] = 0xFF;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }  // $62-$70+$2E (CLC)
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));                                       // $64+$30
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return; }
  if (probe8A9E(ram)) goto L902E;                          // 8FF9 BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }     // $62+$70+$2E
  if (probe8A9E(ram)) goto L9064;                          // 900A BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC)
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }  // $64+$30-$70
  if (probe8A9E(ram) == 0) { CtCollisionCommit(ram); return; }  // 9026 BCC 902B(9C5C); else BRL 908A
  goto L908A;
L902E:
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  if (probe8A9E(ram)) goto L9064;                          // 903D BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC)
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }  // $64+$30-$70
  if (probe8A9E(ram)) goto L908A;                          // 9059 BCS
  wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return;
L908A:
  wr16(d, 0x2E, 0); coll_northSouth(ram, 0); return;       // STZ$2E; BRL 8D11 (SOUTH)
L9064:
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }  // $62-$70+$2E (CLC)
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }  // $64+$30-$70
  if (probe8A9E(ram) == 0) { wr16(d, 0x30, 0); coll_west(ram); return; }   // 907F BCC 9093 (STZ$30; WEST)
  wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return;              // 9081
}
static void coll_se(uint8_t* ram) {                       // C0/909C (X>0,Y>0)
  uint8_t* d = ram + 0x1D00; int X = ram[0x0197], cy;
  d[0x31] = 0; d[0x2F] = 0;
  wr16(d, 0x62, rd16(ram, 0x1800 + X));
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }  // $62+$70+$2E (CLC before ADC$2E)
  wr16(d, 0x64, rd16(ram, 0x1880 + X));
  cy = 0; wr16(d, 0x68, adc16(rd16(d, 0x64), rd16(d, 0x30), &cy));                                       // $64+$30
  if (CtCollisionActorBlock(ram)) { wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return; }
  if (probe8A9E(ram)) goto L9107;                          // 90D2 BCS
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC)
  if (probe8A9E(ram)) goto L913D;                          // 90E3 BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }     // $62+$70+$2E
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }  // $64+$30-$70
  if (probe8A9E(ram) == 0) { CtCollisionCommit(ram); return; }  // 90FF BCC 9104(9C5C); else BRL 9163
  goto L9163;
L9107:
  cy = 1; { int t = sbc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(t, rd16(d, 0x2E), &cy)); }     // $62-$70+$2E (NO CLC)
  if (probe8A9E(ram)) goto L913D;                          // 9116 BCS
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }
  if (probe8A9E(ram)) goto L9163;                          // 9132 BCS
  wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return;
L9163:
  wr16(d, 0x2E, 0); coll_northSouth(ram, 0); return;       // STZ$2E; BRL 8D11 (SOUTH)
L913D:
  cy = 0; { int a = adc16(rd16(d, 0x62), 0x70, &cy); cy = 0; wr16(d, 0x66, adc16(a, rd16(d, 0x2E), &cy)); }  // $62+$70+$2E (CLC before ADC$2E)
  cy = 0; { int a = adc16(rd16(d, 0x64), rd16(d, 0x30), &cy); cy = 1; wr16(d, 0x68, sbc16(a, 0x70, &cy)); }
  if (probe8A9E(ram) == 0) { wr16(d, 0x30, 0); coll_east(ram); return; }   // 9158 BCC 916C (STZ$30; EAST)
  wr16(d, 0x30, 0); wr16(d, 0x2E, 0); return;              // 915A
}

// C0/8A6D dispatch (the caller gates it on $0120 != 0): picks the directional handler from the signs of the
// velocity bytes $1D2E/$1D30. `ct.exe --sweepclamp` checks all eight arms against the ROM.
void CtPlayerCollide(uint8_t* ram) {                      // C0/8A6D
  uint8_t* d = ram + 0x1D00;
  int vy = (int8_t)d[0x30], vx = (int8_t)d[0x2E];
  if (vy == 0) { if (vx == 0) return; if (vx < 0) coll_west(ram); else coll_east(ram); }       // 8A82
  else if (vy < 0) { if (vx == 0) coll_northSouth(ram, 1); else if (vx < 0) coll_nw(ram); else coll_ne(ram); }  // 8A8F
  else { if (vx == 0) coll_northSouth(ram, 0); else if (vx < 0) coll_sw(ram); else coll_se(ram); }              // 8A73
}

// C0/9C37 z-plane gate. carry SET = SOLID -> here 1 = solid (blocked), 0 = walkable.
int CtCollisionZGate(const uint8_t* ram) {
  const uint8_t* d = ram + 0x1D00;
  uint8_t plane = ram[0x0155];
  if (d[0x6C] != 0) return (plane == 3) ? 1 : 0;                    // 9C53: IGNORE_Z -> solid iff player plane 3
  uint8_t z = d[0x6A];                                              // 9C3B
  if (z == 0) return 1;                                             // transition-solid
  if (z == 3) return 0;                                             // transition-walkable
  if (z == plane) return 0;                                         // same plane
  if (plane == 3) return 0;                                         // player on transition plane
  return 1;                                                         // the other plane -> solid
}

// C0/9923 actor-proximity probe. Scans the actor list ($7F2000 = count, X = count*2 down by 2), skipping
// inactive ($0F00,X == 0), hidden ($1100,X bit 7) and party ($0197/$0199/$019B) actors. The first actor inside
// the |dy| < $E0, |dx| < $E0 box around the probe point $1D66/$1D68 decides: returns its $1B01,X bit 0
// (1 = blocked) and stops. No actor in range returns 0.
// Note the carries: the dy SBC inherits carry from `CPX $019B` (dy = pos - $1D68 - (X < $019B)), the dx SBC
// a clear carry from `CMP #$E0` (dx = pos - $1D66 - 1).
int CtCollisionActorBlock(uint8_t* ram) {
  uint8_t* d = ram + 0x1D00;
  // $01EB is the touch latch read by event op 0xB0 (C0/3054 -> C0/3154) later in the frame. Each call
  // re-arms it to 0x80, so with several probes per frame the last one wins, as in the ROM.
  ram[0x01EB] = 0x80;                                    // 9925/9928: "nothing"
  int X = (ram[0x12000] * 2) & 0xFF;                     // LDA $7F2000; ASL; TAX (8-bit)
  static int dbg9923 = -1; if (dbg9923 < 0) dbg9923 = getenv("CT_9923DBG") ? 1 : 0;   // probe census (see DEBUG_SURFACE)
  if (dbg9923) fprintf(stderr, "[9923] probe=(%04X,%04X) lead=(%04X,%04X) n=%d\n", rd16(d, 0x66), rd16(d, 0x68),
                       rd16(ram, 0x1800 + ram[0x0197]), rd16(ram, 0x1880 + ram[0x0197]), ram[0x12000]);
  while (X != 0) {                                       // 9938 DEX DEX; BNE 9933
    if (ram[0x0F00 + X] != 0                             // 9933 active
        && !(ram[0x1100 + X] & 0x80)                     // 9945 not hidden
        && X != ram[0x0197] && X != ram[0x0199] && X != ram[0x019B]) {   // not self/party
      int bit0 = ram[0x1B01 + X] & 0x01;                 // 9959/995C
      int carryDy = (X >= ram[0x019B]) ? 1 : 0;          // carry from CPX $019B persists into the dy SBC
      int rawDy = rd16(ram, 0x1880 + X) - rd16(d, 0x68) - (1 - carryDy);
      int dy = rawDy & 0xFFFF;
      int absDy = (dy & 0x8000) ? ((0x10000 - dy) & 0xFFFF) : dy;        // BPL skip / BMI EOR+INC
      if (dbg9923) fprintf(stderr, "[9923]   X=%02X pos=(%04X,%04X) solid=%d dy=%04X\n", X, rd16(ram, 0x1800 + X), rd16(ram, 0x1880 + X), bit0, dy);
      if (absDy < 0xE0) {                                // CMP #$E0; BCC continue
        int rawDx = rd16(ram, 0x1800 + X) - rd16(d, 0x66) - 1;           // dx SBC, carry clear from CMP
        int dx = rawDx & 0xFFFF;
        int absDx = (dx & 0x8000) ? ((0x10000 - dx) & 0xFFFF) : dx;
        if (dbg9923) fprintf(stderr, "[9923]     dx=%04X -> %s\n", dx, absDx < 0xE0 ? "HIT" : "miss");
        if (absDx < 0xE0) { ram[0x01EB] = (uint8_t)X;     // 9960 (solid) / 99A1 (non-solid): STX $01EB
                            return bit0 ? 1 : 0; }       // in the box: SEC(bit0) / CLC(!bit0)
      }
    }
    X = (X - 2) & 0xFF;
  }
  return 0;                                              // 993C CLC (no blocker)
}

// Tile-property fetch (C0/9AC8): for the tile at $1D67/$1D69 (tile X/Y high bytes, masked by $1D1E/$1D1F),
// returns the three property bytes and 1 if the tile is off-map. The default reads the WRAM planes
// $7E30C0/$7E7000/$7E7040; the interactive field installs its own (CtCollisionSetTileFetch). The treasure
// plane test belongs only to the C0/9AA1 entry, so it lives in CtCollisionSolidAt.
static int collFetchEngine(uint8_t* ram, uint8_t* b0, uint8_t* b1, uint8_t* b2) {
  uint8_t* d = ram + 0x1D00;
  uint16_t X = (uint16_t)((d[0x67] | (d[0x69] << 8)) & (d[0x1E] | (d[0x1F] << 8)));
  *b0 = ram[0x30C0 + X]; *b1 = ram[0x7000 + X]; *b2 = ram[0x7040 + X];
  return 0;
}
static int (*g_collFetch)(uint8_t*, uint8_t*, uint8_t*, uint8_t*) = collFetchEngine;
void CtCollisionSetTileFetch(int (*fn)(uint8_t*, uint8_t*, uint8_t*, uint8_t*)) {
  g_collFetch = fn ? fn : collFetchEngine;
}

// C0/9C5C per-axis commit: the tentative position $1D66/$1D70 = $1D62 + $1D2E and $1D68 = $1D64 + $1D30 is
// fetched (9AC8), decoded (9AD3) and z-gated (9C37). Solid zeroes the velocity; otherwise it latches a door
// tile, updates the plane $0155 and the lead's OBJ priority (unless IGNORE_Z, $1D6C) and applies the conveyor.
// The touch-trigger half of C0/9CA1 ($0112-$0116, $0117 bit 7) stays with the host's exit records.
void CtCollisionCommit(uint8_t* ram) {
  uint8_t* d = ram + 0x1D00;
  int tentX = (rd16(d, 0x62) + rd16(d, 0x2E)) & 0xFFFF;             // 9C5E $66=$70=$62+$2E
  wr16(d, 0x66, tentX); wr16(d, 0x70, tentX);
  int tentY = (rd16(d, 0x64) + rd16(d, 0x30)) & 0xFFFF;            // 9C67 $68=$64+$30
  wr16(d, 0x68, tentY);
  if (rd16(d, 0x30) & 0x0080) wr16(d, 0x72, (tentY - 0x100) & 0xFFFF);   // 9C70 wrap-neighbour
  else wr16(d, 0x72, (tentY + 0x100) & 0xFFFF);
  uint8_t b0 = 0, b1 = 0, b2 = 0;                                   // 9AC8 prop fetch (off-map -> solid)
  if (g_collFetch(ram, &b0, &b1, &b2)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }
  d[0x58] = b0; d[0x5A] = b1; d[0x5C] = b2;
  CtCollisionShapeDecode(ram);                                      // 9AD3
  if (CtCollisionZGate(ram)) { wr16(d, 0x2E, 0); wr16(d, 0x30, 0); return; }  // 9C96 SOLID -> STZ $2E/$30
  // C0/9D35 door-on-step latch: the tile ahead ($1D71/$1D73, masked by $1D1E/$1D1F) with property byte 1
  // bit 4 set is stored in $015B and raises $0117 bit 4. C0/0D3E consumes it (CtFieldSceneDoorOpen).
  {
    uint8_t sx = d[0x67], sy = d[0x69];                  // the fetch hook reads the plane at $67/$69;
    d[0x67] = d[0x71]; d[0x69] = d[0x73];                //   point it at the DOOR tile, read, restore
    uint8_t q0 = 0, q1 = 0, q2 = 0;
    int off = g_collFetch(ram, &q0, &q1, &q2);           // (9D3F does no off-map test; treat it as no door)
    d[0x67] = sx; d[0x69] = sy;
    if (!off && (q1 & 0x10)) {                           // 9D43 BIT #10
      int di = (((d[0x73] & d[0x1F]) << 8) | (d[0x71] & d[0x1E])) & 0xFFFF;
      ram[0x015B] = (uint8_t)di; ram[0x015C] = (uint8_t)(di >> 8);   // 9D47 STX $015B
      ram[0x0117] |= 0x10;                                            // 9D4A/9D4F ORA #10
    }
  }
  if (d[0x6C] == 0) {                                               // 9D54 plane update (unless IGNORE_Z)
    ram[0x0155] = d[0x6A];
    uint8_t px = ram[0x0197];
    ram[0x0C00 + px] = d[0x6E]; ram[0x0C01 + px] = d[0x6F];
    g_collPairCommit = 1;                                           // (the host mirrors the lead's pair into its WRAM)
  }
  d[0x34] = d[0x5E];                                                // 9D6A
  uint8_t sel = (uint8_t)(d[0x5A] & 0x0C);                          // 9D6E conveyor
  if (sel == 0) { d[0x2A] = 0; d[0x2B] = 0; return; }               // 9DBE
  uint8_t speed = (sel == 0x04) ? 0x08 : (sel == 0x08) ? 0x10 : 0x20;
  d[0x34] |= 0x08;
  switch (d[0x5A] & 0x03) {
    case 0: d[0x2B] = (uint8_t)(0 - speed); d[0x2A] = 0; break;     // up
    case 1: d[0x2B] = speed;                d[0x2A] = 0; break;     // down
    case 2: d[0x2A] = (uint8_t)(0 - speed); d[0x2B] = 0; break;     // left
    case 3: d[0x2A] = speed;                d[0x2B] = 0; break;     // right
  }
}

// The C0/8A9E probe core: prop-fetch (9AA1) -> shape-decode (9AD3) -> z-gate (9C37).
int CtCollisionSolidAt(uint8_t* ram, int* offMapOut) {
  uint8_t* d = ram + 0x1D00;
  uint8_t b0 = 0, b1 = 0, b2 = 0;
  // 9AA1-9AB0: a treasure tile ($7E70C0 bit 7 clear) is solid to the player, as off-map is. C0/A712 builds
  // the plane at every load whether or not the chest is open, so an opened chest still blocks.
  // Only C0/9AA1 makes this test; the fetch at 9AC8 skips it.
  { uint16_t X = (uint16_t)((d[0x67] | (d[0x69] << 8)) & (d[0x1E] | (d[0x1F] << 8)));
    if (!(ram[0x70C0 + X] & 0x80)) { if (offMapOut) *offMapOut = 1; return 1; } }
  int off = g_collFetch(ram, &b0, &b1, &b2);                        // 9AB2 prop fetch (+ off-map sentinel)
  if (offMapOut) *offMapOut = off;
  if (off) return 1;                                                // off-map -> SOLID
  d[0x58] = b0; d[0x5A] = b1; d[0x5C] = b2;
  CtCollisionShapeDecode(ram);                                      // 9AD3 -> $1D6A/$6C/$6E/$6F (+$30)
  return CtCollisionZGate(ram);                                     // 9C37 -> solid?
}
