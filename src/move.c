// move.c -- field objects: movement, the event-script runner and its opcode handlers, and the sprite
// animation pipeline (enqueue, drain, release, draw). See move.h and disasm/notes/field_tick.md.
#include <stdio.h>
#include "romview.h"     // the ROM's small tables from ct_assets.dat (CT_ASSET_TABLE)
// A byte of the ROM the sprite engine reads (the sprite tables, frames, palettes, tiles): the container's named
// table when one covers it (CT_ASSET_TABLE, romview.h), else the ROM's.
#define MR(o) CtRomByte(rom, 0x400000L, (long)(o))
#include <stdlib.h>
#include "move.h"
#include "oam.h"   // CtMoveDirAngle (the atan2 heading, C0/ABA2)
#include "collision.h"   // CtCollisionActorMovable (the 5B95 actor-collision)
#include "lz.h"          // ct_lz_decompress = the ported JSL C3/0002 the object loader calls
#include "ct_vbeam.h"    // the anim drain's virtual beam (CtAnimDrainPass, scanline < 0)

// One velocity component through the hardware multiplier: AD08 STA $4202 = speed, $4203 = |table value|, and
// the ROM reads $4217, the product's high byte: (speed * |t|) >> 8. The sign comes from the angle index
// (AD10 CPX #80, X = the $C0:F800 index), not from the table byte; the two differ only at index $80, whose
// byte is $00, so the results agree.
static int acfdMul(const uint8_t* rom, int sp, int ang) {
  int t = MR(0xF800 + (ang & 0xFF));
  int neg = (ang & 0xFF) >= 0x80;                         // AD10 CPX #80 / AD3D CPX #80
  int mag = neg ? ((-t) & 0xFF) : t;                      // AD22 EOR #FF / AD24 INC A
  int p = ((sp * mag) >> 8) & 0xFF;                       // AD1B/AD2C LDA $4217
  return neg ? ((-p) & 0xFF) : p;                         // AD2F EOR #FF / AD31 INC A
}

// C0/AC69: the plain velocity builder, used only by op 0x7A (its one caller is C0/4D4B). It opens like C0/ACFD
// but stores straight into $1900/$1980, with no priority or bounce logic; the arithmetic (acfdMul) is shared.
// Y uses the heading, X the heading + $40, each sign-extended into its high byte. The two STZ at AC6B/AC6E are
// always overwritten.
static void ctMoveVelAC69(uint8_t* r, const uint8_t* rom, int idx) {
  r[0x1900 + idx] = 0x00; r[0x1980 + idx] = 0x00;          // AC6B / AC6E
  int sp = r[0x1A00 + idx];                                 // AC71 -> $4202
  int ang = r[0x01EE];                                      // AC77 LDX $EE
  int vy = acfdMul(rom, sp, ang);                           // AC79-ACA9
  r[0x01DB] = (uint8_t)vy;                                  // AC8C / ACA5 STA $DB
  r[0x1980 + idx] = (uint8_t)vy;
  r[0x1981 + idx] = (uint8_t)((vy & 0x80) ? 0xFF : 0x00);   // ACAF / ACB6
  int vx = acfdMul(rom, sp, (ang + 0x40) & 0xFF);           // ACB9-ACE9 -- the quarter turn
  r[0x1900 + idx] = (uint8_t)vx;
  r[0x1901 + idx] = (uint8_t)((vx & 0x80) ? 0xFF : 0x00);   // ACF3 / ACF9
}

// ---- C0/ACFD: field velocity + priority builder ----
// Twelve callers in bank $C0 (4EBD 4F03 4F5A 4FE0 507A 511F 518A 520E 52B6 5343 5406 54EB); op 0x92 calls it
// at 4EBD and op 0x96 at 4FE0. Includes the $1C80,Y bit-0 gate, the bounce retry and the DP staging bytes
// $01D9/$01DB-$01E1/$01E5.
// ACFD SEP #10 makes X/Y 8-bit for the body and ADF7/AF24 REP #10 restores them (op 0x92's 4EC3 LDX $C7 relies
// on it). The body exists twice: ACFD..ADF9, and a near-copy AE37..AF26 re-entered after a bounce; the
// differences are marked below.
// The cell index is built in the accumulator (AD8D-AD97): the tentative Y's high byte stays in B while
// LDA $E0 loads the tentative X's high byte, and REP #30 joins them as tileY*256 + tileX before AND $1D1E.
// C0/AF30 is not part of this routine: it has no static caller in the ROM and only branches into the second
// copy (AF46/AF4B -> AE97).
void CtBuildVelocityPriority(uint8_t* r, const uint8_t* rom, int idx) {
  r[0x1900 + idx] = 0x00;                                 // ACFF STZ $1900,X
  r[0x1980 + idx] = 0x00;                                 // AD02 STZ $1980,X
  int sp = r[0x1A00 + idx];                               // AD05 LDA $1A00,X / AD08 STA $4202
  for (int pass = 0; ; pass++) {
    // The two components: $DB = Y (angle), $DD = X (angle + $40).
    int ang = r[0x01EE];                                  // AD0B LDX $EE   |  AE3F on the retry
    r[0x01DB] = (uint8_t)acfdMul(rom, sp, ang);            // AD0D..AD32    |  AE41..AE6A
    r[0x01DD] = (uint8_t)acfdMul(rom, sp, (ang + 0x40) & 0xFF);  // AD34..AD5D  |  AE6B..AE96
    // Sign-extension bytes for the 16-bit adds below.
    int dd = r[0x01DD], db = r[0x01DB];
    if (dd == 0) {                                        // AD61 BEQ ->AD6F  |  AE99 BEQ ->AEA7
      // First difference: only the first copy has AD6F STZ $DE on this path (the second goes to AEA7 LDA $DB).
      if (pass == 0) r[0x01DE] = 0x00;                    // AD6F only
      if (db == 0) break;                                 // AD73 BEQ ->ADEB  |  AEA9 BEQ ->AF18
    } else r[0x01DE] = (uint8_t)((dd & 0x80) ? 0xFF : 0x00);   // AD65/AD67 or AD6B  |  AE9D/AE9F or AEA3
    r[0x01DC] = (uint8_t)((db & 0x80) ? 0xFF : 0x00);     // AD77..AD7F  |  AEAD..AEB5
    // The tentative position and its cell.
    int wx = r[0x1800 + idx] | (r[0x1801 + idx] << 8);    // AD85 LDA $1800,X (A is 16-bit here)
    int tx = (wx + (r[0x01DD] | (r[0x01DE] << 8))) & 0xFFFF;   // AD89 ADC $DD, a 16-bit add
    r[0x01DF] = (uint8_t)tx; r[0x01E0] = (uint8_t)(tx >> 8);   // AD8B STA $DF, a 16-bit store
    int wy = r[0x1880 + idx] | (r[0x1881 + idx] << 8);    // AD8D
    int ty = (wy + (r[0x01DB] | (r[0x01DC] << 8))) & 0xFFFF;   // AD91 ADC $DB (not stored)
    int cell = ((((ty >> 8) & 0xFF) << 8) | ((tx >> 8) & 0xFF)) & 0xFFFF;   // AD93/AD95/AD97, see above
    cell &= (r[0x1D1E] | (r[0x1D1F] << 8));               // AD99 AND $1D1E (16-bit)
    // The two property planes (long,X reads, so they cross the bank boundary), read on every arm before any test.
    r[0x01E1] = r[0x7000 + cell];                         // ADA8/ADAC  |  ADFA/ADFE  |  AED5/AED9
    r[0x01E5] = r[0x7040 + cell];                         // ADAE/ADB2  |  AE00/AE04  |  AEDB/AEDF
    if (pass == 0) {
      // AD9F-ADA6: the $1C80,Y bit-0 gate decides only whether the bounce test at ADB4 runs; both arms compute
      // the priority the same way.
      if ((r[0x1C80 + idx] & 1) && (r[0x01E5] & 0x80)) {  // ADB4 BMI ->AE27
        r[0x01F8] = (uint8_t)(r[0x01F8] + 1);             // AE29 INC $F8
        r[0x01EE] = CtRomByte(rom, 0x10000, 0xFE00 + r[0x01F8]);   // AE2D LDA $FE00,X / AE31 STA $EE
        continue;                                          // AE34 BRL ->AE37 -- run the second copy
      }
    } else if (r[0x01E5] & 0x80) {
      // Second difference: the retry has no $1C80 gate, and its BMI (AEE1 -> AF27) zeroes both components and
      // stores them, so a second blocked cell ends the step instead of bouncing again.
      r[0x01DB] = 0x00; r[0x01DD] = 0x00;                 // AF29 STZ $DB / AF2B STZ $DD
      break;                                              // AF2D BRL ->AF18
    }
    // Priority.
    int p = r[0x0F80 + idx];                              // ADB6 LDA $0F80,Y  |  AE06  |  AEE3
    if (p & 0x80) {                                       // ADB9 BPL ->ADD7  |  AE09  |  AEE6
      // Not crossed: $0C01 takes the $7E7040 plane, $0C00 the $7E7000 one. Ops 0x8B (C0/4B74) and 0x8E
      // (C0/4C74) cross them.
      r[0x0C01 + idx] = (uint8_t)(((r[0x01E5] & 0x40) >> 2) | 0x20);   // ADBD..ADC7
      r[0x0C00 + idx] = (uint8_t)(((r[0x01E1] & 0x40) >> 2) | 0x20);   // ADCA..ADD2
    } else {
      r[0x01D9] = (uint8_t)p;                             // ADD7 STA $D9
      r[0x0C01 + idx] = (uint8_t)(p & 0x30);              // ADDB AND #30 / ADDD
      r[0x0C00 + idx] = (uint8_t)((p << 4) & 0x30);       // ADE2-ADE6 four 8-bit ASLs / ADE8
    }
    break;
  }
  r[0x1900 + idx] = r[0x01DD];                            // ADEB/ADED/ADEF  |  AF18/AF1A/AF1C
  r[0x1980 + idx] = r[0x01DB];                            // ADF2/ADF4       |  AF1F/AF21
}

// C0/AA07, one axis of the clamp path (AA29 normal / AA7E clamp): adds the signed 8-bit velocity to the position.
// On a tile crossing, normal crosses (tile +/-1); clamp stops the counter ($1A01 = 0) and stays, the one-frame
// pause before crossing. lo/hi = the position byte addresses.
static void aa07Axis(uint8_t* r, int idx, int lo, int hi, int vel8, int clamp) {
  int vel = (int8_t)vel8;
  if (vel < 0) {
    int v = r[lo] - ((-vel) & 0xFF);                       // SEC; SBC #|vel|
    if (v < 0) { if (clamp) r[0x1A01 + idx] = 0; else { r[lo] = (uint8_t)v; r[hi]--; } }  // borrow -> cross
    else r[lo] = (uint8_t)v;
  } else {
    int v = r[lo] + vel;                                   // CLC; ADC #vel
    if (v > 0xFF) { if (clamp) r[0x1A01 + idx] = 0; else { r[lo] = (uint8_t)v; r[hi]++; } }  // carry -> cross
    else r[lo] = (uint8_t)v;
  }
}

// C0/AA07: position integration for states 3..6 (the A82A branch; AAFD is the simple add for state < 3).
// $1B80,X != 0 -> AAC7, a 16-bit add with the $1B81 Y sub-pixel accumulator; == 0 -> AA11, the per-axis
// integrate with the $7F0B00,X tile clamp, which makes the one-frame pause at each tile boundary.
void CtActorIntegrate(uint8_t* r, int idx) {
  if (r[0x1B80 + idx] != 0) {                              // AA07 -> AAC7 simple add
    if (r[0x1A01 + idx] == 0) return;
    r[0x1A01 + idx]--;
    int x = ((r[0x1800 + idx] | (r[0x1801 + idx] << 8)) + (r[0x1900 + idx] | (r[0x1901 + idx] << 8))) & 0xFFFF;
    r[0x1800 + idx] = (uint8_t)x; r[0x1801 + idx] = (uint8_t)(x >> 8);
    int yf = r[0x1980 + idx] + r[0x1B81 + idx];            // $1980 += $1B81 (sub-pixel), carry -> $1981
    if (yf > 0xFF) r[0x1981 + idx] = (uint8_t)(r[0x1981 + idx] + 1);
    r[0x1980 + idx] = (uint8_t)yf;
    int y = ((r[0x1880 + idx] | (r[0x1881 + idx] << 8)) + (r[0x1980 + idx] | (r[0x1981 + idx] << 8))) & 0xFFFF;
    r[0x1880 + idx] = (uint8_t)y; r[0x1881 + idx] = (uint8_t)(y >> 8);
    return;
  }
  if (r[0x1A01 + idx] == 0) return;                        // AA11
  r[0x1A01 + idx] = (uint8_t)(r[0x1A01 + idx] - 1);        // AA17 DEC counter
  int b00 = r[0x10B00 + idx];                              // $7F0B00,X (move-duration mirror)
  int clamp;
  if (b00 == 0) clamp = 1;                                 // AA1F: ==0 -> clamp path
  else { if (!(b00 & 0x80)) r[0x10B00 + idx] = 0; clamp = 0; }  // AA23: >0 -> zero it; <0 -> leave; both normal
  aa07Axis(r, idx, 0x1800 + idx, 0x1801 + idx, r[0x1900 + idx], clamp);   // X
  aa07Axis(r, idx, 0x1880 + idx, 0x1881 + idx, r[0x1980 + idx], clamp);   // Y
}

// C0/305D: the arrival snap, reached from C0/515F when the move has reached its target tile and $1C81,X bit 0 is
// set. Zeroes both velocities and moves the object one pixel per frame toward the tile's X centre and Y bottom,
// arming $1A01 = 1. `$1800,X & $F0` is the pixel within the tile << 4 ($80 = centre, $F0 = bottom); #10/#F0 are
// +/-1 px. Both axes can be armed in one call (307B/3082 fall through to the Y test).
// Returns 1 = SEC (still snapping, the caller yields), 0 = CLC (in place, the move is done).
static int ctArriveSnap305D(uint8_t* r, int idx) {
  r[0x1900 + idx] = 0;                                    // 305F STZ $1900,X
  r[0x1980 + idx] = 0;                                    // 3062 STZ $1980,X
  int xs = r[0x1800 + idx] & 0xF0;                        // 3065 LDA $1800,X / AND #F0
  int ys = r[0x1880 + idx] & 0xF0;                        // 306E / 3089 LDA $1880,X / AND #F0
  if (xs == 0x80 && ys == 0xF0) return 0;                 // 3077 CLC -- centred and on the bottom
  if (xs != 0x80) r[0x1900 + idx] = (uint8_t)((xs < 0x80) ? 0x10 : 0xF0);   // 307B / 3082
  if (ys != 0xF0) r[0x1980 + idx] = (uint8_t)((ys < 0xF0) ? 0x10 : 0xF0);   // 309B / 30A7
  r[0x1A01 + idx] = 0x01;                                 // 3092 / 30A0 / 30AE: one frame
  return 1;                                               // SEC
}

int ctSweepSnapCall(uint8_t* r, int idx) { return ctArriveSnap305D(r, idx); }

int CtActorMoveStep(uint8_t* r, const uint8_t* rom, int idx) {
  if (r[0x1A01 + idx] != 0) return 0;                     // counter still running -> yield (C0/5129)
  int ip = r[0x1180 + idx] | (r[0x1181 + idx] << 8);      // 16-bit event-script IP
  int oa = 0x12001 + ip;                                  // $7F2001 + IP
  int tgtX = r[oa + 1], tgtY = r[oa + 2];                 // the move target (tile coords, operands)
  int curX = r[0x1801 + idx], curY = r[0x1881 + idx];     // current tile (world hi-byte)
  if (curX == tgtX && curY == tgtY) {                     // 514E-5156: tile-index equality
    // 5158 LDA $1C81,X / BIT #01: with bit 0 set (op 0x0E sets it) the arrival runs the snap and yields
    // (515F JSR C0/305D / 5162 BCS) until the object is in place.
    if ((r[0x1C81 + idx] & 1) && ctArriveSnap305D(r, idx)) return 0;        // 515F/5162 -> yield
    return 1;                                             // 5164 TYX/INX x3/SEC: complete, IP += 3
  }
  // 5140-514C stage the bytes C0/ABA2 reads ($F0/$F1 = the current tile, $F2/$F3 = the target); ABA2 does its
  // own unsigned subtraction on them.
  r[0x01F0] = (uint8_t)curX; r[0x01F1] = (uint8_t)curY;   // 5147 / 514C
  r[0x01F2] = (uint8_t)tgtX; r[0x01F3] = (uint8_t)tgtY;   // 5140 and its sibling
  int ee = CtMoveDirAngle(r, rom);                        // 516D JSR C0/ABA2 -- writes $01EE itself
  // ABA2 wrote $01EE (DP = $0100); only the collision nudge re-writes it.
  if (CtCollisionActorMovable(r, idx)) { ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee; }  // C0/5170: actor-collision heading nudge
  r[0x1A01 + idx] = r[0x1000 + idx];                      // reload the move counter from the move-duration
  r[0x10B00 + idx] = r[0x1000 + idx];                     // mirror to $7F0B00
  CtBuildVelocityPriority(r, rom, idx);
  return 0;                                               // yield (the step now runs)
}

// C0/5614: the scripted mover's anim-mode selector, run at step issue by the move opcodes (op 0x96 at 4FA5 and
// 4FE3); the counterpart of the player's C0/9E84, keyed on the velocity just built.
//   cfg = $1780,X: 0 -> the speed test; 3 -> if $1680,X != $FF, $1780,X = 1 and return, else $1780,X = 0 and
//   fall through (562D); any other value returns.
//   5630 speed = |$1900,X| + |$1980,X| in 8 bits (0x80 + 0x80 wraps to 0, standing); 5638 STA $D9 leaves |velX|
//   in $01D9. speed 0 -> mode 0 (idle); < 0x20 -> mode 1 (walk); else $1100,X >= 4 ? mode 1 : mode 6.
// Every arm ends in C0/9E84's CMP-guarded tail: an unchanged mode resets neither phase nor timer.
static void ctMoveStepAnim5614(uint8_t* r, int idx);
void CtVmMoveStepAnim(uint8_t* ram, int idx) { ctMoveStepAnim5614(ram, idx); }   // the live VM's step-issue (ct_vm.c mvStartStep)
static void ctMoveStepAnim5614(uint8_t* r, int idx) {
  int cfg = r[0x1780 + idx];                              // 5616
  if (cfg != 0) {                                         // 5619 BEQ ->5630
    if (cfg != 3) return;                                 // 561B CMP #03 / 561D BEQ / 561F RTS
    if (r[0x1680 + idx] != 0xFF) {                        // 5620 / 5623 CMP #FF / 5625 BEQ ->562D
      r[0x1780 + idx] = 0x01;                             // 5627 LDA #01 / 5629 STA $1780,X
      return;                                             // 562C
    }
    r[0x1780 + idx] = 0x00;                               // 562D STZ $1780,X, then falls through
  }
  int vx = r[0x1900 + idx]; if (vx & 0x80) vx = ((vx ^ 0xFF) + 1) & 0xFF;   // 5630-5637
  r[0x01D9] = (uint8_t)vx;                                // 5638 STA $D9 (DP = $0100)
  int vy = r[0x1980 + idx]; if (vy & 0x80) vy = ((vy ^ 0xFF) + 1) & 0xFF;   // 563A-5641
  int sp = (vy + vx) & 0xFF;                              // 5642 CLC / 5643 ADC $D9 -- 8-bit
  int k;
  if (sp == 0)          k = 0x00;                         // 5645 BEQ ->565E
  else if (sp < 0x20)   k = 0x01;                         // 5647 CMP #20 / 5649 BCS ->5671
  else                  k = (r[0x1100 + idx] >= 4) ? 0x01 : 0x06;   // 5671/5674/5676 BCS ->564B
  if (r[0x1680 + idx] == (uint8_t)k) return;              // 564B/565E/5678: the CMP guard
  r[0x1680 + idx] = (uint8_t)k;                           // 5652/5665/567F
  r[0x1681 + idx] = 0x00;
  r[0x1601 + idx] = 0x00;
}

// C0/B096: the 2D block move behind op 0xE4. Copies a tile rectangle within bank $7E one row at a time with
// MVN $7E,$7E. The rect is staged in DP: $013E/$013F = (x1,y1), $0140/$0141 = (x2,y2) inclusive, $0142/$0143 =
// (xd,yd). `base` is the plane the caller selected. A row is base + y*$100 + x, so the 16-bit LDA $3E is already
// the offset; the row advance backs up by width+1 and adds $100.
static void ctB096(uint8_t* r, int base) {
  r[0x01DC] = 0x00;                                   // B096 STZ $DC -- 8-bit, A is narrow here
  r[0x01DE] = 0x00;                                   // B098 STZ $DE
  r[0x01DB] = (uint8_t)((r[0x0140] - r[0x013E]) & 0xFF);   // B09A/B09D SEC/SBC -- 8-bit, width
  r[0x01DD] = (uint8_t)((r[0x0141] - r[0x013F]) & 0xFF);   // B0A1/B0A4 -- 8-bit, height
  // B0A8 REP #20: 16-bit from here to B0E3.
  int src = ((r[0x013E] | (r[0x013F] << 8)) + base) & 0xFFFF;   // B0AA/B0AD/B0AF TAX
  { int v = src & 0x00FF;                             // B0B0 AND #00FF / B0B3 STA $DF -- 16-bit; not read
    r[0x01DF] = (uint8_t)v; r[0x01E0] = (uint8_t)(v >> 8); }   // here, but still a write
  int dst = ((r[0x0142] | (r[0x0143] << 8)) + base) & 0xFFFF;   // B0B5/B0B8/B0BA TAY
  { int v = dst & 0x00FF;                             // B0BB / B0BE STA $E1 -- 16-bit, also dead
    r[0x01E1] = (uint8_t)v; r[0x01E2] = (uint8_t)(v >> 8); }
  for (;;) {
    int n = r[0x01DB] | (r[0x01DC] << 8);             // B0C0 LDA $DB -- 16-bit
    for (int k = 0; k <= n; k++)                      // B0C3 MVN moves A+1 bytes
      r[(dst + k) & 0xFFFF] = r[(src + k) & 0xFFFF];  //   both banks are $7E, so offset 0 in the image
    src = (src + n + 1) & 0xFFFF;                     // MVN leaves X and Y at end+1
    dst = (dst + n + 1) & 0xFFFF;
    int hh = r[0x01DD] | (r[0x01DE] << 8);            // B0C7 LDA $DD -- 16-bit
    if (hh == 0) break;                               // B0C9 BEQ ->B0E3
    // Note: B0CB DEC $DD is 16-bit here and writes $01DE too; the same `C6 DD` in the bit-6/7 merges runs with
    // A 8-bit.
    hh = (hh - 1) & 0xFFFF;
    r[0x01DD] = (uint8_t)hh; r[0x01DE] = (uint8_t)(hh >> 8);
    int wd = r[0x01DB] | (r[0x01DC] << 8);
    src = (src - wd - 1 + 0x100) & 0xFFFF;            // B0CD-B0D6 TXA/SBC/DEC/ADC #0100/TAX
    dst = (dst - wd - 1 + 0x100) & 0xFFFF;            // B0D7-B0E0, the same for the destination
  }
}                                                     // B0E3 SEP #20 / B0E5 RTS

// C0/AFAB and C0/B023: the inline blocks bits 6 and 7 of the mask run instead of C0/B096. Identical bodies except
// for two immediates; they merge one bit per tile into the $7E:30C0 plane, keeping the other seven, and add no
// base ($01F0 is ignored; the plane is fixed in the opcodes).
static void ctAF4EMergeBit(uint8_t* r, int andSrc, int andDst) {
  r[0x01DC] = 0x00; r[0x01DE] = 0x00;                 // AFAB / AFAD
  r[0x01E4] = 0x00;                                   // AFAF -- dead, but still a write
  r[0x01DB] = (uint8_t)((r[0x0140] - r[0x013E]) & 0xFF);   // AFB1-AFB6 width
  r[0x01DD] = (uint8_t)((r[0x0141] - r[0x013F]) & 0xFF);   // AFB8-AFBD height
  int x = r[0x013E] | (r[0x013F] << 8);               // AFC1 TAX -- the raw corner, no `ADC $F0`
  { int v = x & 0x00FF;                               // AFC4 / AFC7 STA $DF -- 16-bit, read back:
    r[0x01DF] = (uint8_t)v; r[0x01E0] = (uint8_t)(v >> 8); }   //   the row advance ORs it back in
  int y = r[0x0142] | (r[0x0143] << 8);               // AFC9 TAY
  { int v = y & 0x00FF;                               // AFCC / AFCF STA $E1 -- 16-bit, also live
    r[0x01E1] = (uint8_t)v; r[0x01E2] = (uint8_t)(v >> 8); }
  for (;;) {
    r[0x01E3] = r[0x01DB];                            // AFDA/AFDC -- column counter, reset per row
    for (;;) {
      int s = r[(0x30C0 + x) & 0xFFFF] & andSrc;      // AFDE / AFE1 AND #01 (or #02 for bit 7)
      r[0x01D9] = (uint8_t)s;                         // AFE3
      int d = r[(0x30C0 + y) & 0xFFFF];               // AFE5
      r[(0x30C0 + y) & 0xFFFF] = (uint8_t)((d & andDst) | s);   // AFE8 AND #FE / AFEA ORA / AFEC
      // The merge runs width+1 times but INX/INY only width times: the counter is tested before the decrement.
      if (r[0x01E3] == 0) break;                      // AFEF / AFF1 BEQ ->AFFA
      r[0x01E3] = (uint8_t)(r[0x01E3] - 1);           // AFF3 DEC A / AFF4
      x = (x + 1) & 0xFFFF; y = (y + 1) & 0xFFFF;     // AFF6 INX / AFF7 INY
    }
    if (r[0x01DD] == 0) break;                        // AFFA / AFFC BEQ ->B01A
    r[0x01DD] = (uint8_t)(r[0x01DD] - 1);             // AFFE `C6 DD` -- 8-bit here, unlike B0CB
    // B000 REP #20, then the row advance masks rather than subtracts: keep the new row, restore the saved
    // starting column from $01DF / $01E1.
    x = (((x + 0x100) & 0xFF00) | (r[0x01DF] | (r[0x01E0] << 8))) & 0xFFFF;   // B002-B00C
    y = (((y + 0x100) & 0xFF00) | (r[0x01E1] | (r[0x01E2] << 8))) & 0xFFFF;   // B00D-B017
  }
}

// C0/AF4E: eight bit tests on $0144, applied in ascending order and cumulative (one op can move six planes and
// merge two bits). Bits 0-5 pick a base and call C0/B096; bits 6 and 7 run the inline merges above.
static void ctVmAF4E(uint8_t* r) {
  if (r[0x0144] == 0) return;                         // AF4E / AF50 BNE / AF52 RTS -- no writes
  static const int BASE[6] = { 0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040 };
  for (int b = 0; b < 6; b++) {                       // AF53/AF61/AF6F/AF7D/AF8B/AF99
    if (!(r[0x0144] & (1 << b))) continue;            // `89 xx BIT #imm` sets only Z -> BEQ = clear
    r[0x01F0] = (uint8_t)BASE[b];                     // AF5A STX $F0 -- 16-bit, both bytes
    r[0x01F1] = (uint8_t)(BASE[b] >> 8);
    ctB096(r, BASE[b]);                               // AF5C / AF6A / AF78 / AF86 / AF94 / AFA2
  }
  if (r[0x0144] & 0x40) ctAF4EMergeBit(r, 0x01, 0xFE);   // AFA7 -> AFAB, plane bit 0
  if (r[0x0144] & 0x80) ctAF4EMergeBit(r, 0x02, 0xFD);   // B01D -> B023, plane bit 1
}

// C0/3D97: op 0xE4, the immediate scripted tile copy. Eight bytes, CLC. It stages the same operands as op 0xE5
// but runs JSR C0/AF4E itself, so the copy is done when the op returns. It does not write $0145 (0xE5 only, a BG
// re-upload selector); both ops pass the full $0144 to AF4E.
// `script` is the image the IP indexes ($7F:2001 in the ROM): CtFieldRunnerOne passes WRAM, the live VM its own
// buffer, so `ip` is the live IP either way.
void CtVmTileCopyE4(uint8_t* r, const uint8_t* script, int ip) {
  int a = script[(ip + 1) & 0xFFFF] | (script[(ip + 2) & 0xFFFF] << 8);
  r[0x013E] = (uint8_t)a; r[0x013F] = (uint8_t)(a >> 8);        // 3D9B / 3D9F (16-bit)
  int b2 = script[(ip + 3) & 0xFFFF] | (script[(ip + 4) & 0xFFFF] << 8);
  r[0x0140] = (uint8_t)b2; r[0x0141] = (uint8_t)(b2 >> 8);      // 3DA3 / 3DA7
  int c = script[(ip + 5) & 0xFFFF] | (script[(ip + 6) & 0xFFFF] << 8);
  r[0x0142] = (uint8_t)c; r[0x0143] = (uint8_t)(c >> 8);        // 3DAB / 3DAF
  r[0x0144] = script[(ip + 7) & 0xFFFF];                        // 3DB5 / 3DB9 (8-bit, unmasked)
  r[0x01C7] = (uint8_t)(ip + 7);                                // 3DBB STX $C7 -- 16-bit, and
  r[0x01C8] = (uint8_t)((ip + 7) >> 8);                         //   needed because AF4E eats X
  ctVmAF4E(r);                                                  // 3DBD JSR C0/AF4E
}

// C0/3DC5: op 0xE5, stage a tile copy. Eight bytes: three 16-bit operands into $013E/$0140/$0142 and one byte
// into $0144, whose low three bits go to $0145 (its only writer; C0/179A reads it as the style of the BG
// re-upload). No copy here: 3DEF TSB $17 sets $0117 bit 5 and the main loop's C0/1770 copies (C0/1790 JSR
// C0/AF4E) at the start of the next frame. Exits CLC.
void CtVmTileStageE5(uint8_t* r, const uint8_t* script, int ip) {
  int a = script[(ip + 1) & 0xFFFF] | (script[(ip + 2) & 0xFFFF] << 8);
  int b2 = script[(ip + 3) & 0xFFFF] | (script[(ip + 4) & 0xFFFF] << 8);
  int c  = script[(ip + 5) & 0xFFFF] | (script[(ip + 6) & 0xFFFF] << 8);
  int m  = script[(ip + 7) & 0xFFFF];
  r[0x013E] = (uint8_t)a;  r[0x013F] = (uint8_t)(a >> 8);         // 3DCD STA $3E (16-bit)
  r[0x0140] = (uint8_t)b2; r[0x0141] = (uint8_t)(b2 >> 8);        // 3DD5 STA $40
  r[0x0142] = (uint8_t)c;  r[0x0143] = (uint8_t)(c >> 8);         // 3DDD STA $42
  r[0x0144] = (uint8_t)m;                                         // 3DE7 STA $44 (8-bit)
  r[0x0145] = (uint8_t)(m & 0x07);                                // 3DE9 AND #07 / 3DEB STA $45
  r[0x0117] = (uint8_t)(r[0x0117] | 0x20);                        // 3DED LDA #20 / 3DEF TSB $17
}

void CtVmTileCopyAF4E(uint8_t* r) { ctVmAF4E(r); }

// $C7:0004 sound-mailbox post, closed-gate case only. $C7:0004 = JMP $C7:0140, whose first test (70157 LDA $05 /
// BPL) skips to the epilogue when $1E05 bit 7 is clear; only the epilogue's 70194 STZ $00 runs, so $1E00 is
// cleared and $1E01-$1E03 stay (which keeps $1E03 sticky between ops). $1E05 is the driver's latch: $E0 when
// uploaded and idle ($C7:013B, $C7:0614), the song id during a load ($C7:01B3).
// Not ported: the open-gate path ($C7:061C writes $2140-$2143 and waits for the SPC's echo). It returns 0 and
// the caller traps; the driver itself is src/audio.c.
static int ctVmSoundPost(uint8_t* r) {
  if (r[0x1E05] & 0x80) return 0;                 // 70157 LDA $05 / 70159 BPL -- gate open
  r[0x1E00] = 0x00;                               // 70194 STZ $00, 8-bit: the command byte only
  return 1;
}

// ---- The $C1 call gate, C1/CFC2 ($C1:8003 = JMP C1/CFC2) ----
// A prologue, an indexed JSR through the six-entry table at $C1:D126, and an epilogue; no hardware, no waits,
// every loop bounded by CPX #0100. The prologue sets DB = $7E, DP = $0000 and A = 0 (LDA #0000 / SEP #20); Y is
// the argument. PHP/PLP bracket the call, so only A reaches the caller.
// It owns $7E:2400-24FF (item ids), $7E:2500-25FF (counts), $7E:2C53-2C55 (24-bit gold) and the scratch
// $AEF7/$AEF9 (16-bit) + $AEFB.
int ctVmC1Gate(uint8_t* r, int leaf, int y) {          // declared in move.h; ct_vm.c calls this body too
  int i;
  r[0xAEF7] = (uint8_t)y; r[0xAEF8] = (uint8_t)(y >> 8);   // STY $AEF7 (16-bit), each leaf's first
  if (y == 0) {                                            // instruction, so y == 0 commits it too
    // Each leaf's CPY #0000 / BEQ arm returns at once: leaves 0-2 (items) with A = 0 (D002/D053/D083),
    // leaves 3-5 (gold) with A = $FF (D09F/D0E5/D123). Leaf 5's result is not observable: its only
    // caller, op 0xCE, discards A (C0/387E).
    return (leaf >= 3) ? 0xFF : 0x00;                      // even the rejected calls commit $AEF7
  }
  switch (leaf) {
    case 0:   // C1/CFE9 HasItem -- returns the matched id (never 0 here), or 0 if absent
      for (i = 0; i < 0x100; i++)
        if (r[0x2400 + i] == r[0xAEF7]) return r[0x2400 + i];   // CFF7 CMP $AEF7 is 8-bit, so
      return 0x00;                                              // only Y's low byte is compared
    case 1: {  // C1/D005 AddItem
      int free = 0, got = 0;                                // D010 STX $AEF9 = 0 / D013 STZ $AEFB
      r[0xAEF9] = 0x00; r[0xAEFA] = 0x00; r[0xAEFB] = 0x00;
      for (i = 0; i < 0x100; i++) {
        if (r[0x2400 + i] == 0 && !got) {                   // D016/D019/D01B/D01E: capture the
          free = i; got = 1;                                //   first free slot, once
          r[0xAEF9] = (uint8_t)i; r[0xAEFA] = (uint8_t)(i >> 8);   // D020 STX $AEF9 (16-bit)
          r[0xAEFB] = 0x01;                                 // D023 INC $AEFB
        }
        if (r[0x2400 + i] == r[0xAEF7]) {                   // D029 -- already held
          if (r[0x2500 + i] >= 0x63) return 0x00;           // D049 CMP #63 / BCS -- 99 is the cap
          r[0x2500 + i] = (uint8_t)(r[0x2500 + i] + 1);     // D04D INC A / D04E
          return r[0x2500 + i];
        }
      }
      // ROM quirk, kept: $AEF9 is 0 from D010 and is re-stored only when a free slot is found, so on a full
      // inventory D034 LDX $AEF9 reloads 0 and the new item overwrites slot 0.
      (void)got;
      r[0x2400 + free] = r[0xAEF7];                         // D037/D03A
      r[0x2500 + free] = (uint8_t)(r[0x2500 + free] + 1);   // D03D-D041
      return r[0x2500 + free];
    }
    case 2:   // C1/D056 RemoveItem
      for (i = 0; i < 0x100; i++) {
        if (r[0x2400 + i] != r[0xAEF7]) continue;
        if (r[0x2500 + i] == 0) return 0x00;                // D074 BEQ -- nothing to take
        r[0x2500 + i] = (uint8_t)(r[0x2500 + i] - 1);       // D076/D077
        if (r[0x2500 + i] != 0) return r[0x2500 + i];       // D07A BNE
        r[0x2400 + i] = 0x00;                               // D07C STA $2400,X with A == 0: the
        return 0xFF;                                        //   slot is freed, not just emptied
      }
      return 0x00;                                          // D06F BRA -- absent
    case 3: {  // C1/D086 HasGold -- read-only apart from the $AEF7 spill above
      int g = r[0x2C53] | (r[0x2C54] << 8);                 // D08E LDX $2C53 (16-bit)
      if (g >= y) return 0xFF;                              // D091 CPX $AEF7 / BCS
      if (r[0x2C55] != 0) return 0xFF;                      // D096/D099 -- the high byte covers it
      return 0x00;                                          // D09B -- cannot afford
    }
    case 4: {  // C1/D0A2 AddGold
      int g = (r[0x2C53] | (r[0x2C54] << 8)) + y;           // D0AC-D0B0 (16-bit ADC)
      r[0x2C53] = (uint8_t)g; r[0x2C54] = (uint8_t)(g >> 8);
      if (g > 0xFFFF) r[0x2C55] = (uint8_t)(r[0x2C55] + 1); // D0B6 BCC / D0BD INC $2C55
      if (r[0x2C55] < 0x98) return 0xFF;                    // D0C5 CMP #98 / BCC
      // The clamp is two-stage, not min(gold, 9999999): $2C55 is forced to $98 even when it was larger, then
      // $2C53 is tested, so $99:0000 clamps to $98:0000 = 9,961,472.
      r[0x2C55] = 0x98;                                     // D0C9/D0CB -- unconditional
      if ((r[0x2C53] | (r[0x2C54] << 8)) < 0x9680) return 0xFF;   // D0D3 CMP #9680 / BCC
      r[0x2C53] = 0x7F; r[0x2C54] = 0x96;                   // D0D8/D0DB LDA #967F
      return 0x00;                                          // D0DE LDA #0000 / D0E3 BRA -> RTS
    }
    default: { // C1/D0ED SubGold
      int g = ((r[0x2C53] | (r[0x2C54] << 8)) - y) & 0xFFFF;      // D0F7-D0FB (16-bit SBC)
      r[0xAEF9] = (uint8_t)g; r[0xAEFA] = (uint8_t)(g >> 8);      // D0FE STA $AEF9 -- always
      if ((r[0x2C53] | (r[0x2C54] << 8)) < y) {                   // D101 BCS -- a borrow happened
        if (r[0x2C55] == 0) return 0x00;                          // D10B BEQ -- broke, no change
        r[0x2C55] = (uint8_t)(r[0x2C55] - 1);                     // D10D DEC $2C55
      }
      r[0x2C53] = (uint8_t)g; r[0x2C54] = (uint8_t)(g >> 8);      // D118/D11B
      return 0xFF;
    }
  }
}

// ---- The $C1 item and gold ops: C0/3780 (0xC9), 37B1 (0xCA), 37C7 (0xC7), 37E3 (0xCB), 3827 (0xCC),
// 385B (0xCD), 3874 (0xCE) ----
// Each stages an argument in Y and a leaf index in A and calls JSL $C1:8003. Lengths and operand widths differ:
//   0xCA/0xCB/0xC7 ip+2 (LDX $C7 / SEC / RTS, no INX); 0xCD/0xCE ip+3 (16-bit operand);
//   0xC9 ip+3 or branch; 0xCC ip+4 or branch. Carry is set on every exit.
// 0xC7 is 0xCA with the id read from the locals (37DD LDA $7F0200,X, X = operand*2), then 37E1 BRA ->37BC.
// The script comes in as a pointer: CtFieldRunnerOne passes ram + 0x12001 (the ROM's $7F:2001); the live VM
// (ct_vm.c) passes its own body, since it does not model $7F:2000. Every other address is WRAM and the same for
// both (ct_wram_layout_assert). Returns 1 if `op` is in the family (new IP in *outIp), else 0.
int CtVmItemFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op != 0xC7 && op != 0xC9 && op != 0xCA && op != 0xCB &&
      op != 0xCC && op != 0xCD && op != 0xCE) return 0;
  int c1ip1 = (ip + 1) & 0xFFFF;
  int wide = (op == 0xCC || op == 0xCD || op == 0xCE);
  int spill = wide ? (ip + 3) & 0xFFFF : (ip + 2) & 0xFFFF;      // 3831/3865/387E vs 37BA
  r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8); // STX $C7, 16-bit
  int arg;
  if (wide) arg = script[c1ip1] | (script[(ip + 2) & 0xFFFF] << 8);   // 382B, 16-bit
  else if (op == 0xC7) {
    int n = script[c1ip1];                                       // 37CB
    arg = r[0x10200 + ((n * 2) & 0xFFFF)];                       // 37DD LDA $7F0200,X, 8-bit
  } else arg = script[c1ip1];                                    // 3784 etc, zero-extended
  int leaf = (op == 0xC9) ? 0 : (op == 0xCA || op == 0xC7) ? 1 : (op == 0xCB) ? 2 :
             (op == 0xCC) ? 3 : (op == 0xCD) ? 4 : 5;            // LDA #imm before the JSL
  int a = ctVmC1Gate(r, leaf, arg);
  if (op == 0xC9 || op == 0xCC) {
    // 3792/383C CMP #00 / BEQ: branch when the leaf returned 0 (0xC9: item absent; 0xCC: gold not affordable).
    // The target is spill + disp, with the displacement read at the spill.
    if (a == 0) {
      int disp = script[spill];                                  // 379D / 3847
      r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;               // 37A6 STA $D9 (16-bit)
      *outIp = (spill + disp) & 0xFFFF; return 1;                // 37AA ADC $D9
    }
    *outIp = (spill + 1) & 0xFFFF; return 1;                     // 3796 LDX $C7 / INX / SEC
  }
  *outIp = spill; return 1;                                      // 37C3 LDX $C7 / SEC -- no INX
}

// ---- The $C2 party gate, C2/8C36 ($C2:8004 = BRA -> JSR C2/8C36) ----
// Used by 0xD2 (489 sites), 0xD3 (127), 0xD4 (127), 0xD0 (66), 0xCF (44), 0xF8 (25), 0xD7 (12), 0xD1 (4), and
// 0xF9/0xFA (1 each). Not ported: 0xD5 (no sites in the 504 scenes). No hardware or waits; the only long accesses
// are three ROM table reads and two MVN blocks.
// The prologue sets DB = $7E and DP = $0400 ($C0 = $7E:04C0); SEP #30 makes A/X/Y 8-bit, so STY $C0 / STX $C9
// truncate.
// PHP (8C38) / PLP (8C57) bracket the call, so no leaf's carry reaches the caller: each op exits with its entry
// carry, which is 0 (5AAD ASL A is 16-bit on a value below $100). Ops 0xD0/0xD1/0xD3/0xF8/0xF9/0xFA therefore
// end the visit; 0xCF/0xD2/0xD7 set carry after the call, and 0xD4's carry is its last CMP $D9.
// The gate does not preserve X: C2/82E1 always ends at X = $2830 and C2/834D's PLP narrows it to $30, which op
// 0xF8's second STX $C9 shows.

// C2/8CBE -- the reserve insert loop, shared by leaves 1 and 2. Pushes `a` into slot 3 and walks the
// displaced value down slots 4..8, stopping as soon as the value it displaced was itself empty.
// Returns the A the ROM leaves behind, which the gate stores to $04C1.
static int ctVmC2Insert(uint8_t* r, int a) {
  for (int y = 3; y < 9; y++) {                          // 8CBE LDY #03 / 8CCA CPY #09 / BCC
    int x = r[0x2980 + y];                               // 8CC0 LDX $2980,Y
    r[0x2980 + y] = (uint8_t)a;                          // 8CC3 STA $2980,Y
    a = x;                                               // 8CC6 TXA
    if (a & 0x80) break;                                 // 8CC7 BMI -> RTS
  }
  return a & 0xFF;
}

// C2/8CCF (limit 9, the roster) and C2/8C75 (limit 3, the active party) are one routine: 8C75 is LDA #03 /
// BRA ->8CD1. Returns 0 when the id in $04C0 is found, $FF when not, with the index in Y.
// Note: 8CD1 STA $C2 writes the limit to $7E:04C2 on every call, and slot 0 is always read (do-while).
static int ctVmC2Find(uint8_t* r, int limit, int* yOut) {
  r[0x04C2] = (uint8_t)limit;                            // 8CD1 STA $C2
  int y = 0;                                             // 8CD3 LDY #00 / 8CD5 TYX (X = 0)
  for (;;) {
    if (r[0x2980 + y] == r[0x04C0]) { *yOut = y; return 0x00; }   // 8CD9 CMP $C0 / 8CDB BEQ -> TXA
    y++;                                                 // 8CDD INY
    if (y >= limit) break;                               // 8CDE CPY $C2 / 8CE0 BCC
  }
  *yOut = y;
  return 0xFF;                                           // 8CE2 DEX / 8CE3 TXA
}

// C2/8C93: the shared place tail: OR the PC's bit into $7E:29AF and store the id in slot Y. $FF:F9BB is the bit
// table 80 40 20 10 08 04 02 01; LDX $C0 is 8-bit, so an id above 7 reads past it, as in the ROM.
static int ctVmC2Place(uint8_t* r, const uint8_t* rom, int y) {
  r[0x29AF] = (uint8_t)(r[0x29AF] | (rom ? MR(0x3FF9BB + r[0x04C0]) : 0));   // 8C95 LDA $FFF9BB,X / 8C99 TSB (no-ROM run: none)
  r[0x2980 + y] = r[0x04C0];                             // 8C9C LDA $C0 / 8C9E STA $2980,Y
  return r[0x04C0];
}

// C2/8CF7 (leaf 5): mark the PC's roster slot empty (bit 7), if it is in the roster.
static int ctVmC2Leaf5(uint8_t* r) {
  int y, f = ctVmC2Find(r, 9, &y);
  if (f != 0) return f;                                  // 8CFA BNE -> RTS with A = $FF
  int a = r[0x2980 + y] | 0x80;                          // 8CFC/8CFF ORA #80
  r[0x2980 + y] = (uint8_t)a;                            // 8D01
  return a;
}

// C2/8C79 (leaf 1): add to the active party. Drops the PC from the roster first, then takes the first empty
// active slot; if all three are full it evicts slot 2 and pushes the evicted id into the reserve.
static int ctVmC2Leaf1(uint8_t* r, const uint8_t* rom) {
  ctVmC2Leaf5(r);                                        // 8C79 JSR C2/8CF7
  for (int y = 0; y < 3; y++)                            // 8C7C LDY #00 / 8C84 CPY #03
    if (r[0x2980 + y] & 0x80) return ctVmC2Place(r, rom, y);       // 8C81 BMI ->8C93
  int old = r[0x2982];                                   // 8C88 LDX $2982
  r[0x2982] = r[0x04C0];                                 // 8C8B/8C8D
  return ctVmC2Insert(r, old);                           // 8C90 TXA / 8C91 BRA ->8CBE
}

// C2/8CA2 (leaf 2): remove from the active party: shift the later members up, mark slot 2 empty, then push the
// removed id back into the reserve.
static int ctVmC2Leaf2(uint8_t* r) {
  int y, f = ctVmC2Find(r, 3, &y);
  if (f != 0) return f;                                  // 8CA5 BNE ->8CCE with A = $FF
  while (y != 2) { r[0x2980 + y] = r[0x2981 + y]; y++; } // 8CA7 CPY #02 / 8CAB-8CB2
  r[0x2982] = (uint8_t)(r[0x2982] | 0x80);               // 8CB4-8CB9
  return ctVmC2Insert(r, r[0x04C0]);                     // 8CBC LDA $C0 -> the insert loop
}

// C2/8CE5 (leaf 4): add to the reserve, only if the PC is not in the roster already.
static int ctVmC2Leaf4(uint8_t* r, const uint8_t* rom) {
  int y, a = 0;
  if (ctVmC2Find(r, 9, &y) == 0) return 0x00;            // 8CE8 BEQ ->8CF6
  for (y = 3; y < 9; y++) {                              // 8CEA LDY #03 / 8CF2 CPY #09
    a = r[0x2980 + y];                                   // 8CEC LDA $2980,Y
    if (a & 0x80) return ctVmC2Place(r, rom, y);         // 8CEF BMI ->8C93
  }
  return a;                                              // no empty slot: A = the last one read
}

// C2/8D1C: one PC record. +$05 = base max HP (16-bit), +$2A = the accessory, +$03 = effective max HP. Accessory
// $A1 adds 50%, $A0 adds 25%, anything else nothing (the ROM zeroes A and still runs both LSRs). The 999 clamp's
// >= and > give the same result (at equality A already holds $03E7).
static void ctVmC28D1C(uint8_t* r, int y) {
  int acc = r[y + 0x2A];                                 // 8D1C LDA $002A,Y / 8D1F AND #00FF
  int base = r[y + 5] | (r[y + 6] << 8);                 // 8D23 LDA $0005,Y, 16-bit
  int bonus = (acc == 0xA1) ? (base >> 1)                // 8D26 CPX #00A1 / BEQ ->8D34 (one LSR)
            : (acc == 0xA0) ? (base >> 2)                // 8D2B CPX #00A0 / BEQ ->8D33 (two LSRs)
            : 0;                                         // 8D30 LDA #0000, then both LSRs
  int v = (bonus + base) & 0xFFFF;                       // 8D35 CLC / 8D36 ADC $0005,Y
  if (v >= 0x03E7) v = 0x03E7;                           // 8D39 CMP #03E7 / 8D3C BCC
  r[y + 3] = (uint8_t)v; r[y + 4] = (uint8_t)(v >> 8);   // 8D41 STA $0003,Y, 16-bit
}

// C2/8D05 (leaf 6): recompute max HP on all seven PC records at $7E:2600, stride $50, and
// C2/8D45 (leaf 7) -- restore current MP (+$07) from max MP (+$09) on the same seven.
static void ctVmC2Leaf6(uint8_t* r) {
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) ctVmC28D1C(r, rec);   // 8D13 CPY #2830 / BCC
}
static void ctVmC2Leaf7(uint8_t* r) {
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) r[rec + 7] = r[rec + 9];   // 8D4C/8D4F, 8-bit
}

// C2/8E1E (leaf 12): the count held of item $04C9. C2/87FA walks the $7E:2400 ids / $7E:2500 counts and returns
// carry clear with the slot in $04C7. A slot whose id matches but whose count is 0 is skipped (8804 BNE).
static int ctVmC2Leaf12(uint8_t* r) {
  int id = r[0x04C9], x = 0;                             // 8E1E LDA $C9 / 87FA LDX #00
  do {
    if (r[0x2400 + x] == id && r[0x2500 + x] != 0) {     // 87FC CMP / 8801 LDA $2500,X / 8804 BNE
      r[0x04C7] = (uint8_t)x; r[0x04C8] = 0x00;          // 880E STX $04C7 / 8811 STZ $04C8
      return r[0x2500 + x];                              // 8E27 LDX $C7 / 8E29 LDA $2500,X
    }
    x = (x + 1) & 0xFF;                                  // 8809 INX / 880A BNE -- X is 8-bit
  } while (x != 0);
  return 0x00;                                           // 880C SEC -> 8E23 LDA #00 / 8E25 BCS
}

// C2/F5ED -- map a PC id to its equipped weapon's three-colour palette block in bank $D1. The
// hardware multiplier does pc*$50; $7E:2629 + that is the weapon slot of the record.
static int ctVmC2F5ED(uint8_t* r, const uint8_t* rom, int romLen, int pc) {
  int w = r[(0x2629 + pc * 0x50) & 0xFFFF];              // F601 $4216 / F606 LDA $2629,X / AND #00FF
  r[0x041B] = (uint8_t)w; r[0x041C] = 0x00;              // F60C STA $041B -- 16-bit
  int v = ((size_t)(0xD6CEC + w) < (size_t)romLen) ? MR(0xD6CEC + w) : 0;   // F610 LDA $CD6CEC,X
  return 0x114B00 + v * 6;                               // F617-F61F: v*2 + v*4 + $4B00, bank $D1
}

// $FF:F958: the five triple-tech availability flags; no hardware, three ROM tables, owns $7E:04A0-$04A8
// ($04A1/$04A2 included: they are written whenever a combination's member mask is satisfied).
static void ctVmC2F958(uint8_t* r, const uint8_t* rom) {
  int have = 0xFE;                                       // F960 LDA #FE / F962 STA $A3
  for (int y = 8; y >= 0; y--) {                         // F964 LDY #08 / F971 DEY / F972 BPL
    int pc = r[0x2980 + y];                              // F966 LDX $2980,Y
    if (pc & 0x80) continue;                             // F969 BMI
    have &= ~MR(0x3FF9BB + pc);                         // F96B LDA $FFF9BB,X / F96F TRB $A3
  }
  r[0x04A3] = (uint8_t)have;
  r[0x04A0] = 0x00;                                      // F974 STZ $A0
  do {
    int i = r[0x04A0];                                   // F976 LDX $A0
    r[0x04A4 + i] = 0x00;                                // F978 STZ $A4,X -- before any test
    if (!(MR(0xC2983 + i) & have)) {                    // F97A LDA $CC2983,X / AND $A3 / F980 BNE
      int x = i * 3, ok = 0;                             // F982 ASL A / ADC $A0 -> i*3
      r[0x04A1] = 0x03;                                  // F988 LDA #03 / F98A STA $A1
      for (;;) {
        int t = (MR(0xC2953 + x) - 1) & 0xFF;           // F98C LDA $CC2953,X / F990 DEC A
        r[0x04A2] = (uint8_t)t;                          // F991 STA $A2
        if (!(MR(0x3FF9BB + (t & 7)) & r[0x2837 + (t >> 3)])) break;   // F99D / F9A2 / F9A5 BEQ
        x++;                                             // F9A7 INX
        r[0x04A1] = (uint8_t)(r[0x04A1] - 1);            // F9A8 DEC $A1
        if (r[0x04A1] == 0) { ok = 1; break; }           // F9AA BNE ->F98C
      }
      if (ok) r[0x04A4 + i] = (uint8_t)(r[0x04A4 + i] + 1);           // F9AC LDX $A0 / F9AE INC
    }
    r[0x04A0] = (uint8_t)(r[0x04A0] + 1);                // F9B0 INC $A0
  } while (r[0x04A0] < 5);                               // F9B4 CMP #05 / F9B6 BCC
}

// C2/82E1 -- clear $7E:2857-$285B, then flag every PC whose accessory (+$2A) falls in $AE..$B2 and
// whose $7E:03F6 entry is nonzero. Its terminal X = $2830 is what op 0xF8's second $04C9 records.
// Returns its terminal Y, which the gate does not restore (see ctVmC2Gate).
static int ctVmC2_82E1(uint8_t* r) {
  r[0x2857] = 0x00; r[0x2858] = 0x00;                    // 82E4 STZ $2857 -- 16-bit
  for (int k = 0; k < 3; k++) r[0x2859 + k] = r[0x2857 + k];   // 82F0 MVN $7E,$7E with C = 2
  int y = 0x285C;                                        // where the MVN leaves Y
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) {
    int a = r[rec + 0x2A];                               // 82FB LDA $002A,X
    if (a < 0xAE || a >= 0xB3) continue;                 // 82FE CMP #AE / BCC ; 8302 CMP #B3 / BCS
    y = a;                                               // 8306 TAY -- and it outlives the routine
    if (r[0x03F6 + a] == 0) continue;                    // 8307 LDA $03F6,Y / 830A BEQ
    r[0x27A9 + a] = 0x80;                                // 830C LDA #80 / 830E STA $27A9,Y
  }
  return y;
}

// C2/834D: the epilogue, run after every leaf (op 0xD2's pure queries too). It repaints three OBJ palette rows
// from bank $D1, rebuilds the five triple-tech flags and rescans the seven PC records. An empty party slot is
// skipped without advancing the destination, so a one-member party paints only row $21BA.
static int ctVmC2Epilogue(uint8_t* r, const uint8_t* rom, int romLen) {
  int y = 0x21BA;                                        // 8358 LDY #21BA
  for (int x = 0; x < 3; x++) {                          // 835B LDX #0000 / 8376 CPX #0003
    if (r[0x2980 + x] & 0x80) continue;                  // 8363 BMI ->8375
    int src = ctVmC2F5ED(r, rom, romLen, r[0x2980 + x]); // 8366 JSR C2/F5ED -> X, and A = 5
    for (int k = 0; k < 6; k++)                          // 8369 MVN $7E,$D1 with C = 5 -> six bytes
      r[(y + k) & 0xFFFF] = ((size_t)(src + k) < (size_t)romLen) ? MR(src + k) : 0;
    y = (y + 6 + 0x1A) & 0xFFFF;                         // MVN leaves Y past the block; 8371 ADC #1A
  }
  if ((size_t)romLen > 0x3FF9C3) ctVmC2F958(r, rom);  // 837B JSL $FF:F958 (its $FF:F9BB / $CC:29xx reads; a no-ROM run has none)
  return ctVmC2_82E1(r);                                 // 837F JSR C2/82E1
}
// The live VM's entry to the epilogue: its op 0xD2/0xD3/0xD4 arms edit the party in place and never reach
// C2/8C36, so it calls this to paint the three weapon rows ($7E:21BA/$21DA/$21FA, colours 13-15 of OBJ slots 5-7).
void CtVmC2Epilogue(uint8_t* r, const uint8_t* rom, int romLen) { (void)ctVmC2Epilogue(r, rom, romLen); }
static int ctVmC2Gate(uint8_t* r, const uint8_t* rom, int romLen, int leaf, int y, int x, int* exitY);
// The location load's tail, C0/56CB-56D3: TDC / XBA / LDA #0B / JSL C2/8004 after the slot-1 runner C0/56C8.
// Leaf 11 (C2/8C5A) is a bare RTS, so this runs the gate for its epilogue: every load repaints the weapon rows.
// `x` = C0/595C's exit X (the slot-1 pointer word $7F:2003 when the runner dispatched nothing) -> $04C9; `y` =
// the init runner's last TXY (C0/5906) -> $04C0. The next gate call overwrites both before anything reads them.
void CtVmC2LoadTail(uint8_t* r, const uint8_t* rom, int romLen, int x, int y) {
  (void)ctVmC2Gate(r, rom, romLen, 11, y, x, NULL);     // 56CD LDA #0B -> 8004 -> 8C36
}

// C2/8C36. `x` is X at the JSL, which each op leaves differently (0xD7: the item id; 0xF8: opcode*2; 0xD4: its
// last TAX); it lands in $04C9, which leaf 12 reads. 8C36 PHB / PHD / PHP saves only bank, DP and flags, so X
// and Y are not preserved. `exitY` gets the Y left by C2/82E1's 8306 TAY: $285C when no PC wears a triple-tech
// relic, else that relic's id. Only op 0xF8 (two gate calls) sees it.
static int ctVmC2Gate(uint8_t* r, const uint8_t* rom, int romLen, int leaf, int y, int x,
                      int* exitY) {
  r[0x04C0] = (uint8_t)y;                                // 8C44 STY $C0 -- SEP #30, 8-bit
  r[0x04C9] = (uint8_t)x;                                // 8C46 STX $C9 -- 8-bit
  int a = 0, yy = 0, wide = 0;
  switch (leaf) {                                        // 8C48 ASL A / TAX / 8C4A JSR ($8C5B,X)
    case 0:  a = ctVmC2Find(r, 3, &yy); break;           // 8C75
    case 1:  a = ctVmC2Leaf1(r, rom); break;             // 8C79
    case 2:  a = ctVmC2Leaf2(r); break;                  // 8CA2
    case 3:  a = ctVmC2Find(r, 9, &yy); break;           // 8CCF
    case 4:  a = ctVmC2Leaf4(r, rom); break;             // 8CE5
    case 5:  a = ctVmC2Leaf5(r); break;                  // 8CF7
    case 6:  ctVmC2Leaf6(r); a = 0; wide = 1; break;     // 8D05
    case 7:  ctVmC2Leaf7(r); a = 0; wide = 1; break;     // 8D45
    case 11: a = (leaf << 1) & 0xFF; break;             // 8C5A: a bare RTS -- A is still the 8-bit `ASL A` of the
                                                       //   leaf (8C48), so $04C1 = $16; only the epilogue runs
    default: a = ctVmC2Leaf12(r); break;                 // 8E1E (leaf 12). Not ported: leaves 8-10 (8D63/8D6A/8D8F)
  }
  r[0x04C1] = (uint8_t)a;                                // 8C4D STA $C1
  // Leaves 6 and 7 open with REP #30 and return without restoring it, so the gate's STA $C1 runs 16-bit and also
  // zeroes $04C2 (the scan limit of leaves 0 and 3).
  if (wide) r[0x04C2] = (uint8_t)(a >> 8);
  int ey = ctVmC2Epilogue(r, rom, romLen);               // 8C4F JSR C2/834D
  if (exitY) *exitY = ey;
  return a & 0xFF;                                       // 8C52 LDA #00 / XBA / 8C55 LDA $C1
}

// C0/39E1 and C0/39F4 -- the two OBJ palette-row shifts op 0xD4 runs when it compacts the party.
// $20 bytes each: row $21C0 -> $21A0, then row $21E0 -> $21C0.
static void ctVm39E1(uint8_t* r) { for (int k = 0; k < 0x20; k++) r[0x21A0 + k] = r[0x21C0 + k]; }
static void ctVm39F4(uint8_t* r) { for (int k = 0; k < 0x20; k++) r[0x21C0 + k] = r[0x21E0 + k]; }

// C0/30B3: the $1C81 bit-1 alignment snap (sibling of C0/305D), shared with the live VM's move family (ct_vm.c
// moveOp). The target index is in $01E9/$01EA (the caller's 16-bit STX $E9). Both compares use the 16-bit
// position masked to $FFF0 (whole pixels); $01D9/$01DA holds the self side and ends holding sy past the X test.
// Returns 1 = SEC (still aligning, the caller yields), 0 = CLC (aligned).
// Note: tools/dis.py desyncs inside this routine at 313C; decode it from the raw bytes.
int CtVmMoveSnap30B3(uint8_t* r, int idx) {
  r[0x1900 + idx] = 0x00;                                // 30B5 STZ -- 8-bit, REP #20 is at 30BB
  r[0x1980 + idx] = 0x00;                                // 30B8
  int tgt = r[0x01E9] | (r[0x01EA] << 8);                // 30C5 LDX $E9 (16-bit)
  int sx = (r[0x1800 + idx] | (r[0x1801 + idx] << 8)) & 0xFFF0;   // 30BD / 30C0
  r[0x01D9] = (uint8_t)sx; r[0x01DA] = (uint8_t)(sx >> 8);        // 30C3 STA $D9 (16-bit)
  int tx = (r[0x1800 + tgt] | (r[0x1801 + tgt] << 8)) & 0xFFF0;   // 30C7 / 30CA
  int xEqual = (tx == sx);                               // 30CD CMP $D9 / 30CF BNE ->30EB
  if (!xEqual) {
    // 30EB BCC ->30F8: carry clear means tx < sx unsigned.
    r[0x1900 + idx] = (uint8_t)(tx < sx ? 0xF0 : 0x10);  // 30FA / 30EF
  }
  // Both paths now recompute the Y pair -- the aligned one at 30D1, the adjusted one at 3105.
  int sy = (r[0x1880 + idx] | (r[0x1881 + idx] << 8)) & 0xFFF0;   // 30D3 / 3107
  r[0x01D9] = (uint8_t)sy; r[0x01DA] = (uint8_t)(sy >> 8);        // 30D9 / 310D -- overwrites sx
  int ty = (r[0x1880 + tgt] | (r[0x1881 + tgt] << 8)) & 0xFFF0;   // 30DD / 3111
  if (ty == sy) {
    if (xEqual) return 0;                                // 30E7 SEP #20 / 30E9 CLC -- fully aligned
    r[0x1A01 + idx] = 0x01;                              // 311F / 3121 -- X moved, Y already right
    r[0x10B00 + idx] = 0x01;                             // 3124
    return 1;                                            // 3128 SEC
  }
  // 312A, reached from both 30E5 and 3119. `BCC ->3140`: carry clear means ty < sy.
  r[0x1980 + idx] = (uint8_t)(ty < sy ? 0xF0 : 0x10);    // 3142 / 312E
  r[0x1A01 + idx] = 0x01;                                // 314B / 3137
  r[0x10B00 + idx] = 0x01;                               // 314E / 313A
  return 1;                                              // 3152 / 313E SEC
}

// C0/568B: on arrival, reset the walk anim (only when $1780,X == 0 and $1680,X != 0): 569C STA $1680,X = 0 /
// 569F STZ $1681,X / 56A2 STZ $1601,X.
static void moveArriveAnim(uint8_t* r, int idx) {
  if (r[0x1780 + idx] != 0) return;                      // 5690
  if (r[0x1680 + idx] == 0) return;                      // 5698
  r[0x1680 + idx] = 0;                                   // 569C
  r[0x1681 + idx] = 0;                                   // 569F
  r[0x1601 + idx] = 0;                                   // 56A2
}

// C0/C98A: the enqueue (C98A-CA06) and the reload (CA09-CA75).
// $76/$77 are the head and tail of a FIFO of objects waiting for the sprite assembler; the next pointer is $1081,X
// and any value >= $80 is null. C0/CA76 pops from the head under a raster budget, so an object can still be queued
// when its timer fires again; the phase INC happens only on an accepted enqueue, so the animation advances at the
// rate the assembler drains.
// $1100,X picks the end: >= 3 appends at the tail (C9A6), 0 pushes at the head (C9D0), 1/2 append (C9E2) unless the
// one-shot $78 == 2 sends it to the front (and clears it); bit 7 never enqueues. `ct.exe --sweepc98a` covers it.
void CtAnimTimerTick(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  int t = r[0x1601 + idx];
  if (t != 0) { t = (t - 1) & 0xFF; r[0x1601 + idx] = (uint8_t)t; if (t != 0) return; }  // DEC; still counting -> done
  int st = r[0x1100 + idx];                                 // C997 dispatch
  int front;                                                // which end of the FIFO this object joins
  if (st == 0) front = 1;                                   // C99A BEQ -> C9D0
  else if (st & 0x80) return;                               // C99C BMI -> RTS (never enqueues)
  else if (st == 1 || st == 2) {                            // C99E/C9A2 BEQ -> C9E2
    if (r[0x0178] == 2) { r[0x0178] = 0; front = 1; }       // C9E4-C9EA: the one-shot jumps the queue
    else front = 0;                                         // C9EC.. == C9A6.. byte for byte
  } else front = 0;                                         // C9A6: the tail
  if (front) {                                              // C9D0-C9E0
    int h = r[0x0176];                                      // DP = $0100, so $76 is at $7E0176
    if (idx == h) return;                                   // C9D0 CPX $76 / BEQ: already the head
    if (h & 0x80) { r[0x0177] = (uint8_t)idx; r[0x0176] = (uint8_t)idx; }  // C9D6 BMI -> C9C5/C9C6: empty
    else { r[0x1081 + idx] = (uint8_t)h; r[0x0176] = (uint8_t)idx; }       // C9D8/C9DB: our next = old head
  } else {                                                  // C9A6-CA06
    int tl = r[0x0177];
    if (idx == tl) return;                                  // C9A6 CPX $77 / BEQ: already the tail
    if (!(tl & 0x80) && !(r[0x1081 + idx] & 0x80)) return;  // C9AE LDA $1081,X / BPL: already linked in
    if (tl & 0x80) { r[0x0177] = (uint8_t)idx; r[0x0176] = (uint8_t)idx; } // C9B6 CPX #80 / BPL -> C9C6
    else { r[0x1081 + tl] = (uint8_t)idx; r[0x0177] = (uint8_t)idx; }      // C9BA/C9BD: old tail's next = us
  }
  r[0x1681 + idx] = (uint8_t)((r[0x1681 + idx] + 1) & 0xFF); // INC phase (C9C0/C9CB/C9DD/CA06)
  int mode = r[0x1780 + idx];                               // reload (CA09): pick the modifier table
  int mod = (mode <= 1) ? r[0x1680 + idx] : r[0x1781 + idx];
  // CA22 CLC / ADC $1580,X / STA $C1, then CA2E ADC $C1 with no CLC, so the first add's 16-bit carry-out lands
  // in the index (mod*4 + $1580 can pass $FFFF).
  int sum = ((mod & 0xFF) << 2) + (r[0x1580 + idx] | (r[0x1581 + idx] << 8));
  int c1 = sum & 0xFFFF;
  // CA26 STA $C1 is a 16-bit store to $01C1/$01C2 (DP = $0100); op 0x88's blob arm (C0/4A23) writes the same
  // scratch.
  r[0x01C1] = (uint8_t)c1; r[0x01C2] = (uint8_t)(c1 >> 8);
  int ix = ((r[0x1681 + idx] & 0xFF) + c1 + (sum > 0xFFFF)) & 0xFFFF;  // phase-indexed offset into $E4
  int a = (0x240000 + ix < romLen) ? MR(0x240000 + ix) : 0;
  if (a != 0) { r[0x1601 + idx] = (uint8_t)a; return; }     // CA72: store, phase persists
  a = (0x240000 + c1 < romLen) ? MR(0x240000 + c1) : 0;    // CA45: re-look-up at the phaseless base
  r[0x1601 + idx] = (uint8_t)a;
  if (mode != 2) { r[0x1681 + idx] = 0; return; }           // CA48-CA4D / CA6A: phase := 0
  // CA4F-CA69, the mode-2 ping-pong. $7F0B01,X is the direction/repeat state, read with two chained DECs:
  //   v == 1 -> DEC $1681,X only (CA54 -> CA66): the phase walks backward
  //   v == 2 -> $7F0B01 = 1, then DEC $1681,X (CA57 -> CA61/CA62 -> CA66)
  //   else   -> $7F0B01 = v - 2, $1681 = 0 (CA59/CA5D)
  // So the sequence plays forward to its $FF sentinel and then reverses instead of restarting.
  { int v = r[0x10B01 + idx];                               // $7F0B01,X ($7F is at +0x10000)
    if (((v - 1) & 0xFF) == 0)      r[0x1681 + idx] = (uint8_t)(r[0x1681 + idx] - 1);
    else if (((v - 2) & 0xFF) == 0) { r[0x10B01 + idx] = 1;
                                      r[0x1681 + idx] = (uint8_t)(r[0x1681 + idx] - 1); }
    else { r[0x10B01 + idx] = (uint8_t)((v - 2) & 0xFF); r[0x1681 + idx] = 0; } }
}

int CtFieldFrameLookup(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  int cfg = r[0x1780 + idx];
  if (cfg == 3) return r[0x1301 + idx];                     // CBF9: static-frame short-circuit
  int facing = r[0x1600 + idx];
  int b1480 = r[0x1480 + idx] | (r[0x1481 + idx] << 8);     // 16-bit walk-offset
  int b1500 = r[0x1500 + idx] | (r[0x1501 + idx] << 8);     // 16-bit sequence base
  int d6;
  // C0/CB3D-CB43: BEQ (facing 0) / CMP #02 / BEQ (facing 2) / BPL. The BPL tests the sign of A - 2, so facing 1
  // (1 - 2 = $FF) falls through to CB45, the x1 row; only facing 3 and above reach CB5B, the x3 row.
  if (facing == 0)                  d6 = b1500;             // CB52  -> base only
  else if (facing == 2)             d6 = b1480 * 2 + b1500; // CB6E  -> x2
  else if ((facing - 2) & 0x80)     d6 = b1480 + b1500;     // CB45  -> x1  (facing 1)
  else                              d6 = b1480 * 3 + b1500; // CB5B  -> x3  (facing 3 and above)
  int ph = r[0x1681 + idx] & 0xFF;                          // phase
  int mod = (cfg == 2) ? r[0x1781 + idx] : r[0x1680 + idx]; // CB8D ($1781) vs CBB1 ($1680)
  d6 = (d6 + ((mod & 0xFF) << 2) + ph) & 0xFFFF;
  int fb = (0x240000 + d6 < romLen) ? MR(0x240000 + d6) : 0xFF;  // CB9E/CBC2: [$D6] in bank $E4
  if (fb == 0xFF && cfg == 2) {
    // C0/CBA4-CBB0, the mode-2 arm of the sentinel: decrement the repeat state (unless v == 1, CBA9 BEQ) and
    // return CLC, "do not commit this frame". The wrap below is the cfg != 2 path only (CB8B BNE ->CBB1).
    int v = r[0x10B01 + idx];
    if (((v - 1) & 0xFF) != 0) r[0x10B01 + idx] = (uint8_t)((v - 1) & 0xFF);
    return -1;                                              // CBAF: CLC = do not commit
  }
  if (fb == 0xFF) {
    // The wrap, C0/CBC8-CBDA: the ROM rewinds $D6 by $D9 (the phase, C0/CB7A-CB80), marks the phase $FF and
    // re-fetches, returning that byte with carry set like the normal path (the shared SEC / RTS at CBDA).
    d6 = (d6 - ph) & 0xFFFF;                                // CBCA-CBCF
    r[0x1681 + idx] = 0xFF;                                 // CBD3-CBD5
    fb = (0x240000 + d6 < romLen) ? MR(0x240000 + d6) : 0xFF;   // CBD8: the re-fetch
  }
  return fb;
}

// ---- C0/9E84: the PC's facing selector (disasm/notes/player_anim_9e84.md) ----
// Called from the head of C0/9E29 before it copies the velocity. Indexes a 64-entry word table at $C0:9FF7 from
// the two velocity deltas and dispatches to 16 handlers plus an idle default. It owns $1600 (the facing), not
// $0F01. `ct.exe --sweepanim` checks it against the ROM.

// The shared 15-byte tail every arm ends in (A171/A1A4/A1BB/A1D2/A211/A23D/A254 differ only in k). The CMP guard
// matters: an unchanged mode resets neither phase nor timer, so the walk phase does not stutter while a direction
// is held.
static void ctAnimMode(uint8_t* r, int y, int k) {
  if (r[0x1680 + y] == (uint8_t)k) return;               // A175 LDA $1680,Y / CMP #k / BEQ rts
  r[0x1680 + y] = (uint8_t)k;
  r[0x1681 + y] = 0;                                     // phase
  r[0x1601 + y] = 0;                                     // timer
}

// C0/A18C, the run tail. A live velocity, a non-lead object, or $E5 set all mean run; all of them clear means
// the player asked to run and did not move: the run-into-wall head-shake.
static void ctAnimRunTail(uint8_t* r, int y) {
  r[0x0158] = 0; r[0x0159] = 0;                          // A18C STZ $58 / A18E STZ $59
  if (r[0x1D32] || r[0x1D33] || y != r[0x0197] || r[0x01E5])
    ctAnimMode(r, y, 0x06);                              // A1A4, k = $06 run
  else
    ctAnimMode(r, y, 0x17);                              // A1BB, k = $17 head-shake
}

// C0/A171, the walk tail. It opens with the same STZ $58 / STZ $59 as the run tail.
static void ctAnimWalkTail(uint8_t* r, int y) {
  r[0x0158] = 0; r[0x0159] = 0;                          // A171 STZ $58 / A173 STZ $59
  ctAnimMode(r, y, 0x01);                                // A175, k = $01 walk
}

// C0/A1E9, the idle chain, reached through the table's $9FF2 default (`LDX $6D / BRL ->A1E9`).
// Standing still puts both deltas at 0, so the index is 0 and every zero slot lands here.
static void ctAnimIdleChain(uint8_t* r, const uint8_t* rom, int romLen, int y) {
  // A1E9 LDA $62 / BEQ ->A1F7 / CMP #02 / BNE ->A1F7 / CPY $97 / BNE ->A1F7 / A1F5 BRA ->A1D2
  if (r[0x0162] == 0x02 && y == r[0x0197]) { ctAnimMode(r, y, 0x19); return; }
  if (r[0x011F] == 0) { ctAnimMode(r, y, 0x00); return; }         // A1F7/A1F9 -> A211
  // A1FB REP #20: $58 is read as a word from here to A20E, and the compare is 16-bit.
  int c = r[0x0158] | (r[0x0159] << 8);                           // A1FD LDA $58
  if (c != 0x02FF) {                                              // A1FF CMP #02FF / A202 BEQ ->A22A
    if (c < 0x02FF) { ctAnimMode(r, y, 0x00); return; }           // A204 BCC ->A211
    if ((c & 0x007F) != 0x007F) return;                           // A206-A20C; A20E SEP #20 / A210 RTS
  }                                                               //   -- that arm writes nothing
  // A22A LDA #8000 / A22D TSB $58, 16-bit (`A9 00 80`; a linear disassembler that has seen A20E's SEP misreads it
  // as LDA #00 / BRA). It ORs into the original counter: A206's AND #007F changed A, not memory.
  int v = (c | 0x8000) & 0xFFFF;
  r[0x0158] = (uint8_t)v; r[0x0159] = (uint8_t)(v >> 8);
  int t = (r[0x01F8] + 1) & 0xFF;                                 // A22F SEP #20 / A231 INC $F8
  r[0x01F8] = (uint8_t)t;
  int a = CtRomByte(rom, romLen, 0xFE00 + t);                     // A233 LDA $F8 / TAX / A236 LDA $FE00,X
                                                                  //   DB = $00 and bank $00:$FE00 mirrors
                                                                  //   $C0:$FE00 = file offset $FE00.
  if (a >= 0x08) { ctAnimMode(r, y, 0x00); return; }              // A239 CMP #08 / A23B BCS ->A211
  ctAnimMode(r, y, 0x21);                                         // A23D, k = $21 idle blink
}

void CtPlayerAnimSelect(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  r[0x01E5] = 0;                                         // 9E84 STZ $E5
  int y = idx;                                           // 9E86 LDY $6D
  if (r[0x1D34] != 0) {                                  // 9E88 LDA $1D34 / 9E8B BEQ ->9EAF
    if (r[0x1D34] & 0x04) {                              // 9E8D BIT #04 / 9E8F BNE ->9E95
      // 9E95 LDA $1D2D / 9E98 BEQ ->9EA4
      if (r[0x1D2D] == 0) { r[0x1681 + y] = 0; r[0x1601 + y] = 0; return; }      // 9EA4-9EAE
      // 9E9A BPL / EOR #FF / INC A / 9E9F CMP #18 / 9EA1 BRL ->A254. BRL is unconditional, so the CMP and the
      // fold above it are dead.
      ctAnimMode(r, y, 0x1B);                            // A254, k = $1B
      return;
    }
    r[0x01E5] = 1;                                       // 9E91 INC $E5 (from the STZ above) / BRA ->9EAF
  }
  // The index. The negative fold is EOR #FF / CLC / ADC #21 = (0x20 - a) & 0xFF, not a negate, and only the X
  // axis is shifted, so the axes occupy different bits. AND #7E keeps bits 1..6: an even index into 64 words.
  int bx = r[0x1D2C];                                    // 9EAF
  if (bx & 0x80) bx = ((bx ^ 0xFF) + 0x21) & 0xFF;       // 9EB2 BPL ->9EB9 / 9EB4-9EB8
  int sh = (bx >> 3) & 0xFF;                             // 9EB9 LSR A x3
  r[0x01DB] = (uint8_t)sh;                               // 9EBC STA $DB
  int by = r[0x1D2D];                                    // 9EBE
  if (by & 0x80) by = ((by ^ 0xFF) + 0x21) & 0xFF;       // 9EC1 BPL ->9EC8 / 9EC3-9EC7
  // 9ECA AND #7E. CtAnimDispatch9FF7 masks again for the follower entries (C0/9F1C, 9F6B, 9FCF) that compute
  // their own index.
  int ix = (by | sh) & 0x7E;                             // 9EC8 ORA $DB / 9ECA AND #7E / TAX
  CtAnimDispatch9FF7(r, rom, romLen, y, ix);             // 9ECD JSR ($9FF7,X)
}

// JSR ($9FF7,X): the facing/anim handler table, shared by the lead's selector (C0/9ECD) and the follower entries
// C0/9F1C, C0/9F6B (after a ring advance) and C0/9FCF (the hold arm). Every handler begins LDX $6D, so it runs on
// the current object whichever entry reached it.
void CtAnimDispatch9FF7(uint8_t* r, const uint8_t* rom, int romLen, int y, int ix) {
  ix &= 0x7E;
  int t = MR(0x9FF7 + ix) | (MR(0x9FF8 + ix) << 8);    // 9ECD JSR ($9FF7,X)
  // The handlers' operands are read from the ROM, not copied into a table. Each handler reloads X = idx (LDX $6D)
  // over the TAX above, so it stores through the object index.
  if (t == 0x9FF2) { ctAnimIdleChain(r, rom, romLen, y); return; }               // the default arm
  int tail, d;
  if (t >= 0xA079 && t < 0xA0C9) {                       // cardinal, stride $0A: unconditional facing
    r[0x1600 + y] = MR(t + 3);                          // +2 LDA #f  -> the operand at +3
    d = MR(t + 8) | (MR(t + 9) << 8);                  // +7 BRL, operand at +8, next instr at +10
    tail = (t + 10 + ((d >= 0x8000) ? d - 0x10000 : d)) & 0xFFFF;
  } else if (t >= 0xA0C9 && t < 0xA171) {                // diagonal, stride $15: keep-else-force
    int cur = r[0x1600 + y];                             // +2 LDA $1600,X
    if (cur != MR(t + 6) && cur != MR(t + 10))         // +5 CMP #a / +9 CMP #b
      r[0x1600 + y] = MR(t + 14);                       // +13 LDA #a / +15 STA $1600,X
    d = MR(t + 19) | (MR(t + 20) << 8);                // +18 BRL, operand at +19, next instr at +21
    tail = (t + 21 + ((d >= 0x8000) ? d - 0x10000 : d)) & 0xFFFF;
  } else return;                                         // not a shape this port knows
  if (tail == 0xA171)      ctAnimWalkTail(r, y);
  else if (tail == 0xA18C) ctAnimRunTail(r, y);
}
// C0/A254 and its siblings, reached by name from the follower entries (9EEE / 9F3D BRL ->A254 = mode $1B, the
// stair walk), through the same CMP-guarded store.
void CtAnimModeSet(uint8_t* r, int y, int k) { ctAnimMode(r, y, k); }

// C0/9E29: the PC controller's apply, reached from C0/3040.
//  1. The velocity: $1D32/$1D33 (from C0/881E) go to $1900,Y/$1980,Y; when both are zero the ROM branches to
//     9E36 and STZs them instead.
//  2. The party path-history ring: seven arrays at $7F0C00, $0C80, $0D00, $0D80, $0E00, $0E80 and $0F00, indexed
//     by the 7-bit cursor $AB (INC A / AND #7F, 128 entries). Followers walk this trail. Written only on the
//     non-zero arm: a stationary frame records nothing.
// 9E29 calls C0/9E84 first (CtPlayerAnimSelect); it does not touch the velocity, so it is not called here.
static void ct9E29Apply(uint8_t* r, int idx) {
  int vx = r[0x1D32], vy = r[0x1D33];
  if (vx == 0 && vy == 0) {                                // 9E2C/9E31 both zero -> 9E36
    r[0x1900 + idx] = 0; r[0x1980 + idx] = 0;              // 9E38 / 9E3B STZ
    return;
  }
  int ab = (r[0x01AB] + 1) & 0x7F;                         // 9E42 LDA $AB / INC A / AND #7F / STA $AB
  r[0x01AB] = (uint8_t)ab;                                 //   ($AB is DP-relative, DP = $0100)
  r[0x1900 + idx] = (uint8_t)vx; r[0x10C00 + ab] = (uint8_t)vx;   // 9E4F / 9E53
  r[0x1980 + idx] = (uint8_t)vy; r[0x10C80 + ab] = (uint8_t)vy;   // 9E59 / 9E5D
  r[0x10D00 + ab] = r[0x1D2C];                             // 9E63
  r[0x10D80 + ab] = r[0x1D2D];                             // 9E6A
  r[0x10E00 + ab] = r[0x0C00 + idx];                       // 9E6E LDA $0C00,Y (Y = $6D)
  r[0x10E80 + ab] = r[0x0C01 + idx];                       // 9E75
  r[0x10F00 + ab] = r[0x1D34];                             // 9E7C
}

// ---- C0/A88D: the object activation gate ----
// A predicate recomputed on each call, not a stored flag. Both arms of C0/A810's per-object dispatch reach it
// (A82D for state >= 3, A873 for state < 3), and on its carry (BCC) both skip C98A (anim), A98A (unlink), AB45
// (screen position) and A9CD (relink). Only state 7 (-> C0/A947) and state 8 (skipped) bypass it. The latch
// $0F00,X is a pure function of the predicate ($80 or $00): A8E5 and A913 only skip the edge work.
// Not included (separate mechanisms): the rising-edge one-shot A8EA-A904 ($0E80/$0E81 = $80, $1681/$1601/$1B00 = 0,
// $0F01 = $FF) with its sub-calls C0/7170 and C0/734C (the OBJ palette allocator, in ct_vm.c, see
// disasm/notes/objpal_allocator.md) and C0/6F9A / A9CD / EA42 / 7056 / A98A (ported in this file); the falling-edge
// tails A924-A932 ($0129 = $0D when $7F0A00,X and $0130 allow) and A934-A93C ($7F0B01,X = 2 when non-zero).
// `ct.exe --sweepa88d` checks the carry, the window bytes and the latch.
// ct_vm.c has an equivalent predicate for the live VM, which reads the position from sc->fx[] floats instead of
// $1801/$1881.
int CtObjActiveGate(uint8_t* r, int idx) {
  int act = 1;
  if (r[0x1100 + idx] & 0x80) act = 0;                   // A88F LDA $1100,X / A892 BMI ->A910
  else {
    int vis = r[0x1A81 + idx];                           // A894 -- the byte op 0x90/0x91 writes
    if (vis == 0 || (vis & 0x80)) act = 0;               // A897 BEQ ->A910 / A899 BMI ->A910
    else {
      // A89B-A8C9, the camera window. Each edge saturates: the ROM reloads a literal 0 or $FF instead of wrapping.
      int c3 = r[0x1D0A] - 4; if (c3 < 0)    c3 = 0x00;  // A89F SBC #04 / A8A1 BCS / A8A3 LDA #00
      int c4 = r[0x1D0C] + 4; if (c4 > 0xFF) c4 = 0xFF;  // A8AB ADC #04 / A8AD BCC / A8AF LDA #FF
      int c5 = r[0x1D0E] - 2; if (c5 < 0)    c5 = 0x00;  // A8B7 SBC #02 / A8B9 BCS / A8BB LDA #00
      int c6 = r[0x1D10] + 5; if (c6 > 0xFF) c6 = 0xFF;  // A8C3 ADC #05 / A8C5 BCC / A8C7 LDA #FF
      r[0x01C3] = (uint8_t)c3; r[0x01C4] = (uint8_t)c4;  // A8A5 / A8B1
      r[0x01C5] = (uint8_t)c5; r[0x01C6] = (uint8_t)c6;  // A8BD / A8C9
      // The compares use the tile coordinate doubled by an 8-bit ASL: bit 7 of $1801/$1881 is lost.
      int tx = (r[0x1801 + idx] << 1) & 0xFF;            // A8CD LDA $1801,X / A8D0 ASL A
      int ty = (r[0x1881 + idx] << 1) & 0xFF;            // A8D9 LDA $1881,X / A8DC ASL A
      if (tx < c3 || tx >= c4) act = 0;                  // A8D1 CMP $C3 BCC / A8D5 CMP $C4 BCS
      else if (ty < c5 || ty >= c6) act = 0;             // A8DD CMP $C5 BCC / A8E1 CMP $C6 BCS
    }
  }
  r[0x0F00 + idx] = (uint8_t)(act ? 0x80 : 0x00);        // A8F2 STA / A915 STZ -- see the note above
  return act;                                            // A90E SEC (active) / A940 CLC (inactive)
}

// ---- C0/3154: the player's touch redirect ----
// Op 0xB0's state-0 arm ends 304A LDX $6D / 304C LDA $1600,X / 304F JSR C0/5B8D / 3052 BCC / 3054 JSR C0/3154.
// C0/5B8D sets carry iff $01EB bit 7 is clear, i.e. the touch latch holds an object index rather than the $80
// that C0/881E re-arms each frame (304C's LDA is dead: 5B8D overwrites A). This is how a walking NPC's script is
// preempted into its encounter.
// C0/3154 is op 0x02's call arm with the target taken from $01EB and both priority and function slot fixed at 2;
// its guard LDA $1C00,X / CMP #03 / BCC is op 0x02's newp < cur with newp = 2.
// Not ported: the tail C0/31B1 (called at 31AD on both non-reject paths), gated on $0154 bit 1: a tile-based shove
// of the touched object through $7E7040 with four facing arms. `ct.exe --sweeptouch` sets that bit so the ROM's
// BIT #02 / BNE ->31BB skips it.
void CtVmTouchRedirect(uint8_t* r) {
  int t = r[0x01EB];                                   // 3154 LDX $EB -- the touched object
  // 3156 / 315B / 3160 -- the same three rejects op 0x02 uses, all `-> 31B0 RTS` doing nothing.
  if (r[0x1C01 + t] != 0) return;
  if (r[0x1100 + t] & 0x80) return;
  if (r[0x1000 + t] & 0x80) return;
  int cur = r[0x1C00 + t];                             // 3165
  if (cur < 3) return;                                 // 3168 CMP #03 / 316A BCC ->31AD: too urgent
                                                       //   already to be preempted to level 2
  // 316C-317E: the hardware multiply is a x128, so $7F0580 is indexed by (priority * 128 + object) --
  // the same per-level save array op 0x02 writes.
  int sav = 0x10580 + ((cur * 128 + t) & 0xFFFF);
  r[sav]     = r[0x1180 + t];                          // 3180 LDA $1180,X / 3185 STA $7F0580,X
  r[sav + 1] = r[0x1181 + t];
  int ent = 0x12001 + ((t * 16 + 4) & 0xFFFF);         // 3189 ASL A x4 / 3190 ADC #0004 = slot 2
  int fn  = r[ent] | (r[ent + 1] << 8);                // 3194 LDA $7F2001,X
  r[0x1180 + t] = (uint8_t)fn;                         // 319A STA $1180,X -- the redirect
  r[0x1181 + t] = (uint8_t)(fn >> 8);
  r[0x1C00 + t] = 0x02;                                // 319F LDA #02 / 31A1 STA $1C00,X
  r[0x1A80 + t] = 0;                                   // 31A4
  r[0x1A01 + t] = 0;                                   // 31A7
  r[0x1001 + t] = 0;                                   // 31AA STZ $1001,X -- the scheduler delay, so
                                                       //   the preempted object runs on the very next
                                                       //   pass instead of waiting out its cadence
}

// C0/5FB6 op 0x02, the priority call, and C0/5FBE its body, entered with the target index in A. Op 0x02 reads the
// index from the script (5FBA); op 0x05 joins at 5FBE (BRL) with it taken from the party-slot table. Y is ip+1 at
// the join for both. Three bytes: operand 1 = the target object, operand 2 = the new priority (high nibble) and
// the script-function slot (low nibble). Every arm exits at ip+3 with carry set (the rejects reach 6039, which adds
// the INY they skipped). $7F0580 holds a 128-byte row per priority level, indexed by object (the multiplier: x128).
// `ct.exe --sweepop02` checks it.
static int ctVmCall02(uint8_t* r, int ip, int tgt, int* carry) {
  *carry = 1;                                          // 603C SEC on every path
  // The DP staging ($01D9-$01E4) is shared scratch, written as the ROM writes it.
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 5FBE STA $DB / 5FC1 STZ $DC
  // 5FC3 / 5FC8 / 5FCD -- three reject gates, each `-> 6039`, before any further staging.
  if (r[0x1C01 + tgt] != 0 || (r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80))
    return (ip + 3) & 0xFFFF;
  int cur  = r[0x1C00 + tgt];                          // 5FD2 the target's current priority
  r[0x01D9] = (uint8_t)cur;                            // 5FD5 STA $D9
  int op2  = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 5FD9 operand 2
  r[0x01DD] = (uint8_t)op2;                            // 5FDD STA $DD
  int newp = (op2 >> 4) & 0x0F;                        // 5FDF-5FE2 LSR A x4: operand 2 is newPrio:slot,
                                                       //   high nibble and low nibble
  r[0x01DF] = (uint8_t)newp;                           // 5FE3 STA $DF
  int slot = op2 & 0x0F;                               // 6019 / 6064 AND #000F
  // The object's script-function pointer table, $7F2001 + idx*16 + slot*2, read 16-bit. The call arm
  // (6014-6021) and the defer arm (605E-606E) compute it identically.
  int ent = 0x12001 + ((tgt * 16 + slot * 2) & 0xFFFF);
  int fn  = r[ent] | (r[ent + 1] << 8);
  if (newp < cur) {                                    // 5FE9 BCS not taken -> the CALL
    int e1 = (cur * 128 + tgt) & 0xFFFF;
    r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);        // 5FFF STA $E1 (16-bit)
    int e3 = (tgt * 16) & 0xFFFF;
    r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);        // 6015 STA $E3 (16-bit)
    int sav = 0x10580 + e1;
    r[sav]     = r[0x1180 + tgt];                      // 6003/6008: save the target's live IP at its
    r[sav + 1] = r[0x1181 + tgt];                      //   current level, for op 0x00 to resume
    r[0x1180 + tgt] = (uint8_t)fn;                     // 6027
    r[0x1181 + tgt] = (uint8_t)(fn >> 8);
    r[0x1C00 + tgt] = (uint8_t)newp;                   // 602E
    r[0x1A80 + tgt] = 0; r[0x1A01 + tgt] = 0;          // 6031/6034: drop any in-flight move
  } else if (newp > cur) {                             // 603E -- the defer arm
    // A call to a less urgent level interrupts nothing: it parks the function entry (not a saved IP) at that
    // level for op 0x00 to pick up on its way back down, only when the slot is empty (6053 LDA $7F0580,X / 6057
    // BNE ->6078 drops it otherwise). $1180, $1C00 and the mover are untouched.
    int e1 = (newp * 128 + tgt) & 0xFFFF;
    r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);        // 6050 STA $E1 (16-bit)
    int park = 0x10580 + e1;
    if ((r[park] | (r[park + 1] << 8)) == 0) {
      // $E3 is staged only here, past 6057 BNE ->6078; the call arm always stages it.
      int e3 = (tgt * 16) & 0xFFFF;
      r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);      // 6062 STA $E3 (16-bit)
      r[park] = (uint8_t)fn; r[park + 1] = (uint8_t)(fn >> 8);      // 6074
    }
  }
  // newp == cur (5FE7 BEQ ->603A) lands here having done nothing: same level, no call.
  return (ip + 3) & 0xFFFF;
}

int CtVmOp02(uint8_t* r, int ip, int* carry) {         // 5FB6 INY / 5FBA LDA $7F2001,X
  return ctVmCall02(r, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], carry);
}

// C0/6109: op 0x04, the blocking priority call. Unlike 0x02 (issue, exit SEC at ip+3), it issues the call and then
// parks on its own opcode byte until the target finishes. The caller's $7F0980 latch splits the visits: 6109 LDX
// $6D / LDA $7F0980,X / BEQ ->611A (fresh) / BRL ->61A7 (poll); 61A0 sets it on issue, 61DC clears it on
// completion. Three bytes (opcode, target, priority<<4 | fn slot), but it never executes INY; the exits are
//   6114 TYX/INX/INX/INX/SEC -> ip+3 (fresh: target not runnable)   61A4 TYX/CLC -> ip (fresh: issued or wait)
//   61E0 TYX/INX/INX/INX/SEC -> ip+3 (poll: call completed)          61E6 TYX/CLC -> ip (poll: still running)
// Differences from op 0x02: a busy target ($1C01,X, 6127) parks instead of skipping; there is no defer arm
// (614C/614E park on both equal and greater); the poll arm does not re-test $1C01,X (61B4/61B9 test only $1100,X
// and $1000,X).
// C0/6122 (fresh) and C0/61AF (poll) are the joins, entered with the target resolved: op 0x04 reads it at
// 611E/61AB, op 0x07 BRLs here with it taken from the party-slot table. Y = ip at both.
static int ctVmCall04(uint8_t* r, int idx, int ip, int tgt, int* carry) {
  // 6109 LDX $6D reads $016D/$016E (X is 16-bit after the runner's REP #10 at 5A9E); the runner's 5A9C STX $6D
  // is an 8-bit store and $016E measures 0, so X is the object index.
  if (r[0x10980 + idx] == 0) {
    // Fresh request, C0/611A.
    // 611B INX (X = ip+1) with no INY: the operand is read without moving the IP.
    // 611C TDC / 611D XBA zeroes the accumulator's high half so the 8-bit LDA + 16-bit TAX at 6126
    // yields a clean index; $01DC is then STZ'd so the $DB pair reads as a 16-bit value at 6162.
    r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;        // 6122 STA $DB (8-bit) / 6124 STZ $DC
    if (r[0x1C01 + tgt] != 0) {                        // 6127 -> 61A4: park, not skip
      *carry = 0; return ip; }
    if ((r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80)) {   // 612C / 6131 -> 6114
      *carry = 1; return (ip + 3) & 0xFFFF; }
    int cur = r[0x1C00 + tgt];                         // 6136 the target's current priority
    r[0x01D9] = (uint8_t)cur;                          // 6139
    int op2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 613E (X = ip+2, again without INY)
    r[0x01DD] = (uint8_t)op2;                          // 6142
    int newp = (op2 >> 4) & 0x0F;                      // 6144-6147 LSR A x4
    r[0x01DF] = (uint8_t)newp;                         // 6148
    // 614A CMP $D9: requested vs current priority. 614C BEQ and 614E BCS park, so the call is issued only when
    // requested < current (a lower number is more urgent).
    if (newp >= cur) { *carry = 0; return ip; }        // -> 61A4
    int e1 = (cur * 0x80 + tgt) & 0xFFFF;              // 6152-6162: $4202*$4203 then + $DB
    r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);   // 6164 STA $E1 -- 16-bit
    int sav = 0x10580 + e1;                            // 616D STA $7F0580,X -- also 16-bit
    r[sav]     = r[0x1180 + tgt];                      // 6168: save the target's live IP at its
    r[sav + 1] = r[0x1181 + tgt];                      //   current level, for op 0x00 to resume
    int e3 = (tgt * 16) & 0xFFFF;                      // 6171-6179 AND #00FF / ASL A x4
    r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);   // 617A STA $E3 -- 16-bit
    int ent = 0x12001 + ((e3 + (op2 & 0x0F) * 2) & 0xFFFF);    // 617C-6185: + slot*2
    int fn  = r[ent] | (r[ent + 1] << 8);              // 6186 -- a 16-bit read
    r[0x1180 + tgt] = (uint8_t)fn;                     // 618C STA $1180,X -- 16-bit
    r[0x1181 + tgt] = (uint8_t)(fn >> 8);
    r[0x1C00 + tgt] = (uint8_t)newp;                   // 6193
    r[0x1A80 + tgt] = 0x00;                            // 6196 STZ $1A80,X -- drop any in-flight move
    r[0x1A01 + tgt] = 0x00;                            // 6199 STZ $1A01,X
    r[0x10980 + idx] = 0x01;                           // 61A0 -- arm the caller's wait latch
    *carry = 0; return ip;                             // 61A4 TYX / CLC / RTS -- park on our own byte
  }
  // Poll, C0/61A7.
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 61AF / 61B1
  // 61B4 / 61B9: the target went away. This exit (61D8) clears the latch without writing $01D9/$01DD/$01DF,
  // which the completion arm does write.
  if ((r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80)) {
    r[0x10980 + idx] = 0x00;                           // 61D8-61DC
    *carry = 1; return (ip + 3) & 0xFFFF;              // 61E0 -> ip+3, continue
  }
  int cur = r[0x1C00 + tgt];                           // 61BE
  r[0x01D9] = (uint8_t)cur;                            // 61C1
  int op2 = r[0x12001 + ((ip + 2) & 0xFFFF)];          // 61C6
  r[0x01DD] = (uint8_t)op2;                            // 61CA
  int newp = (op2 >> 4) & 0x0F;                        // 61CC-61CF
  r[0x01DF] = (uint8_t)newp;                           // 61D0
  // 61D2 CMP $D9 again: while the target is at or above the requested urgency it is still running our call, so
  // 61D4/61D6 park. Once op 0x00 has lowered its urgency (a higher number), the call is complete.
  if (newp >= cur) { *carry = 0; return ip; }          // -> 61E6 TYX / CLC / RTS
  r[0x10980 + idx] = 0x00;                             // 61D8: release the latch
  *carry = 1; return (ip + 3) & 0xFFFF;                // 61E0
}

int CtVmOp04(uint8_t* r, int idx, int ip, int* carry) {   // 611E / 61AB read the target themselves
  return ctVmCall04(r, idx, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], carry);
}

// C0/6086: op 0x03's body. Op 0x02 never parks and op 0x04 blocks until the callee finishes; op 0x03 parks while
// the target is unavailable (busy, or running something at least as urgent) and continues once it has issued. No
// defer arm (60B0/60B2 send equal and greater to 6106, which parks the script) and no $7F0980 latch.
// Y is ip at the join (607E TYX/INX, no INY): operand 2 is at ip+2; the exits are 6100 TYX/INX/INX/INX/SEC
// -> ip+3 and 6106 TYX/CLC -> ip.
static void ctVmCall03(uint8_t* r, int ip, int tgt, CtHRet* hp) {
  hp->ok = 1;
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 6086 STA $DB / 6089 STZ $DC
  if (r[0x1C01 + tgt] != 0) {                          // 608B -> 6106 park
    hp->ip = ip; hp->carry = 0; return; }
  if ((r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80)) {   // 6090 / 6095 -> 6100 skip
    hp->ip = (ip + 3) & 0xFFFF; hp->carry = 1; return; }
  int cur = r[0x1C00 + tgt];  r[0x01D9] = (uint8_t)cur;         // 609A / 609D
  int op2 = r[0x12001 + ((ip + 2) & 0xFFFF)];                   // 60A2 (609F TYX/INX/INX)
  r[0x01DD] = (uint8_t)op2;                                     // 60A6
  int newp = (op2 >> 4) & 0x0F;  r[0x01DF] = (uint8_t)newp;     // 60A8-60AC
  if (newp >= cur) { hp->ip = ip; hp->carry = 0; return; }      // 60B0 BEQ / 60B2 BCS -> 6106
  int e1 = (cur * 0x80 + tgt) & 0xFFFF;                         // 60B4-60C6
  r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);      // 60C8 STA $E1 (16-bit)
  int sav = 0x10580 + e1;
  r[sav] = r[0x1180 + tgt]; r[sav + 1] = r[0x1181 + tgt];       // 60CC / 60D1
  int e3 = (tgt * 16) & 0xFFFF;                                 // 60D5-60DD
  r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);      // 60DE STA $E3 (16-bit)
  int ent = 0x12001 + ((e3 + (op2 & 0x0F) * 2) & 0xFFFF);       // 60E0-60E9
  int fn = r[ent] | (r[ent + 1] << 8);                          // 60EA (16-bit)
  r[0x1180 + tgt] = (uint8_t)fn; r[0x1181 + tgt] = (uint8_t)(fn >> 8);   // 60F0
  r[0x1C00 + tgt] = (uint8_t)newp;                              // 60F7
  r[0x1A80 + tgt] = 0x00; r[0x1A01 + tgt] = 0x00;               // 60FA / 60FD
  hp->ip = (ip + 3) & 0xFFFF; hp->carry = 1;                    // falls into 6100
}

// C0/61F2 / 6207 / 6225 / 623B: `LDA $97,X` with X = the script's operand. $0197/$0199/$019B hold the three
// party slots' object indices (spilled by op 0x80's arms), so these ops name their target by party slot. Bit 7
// set = the slot is empty, and each op then skips its call with a three-byte advance.
int ctVmPartySlotTarget(const uint8_t* r, int slot) { return r[(0x0197 + slot) & 0x1FFFF]; }   // declared in move.h

// C0/5228, C0/52D3 and C0/536B: one move-toward-an-actor body in three variants (the same 152-byte shape).
//   `anim`   writes the facing $1600 and calls C0/5614 / C0/568B: ops 0x94/0x95 (5228) and 0x98/0x99 (536B);
//            not ops 0x9E/0x9F (52D3), which run the collision test on the old facing as op 0xA1 does.
//   `legged` keeps the $1A80 leg counter and takes its ticket from the script byte at ip+2 instead of $1000:
//            ops 0x98/0x99 only, which is why they are three bytes.
// Note: C0/568B reloads X = $6D itself, so every arm that calls it acts on self whatever X held.
static void ctVmMoveActorOp(uint8_t* r, const uint8_t* rom, int idx, int ip,
                            int viaSlot, int anim, int legged, int adv, CtHRet* h) {
  if (legged) {                                       // 534C -- op 0x92's $1A80 head
    if (r[0x1A80 + idx] != 0) {
      if (r[0x1A01 + idx] != 0) { h->ip = ip; h->carry = 0; h->ok = 1; return; }   // 5354 / 5356
      r[0x1A80 + idx] = 0x00;                         // 5359 STZ $1A80,X
      h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 535C -- no 568B on this arm
    }
  } else if (r[0x1A01 + idx] != 0) {                  // 5217 / 52C2
    h->ip = ip; h->carry = 0; h->ok = 1; return;      // 521C / 52C7 TYX / CLC
  }
  int tgt = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 5223 / 52CE / 5366
  if (viaSlot) tgt = ctVmPartySlotTarget(r, tgt);     // 5423 / 5508 / 552F LDA $97,X
  // The target-inactive branch lands on a different instruction in each variant: 0x9E/0x9F's 52D6 -> 531E is a
  // bare TYX; 0x94/0x95's 522B -> 5273 is JSR C0/568B; 0x98/0x99's 536E -> 53B6 is LDX $6D first, which also
  // clears self's $1A80.
  if (r[0x1100 + tgt] & 0x80) {                       // 5228/522B, 52D3/52D6, 536B/536E
    if (legged) r[0x1A80 + idx] = 0x00;               // 53B8
    if (anim) moveArriveAnim(r, idx);                 // 5273 / 53BB
    h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 5276 / 531E / 53BE
  }
  r[0x01F2] = r[0x1801 + tgt];                        // 522D / 52D8 / 5370
  r[0x01F3] = r[0x1881 + tgt];                        // 5232 / 52DD / 5375
  r[0x01E9] = (uint8_t)tgt; r[0x01EA] = (uint8_t)(tgt >> 8);   // 5237 STX $E9 -- 16-bit
  r[0x01F0] = r[0x1801 + idx];                        // 523B / 52E6 / 537E
  r[0x01F1] = r[0x1881 + idx];                        // 5240 / 52EB / 5383
  // Adjacency is wrap-exact on each axis (CMP / INC A / CMP / DEC A DEC A / CMP, 8-bit), not a signed |d| <= 1.
  int sy = r[0x1881 + idx], sx = r[0x1801 + idx];
  int yAdj = (sy == r[0x01F3]) || (((sy + 1) & 0xFF) == r[0x01F3]) ||
             (((sy - 1) & 0xFF) == r[0x01F3]);        // 5245-5252 / 52F0-52FD / 5388-5395
  int xAdj = (sx == r[0x01F2]) || (((sx + 1) & 0xFF) == r[0x01F2]) ||
             (((sx - 1) & 0xFF) == r[0x01F2]);        // 5254-5263 / 5301-530E / 5399-53A6
  if (yAdj && xAdj) {
    // BIT #02: bit 1 of $1C81 selects C0/30B3 (ops 0x97/0xA1 test bit 0 and select C0/305D).
    if ((r[0x1C81 + idx] & 2) && CtVmMoveSnap30B3(r, idx)) {   // 526A/526E, 5316/5319, 53AE/53B1
      if (anim) ctMoveStepAnim5614(r, idx);           // 527B / 53C4 -- $1A80 is not cleared
      h->ip = ip; h->carry = 0; h->ok = 1; return;    // 527E / 5323 / 53C7
    }
    if (legged) r[0x1A80 + idx] = 0x00;               // 53B8
    if (anim) moveArriveAnim(r, idx);                 // 5273 / 53BB
    h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 5276 / 531E / 53BE
  }
  int ee = CtMoveDirAngle(r, rom);                    // 5281 / 5326 / 53CA JSR C0/ABA2
  r[0x01EE] = (uint8_t)ee;
  if (anim) r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));          // 528B/5290 / 53D4/53D9
  if (CtCollisionActorMovable(r, idx)) {              // 5293 / 5329 / 53DC JSR C0/5B95
    ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;             // 5298-529D / 532E / 53E1
    if (anim) r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));        // 52A0/52A5 -- the second write
  }
  // The ticket: the legged pair takes the script byte at ip+2, the others $1000; $7F0B00 always gets the same
  // byte as $1A01.
  int tk = legged ? r[0x12001 + ((ip + 2) & 0xFFFF)] : r[0x1000 + idx];   // 52AA / 5337 / 53F4
  r[0x1A01 + idx] = (uint8_t)tk;                      // 52AD / 533A / 53FA
  r[0x10B00 + idx] = (uint8_t)tk;                     // 52B0 / 533D / 53FD
  if (legged) r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);   // 5401 INC $1A80,X
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);        // 52B4 / 5341 / 5404 STY $C7
  CtBuildVelocityPriority(r, rom, idx);               // 52B6 / 5343 / 5406 JSR C0/ACFD
  if (anim) ctMoveStepAnim5614(r, idx);               // 52B9 / 5409 JSR C0/5614
  h->ip = ip; h->carry = 0; h->ok = 1;                // 52BC / 5346 / 540C LDX $C7 / CLC
}

// C0/32C8 and its four copies: release one party slot's object. Ops 0xD9 and 0xDA hold five near-identical copies,
// differing in the DP slot pair and the literal parked in $1180.
//   `xr`  the index the reads use. Op 0xD9's first copy loads it with a 16-bit LDX $97 (32CA); the other four use
//         TAX (3350, 33D7, 34A3, 34F6) on an 8-bit A with a zeroed high half. They differ only when $0198/$019A/
//         $019C is non-zero.
//   `lit` $171F / $173F / $175F for 0xD9's three slots, $16FF for both of 0xDA's.
static void ctVmPartyRelease(uint8_t* r, int dp, int xr, int lit) {
  int prio = r[(0x1C00 + xr) & 0xFFFF];                          // 32CC LDA $1C00,X
  int prod = (prio * 0x80) & 0xFFFF;                             // 32CF/32D2 $4202 x $4203 -> $4216
  int dpv  = r[dp] | (r[dp + 1] << 8);                           // 32DF ADC $97 -- a 16-bit read
  int off  = (prod + dpv) & 0xFFFF;                              // 32DB/32DC/32DF CLC / LDA / ADC
  r[0x01D9] = (uint8_t)off; r[0x01DA] = (uint8_t)(off >> 8);     // 32E1 STA $D9 (16-bit)
  int slot = r[(0x1180 + xr) & 0xFFFF] | (r[(0x1181 + xr) & 0xFFFF] << 8);   // 32E3 (16-bit)
  // STA $7F0580,X is a long store, so the 24-bit sum could leave WRAM; guarded, not modelled (no reachable offset
  // gets near it).
  { int ea = (0x7F0580 + off) & 0xFFFFFF;                        // 32E8
    if (ea >= 0x7E0000 && ea <= 0x7FFFFE) {
      r[ea - 0x7E0000]     = (uint8_t)slot;
      r[ea - 0x7E0000 + 1] = (uint8_t)(slot >> 8); } }
  r[(0x1180 + dpv) & 0xFFFF] = (uint8_t)lit;                     // 32EC LDX $97 -- 16-bit, always
  r[(0x1181 + dpv) & 0xFFFF] = (uint8_t)(lit >> 8);              // 32F1 STA $1180,X (16-bit)
  // SEP #30 at 32F4 zeroes XH, so the four stores below index by the low byte of the value just loaded.
  int x8 = dpv & 0xFF;
  r[0x1C00 + x8] = 0x00;                                         // 32F8
  r[0x1A80 + x8] = 0x00; r[0x1A01 + x8] = 0x00; r[0x1001 + x8] = 0x00;   // 32FB / 32FE / 3301
}

// C0/3284 and C0/3484: the five-store clear both ops run before releasing anything. The ROM stores to $7F0C00,X and
// $7F0D00,X twice each with X unchanged (`9F 00 0C 7F` x2, `9F 00 0D 7F` x2, `9F 00 0F 7F`); kept as is.
static void ctVmPartyClear(uint8_t* r) {
  r[0x01AC] = r[0x01AB]; r[0x01AD] = r[0x01AB];                  // 327D/327F/3281 LDA $AB / STA $AC/$AD
  int x = r[0x01AB];                                             // 3283 TAX (zero-extended)
  r[0x10C00 + x] = 0x00; r[0x10C00 + x] = 0x00;                  // 3286 / 328A -- the same address
  r[0x10D00 + x] = 0x00; r[0x10D00 + x] = 0x00;                  // 328E / 3292 -- likewise
  r[0x10F00 + x] = 0x00;                                         // 3296
}

// C0/3306, 338B and 3412 -- stash one slot's two script-supplied coordinates and the quadrant their
// sign bits name. Three rows of stride $20 at $7F3728 / $7F3748 / $7F3768.
static void ctVmPartyDest(uint8_t* r, int ip, int step, int row) {
  int a = r[0x12001 + ((ip + step) & 0xFFFF)];                   // 3309
  r[0x01DB] = (uint8_t)a;                                        // 330D STA $DB
  r[0x10000 + row]     = (uint8_t)(a & 0x7F);                    // 3311 STA $7F3728
  int b = r[0x12001 + ((ip + step + 1) & 0xFFFF)];               // 3316
  r[0x01DD] = (uint8_t)b;                                        // 331A STA $DD
  r[0x10000 + row + 1] = (uint8_t)(b & 0x7F);                    // 331E STA $7F3729
  // 3322-333C: the two BMI pairs collapse to the two sign bits, high one first.
  r[0x10000 + row + 3] = (uint8_t)(((a & 0x80) ? 2 : 0) | ((b & 0x80) ? 1 : 0));   // 333C
}

// ---- The player's script opcodes ----
// The runner's contract (C0/5A93): a handler is entered with Y = the IP of its own opcode byte and $6D = the
// object index (*2); it returns the new IP in X and the continue/yield decision in the carry (5AB4 BCC yields the
// visit; SEC spends one unit of the $0167 budget and dispatches the next opcode at once).

// The 24-bit store target of op 0x4A in the flat 128 KB image: $7E at 0, $7F at 0x10000, and banks $00-$3F /
// $80-$BF below $2000 as the low-RAM mirror of $7E. Anything else (ROM, MMIO) is not modelled: returns -1.
static int ctFlatAddr24(int bank, int addr) {
  if (bank == 0x7E) return addr & 0xFFFF;
  if (bank == 0x7F) return 0x10000 + (addr & 0xFFFF);
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && (addr & 0xFFFF) < 0x2000) return addr & 0xFFFF;
  return -1;
}

// The hardware divider: 16-bit dividend in $4204/$4205, 8-bit divisor in $4206, quotient at $4214 (the ROM covers
// the latency with seven NOPs). Division by zero is not covered by the ROM (op 0xFE has no zero guard); $FFFF is
// the documented hardware result, and --sweepops seeds a zero divisor to check it.
static int ctSnesDiv16(int dividend, int divisor) {
  if ((divisor & 0xFF) == 0) return 0xFFFF;
  return ((dividend & 0xFFFF) / (divisor & 0xFF)) & 0xFFFF;
}

// C0/67E3 -- the poke family's prologue. Three script bytes become the 24-bit pointer at
// $01D0/$01D1/$01D2 and X is left at ip+4. Ops 0x4A, 0x4B, 0x4C and 0x4D all `JSR` it and differ
// only in what they then move through it, and how wide.
static int ctVmPtr67E3(uint8_t* r, const uint8_t* script, int ip) {
  int lo  = script[(ip + 1) & 0xFFFF];                    // 67E5
  int mid = script[(ip + 2) & 0xFFFF];                    // 67EC
  int bk  = script[(ip + 3) & 0xFFFF];                    // 67F3
  r[0x01D0] = (uint8_t)lo; r[0x01D1] = (uint8_t)mid; r[0x01D2] = (uint8_t)bk;   // 67E9/67F0/67F7
  return (bk << 16) | (mid << 8) | lo;
}

// `87 STA [$D0]`: a store through that pointer, 8 or 16 bits wide. The destination is script-supplied and not
// clamped; ctFlatAddr24 returns -1 outside the modelled WRAM. A 16-bit store carries into the bank byte, so the
// second byte is resolved separately.
static void ctVmPokeLong(uint8_t* r, int full24, int val, int wide) {
  int fa = ctFlatAddr24((full24 >> 16) & 0xFF, full24 & 0xFFFF);
  if (fa >= 0) r[fa] = (uint8_t)val;
  if (!wide) return;
  int nx = (full24 + 1) & 0xFFFFFF;
  int fb = ctFlatAddr24((nx >> 16) & 0xFF, nx & 0xFFFF);
  if (fb >= 0) r[fb] = (uint8_t)(val >> 8);
}

// The variable-assignment grid: ops 0x50-0x56, 0x58, 0x59, ROM C0/68C6..6A11. Locals ($7F0200 + 2n) and globals
// ($7F0000 + word) copied into one another or loaded from the script. Shared with the live VM (ct_vm.c step() and
// runFunc). A local word is little-endian in WRAM, like C0/42C9's LDA #0008 / STA $0D00,X on the same array.
// Returns 1 if `script[ip]` is in the grid.
int CtVmVarFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op < 0x50 || op > 0x59 || op == 0x57) return 0;
  // Nine handlers, all INY-driven, SEC and straight-line, varying in source, destination and store width:
  //
  //   op  len  source                       destination            store
  //   50   4   immediate word  @ip+1        local[byte @ip+3]      16-bit
  //   51   3   local[byte @ip+1]            local[byte @ip+2]       8-bit
  //   52   3   local[byte @ip+1]            local[byte @ip+2]      16-bit
  //   53   4   global[word @ip+1]           local[byte @ip+3]       8-bit
  //   54   4   global[word @ip+1]           local[byte @ip+3]      16-bit
  //   55   2   global[0], read 8-bit        local[byte @ip+1]       8-bit
  //   56   4   immediate byte  @ip+1        global[word @ip+2]      8-bit
  //   58   4   local[byte @ip+1]            global[word @ip+2]      8-bit
  //   59   4   local[byte @ip+1]            global[word @ip+2]     16-bit
  //
  // A local index is a byte and is doubled (ASL A, stride 2, the array of ops 0x12 and 0x71..0x77); a global
  // index is a word used raw. The 8/16-bit pairs (0x51/0x52, 0x53/0x54, 0x58/0x59) differ only in where SEP #20
  // sits relative to the store.
  struct { int len, sw; } K;
  int val, dst;
  switch (op) {
    case 0x50: { val = script[(ip + 1) & 0xFFFF] | (script[(ip + 2) & 0xFFFF] << 8);
                 r[0x01D9] = (uint8_t)val; r[0x01DA] = (uint8_t)(val >> 8);   // 68CE, 16-bit
                 dst = 0x10200 + ((script[(ip + 3) & 0xFFFF] * 2) & 0xFFFF);
                 K.len = 4; K.sw = 16; } break;
    case 0x51: case 0x52: {
                 int s = 0x10200 + ((script[(ip + 1) & 0xFFFF] * 2) & 0xFFFF);
                 val = r[s] | (r[s + 1] << 8);                                // 68F6 / 6920
                 r[0x01D9] = (uint8_t)val; r[0x01DA] = (uint8_t)(val >> 8);   // 68FA / 6924
                 dst = 0x10200 + ((script[(ip + 2) & 0xFFFF] * 2) & 0xFFFF);
                 K.len = 3; K.sw = (op == 0x52) ? 16 : 8; } break;
    case 0x53: case 0x54: {
                 int g = script[(ip + 1) & 0xFFFF] | (script[(ip + 2) & 0xFFFF] << 8);
                 val = r[0x10000 + (g & 0xFFFF)] | (r[0x10000 + ((g + 1) & 0xFFFF)] << 8);
                 r[0x01D9] = (uint8_t)val; r[0x01DA] = (uint8_t)(val >> 8);   // 694A / 6971
                 dst = 0x10200 + ((script[(ip + 3) & 0xFFFF] * 2) & 0xFFFF);
                 K.len = 4; K.sw = (op == 0x54) ? 16 : 8; } break;
    case 0x55: { val = r[0x10000];                                            // 699A, 8-bit
                 dst = 0x10200 + ((script[(ip + 1) & 0xFFFF] * 2) & 0xFFFF);
                 K.len = 2; K.sw = 8; } break;
    case 0x56: { val = script[(ip + 1) & 0xFFFF];
                 r[0x01D9] = (uint8_t)val;         // 69AC STA $D9 -- 8-bit, the REP is at 69AF
                 dst = 0x10000 + ((script[(ip + 2) & 0xFFFF] |
                                  (script[(ip + 3) & 0xFFFF] << 8)) & 0xFFFF);
                 K.len = 4; K.sw = 8; } break;
    default:   { int s = 0x10200 + ((script[(ip + 1) & 0xFFFF] * 2) & 0xFFFF);
                 val = r[s] | (r[s + 1] << 8);                                // 69D1 / 69F8
                 r[0x01D9] = (uint8_t)val; r[0x01DA] = (uint8_t)(val >> 8);   // 69D5 / 69FC
                 dst = 0x10000 + ((script[(ip + 2) & 0xFFFF] |
                                  (script[(ip + 3) & 0xFFFF] << 8)) & 0xFFFF);
                 K.len = 4; K.sw = (op == 0x59) ? 16 : 8; } break;            // 0x58 / 0x59
  }
  r[dst] = (uint8_t)val;
  if (K.sw == 16) r[dst + 1] = (uint8_t)(val >> 8);
  *outIp = (ip + K.len) & 0xFFFF; return 1;
}

// The poke family: ops 0x4A/0x4B/0x4C/0x4D, ROM C0/67D7 / 67FB / 680C / 6829: a byte or a word, literal or from a
// local, through the 24-bit pointer C0/67E3 stages at $01D0-$01D2. Shared with the live VM (ct_vm.c step()); for
// example `4C/4D 00 02 7E nn` puts a local into the text number slot $0200 read by the {num} codes. `script` +
// `ip` as in CtVmItemFamily. Carry is set on every exit. Returns 1 if `script[ip]` is in the family.
int CtVmPokeFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  switch (op) {
    case 0x4A: {
      // C0/67D7: write a byte through the script's 24-bit pointer (lo/mid/bank at $01D0-$01D2); operand 4 is the
      // value (67DE STA [$D0]). Branchless, ip+5, SEC.
      int p = ctVmPtr67E3(r, script, ip);                       // 67D7 JSR C0/67E3
      ctVmPokeLong(r, p, script[(ip + 4) & 0xFFFF], 0);       // 67DA / 67DE STA [$D0]
      *outIp = (ip + 5) & 0xFFFF; return 1;        // 67E0 INX / SEC
    }
    case 0x4B: {
      // C0/67FB: op 0x4A's 16-bit twin (REP #20 before the load): six bytes, pokes a word.
      int p = ctVmPtr67E3(r, script, ip);
      int v = script[(ip + 4) & 0xFFFF] | (script[(ip + 5) & 0xFFFF] << 8);  // 6800
      ctVmPokeLong(r, p, v, 1);                                      // 6804 STA [$D0] (16-bit)
      *outIp = (ip + 6) & 0xFFFF; return 1;        // 6808 INX / INX / SEC
    }
    case 0x4C:
    case 0x4D: {
      // C0/680C and C0/6829: poke a local: the fourth operand indexes the $7F0200 word array. The two differ only in
      // where SEP #20 sits: before the load in 0x4C (8-bit fetch and store), after the store in 0x4D (16-bit).
      int p = ctVmPtr67E3(r, script, ip);
      int sel = script[(ip + 4) & 0xFFFF];                    // 680F / 682C (8-bit)
      int xs = (ip + 4) & 0xFFFF;
      r[0x01D9] = (uint8_t)xs; r[0x01DA] = (uint8_t)(xs >> 8);       // 6813 STX $D9 (16-bit)
      int off = (sel * 2) & 0xFFFF;                                  // 681A / 6837 ASL A
      int lo = r[0x10000 + ((0x0200 + off) & 0xFFFF)];               // 681E / 6839 LDA $7F0200,X
      if (op == 0x4C) {
        ctVmPokeLong(r, p, lo, 0);                                   // 6822 (8-bit)
      } else {
        int hi2 = r[0x10000 + ((0x0201 + off) & 0xFFFF)];
        ctVmPokeLong(r, p, lo | (hi2 << 8), 1);                      // 683D (16-bit)
      }
      *outIp = (ip + 5) & 0xFFFF; return 1;        // 6824/6841 LDX $D9 / INX
    }
    default: return 0;
  }
}

// C0/5F74: op 0x00, the return that partners op 0x02's call.
//   prio == 7 -> 5FB3 TYX / CLC: no writes, IP unchanged; the object parks on the op (priority 7 = nothing to
//                return to, the idle terminator).
//   prio <  7 -> clear our own $7F0580 slot (16-bit), then walk down in urgency (slot += 0x80, INC $1C00,X per
//                step) until a non-zero slot appears, and return that word as the new IP. It is an absolute script
//                address and can be behind the entry IP.
// Note: the ROM writes each intermediate $1C00 value (prio+1, prio+2, ...).
static int ctVmOp00(uint8_t* r, int idx, int ip, int* carry) {
  int cur = r[0x1C00 + idx];                             // 5F76
  if (cur == 7) { *carry = 0; return ip; }               // 5F79 CMP #07 / 5F7B BEQ ->5FB3
  int sl = 0x10580 + (((cur * 0x80) + idx) & 0xFFFF);    // 5F7D-5F8F: the x128 hardware multiply
  r[sl] = 0; r[sl + 1] = 0;                              // 5F90 LDA #0000 / 5F93 STA $7F0580,X (16-bit)
  int p = cur;
  for (int guard = 0; guard < 8; guard++) {              // port-side guard: the ROM has no bound here and
                                                         // trusts a non-zero slot to exist.
    sl += 0x80;                                          // 5F97 TXA / CLC / ADC #0080
    p++;
    r[0x1C00 + idx] = (uint8_t)p;                        // 5FA0 INC $1C00,X -- once per step
    int v = r[sl] | (r[sl + 1] << 8);                    // 5FA6 LDA $7F0580,X
    if (v) { *carry = 1; return v; }                     // 5FAA BNE -> 5FAC/5FAE TAX = the new IP
  }
  *carry = 1; return ip;                                 // unreachable in a well-formed scene
}

// C0/6467: the comparison predicate table the conditional branches dispatch through (6307 / 6433 JSR ($6467,X),
// X = mode*2). Each handler exits CLC when the predicate holds (the shared exits C0/64B3, C0/651F) and SEC when
// not, so the branch ops jump on SEC ("jump if not").
// Sixteen modes: 0-7 compare the 8-bit $01D9 with $01DB; 8-15 are the same predicates on the 16-bit pairs
// $01D9/$01DA vs $01DB/$01DC (each opens with REP #20: C0/64CF, 64DB, 64E7, 64F5, 6503, 6511, 6523, 652F).
// Note: src/ct_vm.c's cmp_ masks the mode with & 7.
static int ctVmCompare(const uint8_t* r, int mode) {
  int wide = (mode & 8) != 0;
  int l  = wide ? (r[0x01D9] | (r[0x01DA] << 8)) : r[0x01D9];
  int rr = wide ? (r[0x01DB] | (r[0x01DC] << 8)) : r[0x01DB];
  switch (mode & 7) {
    case 0:  return l == rr;               // 6487 / 64CF  CMP / BEQ -> CLC
    case 1:  return l != rr;               // 648F / 64DB  CMP / BNE -> CLC
    case 2:  return l >  rr;               // 6497 / 64E7  BEQ -> SEC, then BCS -> CLC
    case 3:  return l <  rr;               // 64A1 / 64F5  BCC -> CLC
    case 4:  return l >= rr;               // 64A9 / 6503  BEQ or BCS -> CLC
    case 5:  return l <= rr;               // 64B5 / 6511  BEQ or BCC -> CLC
    case 6:  return (l & rr) != 0;         // 64BF / 6523  AND / BNE -> CLC
    default: return (l | rr) != 0;         // 64C7 / 652F  ORA / BNE -> CLC
  }
}

// C0/6349: the taken-branch tail shared by ops 0x12 (630A BCS ->6349) and 0x16 (6436 BCC ->643B / 6438 BRL ->6349):
//   6349 REP #10 / LDX $C3 (the spilled base) / 634D LDA $7F2001,X (the displacement) / 6351-6356 STA $D9 (16-bit,
//   overwriting the compared value) / 6358 TXA / CLC / ADC $D9 / TAX / 635F SEC.
// The base is whatever the op spilled to $01C3, the displacement's own address: ip+4 for ops 0x12, 0x14, 0x15 and
// 0x16, ip+5 for op 0x13 (a two-byte immediate comparand).
static void ctVmBranchTakenAt(uint8_t* r, int base, CtHRet* h) {
  base &= 0xFFFF;
  int disp = r[0x12001 + base];
  r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;
  h->ip = (base + disp) & 0xFFFF; h->carry = 1; h->ok = 1;
}

// C0/3570 (op 0xBB) and its two siblings C0/35B5 (0xC1) / C0/35FC (0xC2) are one routine with
// a single hardwired difference: the value stored to $0130. `pos` is that value.
static void ctVmTextRequest(uint8_t* r, int idx, int ip, CtHRet* hp, int pos) {
  CtHRet h = *hp;

        // C0/3570: arm a dialogue/text request and wait on it (3207 static sites in the 504 scenes). Four arms;
        // only one consumes the op's two bytes.
        // Arm 1: 3570 LDA $2D / 3572 BNE ->357A continues when $012D is non-zero. With $012D == 0 it falls into
        // 3574 LDX #$7FE0 (16-bit immediate) / 3577 BRL C0/2E1E: forced blank, NMI and IRQ off, CGRAM painted from
        // X, then BRA -2 forever, a fatal-error hang. $012D is a required precondition; the port traps.
        if (r[0x012D] == 0) { h.ip = ip; h.carry = 0; h.ok = 0; *hp = h; return; }
        // 357C: a request is already in flight -- park on the opcode and yield, changing nothing.
        if (r[0x0129] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; *hp = h; return; }   // 35B2 TYX / 35B3 CLC
        // 3584: our request has completed (the per-object flag is set): clear it and advance. The only arm that
        // advances the IP.
        if (r[0x10A00 + idx] != 0) {
          r[0x10A00 + idx] = 0x00;                          // 35A7 LDA #00 / 35A9 STA $7F0A00,X
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; *hp = h; return;   // 35AD TYX/INX/INX + 35B0 SEC
        }
        // 3586: issue the request, then park and yield until it completes.
        r[0x10A00 + idx] = 0x01;                            // 3586/3588
        r[0x012A] = r[0x12001 + ((ip + 1) & 0xFFFF)];       // 358C-358E operand / 3592 STA $2A
        r[0x012E] = r[0x016D];                              // 3594/3596 -- 8-bit, so the low byte only
        r[0x0132] = 0x00;                                   // 3598 STZ $32
        r[0x0129] = 0x01;                                   // 359A/359C STA $29 -- the in-flight latch
        r[0x0130] = (uint8_t)pos;   // 359E STZ $30 (0xBB) | 35E3/362A LDA #imm / STA $30
        // 35A0 LDA #20 / 35A2 TRB $54 clears bit 5 of $0154 and leaves the other bits.
        r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x20);
        h.ip = ip; h.carry = 0; h.ok = 1; *hp = h; return;            // 35A4 TYX / 35A5 CLC -- park on the op
        *hp = h;
}

// C0/4B2C -- op 0x88's colour-effect slot allocator. Walks $0520 in steps of 12 up to $60, so eight
// twelve-byte slots; a slot is free when its first byte is zero. Returns the byte offset (the ROM leaves it
// in Y, carry clear) or -1 when the table is full (carry set).
static int ctVmOp88Alloc(const uint8_t* r) {
  for (int y = 0; y < 0x60; y += 12)                     // 4B38 ADC #000C / 4B3E CPY #0060
    if (r[0x0520 + y] == 0) return y;                    // 4B2F LDA $0520,Y / 4B32 BEQ ->4B45
  return -1;                                             // 4B47 SEC
}

// Op 0xE2, ROM C0/3B9E: stage a location change from four locals: op 0xE0's fade variant (C0/3AB7: the same $0117
// latch, the same $0119 counter C0/0C8A drains, the same park), with every operand the index of a local. D = $0100:
//   3B9E LDA $17 / BMI ->3C02                    a change already pending: 3C02 TYX / CLC -- park
//   3BA2 STY $C7                                 the entry IP (16-bit)
//   3BA4 INY / TYX / LDA $7F2001,X / REP #20 / AND #00FF / ASL / TAX / LDA $7F0200,X / STA $12
//                                                the location = the local's whole word (no AND #01FF, unlike 3AC5)
//   3BB7 .. SEP #20 / LDA $7F0200,X / STA $14    tile X  = the second local's low byte
//   3BCA .. STA $15                              tile Y  = the third's
//   3BDF .. STA $16                              the facing byte = the fourth's, as it is (no 3AD4 LSR / TSB build)
//   3BF4 LDA $17 / ORA #80 / STA $17 / LDA #0F / STA $19
//   3BFE LDX $C7 / CLC / RTS                     X = the entry IP: the object parks on the op
// Example: the inn at loc 00C leaves the way back in $7F0166..016C; loc 10F's obj0 copies it into locals 0A-0D and
// leaves with `E2 0C 0A 0B 0D`. Shared with the live VM (ct_vm.c step()). `script` = the $7F:2001 base.
void CtVmChangeLocVars(uint8_t* r, const uint8_t* script, int ip, CtHRet* out) {
  out->ip = ip; out->carry = 0; out->ok = 1;                         // both exits: X = the entry IP, CLC
  if (r[0x0117] & 0x80) return;                                      // 3BA0 BMI ->3C02
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);           // 3BA2 STY $C7 (16-bit)
  int x1 = script[(ip + 1) & 0xFFFF] * 2, x2 = script[(ip + 2) & 0xFFFF] * 2;
  int x3 = script[(ip + 3) & 0xFFFF] * 2, x4 = script[(ip + 4) & 0xFFFF] * 2;
  r[0x0112] = r[0x10200 + x1]; r[0x0113] = r[0x10200 + x1 + 1];      // 3BB1 LDA $7F0200,X / STA $12 (16-bit)
  r[0x0114] = r[0x10200 + x2];                                       // 3BC4 / 3BC8
  r[0x0115] = r[0x10200 + x3];                                       // 3BD9 / 3BDD
  r[0x0116] = r[0x10200 + x4];                                       // 3BEE / 3BF2
  r[0x0117] = (uint8_t)(r[0x0117] | 0x80);                           // 3BF4-3BF8
  r[0x0119] = 0x0F;                                                  // 3BFA-3BFC
}

// Op 0x2E, ROM C0/4A4E: one op, three jobs selected by the high nibble of operand 1: $40/$50 claim a colour-effect
// record (a $0520 slot through C0/4B2C, for the fades FD/E82C / FD/E5A4), $80 copies an inline palette blob twice,
// anything else is a two-byte no-op; the three arms have different lengths. Shared with the live VM (ct_vm.c
// step() and init pass). `script` is the $7F:2001 base; the carry comes back in `out` (CLC = the table was full).
void CtVmColorSlotOp(uint8_t* r, const uint8_t* script, int ip, CtHRet* out) {
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 4A4E STY $C7 (16-bit)
  int o1 = script[(ip + 1) & 0xFFFF];                    // 4A52
  r[0x01D9] = (uint8_t)o1;                                      // 4A56 STA $D9
  int hi = o1 & 0xF0;                                           // 4A58
  if (hi == 0x40 || hi == 0x50) {                               // 4A5A / 4A61 -- both to 4A72
    int y = ctVmOp88Alloc(r);                                   // 4A72 JSR C0/4B2C
    // A full table still consumes the six bytes; only the carry differs, so the visit yields.
    if (y < 0) { out->ip = (ip + 6) & 0xFFFF; out->carry = 0; out->ok = 1; return; }   // 4A77 x6 / CLC
    r[0x0520 + y] = (uint8_t)(o1 & 0xF0);                       // 4A85
    r[0x0526 + y] = (uint8_t)(o1 & 0x0F);                       // 4A8C
    r[0x0521 + y] = script[(ip + 2) & 0xFFFF];           // 4A97
    r[0x0522 + y] = script[(ip + 3) & 0xFFFF];           // 4A9F
    int dd = script[(ip + 4) & 0xFFFF];                  // 4AA3
    r[0x01DD] = (uint8_t)dd;                                    // 4AA7 STA $DD -- 8-bit here
    r[0x0525 + y] = script[(ip + 5) & 0xFFFF];           // 4AAE
    r[0x0524 + y] = 0x00;                                       // 4AB3
    r[0x0523 + y] = 0x08;                                       // 4AB8
    // The last two fields each replicate one nibble into both halves of the byte.
    r[0x01D9] = (uint8_t)((dd & 0xF0) >> 4);                    // 4AC0 x4 LSR / 4AC4 STA $D9
    r[0x0527 + y] = (uint8_t)((dd & 0xF0) | ((dd & 0xF0) >> 4));    // 4ACC
    r[0x01D9] = (uint8_t)((dd & 0x0F) << 4);                    // 4AD3 x4 ASL / 4AD7 STA $D9
    r[0x0528 + y] = (uint8_t)((dd & 0x0F) | ((dd & 0x0F) << 4));    // 4ADF
    out->ip = (ip + 6) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 4AE2 SEC
  }
  if (hi != 0x80) {                                             // 4A68
    out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 4A6F INX / SEC
  }
  // 4AE4: copy an inline blob twice. The operands are a table index and a 16-bit length, then the payload, so the
  // IP advance depends on the script.
  int o2 = script[(ip + 2) & 0xFFFF];                    // 4AE8
  int ddw = (o2 * 2) & 0xFFFF;                                  // 4AF1 ASL A (16-bit)
  r[0x01DD] = (uint8_t)ddw; r[0x01DE] = (uint8_t)(ddw >> 8);    // 4AF2 STA $DD -- 16-bit,
                                                                // unlike the same store above
  // REP #20 from 4AEC is still in force, so 4AFA reads two bytes.
  int word = script[(ip + 3) & 0xFFFF] | (script[(ip + 4) & 0xFFFF] << 8);
  int cnt = (word - 3) & 0xFFFF;                                // 4AFE x3 DEC A
  r[0x01C1] = (uint8_t)cnt; r[0x01C2] = (uint8_t)(cnt >> 8);    // 4B01 STA $C1 (16-bit)
  int src = (ip + 5 + 0x2001) & 0xFFFF;                         // 4B05/4B06 TXA / ADC #2001
  r[0x01DB] = (uint8_t)src; r[0x01DC] = (uint8_t)(src >> 8);    // 4B0A STA $DB (16-bit)
  // `54 7E 7F` MVN: destination bank first, so it reads bank $7F (the script) and writes $7E. It moves A+1 bytes,
  // A being the length less three. The second move re-reads the same source from $DB and lands $200 lower.
  for (int pass = 0; pass < 2; pass++) {
    int dstv = ((pass ? 0x2000 : 0x2200) + ddw) & 0xFFFF;       // 4AF5 / 4B16
    for (int k = 0; k <= cnt; k++)
      r[(dstv + k) & 0xFFFF] = script[(ip + 5 + k) & 0xFFFF];   // = $7F:(src + k), src = ip + 5 + $2001
  }
  // 4B22 TXA / SBC #2001 -- X ends past the payload, so this lands on ip + word + 3.
  out->ip = (ip + word + 3) & 0xFFFF; out->carry = 1; out->ok = 1; return;
}

// C0/4892: op 0x88, the per-object colour/palette effect, shared with the live VM in ct_vm.c like CtVmItemFamily
// (the blob's MVN source is the script too). `idx` = obj*2. Always handles the op (out->ok = 1).
//   operand == $00              -> 48C0  restore: free the slot and re-upload the palette
//   operand == $20 or $30       -> 4919  a 4-byte effect (whole values, tested before the mask)
//   (operand & $F0) == $40/$50  -> 4970  a 5-byte effect
//   (operand & $F0) == $80      -> 49F8  a variable-length direct palette upload
//   anything else               -> 48BD  ip+2, SEC, no writes
// 4892 STY $C7 spills the op's own IP (no INY) and every arm advances from it. The slots are C0/4B2C's
// (ctVmOp88Alloc); on a full table the allocating arms still consume their operands and exit CLC.
void CtVmColorFx(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 4892 STY $C7 (16-bit)
  int sel = script[(ip + 1) & 0xFFFF];                   // 4896
  if (sel != 0) r[0x01D9] = (uint8_t)sel;                       // 489C -- not on the zero arm
  int pal = r[0x0F81 + idx];
  if (sel == 0) {
    // 48C0 restore.
    int slot = r[0x10B80 + idx] | (r[0x10B81 + idx] << 8);      // 48C4, 16-bit
    if (slot & 0x8000) {                                        // 48C8 BMI -- nothing active
      out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;   // 4911
    }
    r[0x10B80 + idx] = 0xFF; r[0x10B81 + idx] = 0xFF;           // 48CB/48CE
    r[0x0520 + slot] = 0x00;                                    // 48D6 -- release the slot
    // 48DE-48ED: dest = $7E:($2102 + pal*16). 48F0 `LDA $1400,X` is the 16-bit ROM offset the
    // object loader stashed there; the source bank is $E4 (MVN is `54 dd ss`, dest first).
    int dst = (0x2102 + pal * 16) & 0xFFFF;
    int src = r[0x1400 + idx] | (r[0x1401 + idx] << 8);
    for (int k = 0; k < 0x18; k++) {                            // 48F5 LDA #0017 -> 24 bytes
      int s = 0x240000 + ((src + k) & 0xFFFF);
      r[(dst + k) & 0xFFFF] = (s < romLen) ? MR(s) : 0;        // 48F8 MVN $7E,$E4
    }
    for (int k = 0; k < 0x18; k++) {                            // 4902/4905 -- the same source
      int s = 0x240000 + ((src + k) & 0xFFFF);                  //   again, dest + $200
      r[(dst + 0x200 + k) & 0xFFFF] = (s < romLen) ? MR(s) : 0;
    }
    out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 490B
  }
  if (sel == 0x20 || sel == 0x30) {
    // 4919: a four-byte effect. The selectors are compared whole (489E, 48A2) before 48A6 AND #F0, so $21 is not
    // $20 here.
    int y = ctVmOp88Alloc(r);                                   // 4919 JSR C0/4B2C
    if (y < 0) { out->ip = (ip + 4) & 0xFFFF; out->carry = 0; out->ok = 1; return; }   // 491E, CLC
    r[0x10B80 + idx] = (uint8_t)y; r[0x10B81 + idx] = (uint8_t)(y >> 8);     // 492B, 16-bit
    r[0x0520 + y] = (uint8_t)sel;                               // 4933
    int db = ((pal << 3) + 0x80) & 0xFF;                        // 493B-4941 STA $DB
    r[0x01DB] = (uint8_t)db;
    int o2 = script[(ip + 2) & 0xFFFF];  r[0x01D9] = (uint8_t)o2;     // 4947/494B
    r[0x0525 + y] = script[(ip + 3) & 0xFFFF];           // 4952
    r[0x0524 + y] = 0x00;                                       // 4957
    r[0x0522 + y] = (uint8_t)(o2 & 0x0F);                       // 495F
    r[0x0521 + y] = (uint8_t)(((o2 >> 4) + db) & 0xFF);         // 496B
    out->ip = (ip + 4) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 496E
  }
  if ((sel & 0xF0) == 0x40 || (sel & 0xF0) == 0x50) {
    // 4970: a five-byte effect, selected on the masked nibble.
    int y = ctVmOp88Alloc(r);                                   // 4970 JSR C0/4B2C
    if (y < 0) { out->ip = (ip + 5) & 0xFFFF; out->carry = 0; out->ok = 1; return; }   // 4975, CLC
    r[0x0520 + y] = (uint8_t)(sel & 0xF0);                      // 4982
    r[0x0526 + y] = (uint8_t)(sel & 0x0F);                      // 4989
    int db = ((pal << 3) + 0x80) & 0xFF;                        // 4991-4997
    r[0x01DB] = (uint8_t)db;
    int o2 = script[(ip + 2) & 0xFFFF];  r[0x01D9] = (uint8_t)o2;     // 499D/49A1
    int o3 = script[(ip + 3) & 0xFFFF];  r[0x01DD] = (uint8_t)o3;     // 49A4/49A8
    r[0x0525 + y] = script[(ip + 4) & 0xFFFF];           // 49AF
    r[0x0524 + y] = 0x00;                                       // 49B4
    r[0x0523 + y] = 0x08;                                       // 49B9
    r[0x0522 + y] = (uint8_t)(o2 & 0x0F);                       // 49C1
    r[0x0521 + y] = (uint8_t)(((o2 >> 4) + db) & 0xFF);         // 49CD
    // 49D0-49F3: each nibble of operand 3 is replicated into both halves of its own byte.
    r[0x01D9] = (uint8_t)((o3 & 0xF0) >> 4);                    // 49D8
    r[0x0527 + y] = (uint8_t)((o3 & 0xF0) | ((o3 & 0xF0) >> 4));// 49E0
    r[0x01D9] = (uint8_t)((o3 & 0x0F) << 4);                    // 49EB
    r[0x0528 + y] = (uint8_t)((o3 & 0x0F) | ((o3 & 0x0F) << 4));// 49F3
    out->ip = (ip + 5) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 49F6
  }
  if ((sel & 0xF0) == 0x80) {
    // 49F8: a direct palette upload, the only variable-length op in this file. 4A1C LDA $7F2001,X is 16-bit
    // (REP #20 from 4A0B), so the count is a word at ip+2, and three DEC A make MVN move count-2 bytes. The exit
    // rebuilds the IP from X (4A46 SBC #2001): ip + count + 2.
    int db = ((pal << 3) + 0x80) & 0xFF;                        // 49FD-4A03
    r[0x01DB] = (uint8_t)db;
    int dd = (((sel & 0x0F) | db) * 2) & 0xFFFF;                // 4A05-4A11 STA $DD (16-bit)
    r[0x01DD] = (uint8_t)dd; r[0x01DE] = (uint8_t)(dd >> 8);
    int cnt = script[(ip + 2) & 0xFFFF] |
              (script[(ip + 3) & 0xFFFF] << 8);          // 4A1C, 16-bit
    int n = (cnt - 3) & 0xFFFF;                                 // 4A20-4A22 DEC A x3
    r[0x01C1] = (uint8_t)n; r[0x01C2] = (uint8_t)(n >> 8);      // 4A23 STA $C1 (16-bit)
    int sadr = (ip + 4 + 0x2001) & 0xFFFF;                      // 4A27-4A2C STA $DB (16-bit)
    r[0x01DB] = (uint8_t)sadr; r[0x01DC] = (uint8_t)(sadr >> 8);
    for (int k = 0; k <= n; k++)                                // 4A32 MVN $7E,$7F
      r[(dd + 0x2200 + k) & 0xFFFF] = script[(ip + 4 + k) & 0xFFFF];   // = $7F:(sadr + k)
    for (int k = 0; k <= n; k++)                                // 4A40, dest $2000, same source
      r[(dd + 0x2000 + k) & 0xFFFF] = script[(ip + 4 + k) & 0xFFFF];
    out->ip = (ip + cnt + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;
  }
  out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;       // 48BD -- the default arm
}

// Op 0x80's two workers are defined next to CtObjLoad (they share its descriptor walk).
static int  ctPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn);
static void ctPcPlaceDefault(uint8_t* r, int idx);

// C0/4B9F-4BBD and C0/4C4F-4C6E: one tile-attribute block with two entries. Op 0x8B reaches it with a tile built
// from its operands, op 0x8D with the object's current tile; the two copies are the same instructions.
// The reads are LDA long,X, so $7E7000 + t can cross into $7F (a plain add in the flat image). Rows are 64 tiles on
// a 256-byte pitch, so the planes at +$00 and +$40 do not overlap (the same layout as the plane at $7E7080).
static void ctVmTileAttr(uint8_t* r, int idx, int t) {
  int a1 = ((r[0x7000 + t] & 0x40) >> 2) | 0x20;     // AND #40 / LSR / LSR / ORA #20
  r[0x01D9] = (uint8_t)a1;                           // 4BA9 / 4C59 -- $D9 is reused, no longer the tile
  int a2 = ((r[0x7040 + t] & 0x40) >> 2) | 0x20;     // the second plane
  r[0x0C00 + idx] = (uint8_t)a2;                     // 4BB7 / 4C67 -- from $7E7040
  r[0x0C01 + idx] = (uint8_t)a1;                     // 4BBA / 4C6A -- from $7E7000, via $D9
}

// C0/426A: the join op 0x80 and its seven immediate siblings share. Op 0x80 fetched an operand with 421E INY (Y =
// ip+1 here); ops 0x57/0x5C/0x62/0x68/0x6A/0x6C/0x6D hardwire the id and leave Y = ip. C0/448D's own INY then gives
// 0x80 a two-byte encoding and the siblings a one-byte one. `yIn` is Y at 426A.
static void ctVmPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn,
                       CtHRet* hp) {
  // 426C / 4276 / 4281: the three active party slots, $7E2980-2982.
  int slot = (id == r[0x2980]) ? 0 : (id == r[0x2981]) ? 1 : (id == r[0x2982]) ? 2 : -1;
  if (slot < 0) {
    // 428C: not in the party. The only arm that does not call C0/448D, so Y is still ip+1 and it supplies the
    // INX at 4299 the other arms do not need. All four arms return ip+2.
    r[0x1100 + idx] = 0x80;                            // 4290 -- inactive
    r[0x1101 + idx] = (uint8_t)id;                     // 4295
    hp->ip = (yIn + 1) & 0xFFFF; hp->carry = 1; hp->ok = 1; return;
  }
  // 429C / 4310 / 43C3 -- one skeleton, three per-slot constants.
  static const struct { int spill, slotTbl, oamBase, tplBase; } PS[3] = {
    { 0x0197, 0x0B89, 0x0008, 0x0000 },                // 42B3 / 42C4 / 42CC / 42D2
    { 0x0199, 0x0B8A, 0x0020, 0x0020 },                // 4327 / 4339 / 4341 / 4347
    { 0x019B, 0x0B8B, 0x0028, 0x0040 },                // 43DA / 43EC / 43F4 / 43FA
  };
  r[0x1100 + idx] = (uint8_t)slot;                     // 429E -- A still holds the slot number
  r[0x1C80 + idx] = 0x00;                              // 42A1 STZ $1C80,X
  r[0x1A81 + idx] = 0x01;                              // 42A6
  r[0x1B01 + idx] = 0x01;                              // 42AB -- C0/448D does not write this
  r[0x1101 + idx] = (uint8_t)id;                       // 42B0 LDA $D9 / STA $1101,X
  // 42B3 STX $97 is 16-bit (X is wide from the runner's REP #10 at 5A9E), so it writes $0197 and $0198; STX $0B89
  // below likewise touches $0B8A, slot 1's entry, which slot 1 overwrites in its turn.
  r[PS[slot].spill]     = (uint8_t)idx;
  r[PS[slot].spill + 1] = (uint8_t)(idx >> 8);
  ctPcLoad(r, rom, romLen, idx, id, yIn);                    // 42B5 / 4329 / 43DC JSR C0/448D
  if (slot != 0) {                                     // 432E / 43E1 -- the leader has no such
    if (r[0x0178]) { r[0x0178] = 0x02;                 //   block; slots 1 and 2 both do
         r[0x1601 + idx] = 0x00; } }       //   `LDA $78 / BEQ / LDA #02 / STZ $1601,X`
  r[PS[slot].slotTbl]     = (uint8_t)idx;              // 42C4 STX $0B89 -- 16-bit, see above
  r[PS[slot].slotTbl + 1] = (uint8_t)(idx >> 8);
  r[0x0D00 + idx] = (uint8_t)PS[slot].oamBase;         // 42CC STA $0D00,X (16-bit)
  r[0x0D01 + idx] = (uint8_t)(PS[slot].oamBase >> 8);
  r[0x1700 + idx] = (uint8_t)PS[slot].tplBase;         // 42D2 STA $1700,X (16-bit)
  r[0x1701 + idx] = (uint8_t)(PS[slot].tplBase >> 8);
  r[0x1000 + idx] = (uint8_t)((r[0x1000 + idx] & 0x80) | 0x01);   // 42D7-42DE: keep bit 7 only
  if (slot == 0) {
    // The leader always takes the default placement. In the ROM its five stores straddle the block above
    // (42BA/42BF before, 42E1/42E8/42FA after); the addresses are distinct, so grouping them changes nothing. They
    // must come after C0/448D, which sets $1600,X = 1 at 454D (overwritten at 42E5).
    ctPcPlaceDefault(r, idx);
  } else {
    r[0x01AB] = 0x00; r[0x01AC] = 0x00; r[0x01AD] = 0x00;   // 4356 / 4409 STZ $AB/$AC/$AD --
                                                // the party path-history cursor
    int lead = r[0x0197];                              // 435C / 440F LDA $97 (8-bit)
    if (lead & 0x80) ctPcPlaceDefault(r, idx);         // 435E BMI -- bit 7 = no leader
    else {
      // 4360-4386: follow the leader. 4362 AND #00FF / 4365 TAX zero-extends the byte for the $1800 read, but 436E
      // and 437D are 16-bit LDX $97 and also read $0198. $0198 is 0 in every reachable state (42B3's 16-bit STX
      // writes it), so the two agree; both widths are kept as the ROM has them.
      int leadW = (r[0x0197] | (r[0x0198] << 8)) & 0x1FFFF;
      r[0x1800 + idx] = r[0x1800 + lead];              // 4366 / 436B, via the zero-extended X
      r[0x1801 + idx] = r[0x1801 + lead];
      r[0x1880 + idx] = r[0x1880 + leadW];             // 4370 / 4375, via the wide X
      r[0x1881 + idx] = r[0x1881 + leadW];
      r[0x1900 + idx] = 0x00; r[0x1901 + idx] = 0x00;  // 4378 STZ $1900,X -- 16-bit, so
      r[0x1600 + idx] = r[0x1600 + leadW];             // 437F / 4384, also via the wide X    it clears both
    }
  }
  hp->ip = (yIn + 1) & 0xFFFF; hp->carry = 1; hp->ok = 1;
}


// ---- The $C2 party / roster / inventory-count family: C0/388D (0xCF), 38BE (0xD0), 38D3 (0xD1), 38E8 (0xD2),
// 37F9 (0xD7), 3919 (0xD3), 394E (0xD4), 3E5C/3E6E/3E75 (0xF8/0xF9/0xFA) ----
// Ten ops over 896 static sites, all reaching JSL $C2:8004 with a leaf index. Shared with the live VM (ct_vm.c),
// see disasm/notes/vm_two_implementations.md.
// The carry varies: 0xD0/0xD1/0xD3 end the visit (no SEC/CLC, entry carry 0), 0xD4's is its last CMP $D9, the rest
// set it; so it comes back in `out`. The branch polarity is the opposite of the $C1 side's: 0xCF/0xD2 jump when the
// leaf returns non-zero (PC absent). `script` + `ip` as in CtVmItemFamily. Returns 1 if `script[ip]` is in it.
int CtVmPartyGateFamily(uint8_t* r, const uint8_t* rom, int romLen,
                        const uint8_t* script, int ip, CtHRet* out) {
  int op = script[ip];
  switch (op) {
      case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD7: {
        // The simple half: C0/388D (0xCF), 38BE (0xD0), 38D3 (0xD1), 38E8 (0xD2), 37F9 (0xD7). One prologue:
        //   TYX / INX / TDC / XBA / LDA $7F2001,X / INX / STX $C7 / <stage> / LDA #leaf / JSL $C2:8004
        // so the operand is at ip+1 and $01C7 (16-bit) gets ip+2. The staging decides what the gate's STY $C0 /
        // STX $C9 record: 0xCF/0xD0/0xD1/0xD2 TAY (Y = operand, X = ip+2); 0xD7 TAX (X = the item id, which leaf 12
        // reads from $04C9, and Y = the runner's raw IP into $04C0).
        int arg = script[(ip + 1) & 0xFFFF];                   // 38EC / 37FD
        int spill = (ip + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 38F1 STX $C7, 16-bit
        int leaf = (op == 0xD2) ? 0 : (op == 0xCF) ? 3 : (op == 0xD0) ? 4 : (op == 0xD1) ? 5 : 12;
        int a = ctVmC2Gate(r, rom, romLen, leaf, op == 0xD7 ? ip : arg, op == 0xD7 ? arg : spill, 0);
        if (op == 0xD0 || op == 0xD1) {
          // 38D0 LDX $C7 / RTS, no SEC or CLC: the gate's PHP/PLP returns the entry carry 0, so the visit ends.
          out->ip = spill; out->carry = 0; out->ok = 1; return 1;
        }
        if (op == 0xD7) {
          // 380B STA $D9 (8-bit) then the count is mirrored into the scene array at operand2*2.
          r[0x01D9] = (uint8_t)a;                                     // 380B
          int slot = script[spill];                              // 380F LDA $7F2001,X
          r[0x10200 + ((slot * 2) & 0xFFFF)] = (uint8_t)a;            // 381E STA $7F0200,X
          out->ip = (spill + 1) & 0xFFFF; out->carry = 1; out->ok = 1; return 1;  // 3822 LDX $C7 / INX / SEC
        }
        // 0xCF and 0xD2: CMP #00 / BNE: the jump is taken when the leaf returned non-zero (PC absent). The
        // displacement is read at the spill and added to it.
        if (a == 0) { out->ip = (spill + 1) & 0xFFFF; out->carry = 1; out->ok = 1; return 1; }   // 38FE/3900/SEC
        int disp = script[spill];                                // 3905 / 38AA
        r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;                  // 390E STA $D9, 16-bit after
        out->ip = (spill + disp) & 0xFFFF; out->carry = 1; out->ok = 1; return 1; //   AND #00FF; 3912 ADC $D9
      }
      case 0xD3: {
        // C0/3919: join. Leaf 1 puts the char id in the active party; then $018D + charid (that PC's object index)
        // goes into the first empty one of $0199 / $019B. Length 2; the visit ends (entry carry).
        int arg = script[(ip + 1) & 0xFFFF];
        int spill = (ip + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 3922
        r[0x01D9] = (uint8_t)arg; r[0x01DA] = 0x00;                   // 3925 STA $D9 / 3927 STZ $DA
        ctVmC2Gate(r, rom, romLen, 1, arg, spill, 0);                 // 3929 LDA #01 / 392B JSL
        r[0x0178] = 0x01;                                             // 392F LDA #01 / 3931 STA $78
        int t = r[(0x018D + arg) & 0x1FFFF];                          // 3933 LDX $D9 (16-bit) / 3937
        if (!(t & 0x80)) {                                            // 3939 BMI ->394B
          r[0x01DB] = (uint8_t)t;                                     // 393B STA $DB
          if (r[0x0199] & 0x80) r[0x0199] = (uint8_t)t;               // 393D LDA $99 / 393F BPL
          else r[0x019B] = (uint8_t)t;                                // 3947 LDA $DB / 3949 STA $9B
        }
        out->ip = spill; out->carry = 0; out->ok = 1; return 1;                   // 394B LDX $C7 / RTS
      }
      case 0xD4: {
        // C0/394E: leave. The only member that works before the gate: it scans the party object slots
        // $0197/$0199/$019B and compares each one's $1101,X (char id) with the operand.
        // The scan stops at the first empty slot, and 3998 BMI ->39B1 jumps into the compaction, so an empty
        // slot 1 still shifts $019B up and runs the palette shift without comparing slot 2.
        // The carry is the last CMP $D9 that ran (or the entry 0): the gate's PHP/PLP keeps it and 39DE LDX $C7 /
        // RTS follows. The found path therefore continues (CMP sets carry on equality).
        int arg = script[(ip + 1) & 0xFFFF];
        int spill = (ip + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 3955 STX $C7, 16-bit
        r[0x01D9] = (uint8_t)arg;                                     // 3957 STA $D9 -- 8-bit,
        int carry = 0, gx = spill, done = 0;                          //   $01DA is not touched
        int s0 = r[0x0197];                                           // 395B LDA $97
        if (!(s0 & 0x80)) {                                           // 395D BMI ->3994
          gx = s0;                                                    // 395F TAX
          carry = (r[0x1101 + s0] >= arg);                            // 3960 / 3963 CMP $D9
          if (r[0x1101 + s0] == arg) {                                // 3965 BNE ->3994
            r[0x1100 + s0] = 0x03;                                    // 3967 / 3969
            int s1 = r[0x0199];                                       // 396C LDA $99
            if (!(s1 & 0x80)) {                                       // 396E BMI ->3980
              gx = s1; r[0x1100 + s1] = 0x00;                         // 3970 TAX / 3971 / 3973
              int s2 = r[0x019B];                                     // 3976 LDA $9B
              if (!(s2 & 0x80)) { gx = s2; r[0x1100 + s2] = 0x01; }   // 3978 BMI / 397A TAX / 397D
            }
            r[0x0197] = r[0x0199];                                    // 3980 / 3982
            r[0x0199] = r[0x019B];                                    // 3984 / 3986
            r[0x019B] = 0x80;                                         // 3988 / 398A
            ctVm39E1(r); ctVm39F4(r); gx = 0x2200;                    // 398C / 398F -- MVN ends here
            done = 1;                                                 // 3992 BRA ->39D3
          }
        }
        if (!done) {
          int s1 = r[0x0199];                                         // 3996 LDA $99
          if (s1 & 0x80) {                                            // 3998 BMI ->39B1
            r[0x0199] = r[0x019B]; r[0x019B] = 0x80;                  // 39B1-39B7
            ctVm39F4(r); gx = 0x2200; done = 1;                       // 39B9 JSR C0/39F4
          } else {
            gx = s1;                                                  // 399A TAX
            carry = (r[0x1101 + s1] >= arg);                          // 399B / 399E CMP $D9
            if (r[0x1101 + s1] == arg) {                              // 39A0 BNE ->39BE
              r[0x1100 + s1] = 0x03;                                  // 39A2 / 39A4
              int s2 = r[0x019B];                                     // 39A7 LDA $9B
              if (!(s2 & 0x80)) { gx = s2; r[0x1100 + s2] = 0x01; }   // 39A9 BMI / 39AB TAX / 39AE
              r[0x0199] = r[0x019B]; r[0x019B] = 0x80;                // 39B1-39B7
              ctVm39F4(r); gx = 0x2200; done = 1;                     // 39B9 JSR C0/39F4
            }
          }
        }
        if (!done) {
          int s2 = r[0x019B];                                         // 39BE LDA $9B
          if (!(s2 & 0x80)) {                                         // 39C0 BMI ->39D3
            gx = s2;                                                  // 39C2 TAX
            carry = (r[0x1101 + s2] >= arg);                          // 39C3 / 39C6 CMP $D9
            if (r[0x1101 + s2] == arg) {                              // 39C8 BNE ->39D3
              r[0x1100 + s2] = 0x03;                                  // 39CA / 39CC
              r[0x019B] = (uint8_t)(r[0x019B] | 0x80);                // 39CF LDA #80 / 39D1 TSB $9B
            }
          }
        }
        ctVmC2Gate(r, rom, romLen, 2, arg, gx, 0);                    // 39D7 TAY / 39D8 LDA #02
        out->ip = spill; out->carry = carry; out->ok = 1; return 1;               // 39DE LDX $C7 / RTS
      }
      case 0xF8: case 0xF9: case 0xFA: {
        // C0/3E5C / 3E6E / 3E75: heal the party. One byte each (INY / STY $C7, nothing read from the script). 0xF9
        // runs leaf 6 (max HP), 0xFA leaf 7 (refill MP), 0xF8 both, so its epilogue runs twice. X at the JSL is
        // the dispatch's opcode*2 (these ops never load X).
        int spill = (ip + 1) & 0xFFFF, ey = spill;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 3E5D STY $C7, 16-bit
        if (op != 0xFA) ctVmC2Gate(r, rom, romLen, 6, spill, op * 2, &ey);   // 3E5F LDA #06 / 3E61
        // The second call sees neither of the first call's arguments: X is $30 (see the gate) and Y is what
        // C2/82E1's 8306 TAY left, so $04C9 goes $F0 -> $30 and $04C0 goes ip+1 -> a relic id.
        if (op != 0xF9) ctVmC2Gate(r, rom, romLen, 7, op == 0xF8 ? ey : spill,
                                   op == 0xF8 ? 0x30 : op * 2, 0);
        out->ip = spill; out->carry = 0; out->ok = 1; return 1;                   // 3E6B LDX $C7 / RTS -- yields
      }
    default: return 0;
  }
}

// The pointer read: ops 0x48/0x49, ROM C0/6792 / C0/67C7: a byte or a word through a script-supplied 24-bit pointer
// into a local. Shared with the live VM (ct_vm.c step() and init pass); for example `49 05 01 7E nn` in a
// location's obj0 init reads where the party came from ($0105). Carry set. Returns 1 if in the family.
int CtVmReadPtrFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op != 0x48 && op != 0x49) return 0;
  // C0/6792 / C0/67C7: read a byte (0x48) or word (0x49) through the pointer the shared prologue C0/67A2 stages at
  // $01CD-$01CF (op 0x4A writes through $01D0-$01D2). They differ only in where SEP #20 sits: 6795 before the load
  // (0x48, one byte), 67D0 after the store (0x49, two). Five bytes, SEC.
  // 67BD STX $D9 is 16-bit (REP #20 comes after, at 67BF): ip+4 spills to $01D9/$01DA, and the exit advances from
  // 679D LDX $D9 (ip+4, then INX).
  int lo = script[(ip + 1) & 0xFFFF];         // 67A4 / 67A8
  int mid = script[(ip + 2) & 0xFFFF];        // 67AB / 67AF
  int bk = script[(ip + 3) & 0xFFFF];         // 67B2 / 67B6
  int vi = script[(ip + 4) & 0xFFFF];         // 67B9 -- the destination local index
  r[0x01CD] = (uint8_t)lo; r[0x01CE] = (uint8_t)mid; r[0x01CF] = (uint8_t)bk;
  r[0x01D9] = (uint8_t)(ip + 4); r[0x01DA] = (uint8_t)((ip + 4) >> 8);   // 67BD, 16-bit
  int a = 0x10200 + ((vi * 2) & 0xFFFF);             // 67C4 ASL A -- the stride-two array
  int fa = ctFlatAddr24(bk, (mid << 8) | lo);        // 6797 / 67CA LDA [$CD]
  unsigned io = (unsigned)((mid << 8) | lo);
  if ((bk <= 0x3F || (bk >= 0x80 && bk <= 0xBF)) && io >= 0x2140 && io <= 0x2143) {   // the SPC's output ports (B bus)
    r[a] = g_ctApuOut[io & 3];
    if (op == 0x49) r[a + 1] = g_ctApuOut[(io + 1) & 3];
  } else if (fa >= 0) {
    r[a] = r[fa];
    if (op == 0x49) {                                // the 16-bit twin: the second byte comes
      int fb = ctFlatAddr24(bk, (((mid << 8) | lo) + 1) & 0xFFFF);   // from the same bank,
      r[a + 1] = (fb >= 0) ? r[fb] : 0;              //   the offset wrapping within it
    }
  }
  *outIp = ip + 5;
  return 1;
}

// The local inc / dec / set: ops 0x71/0x72/0x73/0x75/0x76/0x77, ROM C0/6C69 / 6C85 / 6CA1 / 6CBD / 6CD6 / 6CF0, on
// the scene-local array $7F0200 + var*2 (stride 2, as op 0x12). Shared with the live VM. One skeleton: INY / TYX /
// LDA $7F2001,X / REP #20 / AND #00FF / ASL A / TAX, the operation, INY / TYX / SEC / RTS: two bytes, SEC.
// SEP #20 sits before the access in 0x71 (6C76), 0x73 (6CAE), 0x75 (6CCA) and 0x77 (6CFD), but after it in 0x72
// (6C9B) and 0x76 (6CEA), so those two are 16-bit and also touch the byte above the variable.
int CtVmLocalIncFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op != 0x71 && op != 0x72 && op != 0x73 && op != 0x75 && op != 0x76 && op != 0x77) return 0;
  int v = script[ip + 1];
  int a = 0x10200 + ((v * 2) & 0xFFFF);
  switch (op) {
    case 0x71: r[a] = (uint8_t)(r[a] + 1); break;                     // 6C78-6C7D, 8-bit INC
    case 0x73: r[a] = (uint8_t)(r[a] - 1); break;                     // 6CB0-6CB5, 8-bit DEC
    case 0x75: r[a] = 0x01; break;                                    // 6CCC-6CCE, 8-bit
    case 0x77: r[a] = 0x00; break;                                    // 6CFF-6D01, 8-bit
    case 0x72: { int w = ((r[a] | (r[a + 1] << 8)) + 1) & 0xFFFF;     // 6C92-6C97, 16-bit
                 r[a] = (uint8_t)w; r[a + 1] = (uint8_t)(w >> 8); } break;
    case 0x76: r[a] = 0x01; r[a + 1] = 0x00; break;                   // 6CE3-6CE6, 16-bit
  }
  *outIp = ip + 2;
  return 1;
}

// The local arithmetic: ops 0x5B/0x5E/0x5F/0x60/0x61/0x63/0x64/0x67/0x69/0x6B/0x6F, ROM C0/6A1F..6C68. Shared with
// the live VM. `rom` is for the two mask tables. Carry set. Returns 1 if `script[ip]` is in the family.
int CtVmLocalArithFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  switch (op) {
    case 0x5B: case 0x5E: case 0x5F: case 0x60: case 0x61:
    case 0x63: case 0x64: case 0x67: case 0x69: case 0x6B: case 0x6F: break;
    default: return 0;
  }
  // Eleven read-modify-write handlers on $7F0200 + index*2, all SEC, all three bytes except 0x60 (four):
  //
  //   op  source                          op    width
  //   5B  immediate byte  @ip+1           +=     8
  //   5E  local[byte @ip+1]               +=    16
  //   5F  immediate byte  @ip+1           -=     8
  //   60  immediate word  @ip+1           -=    16   (four bytes; dst index at ip+3)
  //   61  local[byte @ip+1]               -=     8
  //   63  C0:$FF20[byte @ip+1]            |=     8
  //   64  C0:$FF28[byte @ip+1]            &=     8
  //   67  immediate byte  @ip+1           &=     8
  //   69  immediate byte  @ip+1           |=     8
  //   6B  immediate byte  @ip+1           ^=     8
  //   6F  immediate byte  @ip+1           >>=    8   (a loop: `LSR A / DEC $D9 / BNE`)
  //
  // 0x63 and 0x64 read the bit-mask tables of ops 0x65/0x66 without their AND #0F, so the full operand byte
  // indexes them and can run past the sixteen entries; 0x69 and 0x67 use the operand raw.
  // 0x6F's count is a do-while (6C5B LSR A / 6C5C DEC $D9 / 6C5E BNE): a count of 0 shifts 256 times.
  int o1 = script[(ip + 1) & 0xFFFF];
  int len = (op == 0x60) ? 4 : 3, wide = (op == 0x5E || op == 0x60);
  int src;
  if (op == 0x60) { src = o1 | (script[(ip + 2) & 0xFFFF] << 8);
                    r[0x01D9] = (uint8_t)src; r[0x01DA] = (uint8_t)(src >> 8); }  // 6AD1, 16-bit
  else if (op == 0x5E || op == 0x61) {
    int s = 0x10200 + ((o1 * 2) & 0xFFFF);
    src = r[s] | (r[s + 1] << 8);                                   // 6A81 / 6AFE, 16-bit
    r[0x01D9] = (uint8_t)src; r[0x01DA] = (uint8_t)(src >> 8);      // 6A85 / 6B02
  } else if (op == 0x63 || op == 0x64) {
    int mt = (op == 0x63) ? 0xFF20 : 0xFF28;                        // 6B29 / 6B54 -- no mask
    src = (mt + o1 < romLen) ? MR(mt + o1) : 0;
    r[0x01D9] = (uint8_t)src;                                       // 6B2C / 6B57, 8-bit
  } else { src = o1; r[0x01D9] = (uint8_t)src; }                    // 6A25 / 6AA9 / ..., 8-bit
  int di = (op == 0x60) ? script[(ip + 3) & 0xFFFF]
                        : script[(ip + 2) & 0xFFFF];
  int a = 0x10200 + ((di * 2) & 0xFFFF);
  int cur = wide ? (r[a] | (r[a + 1] << 8)) : r[a], res;
  switch (op) {
    case 0x5B: case 0x5E: res = cur + src; break;                   // 6A39 / 6A96 CLC / ADC
    case 0x5F: case 0x60: case 0x61: res = cur - src; break;        // SEC / SBC
    case 0x63: case 0x69: res = cur | src; break;                   // 6B41 / 6C11 ORA
    case 0x64: case 0x67: res = cur & src; break;                   // 6B6C / 6BEC AND
    case 0x6B: res = cur ^ src; break;                              // 6C36 EOR
    // 6C5B LSR A / 6C5C DEC $D9 / 6C5E BNE ->6C5B: a do-while (see above); the counter is $01D9 in memory, left at
    // 0 on every path.
    default: { res = cur;
               do { res = (res & 0xFF) >> 1;
                    r[0x01D9] = (uint8_t)(r[0x01D9] - 1); } while (r[0x01D9] != 0); } break;
  }
  r[a] = (uint8_t)res;
  if (wide) r[a + 1] = (uint8_t)(res >> 8);
  *outIp = ip + len;
  return 1;
}

// The facing family: ten ops sharing the tail C0/554B-5556 (LDX $6D / STA $1600,X / STZ $1601,X / TYX / INX / CLC /
// RTS). They differ in where the value comes from and which object X names when the tail runs:
//   0x0F / 0x17 / 0x1B / 0x1D   C0/5535 / 5539 / 553D / 5541   `LDA #imm / BRA ->554B`   facing 0/1/2/3, self, 1 byte
//   0xA6                        C0/5545   `INY / TYX / LDA $7F2001,X` -> 554B              the operand, self, 2 bytes
//   0xA7                        C0/5557   the operand indexes the local array $7F0200 (stride 2) -> 554B, self, 2 bytes
//   0x1E / 0x1F / 0x25 / 0x26   C0/556C / 5579 / 5586 / 5593 `INY / TYX / TDC / XBA / LDA $7F2001,X / TAX / LDA #imm /
//                               BRA ->554D`: facing 0/1/2/3 on the object the operand names, 2 bytes
// The tail zeroes the walk-anim timer ($1601,X, the countdown C0/C98A decrements), so the object re-enqueues on its
// next anim leg instead of when its old countdown runs out; with the drain's deadline (src/ct_vbeam.h) the queue
// order decides which objects are served. Shared with the live VM (applyEffect / init pass). `*outTgt` = the X index
// written. Carry always clear. Returns 1 if `script[ip]` is in it.
int CtVmFacingFamily(uint8_t* r, const uint8_t* script, int ip, int idx, int* outIp, int* outTgt) {
  int op = script[ip & 0xFFFF], tgt = idx, f, len;
  switch (op) {
    case 0x0F: case 0x17: case 0x1B: case 0x1D:                    // 5535 A9 00 80 14 | 5539 A9 01 80 10 | ...
      f = (op == 0x0F) ? 0 : (op == 0x17) ? 1 : (op == 0x1B) ? 2 : 3; len = 1; break;   // Y never moves: one byte
    case 0xA6:                                                     // 5545 INY / TYX / 5547 LDA $7F2001,X
      f = script[(ip + 1) & 0xFFFF]; len = 2; break;
    case 0xA7: {                                                   // 5557 INY / TYX / LDA $7F2001,X
      int v = script[(ip + 1) & 0xFFFF];                           // 555D REP #20 / AND #00FF / ASL A / TAX
      f = r[0x10200 + ((v * 2) & 0xFFFF)]; len = 2; break; }       // 5566 LDA $7F0200,X (8-bit again at 554B)
    case 0x1E: case 0x1F: case 0x25: case 0x26:                    // 5570 LDA $7F2001,X / TAX -- then 554D, not 554B:
      tgt = script[(ip + 1) & 0xFFFF];                             //   `LDX $6D` is skipped, X stays the operand
      f = (op == 0x1E) ? 0 : (op == 0x1F) ? 1 : (op == 0x25) ? 2 : 3; len = 2; break;
    default: return 0;
  }
  r[0x1600 + tgt] = (uint8_t)f;                                    // 554D STA $1600,X
  r[0x1601 + tgt] = 0x00;                                          // 5550 STZ $1601,X
  *outIp = (ip + len) & 0xFFFF; *outTgt = tgt;                     // 5553 TYX / INX / CLC / RTS
  return 1;
}

static int ctLoaderWalk(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int recBase, int stride);
static int ctLoaderTbl(const uint8_t* r, const uint8_t* rom, int romLen, int dp, int y, int wide);
// The screen-effect ops: 0xF1 (C0/3D07, the colour-math transition) and 0xFE (C0/3F26, the eight interpolation
// channels C0/21E1's entry 6 runs). Shared with the live VM (ct_vm.c step()). `script` + `ip` as CtVmPokeFamily.
// Returns 1 if `script[ip]` is one of them (the IP and the carry in *out).
int CtVmScreenFxOp(uint8_t* r, const uint8_t* script, int ip, CtHRet* out) {
  CtHRet h = { ip, 1, 0 };
  switch (script[ip]) {
    case 0xF1: {
      // C0/3D07: stage a colour-math transition. $0BDF is the shadow of $2131 (CGADSUB; read only at C0/A515), so
      // the operand splits as CGADSUB does: top three bits subtract/half/backdrop (#E0), low five the layer mask
      // (#1F). #17 = add and #97 = subtract on the same layers. It only stages: $0118 |= 0x18 requests the transition
      // and the main loop runs it; op 0xF3 waits on bit 3.
      // Five arms; the INX at 3D0F onto the second operand is skipped when the first is zero, so that arm takes two
      // bytes and the others three.
      int o1 = script[((ip + 1) & 0xFFFF)];                    // 3D09
      if (o1 == 0) {                                                // 3D0D BEQ ->3D6B
        r[0x0122] = 0xE0;                                           // 3D6D STA $22
        r[0x0121] = 0xE0;                                           // 3D6F STA $21
        r[0x0BE0] = r[0x0BDF];                                      // 3D71/3D74 restore the shadow
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 3D77 INX / SEC -- two bytes
      }
      r[0x0122] = (uint8_t)o1;                                      // 3D10 STA $22
      r[0x01D9] = (uint8_t)(o1 & 0xE0);                             // 3D14 STA $D9
      r[0x0121] = (uint8_t)((r[0x0121] & 0x1F) | (o1 & 0xE0));      // 3D1C -- mode from the operand
      int o2 = script[((ip + 2) & 0xFFFF)];                    // 3D1E
      if (o2 == 0) {                                                // 3D22 BEQ ->3D44
        r[0x0121] = r[0x0122];                                      // 3D46 -- full byte, not masked
        r[0x0BE0] = 0x17;                                           // 3D4A
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 3D4D INX / SEC
      }
      if (o2 & 0x80) {                                              // 3D24 BMI ->3D50
        int n = o2 & 0x7F;                                          // 3D50 AND #7F
        if (n == 0) {                                               // 3D52 BEQ ->3D5F
          r[0x0121] = r[0x0122];                                    // 3D61
          r[0x0BE0] = 0x97;                                         // 3D65
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 3D68 INX / SEC
        }
        r[0x0123] = (uint8_t)n; r[0x0124] = (uint8_t)n;             // 3D54 / 3D56
        r[0x0BE0] = 0x97;                                           // 3D5A -- subtract
      } else {
        r[0x0123] = (uint8_t)o2; r[0x0124] = (uint8_t)o2;           // 3D26 / 3D28
        r[0x0BE0] = 0x17;                                           // 3D2C -- add
      }
      // 3D2F (fallthrough, and the BRA at 3D5D) recomputes $01D9/$0121 from $0122 = o1, so both stores rewrite the
      // values already there; kept because the ROM issues them.
      r[0x01D9] = (uint8_t)(r[0x0122] & 0xE0);                      // 3D33 STA $D9
      r[0x0121] = (uint8_t)((r[0x0121] & 0x1F) | (r[0x0122] & 0xE0));   // 3D3B STA $21
      r[0x0118] = (uint8_t)(r[0x0118] | 0x18);                      // 3D3F TSB $18 -- request it
      h.ip = (ip + 3) & 0xFFFF; h.carry = 0; h.ok = 1; break;       // 3D41 INX / CLC -- yields
    }
    case 0xFE: {
      // C0/3F26: build eight interpolation channels. Eighteen bytes: a duration, then eight (start, target) pairs.
      // Each pair gets a per-frame delta in 8.8 fixed point through the hardware divider and a seeded running value;
      // $0139 = 6 then arms the effect dispatcher C0/21E1. Single exit, ip+18, CLC.
      int D = script[((ip + 1) & 0xFFFF)];           // 3F28
      r[0x01F4] = (uint8_t)D;                             // 3F2C STA $F4 -- 8-bit, $01F5 untouched
      r[0x11CE8] = (uint8_t)D;                            // 3F2E STA $7F1CE8, the live countdown
      int c7 = (ip + 2) & 0xFFFF;
      r[0x01C7] = (uint8_t)c7; r[0x01C8] = (uint8_t)(c7 >> 8);     // 3F33 STX $C7 (16-bit)
      r[0x01F0] = 0x00;                                   // 3F35 -- once, outside the loop, so the
                                                          // 16-bit word at $01F0 always reads a<<8
      r[0x01F6] = 0x00; r[0x01F7] = 0x00;                 // 3F37 / 3F39
      r[0x01EE] = 0x08;                                   // 3F3B -- the channel count is hardcoded
      for (int i = 0; i < 8; i++) {
        int a = script[(c7 & 0xFFFF)];               // 3F41
        r[0x01F1] = (uint8_t)a;                           // 3F45 STA $F1
        int b = script[((c7 + 1) & 0xFFFF)];         // 3F48
        c7 = (c7 + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)c7; r[0x01C8] = (uint8_t)(c7 >> 8);   // 3F4D STX $C7
        int f0 = r[0x01F0] | (r[0x01F1] << 8);            // 3F55 SBC $F0 reads the word = a<<8
        int diff = (((b << 8) & 0xFFFF) - f0) & 0xFFFF;   // 3F4F XBA / 3F50 LDA #00 / 3F54 SEC/SBC
        int q;
        if (diff == 0) {                                  // 3F57 BEQ ->3F91
          q = 0;                                          // the divider is not touched on this arm
        } else if (b > a) {                               // 3F59 BCS ->3F7B (carry = no borrow)
          q = ctSnesDiv16(diff, D);                       // 3F7B $4204 / 3F82 $4206 / 3F8E $4214
        } else {
          int mag = ((diff ^ 0xFFFF) + 1) & 0xFFFF;       // 3F5B EOR #FFFF / 3F5E INC A
          q = ((ctSnesDiv16(mag, D) ^ 0xFFFF) + 1) & 0xFFFF;       // 3F75 EOR #FFFF / 3F78 INC A
        }
        int xi = r[0x01F6] | (r[0x01F7] << 8);            // 3F91 LDX $F6 (16-bit)
        r[0x11CF9 + xi]     = (uint8_t)q;                 // 3F93 STA $7F1CF9,X -- 16-bit on all
        r[0x11CF9 + xi + 1] = (uint8_t)(q >> 8);          //      three arms
        int cur = r[0x01F0] | (r[0x01F1] << 8);           // 3F97 LDA $F0 (16-bit) = a<<8
        r[0x11CE9 + xi]     = (uint8_t)cur;               // 3F99 STA $7F1CE9,X (16-bit)
        r[0x11CE9 + xi + 1] = (uint8_t)(cur >> 8);
        xi = (xi + 2) & 0xFFFF;
        r[0x01F6] = (uint8_t)xi; r[0x01F7] = (uint8_t)(xi >> 8);   // 3F9F STX $F6 -- also $01F7
        r[0x01EE] = (uint8_t)(r[0x01EE] - 1);             // 3FA3 DEC $EE
      }
      r[0x0139] = 0x06;                                   // 3FA7/3FA9 -- arms C0/21E1's dispatcher
      h.ip = (ip + 18) & 0xFFFF; h.carry = 0; h.ok = 1; break;     // 3FAB LDX $C7 / CLC
    }
    default: return 0;
  }
  *out = h;
  return 1;
}

// Op 0x7A, C0/4D06: the jump toward a tile (the arc in $1B80/$1B81, integrated by C0/AAC7). Shared with the live VM
// (ct_vm.c step()). `script` + `ip` as CtVmPokeFamily; `idx` = the object's X. Returns 1 (the IP and carry in *out).
int CtVmOp7A(uint8_t* r, const uint8_t* rom, const uint8_t* script, int ip, int idx, CtHRet* out) {
  CtHRet h = { ip, 1, 0 };
  switch (script[ip]) {
    case 0x7A: {
      // Four bytes: target tile X, target tile Y, and a third operand that is both a fallback ticket and a scale.
      // Three arms on $1A80,X (the progress flag of ops 0x92/0x9C):
      //   armed, ticket running -> ip+0, CLC
      //   armed, ticket drained -> clear $1A80 and $1B80, ip+4, SEC
      //   not armed             -> arm (below) and park at ip+0, CLC
      // The arm aims with C0/ABA2, builds a velocity with C0/AC69, then divides the distance by the velocity for the
      // leg's ticks (|delta| << 8 into $4204, |velocity| into $4206, the quotient's low byte from $4214). It tries X
      // first (4D53 BEQ dx == 0, 4D67 BEQ velX == 0 -> 4DA1), then Y, then the literal (4DF4), so no axis is used as a
      // zero divisor.
      // All three arms adjust $1980 (the Y velocity): 4D97, 4DEA and 4E11 are the same ADC $1980,X.
      if (r[0x1A80 + idx] != 0) {                        // 4D08
        if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 4D10 -> 4D12
        r[0x1A80 + idx] = 0x00;                          // 4D15 STZ $1A80,X
        r[0x1B80 + idx] = 0x00;                          // 4D18 STZ $1B80,X
        h.ip = (ip + 4) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4D1B TYX/INX x4/SEC
      }
      r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);    // 4D22 STY $C7 (Y = the opcode IP)
      r[0x01F2] = script[((ip + 1) & 0xFFFF)];      // 4D26 / 4D2A -- target tile X
      r[0x01F3] = script[((ip + 2) & 0xFFFF)];      // 4D2D / 4D31 -- target tile Y
      int o3 = script[((ip + 3) & 0xFFFF)];
      r[0x01C1] = (uint8_t)o3;                           // 4D34 / 4D38 STA $C1
      r[0x01F0] = r[0x1801 + idx];                       // 4D3C / 4D3F -- this object's tile X
      r[0x01F1] = r[0x1881 + idx];                       // 4D41 / 4D44 -- ...and tile Y
      CtMoveDirAngle(r, rom);                            // 4D46 JSR C0/ABA2 -- writes $01EE itself
      ctMoveVelAC69(r, rom, idx);                        // 4D4B JSR C0/AC69
      int tick;
      int dx = (r[0x01F2] - r[0x01F0]) & 0xFF, vx = r[0x1900 + idx];   // 4D4E-4D51 / 4D64
      int dy = (r[0x01F3] - r[0x01F1]) & 0xFF, vy = r[0x1980 + idx];   // 4DA1-4DA4 / 4DB7
      if (dx != 0 && vx != 0) {
        int a = (dx & 0x80) ? ((-dx) & 0xFF) : dx;       // 4D55-4D59 -- magnitude, not a signed int
        int b = (vx & 0x80) ? ((-vx) & 0xFF) : vx;       // 4D69-4D6D
        tick = ((a << 8) / b) & 0xFF;                    // 4D5F $4204 / 4D6E $4206 / 4D79 $4214
      } else if (dy != 0 && vy != 0) {
        int a = (dy & 0x80) ? ((-dy) & 0xFF) : dy;       // 4DA8-4DAC
        int b = (vy & 0x80) ? ((-vy) & 0xFF) : vy;       // 4DBC-4DC0
        tick = ((a << 8) / b) & 0xFF;                    // 4DB2 / 4DC1 / 4DCC
      } else {
        tick = o3;                                       // 4DF4 LDA $C1 -- the literal fallback
      }
      r[0x1A01 + idx] = (uint8_t)tick;                   // 4D7C / 4DCF / 4DF6
      r[0x1B81 + idx] = (uint8_t)o3;                     // 4D87 / 4DDA / 4E01
      // The multiplier and the divider share $4216: after STA $4202 / STA $4203 it holds the 16-bit product. LSR A /
      // EOR #FFFF / INC A negates the halved product, and the 16-bit ADC folds it into $1980/$1981.
      int prod = (tick * o3) & 0xFFFF;                   // 4D7F/4D84 -> $4216
      int adj = (-(prod >> 1)) & 0xFFFF;                 // 4D91-4D95
      int v16 = ((r[0x1980 + idx] | (r[0x1981 + idx] << 8)) + adj) & 0xFFFF;   // 4D97 ADC $1980,X
      r[0x1980 + idx] = (uint8_t)v16; r[0x1981 + idx] = (uint8_t)(v16 >> 8);
      r[0x1A80 + idx] = 0x01;                            // 4E1B
      r[0x1B80 + idx] = 0x01;                            // 4E1E
      h.ip = ip; h.carry = 0; h.ok = 1; break;           // 4E21 LDX $C7 / CLC -- park on the arm
    }
    default: return 0;
  }
  *out = h;
  return 1;
}

// ---- Families shared with the live VM ----
// The families below are also run by the live event VM (ct_vm.c step() and its init pass runFunc), so both runners
// call the body --sweepops checks (disasm/notes/live_vm_op_gaps.md). One signature: `script` = the $7F:2001 image the
// IP indexes, `idx` = the object's X; returns 1 if `script[ip]` is in the family (IP + carry in *out).
//
// 0x0A/0x0B/0x0C (C0/6254 / 6269 / 6282) -- an object's kill and its cadence pause.
int CtVmObjEnableFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x0A: {
        // C0/6254: despawn. Writes another object's state (C0/A88D then deactivates it on the bit-7 reject).
        //   6254 INY / TYX / TDC / XBA        (zeroes C's high byte for the 16-bit TAX)
        //   6258 LDA $7F2001,X                operand 1 = the target object index
        //   625D LDA #80 / 625F STA $1100,X   state = $80 (bit 7 = inactive)
        //   6262 LDA #00 / 6264 STA $1A81,X   visibility = 0, the byte op 0x91 clears
        //   6267 BRA ->6247 = TYX / INX / SEC / RTS
        // Two bytes, ip+2, carry set. Both stores are plain assignments.
        if (getenv("PORTRUN_CLAIMS")) printf("  OP0A  self=%02X tgt=%02X\n", idx, script[ip + 1]);
        int tgt = script[ip + 1];       // 6258
        r[0x1100 + tgt] = 0x80;                           // 625F
        r[0x1A81 + tgt] = 0x00;                           // 6264
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x0B: {
        // C0/6269: disable an object: set bit 7 of $1000,X for the target the operand names.
        //   6269 INY / TYX / TDC / XBA / LDA $7F2001,X / TAX
        //   6272 LDA $1000,X / ORA #80 / STA $1000,X
        //   627A CPX $6D / 627C BNE ->6247    -> TYX / INX / SEC / RTS   (ip+2, continue)
        //   627E TYX / INX / CLC / RTS                                   (ip+2, yield)
        // Disabling another object continues; disabling self yields the visit.
        int tgt = script[ip + 1];        // 626D
        r[0x1000 + tgt] = (uint8_t)(r[0x1000 + tgt] | 0x80);            // 6272-6277
        h.ip = (ip + 2) & 0xFFFF; h.carry = (tgt != idx);  // 627A CPX $6D
        h.ok = 1; break;
      }
      case 0x0C: {
        // C0/6282: the inverse of 0x0B (clears bit 7), with no CPX $6D: it goes straight to 62B1 TYX / INX / SEC /
        // RTS, carry set.
        int tgt = script[ip + 1];        // 6286
        r[0x1000 + tgt] = (uint8_t)(r[0x1000 + tgt] & 0x7F);            // 628B-6290
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x89/0x8A (C0/4B49 / 4B58) -- the move speed $1A00,X, literal and from a local.
int CtVmSpeedFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x89:                                          // C0/4B49 -- set move speed
        // `4B51 STA $1A00,X`. It does not touch $1A01, the mover ticket sharing the slot.
        r[0x1A00 + idx] = script[ip + 1];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x8A: {
        // C0/4B58: op 0x89's indirect twin: the speed comes from the scene-local array at $7F0200 (stride 2).
        int v = script[ip + 1];           // 4B5A
        r[0x1A00 + idx] = r[0x10200 + ((v * 2) & 0xFFFF)];  // 4B63 ASL A / 4B67 / 4B6D
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 4B70 TYX / INX / SEC
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x19/0x1C/0x1A (C0/653B / 654E / 6568) -- the op-result word $7F0A80,X and the case on it.
int CtVmResultFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x19: case 0x1C: {
        // C0/653B and C0/654E: load this object's result word $7F0A80,X, joining at C0/655C LDX $6D / STA
        // $7F0A80,X / SEP #20 / TYX / INX / SEC.
        //   0x19: operand = a local index; 6548 LDA $7F0200,X after ASL A (stride 2), 16-bit (REP #20 from 6541), so
        //         it takes the whole word, unlike op 0x4F's 8-bit store into the same array.
        //   0x1C: a 16-bit index read at 6552 and used raw against the global page $7F0000; one more INY (6556), so
        //         three bytes.
        // The store at 655E is 16-bit either way ($7F0A80+idx and +idx+1).
        int val;
        if (op == 0x19) {
          int vi = script[ip + 1];       // 653D
          int a = 0x10200 + ((vi * 2) & 0xFFFF);           // 6546 ASL A
          val = r[a] | (r[a + 1] << 8);                    // 6548, 16-bit
          h.ip = (ip + 2) & 0xFFFF;                        // Y = ip+1 at 6564 TYX / 6565 INX
        } else {
          int gi = script[ip + 1] | (script[ip + 2] << 8);
          val = r[0x10000 + (gi & 0xFFFF)] | (r[0x10000 + ((gi + 1) & 0xFFFF)] << 8);   // 6558
          h.ip = (ip + 3) & 0xFFFF;                        // 6556's extra INY
        }
        r[0x10A80 + idx] = (uint8_t)val;                   // 655E STA $7F0A80,X (16-bit)
        r[0x10A81 + idx] = (uint8_t)(val >> 8);
        h.carry = 1; h.ok = 1; break;
      }
      case 0x1A: {
        // C0/6568: branch on the result word. Three bytes, SEC on both exits. 6570 CMP $7F2001,X / 6574 BNE ->657A
        // takes the displacement when the result does not match and falls through to ip+3 when it does. The compare is
        // 8-bit. The branch base is ip+2, the displacement's own address, as in ops 0x10/0x11.
        int got = r[0x10A80 + idx];                        // 656A
        if (got == script[ip + 1]) {     // 6570 -- equal falls through
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 6576 INX / INX / SEC
        }
        int d = script[ip + 2];          // 657B
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;          // 6584 STA $D9 -- 16-bit, so $01DA
        h.ip = (ip + 2 + d) & 0xFFFF;                      // 6586-658A TXA / CLC / ADC $D9 / TAX
        h.carry = 1; h.ok = 1; break;                      //   is zeroed
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x20/0x23/0x24 (C0/658F / 65F8 / 661F) -- the lead's char id and an object's facing into a local.
int CtVmObjQueryFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x20: {
        // C0/658F -- copy the party byte $7E2980 into a scene variable. The family's only reader of
        // $2980, and its only member with no per-object indexing at all.
        int v = script[ip + 1];             // 6591
        r[0x10200 + ((v * 2) & 0xFFFF)] = r[0x2980];          // 659A ASL (16-bit) / 659E / 65A2
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x23: case 0x24: {
        // C0/65F8 and C0/661F -- read an object's facing into a scene variable. Same split as
        // 0x21/0x22: 0x24 names the object by party slot and BRAs into 0x23's body at 6600.
        int o = (op == 0x23) ? script[ip + 1]
                             : ctVmPartySlotTarget(r, script[ip + 1]);   // 6628
        r[0x01D9] = r[0x1600 + o];                            // 6601 / 6604
        int vf = script[ip + 2];            // 6608
        r[0x10200 + ((vf * 2) & 0xFFFF)] = r[0x01D9];         // 6611 ASL (16-bit) / 6617
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x27/0x28 (C0/662C / 6655) -- branch on an object's activation latch / on its screen box.
int CtVmObjBranchFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x27: {
        // C0/662C -- skip if the object is inactive. The active arm writes nothing at all.
        int o = script[ip + 1];             // 6630
        if (r[0x0F00 + o] != 0) {                             // 6635 / 6638 BEQ
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 663A TYX / INX / INX
        }
        // The branch base is ip+2, the displacement's own offset (663F/6640 TYX / INX with Y = ip+1); op 0x2D has
        // no INX and bases at ip+1.
        int d = script[ip + 2];             // 6641
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;             // 664A STA $D9 -- 16-bit
        h.ip = (ip + 2 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x28: {
        // C0/6655: skip unless the object is inside a box around the camera. The camera halves are read 8-bit and
        // shifted right once, so the box is in half-tiles.
        int o = script[ip + 1];             // 6659
        r[0x01DB] = (uint8_t)(r[0x1D0A] >> 1);                // 665E / 6661 LSR (8-bit) / 6662
        r[0x01DD] = (uint8_t)(r[0x1D0E] >> 1);                // 6664 / 6667 / 6668
        int dx = (r[0x1801 + o] - r[0x01DB]) & 0xFF;          // 666A / 666D SEC / 666E SBC
        int dy = (r[0x1881 + o] - r[0x01DD]) & 0xFF;          // 667A / 667D SEC / 667E SBC
        // Six separate branches, all landing on 668F: dx==0, dx==1, dx>=$0E, dy==0, dy==1, dy>=$0D.
        // So the box is dx in [2,$0D] and dy in [2,$0C], one smaller on the Y axis.
        if (dx >= 0x02 && dx <= 0x0D && dy >= 0x02 && dy <= 0x0C) {
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 668A TYX / INX / INX
        }
        int d = script[ip + 2];             // 6691
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;             // 669A (16-bit)
        h.ip = (ip + 2 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x30/0x31/0x34-0x39/0x3B/0x3C/0x3F-0x44/0x47 (C0/66B2..6788) -- the pad branches and the drain deadline.
int CtVmPadBranchFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x30: case 0x31: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
      case 0x3B: case 0x3C: case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
      case 0x47: {
        // C0/66B2..6790: the button-branch family, seventeen slots and two shared tails, each testing one bit of one
        // byte:
        //   bit clear -> 66D0: read the displacement at ip+1 and jump to ip+1+disp (SEC)
        //   bit set   -> 6701 / 6784 (both INY / TYX / SEC / RTS): fall through to ip+2
        // Ops 0x30-0x39 use LDA $00F8 (absolute, DB = $00): $7E:00F2 / $7E:00F8, the live pad. Ops 0x3B-0x44 use
        // LDA $51 (direct page, DP = $0100): $7E:0150 / $0151, the latched edge, and only they consume their bit (TRB
        // on the taken path, hence AND instead of BIT).
        // 0x47 is not a branch: C0/6788 stores the operand to $016B and falls into the 6784 tail.
        if (op == 0x47) {
          r[0x016B] = script[ip + 1];               // 678A / 678E STA $6B
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 6790 BRA ->6784
        }
        int src, bit, latched = (op >= 0x3B);
        if (op == 0x30)      { src = 0x00F8; bit = 0x02; }            // 66B3 LDA $00F8 / BIT #02
        else if (op == 0x31) { src = 0x00F8; bit = 0x80; }            // 66BD
        else if (op == 0x34) { src = 0x00F2; bit = 0x80; }            // 66C7
        else if (op == 0x35) { src = 0x00F2; bit = 0x08; }            // 66E6
        else if (op == 0x36) { src = 0x00F2; bit = 0x40; }            // 66F0
        else if (op == 0x37) { src = 0x00F2; bit = 0x04; }            // 66FA
        else if (op == 0x38) { src = 0x00F2; bit = 0x20; }            // 6706
        else if (op == 0x39) { src = 0x00F2; bit = 0x10; }            // 6710
        else if (op == 0x3B) { src = 0x0150; bit = 0x02; }            // 671A LDA $50 / AND #02
        else if (op == 0x3C) { src = 0x0150; bit = 0x80; }            // 6725
        else if (op == 0x3F) { src = 0x0151; bit = 0x80; }            // 6733 LDA $51 / AND #80
        else if (op == 0x40) { src = 0x0151; bit = 0x08; }            // 6741
        else if (op == 0x41) { src = 0x0151; bit = 0x40; }            // 674F
        else if (op == 0x42) { src = 0x0151; bit = 0x04; }            // 675D
        else if (op == 0x43) { src = 0x0151; bit = 0x20; }            // 676B
        else                 { src = 0x0151; bit = 0x10; }            // 6779 (0x44)
        if (r[src] & bit) {
          if (latched) r[src] = (uint8_t)(r[src] & ~bit);             // 6720/672E/673C/... TRB
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 6701 INY / TYX / SEC
        }
        int disp = script[ip + 1];                  // 66D1 (X = ip+1 after TYX)
        r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;                  // 66DA STA $D9, 16-bit after
        h.ip = (ip + 1 + disp) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // AND #00FF; 66DE ADC $D9
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// ---- The caption plane's consumers (disasm/notes/field_caption_plane.md) ----
// The host's VRAM, if it keeps one: a DMA the ROM fires into VRAM (word address, source bytes, byte count). NULL in
// hosts without one (the sweeps, --vm); the WRAM side of every routine below is the same either way.
CtVramSinkFn g_ctVramSink = NULL;
static void vmVramDma(int word, const uint8_t* src, int bytes) { if (g_ctVramSink) g_ctVramSink(word, src, bytes); }

// C0/75A0 -- the caption buffer's fill: seven MVNs in a doubling ladder, 0x20 bytes from $D2:0000 into $7E:C800, then
// each block copied onto its own tail (0x20, 0x40, 0x80, 0x100, 0x200, 0x400) until $C800-$CFFF are covered. $D2:0000-001F
// is thirty-two zero bytes, so this is a 2048-byte zero fill, read from that source.
static void vmFill75A0(uint8_t* r, const uint8_t* rom, int romLen) {
  for (int q = 0; q < 0x800; q++)
    r[0xC800 + q] = (rom && 0x120000 + (q & 0x1F) < romLen) ? MR(0x120000 + (q & 0x1F)) : 0;   // a dat-only live run has no ROM
}

// C0/27EB -- a caption line's VRAM address. X (the byte that ended the line) + 1 is the next line's start; the column is
// $0BE3's low five bits, the row $0BE4's low six (the 16-bit `LDA $0BE4` reads $0BE5 too, and `AND #003F` drops it).
static void vm27EB(uint8_t* r, int x) {
  x = (x + 1) & 0xFFFF;                                              // 27EB INX
  r[0x0BE1] = (uint8_t)x; r[0x0BE2] = (uint8_t)(x >> 8);             // 27EC STX $0BE1
  int col = r[0x0BE3] & 0x1F;                                        // 27EF-27F4 (16-bit AND #001F)
  r[0x01D9] = (uint8_t)col; r[0x01DA] = 0x00;                        // 27F7 STA $D9 (D = $0100, 16-bit)
  int va = (((r[0x0BE4] & 0x3F) << 5) + col + 0x7000) & 0xFFFF;      // 27F9-280A ASL x5 / ADC $D9 / ADC #7000
  r[0x0BE5] = (uint8_t)va; r[0x0BE6] = (uint8_t)(va >> 8);           // 280B STA $0BE5
  int n = (0x40 - col - col) & 0xFFFF;                               // 280E-2815 #0040 - $D9 - $D9: the row's bytes
  r[0x0BE7] = (uint8_t)n; r[0x0BE8] = (uint8_t)(n >> 8);             // 2817 STA $0BE7   from that column on
  r[0x0BE4] = (uint8_t)(r[0x0BE4] + 1);                              // 281C INC $0BE4 (8-bit)
  r[0x0146] = (uint8_t)(r[0x0146] | 0x20);                           // 281F LDA #20 / TSB $46: the NMI's EC77
}

// C0/274D, the main loop's C0/00B4 leg: the caption requests ops 0x2A/0x2B/0x32 leave in $0154 bits 2/3/4 (a line
// first, one line a frame; the clears wait for a frame without one). With none of them, bit 5 (a chest's box) takes
// C0/2758, which closes the box ($0129 = $0D) once the chest's tile $5D/$5E is outside the camera.
// disasm/notes/field_caption_plane.md sec. 2 and 8 trace both. Returns 1 when it ran an arm.
int CtVmCaption274D(uint8_t* r, const uint8_t* rom, int romLen) {
  uint8_t a = r[0x0154];
  if (!(a & 0x1C)) {                                                 // 274F BIT #1C / BNE
    if (!(a & 0x20)) return 0;                                       // 2753 BIT #20 / BNE 2758 / RTS
    uint8_t tx = r[0x015D], ty = r[0x015E];
    if ((r[0x1D0A] >> 1) >= tx || (r[0x1D0C] >> 1) < tx ||           // 2758-2766
        (r[0x1D0E] >> 1) >= ty || (r[0x1D10] >> 1) < ty)             // 2768-2776
      r[0x0129] = 0x0D;                                              // 2779 LDA #0D / STA $29
    return 1;
  }
  if (!(a & 0x10)) {                                                 // 277E BIT #10 / BNE 278F
    r[0x0146] = (uint8_t)(r[0x0146] | ((a & 0x0C) << 1));            // 2782 AND #0C / ASL / TSB $46
    vmFill75A0(r, rom, romLen);                                      // 2787 JSR C0/75A0
    r[0x0154] = (uint8_t)(r[0x0154] & ~0x0C);                        // 278A LDA #0C / TRB $54
    return 1;
  }
  for (int k = 0; k < 0x40; k++)                                     // 2792-27A4: two MVNs of $20 from $D2:0000, the
    r[0xD800 + k] = (rom && 0x120000 + (k & 0x1F) < romLen) ? MR(0x120000 + (k & 0x1F)) : 0;   // second at Y = $D820
  int x = r[0x0BE1] | (r[0x0BE2] << 8), w = 0xD800;                  // 27AA-27B5 WMADD = $7E:D800, LDX $0BE1
  for (;;) {
    long at = 0x3DA000L + x;                                         // 27B8 LDA $FDA000,X (long,X: no bank wrap)
    uint8_t c = (rom && at < romLen) ? MR(at) : 0x80;               // (no ROM: no table -- the record ends here)
    if (c & 0x80) {                                                  // 27BC BMI 27E0
      r[0x0154] = (uint8_t)(r[0x0154] & ~0x10);                      // 27E0 LDA #10 / TRB $54
      vm27EB(r, x);                                                  // 27E4 JSR 27EB
      r[0x0BE4] = (uint8_t)(r[0x0BE4] + 1);                          // 27E7 INC $0BE4
      return 1;
    }
    if (c == 0x2F) { vm27EB(r, x); return 1; }                       // 27BE CMP #2F / BEQ 27EB (27EB's RTS is 274D's)
    uint8_t t = (c == 0x2E) ? 0x1B : (c == 0x20) ? 0x00 : (uint8_t)(c - 0x40);   // 27C2-27D3
    r[w & 0x1FFFF] = t; w++;                                         // 27D5 STA $2180
    r[w & 0x1FFFF] = 0x2C; w++;                                      // 27D8 LDA #2C / STA $2180
    x = (x + 1) & 0xFFFF;                                            // 27DD INX / BRA 27B8
  }
}

// The field NMI's caption arms, C0/EAD1-EB77, after the four `CMP #01..#04` style arms (ct_vm.c):
//   EB46 BIT #08 / BEQ / JSR C0/7F5D (VRAM $7000 <- $7E:C800, $0800 bytes: C0/7F62) / LDA #08 / TRB $46 / ... BRL EC15
//   EB5A BIT #10 / BEQ / JSR C0/7F58 (VRAM $7400 <- the same)                       / LDA #10 / TRB $46 / ... BRL EC15
//   EB6E BIT #20 / BEQ / JSR C0/EC77 (VRAM $0BE5 <- $7E:D800, $0BE7 bytes, VMAIN $80) / LDA #20 / TRB $46, into EB79
// One arm per NMI ($46 = 18 takes two NMIs). The 08/10 arms skip C0/EB79-EC13 (no CGRAM, OAM, text line or VRAM
// queue: ct_vm.c CtFieldNmiTilemapArm); 20 falls into it. Returns the arm (8, 0x10, 0x20) or 0.
int CtVmCaptionNmi(uint8_t* r) {
  uint8_t v = r[0x0146];
  if (!v || v <= 4) return 0;                                        // EAD3 BNE / EAD8-EB31 the style arms
  if (v & 0x08) { vmVramDma(0x7000, r + 0xC800, 0x800); r[0x0146] = (uint8_t)(v & ~0x08); return 0x08; }
  if (v & 0x10) { vmVramDma(0x7400, r + 0xC800, 0x800); r[0x0146] = (uint8_t)(v & ~0x10); return 0x10; }
  if (v & 0x20) {
    int va = r[0x0BE5] | (r[0x0BE6] << 8), n = r[0x0BE7] | (r[0x0BE8] << 8);   // EC77 LDX $0BE5 / EC97 LDX $0BE7
    vmVramDma(va, r + 0xD800, n ? n : 0x10000);                      // (DAS = 0 moves 64 KB; 27EB never leaves it 0)
    r[0x0146] = (uint8_t)(v & ~0x20); return 0x20;
  }
  return 0;
}

// 0x29/0x2A/0x2B/0x32/0x2C/0x2F (C0/3E84 / 3ED6 / 3EDE / 3EE2 / 3EE6 / 3F12) -- the staff roll's caption plane.
int CtVmCaptionFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x29: {
        // C0/3E84: arm the BG3 caption/credits plane, then point $0BE1 at record N of the ASCII table at $FD:A000.
        // Length 2, carry set, one exit. It calls C0/6E27 (decompress the caption font through JSL $C3:0002, i.e.
        // ct_lz_decompress, then queue its DMA through C0/2DC8, which writes no WRAM) and C0/75A0 (zero the staging
        // buffer). The $FD:A000 reads are LDA long,X, which does not wrap at the bank boundary, so no masking.
        // DB is $00 throughout: $1D35/$1D36/$0BE1/$0BE9 are the bank-$00 mirror of $7E.
        r[0x1D36] = (uint8_t)(r[0x1D36] | 0x04);         // 3E87 TSB -- test-and-set, not an assign
        r[0x1D35] = (uint8_t)(r[0x1D35] | 0x04);         // 3E8A TSB
        r[0x1D39] = 0x00;                                // 3E8D STZ, 8-bit: it does not touch $1D3A
        r[0x1D3A] = 0x00;                                // 3E90 STZ -- a separate instruction
        // 3E93 JSR C0/6E27: the caption font, $FD:0000 -> $7F:5080, then DMA to VRAM $5000.
        r[0x0300] = 0x00; r[0x0301] = 0x00;              // 6E2A STX $0300 (16-bit) -- source $FD:0000
        r[0x0303] = 0x80; r[0x0304] = 0x50;              // 6E30 STX $0303 (16-bit) -- dest $7F:5080
        r[0x0302] = 0xFD;                                // 6E35
        r[0x0305] = 0x7F;                                // 6E3A
        {
          static uint8_t scr29[0x10000];
          int sz29 = (int)ct_lz_decompress_bounded(rom, (size_t)romLen, scr29,
                                                   sizeof scr29, (size_t)0x3D0000);
          r[0x0306] = (uint8_t)sz29; r[0x0307] = (uint8_t)(sz29 >> 8);   // C3/0002's size report
          for (int q = 0; q < sz29 && 0x5080 + q < 0x10000; q++)
            r[0x15080 + q] = scr29[q];                   // $7F:5080 upward
        }
        // 6E27 ignores the size report: unlike the object loader (C0/466B) it does not advance $71, and the DMA
        // length is the literal $0400.
        r[0x0148] = 0x00; r[0x0149] = 0x50;              // 6E44 STX $48 = $5000 (VRAM word address)
        r[0x014B] = 0x80; r[0x014C] = 0x50;              // 6E49 STX $4B = $5080 (A-bus address)
        r[0x014D] = 0x7F;                                // 6E4D STA $4D = A-bus bank
        r[0x014A] = 0x01;                                // 6E51 STA $4A = DMAP7
        r[0x014E] = 0x00; r[0x014F] = 0x04;              // 6E56 STX $4E = $0400 bytes
        // 6E58 JSR C0/2DC8 -- PPU and DMA registers only, no WRAM write: the $0400 bytes go to VRAM $5000 at once (the
        // BG3 characters, tiles 0-63 -- where C0/6DCF put the location's BG3 tileset), to the host's VRAM if it keeps one.
        vmVramDma(0x5000, r + 0x15080, 0x400);
        // 3E96 JSR C0/75A0: clear the BG3 staging buffer $7E:C800-$7E:CFFF.
        vmFill75A0(r, rom, romLen);
        // 3E9C: the operand's bit 7 picks the mode, its low seven bits the record.
        int b29 = script[ip + 1];      // 3E9C LDA $7F2001,X
        r[0x0BE9] = (uint8_t)((b29 & 0x80) ? 0x02 : 0x01);   // 3EA6 / 3EB1 -- the only difference
        r[0x01D9] = (uint8_t)(b29 & 0x7F);               // 3EA2 / 3EAD: the clear arm stores the raw
        r[0x01C7] = (uint8_t)(ip + 1);                   //   byte, which already has bit 7 clear, so
        r[0x01C8] = (uint8_t)((ip + 1) >> 8);            //   both arms leave b & $7F. 3EB4 (16-bit)
        // 3EB6: walk records until the counter reaches $D9. A record ends at the first byte with bit 7 set, and the
        // INX at 3ECA steps past it, so X lands on the next record's start. Operand 0 exits before the first compare
        // with X = 0.
        int x29 = 0;                                     // 3EB6 LDX #0000
        r[0x01DD] = 0x00;                                // 3EB9 STZ $DD
        while ((r[0x01D9] & 0xFF) != (r[0x01DD] & 0xFF) && 0x3DA000 + x29 < romLen) {   // 3EBB/3EBD/3EBF CMP / BEQ (no ROM: no table)
          int by29 = (0x3DA000 + x29 < romLen) ? MR(0x3DA000 + x29) : 0;   // 3EC1
          x29 = (x29 + 1) & 0xFFFF;                                  // 3EC7 or 3ECA -- both INX
          if (by29 & 0x80) r[0x01DD] = (uint8_t)(r[0x01DD] + 1);     // 3ECB INC $DD, 8-bit
        }
        r[0x0BE1] = (uint8_t)x29; r[0x0BE2] = (uint8_t)(x29 >> 8);   // 3ECF STX $0BE1 (16-bit)
        // The loop exits only through a taken BEQ (CMP sets carry on equality) and nothing after it touches carry,
        // so carry is set on every path, operand 0 included.
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 3ED2 LDX $C7 / 3ED4 INX
      }
      case 0x2A: case 0x2B: case 0x32:
        // C0/3ED6 / 3EDE / 3EE2: set a bit in $0154. Three entries into one tail:
        //   3ED6 A9 04        (0x2A, falls into 3ED8)
        //   3EDE A9 08 80 F6  (0x2B)      3EE2 A9 10 80 F2  (0x32)
        //   3ED8 TSB $54 / TYX / INX / CLC / RTS
        // TSB ORs, so the bits are independent. Y never moves, so these are one-byte ops; the tail exits CLC (yield).
        // Op 0xDA tests bit 0 of the same latch and TRBs it on completion.
        r[0x0154] = (uint8_t)(r[0x0154] | (op == 0x2A ? 0x04 : op == 0x2B ? 0x08 : 0x10));   // 3ED8
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;        // 3EDA TYX / INX / CLC
      case 0x2C: {
        // C0/3EE6: clear three scroll/window bytes, then store two operands shifted left by three. The STZs run with
        // A 8-bit (one byte each); STA $1D8F / STA $1D91 are 16-bit (REP #20 at 3EF7). $1D92 is written twice: by the
        // STZ at 3EEC and as the high half of 3F0A's store, which wins.
        r[0x1D3F] = 0x00;                                   // 3EE6 STZ (8-bit)
        r[0x1D40] = 0x00;                                   // 3EE9
        r[0x1D92] = 0x00;                                   // 3EEC
        int v1 = (script[ip + 1] * 8) & 0xFFFF;    // 3EF3 / 3EF9 ASL x3
        r[0x1D8F] = (uint8_t)v1; r[0x1D90] = (uint8_t)(v1 >> 8);     // 3EFC STA $1D8F (16-bit)
        // 3F00's LDA is 16-bit (two script bytes) but 3F04 AND #00FF drops the upper one: the operand is one byte.
        int v2 = (script[ip + 2] * 8) & 0xFFFF;    // 3F00 / 3F07 ASL x3
        r[0x1D91] = (uint8_t)v2; r[0x1D92] = (uint8_t)(v2 >> 8);     // 3F0A STA $1D91 (16-bit)
        h.ip = (ip + 3) & 0xFFFF; h.carry = 0; h.ok = 1; break;      // 3F0F INX / CLC
      }
      case 0x2F:
        // C0/3F12: two operand bytes into $0BE3/$0BE4 (the caption column and row, see vm27EB). Branchless, ip+3,
        // SEC.
        r[0x0BE3] = script[ip + 1];       // 3F14 / 3F18
        r[0x0BE4] = script[ip + 2];       // 3F1C / 3F20
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 3F23 INX / SEC
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x33 (C0/3711) -- the script takes an OBJ palette slot and uploads its 24 bytes.
int CtVmPalOp33(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x33: {
        // C0/3711: assign this object's palette slot from the operand, then upload it. Straight-line (3711..3756),
        // one exit, SEC. It opens with the same LDA #80 / TSB $54 as ops 0x2A/0x2B/0x32 (a fourth bit of that latch).
        // The upload is op 0x88's restore tail (48DE-490B): dest $2102 + pal*16, the 16-bit ROM offset from $1400,X,
        // bank $E4, 24 bytes written twice (dest and dest + $200). Op 0x88 reads pal from $0F81,X; 0x33 computes and
        // writes it.
        // The operand is not the slot: 371E ASL A / 371F AND #0E gives pal = (operand & 7) * 2, so only the even
        // slots 0..14 are reachable.
        // 371C STX $C7 spills ip+2 with X 16-bit ($01C7 and $01C8) and 3753 reloads it, so the length is 2. X is
        // 16-bit on entry from the runner, so idx is a full index for $0F81,X and $1400,X.
        r[0x0154] = (uint8_t)(r[0x0154] | 0x80);                       // 3711 LDA #80 / TSB $54
        r[0x01C7] = (uint8_t)(ip + 2); r[0x01C8] = (uint8_t)((ip + 2) >> 8);   // 371C (16-bit)
        int pal33 = (script[ip + 1] << 1) & 0x0E;    // 371E ASL / 371F AND #0E
        r[0x0F81 + idx] = (uint8_t)pal33;                              // 3723 STA $0F81,X
        int dst33 = (0x2102 + pal33 * 16) & 0xFFFF;                    // 372B-3735 (*16, +2, +$2100)
        int src33 = r[0x1400 + idx] | (r[0x1401 + idx] << 8);          // 3738 LDA $1400,X (16-bit)
        for (int k = 0; k < 0x18; k++) {                               // 373D LDA #0017 -> 24 bytes
          int s = 0x240000 + ((src33 + k) & 0xFFFF);
          r[(dst33 + k) & 0xFFFF] = (s < romLen) ? MR(s) : 0;         // 3740 MVN $7E,$E4
        }
        for (int k = 0; k < 0x18; k++) {                               // 3743 PLX/PLA -- the same
          int s = 0x240000 + ((src33 + k) & 0xFFFF);                   //   source again, dest + $200
          r[(dst33 + 0x200 + k) & 0xFFFF] = (s < romLen) ? MR(s) : 0; // 374D MVN $7E,$E4
        }
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;        // 3753 LDX $C7 / 3755 SEC
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x7F (C0/6D09) -- a local from the $FE00 random table.
int CtVmRandOp7F(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x7F: {
        // C0/6D09: step the $01F8 cursor and read the $C0:FE00 table into a scene variable. The increment comes
        // first and is stored back, so two calls read two entries (the cursor and table of C0/ACFD's bounce).
        // DB = $00, and $00:$FE00 mirrors $C0:$FE00 = file offset $FE00.
        int t = (r[0x01F8] + 1) & 0xFF;                     // 6D0B LDA $F8 / 6D0D INC A (8-bit)
        r[0x01F8] = (uint8_t)t;                             // 6D0E STA $F8
        int b = CtRomByte(rom, romLen, 0xFE00 + t);                 // 6D11 LDA $FE00,X
        r[0x01D9] = (uint8_t)b;                             // 6D14 STA $D9
        int v = script[ip + 1];           // 6D18
        r[0x10200 + ((v * 2) & 0xFFFF)] = (uint8_t)b;       // 6D21 ASL A / 6D27 STA $7F0200,X (8-bit)
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 6D2B INY / TYX / SEC
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0xE6/0xF4 (C0/3DF4 / 3D84) -- the layer drift and the auto-scroll mode.
int CtVmScrollFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0xE6: {
        // C0/3DF4: write one 16-bit value into up to three scroll registers selected by a mask. Five bytes: the value
        // at ip+1, the mask at ip+3, and a byte at ip+4 for $1D4D. Mask bits 0/1/2 gate $1D24/$1D26/$1D28, each a
        // 16-bit LDY $DB / STY with the same value.
        int v = script[ip + 1] | (script[ip + 2] << 8);
        int mask = script[ip + 3];
        r[0x01DB] = (uint8_t)v; r[0x01DC] = (uint8_t)(v >> 8);          // 3DFC STA $DB (16-bit)
        r[0x01D9] = (uint8_t)mask;                                      // 3E06 STA $D9 (8-bit)
        r[0x1D4D] = script[ip + 4];                   // 3E0D STA $1D4D
        if (mask & 0x01) { r[0x1D24] = (uint8_t)v; r[0x1D25] = (uint8_t)(v >> 8); }   // 3E18 STY
        if (mask & 0x02) { r[0x1D26] = (uint8_t)v; r[0x1D27] = (uint8_t)(v >> 8); }   // 3E21
        if (mask & 0x04) { r[0x1D28] = (uint8_t)v; r[0x1D29] = (uint8_t)(v >> 8); }   // 3E2A
        h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xF4: {
        // C0/3D84: set $01BC. STA $BC / BNE ->3D94 skips the next two stores for a non-zero operand, so $1D4E/$1D4F
        // are cleared (with A = 0) only by an operand of 0. Two bytes, ip+2, SEC.
        int v = script[ip + 1];          // 3D86
        r[0x01BC] = (uint8_t)v;                            // 3D8A STA $BC
        if (v == 0) { r[0x1D4E] = 0x00; r[0x1D4F] = 0x00; }             // 3D8E / 3D91
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// See move.h. The order is the order of --sweepops' per-opcode report lines.
const CtVmOpInfo g_ctVmOps[CT_VM_NOPS] = {
  { 0x00, 0x5F74, "return"    }, { 0x4A, 0x67D7, "poke24"   }, { 0x5A, 0x6A12, "storyline" },
  { 0x87, 0x4876, "period"    }, { 0x89, 0x4B49, "speed"    }, { 0xAA, 0x2E67, "animeset"  },
  { 0xAB, 0x2E9A, "animeonce" }, { 0xAD, 0x2F96, "wait"     }, { 0xB3, 0x2E87, "animeset0" },
  { 0xE3, 0x3C05, "ctllock"   },
  { 0x8B, 0x4B74, "placetile" }, { 0x92, 0x4E73, "movehdg"  }, { 0xB4, 0x2E8B, "animeset1" },
  { 0xEE, 0x3CC5, "waitsound" }, { 0x0A, 0x6254, "despawn"  }, { 0x91, 0x4CE0, "hide"      },
  { 0x96, 0x4F61, "movetile"  }, { 0x8E, 0x4C74, "setprio"  },
  { 0x10, 0x62B5, "jmpfwd"   }, { 0x11, 0x62CB, "jmpback"  },
  { 0x0D, 0x6295, "collgate" }, { 0x0E, 0x62A4, "snapopt"  }, { 0xAC, 0x2F71, "animasm"  },
  { 0x90, 0x4CD5, "show"     }, { 0x7E, 0x4CE6, "hide80"   },
  { 0x12, 0x62DE, "brlocal"  }, { 0x16, 0x63E6, "brglobal" }, { 0xBB, 0x3570, "textreq"  },
  { 0xC1, 0x35B5, "textreq1" }, { 0xC2, 0x35FC, "textreq2" },
  { 0x82, 0x4590, "loadnpc"  }, { 0x04, 0x6109, "callwait" },
  { 0x8D, 0x4C27, "placepix" }, { 0xAE, 0x2E8F, "animeoff" },
  { 0x80, 0x421E, "loadpc"   },
  { 0x0F, 0x5535, "face0"    }, { 0x17, 0x5539, "face1"    }, { 0x1B, 0x553D, "face2"   },
  { 0x1D, 0x5541, "face3"    }, { 0xA6, 0x5545, "faceop"   },
  { 0x71, 0x6C69, "locinc"   }, { 0x72, 0x6C85, "locinc16" }, { 0x73, 0x6CA1, "locdec"  },
  { 0x75, 0x6CBD, "locset1"  }, { 0x76, 0x6CD6, "locset1w" }, { 0x77, 0x6CF0, "locset0" },
  { 0x57, 0x41E4, "loadpc0"  }, { 0x5C, 0x41EC, "loadpc1"  }, { 0x62, 0x41F4, "loadpc2" },
  { 0x6A, 0x4204, "loadpc3"  }, { 0x68, 0x41FC, "loadpc4"  }, { 0x6C, 0x420C, "loadpc5" },
  { 0x6D, 0x4214, "loadpc6"  },
  { 0x08, 0x6240, "busyset"  }, { 0x09, 0x624B, "busyclr"  },
  { 0x0B, 0x6269, "objoff"   }, { 0x0C, 0x6282, "objon"    },
  { 0x02, 0x5FB6, "call2"    }, { 0x03, 0x607E, "call3"    }, { 0x05, 0x61E9, "pcall2" },
  { 0x06, 0x61FE, "pcall3"   }, { 0x07, 0x6214, "pcallwait" },
  { 0xF3, 0x3D7A, "waitbit"  }, { 0xF4, 0x3D84, "setBC"    }, { 0xF1, 0x3D07, "colormath" },
  { 0xD9, 0x326C, "scatter3" }, { 0xDA, 0x345A, "scatter2" }, { 0x2E, 0x4A4E, "slot/blob" },
  { 0x4B, 0x67FB, "poke24w"  }, { 0x4C, 0x680C, "pokevar"  }, { 0x4D, 0x6829, "pokevarw" },
  { 0xE0, 0x3AB7, "chglocfade" }, { 0xE2, 0x3B9E, "chglocvars" },
  { 0xB9, 0x2FC6, "wait4"     }, { 0x2A, 0x3ED6, "setbit54a" }, { 0x2B, 0x3EDE, "setbit54b" },
  { 0x32, 0x3EE2, "setbit54c" }, { 0x33, 0x3711, "palupload" },
  { 0x29, 0x3E84, "captionrec" }, { 0x83, 0x46DF, "loadenemy" },
  { 0xC9, 0x3780, "hasitem"   }, { 0xCA, 0x37B1, "additem"   },
  { 0xC7, 0x37C7, "additemvar"}, { 0xCB, 0x37E3, "delitem"   },
  { 0xCC, 0x3827, "hasgold"   }, { 0xCD, 0x385B, "addgold"   },
  { 0xCE, 0x3874, "subgold"   },
  { 0xD2, 0x38E8, "inparty"   }, { 0xCF, 0x388D, "inroster"  },
  { 0xD0, 0x38BE, "addreserve"}, { 0xD1, 0x38D3, "dropparty" },
  { 0xD3, 0x3919, "joinparty" }, { 0xD4, 0x394E, "leaveparty"},
  { 0xD7, 0x37F9, "itemcount" }, { 0xC8, 0x3757, "reqack"    },
  { 0xDD, 0x3B1B, "chgloc"    }, { 0xDE, 0x3B45, "chgloc1"   },
  { 0x30, 0x66B2, "brF8b1"    }, { 0x31, 0x66BC, "brF8b7"    },
  { 0x34, 0x66C6, "brF2b7"    }, { 0x35, 0x66E5, "brF2b3"    },
  { 0x36, 0x66EF, "brF2b6"    }, { 0x37, 0x66F9, "brF2b2"    },
  { 0x38, 0x6705, "brF2b5"    }, { 0x39, 0x670F, "brF2b4"    },
  { 0x3B, 0x6719, "br50b1"    }, { 0x3C, 0x6724, "br50b7"    },
  { 0x3F, 0x6732, "br51b7"    }, { 0x40, 0x6740, "br51b3"    },
  { 0x41, 0x674E, "br51b6"    }, { 0x42, 0x675C, "br51b2"    },
  { 0x43, 0x676A, "br51b5"    }, { 0x44, 0x6778, "br51b4"    },
  { 0x47, 0x6788, "set016B"   }, { 0x4E, 0x6846, "blobcopy"  },
  { 0xF8, 0x3E5C, "healall"   }, { 0xF9, 0x3E6E, "healhp"    },
  { 0xFA, 0x3E75, "healmp"    },
  { 0xE8, 0x3C11, "sfxpost"   }, { 0xEA, 0x3C2F, "songpost"  },
  { 0xEB, 0x3C4C, "sndtriple" }, { 0xEC, 0x3C98, "sndraw"    },
  { 0x1E, 0x556C, "facetgt0"  }, { 0x1F, 0x5579, "facetgt1"  },
  { 0x25, 0x5586, "facetgt2"  }, { 0x26, 0x5593, "facetgt3"  }, { 0xA7, 0x5557, "faceind"   },
  { 0x2F, 0x3F12, "set0BE3"   }, { 0x8A, 0x4B58, "speedind"  }, { 0xFE, 0x3F26, "interp8"   },
  { 0x81, 0x4476, "loadnpc81" }, { 0x2C, 0x3EE6, "scrollset2" }, { 0x7F, 0x6D09, "fe00step" },
  { 0x8C, 0x4BC3, "placeind" }, { 0x13, 0x6313, "brloc16i" }, { 0x14, 0x6361, "brloc8loc8" },
  { 0x15, 0x63A4, "brloc16loc16" }, { 0xC0, 0x3643, "choice0" }, { 0xC3, 0x36AA, "choice1" },
  { 0xC4, 0x36DD, "choice2" },
  { 0x20, 0x658F, "rdparty"  }, { 0x21, 0x65AA, "rdtile"   }, { 0x22, 0x65EB, "rdtileslot" },
  { 0x23, 0x65F8, "rdface"   }, { 0x24, 0x661F, "rdfaceslot" }, { 0x27, 0x662C, "skipinact" },
  { 0x28, 0x6655, "skipfarcam" }, { 0x2D, 0x66A5, "skipnoinput" },
  { 0x97, 0x5084, "movetileind" }, { 0xA1, 0x5191, "movetilesil" }, { 0x9D, 0x4F0A, "movehdgind" },
  { 0x9E, 0x52C0, "movetoobj" }, { 0x9F, 0x54F5, "movetoslot" }, { 0x98, 0x534A, "movetoobjd" },
  { 0x99, 0x550E, "movetoslotd" }, { 0x8F, 0x5429, "movetopc" },
  { 0x94, 0x5215, "movetoactor" }, { 0x95, 0x5410, "movetoactorS" },
  { 0xB5, 0x3546, "movetoactorP" }, { 0xB6, 0x354F, "movetoactorPS" },
  { 0xE4, 0x3D97, "tilecopy" }, { 0xFF, 0x3FAF, "screenmode" },
  { 0xE5, 0x3DC5, "copyreq"  }, { 0xE6, 0x3DF4, "scrollset" }, { 0xE7, 0x3E30, "scrollwait" },
  { 0x84, 0x4867, "setsolid" }, { 0x7C, 0x4CEC, "showobj"  }, { 0x7D, 0x4CF9, "hideobj" },
  { 0x4F, 0x68A5, "locsetimm" }, { 0x19, 0x653B, "resfromloc" }, { 0x1C, 0x654E, "resfromglb" },
  { 0x1A, 0x6568, "brresult" }, { 0x65, 0x6B76, "glbset"   }, { 0x66, 0x6BA4, "glbclr" },
  { 0x18, 0x6442, "brstory"  }, { 0xED, 0x3CBC, "waitapu1" },
  { 0xF0, 0x3CD0, "fadeset"  }, { 0xF2, 0x3CFD, "fadewait" },
  { 0x9C, 0x4EC7, "slide"    }, { 0xA8, 0x55A0, "faceobj"  }, { 0xA9, 0x5605, "facepc" },
  { 0x50, 0x68C6, "loc=imm16" }, { 0x51, 0x68E9, "loc=loc8" }, { 0x52, 0x6913, "loc=loc16" },
  { 0x53, 0x693D, "loc=glb8"  }, { 0x54, 0x6964, "loc=glb16"}, { 0x55, 0x698B, "loc=story" },
  { 0x56, 0x69A6, "glb=imm8"  }, { 0x58, 0x69C4, "glb=loc8" }, { 0x59, 0x69EB, "glb=loc16" },
  { 0x5B, 0x6A1F, "loc+=i8"  }, { 0x5E, 0x6A74, "loc+=l16" }, { 0x5F, 0x6AA3, "loc-=i8"  },
  { 0x60, 0x6AC9, "loc-=i16" }, { 0x61, 0x6AF1, "loc-=l8"  }, { 0x63, 0x6B20, "locbitset" },
  { 0x64, 0x6B4B, "locbitclr"}, { 0x67, 0x6BD2, "loc&=i8"  }, { 0x69, 0x6BF7, "loc|=i8"  },
  { 0x6B, 0x6C1C, "loc^=i8"  }, { 0x6F, 0x6C41, "loc>>=i8" },
  { 0xB8, 0x3557, "textstage" }, { 0x48, 0x6792, "peek8"   }, { 0x49, 0x67C7, "peek16" },
  { 0x88, 0x4892, "colorfx"  }, { 0xD8, 0x3A83, "rendezvous" },
  { 0xB7, 0x2F01, "animerep" }, { 0xBA, 0x2FCA, "wait08"   },
  { 0xBC, 0x2FD2, "wait10"   }, { 0xBD, 0x2FCE, "wait20"   },
  { 0x7A, 0x4D06, "movetick" },
};

int CtFieldRunnerOne(uint8_t* r, const uint8_t* rom, int romLen, int idx, int ip, CtHRet* out) {
  // 5A9C STX $6D with DP = $0100 writes $016D. X is still 8-bit here (5A9E's REP #10 comes after), so $016E is
  // left alone.
  r[0x016D] = (uint8_t)idx;
    int op = r[0x12001 + ip];                             // fetch opcode from $7F2001 + IP
    CtHRet h;
    switch (op) {
      case 0x00:                                          // C0/5F74 -- return (op 0x02's partner)
        h.ip = ctVmOp00(r, idx, ip, &h.carry); h.ok = 1; break;
      case 0x10: {
        // C0/62B5: jump forward. 62C2 TXA takes X = ip+1 (the operand's own address) and 62C4 ADC $D9 adds the
        // operand, so the target is ip + 1 + operand.
        //   62B5 C8 INY / 62B6 BB TYX / 62B7 BF 01 20 7F LDA $7F2001,X
        //   62BB C2 20 REP #20 / 62BD 29 FF 00 AND #00FF / 62C0 85 D9 STA $D9
        //   62C2 8A TXA / 62C3 18 CLC / 62C4 65 D9 ADC $D9
        //   62C6 AA TAX / 62C7 E2 20 SEP #20 / 62C9 38 SEC / 62CA 60 RTS
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];
        // 62C0 STA $D9 is 16-bit (62BB REP #20): $01D9 and $01DA, the high byte already cleared by 62BD AND #00FF.
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;
        h.ip = (ip + 1 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x11: {
        // C0/62CB: jump backward: op 0x10 with SBC for ADC; 62DC BRA ->62C6 reuses op 0x10's tail.
        //   62D8 8A TXA / 62D9 38 SEC / 62DA E5 D9 SBC $D9   -> target = ip + 1 - operand
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;    // 62D6, 16-bit as above
        h.ip = (ip + 1 - d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x82: {
        // C0/4590: load an NPC. The handler is LDA #04 / JSR C0/4557, and the rest is the object loader CtObjLoad (the
        // $E4:F023 descriptor walk, the C0/5C90 graphics dedup, the JSL C3/0002 decompression, the $71 heap cursor;
        // `ct.exe --sweeploader` checks it). #04 is the $1100,X state C0/4557 stamps at 4559 (CtObjLoad's `state`).
        // Two bytes: 459A STY $C7 spills Y after C0/4557's two INYs (455C, 458D) and both exits restore it (4667,
        // 46DB LDX $C7), so the result is ip+2, which is also what CtObjLoad returns. Carry is always set (4669 and
        // 46DD SEC / RTS); the only other paths are the diagnostic halts (LDX #imm / BRL C0/2E1E).
        // Op 0x83 is not this routine with state = 5: C0/46DF is a separate handler (stride $0A, descriptors at
        // $E4:F600, three bytes, an inline OBJ-slot allocation, a CMP #F8 early-out at 477A, JSR C0/E12A).
        h.ip = CtObjLoad(r, rom, romLen, idx, ip, 4) & 0xFFFF;
        h.carry = 1; h.ok = 1; break;
      }
      case 0x4A: case 0x4B: case 0x4C: case 0x4D: {
        // C0/67D7 / 67FB / 680C / 6829, the poke family; the body (CtVmPokeFamily) is shared with the live VM. SEC
        // on every exit.
        int ni = ip;
        CtVmPokeFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x5A:                                          // C0/6A12 -- set storyline
        // 6A18 STA $7F0000, an absolute long store: the operand lands as is, unmasked.
        r[0x10000] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x87: {
        // C0/4876: set the scheduler period. Branchless, ip+2, SEC. 487E INC A is 8-bit, so operand $FF wraps to $00;
        // $01D9 gets the value before the bit-7 merge; $1000 (the period) and $1001 (the live countdown) both take
        // (old & 0x80) | v, keeping the halted flag in bit 7 and restarting the countdown at once.
        int v = (r[0x12001 + ((ip + 1) & 0xFFFF)] + 1) & 0xFF;
        r[0x01D9] = (uint8_t)v;                           // 487F, pre-merge
        int old = r[0x1000 + idx];                        // 4881
        r[0x1000 + idx] = (uint8_t)((old & 0x80) | v);    // 4888
        r[0x1001 + idx] = (uint8_t)((old & 0x80) | v);    // 488B
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x89: case 0x8A:
        CtVmSpeedFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x8B: {
        // C0/4B74: place at an absolute tile. Three bytes, always SEC. The sub-position literals are the engine's
        // placement convention: 4B81 LDA #80 / STA $1800,X (X = tile centre) and 4B93 LDA #FF / STA $1880,X (Y =
        // tile foot).
        int tx = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4B74 INY / 4B76 LDA $7F2001,X
        r[0x1801 + idx] = (uint8_t)tx;                    // 4B7C
        r[0x01D9] = (uint8_t)tx;                          // 4B7F STA $D9  -- DP = $0100
        r[0x1800 + idx] = 0x80;                           // 4B83
        int ty = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 4B86 INY / 4B88 LDA $7F2001,X
        r[0x1881 + idx] = (uint8_t)ty;                    // 4B8E
        r[0x01DA] = (uint8_t)ty;                          // 4B91 STA $DA
        r[0x1880 + idx] = 0xFF;                           // 4B95
        // 4B98 LDA $0F80,X / 4B9B BPL ->4BBF: bit 7 gates the priority block; clear = $0C00/$0C01 are left alone.
        if (r[0x0F80 + idx] & 0x80) {
          // 4B9D LDX $D9 is 16-bit (the runner's REP #10 at C0/5A9E stays in force), so it loads tileY*256 + tileX.
          ctVmTileAttr(r, idx, (tx | (ty << 8)) & 0xFFFF);   // 4B9F-4BBD, shared with op 0x8D
        }
        // 4BBF `TYX` with Y = ip+2 (both INYs ran) / 4BC0 INX / 4BC1 SEC.
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x8C: {
        // C0/4BC3: op 0x8B's indirect twin. Same three bytes, one exit, always SEC, and its own copy of the
        // tile-attribute block (4C03-4C22, the same as op 0x8B's and op 0x8D's), hence ctVmTileAttr.
        // Both operands index the scene-local array at $7F0200 (stride 2, as op 0x8A). The ASL A at 4BCE is 16-bit
        // (REP #20 at 4BC9), so an index of $80 reaches offset $100; only the low byte of each slot is used.
        // Note: it does not write $1800,X. Op 0x8B has LDA #80 / STA $1800,X there; op 0x8C goes straight to the INY
        // at 4BDD. $1880,X still gets $FF.
        int vxi = r[0x12001 + ((ip + 1) & 0xFFFF)];       // 4BC3 INY / 4BC5
        int tx = r[0x10200 + ((vxi * 2) & 0xFFFF)];       // 4BCE ASL (16-bit) / 4BD2
        r[0x1801 + idx] = (uint8_t)tx;                    // 4BD8
        r[0x01D9] = (uint8_t)tx;                          // 4BDB STA $D9
        int vyi = r[0x12001 + ((ip + 2) & 0xFFFF)];       // 4BDD INY / 4BDF
        int ty = r[0x10200 + ((vyi * 2) & 0xFFFF)];       // 4BE8 ASL (16-bit) / 4BEC
        r[0x1881 + idx] = (uint8_t)ty;                    // 4BF2
        r[0x01DA] = (uint8_t)ty;                          // 4BF5 STA $DA
        r[0x1880 + idx] = 0xFF;                           // 4BF7/4BF9 -- Y sub-position only
        if (r[0x0F80 + idx] & 0x80) {                     // 4BFC / 4BFF BPL ->4C23
          ctVmTileAttr(r, idx, (tx | (ty << 8)) & 0xFFFF);   // 4C01 LDX $D9 (16-bit) / 4C03-4C22
        }
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4C23 TYX / INX / SEC
      }
      case 0x8E: {
        // C0/4C74: set the priority / OAM-group byte $0F80,X. Two bytes; both arms end TYX / INX / SEC (4CB3, 4CD1):
        // ip+2, SEC. This writes the bit 7 that op 0x8B and C0/ACFD gate their tile-property blocks on. The operand is
        // stored as is, before the test.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4C74 INY / 4C75 TYX / 4C76 LDA $7F2001,X
        r[0x0F80 + idx] = (uint8_t)v;                    // 4C7C STA $0F80,X
        if (v & 0x80) {                                  // 4C7F `89 80` BIT #80 (immediate) / 4C81 BEQ ->4CB7
          // 4C83-4C8A builds the cell in the accumulator: LDA $1881,X (tile Y), XBA, then an 8-bit LDA $1801,X fills
          // the low byte and TAX moves all 16 bits: tileY*256 + tileX, unmasked.
          int cell = (r[0x1801 + idx] | (r[0x1881 + idx] << 8)) & 0xFFFF;
          // Both reads are long,X, which crosses the bank boundary; r[] is one flat image.
          int a1 = ((r[0x7000 + cell] & 0x40) >> 2) | 0x20;   // 4C8B/4C8F/4C91/4C92/4C93
          r[0x01D9] = (uint8_t)a1;                            // 4C95 STA $D9 -- DP = $0100
          int a2 = ((r[0x7040 + cell] & 0x40) >> 2) | 0x20;   // 4C97..4C9F
          // Crossed, as in op 0x8B: the $7E7040 plane feeds $0C00 and $7E7000 feeds $0C01 (C0/ACFD assigns them the
          // other way).
          r[0x0C00 + idx] = (uint8_t)a2;                      // 4CA1 LDX $6D / 4CA3 STA $0C00,X
          r[0x0C01 + idx] = (uint8_t)a1;                      // 4CA6 LDA $D9 / 4CA8 STA $0C01,X
        } else {
          // 4CB7: fixed priority packed into the operand's nibbles, the law of C0/ADD7 (CtBuildVelocityPriority's
          // bit-7-clear arm).
          r[0x01D9] = (uint8_t)v;                        // 4CB7 STA $D9 (A still holds the operand)
          r[0x0C01 + idx] = (uint8_t)(v & 0x30);         // 4CB9 AND #30 / 4CBB STA $0C01,X
          // 4CC0-4CC3: four 8-bit ASL A put bits 0-1 of the operand into bits 4-5.
          r[0x0C00 + idx] = (uint8_t)((v << 4) & 0x30);  // 4CC4 AND #30 / 4CC6 STA $0C00,X
        }
        // Both arms then clear the cached anim frame ($0F01 = $FF, the "no frame" marker C0/A8F5 also writes) and its
        // timer: 4CAB/4CAD + 4CB0, or 4CC9/4CCB + 4CCE.
        r[0x0F01 + idx] = 0xFF;
        r[0x1601 + idx] = 0x00;
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xAA:
      case 0xAE:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x8D: {
        // C0/4C27: place at an absolute pixel position. Five bytes; both coordinates are 16-bit reads:
        //   4C27 INY / TYX / 4C29 REP #20 / 4C2B LDA $7F2001,X   -- 16-bit, from ip+1
        //   4C31 STA $1800,X                                      -- 16-bit: sub-pixel and tile
        //   4C34 INY / 4C35 INY / 4C37 LDA $7F2001,X              -- 16-bit, from ip+3
        //   4C3D STA $1880,X
        // The low byte of each store is the sub-tile offset and the high byte the tile index (op 0x8B writes
        // $1801/$1881 directly and fills $1800/$1880 with its literals).
        int px = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);
        r[0x1800 + idx] = (uint8_t)px; r[0x1801 + idx] = (uint8_t)(px >> 8);      // 4C31
        int py = r[0x12001 + ((ip + 3) & 0xFFFF)] | (r[0x12001 + ((ip + 4) & 0xFFFF)] << 8);
        r[0x1880 + idx] = (uint8_t)py; r[0x1881 + idx] = (uint8_t)(py >> 8);      // 4C3D
        // 4C42 LDA $0F80,X / BPL ->4C6F: the same bit-7 gate as op 0x8B, on the object's current tile (4C47-4C4E
        // LDA $1881,X / XBA / LDA $1801,X / TAX), the one just written.
        if (r[0x0F80 + idx] & 0x80)
          ctVmTileAttr(r, idx, (r[0x1801 + idx] | (r[0x1881 + idx] << 8)) & 0xFFFF);
        // 4C6F `TYX` with Y = ip+3 (three INYs ran) / INX / INX / SEC.
        h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xB3:
      case 0xB4:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x7A:
        // C0/4D06; the body (CtVmOp7A) is shared with the live VM.
        CtVmOp7A(r, rom, r + 0x12001, ip, idx, &h); break;
      case 0xB9: case 0xBA: case 0xBC: case 0xBD: {
        // C0/2FC6, C0/2FCA, C0/2FD2 and C0/2FCE: wait a fixed number of visits. Four one-byte entries into the tail at
        // C0/2FD4:
        //   2FC6 LDA #04 / BRA ->2FD4   (0xB9)     2FCA LDA #08 / BRA ->2FD4   (0xBA)
        //   2FCE LDA #20 / BRA ->2FD4   (0xBD)     2FD2 LDA #10   (0xBC, falls straight in)
        // The tail compares the limit with op 0xAD's counter $7F0A01,X:
        //   limit == counter -> 2FE7: clear the counter, ip+1, SEC   (done; the only arm that consumes the byte)
        //   limit >  counter -> 2FF1: INC the counter, ip+0, CLC     (keep waiting)
        //   limit <  counter -> 2FDE: clear the counter, ip+0, SEC   (restart: the runner re-enters this op at once
        //                             with the counter zeroed)
        int lim = (op == 0xB9) ? 0x04 : (op == 0xBA) ? 0x08 : (op == 0xBC) ? 0x10 : 0x20;
        int cnt = r[0x10A01 + idx];                       // 2FD6 CMP $7F0A01,X
        if (lim == cnt) {                                 // 2FDA BEQ ->2FE7
          r[0x10A01 + idx] = 0x00;                        // 2FE9
          h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 2FED TYX / INX / SEC
        }
        if (lim > cnt) {                                  // 2FDC BCS ->2FF1
          r[0x10A01 + idx] = (uint8_t)(cnt + 1);          // 2FF5 INC A / 2FF6
          h.ip = ip; h.carry = 0; h.ok = 1; break;        // 2FFA TYX / CLC
        }
        r[0x10A01 + idx] = 0x00;                          // 2FE0
        h.ip = ip; h.carry = 1; h.ok = 1; break;          // 2FE4 TYX / SEC -- no INX
      }
      case 0xB7:
      case 0xAB:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0xAD: {
        // C0/2F96: wait N visits, counting in $7F0A01,X (used only by this handler and ops 0xB9/0xBA/0xBC/0xBD).
        int n = r[0x12001 + ((ip + 1) & 0xFFFF)];
        int c = r[0x10A01 + idx];                         // 2F9E
        if (n > c) { r[0x10A01 + idx] = (uint8_t)(c + 1); // 2FBF -- cannot wrap: c+1 <= n <= 0xFF
                     h.ip = ip; h.carry = 0; }            // 2FC3 TYX / 2FC4 CLC: park and yield
        else if (n == c) { r[0x10A01 + idx] = 0x00;       // 2FB1
                     h.ip = (ip + 2) & 0xFFFF; h.carry = 1; }   // 2FB5 TYX/INX/INX + 2FB8 SEC
        else { r[0x10A01 + idx] = 0x00;                   // 2FA8 -- a stale counter above N resets
                     h.ip = ip; h.carry = 1; }            // 2FAC TYX / 2FAD SEC: the same op re-runs
                                                          //   this visit, now against counter 0
        h.ok = 1; break;
      }
      case 0xE3:                                          // C0/3C05 -- the control-lock flag $011F
        // 3C0B STA $1F with A 8-bit: one byte (a 16-bit store would spill into $0120, which C0/8840 tests to gate
        // the player collision). The operand is stored as is (values 2/3/4 occur).
        r[0x011F] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x02:                                          // C0/5FB6 -- the priority call
        h.ip = CtVmOp02(r, ip, &h.carry); h.ok = 1; break;
      case 0x04:                                          // C0/6109 -- the blocking priority call
        h.ip = CtVmOp04(r, idx, ip, &h.carry); h.ok = 1; break;
      case 0x80: {
        // C0/421E: load a PC, party-aware. Two bytes, SEC on every path (disasm/notes/pc_loader_op80.md). Not the
        // same as op 0x81 (C0/4476), the always-NPC loader.
        int id = r[0x12001 + ((ip + 1) & 0xFFFF)];           // 4220 LDA $7F2001,X
        r[0x01D9] = (uint8_t)id;                             // 4224 STA $D9 (8-bit)
        // 4226-4268: a seven-way CMP #n / BNE chain storing this object's index into $018D + id (which object holds
        // PC id); there is no default arm, so an id > 6 stores nothing.
        if (id <= 6) r[0x018D + id] = (uint8_t)idx;          // 422A LDA $6D / STA $8D (8-bit)
        ctVmPcLoad(r, rom, romLen, idx, id, (ip + 1) & 0xFFFF, &h);   // 421E INY, then join 426A
        break;
      }
      case 0x57: case 0x5C: case 0x62: case 0x68: case 0x6A: case 0x6C: case 0x6D: {
        // C0/41E4 / 41EC / 41F4 / 41FC / 4204 / 420C / 4214: seven one-byte siblings of op 0x80 with the PC id
        // hardwired: LDA $6D / STA $8D+id / LDA #id / BRA ->421A (STA $D9 / BRA ->426A, op 0x80's party test). Y stays
        // the op's own IP, so C0/448D's INY gives ip+1. The op -> id mapping is not in address order: op 0x68
        // (C0/41FC) is id 4 and op 0x6A (C0/4204) id 3.
        static const struct { int op, id; } PCIMM[7] = {
          { 0x57, 0 }, { 0x5C, 1 }, { 0x62, 2 }, { 0x6A, 3 },     // 41E4 / 41EC / 41F4 / 4204
          { 0x68, 4 }, { 0x6C, 5 }, { 0x6D, 6 },                  // 41FC / 420C / 4214
        };
        int id = 0;
        for (int k = 0; k < 7; k++) if (PCIMM[k].op == op) { id = PCIMM[k].id; break; }
        r[0x018D + id] = (uint8_t)idx;                     // 41E4/41E6 LDA $6D / STA $8D+id
        r[0x01D9] = (uint8_t)id;                           // 421A STA $D9
        ctVmPcLoad(r, rom, romLen, idx, id, ip, &h);       // 421C BRA ->426A, with Y unmoved
        break;
      }
      case 0x0A: case 0x0B: case 0x0C:
        CtVmObjEnableFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0xC1: case 0xC2:
        // C0/35B5 and C0/35FC: op 0xBB's routine with one instruction changed: STZ $30 (359E, two bytes) becomes LDA
        // #01 / STA $30 (35E3) or LDA #02 / STA $30 (362A), and every branch displacement around it shifts by 2. All
        // three BRLs reach the same panic handler C0/2E1E. So $0130 is a three-valued selector, 0/1/2.
        ctVmTextRequest(r, idx, ip, &h, (op == 0xC1) ? 1 : 2);
        break;
      case 0xBB:
        ctVmTextRequest(r, idx, ip, &h, 0);
        break;
      case 0x12: {
        // C0/62DE: branch on a scene-local variable. Five bytes: var, comparand, mode, disp. 62E9 ASL A / 62ED LDA
        // $7F0200,X: stride 2.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = r[0x10200 + ((v * 2) & 0xFFFF)];      // 62F1 STA $D9 (8-bit)
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 62F5 / 62F9 STA $DB
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 62FC
        // 6301 STX $C3 (X = ip+4) is 16-bit (SEP #10 comes at 6304): $01C3 and $01C4.
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);
        // 6303 ASL A is 8-bit and 6307 JSR ($6467,X) has sixteen entries, so a mode >= $10 dispatches past the
        // table. That target is not derived, so the port traps.
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }  // 630C-6312
        ctVmBranchTakenAt(r, ip + 4, &h);                       // 630A BCS ->6349
        break;
      }
      case 0x20: case 0x23: case 0x24:
        CtVmObjQueryFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x21: case 0x22: {
        // C0/65AA and C0/65EB: read an object's tile pair into two scene variables. 0x22 names the object by party
        // slot and BRAs into 0x21's body at 65B2; both are four bytes. LDA $97,X indexes by the operand as is (no
        // ASL), unlike the stride-2 $7F0200 accesses here.
        int o = (op == 0x21) ? r[0x12001 + ((ip + 1) & 0xFFFF)]
                             : ctVmPartySlotTarget(r, r[0x12001 + ((ip + 1) & 0xFFFF)]);   // 65F4
        r[0x01D9] = r[0x1801 + o];                            // 65B3 / 65B6 (8-bit; $01DA untouched)
        r[0x01DB] = r[0x1881 + o];                            // 65B8 / 65BB (8-bit; $01DC untouched)
        int vx = r[0x12001 + ((ip + 2) & 0xFFFF)];            // 65BF
        int vy = r[0x12001 + ((ip + 3) & 0xFFFF)];            // 65D4
        // Both bytes are loaded before either is stored, X first, so if vx == vy the Y byte survives.
        r[0x10200 + ((vx * 2) & 0xFFFF)] = r[0x01D9];         // 65C8 ASL (16-bit) / 65CE
        r[0x10200 + ((vy * 2) & 0xFFFF)] = r[0x01DB];         // 65DD ASL (16-bit) / 65E3
        h.ip = (ip + 4) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x27: case 0x28:
        CtVmObjBranchFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x2D: {
        // C0/66A5 -- skip if nothing is held. $00F8 is the button latch and $00F9 the direction
        // latch; either one non-zero falls through, and only both-zero branches.
        if (r[0x00F8] != 0 || r[0x00F9] != 0) {               // 66A6/66A9 BNE, 66AB/66AE BEQ
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 6701 INY / TYX
        }
        // The base is ip+1: C0/66D0 is a bare TYX with no INX (ops 0x27/0x28 do TYX / INX), which also makes this
        // op two bytes where they are three.
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];             // 66D1
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;             // 66DA (16-bit)
        h.ip = (ip + 1 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xC0: case 0xC3: case 0xC4: {
        // C0/3643 / 36AA / 36DD: the decision-box request and wait. Four arms over one issue tail at C0/3674, entered
        // at its first instruction. The three differ only in the value put in $0130 (0, 1 or 2: STZ $30 vs LDA #0n /
        // STA $30, with the branch displacements shifted to match).
        // Not the 0xBB/0xC1/0xC2 family (ctVmTextRequest): this one is three bytes, reads a second operand, writes
        // $0130 in the prologue, and writes two more bytes on completion.
        if (r[0x012D] == 0) {                                  // 3643 / 3645 BNE -- non-zero continues
          // 3647 LDX #$7FE0 / 364A BRL C0/2E1E: the fatal hang (see ctVmTextRequest). The port traps.
          h.ip = ip; h.carry = 0; h.ok = 0; break;
        }
        if (r[0x0129] != 0) {                                  // 364D / 364F -- a request in flight
          h.ip = ip; h.carry = 0; h.ok = 1; break;             // 3671 TYX / CLC -- no writes
        }
        // 3651 LDX $6D is 16-bit, so this index takes $016E as its high byte (3699's LDA $6D takes the low byte only).
        int x6 = r[0x016D] | (r[0x016E] << 8);
        int a0 = 0x10000 + ((0x0A00 + x6) & 0xFFFF);
        if (r[a0] != 0) {                                      // 3653 / 3657 BNE -- ours completed
          r[a0] = 0x00;                                        // 365D / 365F
          r[0x10000 + ((0x0A80 + x6) & 0xFFFF)] = r[0x0166];   // 3663 LDA $66 / 3665 -- 8-bit both
          r[0x0162] = 0x00;                                    // 3669 STZ $62
          // 366B TYX / three INX / SEC: three bytes.
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        // Arm D: arm the request, then park. Both operands are read without moving the IP (36A7 TYX restores ip);
        // only the completion arm consumes the bytes.
        r[0x0130] = (uint8_t)(op == 0xC0 ? 0x00 : op == 0xC3 ? 0x01 : 0x02);   // 3659/36C0/36F3
        r[a0] = 0x01;                                          // 3674 / 3676
        r[0x012A] = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 367C / 3680
        int o2 = r[0x12001 + ((ip + 2) & 0xFFFF)];             // 3683
        r[0x01D9] = (uint8_t)o2;                               // 3687 STA $D9 -- the whole byte
        r[0x0165] = (uint8_t)(o2 & 0x03);                      // 3689 AND #03 / 368B
        r[0x0164] = (uint8_t)((o2 >> 2) & 0x03);               // 368D LDA $D9 / LSR / LSR / 3691 / 3693
        r[0x0162] = 0x01;                                      // 3695 / 3697 -- the pair of 3669's 0
        r[0x012E] = r[0x016D];                                 // 3699 -- 8-bit, low byte only
        r[0x0132] = 0x00;                                      // 369D STZ $32
        r[0x0129] = 0x01;                                      // 369F / 36A1 -- arms arm B next frame
        // 36A5 TRB $54 clears bit 5 (ops 0x2A/0x2B/0x32 set bits of the same byte with TSB).
        r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x20);
        h.ip = ip; h.carry = 0; h.ok = 1; break;               // 36A7 TYX / CLC -- IP unchanged
      }
      case 0x13: {
        // C0/6313: op 0x12 with wider operands. Six bytes: [var][immLo][immHi][mode][disp]. It lacks op 0x12's SEP #20
        // at 62EB (its only one is at 6330), so the local read, its $D9 spill, the script fetch and its $DB spill are
        // all 16-bit, which makes operand 2 a two-byte immediate.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 6315 (8-bit) / 631E ASL / 631F TAX
        { int a = 0x10200 + ((v * 2) & 0xFFFF);
          r[0x01D9] = r[a]; r[0x01DA] = r[a + 1]; }       // 6320 / 6324 STA $D9 (16-bit)
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 6328 / 632C STA $DB (16-bit): two
        r[0x01DC] = r[0x12001 + ((ip + 3) & 0xFFFF)];     //   script bytes
        int mode = r[0x12001 + ((ip + 4) & 0xFFFF)];      // 6332, after 6330 SEP #20
        // 6337 STX $C3 with X = ip+5, still 16-bit (SEP #10 at 633A): the displacement is one byte further out than
        // in the siblings.
        r[0x01C3] = (uint8_t)(ip + 5); r[0x01C4] = (uint8_t)((ip + 5) >> 8);
        // The off-table bound is 8: 633D JSR ($6477,X) starts at entry 8 of the C0/6467 table, and what follows its
        // last entry is handler code ($6487: A5 D9).
        if (mode >= 0x08) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, 8 | mode)) { h.ip = (ip + 6) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 5, &h);                 // 6340 BCS ->6349
        break;
      }
      case 0x14: {
        // C0/6361: op 0x12 with the right operand a second local. Five bytes: [var1][var2][mode][disp]. Both SEP #20
        // are present (636E, 6383), so the reads and spills are 8-bit and $01DA/$01DC are untouched.
        int v1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 6363 / 636C ASL / 636D TAX
        r[0x01D9] = r[0x10200 + ((v1 * 2) & 0xFFFF)];     // 6370 / 6374 STA $D9 (8-bit)
        int v2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 6378 / 6381 ASL / 6382 TAX
        r[0x01DB] = r[0x10200 + ((v2 * 2) & 0xFFFF)];     // 6385 / 6389 STA $DB (8-bit)
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 638D
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 6392 STX $C3
        // Dispatched from the table base C0/6467, so the bound is $10 (as 0x12/0x16; 0x13/0x15 use 8).
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 4, &h);                 // 639B BCS ->6349
        break;
      }
      case 0x15: {
        // C0/63A4: both operands are local words. Five bytes: [var1][var2][mode][disp]. Its only SEP #20 is at 63CA,
        // after both reads, so all four accesses are 16-bit.
        int v1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 63A6 -- 8-bit, the REP #20 is at 63AA
        { int a = 0x10200 + ((v1 * 2) & 0xFFFF);
          r[0x01D9] = r[a]; r[0x01DA] = r[a + 1]; }       // 63B1 / 63B5 STA $D9 (16-bit)
        // 63B9's 16-bit LDA reads script[ip+2] and [ip+3], but 63BD AND #00FF drops the upper one: the index is one
        // byte.
        int v2 = r[0x12001 + ((ip + 2) & 0xFFFF)];
        { int a = 0x10200 + ((v2 * 2) & 0xFFFF);
          r[0x01DB] = r[a]; r[0x01DC] = r[a + 1]; }       // 63C2 / 63C6 STA $DB (16-bit)
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 63CC, after 63CA SEP #20
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 63D1 STX $C3
        if (mode >= 0x08) { h.ip = ip; h.carry = 0; h.ok = 0; break; }         // the 16-bit half again
        if (ctVmCompare(r, 8 | mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 4, &h);                 // 63DA BCC ->63DF / 63DC BRL ->6349
        break;
      }
      case 0x16: {
        // C0/63E6: branch on a global (storyline) byte. Five bytes: index, comparand, mode byte, disp. The mode byte's
        // bit 7 selects the high page and is masked off before dispatch (63FC arm only).
        int li = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = (uint8_t)li;                          // 63EC STA $D9 -- overwritten at 6410/6427
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 63EF / 63F3 STA $DB
        int mb = r[0x12001 + ((ip + 3) & 0xFFFF)];        // 63F6 / 63FA BPL ->6418
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 63FD / 6419 STX $C3
        int mode = (mb & 0x80) ? (mb & 0x7F) : mb;        // 63FF AND #7F, on the bit-7 arm only
        r[0x01DD] = (uint8_t)mode;                        // 6401 / 641B STA $DD
        int g = (mb & 0x80) ? ((li | 0x0100) & 0xFFFF) : li;   // 640A ORA #0100
        r[0x01D9] = r[0x10000 + g];                       // 6410 / 6427 LDA $7F0000,X / 642B STA $D9
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }   // same off-table case
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }  // 643B-6441
        ctVmBranchTakenAt(r, ip + 4, &h);                       // 6436 BCC ->643B / 6438 BRL ->6349
        break;
      }
      case 0x03:
        // C0/607E: the middle member of the call family. TYX / INX and no INY, so Y stays at the op's own IP (see
        // ctVmCall03).
        ctVmCall03(r, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], &h);        // 6082 / join 6086
        break;
      case 0x05: case 0x06: case 0x07: {
        // C0/61E9 / 61FE / 6214: the party-slot calls, each its parent op with the target from $0197 + slot and a
        // skip arm for an empty slot:
        //   0x05 -> BRL ->5FBE, op 0x02's body      0x06 -> BRL ->6086, op 0x03's body
        //   0x07 -> BRL ->6122 (fresh) and ->61AF (poll), op 0x04's two arms
        // Y matches the parent: 0x05 opens with 61E9 INY (Y = ip+1, like 5FB6), 0x06/0x07 with TYX / INX (Y = ip), so
        // the shared bodies need no IP fixup.
        int slot = r[0x12001 + ((ip + 1) & 0xFFFF)];                    // 61ED / 6202 / 6220
        int tgt  = ctVmPartySlotTarget(r, slot);                        // 61F2 / 6207 / 6225
        if (tgt & 0x80) {                    // 61F4 / 6209 / 6227 BMI -- the slot is empty
          // 61F9 INY INY / 620E INY INY INY / 622C INY INY INY, then TYX / SEC: all land on ip+3.
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        if (op == 0x05)      h.ip = ctVmCall02(r, ip, tgt, &h.carry), h.ok = 1;
        else if (op == 0x06) ctVmCall03(r, ip, tgt, &h);
        else                 h.ip = ctVmCall04(r, idx, ip, tgt, &h.carry), h.ok = 1;
        break;
      }
      case 0xD8:
        // C0/3A83: a request/ack rendezvous on $0117/$0118 bit 0. Three bytes, always CLC; only one arm moves the IP:
        //   $0117 bit 0 set -> 3AAA: park on this byte, no writes (the request is in flight).
        //   $0118 bit 0 set -> 3AAD: LDA #01 / TRB $18 clears the ack and advances ip+3 (CLC / TYX / INX x3).
        //   otherwise       -> 3A8F: issue: stage both operands, ORA #21 into $0117 (bits 0 and 5), STZ $011F, then
        //                      fall into 3AAA and park.
        // The ack is raised inside bank $C0 (LDA #01 / TSB $18 at C0/18BF and C0/18D4).
        if (r[0x0117] & 0x01) { h.ip = ip; h.carry = 0; h.ok = 1; break; }          // 3A87 -> 3AAA
        if (r[0x0118] & 0x01) {                                                     // 3A8D -> 3AAD
          r[0x0118] = (uint8_t)(r[0x0118] & (uint8_t)~0x01);                        // 3AAF TRB $18
          h.ip = (ip + 3) & 0xFFFF; h.carry = 0; h.ok = 1; break;                   // 3AB1-3AB5
        }
        r[0x2989] = r[0x12001 + ((ip + 1) & 0xFFFF)];                               // 3A91 / 3A95
        r[0x2A1F] = r[0x12001 + ((ip + 2) & 0xFFFF)];                               // 3A9A / 3A9E
        r[0x0117] = (uint8_t)(r[0x0117] | 0x21);                                    // 3AA4 ORA #21
        r[0x011F] = 0x00;                                                           // 3AA8 STZ $1F
        h.ip = ip; h.carry = 0; h.ok = 1; break;                                    // falls into 3AAA
      case 0x88:
        CtVmColorFx(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step()
      case 0xB8:
        // C0/3557: stage the text machine's $012B/$012C/$012D. Four bytes, branchless, SEC; TYX / INX walk the
        // operands and the INX at 356D gives ip+4. $012D must be non-zero for op 0xBB (else its panic handler runs).
        r[0x012B] = r[0x12001 + ((ip + 1) & 0xFFFF)];      // 3559 / 355D
        r[0x012C] = r[0x12001 + ((ip + 2) & 0xFFFF)];      // 3560 / 3564
        r[0x012D] = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 3567 / 356B
        h.ip = (ip + 4) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x48: case 0x49: {
        // C0/6792 / C0/67C7 -- the pointer read; the body is CtVmReadPtrFamily (the live VM runs it too). SEC.
        int ni = ip;
        CtVmReadPtrFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x5B: case 0x5E: case 0x5F: case 0x60: case 0x61:
      case 0x63: case 0x64: case 0x67: case 0x69: case 0x6B: case 0x6F: {
        // C0/6A1F..6C68 -- the local arithmetic; the body is CtVmLocalArithFamily (the live VM runs it too). SEC.
        int ni = ip;
        CtVmLocalArithFamily(r, rom, romLen, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
      case 0x55: case 0x56: case 0x58: case 0x59: {
        // C0/68C6..6A11, the variable-assignment grid; the body (CtVmVarFamily) is shared with the live VM's step()
        // and init pass. SEC on every exit.
        int ni = ip;
        CtVmVarFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xA8: case 0xA9: {
        // C0/55A0 and C0/5605: turn to face another object. Two bytes each. 0xA8 takes an object index; 0xA9 a party
        // slot, resolved by 560E LDA $97,X before BRL ->55A9 into 0xA8's body (Y still the op's IP: ip+2 for both).
        // 0xA9 does not test the slot for empty (unlike ops 0x05/0x06/0x07): an empty slot ($80) is used as object
        // index $80.
        // The carry differs by arm: an inactive target exits SEC at 5603 (keep running); the turn exits CLC at 55FE.
        int tgt;
        if (op == 0xA8) tgt = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 55A4 / 55A8 TAX
        else            tgt = r[(0x0197 + r[0x12001 + ((ip + 1) & 0xFFFF)]) & 0x1FFFF];  // 560E
        if (r[0x1100 + tgt] & 0x80) {                                  // 55A9 / 55AC BMI ->5600
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 5600 TYX / INX / INX / SEC
        }
        r[0x01F2] = r[0x1801 + tgt];                                   // 55AE the target's tile X
        r[0x01F3] = r[0x1881 + tgt];                                   // 55B3 ...and tile Y
        r[0x01D9] = (uint8_t)tgt; r[0x01DA] = 0x00;                    // 55B8 STX $D9 -- X is 16-bit
        r[0x01F0] = r[0x1801 + idx];                                   // 55BC this object's tile X
        r[0x01F1] = r[0x1881 + idx];                                   // 55C1 ...and tile Y
        // 55C6-55CE: when both tiles match, the routine re-stages the sub-tile positions and aims with those (self
        // into $F0/$F1 from $1800/$1880, target into $F2/$F3).
        if (r[0x01F1] == r[0x01F3] && r[0x01F0] == r[0x01F2]) {
          r[0x01F0] = r[0x1800 + idx]; r[0x01F1] = r[0x1880 + idx];    // 55D0 / 55D5
          r[0x01F2] = r[0x1800 + tgt]; r[0x01F3] = r[0x1880 + tgt];    // 55DC / 55E1
        }
        int ee = CtMoveDirAngle(r, rom);                               // 55E6 JSR C0/ABA2
        r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));                   // 55F0 / 55F5
        r[0x1601 + idx] = 0x00;                                        // 55F8 STZ $1601,X
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;        // 55FB TYX / INX / INX / CLC
      }
      case 0x9C: {
        // C0/4EC7: move along a heading without turning. Three bytes; op 0x92's twin ($1A80 progress flag, $1A01
        // ticket, park on the op, three arms) minus three things: no LDA $F700,X / STA $1600,X (the facing is kept),
        // no JSR C0/5614 after the velocity build (the animation is kept), and no JSR C0/568B on arrival (4ED6 only
        // clears the flag). A pure translation; the script owns the animation.
        if (r[0x1A80 + idx] != 0) {                        // 4EC9 / 4ECC BEQ ->4EDF
          if (r[0x1A01 + idx] != 0) {                      // 4ECE / 4ED1 BEQ ->4ED6
            h.ip = ip; h.carry = 0; h.ok = 1; break;       // 4ED3 TYX / CLC -- ticket still running
          }
          r[0x1A80 + idx] = 0x00;                          // 4ED6 STZ $1A80,X -- and nothing else
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4ED9 TYX/INX x3 / SEC
        }
        int ee = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 4EE1
        r[0x01EE] = (uint8_t)ee; r[0x01EF] = 0x00;         // 4EEA STA $EE -- 16-bit, so $01EF zeroed
        r[0x1A01 + idx] = r[0x12001 + ((ip + 2) & 0xFFFF)];// 4EEF / 4EF5 the ticket
        r[0x10B00 + idx] = 0x80;                           // 4EFA STA $7F0B00,X -- a literal, as 0x92
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);  // 4EFE INC $1A80,X -- increment, not set
        // 4F01 STY $C7 with Y = the op's own IP (the handler walks operands with TYX/INX), so 4F06 LDX $C7 parks here.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);              // 4F03 JSR C0/ACFD (reads $01EE itself)
        h.ip = ip; h.carry = 0; h.ok = 1; break;           // 4F08 CLC -- the arm visit parks too
      }
      case 0x18: {
        // C0/6442: branch on the storyline byte ($7F0000). Three bytes, SEC on both exits; op 0x1A's twin: 6448 CMP
        // $7F2001,X / 644C BCS ->6452 takes the displacement when storyline >= operand and falls through to ip+3 when
        // it is less. The branch base is ip+2.
        if (r[0x10000] < r[0x12001 + ((ip + 1) & 0xFFFF)]) {           // 644C BCS not taken
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 644E INX / INX / SEC
        }
        int d = r[0x12001 + ((ip + 2) & 0xFFFF)];                      // 6453
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;                      // 645C STA $D9 -- 16-bit
        h.ip = (ip + 2 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 645E-6462
      }
      case 0xED:
        // C0/3CBC: wait on APU mailbox port 1. TYX / LDA $2141 / BNE ->3CC3 / INX / CLC / RTS. One byte, always CLC;
        // non-zero parks, zero advances to ip+1. Unlike op 0xEE ($2143, AND #0F) it tests the whole byte.
        h.ip = (g_ctApuPort1 == 0) ? ((ip + 1) & 0xFFFF) : ip;
        h.carry = 0; h.ok = 1; break;
      case 0xF2:
        // C0/3CFD -- wait on $0118 bit 1, the bit op 0xF0's non-zero arm sets with `TSB $18`. One
        // byte, always CLC, park while set. Same shape as 0xF3, one bit over.
        h.ip = (r[0x0118] & 0x02) ? ip : ((ip + 1) & 0xFFFF);
        h.carry = 0; h.ok = 1; break;
      case 0xF0: {
        // C0/3CD0: screen fade / brightness. Two bytes, CLC on both arms, selected on the operand's low nibble:
        //   low != 0 -> $011C = $011D = low, $011B = high, and LDA #02 / TSB $18 arms the wait op 0xF2 spins on
        //               (a timed fade).
        //   low == 0 -> $0119 = high only: an immediate set of the brightness byte.
        // Both exits are ip+2 (3CD6 INX runs before the split). $0119 is the screen brightness.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 3CD2
        r[0x01D9] = (uint8_t)v;                            // 3CD7 STA $D9
        int lo = v & 0x0F, hi = (v >> 4) & 0x0F;
        if (lo != 0) {
          r[0x011C] = (uint8_t)lo;                         // 3CDD
          r[0x011D] = (uint8_t)lo;                         // 3CDF
          r[0x011B] = (uint8_t)hi;                         // 3CE9
          r[0x0118] = (uint8_t)(r[0x0118] | 0x02);         // 3CED TSB $18
        } else {
          r[0x0119] = (uint8_t)hi;                         // 3CF9
        }
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      }
      case 0x4F: {
        // C0/68A5: local variable = immediate. Three bytes: the value first (ip+1), the index second (ip+2). The index
        // is doubled (68B7 ASL A, stride 2), and the store is 8-bit (SEP #20 at 68B9).
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 68A7
        r[0x01D9] = (uint8_t)v;                            // 68AB STA $D9
        int vi = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 68AE
        r[0x10200 + ((vi * 2) & 0xFFFF)] = (uint8_t)v;     // 68BD STA $7F0200,X
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x19: case 0x1C: case 0x1A:
        CtVmResultFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x65: case 0x66: {
        // C0/6B76 and C0/6BA4: set and clear a bit in the global flag page. Three bytes: operand 1 selects the bit,
        // operand 2 is the byte index.
        //   mask = C0:$FF20[operand1 & $0F]   (0x65, an OR mask)
        //   mask = C0:$FF28[operand1 & $0F]   (0x66, an AND mask)
        //   page = bit 7 of operand 1: 6B88-6B8C LDA $DB / ROL A / ROL A / AND #01 puts the original bit 7 in bit 0
        //     whatever the incoming carry. 6B8E XBA parks it in B, so the 8-bit LDA of operand 2 and the 16-bit TAX at
        //     6B95 give X = page*256 + operand2; the address is $7F0000 + X.
        // The index is the whole low nibble. $FF20 is `01 02 04 08 10 20 40 80 FE FD FB F7 EF DF BF 7F` and $FF28
        // starts at its ninth byte, so $FF28[8..15] runs into `00 00 01 00 02 00 03 00`: a low nibble >= 8 makes 0x65
        // OR with an AND mask and 0x66 AND with 0-3, as in the ROM. Both tables are read from `rom`.
        int o1 = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 6B7A
        r[0x01DB] = (uint8_t)o1;                           // 6B7E STA $DB
        int mt = (op == 0x65) ? 0xFF20 : 0xFF28;           // 6B83 / 6BB1 LDA $FFxx,X
        int m = (mt + (o1 & 0x0F) < romLen) ? MR(mt + (o1 & 0x0F)) : 0;
        r[0x01D9] = (uint8_t)m;                            // 6B86 STA $D9
        int a = 0x10000 + ((((o1 >> 7) & 1) << 8) | r[0x12001 + ((ip + 2) & 0xFFFF)]);
        r[a] = (uint8_t)(op == 0x65 ? (r[a] | m) : (r[a] & m));        // 6B9A ORA / 6BC8 AND
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x84:
        // C0/4867: set the solidity byte $1B01,X from the operand. Two INYs (4867, 4872) and a bare TYX / SEC exit:
        // ip+2.
        //   4867 INY / TYX / LDA $7F2001,X / 486D LDX $6D / 486F STA $1B01,X
        //   4872 INY / 4873 TYX / SEC / RTS
        // $1B01 is what C0/5B95's actor-collision scan tests at 5BDB; the object loaders set it to 1.
        r[0x1B01 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];   // 4869 / 486F
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xE0: {
        // C0/3AB7: stage a location change, fade variant. Both exits are TYX / CLC with Y untouched, so the object
        // parks on the op; the commit happens elsewhere and replaces the script, and a re-entry meanwhile hits the
        // bit-7 guard.
        if (r[0x0117] & 0x80) {                                       // 3AB9 BMI ->3AEE
          h.ip = ip; h.carry = 0; h.ok = 1; break;                    // 3AEE TYX / CLC
        }
        int w1 = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 3ABF
        r[0x01D9] = (uint8_t)w1; r[0x01DA] = (uint8_t)(w1 >> 8);      // 3AC3 STA $D9 (16-bit)
        int loc = w1 & 0x01FF;                                        // 3AC5 AND #01FF -- nine bits
        r[0x0112] = (uint8_t)loc; r[0x0113] = (uint8_t)(loc >> 8);    // 3AC8 STA $12 (16-bit)
        int w2 = r[0x12001 + ((ip + 3) & 0xFFFF)] | (r[0x12001 + ((ip + 4) & 0xFFFF)] << 8);  // 3ACC
        r[0x0114] = (uint8_t)w2; r[0x0115] = (uint8_t)(w2 >> 8);      // 3AD0 STA $14 (16-bit)
        // $0116 is built from the first operand's high byte: the shifted middle nibble is stored, then bit 7 is ORed
        // in with TSB.
        int hb = r[0x01DA];                                           // 3AD4 LDA $DA
        r[0x0116] = (uint8_t)((hb >> 1) & 0x0F);                      // 3AD6/3AD7/3AD9
        r[0x0116] = (uint8_t)(r[0x0116] | (hb & 0x80));               // 3ADF TSB $16
        r[0x0117] = (uint8_t)(r[0x0117] | 0x80);                      // 3AE5 -- the one-shot latch
        r[0x0119] = 0x0F;                                             // 3AE9 STA $19
        h.ip = ip; h.carry = 0; h.ok = 1; break;                      // 3AEB TYX / CLC -- no advance
      }
      case 0xE2:
        CtVmChangeLocVars(r, r + 0x12001, ip, &h); break;             // C0/3B9E, shared with ct_vm.c step()
      case 0x2E:
        CtVmColorSlotOp(r, r + 0x12001, ip, &h); break;   // one body, shared with ct_vm.c step() + the init pass
      case 0xD9: {
        // C0/326C: scatter the party. Seven bytes (three slots x two coordinates), driven by the phase counter
        // $7F03FC, which it reads and writes, so it re-enters on later frames:
        //   phase 0      -> the first visit: release each occupied slot and stash its destination
        //   phase 1..3   -> park at ip
        //   phase >= 4   -> the completion check, the only path that consumes the seven bytes
        // The phase reaches 4 elsewhere (the drain that finishes the walk-off).
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 326C STY $C7 (16-bit)
        int ph = r[0x103FC];                                          // 326E LDA $7F03FC
        if (ph != 0) {                                                // 3272 BEQ ->32C8
          if (ph < 0x04) { h.ip = ip; h.carry = 0; h.ok = 1; break; } // 3274/3276 -- 3278 TYX / CLC
          ctVmPartyClear(r);                                          // 327B..3296
          // 329A/32A4/32AE: a slot not flagged in bit 7 whose $1C00 priority is still 0 parks the whole op
          // (BEQ ->3278); it completes only once every occupied slot's priority is non-zero.
          for (int s = 0; s < 3; s++) {
            int v = r[0x0197 + s * 2];
            if (!(v & 0x80) && r[0x1C00 + v] == 0) { h.ip = ip; h.carry = 0; h.ok = 1; goto d9done; }
          }
          r[0x103FC] = 0x00;                                          // 32B8/32BA reset the phase
          h.ip = (ip + 7) & 0xFFFF; h.carry = 0; h.ok = 1;            // 32BE x7 INY / TYX / CLC
          d9done: break;
        }
        r[0x011F] = 0x00;                                             // 32C8 STZ $1F
        // Slot 0 is released without a bit-7 test, and takes its read index from a 16-bit LDX $97 rather than a
        // zero-extended TAX.
        ctVmPartyRelease(r, 0x0197, r[0x0197] | (r[0x0198] << 8), 0x171F);   // 32CA..3301
        ctVmPartyDest(r, ip, 1, 0x3728);                              // 3306..333C
        static const struct { int dp, lit, row, step, phase; } SL[2] = {
          { 0x0199, 0x173F, 0x3748, 3, 0x03 }, { 0x019B, 0x175F, 0x3768, 5, 0x02 } };
        for (int s = 0; s < 2; s++) {
          if (r[SL[s].dp] & 0x80) {                                   // 3344 BPL / 33CB BPL
            r[0x103FC] = (uint8_t)SL[s].phase;                        // 3346 #03 / 33CD #02
            h.ip = ip; h.carry = 0; h.ok = 1; goto d9out;             // 334C LDX $C7 / CLC
          }
          ctVmPartyRelease(r, SL[s].dp, r[SL[s].dp], SL[s].lit);      // 3350.. / 33D7..
          ctVmPartyDest(r, ip, SL[s].step, SL[s].row);                // 338B.. / 3412..
        }
        r[0x103FC] = 0x01;                                            // 3450/3452
        h.ip = ip; h.carry = 0; h.ok = 1;                             // 3456 LDX $C7 / CLC
        d9out: break;
      }
      case 0xDA: {
        // C0/345A -- the one-byte sibling of 0xD9, releasing slots 1 and 2 only, on its own phase
        // counter $7F03FE and behind the $0154 bit-0 latch. Both its copies park $16FF in $1180,
        // where 0xD9 uses three different literals.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 345A STY $C7
        if (r[0x0199] & 0x80) {                                       // 345F/3461 BPL ->3466
          h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;     // 3463 INX / CLC
        }
        if (r[0x0154] & 0x01) {                                       // 3466/3468 BIT #01
          // The latch is up: a later visit, waiting for the phase to reach 3.
          if (r[0x103FE] < 0x03) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 3470/3472 -- 3474
          r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x01);          // 3477/3479 TRB $54
          ctVmPartyClear(r);                                          // 347B..3496
          h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 349A LDX $C7 / INX / SEC
        }
        // 349F STX $C3: a second 16-bit IP spill, to a different pair than $C7 (0xD9 reloads from $C7 only).
        r[0x01C3] = (uint8_t)ip; r[0x01C4] = (uint8_t)(ip >> 8);      // 349F
        ctVmPartyRelease(r, 0x0199, r[0x0199], 0x16FF);               // 34A1..34D9
        if (r[0x019B] & 0x80) {                                       // 34E2/34E4 BPL ->34F4
          r[0x103FE] = 0x02;                                          // 34E6/34E8
          r[0x0154] = (uint8_t)(r[0x0154] | 0x01);                    // 34EE/34F0 TSB $54
          h.ip = ip; h.carry = 0; h.ok = 1; break;                    // 34EC LDX $C7 / CLC
        }
        r[0x01C3] = (uint8_t)ip; r[0x01C4] = (uint8_t)(ip >> 8);      // 34F4 STX $C3 (same value)
        ctVmPartyRelease(r, 0x019B, r[0x019B], 0x16FF);               // 34F6..352C
        r[0x103FE] = 0x01;                                            // 3533/3535
        r[0x0154] = (uint8_t)(r[0x0154] | 0x01);                      // 3539/353B TSB $54
        h.ip = ip; h.carry = 0; h.ok = 1; break;                      // 3531 LDX $C3 / CLC
      }
      case 0xF1: case 0xFE:
        // C0/3D07 / C0/3F26; the bodies (CtVmScreenFxOp) are shared with the live VM.
        CtVmScreenFxOp(r, r + 0x12001, ip, &h); break;
      case 0xF3:
        // C0/3D7A: wait on $0118 bit 3. TYX / LDA $18 / BIT #08 / BNE ->3D82 / INX / CLC / RTS. One byte, CLC either
        // way: bit set parks on this byte, bit clear advances to ip+1.
        h.ip = (r[0x0118] & 0x08) ? ip : ((ip + 1) & 0xFFFF);
        h.carry = 0; h.ok = 1; break;
      case 0xFF: {
        // C0/3FAF: the screen-mode op. 3FB7 CMP #90 / 3FB9 BCS ->4000 splits it:
        //   subcmd <  $90  the mode switch: a fade loop with JSL $FD:FFF7, then 3FD6 LDX #06FF / TXS resets the stack
        //                  and it never returns to the runner (JML $C3:0000 or one of four JML $FD:xxxx stubs).
        //                  Not ported: the port traps.
        //   subcmd >= $90  4000..400D: indexes the 16-entry table at C0/400E and ends 400C CLC / RTS, so every
        //                  returning sub-handler yields, with the IP the sub-handler left in X.
        // $01DF = subcmd is written first on every call, so the trapped arms commit it too.
        int sc = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 3FB1
        r[0x01DF] = (uint8_t)sc;                          // 3FB5 STA $DF -- before the CMP
        // Not ported inside the >= $90 half: $96 (JML $FD:C000) and $9F ($C2:8004). $A0-$A3 are the ROM's invalid-op
        // stub $5F6E (table entries 16-19), and past $A3 the index runs into code.
        if (sc < 0x90 || sc == 0x96 || sc == 0x9F || sc > 0x9F) {
          h.ip = ip; h.carry = 0; h.ok = 0; break;        // 0x96 -> JML $FD:C000, 0x9F -> $C2:8004
        }
        switch (sc) {
          case 0x90: case 0x97: {
            // C0/4036 and C0/4191: identical but for the mode they arm. Five bytes: three operands from ip+2..ip+4;
            // $013C is zeroed.
            r[0x013A] = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 4039 / 4194
            r[0x013B] = r[0x12001 + ((ip + 3) & 0xFFFF)];         // 4040 / 419B
            r[0x013D] = r[0x12001 + ((ip + 4) & 0xFFFF)];         // 4047 / 41A2
            r[0x013C] = 0x00;                                     // 404D / 41A8 STZ $3C
            r[0x0139] = (uint8_t)(sc == 0x90 ? 0x01 : 0x04);      // 404F / 41AA -- the only difference
            h.ip = (ip + 5) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          }
          case 0x91:                                              // C0/4055
            r[0x0139] = 0x02;                                     // 4058 / 405A
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C:
            // C0/41B0/41B4/41B8/41C0/41C4 -- five two-byte entries into one tail at C0/41BA
            // (`STA $39 / TYX / INX / INX`). $41B8 is the fall-through case and the other four BRA in.
            r[0x0139] = (uint8_t)(sc == 0x98 ? 0x05 : sc == 0x99 ? 0x07 :
                                  sc == 0x9A ? 0x09 : sc == 0x9B ? 0x0B : 0x0E);
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x9D:                                              // C0/41DC
            // LDA #02 / TRB $54 clears bit 1 of $0154.
            r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x02);    // 41DE
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x9E:                                              // C0/41C8
            if (r[0x0139] == 0x00) {                              // 41CA BEQ ->41D6
              r[0x0139] = 0x0C;                                   // 41D6 / 41D8
              h.ip = ip; h.carry = 0; h.ok = 1; break;            // 41DA TYX -- parks
            }
            if (r[0x0139] == 0x0D) {                              // 41CC CMP #0D / 41CE BEQ ->41D2
              h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;   // and $0139 is left at $0D
            }
            h.ip = ip; h.carry = 0; h.ok = 1; break;              // 41D0 -- still running, park
          default: {
            // C0/405D / 40EF / 40B3 / 4145 -- ops 0x92/0x93/0x94/0x95, one shape with two finish
            // bodies. Each is a three-state handshake against $0139 and $0350.
            int p = (sc == 0x92 || sc == 0x94) ? 0x01 : 0x02;     // 4098 / 412A / 40D4 / 4166
            int c = (sc == 0x92) ? 0x00 : (sc == 0x93) ? 0x01 : (sc == 0x95) ? 0x02 : 0x03;
            int shortFinish = (sc == 0x94 || sc == 0x95);         // 40C2 / 4154 vs 406C / 40FE
            if (r[0x0139] == 0x00) {                              // 405F BEQ ->408E
              if (r[0x0350] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 4091 -- busy, park
              r[0x0351] = 0x00;                                   // 4095 STZ $0351
              r[0x0350] = (uint8_t)p;                             // 409A
              r[0x0352] = 0xC8; r[0x0353] = 0x17;                 // 40A0 STX $0352 -- 16-bit
              r[0x0354] = 0x7F;                                   // 40A5 -- so $0352-4 = $7F:17C8
              r[0x0139] = 0x03;                                   // 40AA
              r[0x013C] = (uint8_t)c;                             // 40AE
              h.ip = ip; h.carry = 0; h.ok = 1; break;            // 40B1 TYX -- parks until done
            }
            // 4061 CMP #03 / 4063 BNE: any mode other than 3 is somebody else's, so park.
            if (r[0x0139] != 0x03 || r[0x0350] != 0) {            // 4063 / 4068
              h.ip = ip; h.carry = 0; h.ok = 1; break;            // 406A TYX
            }
            if (shortFinish) {
              r[0x0119] = 0x00;                                   // 40C2 / 4154 STZ $19 -- brightness
              r[0x0139] = 0x00;                                   // 40C4 / 4156 STZ $39
            } else {
              r[0x0139] = 0x00;                                   // 406C / 40FE
              r[0x11522] = 0x00; r[0x11523] = 0x00;               // 4070 / 4074 (long, 8-bit each)
              r[0x0BD9] = 0x00; r[0x0BDA] = 0x00;                 // 407A / 407D
              r[0x0BDB] = 0x00; r[0x0BDD] = 0x00;                 // 4080 / 4083 -- $0BDC is skipped
              r[0x0128] = (uint8_t)(r[0x0128] & (uint8_t)~0x80);  // 4088 TRB $28 -- reset, not store
            }
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          }
        }
        break;
      }
      case 0xE4:
        // C0/3D97: the immediate scripted tile copy (CtVmTileCopyE4, shared with the live VM); this runner keeps the
        // script at $7F:2001.
        CtVmTileCopyE4(r, r + 0x12001, ip);
        h.ip = (ip + 8) & 0xFFFF; h.carry = 0; h.ok = 1; break;       // 3DC0 LDX $C7 / INX / CLC
      case 0xE5:
        // C0/3DC5: stage a tile copy (CtVmTileStageE5, shared with the live VM): $013E-$0145 and $0117 bit 5; the main
        // loop's C0/1770 copies (ct_vm.c, the $0117 bit-5 service). Exits CLC.
        CtVmTileStageE5(r, r + 0x12001, ip);
        h.ip = (ip + 8) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0xE6: case 0xF4:
        CtVmScrollFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0xE7:
        // C0/3E30: a three-state op on the counter $1D00, each arm with its own IP:
        //   $1D00 == 0 -> 3E41: arm: both operands doubled (ASL A) into $1D02/$1D03, INC the counter, clear $011F,
        //                 then 3E59 TYX / CLC parks on this byte.
        //   $1D00 == 1 -> 3E36 BEQ ->3E59: park, no writes (the wait).
        //   otherwise  -> 3E38: clear the counter and advance three bytes, SEC.
        if (r[0x1D00] == 0) {
          r[0x1D02] = (uint8_t)(r[0x12001 + ((ip + 1) & 0xFFFF)] << 1);   // 3E43-3E48
          r[0x1D03] = (uint8_t)(r[0x12001 + ((ip + 2) & 0xFFFF)] << 1);   // 3E4C-3E51
          r[0x1D00] = (uint8_t)(r[0x1D00] + 1);                           // 3E54 INC $1D00
          r[0x011F] = 0x00;                                               // 3E57 STZ $1F
          h.ip = ip; h.carry = 0; h.ok = 1; break;
        }
        if (r[0x1D00] == 1) { h.ip = ip; h.carry = 0; h.ok = 1; break; }  // 3E36 -> 3E59
        r[0x1D00] = 0x00;                                                 // 3E38 STZ $1D00
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x08:
        // C0/6240: arm this object's busy flag: LDX $6D / LDA #01 / STA $1C01,X / TYX / INX / SEC / RTS. One byte.
        // $1C01,X is what op 0x02 rejects on (5FC3) and op 0x04 parks on (6127).
        r[0x1C01 + idx] = 0x01;                            // 6244
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x09:
        // C0/624B -- its partner: `LDA #00 / STA $1C01,X / BRA ->6247`, joining 0x08's own tail.
        r[0x1C01 + idx] = 0x00;                            // 624F
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x0D:
        // C0/6295: set the per-object collision gate $1C80,X. Two bytes, ip+2, carry set.
        //   6295 INY / TYX / TDC / XBA   (TDC / XBA zeroes A's high byte; no memory access)
        //   6299 LDA $7F2001,X / 629D LDX $6D / 629F STA $1C80,X
        //   62A2 BRA ->62B1 = TYX / INX / SEC / RTS
        // $1C80 bit 0 is what C0/ACFD tests (ADA1-ADA6) to decide whether the bounce arm exists.
        r[0x1C80 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x0E:
        // C0/62A4: op 0x0D with STA $1C81,X, and its own tail (`BB E8 38 60`). $1C81 bit 0 is op 0x96's
        // snap-to-tile-centre opt-in (C0/4F90 BIT #01).
        r[0x1C81 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xAC:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x90:
        // C0/4CD5: show. The head of the family sharing the tail 4CD9 STA $1A81,X / TYX / INX / CLC / RTS: 0x90
        // enters with #$01, 0x91 (C0/4CE0) with #$00, 0x7E (C0/4CE6) with #$80. One byte, ip+1, carry clear. $1A81 is
        // the visibility latch C0/A88D rejects on (0 or bit 7 set).
        r[0x1A81 + idx] = 0x01;                               // 4CD7 LDA #01 / 4CD9 STA $1A81,X
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x7C: case 0x7D:
        // C0/4CEC and C0/4CF9: the other-object entries into the 4CD9 tail. They TAX an operand, so the store lands
        // on that object's $1A81:
        //   4CEC INY / TYX / TDC / XBA / LDA $7F2001,X / 4CF4 TAX / LDA #01 / BRA ->4CD9
        //   4CF9 the same with #00
        // Two bytes each (the extra INY), and the tail's CLC yields the visit as for self.
        r[0x1A81 + r[0x12001 + ((ip + 1) & 0xFFFF)]] = (uint8_t)(op == 0x7C ? 0x01 : 0x00);
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x7E:
        // C0/4CE6: the third entry, with #$80. A88D also rejects on $1A81 bit 7 (A899 BMI ->A910).
        r[0x1A81 + idx] = 0x80;                               // 4CE8 LDA #80 / 4CEA BRA ->4CD9
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x91:
        // C0/4CE0: LDX $6D / LDA #00 / BRA ->4CD9, op 0x90's tail with 0 (hide). One byte, ip+1, carry clear (yield).
        r[0x1A81 + idx] = 0;                              // 4CD9 with A = #00
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x92: {
        // C0/4E73: move along a fixed heading for a duration. Three bytes (heading, duration). It aims once (op 0x96
        // re-aims each visit) and runs the clock down, with $1A80,X as its progress flag. It parks on the op; only
        // the arrival visit consumes the bytes.
        if (r[0x1A80 + idx] != 0) {                       // 4E75 LDA $1A80,X / 4E78 BEQ ->4E8E
          if (r[0x1A01 + idx] != 0) {                     // 4E7A LDA $1A01,X / 4E7D BEQ ->4E82
            h.ip = ip; h.carry = 0; h.ok = 1; break;      // 4E7F TYX / 4E80 CLC -- ticket still running
          }
          r[0x1A80 + idx] = 0x00;                         // 4E82 STZ $1A80,X -- the leg is over
          moveArriveAnim(r, idx);                         // 4E85 JSR C0/568B
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4E88 TYX/INX x3 / 4E8C SEC
        }
        // 4E8E: start. The heading comes from the operand as is (no C0/ABA2).
        int ee = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4E8E TYX/INX / 4E90 LDA $7F2001,X
        // 4E94 REP #20 makes 4E99 STA $EE 16-bit ($01EE and $01EF), the high byte cleared by 4E96 AND #00FF.
        r[0x01EE] = (uint8_t)ee; r[0x01EF] = 0x00;        // 4E99 STA $EE -- DP = $0100, so $01EE
        r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));      // 4E9E LDA $F700,X / 4EA3 STA $1600,X
        r[0x10B00 + idx] = 0x80;                          // 4EA6 LDA #80 / 4EA8 STA $7F0B00,X --
                                                          //   a literal here; op 0x96 mirrors $1000
        r[0x1A01 + idx] = r[0x12001 + ((ip + 2) & 0xFFFF)];   // 4EAF operand 2 = duration -> ticket
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);     // 4EB8 INC $1A80,X -- an increment,
                                                          //   8-bit (M still set from 4E9C SEP #20)
        // 4EBB STY $C7 spills the IP (C0/ACFD clobbers Y) and 4EC3 LDX $C7 reloads it; 16-bit: $01C7 and $01C8.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);               // 4EBD JSR C0/ACFD (reads $01EE itself)
        ctMoveStepAnim5614(r, idx);                       // 4EC0 JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4EC3 LDX $C7 / 4EC5 CLC -- park and yield
      }
      case 0xEE:
        // C0/3CC5: wait on the sound driver, the only field op that reads outside WRAM.
        //   3CC5 BB          TYX          X = the IP of this op
        //   3CC6 AD 43 21    LDA $2143    the SPC's output mailbox port 3
        //   3CC9 29 0F       AND #0F
        //   3CCB D0 01       BNE ->3CCE   low nibble non-zero -> X stays parked on the op
        //   3CCD E8          INX          zero -> consume the byte
        //   3CCE 18 60       CLC / RTS    both arms yield; only the IP differs
        // $2143 reads the SPC's output port 3 (CPU writes to $2140-$2143 go to its input ports); the bank-$C7 driver
        // writes it and the field engine reads it only here, a CPU<->APU handshake the host supplies (g_ctApuPort3 in
        // move.h). C2/CBE0 (LDA $00:2143 / AND #0F / BEQ back) spins on the same nibble.
        // Note: src/ct_vm.c has the same test for the live VM (vmSoundPort(3) & 0x0F, park on non-zero); change both
        // together.
        h.ip = ((g_ctApuPort3 & 0x0F) == 0) ? ((ip + 1) & 0xFFFF) : ip;
        h.carry = 0; h.ok = 1; break;
      case 0xA1: {
        // C0/5191: op 0x97 without the anim/facing layer. 51F1 JSR C0/ABA2 is followed directly by 51F4 JSR C0/5B95
        // (no F700 lookup, no STA $1600,X), so the collision test uses the old facing, the +$30 nudge writes only
        // $01EE, the arrival has no JSR C0/568B, and nothing calls C0/5614. Otherwise op 0x97's three-byte
        // park-on-op shape.
        if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 5193/5196/5198
        int t1 = r[0x10200 + ((r[0x12001 + ((ip + 1) & 0xFFFF)] * 2) & 0xFFFF)]; // 51AA / 51AE
        int t2 = r[0x10200 + ((r[0x12001 + ((ip + 2) & 0xFFFF)] * 2) & 0xFFFF)]; // 51C0 / 51C4
        r[0x01F2] = (uint8_t)t1; r[0x01F3] = (uint8_t)t2;
        r[0x01F0] = r[0x1801 + idx]; r[0x01F1] = r[0x1881 + idx];                // 51C8 / 51CD
        if (r[0x1881 + idx] == t2 && r[0x1801 + idx] == t1) {                    // 51D4 / 51DA
          if ((r[0x1C81 + idx] & 1) && ctArriveSnap305D(r, idx)) {               // 51DF BIT #01 / 51E3
            h.ip = ip; h.carry = 0; h.ok = 1; break;                             // 51EE -- no 5614
          }
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;                // 51E8 -- no 568B
        }
        int ee = CtMoveDirAngle(r, rom);                  // 51F1 JSR C0/ABA2
        r[0x01EE] = (uint8_t)ee;
        if (CtCollisionActorMovable(r, idx)) {            // 51F4 -- against the old facing
          ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;   // 51F9-51FE -- and no F700 write
        }
        r[0x1A01 + idx] = r[0x1000 + idx];                // 5202 / 5205
        r[0x10B00 + idx] = r[0x1000 + idx];               // 5208
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);   // 520C STY $C7 (16-bit)
        CtBuildVelocityPriority(r, rom, idx);             // 520E JSR C0/ACFD
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 5211 LDX $C7 -- no 5614
      }
      case 0x9D: {
        // C0/4F0A -- op 0x92 with both operands read through the scene-var array and the anim layer
        // removed: no `JSR C0/568B` on the drain arm (4F19 is `9E 80 1A BB ...` where 0x92 has
        // `9E 80 1A 20 8B 56`), no $1600 write, and no `JSR C0/5614`.
        if (r[0x1A80 + idx] != 0) {                       // 4F0C / 4F0F BEQ ->4F22
          if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 4F14 / 4F16
          r[0x1A80 + idx] = 0x00;                         // 4F19 STZ $1A80,X -- and no 568B
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4F1C TYX / INX x3
        }
        // The heading load is 16-bit (4F2F) and the duration load 8-bit (4F46; SEP #20 at 4F44 between them). The
        // heading's AND #00FF drops the high half, so STA $EE zeroes $01EF.
        int ee = r[0x10200 + ((r[0x12001 + ((ip + 1) & 0xFFFF)] * 2) & 0xFFFF)];   // 4F2D / 4F2F
        r[0x01EE] = (uint8_t)ee; r[0x01EF] = 0x00;        // 4F37 STA $EE (16-bit)
        r[0x1A01 + idx] = r[0x10200 + ((r[0x12001 + ((ip + 2) & 0xFFFF)] * 2) & 0xFFFF)];  // 4F46/4F4C
        r[0x10B00 + idx] = 0x80;                          // 4F4F/4F51 -- a literal, like 0x92's
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1); // 4F55 INC $1A80,X (8-bit)
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);   // 4F58 STY $C7
        CtBuildVelocityPriority(r, rom, idx);             // 4F5A JSR C0/ACFD -- reads $01EE itself
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4F5D LDX $C7 -- no 5614, no $1600
      }
      case 0x94: case 0x95: case 0x9E: case 0x9F: case 0x98: case 0x99: {
        // C0/5215 / 5410 (0x94/0x95), C0/52C0 / 54F5 (0x9E/0x9F) and C0/534A / 550E (0x98/0x99).
        // The odd member of each pair resolves its operand through the party-slot table -- three
        // inserted bytes, `B5 97 AA` (LDA $97,X / TAX) -- and everything downstream is shared.
        int legged = (op == 0x98 || op == 0x99);
        int anim   = (op == 0x94 || op == 0x95 || op == 0x98 || op == 0x99);
        int viaSlot = (op == 0x95 || op == 0x9F || op == 0x99);
        ctVmMoveActorOp(r, rom, idx, ip, viaSlot, anim, legged, legged ? 3 : 2, &h);
        break;
      }
      case 0x8F: {
        // C0/5429: move toward a party member until adjacent or well inside the camera window. Two bytes. No $1C81
        // test and no snap call (neither 89 01 / C0/305D nor 89 02 / C0/30B3 occurs in 5429-54F4); instead it runs
        // op 0x28's box test on the same halved camera bytes.
        if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 542B / 5430
        int tgt = ctVmPartySlotTarget(r, r[0x12001 + ((ip + 1) & 0xFFFF)]);      // 543C LDA $97,X
        if (r[0x1100 + tgt] & 0x80) {                     // 543F / 5442 BMI ->54A8
          // 5442 BMI ->54A8 skips 54A6 LDX $6D, leaving X = the target, but C0/568B starts with LDX $6D and acts on
          // self anyway.
          moveArriveAnim(r, idx);                         // 54A8 JSR C0/568B -- self, via its own LDX
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 54AB TYX / INX INX
        }
        r[0x01F2] = r[0x1801 + tgt];                      // 5444 / 5447
        r[0x01F3] = r[0x1881 + tgt];                      // 5449 / 544C
        // 544E STX $E9 is written although this op never calls C0/30B3.
        r[0x01E9] = (uint8_t)tgt; r[0x01EA] = (uint8_t)(tgt >> 8);
        r[0x01F0] = r[0x1801 + idx];                      // 5452 / 5455
        r[0x01F1] = r[0x1881 + idx];                      // 5457 / 545A
        int sy = r[0x1881 + idx], sx2 = r[0x1801 + idx];
        int yAdj = (sy == r[0x01F3]) || (((sy + 1) & 0xFF) == r[0x01F3]) ||
                   (((sy - 1) & 0xFF) == r[0x01F3]);      // 545C-546F
        int xAdj = (sx2 == r[0x01F2]) || (((sx2 + 1) & 0xFF) == r[0x01F2]) ||
                   (((sx2 - 1) & 0xFF) == r[0x01F2]);     // 5471-547A
        if (yAdj && xAdj) {
          moveArriveAnim(r, idx);                         // 54A6 LDX $6D / 54A8 -- self
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        // Not adjacent: the camera-window test. These two writes happen on both remaining arms; on the step arm
        // C0/ACFD overwrites them.
        r[0x01DB] = (uint8_t)(r[0x1D0A] >> 1);            // 547C LDA $1D0A (8-bit) / 547F LSR / 5480
        r[0x01DD] = (uint8_t)(r[0x1D0E] >> 1);            // 5482 / 5485 / 5486
        int dx = (r[0x01F0] - r[0x01DB]) & 0xFF;
        int dy = (r[0x01F1] - r[0x01DD]) & 0xFF;
        if (dx >= 0x02 && dx <= 0x0D && dy >= 0x02 && dy <= 0x0C) {   // the same asymmetric box op
          moveArriveAnim(r, idx);                         // 0x28 uses -- 548D..54A4 all -> 54B6
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        int ee = CtMoveDirAngle(r, rom);                  // 54B6 JSR C0/ABA2
        r[0x01EE] = (uint8_t)ee;
        r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));      // 54C0 / 54C5
        if (CtCollisionActorMovable(r, idx)) {            // 54C8 JSR C0/5B95
          ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;          // 54CD-54D2
          r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));               // 54D5 / 54DA
        }
        r[0x1A01 + idx] = r[0x1000 + idx];                // 54DF / 54E2
        r[0x10B00 + idx] = r[0x1000 + idx];               // 54E5
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);     // 54E9 STY $C7
        CtBuildVelocityPriority(r, rom, idx);             // 54EB JSR C0/ACFD
        ctMoveStepAnim5614(r, idx);                       // 54EE JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 54F1 LDX $C7
      }
      case 0x96: case 0x97: {
        // C0/4F61: move to an absolute tile. Park-on-op: it consumes its three bytes only on the visit that declares
        // arrival; every other visit re-parks on the op. Five paths, one of them SEC.
        // C0/5084 (op 0x97) is the same routine with each operand fetch replaced by REP #20 / AND #00FF / ASL A /
        // TAX / SEP #20 / LDA $7F0200,X (an 8-bit read of the scene-var array, stride 2). From 50B9 on its bytes
        // equal op 0x96's from 4F7A.
        if (r[0x1A01 + idx] != 0) {                       // 4F63 LDA $1A01,X / 4F66 BEQ ->4F6B
          h.ip = ip; h.carry = 0; h.ok = 1; break;        // 4F68 TYX / CLC: mid-step, yield
        }
        int t1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4F6D operand 1 = target tile X
        int t2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 4F74 operand 2 = target tile Y
        if (op == 0x97) {                                 // 5099/509D and 50AF/50B3
          t1 = r[0x10200 + ((t1 * 2) & 0xFFFF)];
          t2 = r[0x10200 + ((t2 * 2) & 0xFFFF)];
        }
        // 4F71/4F78/4F7F/4F84 stage the four bytes C0/ABA2 reads ($01F0-$01F3).
        r[0x01F2] = (uint8_t)t1; r[0x01F3] = (uint8_t)t2;
        r[0x01F0] = r[0x1801 + idx]; r[0x01F1] = r[0x1881 + idx];
        if (r[0x1881 + idx] == t2 && r[0x1801 + idx] == t1) {          // 4F86/4F88 then 4F8A/4F8C
          // Arrived. $1C81,X bit 0 is the snap-to-centre opt-in (set by op 0x0E, default 0); C0/305D returns SEC only
          // when it had to move.
          if ((r[0x1C81 + idx] & 1) && ctArriveSnap305D(r, idx)) {     // 4F90/4F93 BIT #01 / 4F97 JSR
            ctMoveStepAnim5614(r, idx);                   // 4FA5 -- the snap is a step, so re-select
            h.ip = ip; h.carry = 0; h.ok = 1; break;      // 4FA8 TYX / CLC: still parked
          }
          moveArriveAnim(r, idx);                         // 4F9C JSR C0/568B
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 4F9F TYX/INX x3 / 4FA3 SEC
        }
        // 4FAB: not arrived: issue a fresh step (C0/ABA2 subtracts the staged bytes itself, unsigned).
        int ee = CtMoveDirAngle(r, rom);                  // 4FAB JSR C0/ABA2 -- writes $01EE
        r[0x01EE] = (uint8_t)ee;                          // ABB6/ABC7/... -- $EE is DP, so $01EE
        r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));      // 4FB5 LDA $F700,X / 4FBA STA $1600,X
        if (CtCollisionActorMovable(r, idx)) {            // 4FBD JSR C0/5B95 / 4FC0 BCC ->4FD2
          ee = (ee + 0x30) & 0xFF;                        // 4FC2-4FC5: the +$30 sidestep
          r[0x01EE] = (uint8_t)ee;                        // 4FC7
          r[0x1600 + idx] = MR(0xF700 + (ee & 0xFF));    // 4FCA/4FCF -- the facing is rewritten too
        }
        r[0x1A01 + idx] = r[0x1000 + idx];                // 4FD4/4FD7 arm the ticket
        r[0x10B00 + idx] = r[0x1000 + idx];               // 4FDA mirror to $7F0B00
        // 4FDE STY $C7 spills the IP (C0/ACFD clobbers Y) for 4FE6 LDX $C7; 16-bit: $01C7 and $01C8.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);               // 4FE0 JSR C0/ACFD (reads $01EE itself)
        ctMoveStepAnim5614(r, idx);                       // 4FE3 JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4FE6 LDX $C7 / 4FE8 CLC
      }
      case 0xA0:                                          // MOVE_TO_TILE (C0/5129) -- CtActorMoveStep
        h.ip = CtActorMoveStep(r, rom, idx) ? ((ip + 3) & 0xFFFF) : ip;   // arrived -> IP+3 + continue
        h.carry = (h.ip != ip); h.ok = 1; break;          // arrived -> carry set; yield/issue -> clear
      case 0xA6:                                           // SET_FACING (C0/5545) -- CtVmFacingFamily, with its nine
      case 0x0F: case 0x17: case 0x1B: case 0x1D:          //   siblings: one tail, C0/554B-5556, which also zeroes
      case 0x1E: case 0x1F: case 0x25: case 0x26: case 0xA7: {   // the anim timer $1601,X
        int ni, tg; CtVmFacingFamily(r, r + 0x12001, ip, idx, &ni, &tg);
        h.ip = ni; h.carry = 0; h.ok = 1; break; }
      case 0x81: {
        // C0/4476: the always-NPC object loader. Straight-line (no branch or call in C0/4476..4556): one exit, ip+2,
        // carry set. It falls through into the descriptor walk at C0/448D, the body op 0x80's in-party arms JSR to
        // (ctPcLoad: record base $E4:F000, unbiased; op 0x82's copy at C0/45A3 reads $E4:F024, entry id+7), so it
        // adds only a four-store prologue.
        // $1100,X = 3 is the always-NPC marker (op 0x82 writes 4, op 0x83 5). Unlike op 0x80, nothing overwrites
        // $1600,X afterwards: ctPcLoad leaves it at 1.
        r[0x1100 + idx] = 0x03;                             // 4476 LDA #03 / 4478 LDX $6D / 447A
        r[0x1B01 + idx] = 0x01;                             // 447D LDA #01 / 447F -- before the id
        int id = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 4482 INY / 4483 TYX / 4484
        r[0x1101 + idx] = (uint8_t)id;                      // 4488 LDX $6D / 448A -- the raw operand
        // Y is ip+1 at the fall-through; ctPcLoad does 4490's own INY, so the exit is ip+2.
        h.ip = ctPcLoad(r, rom, romLen, idx, id, (ip + 1) & 0xFFFF);
        h.carry = 1; h.ok = 1; break;                       // 4555 SEC
      }
      case 0x7F:
        CtVmRandOp7F(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x33:
        CtVmPalOp33(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x29: case 0x2A: case 0x2B: case 0x32: case 0x2C: case 0x2F:
        CtVmCaptionFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: {
        // The $C1 item and gold family; the body (CtVmItemFamily) is shared with the live VM
        // (disasm/notes/vm_two_implementations.md). Carry is set on every exit.
        int ni = ip;
        CtVmItemFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD7:
      case 0xD3: case 0xD4:
      case 0xF8: case 0xF9: case 0xFA:
        // The $C2 party family; the body (CtVmPartyGateFamily) is shared with the live VM and sets ip, carry and ok
        // itself (the carry varies).
        CtVmPartyGateFamily(r, rom, romLen, r + 0x12001, ip, &h);
        break;
      case 0x4E: {
        // C0/6846: copy an inline script blob to WRAM. Two arms differing in one byte (6872 MVN $7F,$7F vs 6897 MVN
        // $7E,$7F), chosen by a destination-bank operand tested CMP #7E: only the literal $7E takes the second.
        // Layout: `4E <dst16> <dstbank> <len16> <data...>`. The data is inline, so the returned IP is ip+6+count,
        // which the ROM recovers as TXA / SEC / SBC #2001 after the MVN.
        // The count is len - 3 (three 16-bit DEC A at 685F-6864) and MVN moves count+1, so len-2 bytes; a len below 3
        // wraps and moves most of a bank, as in the ROM.
        int dst = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 684A
        int bank = r[0x12001 + ((ip + 3) & 0xFFFF)];                  // 6853 / 6857 CMP #7E
        int len4E = r[0x12001 + ((ip + 4) & 0xFFFF)] | (r[0x12001 + ((ip + 5) & 0xFFFF)] << 8); // 685E
        int cnt = (len4E - 3) & 0xFFFF;                               // 6862-6864 DEC A x3, 16-bit
        r[0x01C1] = (uint8_t)cnt; r[0x01C2] = (uint8_t)(cnt >> 8);    // 6865 STA $C1 (16-bit)
        int src = (0x2001 + ip + 6) & 0xFFFF;                         // 6869 TXA / CLC / ADC #2001
        int dbase = (bank == 0x7E) ? 0x00000 : 0x10000;               // the only difference: MVN's
        for (int k = 0; k <= cnt; k++)                                //   destination bank byte
          r[dbase + ((dst + k) & 0xFFFF)] = r[0x10000 + ((src + k) & 0xFFFF)];
        h.ip = (ip + 6 + cnt + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 6876-687B / 687E SEC
      }
      case 0xC8: {
        // C0/3757: request / ack / park, the shape of op 0x04's call-and-wait, all in bank $C0:
        //   $0117 bit 6 set  -- a request of ours is outstanding: park on the op.
        //   $0118 bit 6 set  -- the service answered: clear it and step over the operand.
        //   neither          -- issue: publish the operand and latch $0117, then park.
        // The outstanding test comes first, so the service must clear $0117 before setting $0118 for the 3774 arm
        // to be reached.
        if (r[0x0117] & 0x40) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 375B / 377D TYX / CLC
        if (r[0x0118] & 0x40) {
          r[0x0118] = (uint8_t)(r[0x0118] & ~0x40);                   // 3774 LDA #40 / 3776 TRB $18
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 3778 TYX / INX / INX / SEC
        }
        r[0x0125] = r[0x12001 + ((ip + 1) & 0xFFFF)];                 // 3765 / 3769 STA $25
        r[0x0117] = (uint8_t)(r[0x0117] | 0x40);                      // 376D TSB $17
        r[0x0118] = (uint8_t)(r[0x0118] & ~0x40);                     // 376F TRB $18 -- A is still
        h.ip = ip; h.carry = 0; h.ok = 1; break;                      //   $40; TSB does not alter it
      }
      case 0xDD: case 0xDE: {
        // C0/3B1B (0xDD) and C0/3B45 (0xDE): stage a location change. 0xDE is 0xDD with LDA #01 / STA $1E in front and
        // a BRA into the same body. These are the returning members of the 0xDD-0xE1 family.
        // Not ported: 0xDF and 0xE1, which stage the same fields into $0112/$0114/$0116, spin on LDA $4210 / BPL, then
        // 3B91 LDX #06FF / TXS / BRL ->0000 (a stack reset that never returns); they have no case, so they trap.
        // Five bytes: a 16-bit destination, a 16-bit coordinate pair, and a field built from the first operand's high
        // byte in two passes.
        if (op == 0xDE) r[0x011E] = 0x01;                             // 3B45 LDA #01 / 3B47 STA $1E
        int w1 = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 3B1F
        r[0x01D9] = (uint8_t)w1; r[0x01DA] = (uint8_t)(w1 >> 8);      // 3B23 STA $D9 (16-bit)
        int loc = w1 & 0x01FF;                                        // 3B25 AND #01FF -- nine bits
        r[0x0100] = (uint8_t)loc; r[0x0101] = (uint8_t)(loc >> 8);    // 3B28 STA $00 (16-bit)
        int w2 = r[0x12001 + ((ip + 3) & 0xFFFF)] | (r[0x12001 + ((ip + 4) & 0xFFFF)] << 8);  // 3B2C
        r[0x0102] = (uint8_t)w2; r[0x0103] = (uint8_t)(w2 >> 8);      // 3B30 STA $02 (16-bit)
        int hb = r[0x01DA];                                           // 3B34 LDA $DA
        r[0x0104] = (uint8_t)((hb >> 1) & 0x0F);                      // 3B36 LSR / 3B37 AND #0F
        r[0x0104] = (uint8_t)(r[0x0104] | (hb & 0x80));               // 3B3F TSB $04
        h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break;       // 3B41 INX / INX / 3B43 SEC
      }
      case 0x30: case 0x31: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
      case 0x3B: case 0x3C: case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
      case 0x47:
        CtVmPadBranchFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x83: {
        // C0/46DF: load a field enemy / big sprite. Body C0/46DF..4866, three bytes, one RTS (47FD), carry always set.
        // Its first half is the object loader: C0/4557 is shared (JSR at 46E1 with A = 5), and C0/46E4-476A is
        // C0/4595-461B with the stride and five table offsets changed. The tail differs: no ctLoaderPartySlot and no
        // INC $73. The only out-of-bank transfer is 4825 JSL $C3:0002, the shared LZ.
        r[0x1100 + idx] = 0x05;                                  // 4559 -- state 5 = enemy
        int ip831 = (ip + 1) & 0xFFFF, ip832 = (ip + 2) & 0xFFFF;
        int id83 = r[0x12001 + ip831];                           // 455E
        r[0x1101 + idx] = (uint8_t)id83;                         // 4564
        r[0x1280 + idx] = r[0x0171]; r[0x1281 + idx] = r[0x0172]; // 456C (16-bit) -- staging address
        r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 4571
        r[0x1200 + idx] = 0x7F;                                  // 4579 -- the graphics bank
        r[0x1600 + idx] = 0x01;                                  // 457E
        r[0x1680 + idx] = 0x00; r[0x1681 + idx] = 0x00;          // 4583 / 4588
        r[0x01C7] = (uint8_t)ip832; r[0x01C8] = (uint8_t)(ip832 >> 8);  // 46E9 STY $C7, 16-bit --
        r[0x1B01 + idx] = 0x01;                                  // 46EF    the only surviving IP
        int rec83 = ctLoaderWalk(r, rom, romLen, idx, id83, 0x24F600, 10);   // 46E4-476A
        int e383 = (romLen > rec83) ? MR(rec83) : 0;            // 476F $E4F600,X
        r[0x01E3] = (uint8_t)e383;                               // 4773 STA $E3
        // The three graphics arms. 4777 LDA $1101,X / CMP #F8 / BCC; the walk above has already run for every id.
        if (id83 >= 0xF8) {
          // 4845 ROM-direct: the sheet is read straight out of a ROM bank, not staged in $7F, so 4861 overwrites the
          // prologue's $1200,X = $7F. 8 of 1471 sites.
          r[0x01D9] = (uint8_t)e383; r[0x01DA] = 0x00;           // 484C (16-bit, high byte zeroed)
          int a83 = ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3, 1);          // 4858
          r[0x1280 + idx] = (uint8_t)a83; r[0x1281 + idx] = (uint8_t)(a83 >> 8);
          r[0x1200 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3 + 2, 0);  // 4861
        } else {
          int k83; for (k83 = 0; k83 < 16; k83++) if (r[0x0920 + k83] == (uint8_t)e383) break;
          if (k83 < 16) {
            // 4781 dedup hit: share the owner's staging address. C0/5C90 writes nothing on a hit, and unlike op
            // 0x82's hit arm (4639-4663) there is no party-slot store.
            int own83 = r[0x0930 + k83];                         // 5CC2 LDA $0930,X
            r[0x1280 + idx] = r[0x1280 + own83];                 // 4794 (16-bit)
            r[0x1281 + idx] = r[0x1281 + own83];
          } else {
            // 47FE dedup miss: claim a slot, decompress, advance the cursor.
            for (k83 = 0; k83 < 16; k83++) if (r[0x0920 + k83] == 0xFF) break;   // 5CA5 CMP #FF
            if (k83 >= 16) { h.ip = ip; h.carry = 0; h.ok = 0; break; }  // 5CB0 LDX #4010 / BRL
            r[0x0920 + k83] = (uint8_t)e383;                     // 5CB6-5CBD    C0/2E1E: SEI, force
            r[0x0930 + k83] = (uint8_t)idx;                      //   blank, then `2E65 BRA ->2E65`
            r[0x01D9] = (uint8_t)e383; r[0x01DA] = 0x00;         // 4805 -- the second $D9 spill
            int sLo83 = ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3, 1);       // 480F
            int sBk83 = ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3 + 2, 0);   // 481D
            int cur83 = r[0x0171] | (r[0x0172] << 8);
            r[0x0300] = (uint8_t)sLo83; r[0x0301] = (uint8_t)(sLo83 >> 8);
            r[0x0302] = (uint8_t)sBk83;
            r[0x0303] = (uint8_t)cur83; r[0x0304] = (uint8_t)(cur83 >> 8);    // 4814
            r[0x0305] = 0x7F;                                                 // 4822
            if (sBk83 >= 0xC0) {
              static uint8_t scr83[0x10000];
              int src83 = ((sBk83 - 0xC0) << 16) | (sLo83 & 0xFFFF);
              if (src83 + 2 <= romLen) {
                int sz83 = (int)ct_lz_decompress_bounded(rom, (size_t)romLen, scr83,
                                                         sizeof scr83, (size_t)src83);
                r[0x0306] = (uint8_t)sz83; r[0x0307] = (uint8_t)(sz83 >> 8);  // 4825 JSL $C3:0002
                for (int q = 0; q < sz83 && cur83 + q < 0x10000; q++)
                  r[0x10000 + cur83 + q] = scr83[q];
                cur83 = (cur83 + sz83) & 0xFFFF;                              // 4831 $71 += $0306
                r[0x0171] = (uint8_t)cur83; r[0x0172] = (uint8_t)(cur83 >> 8);
              }
            }
            // 4833 CMP #8000 / BPL (16-bit): the halt fires when the cursor is below $8000, i.e. the staging heap
            // wrapped past $FFFF.
            if (cur83 < 0x8000) { h.ip = ip; h.carry = 0; h.ok = 0; break; }  // 4838 / BRL C0/2E1E
          }
        }
        // Common tail, C0/4799-47FD; all three arms converge here.
        int op283 = r[0x12001 + ip832];                          // 479B, read back via the $C7 spill
        r[0x01E1] = (uint8_t)op283;                              // 479F
        int n83 = ((op283 & 0x7F) + 1) & 0xFF;                   // 47A1/47A3 -- INC A is 8-bit
        // No allocation here: 47A9 claims $0B88+n unconditionally (`E2 10 AA A5 6D 9D 88 0B`: no scan, no test, no
        // branch).
        r[0x0B88 + n83] = (uint8_t)idx;                          // 47A9 STA $0B88,X
        int v83 = ((n83 & 0xFE) << 4) & 0xFFFF;                  // 47AF-47B5 AND #00FE / ASL x4
        r[0x01D9] = (uint8_t)v83; r[0x01DA] = (uint8_t)(v83 >> 8);            // 47B6 STA $D9, 16-bit
        int dv83 = (v83 + ((n83 & 1) << 3)) & 0xFFFF;            // 47B9-47C0 AND #0001 / ASL x3 / ADC
        // 47C2 STX $D9 runs under SEP #10, so only $01D9 is written; $01DA keeps the high half of the store above.
        r[0x01D9] = (uint8_t)n83;                                             // 47C2 STX $D9, 8-bit
        r[0x0D00 + idx] = (uint8_t)dv83; r[0x0D01 + idx] = (uint8_t)(dv83 >> 8);   // 47C6, 16-bit
        int tv83 = (n83 << 5) & 0xFFFF;                          // 47CF AND #00FF (clears B) / 47D2 ASL x5
        r[0x1700 + idx] = (uint8_t)tv83;
        r[0x1701 + idx] = (uint8_t)(tv83 >> 8);
        if ((r[0x1201 + idx] & 0x03) == 0x03) {                  // 47DC-47E3
          // 47E5: the big-sprite VRAM upload arm: STX $AE (8-bit X, $01AF untouched), then JSR C0/E12A.
          // Not ported: C0/E12A (decompresses 96 tiles to $7F:3800-43FF, DMAs $0C00 bytes to VRAM word $0400, lays a
          // 24x8 OAM template at $7F:4BC2), so this traps. Reached by 10 of the 1471 sites (record byte+4 = $23).
          h.ip = ip; h.carry = 0; h.ok = 0; break;
        }
        if (op283 & 0x80) r[0x1100 + idx] = 0x06;                // 47EC-47F4 -- 73 of 1471 sites
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;  // 47F9 LDX $C7 / INX / 47FC SEC
      }
      case 0xE8: case 0xEA: case 0xEB: case 0xEC: {
        // The sound-mailbox family: C0/3C11, C0/3C2F, C0/3C4C, C0/3C98. Each stages bytes into the $1E00 mailbox and
        // calls JSL $C7:0004; all exit CLC (yield). The handlers (staging, $01C7 spill, length, carry) are ported; the
        // driver is not modelled here (see ctVmSoundPost): with $1E05 bit 7 clear its only WRAM effect is $1E00 = 0.
        // The command byte ($18, $10, $81/$82/$83, 0xEC's script byte) is wiped by that STZ $00 before the handler
        // returns, so no WRAM sweep can check it; src/ct_vm.c's live path exercises it (--vmdiff, --audiotest).
        // $1E03 is sticky: only 0xEB writes it, and $C7:061C sends whatever the previous command left there.
        {
          int a1 = r[0x12001 + ((ip + 1) & 0xFFFF)];
          int a2 = r[0x12001 + ((ip + 2) & 0xFFFF)];
          int a3 = r[0x12001 + ((ip + 3) & 0xFFFF)];
          int spill, len;
          if (op == 0xE8) {                             // C0/3C11 -- play SFX
            spill = ip + 1; len = 2;                    // 3C13 STY $C7 -- Y, not X
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            r[0x1E01] = (uint8_t)a1;                    // 3C19
            r[0x1E02] = 0x80;                           // 3C1E
            r[0x1E00] = 0x18;                           // 3C23
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          } else if (op == 0xEA) {                      // C0/3C2F -- play song
            spill = ip + 1; len = 2;                    // 3C41 STX $C7
            r[0x29AE] = (uint8_t)a1;                    // 3C35 STA $7E29AE -- the shadow copy, and
            r[0x1E01] = (uint8_t)a1;                    //   it is written even when the driver goes
            r[0x1E00] = 0x10;                           //   on to reject the id ($C7:0AE9 = $53)
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          } else if (op == 0xEB) {                      // C0/3C4C -- three round-trips back to back
            spill = ip + 2; len = 3;                    // 3C78 STX $C7, spilled once before the 1st
            r[0x1E01] = (uint8_t)a1; r[0x1E02] = (uint8_t)a2;
            r[0x1E03] = 0xFF; r[0x1E00] = 0x81;         // the only writer of $1E03 in the family
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
            r[0x1E01] = 0x00; r[0x1E02] = 0xFF; r[0x1E00] = 0x82;
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
            r[0x1E01] = 0x00; r[0x1E02] = 0xFF; r[0x1E00] = 0x83;
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          } else {                                      // 0xEC, C0/3C98 -- the raw escape
            // Four bytes and no trailing INX: three INX run before 3CB2 STX $C7, so the spill already holds ip+4
            // and 3CB6 reloads it unchanged.
            spill = ip + 4; len = 4;
            r[0x1E00] = (uint8_t)a1;                    // the command itself is script data
            r[0x1E01] = (uint8_t)a2; r[0x1E02] = (uint8_t)a3;   // $1E03 untouched
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          }
          h.ip = (ip + len) & 0xFFFF; h.carry = 0; h.ok = 1; break;
        }
      }
      case 0x71: case 0x72: case 0x73: case 0x75: case 0x76: case 0x77: {
        // C0/6C69..6CF0 -- the local INC/DEC/SET; the body is CtVmLocalIncFamily (the live VM runs it too). SEC.
        int ni = ip;
        CtVmLocalIncFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xB1:                                           // STAY (C0/353F): IP+1, yield
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0xB2:                                           // EVERSTAY (C0/3543): yield in place (IP unchanged)
        h.ip = ip; h.carry = 0; h.ok = 1; break;
      case 0xB5: case 0xB6: {
        // C0/3546 and C0/354F: perpetual wrappers around op 0x94 / op 0x95:
        //   3546 STY $E3 / JSR C0/5215 / LDX $E3 / CLC / RTS
        //   354F STY $E3 / JSR C0/5410 / LDX $E3 /       RTS
        // STY $E3 is 16-bit ($01E3 and $01E4). LDX $E3 discards the inner handler's IP (hence perpetual), but its
        // writes stand. 0xB5 has a CLC before its RTS and always yields; 0xB6 passes the inner carry through.
        r[0x01E3] = (uint8_t)ip; r[0x01E4] = (uint8_t)(ip >> 8);      // 3546 / 354F STY $E3
        ctVmMoveActorOp(r, rom, idx, ip, op == 0xB6, 1, 0, 2, &h);    // 3548 / 3551 JSR
        h.ip = ip;                                                    // 354B / 3554 LDX $E3
        if (op == 0xB5) h.carry = 0;                                  // 354D CLC -- 0xB6 has none
        h.ok = 1; break;
      }
      case 0xAF:                                           // C0/2FFD = `JSR C0/3002 / INX / RTS`
      case 0xB0: {                                         // C0/3002 directly -- one shared body.
        // The IP pairing mirrors B1/B2: 3002 returns with the IP unchanged (0xB0 is the perpetual player-control op
        // a PC's script sits on), and 2FFD's INX advances it by one.
        //   3004  $0138 non-zero -> CLC, nothing applied
        //   3012  $1100,X == 0 -> 3040: arm $1A01 = 1, then JSR C0/9E29
        //   3015  == 1 -> C0/A26B ; 3018 == 2 -> C0/A2CE. Not ported: both trap (arming the ticket without the
        //         handler would move the object with a stale velocity).
        //   301A  >= 3 -> nothing
        int st = r[0x1100 + idx];
        if (r[0x0138] == 0) {
          if (st == 0) {
            r[0x1A01 + idx] = 1;                           // 3040
            // C0/9E29 starts with JSR C0/9E84, the facing selector (the player's only writer of $1600), before the
            // velocity copy. romLen = 0x10000: only bank $C0 is read ($9FF7's table and $FE00's blink table).
            CtPlayerAnimSelect(r, rom, 0x10000, idx);      // 9E29
            ct9E29Apply(r, idx);                           // 9E2C onward
            // 304A-3054: C0/5B8D's carry is $01EB bit 7 clear; then C0/3154 preempts the touched object's script.
            if (!(r[0x01EB] & 0x80)) CtVmTouchRedirect(r);
          }
          else if (st == 1 || st == 2) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        }
        h.ip = (op == 0xAF) ? ((ip + 1) & 0xFFFF) : ip;    // 2FFD's INX vs 3002's bare RTS
        h.carry = 0; h.ok = 1; break;                      // both exits are CLC = yield
      }
      default:                                             // not ported -> trap
        h.ip = ip; h.carry = 0; h.ok = 0; break;
    }
  *out = h;
  return h.ok ? -1 : op;
}

int g_ctVmTraceIdx = -1, g_ctVmTraceFrame = -1;

// The SPC's output mailbox port 3, supplied by the host (see op 0xEE, C0/3CC5). Default 0 = low nibble clear, the arm
// that lets the script advance, so a host that never sets it does not wedge the script.
uint8_t g_ctApuPort3 = 0;
uint8_t g_ctApuOut[4] = { 0, 0, 0, 0 };                     // ops 0x48 / 0x49 on $2140-$2143; see move.h
uint8_t g_ctApuPort1 = 0;                                   // op 0xED (C0/3CBD); see move.h

int CtFieldRunner(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // C0/5A93 LDA $68 / STA $67, with DP = $0100: the reload is $0168 and the live counter $0167. A budget of 0 wraps
  // to $FF at the first DEC $67, i.e. 256 ops.
  int budget = r[0x0168];                                 // 5A93 LDA $68
  r[0x0167] = (uint8_t)budget;                            // 5A95 STA $67 -- the counter is in memory
  int ip = r[0x1180 + idx] | (r[0x1181 + idx] << 8);      // 16-bit script IP
  // The $6D latch (5A9C) is written by CtFieldRunnerOne.
  int trapped = -1;
  for (;;) {
    CtHRet h;
    if (idx == g_ctVmTraceIdx)
      printf("  PDISP  f%-3d op %02X  budget=%02X ip=%04X\n", g_ctVmTraceFrame, r[0x12001 + ip], budget, ip);
    int op = CtFieldRunnerOne(r, rom, romLen, idx, ip, &h);
    if (!h.ok) { trapped = op; ip = h.ip; break; }
    if (!h.carry) { ip = h.ip; break; }                   // yield -> end the tick (5AB4 BCC ->5ABA)
    ip = h.ip;                                             // continue -> next opcode
    budget = (budget - 1) & 0xFF;                          // 5AB6 DEC $67 -- 8-bit (SEP #20 at 5AAF)
    r[0x0167] = (uint8_t)budget;
    if (budget == 0) break;                                // 5AB8 BNE ->5AA3
  }
  r[0x1180 + idx] = (uint8_t)ip; r[0x1181 + idx] = (uint8_t)(ip >> 8);   // write the IP back
  return trapped;
}

// ---- C0/E952 - C0/EA62: the four-slot sprite-assembly arena; C0/CAD9: the drain's per-object visit ----
// disasm/notes/anim_drain_cad9.md. $0BC0..$0BC3 is a byte owner table: bit 7 set = free (every release writes $80),
// else the owner's $6D (obj*2, at most $7E). A grant writes $3800 + slot*$200 into $0D80,X (a word; an offset into
// the $7F staging bank, 512 bytes = 16 4bpp tiles per slot).
// The pool is not frame-scoped: C0/CA76 skips the pass while the VRAM upload queue is non-empty (CA76 LDA $09A0 /
// BEQ / RTS), and its raster budget can cut between a class-1/2 object's passes, so a partly staged object can keep
// its slots into the next frame (C0/B701 then returns SEC without freeing).

int CtArenaAlloc(uint8_t* r, int idx, int n) {
  // C0/E952 (n=1) / C0/E97A (n=2) / C0/E9AA (n=3). The scan bound is 4/3/2 (4-(n-1)), and a multi-slot claim needs
  // consecutive slots (E984 / E9B4 / E9B9 each BPL back to the INX on the first occupied follower).
  for (int k = 0; k + n <= 4; k++) {                      // E974 CPX #04 / E9A4 #03 / E9DC #02
    int free = 1;
    for (int j = 0; j < n; j++) if (!(r[0x0BC0 + k + j] & 0x80)) { free = 0; break; }  // BPL = occupied
    if (!free) continue;
    for (int j = 0; j < n; j++) r[0x0BC0 + k + j] = (uint8_t)idx;   // owner := $6D
    int a = 0x3800 + k * 0x200;                           // E95E-E967 TXA/XBA/AND #FF00/ASL/ADC #3800
    r[0x0D80 + idx] = (uint8_t)a;                         // E96A LDX $6D / E96C STA $0D80,X (16-bit)
    r[0x0D81 + idx] = (uint8_t)(a >> 8);
    return 1;                                             // E971 / E9A1 / E9D9 SEC = granted
  }
  return 0;                                               // E978 / E9A8 / E9E0 CLC = pool full
}

void CtArenaFree(uint8_t* r, int idx, int n) {
  // C0/E9E2 (n=1) / C0/E9FF (n=2) / C0/EA1F (n=3), called from C0/B701 once the object's staged-pass
  // count $1B00,X reaches the class arity. Note: once it finds k the ROM frees slots k..k+n-1 without checking that
  // the followers are owned by $6D.
  int bound = 4 - (n - 1);                                // E9F0 #04 / EA0D #03 / EA2D #02
  for (int k = 0; k < bound; k++) {
    if (r[0x0BC0 + k] != (uint8_t)idx) continue;          // E9EB / EA08 / EA28 CMP $6D
    for (int j = 0; j < n; j++) r[0x0BC0 + k + j] = 0x80; // E9F9 / EA16-EA19 / EA36-EA3C
    return;
  }
  // not found -> silent no-op (E9F4 / EA11 / EA31)
}

void CtArenaFreeRun(uint8_t* r, int idx) {
  // C0/EA42: the class-agnostic release: free every consecutive slot owned by $6D (1 to 4). Used by the camera-window
  // falling edge (C0/A918) and the scene-entry sweep (C0/B117); not the same as CtArenaFree(r, idx, 1), which would
  // leak a class-2 object's other two slots. EA42 has no SEP/REP; X is 8-bit on entry.
  int k = 0;
  for (; k < 4; k++) if (r[0x0BC0 + k] == (uint8_t)idx) break;   // EA44-EA4E
  if (k == 4) return;                                            // EA50 RTS
  for (;;) {
    r[0x0BC0 + k] = 0x80;                                        // EA51-EA53
    if (++k == 4) return;                                        // EA56-EA59
    if (r[0x0BC0 + k] != (uint8_t)idx) return;                   // EA5B-EA60
  }
}

// A 24-bit read as the handlers' `LDA [$D3],Y` sees it. HiROM: banks $C0-$FF are ROM at file offset (bank-$C0)<<16;
// $7E/$7F are WRAM; banks $00-$3F (and $80-$BF) expose $7E:0000-1FFF below $2000. Anything else is not modelled and
// returns 0 (the live sprite tables use banks $D2/$D3/$D4).
static int romRead24(const uint8_t* r, const uint8_t* rom, int romLen, int bank, int addr) {
  bank &= 0xFF; addr &= 0xFFFF;
  if (bank >= 0xC0) { int off = ((bank - 0xC0) << 16) | addr; return off < romLen ? MR(off) : 0; }
  if (bank == 0x7E || bank == 0x7F) return r[((bank - 0x7E) << 16) | addr];
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && addr < 0x2000) return r[addr];
  return 0;
}

// The same read as a flat 24-bit address, for the two copiers' `[$CD],Y` / `$D20000,X` forms, both
// of which can carry out of their base bank.
static int romRead24Flat(const uint8_t* r, const uint8_t* rom, int romLen, int a24) {
  return romRead24(r, rom, romLen, (a24 >> 16) & 0xFF, a24 & 0xFFFF);
}

// C0/CC3F-CC76: the pixel copy and its two copiers (disasm/notes/anim_drain_cad9.md sec. 11). Each tile is a word of
// the frame record: bits 0-10 the tile index (source $CF:($CD + index*32)), bit 14 the copier; the rest is masked
// off (AND #07FF).
// C0/E534 (bit 14 set) pushes 32 bytes through the auto-incrementing $2180 port, every byte through the bit-reversal
// table $C0:FD00: the horizontal flip, baked into VRAM. C0/E687 (bit 14 clear) stores directly with DB = $7F and
// Y = $D0, then resyncs the port (E72E-E735); the caller adds $20 to $D0 per tile (CC55), so both land tile t at
// $7F:($0D80,X + t*$20).
// E687 reads its own bank only for $7F (E8C2), $D2 (E7BC), $D3 (E83F) and $D4 (E739); any other $CF falls through to
// C0/E6B6, which reads bank $D5. E534 honours $CF for every bank.
// CtBlitSeg: one staging run of `n` tiles from tile-word offset `wOff`, written at the arena base + `dOff`. `wIdx` =
// the run reads $0D80,X with the word $6D/$6E: every two-segment leg does REP #30 between its runs (D58C, D68B, D738,
// DA69), so the second run is word-indexed:
//   byte: CC1B CC94 CF3B CFB4 D2A7 D30F D38C D563 D625 D68F D73C D7E7 D864 DA6B DB23
//   word: D5C4 D6F4 D7A1 DACE
typedef struct { int dOff; int wOff; int n; int wIdx; } CtBlitSeg;

static void ctAnimBlit(uint8_t* r, const uint8_t* rom, int romLen, int idx,
                       int rec, int recBank, const CtBlitSeg* seg) {
  int gfxBank = r[0x1200 + idx];                            // $CF
  int gfxAddr = r[0x1280 + idx] | (r[0x1281 + idx] << 8);   // $CD/$CE
  int bi = seg->wIdx ? (r[0x016D] | (r[0x016E] << 8)) : idx;                    // see CtBlitSeg
  int dst = ((r[0x0D80 + bi] | (r[0x0D81 + bi] << 8)) + seg->dOff) & 0xFFFF;    // $D0 (CC1B / D5C4..)
  for (int t = 0; t < seg->n; t++) {
    int wo = seg->wOff + t * 2;
    int w = romRead24(r, rom, romLen, recBank, rec + wo)
          | (romRead24(r, rom, romLen, recBank, rec + wo + 1) << 8);      // CC5D LDA [$D3],Y
    int off = (w & 0x07FF) << 5;
    int d = (dst + t * 0x20) & 0xFFFF;
    if (w & 0x4000) {                                       // CC5F BIT #4000 -> C0/E534
      // `LDA [$CD],Y` is a 24-bit add, so it can carry out of $CF into the next bank.
      int base = ((gfxBank & 0xFF) << 16) | gfxAddr;
      for (int i = 0; i < 32; i++)
        r[0x10000 + ((d + i) & 0xFFFF)] =
            MR(0xFD00 + romRead24Flat(r, rom, romLen, base + off + i));   // E546 LDA $FD00,X
    } else {                                                // C0/E687
      int sb = (gfxBank == 0x7F || gfxBank == 0xD2 || gfxBank == 0xD3 || gfxBank == 0xD4)
             ? gfxBank : 0xD5;                              // E699-E6B3, else E6B6's hardcoded $D5
      int x = (gfxAddr + off) & 0xFFFF;                     // E691 CLC / ADC $CD -- a 16-bit add
      for (int i = 0; i < 32; i++)
        r[0x10000 + ((d + i) & 0xFFFF)] =
            (uint8_t)romRead24Flat(r, rom, romLen, ((sb & 0xFF) << 16) + x + i);
    }
  }
}

// C0/CC17-CDBE, the tile half, shared by the three classes (piece count, record stride and transfer length differ):
//   CC17-CC3B  the frame record pointer (ctAnimRec)
//   CC3F-CC76  the 16-tile pixel copy (ctAnimBlit)
//   CC78-CCB4  the VRAM upload queue append (ctAnimAppend)
//   CCB6-CDBE  the OBJ template at $7F4802 + $1700,X (ctAnimTemplate)
// ctAnimRec: $D3/$D4 = $1380,X + frame*stride through the hardware multiplier ($4202 = frame, $4203 = stride), bank
// $1300,X; the stride is $28 / $50 / $78 by class.
static int ctAnimRec(const uint8_t* r, int idx, int stride) {
  return ((r[0x1380 + idx] | (r[0x1381 + idx] << 8)) + r[0x0F01 + idx] * stride) & 0xFFFF;
}

// CC78-CCB4: the upload-queue append, once per sprite by the pass that returns CLC (STA $0950,X appears only at CCA8,
// CFA8, D380, D858 and DB17). Seven 16-bit tables ($0940 srcA, $0950 dstA, $0960 srcB, $0970 dstB, $0980 lenA, $0990
// lenB, $09A0 used), cursor $79 by 2, a 16-bit STZ ending the next slot. INC $09A0,X and that STZ are 16-bit, but
// C0/CA76 reads $09A0 8-bit, so $09A1 collects stale values.
// The B offsets differ per class: class 0 +$100/+$100, class 1 +$100/+$200, class 2 +$300/+$500 (non-$68) or
// +$100/+$100 ($68), with the lengths $500/$100 swapped between the two.
static void ctAnimAppend(uint8_t* r, int idx, int dstBOff, int srcBOff, int lenA, int lenB) {
  int dstA = ((r[0x0D00 + idx] | (r[0x0D01 + idx] << 8)) & 0x01FF) << 4;   // CC7C-CC85
  int srcA = r[0x0D80 + idx] | (r[0x0D81 + idx] << 8);                     // CC94, the arena base
  int q = r[0x0179] & 0xFF;                                               // $79, the 2-byte cursor
  #define CT_QW(tbl, v) do { r[(tbl) + q] = (uint8_t)(v); r[(tbl) + q + 1] = (uint8_t)((v) >> 8); } while (0)
  CT_QW(0x0950, dstA);                                                    // CC88
  CT_QW(0x0970, (dstA + dstBOff) & 0xFFFF);                               // CC8C-CC8F
  CT_QW(0x0940, srcA);                                                    // CC99
  CT_QW(0x0960, (srcA + srcBOff) & 0xFFFF);                               // CC9D-CCA0
  CT_QW(0x0980, lenA);                                                    // CCA3-CCA6
  CT_QW(0x0990, lenB);                                                    // CCA9
  int used = ((r[0x09A0 + q] | (r[0x09A1 + q] << 8)) + 1) & 0xFFFF;        // CCAC INC, 16-bit
  r[0x09A0 + q] = (uint8_t)used; r[0x09A1 + q] = (uint8_t)(used >> 8);
  q = (q + 2) & 0xFF;                                                     // CCAF/CCB0 INX INX
  r[0x09A0 + q] = 0; r[0x09A1 + q] = 0;                                   // CCB1 STZ, also 16-bit
  r[0x0179] = (uint8_t)q;                                                 // CCB4 STX $79
  #undef CT_QW
}

// CCB6-CDBE: the OBJ template at $7F4802 + $1700,X, once per sprite. `pieces` 8-byte entries, five bytes written:
// +2/+3 the record's X displacement sign-extended, +4 its Y displacement, +6 ($0D00,Y & $FF) + e*2, +7 the attribute;
// +0/+1 (the on-screen X) and +5 are left alone. `recOff` = past the tile words: $20 / $40 / $60 by class.
// The priority rule differs: class 0 tests each piece's +4 against #E8 (CD4F/CD6B/...); classes 1 and 2 have no
// CMP #E8 and split by piece index, pieces 0-3 taking $0C00,Y and the rest $0C01,Y (D4C6/D4DB; DA23/DA38/DA4D).
static void ctAnimTemplate(uint8_t* r, const uint8_t* rom, int romLen, int idx,
                           int rec, int recBank, int pieces, int recOff, int e8Test, int jumpAt) {
  int base = r[0x1700 + idx] | (r[0x1701 + idx] << 8);   // CCB8 8-bit LDX / CCBA 16-bit LDA / CCBF TAX
  int y16 = r[0x016D] | (r[0x016E] << 8);                // CD2C LDY $6D, Y 16-bit (REP #10 at CCBD)
  int tile = r[0x0D00 + y16] & 0xFF;                     // CD2E, an 8-bit load
  int attr = (r[0x0F81 + y16] | r[0x0D01 + y16]) & 0xFF; // CD47-CD4D -> $D9
  for (int e = 0; e < pieces; e++) {
    int p = (0x4802 + base + e * 8) & 0xFFFF;
    int xd = romRead24(r, rom, romLen, recBank, rec + recOff + e * 2);     // CCC5 LDA [$D3],Y
    int yd = romRead24(r, rom, romLen, recBank, rec + recOff + 1 + e * 2); // CCD8
    r[0x10000 + p]     = (uint8_t)xd;                                     // +2
    r[0x10000 + p + 1] = (uint8_t)((xd & 0x80) ? 0xFF : 0x00);            // +3, CCCB BPL sign-extend
    r[0x10000 + p + 2] = (uint8_t)yd;                                     // +4
    // +6: the tile advances by 2 per piece, plus one extra $10 in class 2 only, at a point that differs between its
    // geometries (DA02, non-$68: before piece 8; DCA9, $68: before piece 4). `jumpAt` < 0 means none.
    int tn = tile + e * 2 + ((jumpAt >= 0 && e >= jumpAt) ? 0x10 : 0);
    r[0x10000 + p + 4] = (uint8_t)(tn & 0xFF);                            // CD35 / DA00-DA03
    int pr = e8Test ? ((r[0x10000 + p + 2] >= 0xE8) ? r[0x0C01 + y16] : r[0x0C00 + y16])
                    : ((e < 4) ? r[0x0C00 + y16] : r[0x0C01 + y16]);
    r[0x10000 + p + 5] = (uint8_t)(attr | pr);                            // +7
  }
}

// C0/CBDC: class 0 (4 OBJ pieces): resolve, guard, allocate, commit, then the tile half (CC17-CDBE). Class 0 never
// returns SEC (its three RTS, CC03/CC09/CDC7, are all carry-clear), so the drain pops it after one visit; the
// resumable SEC protocol is classes 1 and 2's. `ct.exe --sweepcad9` checks it against the ROM.
static int ctAnimClass0(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // CBF2-CBFE. CtFieldFrameLookup already folds in the $1780,X == 3 -> $1301,X short-circuit, so it is not
  // repeated here.
  int fb = CtFieldFrameLookup(r, rom, romLen, idx);
  if (fb < 0) return 0;                                   // CC01 BCS not taken -> CC03 RTS, C clear
  if (fb == r[0x0F01 + idx]) return 0;                    // CC04 CMP / CC07 BNE -> CC09 CLC/RTS
  if (!CtArenaAlloc(r, idx, 1)) return 0;                 // CC0D JSR E952 / CC10 BCC -> CC09:
                                                          //   pool full -> the frame is dropped and
                                                          //   $0F01 keeps its old value
  r[0x0F01 + idx] = (uint8_t)fb;                          // CC12-CC14: the commit
  { static const CtBlitSeg SEG = { 0x000, 0x00, 16, 0 };  // CC17-CDBE, the whole tile half
    int rec = ctAnimRec(r, idx, 0x28), bk = r[0x1300 + idx];
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &SEG);
    ctAnimAppend(r, idx, 0x100, 0x100, 0x0100, 0x0100);
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 4, 0x20, 1, -1); }
  // CDBF LDX $6D runs with X 16-bit (REP #10 at CCBD; SEP #10 only at CDC4), so the INC uses the word $6D/$6E.
  int x16 = r[0x016D] | (r[0x016E] << 8);                 // DP = $0100
  r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);       // CDC1 INC $1B00,X (staged-pass count)
  return 0;                                               // CDC6 CLC / CDC7 RTS
}

// C0/CEF5: class 1 (8 OBJ pieces, a $50-byte record, two consecutive arena slots); anim_drain_cad9.md sec. 8.
// CF0B-CF1E dispatches three ways:
//   $1780,X == 3            -> CF21: one visit stages the whole two-slot run (32 tiles, CF6B LDA #0020), adds 2 to
//                             $1B00,X and returns CLC; it never reads $1B00,X.
//   ($1B00,X & $7F) == 0    -> D28A, pass 1: resolve, guard, allocate two slots (C0/E97A), commit $0F01,X, stage 16
//                             tiles, INC $1B00,X, return SEC (keep this object at the FIFO head).
//   otherwise               -> D30D, pass 2: no lookup or commit; stage the other 16 tiles at $0D80,X + $200, INC,
//                             CLC.
// The three INC $1B00,X differ in index width: D308 (pass 1) runs after D304 SEP #30 (byte $6D); D4F0 (pass 2) and
// D11A/D11D (mode 3) run with X 16-bit (word $6D/$6E).
static int ctAnimClass1(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  int mode3 = (r[0x1780 + idx] == 3);                     // CF0B-CF12 CMP #03
  int x16 = r[0x016D] | (r[0x016E] << 8);                 // DP = $0100
  int rec = ctAnimRec(r, idx, 0x50), bk = r[0x1300 + idx];   // class-1 record stride $50
  if (!mode3 && (r[0x1B00 + idx] & 0x7F) != 0) {          // CF14-CF1B -> D30D, pass 2
    static const CtBlitSeg S2 = { 0x200, 0x20, 16, 0 };   // D30F: base + $200, tile words 16..31
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S2);
    ctAnimAppend(r, idx, 0x100, 0x200, 0x0200, 0x0200);   // D380: dstB +$100 but srcB +$200
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 8, 0x40, 0, -1);   // D3B9 LDY #0040, 8 pieces
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D4F0, word-indexed
    return 0;                                             // D4F5 CLC / D4F6 RTS
  }
  // CF21 (mode 3) and D28A (pass 1) share the resolve/guard/allocate/commit. CtFieldFrameLookup
  // already folds the `$1780 == 3 -> $1301,X` case, which is exactly what CF21 reads directly.
  int fb = CtFieldFrameLookup(r, rom, romLen, idx);       // CF21 / D28A JSR C0/CB3A
  if (fb < 0) return 0;                                   // D28D BCS not taken -> D28F RTS
  if (fb == r[0x0F01 + idx]) return 0;                    // CF24-CF2A / D290-D296
  if (!CtArenaAlloc(r, idx, 2)) return 0;                 // CF2D / D299 JSR C0/E97A -- two slots;
                                                          //   CF30 / D29C BCC -> drop the frame
  r[0x0F01 + idx] = (uint8_t)fb;                          // CF32-CF34 / D29E-D2A0
  rec = ctAnimRec(r, idx, 0x50);   // CC22/D2A3: the multiplier reads $0F01 after the commit, so
                                   // the record must be re-resolved here. Pass 2 has no commit
                                   // and correctly keeps the value resolved at entry.
  if (mode3) {
    static const CtBlitSeg S = { 0x000, 0x00, 32, 0 };    // CF6B #0020: both slots in one visit
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S);
    ctAnimAppend(r, idx, 0x100, 0x200, 0x0200, 0x0200);   // CFA8
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 8, 0x40, 0, -1);   // CFE3 LDY #0040
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D11A  -- two increments in one visit,
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D11D     so B701 sees the arity at once
    return 0;                                             // D122 CLC / D123 RTS
  }
  { static const CtBlitSeg S1 = { 0x000, 0x00, 16, 0 };   // D2A7: base, tile words 0..15.
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S1); }      //   Pass 1 does not append or build.
  r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);       // D308, byte-indexed (D304 SEP #30)
  return 1;                                               // D30B SEC -- "call me again"
}

// C0/D4F7: class 2 (12 OBJ pieces, a $78-byte record, three consecutive arena slots); anim_drain_cad9.md sec. 9.
// D50D-D514 LDA $1780,X / CMP #03 / BNE / RTS: in anim mode 3 the CMP leaves carry set, so the object returns SEC
// having done nothing and C0/CA76 keeps calling it until the raster deadline (see CtAnimDrainPass).
// $1B00,X & $7F picks the pass (D515-D51E) and $0D00,X == $68 one of two staging geometries (D520/D52D/D539):
//     phase 0 -> D546 (non-$68) / D608 ($68)      SEC, D603 / D686
//     phase 1 -> D68B            / D738            SEC, D733 / D7E0
//     phase 2 -> D7E5            / DA69            CLC, DA62 / DD21
// The lookup, guard, allocation and commit live in phase 0 only; phases 1 and 2 only stage. Every SEC tail runs
// after SEP #30 and is byte-indexed; every CLC tail runs with X 16-bit and is word-indexed.
static int ctAnimClass2(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  if (r[0x1780 + idx] == 3) return 1;                     // D510-D514: SEC, and NO work done
  int n = r[0x1B00 + idx] & 0x7F;                         // D515-D518
  int x16 = r[0x016D] | (r[0x016E] << 8);
  // D520/D52D/D539: the low byte of $0D00,X picks one of two staging geometries (where each run lands and which tile
  // words it takes).
  int v68 = ((r[0x0D00 + idx] & 0xFF) == 0x68);
  int rec = ctAnimRec(r, idx, 0x78), bk = r[0x1300 + idx];   // class-2 record stride $78
  //                                     dOff  wOff  n
  static const CtBlitSeg P0A[2] = { { 0x000, 0x00, 8, 0 }, { 0x200, 0x10, 8, 1 } };  // D546 / D5C4 (word)
  static const CtBlitSeg P0B[1] = { { 0x000, 0x00, 16, 0 } };                        // D608
  static const CtBlitSeg P1A[2] = { { 0x100, 0x20, 8, 0 }, { 0x300, 0x30, 8, 1 } };  // D68F / D6F4 (word)
  static const CtBlitSeg P1B[2] = { { 0x200, 0x20, 8, 0 }, { 0x400, 0x30, 8, 1 } };  // D73C / D7A1 (word)
  static const CtBlitSeg P2A[1] = { { 0x400, 0x40, 16, 0 } };                        // D7E7
  static const CtBlitSeg P2B[2] = { { 0x300, 0x40, 8, 0 }, { 0x500, 0x50, 8, 1 } };  // DA6B / DACE (word)
  if (n == 0) {                                           // D51A BEQ -> D52D -> D546 / D608
    int fb = CtFieldFrameLookup(r, rom, romLen, idx);     // D546 / D608 JSR C0/CB3A
    if (fb < 0) return 0;                                 // D549 BCS not taken -> D54B RTS
    if (fb == r[0x0F01 + idx]) return 0;                  // D54C-D552 the equality guard
    if (!CtArenaAlloc(r, idx, 3)) return 0;               // D555 / D617 JSR C0/E9AA -- three slots
    r[0x0F01 + idx] = (uint8_t)fb;                        // D55A-D55C / D61C-D61E
    rec = ctAnimRec(r, idx, 0x78);                        // the commit moved $0F01, so re-resolve
    if (v68) ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0B[0]);
    else { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0A[0]);
           ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0A[1]); }
    r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);     // D603 / D686, byte-indexed
    return 1;                                             // SEC
  }
  if (n == 1) {                                           // D51C CMP #01 / D51E BEQ -> D539
    const CtBlitSeg* p1 = v68 ? P1B : P1A;
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &p1[0]);
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &p1[1]);
    r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);     // D733 / D7E0, byte-indexed
    return 1;                                             // SEC
  }
  if (v68) { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2B[0]);
             ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2B[1]);
             ctAnimAppend(r, idx, 0x100, 0x100, 0x0100, 0x0500); }   // DB07
  else     { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2A[0]);
             ctAnimAppend(r, idx, 0x300, 0x500, 0x0500, 0x0100); }   // D848 -- note the swap
  ctAnimTemplate(r, rom, romLen, idx, rec, bk, 12, 0x60, 0, v68 ? 4 : 8);  // D893/DB52 LDY #0060;
                                                          // DCA9 jumps at piece 4, DA02 at 8
  r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);       // DA62 / DD21, word-indexed
  return 0;                                               // CLC -- the object is finally popped
}

int CtAnimDrainVisit(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // C0/CAD9. Four gate tests, all returning CLC (CLC makes C0/CA76 pop the object; SEC would keep it at the head).
  if (r[0x1100 + idx] & 0x80) return 0;                   // CADE BPL: bit 7 set = destroyed/inactive
  int a = r[0x1A81 + idx];
  if (a == 0 || (a & 0x80)) return 0;                     // CAE5 BEQ / CAE7 BMI (any $01-$7F passes)
  if (r[0x0F00 + idx] == 0) return 0;                     // CAEC: outside the camera window
  switch (r[0x1201 + idx] & 3) {                          // CAEE-CAFD: the sprite's piece count
    case 0:  return ctAnimClass0(r, rom, romLen, idx);    // CAF3 BEQ / CB01 BRL C0/CBDC -- 4 pieces
    case 1:  return ctAnimClass1(r, rom, romLen, idx);    // CAF9 BRA / CB04 BRL C0/CEF5 -- 8 pieces
    case 2:  return ctAnimClass2(r, rom, romLen, idx);    // CAFD BEQ / CB07 BRL C0/D4F7 -- 12 pieces
    default: return 0;                                    // CAFF CLC/RTS -- class 3 is never drawn
                                                          // by this pass (C0/E12A handles it).
  }
}

// ---- C0/B701 (the release) and C0/CA76 (the FIFO drain) ----
// With the enqueue (C0/C98A) and the visit (C0/CAD9) they close the arena's lifecycle within one frame:
//   C0/00D3 JSR C0/A810   the movement loop -> C0/C98A per object = the enqueue
//   C0/00D6 JSR C0/CA76   the drain         -> C0/CAD9 per pop    = the grant (E952/E97A/E9AA)
//   C0/00DB BRL C0/B271   the draw pass     -> B2AF/B2BB JSR C0/B309 -> B309 JSR C0/B701 = the free
// C0/B701's three JSR E9E2 / E9FF / EA1F are the only callers of the 1/2/3-slot frees.

// C0/B788 (reposition) and C0/B8CA (instantiate): class 0's two OAM emitters; anim_drain_cad9.md sec. 13-14 has the
// full trace. The instantiate one also copies the staged record ($7F:4802 + $1700,X) into the live one ($7F:4BC0 +
// $1700,X), which is why C0/B701 may free the slot then. The emitters write +0/+1 (the on-screen X low byte and Y);
// the template wrote +2..+4, +6, +7; +5 is written by neither.
// B796 STA $C5 is 16-bit (REP #20 at B78D), so $C6 is the screen Y's bit 8, and B7D9/B836 classify the screen Y:
// path A Y >= $100 (B7DD / B922), path B $80..$FF (B83A / B9A5), path C <= $7F (B882 / B9F7).
// ctOamClamp: the Y clamp is a separate hand-written rule per class and path; `d` = the record's Y displacement,
// `sy` = the screen Y.
static int ctOamClamp(int cls, int mode, int isRep, int d, int sy) {
  int t = d + (sy & 0xFF), c = t > 0xFF, y = t & 0xFF;      // CLC / ADC $C5
  if (cls == 0) {
    if (mode == 0) return (c && y >= 0xE0) ? y : 0xE0;      // B7DD/B922: BCC->E0, >=E0 keep
    if (mode == 1) return (!c || y < 0xE0) ? y : 0xE0;      // B83A/B9A5: BCC keep, <E0 keep
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;       // B882/B9F7: BPL keep, >=E0 keep
  }
  if (cls == 1) {
    if (mode == 0) return (c && y >= 0xE0) ? y : 0xE0;      // BC50/BF1C -- same as class 0's A
    if (mode == 1) return (y < 0xE0) ? y : 0xE0;            // BB00/BD80 -- no carry test
    // Mode 2 differs between the emitters: only the reposition one pre-tests the displacement's sign (BB7F BMI
    // ->BB88), so a non-negative d that does not carry is kept (BB84 BCC ->BB93). BE46 has no pre-test.
    if (isRep && !(d & 0x80) && !c) return y;
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;       // BB8B / BE49
  }
  if (cls == 3) {   // C0/C73A's three paths, each read at its own entry (they match class 2's)
    if (mode == 0) return (!c && (y & 0x80) == 0) ? y : 0xE0; // C826 BCS ->#E0 / C828 BPL keep
    if (mode == 1) return y;                                  // C7E2-C7E8: no clamp
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;         // C805 BPL keep / C807 CMP #E0 BCS keep
  }
  if (mode == 0) return (!c && (y & 0x80) == 0) ? y : 0xE0; // C209/C5C1: BCS->E0, BPL keep
  if (mode == 1) return y;                                  // C0CD/C3A5: no clamp
  return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;         // C13B/C483
}

static void ctOamEmit(uint8_t* r, int cls, int pieces, int instantiate) {
  int w = r[0x016D] | (r[0x016E] << 8);                       // $6D/$6E, X 16-bit throughout
  int sy = (r[0x0A80 + w] | (r[0x0A81 + w] << 8)) & 0x01FF;   // B78F $C5 = screen Y, 9 bits
  int sx = r[0x0A00 + w] | (r[0x0A01 + w] << 8);              // B798 $C3 = screen X
  int base = r[0x1700 + w] | (r[0x1701 + w] << 8);            // B7A0 $D9 = the record base
  // These three are stores into the direct page (D = $0100), all 16-bit (REP #20 from B78D). $01D9 is the byte a
  // frame ends on (the last emit's record base); $01C6 is the mode the Y half reads back as $C6 != 0.
  r[0x01C5] = (uint8_t)sy;   r[0x01C6] = (uint8_t)(sy >> 8);  // B796 STA $C5
  r[0x01C3] = (uint8_t)sx;   r[0x01C4] = (uint8_t)(sx >> 8);  // B79C STA $C3
  r[0x01E5] = 0;             r[0x01E6] = 0;                   // B79E STZ $E5 (16-bit: clears the pair)
  r[0x01D9] = (uint8_t)base; r[0x01DA] = (uint8_t)(base >> 8);// B7A4 STA $D9
  #define CT_LIVE(o)  r[0x10000 + ((0x4BC0 + base + (o)) & 0xFFFF)]
  #define CT_STAGE(o) r[0x10000 + ((0x4802 + base + (o)) & 0xFFFF)]
  // The X half runs in groups of four, each walking its pieces backwards and ending in its own ORA #$AA and
  // high-table store. The destinations are not contiguous: class 2's third byte goes to $4B40 (C0BA / C310 family).
  static const int HIDST[3][3] = { { 0x4F00, 0, 0 }, { 0x4F00, 0x4F01, 0 },
                                   { 0x4F00, 0x4F01, 0x4B40 } };
  for (int g = 0; g * 4 < pieces; g++) {
  // The accumulator is in the direct page: B7B9 STA $E6 (this piece's X bit 8), B7BB-B7BF A = ($E5 << 2) | $E6,
  // and B7C5 STA $E5 for every piece but the group's last (CPX $D9 / BEQ comes first). So a group leaves $E5 = the
  // bits of its first three pieces and $E6 = the last piece's bit.
  int hi = 0, hiBeforeLast = 0, bit = 0;
  // From the second group on the loop's end is a direct-page word too: BAB6 `STZ $E5`, then BABA-BAC0 `LDA $D9 / CLC /
  // ADC #0020 / STA $E7` (16-bit; the third group's C087 adds #0040), the `CPX $E7` the group's last piece meets.
  if (g) { int eb = (base + 0x20 * g) & 0xFFFF; r[0x01E7] = (uint8_t)eb; r[0x01E8] = (uint8_t)(eb >> 8); }
  for (int e = g * 4 + 3; e >= g * 4; e--) {                  // B7A6 starts at the last piece
    int o = e * 8;
    int xd;
    if (instantiate) {                                        // B8ED LDA $4802,X / STA $4BC2,X
      xd = CT_STAGE(o) | (CT_STAGE(o + 1) << 8);
      CT_LIVE(o + 2) = (uint8_t)xd; CT_LIVE(o + 3) = (uint8_t)(xd >> 8);
    } else {                                                  // B7AB LDA $4BC2,X
      xd = CT_LIVE(o + 2) | (CT_LIVE(o + 3) << 8);
    }
    int s = (xd + sx) & 0xFFFF;                               // B7AF ADC $C3
    CT_LIVE(o) = (uint8_t)s;                                  // B7B3 STA $4BC0,X -> +0
    bit = (s >> 8) & 1;                                       // B7B6 XBA / B7B7 AND #01 / B7B9 STA $E6
    hiBeforeLast = hi;
    hi = ((hi << 2) | bit) & 0xFF;                            // B7BB-B7BF, 2 bits per sprite
  }
  r[0x01E5] = (uint8_t)hiBeforeLast; r[0x01E6] = (uint8_t)bit;   // B7C5 STA $E5 (not on the last) / B7B9 STA $E6
  r[0x10000 + ((HIDST[cls][g] + w) & 0xFFFF)] = (uint8_t)(hi | 0xAA);   // B7D0 ORA / B7D4 STA
  }
  // The Y half. `mode` 0 = B7DD/B922 ($C6 != 0), 1 = B83A/B9A5 ($C6 == 0 and screen-Y bit7 set),
  // 2 = B882/B9F7 ($C6 == 0, bit7 clear). All three clamp to #$E0 but on different conditions.
  int mode = ((sy >> 8) & 1) ? 0 : ((sy & 0x80) ? 1 : 2);     // B7D9 LDA $C6 / B836 LDA $C5 BPL
  // The spare record is class 0's alone, on path A (5/4/4 records for class 0, 8/8/8 for class 1, 12/12/12 for
  // class 2).
  int yPieces = pieces + (cls == 0 && mode == 0);
  for (int e = 0; e < yPieces; e++) {
    int o = e * 8;
    int yd;
    if (instantiate && e < pieces) { yd = CT_STAGE(o + 2); CT_LIVE(o + 4) = (uint8_t)yd; }  // B922
    else if (instantiate) yd = CT_LIVE(o + 4);            // no staged record for the spare
    else               yd = CT_LIVE(o + 4);                                   // B7DD
    CT_LIVE(o + 1) = (uint8_t)ctOamClamp(cls, mode, !instantiate, yd, sy);   // -> +1
  }
  if (instantiate)                                            // B983 LDA $4806,X / STA $4BC6,X --
    for (int e = 0; e < pieces; e++) {                        //   a 16-bit copy moving +6 AND +7
      int o = e * 8;
      CT_LIVE(o + 6) = CT_STAGE(o + 4); CT_LIVE(o + 7) = CT_STAGE(o + 5);
    }
  #undef CT_LIVE
  #undef CT_STAGE
}

// ---- C0/C73A: class 3's emitter and its whole release (anim_drain_cad9.md sec. 15) ----
// B701 reaches it as a tail call (B712 BNE ->B785 / B785 BRL C73A), so C73A's RTS (C7FA/C81F/C989) returns to B309's
// BCC draw gate. There is one emitter only: C0/C6E7 reads $4BC2,X (live) and never $4802,X, and C73A never tests
// $1B00,X, frees nothing and never stamps $80, which is why B701's T = (class & 3) + 1 law stops at class 2.
// Class 3 is six groups of four pieces (24): C0/C6E7 runs six times, X += $20 between calls, and the six high-table
// bytes go to $4F00 / $4F01 / $4B40 / $4B41 / $4F80 / $4F81 (as the draw arm C0/B5CB pushes them). The Y half counts
// LDA #18 = 24 on every path.
// C0/C6E7 walks its four pieces forward: it keeps pieces 0/1/2's X bit 8 in $E5/$E6/$E7 and piece 3's in A, then
// folds them (C728-C737); the byte matches B788's layout but the DP bytes left behind differ, so it is separate.
// The carry: path A (C820, 24 unrolled records) returns the carry of the last record's ADC $C5 (C97D); paths B and
// C return the carry of the final 16-bit ADC #0008 on X (C7EF / C814), set only when the record base is within
// eight bytes of wrapping bank $7F.
static int ctOamEmitClass3(uint8_t* r) {
  int w = r[0x016D] | (r[0x016E] << 8);                       // C741 LDX $6D -- X 16-bit (C0/00D9)
  int sy = (r[0x0A80 + w] | (r[0x0A81 + w] << 8)) & 0x01FF;   // C743 LDA $000A80,X / C747 AND #01FF
  int sx = r[0x0A00 + w] | (r[0x0A01 + w] << 8);              // C74C LDA $000A00,X
  int base = r[0x1700 + w] | (r[0x1701 + w] << 8);            // C752 LDA $001700,X / C756 TAX
  r[0x01C5] = (uint8_t)sy;   r[0x01C6] = (uint8_t)(sy >> 8);  // C74A STA $C5 -- 16-bit, bit 8 -> $C6
  r[0x01C3] = (uint8_t)sx;   r[0x01C4] = (uint8_t)(sx >> 8);  // C750 STA $C3
  #define CT_LIVE3(o) r[0x10000 + ((0x4BC0 + base + (o)) & 0xFFFF)]
  static const int HIDST3[6] = { 0x4F00, 0x4F01, 0x4B40, 0x4B41, 0x4F80, 0x4F81 };
  for (int g = 0; g < 6; g++) {
    int gb = (base + g * 0x20) & 0xFFFF;                      // C765-C76A TXA / CLC / ADC #0020
    r[0x01D9] = (uint8_t)gb; r[0x01DA] = (uint8_t)(gb >> 8);  // C75A STX $D9 (after the JSR, 16-bit)
    int hi = 0;
    for (int e = 0; e < 4; e++) {                             // C6E7, FORWARD over the group's four
      int o = g * 0x20 + e * 8;
      int xd = CT_LIVE3(o + 2) | (CT_LIVE3(o + 3) << 8);      // C6E7 LDA $4BC2,X (live, 16-bit)
      int s = (xd + sx) & 0xFFFF;                             // C6EA CLC / C6EB ADC $C3
      CT_LIVE3(o) = (uint8_t)s;                               // C6EF STA $4BC0,X -> +0
      int b8 = (s >> 8) & 1;                                  // C6F2 XBA / C6F3 AND #01
      if (e < 3) r[0x01E5 + e] = (uint8_t)b8;                 // C6F5/C707/C719 STA $E5 / $E6 / $E7
      hi |= b8 << (2 * e);                                    // C72B-C735: piece 0 lands in bits 0-1
    }
    r[0x10000 + ((HIDST3[g] + w) & 0xFFFF)] = (uint8_t)(hi | 0xAA);   // C737 ORA #AA / C75E STA
  }
  // C7C5-C7DD: X reloads from $1700,X and the Y-range classifier runs on the screen Y as in the other emitters
  // ($C6 != 0 -> path A, then $C5 bit 7 picks B over C).
  int mode = ((sy >> 8) & 1) ? 0 : ((sy & 0x80) ? 1 : 2);     // C7D0 LDA $C6 / C7D7 LDA $C5 BMI
  for (int e = 0; e < 24; e++) {                              // 24 on every path (C820 unrolled, #18)
    int o = e * 8;
    CT_LIVE3(o + 1) = (uint8_t)ctOamClamp(3, mode, 1, CT_LIVE3(o + 4), sy);   // -> +1
  }
  if (mode == 0)                                              // path A: the last ADC $C5 is the flag
    return ((CT_LIVE3(23 * 8 + 4) + (sy & 0xFF)) > 0xFF) ? 1 : 0;             // C97C-C97F
  r[0x01C9] = 0;                                              // C7E0/C7FD STA $C9 = #18, C7F5/C81A
  return ((base + 24 * 8) > 0xFFFF) ? 1 : 0;                  //   DEC it to 0; C7EF/C814 ADC #0008
  #undef CT_LIVE3
}

int CtAnimRelease(uint8_t* r, int idx) {
  // C0/B701. Its only caller is C0/B309 (JSR B701 / B30C BCC ->B30F / B30E RTS), so the carry is the draw gate:
  // SEC = no OAM this frame, CLC = draw. The class arms at B716 / B737 / B75E are the same sequence with a different
  // threshold, emitters and free:
  //     T = (class & 3) + 1                 == the arena arity in slots (1 / 2 / 3)
  //     n = $1B00,X                         == the staged-pass counter
  //     if ((n & 0x7F) < T) { bit7 set -> reposition-emit, CLC ; else -> SEC }
  //     else                { instantiate-emit, free T slots, $1B00,X = 0x80, CLC }
  // Bit 7 = already released: the reposition emitters (B788/BA65/BFF2) re-derive OAM X/Y from the live records; the
  // instantiate emitters (B8CA/BCDC/C2BF) first copy the staged template out of $7F:4802 + $1700,X into $7F:4BC0 +
  // $1700,X, which is why the slot may be freed then. The seven emitters touch none of $0BC0..$0BC3, $0D80, $1B00,
  // $0F01 or $0F00.
  int T = (r[0x1201 + idx] & 3) + 1;                       // B703-B714 (class 3 falls out below)
  // Class 3: B712 BNE ->B785 / B785 BRL C0/C73A, a tail call (see ctOamEmitClass3); its carry is data-dependent.
  if ((r[0x1201 + idx] & 3) == 3) return ctOamEmitClass3(r);
  int n = r[0x1B00 + idx];                                 // B716 / B737 / B75E
  int cls = r[0x1201 + idx] & 3;
  if ((n & 0x7F) < T) {
    if (n & 0x80) {                                        // B732/B759/B780 emit, then CLC
      ctOamEmit(r, cls, (cls + 1) * 4, 0);                 // B788 / BA65 / BFF2 -- reposition
      return 0;
    }
    return 1;                                              // B71B/B73C/B763 SEC -- not fully staged
  }
  ctOamEmit(r, cls, (cls + 1) * 4, 1);                     // B8CA / BCDC / C2BF -- instantiate
  CtArenaFree(r, idx, T);                                  // B722 E9E2 / B747 E9FF / B76E EA1F
  r[0x1B00 + idx] = 0x80;                                  // B727-B729 / B74C-B74E / B773-B775
  return 0;                                                // B72C / B751 / B778 CLC
}

// ---- C0/B271: the draw pass, ported for the arena lifecycle ----
// Its per-object body C0/B309 opens JSR C0/B701 / B30C BCC ->B30F / B30E RTS, and C0/B701 is the engine's only arena
// release (and the only stamp of $1B00,X = 0x80), so a host that runs the movement and the drain without this pass
// exhausts the pool.
// Ported: the per-group cursor init (B271-B2A3), the bucket walk high to low (B2A5-B2C2), the B701 gate, stage 2's
// cursor bookkeeping (B341 / B397 / B3BB; the group by $0F80,X & 0x0C: 0 -> $0181/$01DB, bit 2 -> $0185/$01DD,
// else $0189/$01DF), and the three hide-the-stale-tail loops (B2C4-B306).
// Not ported here: the OAM bytes. Stage 2 copies them through $2180 from the $7F:4Bxx and $7F:4F00 records;
// src/oam.c's CtBuildFieldOAM models that copy (`ct.exe --checkoam`), so the buffer is not written here.
// `unresolved` (may be NULL) counts objects whose B701 gate could not be decided; every class returns a derived
// carry, so it stays 0 unless a new undecidable case appears.
int gCtDrawSkipped;   // objects the B701 gate refused (SEC) on the last pass -- see --sweepdraw
// Cumulative trips of the draw walk's non-convergence guard, reported by Belthasar's worldtest so the guard cannot
// hide a corrupted list.
int gCtDrawRunaway;
int gCtDrawClsMask;   // bitmask of the sprite classes that reached the draw list, i.e. the classes whose OAM bytes
                      // a lockstep check can compare
int CtAnimDrawPass(uint8_t* r, int* unresolved) {
  int drawn = 0, unres = 0; gCtDrawSkipped = 0; gCtDrawClsMask = 0;
  r[0x0189] = 0x01; r[0x018A] = 0x09;                      // B271 LDX #0901 / B274 STX $89
  r[0x018B] = 0x00; r[0x018C] = 0x00;                      // B276 / B279
  r[0x0181] = 0x07; r[0x0182] = 0x09;                      // B27B LDX #0907 / B27E STX $81
  r[0x0183] = 0x00; r[0x0184] = 0x00;                      // B280 / B283
  r[0x0185] = 0x1A; r[0x0186] = 0x09;                      // B285 LDX #091A / B288 STX $85
  r[0x0187] = 0x00; r[0x0188] = 0x00;                      // B28A / B28D
  r[0x01DF] = 0x10; r[0x01E0] = 0x07;                      // B28F LDX #0710 / B292 STX $DF
  r[0x01DB] = 0x70; r[0x01DC] = 0x07;                      // B294 LDX #0770 / B297 STX $DB
  r[0x016E] = 0x00;                                        // B299 LDA #00 / B29E STA $6E ($2183 is a PPU reg)
  r[0x01DD] = 0xA0; r[0x01DE] = 0x08;                      // B2A0 LDX #08A0 / B2A3 STX $DD
  for (int Y = 0x7E; Y >= 0; Y -= 2) {                     // B2A5 LDY #007E ... B2C0 DEY DEY / BPL
    int obj = r[0x0E00 + Y];                               // B2A8 LDA $0E00,Y
    if (obj & 0x80) continue;                              // B2AB BMI ->B2C0: empty bucket
    int guard = 0;
    for (;;) {
      // Port-side non-convergence guard, not ROM behaviour: the ROM's B2B2 loop is unbounded because its $0E81 chain
      // is always well-formed. If the port's chain is ever corrupted (unlink/relink leaving stale heads), this makes
      // a counted wrong frame (gCtDrawRunaway) instead of a hang; 128 is above any real chain (at most 64 objects).
      if (++guard > 128) { gCtDrawRunaway++; if (unresolved) *unresolved = unres; return -1; }
      r[0x016D] = (uint8_t)obj;                            // B2AD / B2B9 STA $6D (8-bit)
      int c = CtAnimRelease(r, obj);                        // B309 JSR C0/B701
      if (c < 0) unres++;                                  // an undecided gate (no class produces one now)
      else if (c == 0) {                                   // B30C BCC ->B30F: CLC = draw it
        int prio = r[0x0F80 + obj] & 0x0C;                 // B334 LDA $0F80,X / B337 AND #0C
        int hi, mn;                                        // the group's (high, main) cursor pair
        if (prio == 0)          { hi = 0x0181; mn = 0x01DB; }   // B339 BEQ ->B341
        else if (prio & 0x04)   { hi = 0x0185; mn = 0x01DD; }   // B33B BIT #04 / BNE ->B397
        else                    { hi = 0x0189; mn = 0x01DF; }   // B33F BRA ->B3BB
        // The stride is per class: B309 dispatches on the class first (B316 BEQ ->B329 / B31C BRL ->B3DF / B323 BRL
        // ->B4B8) and each class body has its own priority arms. Group-0 arms: class 0 (B341) writes one high byte
        // ($7F4F00) and advances by $10 (B35D); class 1 (B3FB) two ($4F00/$4F01) and $20 (B421); class 2 (B4D4) three
        // ($4F00/$4F01/$4B40) and $30; class 3 (B5CB) six ($4F00/$4F01/$4B40/$4B41/$4F80/$4F81, as C0/C73A) and $60
        // on all three priority arms (B619 / B6A1 / B6F8). So the entry count is 1/2/3/6 by class.
        static const int ENT[4] = { 1, 2, 3, 6 };               // B341 / B3FB / B4D4 / B5CB
        int cls = r[0x1201 + obj] & 3;
        int ent = ENT[cls];                                     // OAM entries this object emits
        gCtDrawClsMask |= 1 << cls;                             // the coverage denominator, see above
        int h = (r[hi] | (r[hi + 1] << 8)) + ent;               // B34B/B40A+B40F/B4E8+B4ED+B4F2 INX
        r[hi] = (uint8_t)h; r[hi + 1] = (uint8_t)(h >> 8);
        int m = ((r[mn] | (r[mn + 1] << 8)) + 0x10 * ent) & 0xFFFF;   // B35D / B421 / B4F9-family
        r[mn] = (uint8_t)m; r[mn + 1] = (uint8_t)(m >> 8);
        drawn++;
      }
      else gCtDrawSkipped++;                               // B30E RTS: SEC = this object emits nothing
      obj = r[0x0E81 + r[0x016D]];                         // B2B2 LDX $6D / B2B4 LDA $0E81,X -- re-read
      if (obj & 0x80) break;                               // B2B7 BMI ->B2C0: chain end   $6D, as the ROM does
    }
  }
  // B2C4-B306. Per group: blank the Y byte of every OAM entry between this frame's cursor and the
  // PREVIOUS frame's high-water mark ($017B/$017D/$017F), then publish the new mark. `LDA #E0` is the
  // off-screen Y, `STA $0001,X` is the entry's Y byte, and each loop has its own upper bound so it
  // cannot run into the next group's block.
  { static const int CUR[3] = { 0x01DB, 0x01DD, 0x01DF };   // B2C4 / B2DC / B2F2, in that order
    static const int MARK[3] = { 0x017B, 0x017D, 0x017F };
    static const int LIM[3] = { 0x08A0, 0x0900, 0x0770 };   // B2D3 / B2E9 / B2FF
    for (int g = 0; g < 3; g++) {
      int x = r[CUR[g]] | (r[CUR[g] + 1] << 8);
      int mk = r[MARK[g]] | (r[MARK[g] + 1] << 8);
      for (; x < mk && x < LIM[g]; x += 4) r[(0x0001 + x) & 0xFFFF] = 0xE0;   // B2C8-B2D6
      x = r[CUR[g]] | (r[CUR[g] + 1] << 8);                 // B2D8 LDX $DB (re-read: the loop used a copy)
      r[MARK[g]] = (uint8_t)x; r[MARK[g] + 1] = (uint8_t)(x >> 8);            // B2DA STX $7B
    } }
  if (unresolved) *unresolved = unres;
  return drawn;
}

int CtAnimDrainPass(uint8_t* r, const uint8_t* rom, int romLen, int scanline) {
  // C0/CA76. Pops the FIFO head into $6D and calls C0/CAD9 until the queue empties or the raster
  // deadline expires. SEC keeps the object at the head (a resumable multi-pass stage), CLC pops it.
  if (r[0x09A0] != 0) return 0;                            // CA76 LDA $09A0 / BEQ / RTS -- the whole
                                                           //   pass is skipped while the VRAM upload
                                                           //   queue holds work. An 8-bit read: $09A1
                                                           //   collects stale values because B201's
                                                           //   clear is STZ $A0, also 8-bit.
  r[0x0179] = 0;                                           // CA80 STZ $79 (the upload cursor; DP=$0100)
  int visits = 0;
  for (;;) {
    // CA85-CA9B, the raster deadline. CA88-CA91 assemble the 9-bit V counter from two $213D reads; CA94 CMP #00F0
    // / BPL is signed (>= 240 = vblank, no limit) and CA99 CMP $6B / BCS is unsigned against the word $6B/$6C
    // (0x0078 = line 120 in the states measured; op C0/678E can set it from a script).
    // The clock is the virtual beam of the zelda3 model (scanline < 0, the live VM's call; see CLAUDE.md and
    // disasm/notes/anim_drain_cad9.md sec. 16): V = CT_DRAIN_VBEAM_ENTRY + visits * CT_DRAIN_VBEAM_STEP
    // (src/ct_vbeam.h), the same V the oracle reads under CT_ORACLE_VIRTUAL_BEAM. At $6B = 0x78 a pass makes two
    // visits. A fixed `scanline` (the harnesses' 0xF0 = vblank) drains the queue fully.
    int v = scanline < 0 ? CT_DRAIN_VBEAM_ENTRY + visits * CT_DRAIN_VBEAM_STEP : (scanline & 0x1FF);
    int budget = r[0x016B] | (r[0x016C] << 8);
    if (v < 0xF0 && v >= budget) return visits;            // CA9B BCS -> CAD6 SEP #20 / RTS
    int h = r[0x0176];                                     // CAA2 LDA $76
    if (h & 0x80) return visits;                           // CAA4 BMI -> CAD5 RTS: queue empty
    r[0x016D] = (uint8_t)h;                                // CAA6 STA $6D
    int last = (h == r[0x0177]);                           // CAA8-CAAC: head == tail = last element
    int c = CtAnimDrainVisit(r, rom, romLen, h);
    visits++;
    if (c < 0) return -1;                                  // an unported class: refuse, do not guess
    // Note: this SEC branch is derived, not checked: --sweepdrain cannot queue a class-1 object (on a fresh-boot
    // state C0/E687 falls through to the C0/E6B6 copier and writes bank 0), so only --sweepcad9 checks the SEC
    // return itself.
    if (c) {                                               // CAB1 / CAC5 BCS -> CA85: SEC, do not pop
      if (visits > 64) return -2;                          // port-side guard: C0/D50D-D514 returns SEC
      continue;                                            //   doing nothing for a class-2 object in
    }                                                      //   anim mode 3, so only the raster
                                                           //   deadline ends the ROM's spin
    if (last) {                                            // CAC7-CAD3: empty the list
      r[0x1081 + h] = 0x80; r[0x0176] = 0x80; r[0x0177] = 0x80;
    } else {                                               // CAB3-CAC0: pop the head
      r[0x0176] = r[0x1081 + h];
      r[0x1081 + h] = 0x80;
    }
    if (visits > 64) return -2;                            // same guard on the pop path
  }
}

// ---- C0/4557 + C0/4590: the object loader's WRAM-field half (disasm/notes/object_loader.md) ----
// It fills the tables the C0/CAD9 tree reads, walking the sprite id's 5-byte descriptor through four bank-$E4 pointer
// tables whose bases C0/0BB0-0C0E load from $E4:FFE0 into $AF / $B2 / $B5 / $B8 ($E4:2000 / 2300 / 2600 / 2800).
// Descriptors live at $E4:F000; records 0..6 are the seven PCs, so the NPC path indexes from $E4:F023 = $F000 + 7*5.

// `[$dp],Y` on one of the four base pointers: a 24-bit add that can carry out of the base bank.
static int ctLoaderTbl(const uint8_t* r, const uint8_t* rom, int romLen, int dp, int y, int wide) {
  int base = ((r[0x0100 + dp + 2] & 0xFF) << 16) | r[0x0100 + dp] | (r[0x0100 + dp + 1] << 8);
  int lo = romRead24Flat(r, rom, romLen, base + y);
  return wide ? (lo | (romRead24Flat(r, rom, romLen, base + y + 1) << 8)) : lo;
}

// C0/4639-4663 and C0/4686-46AE: the same three-way store, once per arm. LDA $73 / CMP #01 / BMI / BEQ is a signed
// compare, so the $29C8 arm is ($73 - 1) & $80, which $73 = $81 takes as well as $73 = 0.
static void ctLoaderPartySlot(uint8_t* r, int cur) {
  int p = r[0x0173];
  int dst = (((p - 1) & 0x80) != 0) ? 0x29C8 : (p == 1 ? 0x29D4 : 0x29E0);
  r[dst] = (uint8_t)cur; r[dst + 1] = (uint8_t)(cur >> 8);
}

// The descriptor walk, shared by the NPC loader (C0/45A3-4620), the PC loader (C0/449B-4518) and op 0x83 (C0/46E4).
// The copies are the same instructions apart from the table offsets and, for op 0x83, the stride (5 vs 10); the NPC
// and PC copies also reorder DEY DEY SEC / SEC DEY DEY, which has no effect. So the record base and the stride are
// the only parameters: $E4:F023 for NPCs (past the seven PC records), $E4:F000 for PCs, $E4:F600 for op 0x83.
// Returns the record's flat ROM offset (the NPC tail's dedup key).
static int ctLoaderWalk(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int recBase,
                        int stride) {
  int bf = (id * stride) & 0xFFFF;                         // 4595 LDA #05 (op 0x82) / 46E4 LDA #0A
  int rec = recBase + bf;                                  // $E4:F023 (NPC) or $E4:F000 (PC)
  r[0x01BF] = (uint8_t)bf; r[0x01C0] = (uint8_t)(bf >> 8); // 45A3 LDX $4216 / 45A6 STX $BF (16-bit)

  int f1 = (romLen > rec + 1) ? MR(rec + 1) : 0;          // 45A8 $E4F024,X
  // 45B1 STA $D9 is 16-bit after 45AE AND #00FF, so $01DA is zeroed. $01BF/$01C0, $01D9/$01DA and $01E3 are all C0/4590
  // writes to the shared direct page.
  r[0x01D9] = (uint8_t)f1; r[0x01DA] = 0x00;
  int y1 = f1 * 3;                                         // 45B3-45B6: *3 (a 3-byte pointer table)
  int a1 = ctLoaderTbl(r, rom, romLen, 0xB2, y1, 1);       // 45B9 LDA [$B2],Y -- the 16-bit address
  r[0x1380 + idx] = (uint8_t)a1; r[0x1381 + idx] = (uint8_t)(a1 >> 8);
  r[0x1300 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xB2, y1 + 2, 0);   // 45C4: the bank

  int f2 = (romLen > rec + 2) ? MR(rec + 2) : 0;          // 45CB $E4F025,X
  int v2 = (f2 * 0x18) & 0xFFFF;                           // 45D2 LDA #18 -> $4216
  r[0x1400 + idx] = (uint8_t)v2; r[0x1401 + idx] = (uint8_t)(v2 >> 8);       // 45E5 (16-bit)

  int f3 = (romLen > rec + 3) ? MR(rec + 3) : 0;          // 45EC $E4F026,X
  int y3 = f3 * 2;                                         // 45F5 ASL A -- a word table
  int w0 = ctLoaderTbl(r, rom, romLen, 0xB5, y3, 1);       // 45F9/45FE: [$B5],Y and [$B5],Y+2
  int w1 = ctLoaderTbl(r, rom, romLen, 0xB5, y3 + 2, 1);
  int d = ((w1 - w0) & 0xFFFF) >> 2;                       // 45FD-4601: (next - this) >> 2, a
  r[0x1480 + idx] = (uint8_t)d; r[0x1481 + idx] = (uint8_t)(d >> 8);         //   difference, not a
  r[0x1500 + idx] = (uint8_t)w0; r[0x1501 + idx] = (uint8_t)(w0 >> 8);       //   table value
  int w2 = ctLoaderTbl(r, rom, romLen, 0xB8, y3, 1);       // 460C LDA [$B8],Y
  r[0x1580 + idx] = (uint8_t)w2; r[0x1581 + idx] = (uint8_t)(w2 >> 8);

  r[0x1201 + idx] = (uint8_t)((romLen > rec + 4) ? MR(rec + 4) : 0);        // 4615: the class byte
  return rec;
}

int CtObjLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int ip, int state) {
  // C0/4557, the shared prologue.
  r[0x1100 + idx] = (uint8_t)state;                        // 4559: 4 = NPC (4590), 5 = enemy (46E1)
  ip = (ip + 1) & 0xFFFF;                                  // 455C INY
  int id = r[0x12001 + ip];                                // 455E LDA $7F2001,X -- the script byte
  r[0x1101 + idx] = (uint8_t)id;                           // 4564
  r[0x1280 + idx] = r[0x0171];                             // 456C LDA $71 / STA $1280,X (16-bit) --
  r[0x1281 + idx] = r[0x0172];                             //   the graphics staging address
  r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 4571 LDA #2020 / STA $0C00,X
  r[0x1200 + idx] = 0x7F;                                  // 4579: the graphics bank
  r[0x1600 + idx] = 0x01;                                  // 457E: facing
  r[0x1680 + idx] = 0x00; r[0x1681 + idx] = 0x00;          // 4583 / 4588
  ip = (ip + 1) & 0xFFFF;                                  // 458D INY
  // C0/4590, the descriptor walk. 459A STY $C7 (16-bit) keeps the only copy of the advanced IP: Y is clobbered from
  // 45B8 on and both exits reload LDX $C7 (4667 / 46DB).
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
  r[0x1B01 + idx] = 0x01;                                  // 459E
  int rec = ctLoaderWalk(r, rom, romLen, idx, id, 0x24F023, 5);   // C0/45A3-4620

  // C0/5C90, the per-scene graphics dedup. $0920[0..15] holds the sprite ids loaded this scene and $0930[0..15] the
  // owner's $6D. A hit returns the owner with carry set and the caller copies its $1280 (4631); a miss claims the
  // first $FF slot, records id and owner and returns carry clear, and the caller queues a decompression and advances
  // the cursor.
  int e3 = (romLen > rec) ? MR(rec) : 0;                  // 4620 $E4F023,X
  r[0x01E3] = (uint8_t)e3;                                 // 4624 STA $E3 -- DP is $0100, so $01E3
  int k;
  for (k = 0; k < 16; k++) if (r[0x0920 + k] == (uint8_t)e3) break;          // 5C95 CMP $0920,X
  if (k < 16) {                                            // HIT (5CC2 LDA $0930,X / SEC)
    int owner = r[0x0930 + k];
    r[0x1280 + idx] = r[0x1280 + owner];                   // 4631-4636
    r[0x1281 + idx] = r[0x1281 + owner];
    // 4639-4663: the hit arm records the current cursor, not the owner's address, as the ROM does.
    ctLoaderPartySlot(r, r[0x0171] | (r[0x0172] << 8));
  } else {
    // The miss arm, C0/466B-46DE: queue the decompression and advance the cursor.
    for (k = 0; k < 16; k++) if (r[0x0920 + k] == 0xFF) break;               // 5CA5 CMP #FF
    if (k >= 16) return ip;   // ROM: LDX #4010 / BRL C0/2E1E, a diagnostic halt (SEI, force blank, NMI and DMA
                              // off) that never returns. The port stops instead.
    r[0x0920 + k] = (uint8_t)e3;                           // 5CB6-5CBD: claim the slot
    r[0x0930 + k] = (uint8_t)idx;
    // The 6-byte request record: a 24-bit source from the 3-byte packet table [$AF], and a 24-bit destination, the
    // cursor in bank $7F.
    r[0x01D9] = (uint8_t)e3; r[0x01DA] = 0x00;             // 4672 STA $D9 -- the second spill, which
    int y = e3 * 3;                                        // 466D-4679: *3   overwrites 45B1's
    int sLo = ctLoaderTbl(r, rom, romLen, 0xAF, y, 1);     // 467A LDA [$AF],Y / STA $0300
    int sBk = ctLoaderTbl(r, rom, romLen, 0xAF, y + 2, 0); // 46B2 (after INY INY) / STA $0302
    int cur = r[0x0171] | (r[0x0172] << 8);
    r[0x0300] = (uint8_t)sLo; r[0x0301] = (uint8_t)(sLo >> 8);
    r[0x0302] = (uint8_t)sBk;
    r[0x0303] = (uint8_t)cur; r[0x0304] = (uint8_t)(cur >> 8);               // 467F LDA $71
    r[0x0305] = 0x7F;                                      // 46B7 LDA #7F / STA $0305
    ctLoaderPartySlot(r, cur);                             // 4686-46AE: the pre-advance cursor
    // JSL $C3:0002, the shared LZ (ct_lz_decompress): the sheet goes to $7F:cur and the byte count to $0306, which
    // is what moves $71. Only a HiROM packet bank ($C0..) is decompressed.
    if (sBk >= 0xC0) {
      static uint8_t scratch[0x10000];
      int src = ((sBk - 0xC0) << 16) | (sLo & 0xFFFF);
      if (src + 2 <= romLen) {
        int sz = (int)ct_lz_decompress_bounded(rom, (size_t)romLen, scratch,
                                               sizeof scratch, (size_t)src);
        // The load's black window also covers this decompression ($0305 = $7F, C3/0557's odd-bank arm); the host
        // zeroes the accumulator before a load.
        g_ctLzSpriteCycles += ct_lz_rom_cycles(rom, (size_t)romLen, (size_t)src, 1);
        r[0x0306] = (uint8_t)sz; r[0x0307] = (uint8_t)(sz >> 8);             // C3/0002's report
        for (int q = 0; q < sz && cur + q < 0x10000; q++)                    // ... to $7F:cur
          r[0x10000 + cur + q] = scratch[q];
        cur = (cur + sz) & 0xFFFF;                         // 46C2-46C8: $71 += $0306
        r[0x0171] = (uint8_t)cur; r[0x0172] = (uint8_t)(cur >> 8);
      }
    }
    // 46CA CMP #8000 / BPL: the staging heap is $7F:8000-$FFFF, so a cursor below $8000 has wrapped past $FFFF, and
    // that arm is LDX #7C1F / BRL C0/2E1E, the same diagnostic halt.
    if (cur < 0x8000) return ip;
  }
  r[0x0173] = (uint8_t)((r[0x0173] + 1) & 0xFF);           // 4665 / 46D9 INC $73
  return ip;
}

// C0/448D: the PC descriptor loader, op 0x80's worker: the walk above with record base $E4:F000 and a different
// tail: it reads the graphics pointer from the packet table [$AF] and stops (no dedup, no decompression; a PC's
// sheet is resident). It does not write $1B01,X (448D has three NOPs at 4498-449A where C0/4590 stores it).
// `yIn` is Y at the JSR. 4490 does one INY and 4496 STY $C7 spills it; 4552 LDY $C7 restores it, so op 0x80's arms
// end TYX / SEC. Op 0x80 arrives with yIn = ip+1 (421E INY: two bytes), its seven siblings with yIn = ip (one byte).
static int ctPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn) {
  int ip = (yIn + 1) & 0xFFFF;                             // 4490 INY -- this routine's own, only
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8); // 4496 STY $C7 (16-bit)
  int rec = ctLoaderWalk(r, rom, romLen, idx, id, 0x24F000, 5);       // C0/449B-4518
  // The PC tail, C0/4518-4551.
  int e0 = (romLen > rec) ? MR(rec) : 0;                  // 4518 LDA $E4F000,X
  r[0x01D9] = (uint8_t)e0; r[0x01DA] = 0x00;               // 4521 STA $D9 -- 16-bit, so $01DA is
  int y = e0 * 3;                                          // 4523-4528: *3   zeroed
  int a = ctLoaderTbl(r, rom, romLen, 0xAF, y, 1);         // 4529 LDA [$AF],Y
  r[0x1280 + idx] = (uint8_t)a; r[0x1281 + idx] = (uint8_t)(a >> 8);  // 452D (16-bit)
  r[0x1200 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xAF, y + 2, 0);   // 4534/4536: the bank
  r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 453B LDA #2020 / STA $0C00,X
  r[0x1680 + idx] = 0x00;                                  // 4543
  r[0x1681 + idx] = 0x00;                                  // 4548
  r[0x1600 + idx] = 0x01;                                  // 454D
  return ip;                                               // 4552 LDY $C7 / TYX / SEC / RTS
}

// C0/42BA-430F, C0/438A-43C2 and C0/443D-4472: one default placement, identical in op 0x80's three party arms. Its
// inputs $0102/$0103/$0104 are the arrival staging of the location change C0/3B1B (3B30 STA $02, 16-bit: the
// arrival tile; 3B34-3B3F build $0104 from the destination word's high byte: facing in bits 0-1, bit 7 separately).
// The $FF / $80 literals are the tile-foot / tile-centre convention of op 0x8B.
static void ctPcPlaceDefault(uint8_t* r, int idx) {
  r[0x1801 + idx] = r[0x0102];                                          // 42BA / 438A / 443D
  r[0x1881 + idx] = r[0x0103];                                          // 42BF / 438F / 4442
  r[0x1600 + idx] = (uint8_t)(r[0x0104] & 3);                           // 42E1 / 4394 / 4447
  r[0x1880 + idx] = (uint8_t)((r[0x0104] & 0x08) ? 0x7F : 0xFF);        // 42E8 BIT #08
  r[0x1800 + idx] = (uint8_t)((r[0x0104] & 0x04) ? 0x00 : 0x80);        // 42FA BIT #04
}

// ---- C0/6F9A / 6FBA / 7008 / 7056: the OBJ slot allocator and release ----
// $0B88 is the slot table: bit 7 set = free, else the owner's $6D. It fills $1700 (the OBJ template base) and $0D00
// (the OAM tile base) for the CAD9 tree.

// C0/6F9A: LDA $1100,X / BMI -> RTS / ASL A / TAX / JSR ($6FA7,X). The 9-entry table at $6FA7 is B9 6F B9 6F B9 6F BA
// 6F BA 6F B9 6F B9 6F B9 6F B9 6F, and $6FB9 is an RTS, so only states 3 and 4 allocate; the others return with the
// carry untouched, hence -1 ("no verdict") rather than 0.
// Returns 1 = SEC (granted, $0D00/$1700 written), 0 = CLC (table full, nothing written).
int CtObjSlotAlloc(uint8_t* r, int idx) {
  static const uint8_t kArm[9] = { 0, 0, 0, 1, 1, 0, 0, 0, 0 };             // $6FA7 table
  int st = r[0x1100 + idx];
  if (st & 0x80) return -1;                                                 // 6F9E BMI ->6FA6
  // 6FA1 ASL A / TAX is 8-bit (bit 7 goes to the carry); the BMI above has already rejected bit-7 states, so the
  // & 0xFF only documents the width.
  int e = (st << 1) & 0xFF;                                                 // 6FA1 ASL A / TAX
  if (e >= 18) return -1;                       // port-side guard: past the nine entries the ROM would read
                                                // $6FB9/$6FBA as a pointer
  if (!kArm[e >> 1]) return -1;                                             // -> $6FB9 = RTS
  int slot = -1, x;
  if ((r[0x1201 + idx] & 3) == 0) {                                         // 6FBD AND #03 / BEQ
    for (x = 0x19; x != 0; x--)                                             // 6FC5 LDX #19 .. 7003 DEX/BNE
      if (r[0x0B88 + x] & 0x80) { slot = x; break; }                        // 6FC7 BPL = occupied
    if (slot < 0) return 0;                                                 // 7006 CLC
    r[0x0B88 + slot] = (uint8_t)idx;                                        // 6FCC
  } else {                                                                  // 6FC3 BRA ->7008
    for (x = 0x0C; x < 0x18; x += 2)                                        // 7008 LDX #0C .. 7050 CPX #18
      if ((r[0x0B88 + x] & 0x80) && (r[0x0B89 + x] & 0x80)) { slot = x; break; }
    if (slot < 0) return 0;                                                 // 7054 CLC
    r[0x0B88 + slot] = (uint8_t)idx;                                        // 7016
    r[0x0B89 + slot] = (uint8_t)idx;                                        // 7019
  }
  // One formula on both paths (6FD1-6FE6 and 701C-7031): the even part of the slot * 16, plus 8 for an odd slot, so
  // a pair shares a 16-tile row.
  int tile = (((slot & 0xFE) << 4) + ((slot & 1) << 3)) & 0xFFFF;
  r[0x0D00 + idx] = (uint8_t)tile; r[0x0D01 + idx] = (uint8_t)(tile >> 8);  // 6FEB / 7036
  int tmpl = (slot & 0xFF) << 5;                                            // 6FF7 / 7042 ASL x5
  r[0x1700 + idx] = (uint8_t)tmpl; r[0x1701 + idx] = (uint8_t)(tmpl >> 8);  // 6FFC / 7047
  return 1;                                                                 // 7001 / 704C SEC
}

// ---- C0/B1B2: the VRAM upload ----
// The consumer of the queue C0/CAD9 fills (ctAnimAppend) and the only clear of $09A0, C0/CA76's whole-pass gate.
// Called from C0/B111 (the scene-entry sweep C0/B0E6) and C0/EB8C (the NMI). DP = $0900; DMA channel 7 is set once:
//   $2115 = $80    VMAIN: increment after the $2119 (high) write, step 1 word
//   $4370 = $01    DMAP7 mode 1: $2118 then $2119, alternately
//   $4371 = $18    BBAD7 = $2118 (VMDATAL)
//   $4374 = $7F    A1B7, the source bank (the staging bank)
// then per entry X (by two) it fires pair A ($0950 dst / $0940 src / $0980 len) and pair B ($0970 / $0960 / $0990).
// $09A0,X is read 8-bit and a zero ends the walk (ctAnimAppend writes it 16-bit).
// `vram` is a 64 KB VRAM image, or NULL to walk the queue and clear the gate without copying.

// One STA $420B with channel 7 set as above. Mode 1 alternates the two data ports and VMAIN $80 advances the word
// address after the second of each pair. A DMA size of 0 means 0x10000 bytes.
static void ctVramDma(const uint8_t* r, uint8_t* vram, int vmaddr, int src, int len) {
  if (!vram) return;
  // VMADD is a word address into 32K words; the byte index below masks to 0xFFFF, which folds word $8000 onto $0000
  // as the hardware does.
  int w = vmaddr;
  int n = (len == 0) ? 0x10000 : len;
  int i, hi = 0;
  for (i = 0; i < n; i++) {
    uint8_t b = r[0x10000 + ((src + i) & 0xFFFF)];        // the source bank is $7F, fixed at B1C9
    if (!hi) { vram[(w * 2) & 0xFFFF] = b; hi = 1; }      // $2118
    else     { vram[(w * 2 + 1) & 0xFFFF] = b; hi = 0;    // $2119
               w = w + 1; }
  }
}

int CtAnimUploadFlush(uint8_t* r, uint8_t* vram) {
  int n = 0, x;
  // The ROM has no bound here (B1FD INX INX / BRA ->B1D1) and trusts ctAnimAppend's terminator; 0x80 bytes = 64
  // entries is a port-side backstop.
  for (x = 0; x < 0x80; x += 2) {
    if (r[0x09A0 + x] == 0) break;                                         // B1D1 8-bit / B1D3 BEQ
    ctVramDma(r, vram, r[0x0950 + x] | (r[0x0951 + x] << 8),               // B1D5 dst A
                       r[0x0940 + x] | (r[0x0941 + x] << 8),               // B1DA src A
                       r[0x0980 + x] | (r[0x0981 + x] << 8));              // B1DF len A
    ctVramDma(r, vram, r[0x0970 + x] | (r[0x0971 + x] << 8),               // B1E9 dst B
                       r[0x0960 + x] | (r[0x0961 + x] << 8),               // B1EE src B
                       r[0x0990 + x] | (r[0x0991 + x] << 8));              // B1F3 len B
    n++;
  }
  r[0x09A0] = 0;                    // B201 STZ $A0 -- 8-bit, entry 0 only: the next append restarts the
                                    // queue at cursor $79, so entry 0's terminator is all C0/CA76 needs.
  return n;
}

// ---- C0/56B0's scene-entry init of the animation tree's tables ----
// C0/56B0 runs 6F79 / 7155 / E935 / 5929 / B204 / A7E9 / 5709 / 595C; four of those are the fills below. They are a
// precondition: C0/A9CD links only when $0E01,X is already negative, which only C0/B204 sets. Three of them write
// through the WRAM port ($2180, DP = $2100); only the destinations and values matter here.
void CtObjSceneInit(uint8_t* r, int nobj) {
  int k;
  for (k = 0; k < 0x1E; k++) r[0x0B88 + k] = 0x80;   // C0/6F79: 30 OBJ slots, all free
  for (k = 0; k < 8;    k++) r[0x0BC0 + k] = 0x80;   // C0/E935: the arena owner table -- eight bytes
                                                     //   cleared, though C0/E952 hands out only four
  // C0/B204: $0E00..$0E1F written directly, then three chained MVNs ($20, $40, $80 bytes from $0E00) double the fill
  // out to $0EFF. Every head, flag, prev and next starts at $80 ("none" / unlinked).
  for (k = 0; k < 0x100; k++) r[0x0E00 + k] = 0x80;
  // C0/A7E9: one (00, FF) pair per object from $0F00 (the activation latch and the "no frame yet" sentinel),
  // counted by the object count.
  for (k = 0; k < nobj; k++) { r[0x0F00 + k * 2] = 0x00; r[0x0F01 + k * 2] = 0xFF; }
}

// ---- C0/A98A / C0/A9CD: the Y-sorted sprite list's unlink and link ----
// They bracket C0/AB45 in the movement pass (A832 C98A / A835 A98A / A838 AB45 / A83B A9CD, again at A878-A881): an
// object leaves the bucket of its old screen Y before AB45 recomputes it, and re-enters by the new one. Layout:
//   $0E00,Y  (Y even, 0..$7E)  = the bucket heads; bucket = ($0A80,X >> 1) & $FE (the screen Y's low byte only)
//   $0E01,X  (X even)          = the per-object out-of-the-list flag; bit 7 set = unlinked
//   $0E80,X / $0E81,X          = the per-object prev / next links
// $80 = "none" on every link. The draw pass walks $0E00 from bucket $7E down along $0E81 (C0/B2A0-B2C0), as
// CtBuildFieldOAM in oam.c does. The chain order depends on insertion history, so the list has to be maintained
// incrementally; a per-frame rebuild does not match (`ct.exe --checksort`).

// C0/A98A. Removes the object from its bucket. Returns 1 if it did work, 0 if it was already out
// (`$0E01,X` negative -> A991 BMI, an immediate RTS).
int CtObjListUnlink(uint8_t* r, int idx) {
  if (r[0x0E01 + idx] & 0x80) return 0;                    // A98E/A991: already unlinked
  int prev = r[0x0E80 + idx];                              // A993
  if (prev & 0x80) {                                       // A996 BPL: negative = we are the head
    int bucket = (r[0x0A80 + idx] >> 1) & 0xFE;            // A998-A99E (8-bit: the low byte only)
    int next = r[0x0E81 + idx];                            // A99F
    r[0x0E00 + bucket] = (uint8_t)next;                    // A9A2: the head becomes our next
    int a = next;
    if (!(next & 0x80)) { a = 0x80; r[0x0E80 + next] = 0x80; }   // A9A5-A9AC: next is the head now
    r[0x0E81 + idx] = (uint8_t)a;                          // A9AD -- `a` is the next value when next was
    r[0x0E01 + idx] = (uint8_t)a;                          // A9B0    already negative, else $80
    return 1;
  }
  int next = r[0x0E81 + idx];                              // A9B6
  r[0x0E81 + prev] = (uint8_t)next;                        // A9B9: prev.next = next
  // A9BC-A9C0 TAY / STA $0E80,Y has no sign test, so for the tail (next = $80) the ROM writes prev into $0E80+$80 =
  // $0F00, object 0's activation latch. Kept as the ROM has it.
  r[0x0E80 + next] = (uint8_t)prev;                        // A9BE: next.prev = prev
  r[0x0E80 + idx] = 0x80;                                  // A9C3
  r[0x0E81 + idx] = 0x80;                                  // A9C6
  r[0x0E01 + idx] = 0x80;                                  // A9C9
  return 1;
}

// C0/A9CD: inserts the object at the head of the bucket its current screen Y selects. Returns 1 if it linked, 0 if
// it was already in a list ($0E01,X non-negative -> A9D4 BPL). The non-empty arm never writes $0E80,X: our prev is
// $80 from the unlink that preceded.
int CtObjListLink(uint8_t* r, int idx) {
  if (!(r[0x0E01 + idx] & 0x80)) return 0;                 // A9D1/A9D4: already linked
  int bucket = (r[0x0A80 + idx] >> 1) & 0xFE;              // A9D6-A9DC
  int head = r[0x0E00 + bucket];                           // A9DD
  if (head & 0x80) {                                       // A9E0 BPL: negative = the bucket is empty
    r[0x0E00 + bucket] = (uint8_t)idx;                     // A9E3
    r[0x0E01 + idx] = 0x00;                                // A9E6 STZ
    r[0x0E80 + idx] = 0x80;                                // A9EB
    r[0x0E81 + idx] = 0x80;                                // A9EE
    return 1;
  }
  r[0x0E80 + head] = (uint8_t)idx;                         // A9F6: oldHead.prev = us
  r[0x0E81 + idx] = (uint8_t)head;                         // A9FA: our next = oldHead
  r[0x0E00 + bucket] = (uint8_t)idx;                       // AA00: we are the head
  r[0x0E01 + idx] = 0x00;                                  // AA03 STZ
  return 1;
}

// C0/AB45: the per-object screen position, called from the movement pass (C0/A838 and C0/A87E, in C0/A810) and from
// C0/B101. The camera is the low byte of $1D0A/$1D0E * 8 plus the sub-tile phase $1D87/$1D89 & 7; the object's
// position is $1800/$1880 >> 4 (the low nibble is sub-pixel), and the difference is kept to nine bits with bit 8
// forced on a borrow. Bit 8 is the >= $100 arm the OAM emitters' Y clamps take, so an object left of or above the
// camera clamps as off-screen.
void CtObjScreenPos(uint8_t* r, int idx) {
  int phx = (r[0x1D87] | (r[0x1D88] << 8)) & 7;                          // AB49 LDA $1D87/AND #0007
  int phy = (r[0x1D89] | (r[0x1D8A] << 8)) & 7;                          // AB51
  int camX = ((((r[0x1D0A] | (r[0x1D0B] << 8)) & 0xFF) << 3) + phx) & 0xFFFF;   // AB59 ASL x3 + $D9
  int camY = ((((r[0x1D0E] | (r[0x1D0F] << 8)) & 0xFF) << 3) + phy) & 0xFFFF;   // AB67
  r[0x01D9] = (uint8_t)phx;  r[0x01DA] = (uint8_t)(phx >> 8);            // the DP scratch the ROM
  r[0x01DB] = (uint8_t)phy;  r[0x01DC] = (uint8_t)(phy >> 8);            // leaves behind
  r[0x01DD] = (uint8_t)camX; r[0x01DE] = (uint8_t)(camX >> 8);
  r[0x01DF] = (uint8_t)camY; r[0x01E0] = (uint8_t)(camY >> 8);
  int wx = (r[0x1800 + idx] | (r[0x1801 + idx] << 8)) >> 4;              // AB75 LSR A x4
  int sx = (wx - camX) & 0xFFFF;
  if (wx < camX) sx |= 0x0100;                                           // AB7F BCC -> ORA #0100
  sx &= 0x01FF;                                                          // AB84 AND #01FF
  r[0x0A00 + idx] = (uint8_t)sx; r[0x0A01 + idx] = (uint8_t)(sx >> 8);   // AB87
  int wy = (r[0x1880 + idx] | (r[0x1881 + idx] << 8)) >> 4;              // AB8A
  int sy = (wy - camY) & 0xFFFF;
  if (wy < camY) sy |= 0x0100;                                           // AB94
  sy &= 0x01FF;                                                          // AB99
  r[0x0A80 + idx] = (uint8_t)sy; r[0x0A81 + idx] = (uint8_t)(sy >> 8);   // AB9C
}

// C0/7056: recovers the slot from $1700,X >> 5 and releases it. It never consults the class: it frees the pair iff
// the neighbour is also ours (like the arena's C0/EA42). Returns 1 = SEC (released), 0 = CLC (not this object's).
int CtObjSlotFree(uint8_t* r, int idx) {
  int slot = ((r[0x1700 + idx] | (r[0x1701 + idx] << 8)) >> 5) & 0xFF;      // 705A LSR x5 / TAX
  if (r[0x0B88 + slot] != (uint8_t)idx) return 0;                           // 7067 CMP / 706C CLC
  if (r[0x0B89 + slot] == (uint8_t)idx) {                                   // 706E CMP $0B89,X
    r[0x0B89 + slot] = 0x80;                                                // 7075
    r[0x0B88 + slot] = 0x80;                                                // 7078
  } else {
    r[0x0B88 + slot] = 0x80;                                                // 707F
  }
  return 1;                                                                 // 707B / 7082 SEC
}

// ---- The animation family: C0/2E67 (0xAA), 2E8F (0xAE), 2E87 (0xB3), 2E8B (0xB4), 2E9A (0xAB), 2F01 (0xB7),
// 2F71 (0xAC) ----
// Shared with the live VM (ct_vm.c step()). The blocking ops complete on $7F0B01,X, the byte C0/2E9C branches on,
// which the frame legs (CtAnimTimerTick's CA4F ping-pong and CtFieldFrameLookup's CBA4 arm) drive 2 -> 1. Operands
// are read through `script` as in CtVmItemFamily. Returns 1 if `script[ip]` is in the family (and fills *out).
int CtVmAnimFamily(uint8_t* r, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  if (op != 0xAA && op != 0xAB && op != 0xAC && op != 0xAE && op != 0xB3 && op != 0xB4 && op != 0xB7)
    return 0;
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 0;
  switch (op) {
      case 0xAA:                                          // C0/2E67 -- looping animeset (operand id)
        // Five unconditional writes; op 0xB3 below is the same tail entered with A = 0 and without the leading INY,
        // so its IP delta is one less.
        r[0x1680 + idx] = script[(ip + 1) & 0xFFFF];   // 2E6F
        r[0x1780 + idx] = 0x01;                               // 2E74
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xAE:
        // C0/2E8F: a fourth entry into the same tail. Unlike 0xB3/0xB4 (LDA #imm / BRA ->2E6D) it stores $1680,X
        // itself and branches to 2E74, past 2E72 LDA #01, so $1780,X receives the 0 still in A:
        //     0xB3 -> $1680=0, $1780=1     0xB4 -> $1680=1, $1780=1     0xAE -> $1680=0, $1780=0
        //   2E8F A6 6D       LDX $6D
        //   2E91 A9 00       LDA #00
        //   2E93 9D 80 16    STA $1680,X
        //   2E96 A9 00       LDA #00
        //   2E98 80 DA       BRA -38 -> 2E74
        // $1780 is the anim mode the drain tree branches on (and op 0xAC gates on).
        r[0x1680 + idx] = 0x00;                               // 2E93
        r[0x1780 + idx] = 0x00;                               // 2E74, with A = 0 and not 1
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // one byte: no INY anywhere
      case 0xB3:                                          // C0/2E87 -- animeset 0. `LDA #00 / BRA ->2E6D`
        r[0x1680 + idx] = 0x00;                               // the shared 2E6D tail, A = 0
        r[0x1780 + idx] = 0x01;
        r[0x1601 + idx] = 0x00;
        r[0x10B01 + idx] = 0x00;
        r[0x1681 + idx] = 0x00;
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // one byte: no operand
      case 0xB4:                                          // C0/2E8B -- animeset 1. `LDA #01 / BRA ->2E6D`
        // The third entry into the shared tail:
        //   2E6D A6 6D       LDX $6D
        //   2E6F 9D 80 16    STA $1680,X   <- the only thing the three entries differ in (A)
        //   2E72 A9 01       LDA #01
        //   2E74 9D 80 17    STA $1780,X
        //   2E77 9E 01 16    STZ $1601,X
        //   2E7A A9 00       LDA #00
        //   2E7C 9F 01 0B 7F STA $7F0B01,X
        //   2E80 9E 81 16    STZ $1681,X
        //   2E83 BB E8 38 60 TYX / INX / SEC / RTS
        // The tail exits TYX / INX (one INX), so the IP delta depends on whether the entry moved Y: 0xAA (2E67) opens
        // with INY for its operand (ip+2); 0xB3 (2E87) and 0xB4 (2E8B) leave Y alone (ip+1, carry set). 2E8D `80 DE`
        // targets 2E6D. No operand.
        r[0x1680 + idx] = 0x01;                               // 2E6F with A = 1
        r[0x1780 + idx] = 0x01;                               // 2E74
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xB7:
      case 0xAB: {
        // C0/2E9A: anime once. Four arms on $7F0B01,X; the handler never executes INY, so only the completion arm
        // (TYX/INX at its exit) consumes the op's bytes. The others re-park on the op, so a one-shot animation
        // blocks the script until the drain reports it finished.
        int st = r[0x10B01 + idx];                        // 2E9C -- the only arm selector
        int oper = script[(ip + 1) & 0xFFFF];
        if (st == 1) {                                    // 2EA2 DEC A / 2EA3 BEQ ->2EB4: complete
          r[0x10B01 + idx] = 0x00;                        // 2EB6
          r[0x1601 + idx] = 0x00;                         // 2EBA
          r[0x1681 + idx] = 0x00;                         // 2EBD
          int was = r[0x1680 + idx];
          if (was == 0xFF) r[0x1680 + idx] = 0x00;        // 2ECB, only when the $FF marker is there
          r[0x1780 + idx] = (uint8_t)((was == 0xFF) ? 0 : 1);   // 2ED0
          // Op 0xB7 (C0/2F01) has one extra INX (2F3D) before its SEC, so it advances three bytes (the third is
          // consumed and ignored here).
          h.ip = (ip + (op == 0xB7 ? 3 : 2)) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 2ED3 / 2F3D
        }
        if (st >= 2 && oper == r[0x1781 + idx]) {         // park: the same anim is already running
          h.ip = ip; h.carry = 0; h.ok = 1; break;        // no writes
        }
        // Arm (st == 0) and re-arm (st >= 2 with a different id) are the same code at 2EE0-2F00.
        r[0x1781 + idx] = (uint8_t)oper;                  // 2EE0
        r[0x1681 + idx] = 0x00;                           // 2EE3
        r[0x1601 + idx] = 0x00;                           // 2EE6
        if (r[0x1780 + idx] == 0) r[0x1680 + idx] = 0xFF; // 2EF0, conditional
        r[0x1780 + idx] = 0x02;                           // 2EF5 / 2F5D -- the same in both
        // Here 0xB7 differs: 0xAB stores the literal 2 (2EF8 LDA #02 / 2EFA STA $7F0B01,X); 0xB7 does 2F60 TYX / INX
        // / INX / 2F63 LDA $7F2001,X / 2F67 INC A / 2F6A STA $7F0B01,X, storing its third operand + 1 (the repeat
        // count).
        r[0x10B01 + idx] = (op == 0xB7)
                           ? (uint8_t)(script[(ip + 2) & 0xFFFF] + 1)   // 2F63-2F6A
                           : 0x02;                                             // 2EF8-2EFA
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 2EFE / 2F6E TYX / CLC
      }
      case 0xAC: {
        // C0/2F71: animeset from a sprite-assembly id, the $1780 = 3 member (0xAA/0xB3/0xB4 use mode 1, 0xAB 2). Two
        // bytes, ip+2, carry clear (it yields, unlike the mode-1 ops). It never executes INY: 2F71 TYX / 2F72 INX
        // reads the operand, and the exit 2F91 TYX / INX / INX gives ip+2.
        r[0x1301 + idx] = script[(ip + 1) & 0xFFFF];   // 2F73/2F79 STA $1301,X
        r[0x1681 + idx] = 0x00;                               // 2F7C STZ $1681,X
        r[0x1601 + idx] = 0x00;                               // 2F7F STZ $1601,X
        if (r[0x1780 + idx] == 0) r[0x1680 + idx] = 0xFF;     // 2F82/2F85 BNE ->2F8C / 2F87/2F89
        r[0x1780 + idx] = 0x03;                               // 2F8C/2F8E
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;   // 2F91 TYX/INX/INX + 2F94 CLC
      }
    default: break;
  }
  *out = h;
  return 1;
}

// ---- C0/571A-58AA: the per-object default fill of a location load ----
// It sits between the DMA clears (5717 JSR C0/2DF1 = $7F:0200; 58B9/58CA = $7E:1900 and $7F:0580-1B3F) and writes
// through the WRAM port (PHD / LDA #2100 / TCD: $81-$83 = WMADD, $80 = WMDATA), so neither a DMA census nor a grep
// for STA $1000,X finds it. `$1000,X = 4` clears bit 7, the object pause (op 0x0B) that the activate guard C0/5AD1
// refuses on; many scripts never clear it themselves, so without this fill it would carry over between scenes.
// `$1080` is the anim FIFO's link pair, whose bit-7 marker C0/5A0F reads (LDA $1080,X / BPL = already queued).
// The count is $7F2000 (the object count) for every array but `$1100`, which is first filled with 63 (80,00) pairs
// (the whole array unused) and then (07,00) per object. Each pair is the ROM's LDX #a / LDY #b / STX $80 / STY $80.
// Not ported: 5876 ($7F:0B00,X = 00,00) and 5892 ($7F:0B80,X = FF,FF). 58BC-58CA then DMA-clear $7F:0580-1B3F, which
// covers both, so their net effect is none; the port applies that clear earlier (memLocationLoad), so writing them
// here would leave $7F:0B80 = FF where the ROM leaves 0.
void CtObjSceneDefaults(uint8_t* r, const uint8_t* body16, int nobj) {
  int o;
  if (nobj < 0) nobj = 0;
  if (nobj > 64) nobj = 64;                            // the arrays are 64 objects wide (obj*2, 128 bytes)
  // 5731-5758: `$1180,X` := the object's fn0 pointer, read from the script's own 32-byte-per-object
  // pointer table at $7F2001 (16 word entries; slot 0 is fn0). `CPX $01D9 / BMI` with $01D9 = nobj<<5.
  if (body16)
    for (o = 0; o < nobj; o++) {
      r[0x1180 + o * 2] = body16[o * 32];               // 5740 LDA $7F2001,X / 5744 STA $80
      r[0x1181 + o * 2] = body16[o * 32 + 1];           // 5746 LDA $7F2002,X / 574A STA $80
    }
  for (o = 0; o < nobj; o++) { r[0x1C00 + o * 2] = 0x07; r[0x1C01 + o * 2] = 0x00; }   // 575A-576E
  for (o = 0; o < nobj; o++) { r[0x1000 + o * 2] = 0x04; r[0x1001 + o * 2] = 0x00; }   // 5772-5786 (clears the pause)
  for (o = 0; o < nobj; o++) { r[0x1080 + o * 2] = 0x80; r[0x1081 + o * 2] = 0x80; }   // 578A-579C
  for (o = 0; o < 0x3F; o++) { r[0x1100 + o * 2] = 0x80; r[0x1101 + o * 2] = 0x00; }   // 57A0-57B2 (63, not 64)
  for (o = 0; o < nobj; o++) { r[0x1100 + o * 2] = 0x07; r[0x1101 + o * 2] = 0x00; }   // 57B6-57CA
  for (o = 0; o < nobj; o++) { r[0x1800 + o * 2] = 0x00; r[0x1801 + o * 2] = 0xFF; }   // 57CE-57E2
  for (o = 0; o < nobj; o++) { r[0x1880 + o * 2] = 0x00; r[0x1881 + o * 2] = 0xFF; }   // 57E6-57FA
  for (o = 0; o < nobj; o++) { r[0x1A00 + o * 2] = 0x10; r[0x1A01 + o * 2] = 0x00; }   // 57FE-5812
  for (o = 0; o < nobj; o++) { r[0x1A80 + o * 2] = 0x00; r[0x1A81 + o * 2] = 0x01; }   // 5816-582A
  for (o = 0; o < nobj; o++) { r[0x1780 + o * 2] = 0x00; r[0x1781 + o * 2] = 0x00; }   // 582E-5842
  for (o = 0; o < nobj; o++) { r[0x1C80 + o * 2] = 0x03; r[0x1C81 + o * 2] = 0x00; }   // 5846-585A
  for (o = 0; o < nobj; o++) { r[0x0F80 + o * 2] = 0x80; r[0x0F81 + o * 2] = 0x00; }   // 585E-5872
}
