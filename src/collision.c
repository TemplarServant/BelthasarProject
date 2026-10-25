// collision.c -- CT field tile-collision decode (see collision.h + disasm/notes/collision.md).
// Ported byte-exact from the adversarially-verified disassembly. DP = $1D00 throughout, so the
// source "$XX" maps to ram[0x1D00 + XX]; absolute operands ($0155 etc.) are ram[that addr].
#include "collision.h"
// Set by the commit C0/9D54-9D67 when it writes the lead's plane and OAM priority pair ($6C == 0): the host keeps
// the collision RAM apart from its scene WRAM and copies them across (field.c fieldPlayerCollide).
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

// Pure shape-solidity verdict (the walk-vs-solid TAIL decision of C0/9AD3, byte-exact). `shapeFull` =
// (byte0 & 0xFC) >> 2 (0..0x3F). `subX` = $1D66, `subY` = $1D68: the sub-tile position low bytes (0..255,
// i.e. 256 subpixels per 16px tile). Returns 1 = SOLID (the 9B62 tail), 0 = walkable (9B41). This is the
// engine's real collision shape (slope tables + one-way), shared by CtCollisionShapeDecode and usable
// standalone (e.g. the interactive field). The conveyor (orig 0x1E) reports walkable here; its $1D30/
// $1D5E side effects live only in CtCollisionShapeDecode.
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

// ---- C0/5B95 actor-vs-actor "movability" test for the NPC move-step (C0/5129->5170). Returns 1 =
// BLOCKED (a solid actor sits in the 1-tile box ahead in the mover's facing), 0 = movable. Gated by
// $1C80[mover]&0x02 (collision-enable); when off it returns movable without scanning (the field default).
// Scan stride = idx (actorNum*2), same as the rest of the mover. The 4 facing box-handlers (C0/5BFA up /
// 5C20 down / 5C47 left / 5C6D right) test |dprimary|<0x100 in the facing dir AND |dlateral|<0x100, with
// the ROM's carry quirks: the primary SBC inherits carry from `CPX $6D` (candIdx>=moverIdx), the lateral
// SBC from `CMP #$0100` (clear), and abs uses one's-complement (EOR #$FFFF, no INC). ----
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
  // 5B95 `STZ $EA` -- the routine's FIRST instruction, before any test, so it fires on every path
  // including both early exits. DP = $0100, so it clears $01EA. This was missing for as long as the
  // routine has been ported and nothing could see it: --sweepops compares the whole 128 KB image,
  // but $01EA already happened to hold 0, and a write of 0 over a 0 is indistinguishable from no
  // write at all. It surfaced only when the mode's poison audit was taught to flag oracle writes
  // that do not CHANGE the value, and $01EA was then seeded to $5A -- at which point op 0x96 went
  // 60/60 -> 30/60. Grading a byte is not the same as poisoning it first.
  r[0x01EA] = 0x00;
  if (r[0x0F00 + moverIdx] == 0) return 0;                // 5B99 mover inactive -> movable
  if (!(r[0x1C80 + moverIdx] & 0x02)) return 0;           // 5BA0/5BA3 GATE clear -> movable
  // 5BA7-5BB9 -- FOUR DP SPILLS THE PORT NEVER MADE. They were invisible while --sweepops drove
  // this routine only through ops 0x92/0x96/0x9C, which never reached a live candidate; the mover
  // neighbourhood (0x8F/0x97/0x98/0x99/0x9D/0x9E/0x9F/0xA1) does, and they surfaced at once --
  // measured `$001D9: C=5A RO=00`.
  // 5BA7 `STY $C7` is NOT modelled: it spills the CALLER's Y, and every caller in this engine is a
  // handler holding Y = its own ip which then writes $01C7 = ip itself after this returns. Same
  // value, later write, so the spill is unobservable -- and reproducing it would mean threading an
  // ip parameter through a routine the ROM gives no such argument.
  // ⚠ 5BAC's `ASL A` RUNS ON THE RAW BYTE -- there is no AND. A facing outside 0..3 therefore
  // indexes past the four-entry table that 5BE6 `JSR ($5BF2,X)` dispatches through, and the ROM
  // jumps into data. The engine only ever stores 0..3 here, so the two agree in practice, but the
  // spill itself must carry the UNMASKED product.
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
      // 5BE2 `STX $E9` -- the candidate index is spilled before `5BE6 JSR ($5BF2,X)` and reloaded
      // by `5BE9 LDX $E9` afterwards, because the dispatch clobbers X. Missed for the same reason
      // $01EA above was: nothing poisoned it, so the port's silence compared equal.
      r[0x01E9] = (uint8_t)X;
      int carryIn = (X >= moverIdx) ? 1 : 0;              // carry from CPX $6D into the box handler's 1st SBC
      if (collActorBox(facing, rd16(r, 0x1800 + X), rd16(r, 0x1880 + X), moverX, moverY, carryIn)) return 1;
    }
    X = (X - 2) & 0xFF;
  }
  return 0;                                               // 5BD0 fall-through -> movable
}

// ---- C0/8A6D player tile-collision: directional feeler/slide handlers around the validated probe
// (8A9E) + commit (9C5C). 16-bit ADC/SBC mirrored with an explicit carry so the ROM's carry-chained
// feeler math is byte-exact (cy=1 == SEC, cy=0 == CLC). Each handler nets to the clamped $1D2E/$1D30. ----
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

// Diagonal handlers: corner-box probes with cardinal fallbacks. The carry-chained feeler math varies
// per site (some `SEC;SBC#70;ADC$2E` chain the SBC carry into the ADC, others `...;CLC;ADC$2E` reset it)
// -- transcribed exactly. Each falls back to a cardinal handler (zeroing one axis first) when boxed.
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

// C0/8A6D dispatch (gated by $0120!=0 at the call site). Dispatches on the sign of the 8-bit velocity
// accumulators $1D2E/$1D30 to a directional handler; the handler nets to the clamped $1D2E/$1D30.
// ALL EIGHT arms are ported and EXHAUSTIVELY graded: --sweepclamp micro-executes the real 8A6D over a
// position grid x 49 velocity vectors x both entry-carry values (16562 combos, every arm reached
// >= 1014 times) and compares velocity + committed position + plane + the $01EB touch latch + the
// feeler scratch -- 16562/16562 byte-exact. (The older in-situ --checkclamp only reached whatever a
// walk happened to hit, which could not tell a correct diagonal from an unexercised one.)
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

// Port of the actor-proximity probe C0/9923 (DP=$1D00; A/X 8-bit at the per-actor compares). Scans the
// actor list (count at $7F2000, X = count*2 stepping down by 2), skipping inactive ($0F00,X==0), hidden
// ($1100,X bit7), and self/party ($0197/$0199/$019B). For the first remaining actor within the |dy|<$E0
// && |dx|<$E0 box (dy/dx = actor pos $1880,X/$1800,X minus the probe point $1D68/$1D66), returns its
// $1B01,X bit0: bit0 SET = BLOCKED (1), bit0 CLEAR = explicitly CLEAR (0, and stops scanning). If no
// actor is in range, returns 0 (clear). NOTE the byte-exact carry quirk: the dy SBC inherits its carry
// from the preceding `CPX $019B` (no SEC), so dy = $1880,X - $1D68 - (X < $019B ? 1 : 0); the dx SBC
// inherits a CLEARED carry from the `CMP #$E0`, so dx = $1800,X - $1D66 - 1.
int CtCollisionActorBlock(uint8_t* ram) {
  uint8_t* d = ram + 0x1D00;
  // $01EB = THE TOUCH HIT LATCH (C0/9925-9928 `LDA #0080 / STA $01EB` on entry, `STX $01EB` at 9960
  // for a solid hit and 99A1 for a non-solid one). The event-VM's op 0xB0 consumes it later in the
  // same frame (C0/3054 -> C0/3154) -- the ROM's ONE scan drives both the player BLOCK and the touch,
  // which is why the port used to need a second, hand-rolled box in field.c.
  // NOTE the "last scan wins" semantics, which are the ROM's: the eight directional callers in
  // C0/8A6D-C0/90BF can run this several times per frame and each entry re-arms 0x80, so a hit
  // recorded by an early probe can be cleared by a later one (writer C0:9941 is observable on
  // non-firing frames). Mirror it exactly rather than latching the first hit.
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

// Port of the per-axis collision COMMIT C0/9C5C (DP=$1D00). Computes the tentative next position
// $1D66/$1D70 = $1D62 + $1D2E (X, 16-bit incl the sign byte $1D2F) and $1D68 = $1D64 + $1D30 (Y);
// fetches the cell props UNCHECKED (C0/9AC8, no off-map sentinel) at the tentative tile, decodes the
// shape (9AD3), z-gates (9C37). If SOLID -> zeroes the velocity accumulators $1D2E/$1D2F/$1D30/$1D31
// (the wall-stop). Else updates the player plane $0155 + OBJ priority $0C00/$0C01[$0197] (gated by
// IGNORE_Z $1D6C) and applies the conveyor (byte1 $1D5A bits) into the momentum seed $1D2A/$1D2B.
// The C0/9CA1 touch-trigger / door-on-step side-state ($0112-$0116/$015B/$0117) is NOT modelled here
// (it does not affect position/velocity); port it separately before relying on triggers.
// Tile-property fetch for the collision probe: given the tentative tile ($1D67=tileX hi, $1D69=tileY hi),
// return the 3 prop bytes b0/b1/b2 and 1 if the tile is OFF-MAP (else 0). Default reads the engine's
// de-interleaved WRAM planes ($7E30C0/$7E7000/$7E7040, treasure plane $7E70C0, masked by $1D1E/$1D1F). The
// interactive field overrides it (CtCollisionSetTileFetch) to read its asset shp/zb directly.
static int collFetchEngine(uint8_t* ram, uint8_t* b0, uint8_t* b1, uint8_t* b2) {
  uint8_t* d = ram + 0x1D00;
  uint16_t X = (uint16_t)((d[0x67] | (d[0x69] << 8)) & (d[0x1E] | (d[0x1F] << 8)));
  // (The $7E70C0 TREASURE-plane test that used to sit here has moved to CtCollisionSolidAt: it is the
  //  head of C0/9AA1 ONLY. C0/9AC8 -- the same fetch reached by the COMMIT C0/9C5C and by the arrival
  //  evaluation C0/9DDE -- is literally 9AA1 minus that test (`REP #10 / REP #20 / AND $1E / SEP #20 /
  //  TAX / BRA ->9AB2`), so a shared fetch that tested it made the commit solid on a chest tile too.)
  *b0 = ram[0x30C0 + X]; *b1 = ram[0x7000 + X]; *b2 = ram[0x7040 + X];
  return 0;
}
static int (*g_collFetch)(uint8_t*, uint8_t*, uint8_t*, uint8_t*) = collFetchEngine;
void CtCollisionSetTileFetch(int (*fn)(uint8_t*, uint8_t*, uint8_t*, uint8_t*)) {
  g_collFetch = fn ? fn : collFetchEngine;
}

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
  // ---- C0/9D35: THE DOOR-ON-STEP LATCH ----------------------------------------------------
  // `LDA $73 / AND $1F / XBA / LDA $71 / AND $1E / TAX` builds the tile index (row << 8) | col
  // from the pair $1D71/$1D73 -- the tile AHEAD in the direction of travel, which 9C5E/9C70
  // already computed into $1D70/$1D72 above -- masked by the map dimensions $1D1E/$1D1F, exactly
  // as collFetchEngine masks $1D67/$1D69. Then `LDA $7E7000,X / BIT #10 / BEQ` -> prop1 bit4 is
  // the DOOR flag; on a hit it stores the index in $015B and raises $0117 bit4. The consumer is
  // C0/0D3E, which clears the bit, reads the L1 metatile at $015B and bumps the door's tile block
  // (see CtFieldSceneDoorOpen). This is the whole of the 9CA1 side-state that has a VISIBLE
  // effect; the exit-staging half ($0112-$0116 + $0117 bit7) stays with the host's own exit
  // records. Without it doors never opened when walked into ("doors like the one in scene 4 are not
  // opening when walking through").
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
  // ⭐ THE HEAD OF C0/9AA1: A TREASURE TILE IS IMPASSABLE TO THE PLAYER, and this is the only thing
  // that makes it so. The two prop-fetch entries differ in exactly this test --
  //   9AA5 AND $1E / TAX / 9AAA LDA $7E70C0,X / 9AAE BMI ->9AB2 / 9AB0 SEC / RTS
  // -- and 9AA1's ONE caller is C0/8AA4 inside the player feeler helper C0/8A9E, which the eight
  // direction routines dispatched by C0/8A6D reach from 51 sites. Carry set returns A = 1, the same
  // hard-solid tri-state as off-map. The plane is built by C0/A712 at every location load (bit7 set
  // everywhere, then the record's slot stored at `tile & $1D1E/$1D1F`, UNCONDITIONALLY -- the isOpened
  // test at A782 gates only the metatile pre-bump), so an opened chest still blocks.
  // Without it the player walks ONTO the chest tile and the treasure probe, which looks at the tile in
  // FRONT of him, then points past the chest: 'some chests do not open', defect #3 of the 2026-09-02
  // list. 89 of the 256 shipped records sit on a tile whose own shape/z data is not solid.
  { uint16_t X = (uint16_t)((d[0x67] | (d[0x69] << 8)) & (d[0x1E] | (d[0x1F] << 8)));
    if (!(ram[0x70C0 + X] & 0x80)) { if (offMapOut) *offMapOut = 1; return 1; } }
  int off = g_collFetch(ram, &b0, &b1, &b2);                        // 9AB2 prop fetch (+ off-map sentinel)
  if (offMapOut) *offMapOut = off;
  if (off) return 1;                                                // off-map -> SOLID
  d[0x58] = b0; d[0x5A] = b1; d[0x5C] = b2;
  CtCollisionShapeDecode(ram);                                      // 9AD3 -> $1D6A/$6C/$6E/$6F (+$30)
  return CtCollisionZGate(ram);                                     // 9C37 -> solid?
}
