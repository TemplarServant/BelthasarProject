// move.c — the CT field actor movement tick (the move-step handler). See move.h + field_tick.md.
#include <stdio.h>
#include "romview.h"     // the ROM's small tables from ct_assets.dat (CT_ASSET_TABLE)
#include <stdlib.h>
#include "move.h"
#include "oam.h"   // CtMoveDirAngle (the atan2 heading, C0/ABA2)
#include "collision.h"   // CtCollisionActorMovable (the 5B95 actor-collision)
#include "lz.h"          // ct_lz_decompress = the ported JSL C3/0002 the object loader calls
#include "ct_vbeam.h"    // the anim drain's virtual beam (CtAnimDrainPass, scanline < 0)

// C0/ACFD..AE25: build the velocity ($1900/$1980) from the speed $1A00 and heading `ee`, then the
// OBJ priority ($0C00/$0C01) gated on $0F80 bit 7 (tilemap cell at the tentative position when set,
// else fixed from $0F80, C0/ADD7).
// ONE COMPONENT OF THE VELOCITY, through the SNES hardware multiplier exactly as the ROM drives it.
// `AD08 STA $4202` is the speed, `$4203` the magnitude, and the ROM reads `$4217` -- the HIGH byte of
// the 16-bit product -- i.e. (speed * |t|) >> 8.
//
// ⚠ THE SIGN COMES FROM THE ANGLE INDEX, NOT FROM THE TABLE BYTE: `AD10 CPX #80 / AD12 BCS`, where X
// is the index into $C0:F800. The old helper tested the table VALUE's sign instead. Measured over all
// 256 entries, the two tests disagree at exactly ONE index -- $80, where the byte is $00 -- and
// negating zero yields zero (EOR #FF -> $FF, INC -> $00), so they agree on every RESULT. That is why
// the substitution was invisible, and it is written down here so nobody has to re-measure it.
static int acfdMul(const uint8_t* rom, int sp, int ang) {
  int t = rom[0xF800 + (ang & 0xFF)];
  int neg = (ang & 0xFF) >= 0x80;                         // AD10 CPX #80 / AD3D CPX #80
  int mag = neg ? ((-t) & 0xFF) : t;                      // AD22 EOR #FF / AD24 INC A
  int p = ((sp * mag) >> 8) & 0xFF;                       // AD1B/AD2C LDA $4217
  return neg ? ((-p) & 0xFF) : p;                         // AD2F EOR #FF / AD31 INC A
}

// C0/AC69 -- A PLAIN VELOCITY BUILDER, and op 0x7A's alone (a ROM-wide scan finds exactly one
// `JSR C0/AC69`, at C0/4D4B). It sits immediately BEFORE C0/ACFD and opens with the identical four
// instructions, which invites treating it as the same routine -- it is not. Byte-diffed over its
// whole $94 bytes the two agree for 22 and then differ in 119: AC69 stores its results STRAIGHT
// into $1900/$1980 and stops, while ACFD stages them onward and carries the priority and bounce
// machinery. What IS shared is the arithmetic, which is why acfdMul serves both: the $C0:F800
// table, the sign taken from the angle INDEX (>= $80), and the product's HIGH byte from $4217.
//
// The Y component uses the heading as-is; the X component uses heading + $40 (a quarter turn), and
// each is sign-extended into its high byte. The two `STZ` at AC6B/AC6E are overwritten by this same
// routine on every path, so they cannot be observed from outside; they are here for the record.
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

// =================================================================================================
// C0/ACFD -- THE FIELD VELOCITY + PRIORITY BUILDER, byte-exact. Twelve callers, all JSR from bank
// $C0 (4EBD 4F03 4F5A 4FE0 507A 511F 518A 520E 52B6 5343 5406 54EB); op 0x92 calls it at 4EBD and
// op 0x96 at 4FE0.
//
// WHAT THIS REPLACES. The previous version was a mathematical paraphrase of about a third of the
// routine, and --sweepops caught it the moment that sweep grew to cover an opcode which calls it:
// 30 of 60 op-0x96 cases diverged, and NOT only in scratch -- they reached $01EE, $1600+idx (facing)
// and $1900+idx (X velocity). It was missing three whole mechanisms: the `$1C80,Y` bit-0 gate, the
// BOUNCE retry, and every one of the direct-page staging bytes $01D9/$01DB-$01E1/$01E5.
//
// THE SHAPE. `ACFD SEP #10` makes X/Y EIGHT-BIT for almost the whole body, and `ADF7/AF24 REP #10`
// restores them before the RTS -- which is why op 0x92's `4EC3 LDX $C7` reload works as a 16-bit
// load. The body is written TWICE in the ROM: ACFD..ADF9 is the first pass and AE37..AF26 is a
// near-copy re-entered after a bounce. The copies differ in exactly three ways, all of them visible
// in the bytes, and all three are reproduced below.
//
// THE CELL INDEX IS BUILT IN THE ACCUMULATOR, NOT WITH A SHIFT (AD8D-AD97, and it is the one piece
// of this routine that cannot be guessed): the 16-bit tentative Y is computed but never stored, then
// `SEP #20` leaves its HIGH byte parked in the hidden B half while `LDA $E0` loads the tentative X's
// high byte into the low half, and `REP #30` re-widens the pair into one 16-bit value = tileY*256 +
// tileX. Only then is `AND $1D1E` applied.
//
// SCOPE NOTE: C0/AF30 is NOT part of this routine. Nothing inside ACFD..AF27 branches to it, and it
// is reached by NO static control transfer anywhere in the 4 MB image: zero in-bank JSR/JMP $AF30 in
// bank $C0, and zero `22/5C 30 AF C0` long calls ROM-wide. It only jumps INTO the second copy (its
// two BRLs at AF46/AF4B target AE97), never the other way round, so it cannot pull ACFD's flow into
// itself. Porting it here would be inventing a caller.
//   ⛔ CORRECTION, and the reason this is spelled out: an earlier version of this comment claimed it
//   was "called by JSR from banks $E7 and $FB". That was a SCAN BUG, not a ROM fact -- JSR absolute
//   is BANK-RELATIVE, so the `20 30 AF` bytes found at $E7:2997 and $FB:9DE1 are calls to $E7:AF30
//   and $FB:AF30, two unrelated routines. A cross-bank call would need JSL/JML, and there are none.
// -------------------------------------------------------------------------------------------------
void CtBuildVelocityPriority(uint8_t* r, const uint8_t* rom, int idx) {
  r[0x1900 + idx] = 0x00;                                 // ACFF STZ $1900,X
  r[0x1980 + idx] = 0x00;                                 // AD02 STZ $1980,X
  int sp = r[0x1A00 + idx];                               // AD05 LDA $1A00,X / AD08 STA $4202
  for (int pass = 0; ; pass++) {
    // ---- the two components. $DB is the Y component (angle) and $DD the X (angle + $40). -------
    int ang = r[0x01EE];                                  // AD0B LDX $EE   |  AE3F on the retry
    r[0x01DB] = (uint8_t)acfdMul(rom, sp, ang);            // AD0D..AD32    |  AE41..AE6A
    r[0x01DD] = (uint8_t)acfdMul(rom, sp, (ang + 0x40) & 0xFF);  // AD34..AD5D  |  AE6B..AE96
    // ---- the sign-extension bytes that make the 16-bit adds below work -------------------------
    int dd = r[0x01DD], db = r[0x01DB];
    if (dd == 0) {                                        // AD61 BEQ ->AD6F  |  AE99 BEQ ->AEA7
      // ⚠ THE FIRST DIFFERENCE BETWEEN THE COPIES, and it is two bytes of ROM rather than a matter
      // of opinion: the first copy does `AD6F STZ $DE` on this path, the second goes straight to
      // `AEA7 LDA $DB`. Every branch displacement around it is 2 smaller in the second copy, which
      // is exactly how a missing instruction shows up in the encoding.
      if (pass == 0) r[0x01DE] = 0x00;                    // AD6F only
      if (db == 0) break;                                 // AD73 BEQ ->ADEB  |  AEA9 BEQ ->AF18
    } else r[0x01DE] = (uint8_t)((dd & 0x80) ? 0xFF : 0x00);   // AD65/AD67 or AD6B  |  AE9D/AE9F or AEA3
    r[0x01DC] = (uint8_t)((db & 0x80) ? 0xFF : 0x00);     // AD77..AD7F  |  AEAD..AEB5
    // ---- the tentative position, and the cell it lands in --------------------------------------
    int wx = r[0x1800 + idx] | (r[0x1801 + idx] << 8);    // AD85 LDA $1800,X (A is 16-bit here)
    int tx = (wx + (r[0x01DD] | (r[0x01DE] << 8))) & 0xFFFF;   // AD89 ADC $DD, a 16-bit add
    r[0x01DF] = (uint8_t)tx; r[0x01E0] = (uint8_t)(tx >> 8);   // AD8B STA $DF, a 16-bit store
    int wy = r[0x1880 + idx] | (r[0x1881 + idx] << 8);    // AD8D
    int ty = (wy + (r[0x01DB] | (r[0x01DC] << 8))) & 0xFFFF;   // AD91 ADC $DB -- NOT stored anywhere
    int cell = ((((ty >> 8) & 0xFF) << 8) | ((tx >> 8) & 0xFF)) & 0xFFFF;   // AD93/AD95/AD97, see above
    cell &= (r[0x1D1E] | (r[0x1D1F] << 8));               // AD99 AND $1D1E (16-bit)
    // ---- the two tile-property planes. Both `BF` long,X, so they CROSS the bank boundary, and r[]
    // is one flat image. Read on EVERY arm, before any test -- the old version never wrote them.
    r[0x01E1] = r[0x7000 + cell];                         // ADA8/ADAC  |  ADFA/ADFE  |  AED5/AED9
    r[0x01E5] = r[0x7040 + cell];                         // ADAE/ADB2  |  AE00/AE04  |  AEDB/AEDF
    if (pass == 0) {
      // AD9F LDY $6D / ADA1 LDA $1C80,Y / ADA4 BIT #01 / ADA6 BEQ ->ADFA. The gate does NOT select
      // the priority law -- both arms compute it identically. All it decides is whether the BOUNCE
      // test at ADB4 exists at all: the ADFA arm simply has no BMI.
      if ((r[0x1C80 + idx] & 1) && (r[0x01E5] & 0x80)) {  // ADB4 BMI ->AE27
        r[0x01F8] = (uint8_t)(r[0x01F8] + 1);             // AE29 INC $F8
        r[0x01EE] = CtRomByte(rom, 0x10000, 0xFE00 + r[0x01F8]);   // AE2D LDA $FE00,X / AE31 STA $EE
        continue;                                          // AE34 BRL ->AE37 -- run the second copy
      }
    } else if (r[0x01E5] & 0x80) {
      // ⚠ THE SECOND DIFFERENCE: the retry has NO `$1C80` gate and its BMI goes somewhere else
      // entirely -- AEE1 BMI ->AF27, which ZEROES both components and falls into the store tail.
      // That is what stops the bounce recursing: a second blocked cell kills the step instead of
      // picking another heading.
      r[0x01DB] = 0x00; r[0x01DD] = 0x00;                 // AF29 STZ $DB / AF2B STZ $DD
      break;                                              // AF2D BRL ->AF18
    }
    // ---- priority ------------------------------------------------------------------------------
    int p = r[0x0F80 + idx];                              // ADB6 LDA $0F80,Y  |  AE06  |  AEE3
    if (p & 0x80) {                                       // ADB9 BPL ->ADD7  |  AE09  |  AEE6
      // NOT crossed: $0C01 takes the $7E7040 plane and $0C00 the $7E7000 one. Op 0x8B (C0/4B74) and
      // op 0x8E (C0/4C74) do the OPPOSITE, and both are separately confirmed -- the laws differ.
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

// C0/AA07 one-axis integrate for the tile-clamp path (AA29 normal / AA7E clamp): add the 8-bit signed
// velocity into the 2-byte position; on a tile-boundary crossing, NORMAL crosses (tile +/-1), CLAMP
// stops the counter ($1A01:=0) and stays put (the 1-frame pause before crossing). lo/hi = pos byte addrs.
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

// C0/AA07: the state-3..6 actor position integration (the loop's A82A branch; sibling of AAFD which is
// the state<3 simple add). Path gated by $1B80,X: !=0 -> AAC7 simple 16-bit add (with the $1B81 Y
// sub-pixel accumulator); ==0 -> AA11 per-axis byte integrate with the $7F0B00,X tile-CLAMP described
// above. This clamp is the source of the 1-frame pause at each tile boundary (the --portrun residual).
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

// C0/305D -- the ARRIVAL SNAP, reached from C0/515F when the move has landed on the target tile and
// `$1C81,X & 1` is set. It zeroes both velocities and then walks the object ONE PIXEL PER FRAME
// toward the tile's X CENTRE and Y BOTTOM, arming `$1A01 = 1` each time so exactly one step runs.
//
// `$1800,X` read 8-bit is the sub-tile offset in 1/16 px, so `AND #F0` isolates the PIXEL within the
// tile shifted left four: `$80` is pixel 8 (the centre) and `$F0` is pixel 15 (the bottom). `#10` is
// +1 px in those units, `#F0` is -1. Both axes can be armed in the same call -- the ROM sets vx at
// 307B/3082 and then FALLS THROUGH to the Y test rather than returning.
//
// Returns 1 = SEC (still snapping; the caller yields) / 0 = CLC (already centred, the move is done).
static int ctArriveSnap305D(uint8_t* r, int idx) {
  r[0x1900 + idx] = 0;                                    // 305F STZ $1900,X
  r[0x1980 + idx] = 0;                                    // 3062 STZ $1980,X
  int xs = r[0x1800 + idx] & 0xF0;                        // 3065 LDA $1800,X / AND #F0
  int ys = r[0x1880 + idx] & 0xF0;                        // 306E / 3089 LDA $1880,X / AND #F0
  if (xs == 0x80 && ys == 0xF0) return 0;                 // 3077 CLC -- centred AND on the bottom
  if (xs != 0x80) r[0x1900 + idx] = (uint8_t)((xs < 0x80) ? 0x10 : 0xF0);   // 307B / 3082
  if (ys != 0xF0) r[0x1980 + idx] = (uint8_t)((ys < 0xF0) ? 0x10 : 0xF0);   // 309B / 30A7
  r[0x1A01 + idx] = 0x01;                                 // 3092 / 30A0 / 30AE: ONE frame
  return 1;                                               // SEC
}

int ctSweepSnapCall(uint8_t* r, int idx) { return ctArriveSnap305D(r, idx); }

int CtActorMoveStep(uint8_t* r, const uint8_t* rom, int idx) {
  if (r[0x1A01 + idx] != 0) return 0;                     // counter still running -> yield (C0/5129)
  int ip = r[0x1180 + idx] | (r[0x1181 + idx] << 8);      // 16-bit event-script IP
  int oa = 0x12001 + ip;                                  // $7F2001 + IP
  int tgtX = r[oa + 1], tgtY = r[oa + 2];                 // the move target (tile coords, operands)
  int curX = r[0x1801 + idx], curY = r[0x1881 + idx];     // current tile (world hi-byte)
  if (curX == tgtX && curY == tgtY) {                     // 514E-5156: TILE-INDEX equality
    // 5158 `LDA $1C81,X / BIT #01 / BEQ ->5164`: with bit0 set the ROM does NOT complete on arrival
    // -- it runs the snap and yields (515F JSR C0/305D / 5162 BCS ->516A `TYX / CLC / RTS`) until the
    // object sits on the tile's centre/bottom. bit0 is CLEAR by default ($1C81 is set by op 0x0E), so
    // this path is dormant on every object measured so far -- which is exactly why it went unported
    // twice, here and at ct_vm.c's op-0xA0 arm. "Not seen yet" is not "absent".
    if ((r[0x1C81 + idx] & 1) && ctArriveSnap305D(r, idx)) return 0;        // 515F/5162 -> yield
    return 1;                                             // 5164 TYX/INX x3/SEC: complete, IP += 3
  }
  // C0/5129 stages the four bytes C0/ABA2 reads BEFORE calling it -- 5140 `STA $F3`, 5147
  // `STA $F0` (= $1801,X) and 514C `STA $F1` (= $1881,X), with $F2 set alongside $F3. ABA2 does its
  // own UNSIGNED subtraction on them; handing it signed deltas is what aimed the port backwards at
  // the wrap boundary.
  r[0x01F0] = (uint8_t)curX; r[0x01F1] = (uint8_t)curY;   // 5147 / 514C
  r[0x01F2] = (uint8_t)tgtX; r[0x01F3] = (uint8_t)tgtY;   // 5140 and its sibling
  int ee = CtMoveDirAngle(r, rom);                        // 516D JSR C0/ABA2 -- writes $01EE itself
  // $EE is DP-relative and DP = $0100, so this is $01EE. It read $7E00EE -- a byte nothing owns --
  // and went unnoticed only because buildVelocityPriority used to take the heading as a C
  // PARAMETER. Now that it reads $01EE like the ROM, the wrong address would be a live defect.
  // ABA2 has already written $01EE; only the collision nudge below re-writes it.
  if (CtCollisionActorMovable(r, idx)) { ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee; }  // C0/5170: actor-collision heading nudge
  r[0x1A01 + idx] = r[0x1000 + idx];                      // reload the move counter from the move-duration
  r[0x10B00 + idx] = r[0x1000 + idx];                     // mirror to $7F0B00
  CtBuildVelocityPriority(r, rom, idx);
  return 0;                                               // yield (the step now runs)
}

// C0/5614 -- the SCRIPTED MOVER'S ANIM-MODE SELECTOR, called at step issue by the move opcodes (op
// 0x96 calls it at 4FA5 and 4FE3). It is the mover's counterpart to C0/9E84: where 9E84 picks the
// PLAYER's mode from the d-pad table, this one picks a mode from the magnitude of the velocity the
// step just built.
//
//   5616 cfg = $1780,X
//        cfg == 0        -> fall straight through to the speed test
//        cfg != 0 && != 3 -> RETURN, writing nothing            (561B CMP #03 / 561F RTS)
//        cfg == 3        -> if $1680,X != $FF then $1780,X = 1 and RETURN  (5627)
//                           else $1780,X = 0 and FALL THROUGH   (562D STZ, no branch)
//   5630 speed = |$1900,X| + |$1980,X|, EIGHT-BIT -- the ADC's carry-out is discarded, so
//        0x80 + 0x80 wraps to 0x00 and takes the "standing still" arm. Modelled, not assumed away.
//        ALSO note 5638 `STA $D9` leaves |velX| in $01D9; that byte is a real observable write.
//   5645 speed == 0        -> mode 0   (idle)
//   5647 speed <  0x20     -> mode 1   (walk)
//   5671 speed >= 0x20     -> $1100,X >= 4 ? mode 1 : mode 6    (BCS ->564B is the WALK arm)
//
// Every arm ends in the SAME CMP-guarded tail as C0/9E84's: an unchanged mode resets neither the
// phase nor the timer.
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
    r[0x1780 + idx] = 0x00;                               // 562D STZ $1780,X -- then FALLS THROUGH
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

// C0/568B: on arrival, reset the walk anim -- and it clears THREE bytes, not two.
//   568D LDA $1780,X / 5690 BEQ ->5693 / 5692 RTS      gated: only cfg 0 proceeds
//   5693 LDA $1680,X / 5696 CMP #00 / 5698 BEQ ->56A5  already idle -> nothing
//   569C STA $1680,X = 0 / 569F STZ $1681,X / 56A2 STZ $1601,X
// The 56A2 store was MISSING here. Verified from raw bytes (569A: A9 00 9D 80 16 9E 81 16
// 9E 01 16 60) -- three stores, and the port kept the anim TIMER running across an arrival.
// C0/B096 -- THE 2D BLOCK MOVE behind op 0xE4. Copies a tile rectangle within bank $7E, one row at
// a time, with `MVN $7E,$7E`. The rect is staged in DP by the opcode: $013E/$013F = (x1,y1),
// $0140/$0141 = (x2,y2) INCLUSIVE, $0142/$0143 = (xd,yd). `base` is the plane the caller selected.
//
// ⚠ A ROW IS ADDRESSED AS base + y*$100 + x -- the two halves of each DP pair are the two
// coordinates, so `LDA $3E` read 16-bit IS the address offset already. The row advance is
// ARITHMETIC, not a mask: back up by width+1 and add $100.
static void ctB096(uint8_t* r, int base) {
  r[0x01DC] = 0x00;                                   // B096 STZ $DC -- 8-bit, A is narrow here
  r[0x01DE] = 0x00;                                   // B098 STZ $DE
  r[0x01DB] = (uint8_t)((r[0x0140] - r[0x013E]) & 0xFF);   // B09A/B09D SEC/SBC -- 8-bit, width
  r[0x01DD] = (uint8_t)((r[0x0141] - r[0x013F]) & 0xFF);   // B0A1/B0A4 -- 8-bit, height
  // B0A8 REP #20: everything from here to B0E3 is SIXTEEN-bit.
  int src = ((r[0x013E] | (r[0x013F] << 8)) + base) & 0xFFFF;   // B0AA/B0AD/B0AF TAX
  { int v = src & 0x00FF;                             // B0B0 AND #00FF / B0B3 STA $DF -- 16-bit, and
    r[0x01DF] = (uint8_t)v; r[0x01E0] = (uint8_t)(v >> 8); }   // DEAD here, but the compare sees it
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
    // ⚠ B0CB `DEC $DD` IS SIXTEEN-BIT -- it writes $01DE too. The identical `C6 DD` byte pair in
    // the bit-6/bit-7 blocks below runs with A NARROW and writes one byte. Same instruction, two
    // widths, and a port that makes this one 8-bit loses ten graded writes per call.
    hh = (hh - 1) & 0xFFFF;
    r[0x01DD] = (uint8_t)hh; r[0x01DE] = (uint8_t)(hh >> 8);
    int wd = r[0x01DB] | (r[0x01DC] << 8);
    src = (src - wd - 1 + 0x100) & 0xFFFF;            // B0CD-B0D6 TXA/SBC/DEC/ADC #0100/TAX
    dst = (dst - wd - 1 + 0x100) & 0xFFFF;            // B0D7-B0E0, the same for the destination
  }
}                                                     // B0E3 SEP #20 / B0E5 RTS

// C0/AFAB and C0/B023 -- the two INLINE blocks bits 6 and 7 of the mask run instead of C0/B096.
// They are byte-for-byte identical 114-byte bodies differing in exactly two immediates, and they do
// NOT block-move: they merge ONE BIT per tile into the $7E:30C0 plane, preserving the other seven.
// They also ignore $01F0 entirely -- the plane is baked into the opcodes, so no base is added.
static void ctAF4EMergeBit(uint8_t* r, int andSrc, int andDst) {
  r[0x01DC] = 0x00; r[0x01DE] = 0x00;                 // AFAB / AFAD
  r[0x01E4] = 0x00;                                   // AFAF -- dead, but still a write
  r[0x01DB] = (uint8_t)((r[0x0140] - r[0x013E]) & 0xFF);   // AFB1-AFB6 width
  r[0x01DD] = (uint8_t)((r[0x0141] - r[0x013F]) & 0xFF);   // AFB8-AFBD height
  int x = r[0x013E] | (r[0x013F] << 8);               // AFC1 TAX -- the RAW corner, no `ADC $F0`
  { int v = x & 0x00FF;                               // AFC4 / AFC7 STA $DF -- 16-bit, and LIVE here:
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
      // ⚠ THE MERGE RUNS width+1 TIMES BUT THE INX/INY ONLY width TIMES -- the counter is tested
      // BEFORE it is decremented, so the row ends at rowstart+width, not rowstart+width+1.
      if (r[0x01E3] == 0) break;                      // AFEF / AFF1 BEQ ->AFFA
      r[0x01E3] = (uint8_t)(r[0x01E3] - 1);           // AFF3 DEC A / AFF4
      x = (x + 1) & 0xFFFF; y = (y + 1) & 0xFFFF;     // AFF6 INX / AFF7 INY
    }
    if (r[0x01DD] == 0) break;                        // AFFA / AFFC BEQ ->B01A
    r[0x01DD] = (uint8_t)(r[0x01DD] - 1);             // AFFE `C6 DD` -- EIGHT-bit here, unlike B0CB
    // B000 REP #20, then the row advance MASKS rather than subtracts: keep the new row, restore the
    // saved starting column from $01DF / $01E1.
    x = (((x + 0x100) & 0xFF00) | (r[0x01DF] | (r[0x01E0] << 8))) & 0xFFFF;   // B002-B00C
    y = (((y + 0x100) & 0xFF00) | (r[0x01E1] | (r[0x01E2] << 8))) & 0xFFFF;   // B00D-B017
  }
}

// C0/AF4E -- EIGHT independent bit tests on $0144, applied in ascending order and CUMULATIVE, not
// exclusive: one opcode can move six planes and merge two bits. Bits 0-5 pick a base and call
// C0/B096; bits 6 and 7 run the inline merges above.
static void ctVmAF4E(uint8_t* r) {
  if (r[0x0144] == 0) return;                         // AF4E / AF50 BNE / AF52 RTS -- NO writes
  static const int BASE[6] = { 0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040 };
  for (int b = 0; b < 6; b++) {                       // AF53/AF61/AF6F/AF7D/AF8B/AF99
    if (!(r[0x0144] & (1 << b))) continue;            // `89 xx BIT #imm` sets ONLY Z -> BEQ = clear
    r[0x01F0] = (uint8_t)BASE[b];                     // AF5A STX $F0 -- SIXTEEN-bit, both bytes
    r[0x01F1] = (uint8_t)(BASE[b] >> 8);
    ctB096(r, BASE[b]);                               // AF5C / AF6A / AF78 / AF86 / AF94 / AFA2
  }
  if (r[0x0144] & 0x40) ctAF4EMergeBit(r, 0x01, 0xFE);   // AFA7 -> AFAB, plane bit 0
  if (r[0x0144] & 0x80) ctAF4EMergeBit(r, 0x02, 0xFD);   // B01D -> B023, plane bit 1
}

// C0/3D97 -- op 0xE4, the IMMEDIATE SCRIPTED TILE COPY. Eight bytes, straight-line, CLC. Its staging is byte-for-byte
// op 0xE5's; the difference is the tail. Where 0xE5 latches $0117 bit 5 and lets the vblank servicer do the work later,
// this one does `JSR C0/AF4E` and the copy has already happened by the time the opcode returns.
//
// ⚠ IT DOES NOT WRITE $0145. That byte is 0xE5-only, and it is NOT a reduced copy mask -- it is a post-copy BG
// re-upload selector. Both opcodes hand the FULL 8-bit $0144 to AF4E.
//
// `script` is the image the IP indexes ($7F:2001 in the ROM): CtFieldRunnerOne keeps it in WRAM, the live VM in its
// own buffer (handed in shifted by the pointer table, so `ip` is the live IP either way).
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

// C0/3DC5 -- op 0xE5, STAGE A TILE COPY. EIGHT bytes: three SIXTEEN-BIT operands into $013E, $0140 and $0142, then one
// byte into $0144 whose low three bits are mirrored to $0145 -- the ONLY writer of $0145 in the ROM, and not a copy
// mask: C0/179A reads it as the STYLE of the BG re-upload that follows the copy. It does NOT perform the copy:
// `3DED LDA #20 / 3DEF TSB $17` sets bit 5 of $0117 and the main loop's C0/1770 does the work (C0/1790 JSR C0/AF4E) at
// the start of the next frame. TSB is test-and-SET (memory |= A). It exits CLC -- staging the request ends the visit.
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

// C0/30B3 -- THE $1C81 BIT-1 ALIGNMENT SNAP, the sibling of C0/305D and the one helper this whole
// mover family needed that did not exist yet. Returns 1 for the ROM's SEC (still aligning, so the
// caller yields) and 0 for CLC (aligned, caller completes).
//
// The target comes from $01E9/$01EA, which the CALLER put there with a 16-bit `STX $E9`.
// Re-decoded from raw bytes: tools/dis.py desyncs inside this routine at 313C.
//
// Both coordinate comparisons are on the SIXTEEN-BIT position masked to $FFF0 -- i.e. the tile,
// with the sub-tile thrown away -- and $01D9/$01DA is used as the scratch for the SELF side, so it
// ends holding sy on every path that gets past the X test.
// The $C7:0004 sound-mailbox round-trip, CLOSED-GATE CONTRACT ONLY.
//
// $C7:0004 is `4C 40 01` = JMP $C7:0140. That routine pushes everything, sets DB = $00 and
// DP = $1E00, and its FIRST act is `70157 LDA $05 / 70159 BPL +$37` -- a branch on bit 7 of
// $1E05 whose target is 0x70192, the epilogue. So with $1E05 bit 7 CLEAR the entire driver is
// skipped, and the only instruction that runs is the epilogue's `70194 STZ $00`, an 8-bit store
// clearing ONLY the command byte $1E00. $1E01-$1E03 are left dirty on purpose -- that is what
// makes $1E03 STICKY between opcodes.
//
// $1E05 is the driver's own ready/re-entrancy latch: $C7:013B and $C7:0614 set it to $E0 ("driver
// uploaded and idle"), and $C7:01B3 parks the SONG ID in it (bit 7 clear) for the duration of a
// load, which slams this gate shut so a second command cannot interleave.
//
// ⚠ WITH THE GATE OPEN this returns 0 and the caller TRAPS rather than guessing. The open path
// goes to $C7:061C, which writes $2143/$2142/$2141/$2140 and then SPINS on `CMP $2140` waiting
// for the SPC700 to echo the command -- bounded at $400 tries, and on timeout it re-sends all
// four ports and retries UNBOUNDED. There is no way to model that without the audio core in the
// loop, and the port does not pretend otherwise. The real driver lives in src/audio.c and is
// graded separately by --audiotest and --songload.
static int ctVmSoundPost(uint8_t* r) {
  if (r[0x1E05] & 0x80) return 0;                 // 70157 LDA $05 / 70159 BPL -- gate OPEN
  r[0x1E00] = 0x00;                               // 70194 STZ $00, 8-bit: the command byte only
  return 1;
}

// ---- THE $C1 CALL GATE, C1/CFC2 (entry $C1:8003 = `4C C2 CF` = JMP C1/CFC2) -----------------
//
// Recorded for a long time as a BLOCKER ("bank $C1, items"). It is nothing of the kind, and it is
// not a gated service like $C7:0140 either: it is a bare prologue, an indexed JSR, and an epilogue,
// with NO branch anywhere before the dispatch. The whole $C1 side is 371 bytes -- dispatcher (39) +
// the six-entry table at $C1:D126 (12) + six contiguous leaves (317) -- with ZERO JSL, ZERO long
// addressing, ZERO MVN and ZERO hardware. Nothing busy-waits. Every loop is bounded by `CPX #0100`.
//
// The prologue sets DB = $7E and DP = $0000, so every address below is a plain $7E offset, and it
// does `A9 00 00 LDA #0000 / SEP #20` -- SO EVERY LEAF IS ENTERED WITH A = 0. Y is the argument and
// is never touched. `08 PHP` / `28 PLP` bracket the whole thing, so the leaf's FLAGS DO NOT REACH
// THE CALLER: only A comes back.
//
// Two WRAM regions and one scratch triple are all it owns: $7E:2400-24FF (item ids), $7E:2500-25FF
// (item counts), $7E:2C53-2C55 (24-bit gold), and $AEF7/$AEF9 (16-bit) + $AEFB (8-bit).
int ctVmC1Gate(uint8_t* r, int leaf, int y) {          // declared in move.h -- ct_vm.c calls this SAME body
  int i;
  r[0xAEF7] = (uint8_t)y; r[0xAEF8] = (uint8_t)(y >> 8);   // `STY $AEF7` is SIXTEEN-BIT and it is
  if (y == 0) {                                            // the FIRST thing every leaf does, so
    // Every leaf's zero-argument arm returns before touching anything else, and the split is
    // ITEMS vs GOLD, not "two of them": re-disassembled leaf by leaf, the `CPY #0000 / BEQ` lands on
    //   leaf 0 C1/CFE9 -> D002 LDA #00     leaf 3 C1/D086 -> D09F LDA #FF
    //   leaf 1 C1/D005 -> D053 LDA #00     leaf 4 C1/D0A2 -> D0E5 ... LDA #FF
    //   leaf 2 C1/D056 -> D083 LDA #00     leaf 5 C1/D0ED -> D123 LDA #FF
    // ⚠ THIS USED TO READ `leaf == 3 || leaf == 4` while its own comment cited D0E5 and D123 --
    // i.e. leaves 4 and 5. SubGold's zero arm was the one actually left out, and NOTHING GRADES IT:
    // op 0xCE is leaf 5's only caller and it discards A (C0/387E is `LDX $C7 / SEC / RTS`), so the
    // return is invisible to --sweepops' whole-image compare and to the game alike. Corrected from
    // the disassembly rather than from a test going red, and recorded as ungraded on purpose.
    return (leaf >= 3) ? 0xFF : 0x00;                      // even the rejected calls commit $AEF7
  }
  switch (leaf) {
    case 0:   // C1/CFE9 HasItem -- returns the MATCHED ID (never 0 here), or 0 if absent
      for (i = 0; i < 0x100; i++)
        if (r[0x2400 + i] == r[0xAEF7]) return r[0x2400 + i];   // CFF7 CMP $AEF7 is EIGHT-BIT, so
      return 0x00;                                              // only Y's LOW byte is compared
    case 1: {  // C1/D005 AddItem
      int free = 0, got = 0;                                // D010 STX $AEF9 = 0 / D013 STZ $AEFB
      r[0xAEF9] = 0x00; r[0xAEFA] = 0x00; r[0xAEFB] = 0x00;
      for (i = 0; i < 0x100; i++) {
        if (r[0x2400 + i] == 0 && !got) {                   // D016/D019/D01B/D01E: capture the
          free = i; got = 1;                                //   FIRST free slot, once
          r[0xAEF9] = (uint8_t)i; r[0xAEFA] = (uint8_t)(i >> 8);   // D020 STX $AEF9 (16-bit)
          r[0xAEFB] = 0x01;                                 // D023 INC $AEFB
        }
        if (r[0x2400 + i] == r[0xAEF7]) {                   // D029 -- ALREADY HELD
          if (r[0x2500 + i] >= 0x63) return 0x00;           // D049 CMP #63 / BCS -- 99 is the cap
          r[0x2500 + i] = (uint8_t)(r[0x2500 + i] + 1);     // D04D INC A / D04E
          return r[0x2500 + i];
        }
      }
      // ⚠ A ROM QUIRK, PORTED VERBATIM. $AEF9 was set to 0 at D010 and is only re-stored when a
      // free slot is found, so on a FULL inventory D034 `LDX $AEF9` reloads ZERO and the new item
      // CLOBBERS SLOT 0. That is what the engine does, so that is what this does.
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
        return 0xFF;                                        //   slot is FREED, not just emptied
      }
      return 0x00;                                          // D06F BRA -- absent
    case 3: {  // C1/D086 HasGold -- READ-ONLY apart from the $AEF7 spill above
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
      // ⚠ THE CLAMP IS TWO-STAGE AND IT IS NOT `min(gold, 9999999)`. $2C55 is forced to $98 even
      // when it was already LARGER, and only then is $2C53 tested. So a pre-existing out-of-range
      // value does NOT become 9,999,999: $2C55=$99 with $2C53=$0000 clamps to $98:0000 = 9,961,472.
      r[0x2C55] = 0x98;                                     // D0C9/D0CB -- UNCONDITIONAL
      if ((r[0x2C53] | (r[0x2C54] << 8)) < 0x9680) return 0xFF;   // D0D3 CMP #9680 / BCC
      r[0x2C53] = 0x7F; r[0x2C54] = 0x96;                   // D0D8/D0DB LDA #967F
      return 0x00;                                          // D0DE LDA #0000 / D0E3 BRA -> RTS
    }
    default: { // C1/D0ED SubGold
      int g = ((r[0x2C53] | (r[0x2C54] << 8)) - y) & 0xFFFF;      // D0F7-D0FB (16-bit SBC)
      r[0xAEF9] = (uint8_t)g; r[0xAEFA] = (uint8_t)(g >> 8);      // D0FE STA $AEF9 -- ALWAYS
      if ((r[0x2C53] | (r[0x2C54] << 8)) < y) {                   // D101 BCS -- a borrow happened
        if (r[0x2C55] == 0) return 0x00;                          // D10B BEQ -- broke, no change
        r[0x2C55] = (uint8_t)(r[0x2C55] - 1);                     // D10D DEC $2C55
      }
      r[0x2C53] = (uint8_t)g; r[0x2C54] = (uint8_t)(g >> 8);      // D118/D11B
      return 0xFF;
    }
  }
}

// ---- THE $C1 ITEM + GOLD FAMILY -- C0/3780 (0xC9), 37B1 (0xCA), 37C7 (0xC7), 37E3 (0xCB),
// 3827 (0xCC), 385B (0xCD), 3874 (0xCE). All seven stage an argument in Y, put a LEAF INDEX in A
// and `JSL $C1:8003`; see ctVmC1Gate for why that was never a blocker.
//
// ⚠ THEIR LENGTHS ARE NOT UNIFORM AND THE OPERAND WIDTHS ARE NOT EITHER, which is the whole
// reason they cannot share one arm blindly:
//   0xCA/0xCB/0xC7  ip+2   -- `LDX $C7 / SEC / RTS` with NO trailing INX
//   0xCD/0xCE       ip+3   -- same tail, but the operand is SIXTEEN-BIT
//   0xC9            ip+3 or BRANCH   0xCC   ip+4 or BRANCH
// Carry is SET on every exit of all seven.
//
// 0xC7 is 0xCA with an indirection: `37DD LDA $7F0200,X` with X = operand*2 fetches the id out of
// the scene array instead of the script, then `37E1 BRA ->37BC` lands in 0xCA's own tail -- so it
// shares 0xCA's leaf, its spill and its length.
//
// ⚠ WHY THE SCRIPT COMES IN AS A POINTER instead of being read out of `ram` like every other
// handler in this file. CtFieldRunnerOne runs on the ROM's own addresses, where the running script
// IS at $7F:2001 -- so it passes `ram + 0x12001` and nothing changes for it. The live event VM in
// ct_vm.c keeps the scene body in host memory and models bank $7F only up to $7F:03FF (ct_vm.c
// WM_LEN), so it has no $7F:2000 image to read; it passes its own body pointer. EVERYTHING ELSE --
// the $7F:0200 locals read, the $01C7/$01D9 spills, the inventory and the gold -- is a WRAM address
// and is identical on both sides, because ct_vm.c's Mem is the same flat 128 KB layout (its
// ct_wram_layout_assert is what guarantees $7F:0200 == offset 0x10200 for both callers).
// Returns 1 if `op` belongs to the family (and writes the new IP to *outIp), 0 otherwise.
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
    // 3792/383C `CMP #00 / BEQ`: BRANCH WHEN THE LEAF RETURNED ZERO -- item ABSENT for 0xC9, gold
    // NOT affordable for 0xCC. The displacement is read from the spill and added to it, so the
    // target is spill + disp, not ip + disp.
    if (a == 0) {
      int disp = script[spill];                                  // 379D / 3847
      r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;               // 37A6 STA $D9 (16-bit)
      *outIp = (spill + disp) & 0xFFFF; return 1;                // 37AA ADC $D9
    }
    *outIp = (spill + 1) & 0xFFFF; return 1;                     // 3796 LDX $C7 / INX / SEC
  }
  *outIp = spill; return 1;                                      // 37C3 LDX $C7 / SEC -- no INX
}

// ---- THE $C2 PARTY GATE, C2/8C36 (entry $C2:8004 = `80 04` BRA -> `20 36 8C` JSR) ---------------
//
// The fifth and last of the recorded "blockers", and like the other four it is not one. Eleven
// opcodes route through it -- 0xD2 (489 sites), 0xD3 (127), 0xD4 (127), 0xD0 (66), 0xCF (44),
// 0xF8 (25), 0xD7 (12), 0xD1 (4), 0xF9 (1), 0xFA (1) = 896, plus 0xD5 which has ZERO sites in the
// 504 scenes and is left unported rather than guessed at. There is no hardware, nothing waits, and
// the only long addressing is three ROM table reads and two MVN blocks.
//
// The prologue sets DB = $7E and DP = $0400, so `$C0` is $7E:04C0 and `$C9` is $7E:04C9, while
// every `$2980`-style absolute is a plain $7E offset. `SEP #30` makes A/X/Y EIGHT bits for the
// whole gate, which is why `STY $C0` / `STX $C9` truncate their arguments.
//
// ⚠ `08 PHP` at 8C38 / `28 PLP` at 8C57 BRACKET THE ENTIRE CALL, so no leaf's carry ever reaches
// the caller: every one of these opcodes exits with the carry it had at handler entry, and that is
// ZERO (5AAD `ASL A` is a SIXTEEN-bit shift of a value masked below $100, so the opcode's bit 7
// lands in bit 8). Ops 0xD0/0xD1/0xD3/0xF8/0xF9/0xFA therefore END THE VISIT. Ops 0xCF/0xD2/0xD7
// set carry themselves after the call; op 0xD4's carry is its own last `CMP $D9`.
//
// ⚠ AND THE GATE DOES NOT PRESERVE X. It exits with X = $0030 -- always, on every path -- because
// the epilogue's last routine C2/82E1 runs its record loop to X = $2830 unconditionally, and
// C2/834D's `PLP` then narrows X back to 8 bits. That is visible: op 0xF8 calls the gate twice, so
// its second `STX $C9` writes $30 where the first wrote $F0 (the dispatch's opcode*2).

// C2/8CBE -- the reserve INSERT loop, shared by leaves 1 and 2. Pushes `a` into slot 3 and walks the
// displaced value down slots 4..8, stopping as soon as the value it displaced was itself EMPTY.
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

// C2/8CCF (limit 9 = the whole roster) and C2/8C75 (limit 3 = the active party) are ONE routine:
// 8C75 is `LDA #03 / BRA ->8CD1`, entering 8CCF one instruction in. Returns 0 when the id in $04C0
// is found and $FF when it is not (`DEX` from an 8-bit zero), leaving the index in Y.
// ⚠ THE LIMIT IS A REAL WRITE: `8CD1 STA $C2` puts 3 or 9 into $7E:04C2 on every single call,
// including the 489 pure queries of op 0xD2. And the scan is a DO-while -- slot 0 is always read.
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

// C2/8C93 -- the shared PLACE tail: OR the PC's bit into $7E:29AF and store the id in slot Y.
// $FF:F9BB is the eight-entry bit table 80 40 20 10 08 04 02 01, and `LDX $C0` is EIGHT bits, so a
// PC id above 7 indexes past it into code. Read it out of the ROM exactly as the ROM does.
static int ctVmC2Place(uint8_t* r, const uint8_t* rom, int y) {
  r[0x29AF] = (uint8_t)(r[0x29AF] | (rom ? rom[0x3FF9BB + r[0x04C0]] : 0));   // 8C95 LDA $FFF9BB,X / 8C99 TSB (no-ROM run: none)
  r[0x2980 + y] = r[0x04C0];                             // 8C9C LDA $C0 / 8C9E STA $2980,Y
  return r[0x04C0];
}

// C2/8CF7 (leaf 5) -- mark the PC's roster slot EMPTY (bit 7), if it is in the roster at all.
static int ctVmC2Leaf5(uint8_t* r) {
  int y, f = ctVmC2Find(r, 9, &y);
  if (f != 0) return f;                                  // 8CFA BNE -> RTS with A = $FF
  int a = r[0x2980 + y] | 0x80;                          // 8CFC/8CFF ORA #80
  r[0x2980 + y] = (uint8_t)a;                            // 8D01
  return a;
}

// C2/8C79 (leaf 1) -- ADD TO THE ACTIVE PARTY. Drops the PC from the roster first, then takes the
// first EMPTY active slot; if all three are full it EVICTS slot 2 and pushes the evicted id into
// the reserve.
static int ctVmC2Leaf1(uint8_t* r, const uint8_t* rom) {
  ctVmC2Leaf5(r);                                        // 8C79 JSR C2/8CF7
  for (int y = 0; y < 3; y++)                            // 8C7C LDY #00 / 8C84 CPY #03
    if (r[0x2980 + y] & 0x80) return ctVmC2Place(r, rom, y);       // 8C81 BMI ->8C93
  int old = r[0x2982];                                   // 8C88 LDX $2982
  r[0x2982] = r[0x04C0];                                 // 8C8B/8C8D
  return ctVmC2Insert(r, old);                           // 8C90 TXA / 8C91 BRA ->8CBE
}

// C2/8CA2 (leaf 2) -- REMOVE FROM THE ACTIVE PARTY: shift the later members up, mark slot 2 empty,
// then push the removed id back into the reserve.
static int ctVmC2Leaf2(uint8_t* r) {
  int y, f = ctVmC2Find(r, 3, &y);
  if (f != 0) return f;                                  // 8CA5 BNE ->8CCE with A = $FF
  while (y != 2) { r[0x2980 + y] = r[0x2981 + y]; y++; } // 8CA7 CPY #02 / 8CAB-8CB2
  r[0x2982] = (uint8_t)(r[0x2982] | 0x80);               // 8CB4-8CB9
  return ctVmC2Insert(r, r[0x04C0]);                     // 8CBC LDA $C0 -> the insert loop
}

// C2/8CE5 (leaf 4) -- ADD TO THE RESERVE, and only if the PC is nowhere in the roster already.
static int ctVmC2Leaf4(uint8_t* r, const uint8_t* rom) {
  int y, a = 0;
  if (ctVmC2Find(r, 9, &y) == 0) return 0x00;            // 8CE8 BEQ ->8CF6
  for (y = 3; y < 9; y++) {                              // 8CEA LDY #03 / 8CF2 CPY #09
    a = r[0x2980 + y];                                   // 8CEC LDA $2980,Y
    if (a & 0x80) return ctVmC2Place(r, rom, y);         // 8CEF BMI ->8C93
  }
  return a;                                              // no empty slot: A = the last one read
}

// C2/8D1C -- one PC record. +$05 = base max HP (16-bit), +$2A = the accessory, +$03 = effective max
// HP. Accessory $A1 adds 50%, $A0 adds 25%, anything else adds nothing (the ROM zeroes A and still
// runs both LSRs). ⚠ The 999 clamp's `>=` and `>` are byte-identical for every input, because at
// equality the store puts back the $03E7 A already held -- no seed can tell them apart.
static void ctVmC28D1C(uint8_t* r, int y) {
  int acc = r[y + 0x2A];                                 // 8D1C LDA $002A,Y / 8D1F AND #00FF
  int base = r[y + 5] | (r[y + 6] << 8);                 // 8D23 LDA $0005,Y, SIXTEEN bits
  int bonus = (acc == 0xA1) ? (base >> 1)                // 8D26 CPX #00A1 / BEQ ->8D34 (one LSR)
            : (acc == 0xA0) ? (base >> 2)                // 8D2B CPX #00A0 / BEQ ->8D33 (two LSRs)
            : 0;                                         // 8D30 LDA #0000, then both LSRs
  int v = (bonus + base) & 0xFFFF;                       // 8D35 CLC / 8D36 ADC $0005,Y
  if (v >= 0x03E7) v = 0x03E7;                           // 8D39 CMP #03E7 / 8D3C BCC
  r[y + 3] = (uint8_t)v; r[y + 4] = (uint8_t)(v >> 8);   // 8D41 STA $0003,Y, SIXTEEN bits
}

// C2/8D05 (leaf 6) -- recompute max HP on all SEVEN PC records at $7E:2600, stride $50, and
// C2/8D45 (leaf 7) -- restore current MP (+$07) from max MP (+$09) on the same seven.
static void ctVmC2Leaf6(uint8_t* r) {
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) ctVmC28D1C(r, rec);   // 8D13 CPY #2830 / BCC
}
static void ctVmC2Leaf7(uint8_t* r) {
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) r[rec + 7] = r[rec + 9];   // 8D4C/8D4F, 8-bit
}

// C2/8E1E (leaf 12) -- HOW MANY OF ITEM $04C9 DO I HOLD. C2/87FA walks the same $7E:2400 ids /
// $7E:2500 counts pair the $C1 side owns and returns carry CLEAR with the slot in $04C7. ⚠ A slot
// whose id MATCHES but whose count is ZERO is skipped, not accepted (`8804 BNE`).
static int ctVmC2Leaf12(uint8_t* r) {
  int id = r[0x04C9], x = 0;                             // 8E1E LDA $C9 / 87FA LDX #00
  do {
    if (r[0x2400 + x] == id && r[0x2500 + x] != 0) {     // 87FC CMP / 8801 LDA $2500,X / 8804 BNE
      r[0x04C7] = (uint8_t)x; r[0x04C8] = 0x00;          // 880E STX $04C7 / 8811 STZ $04C8
      return r[0x2500 + x];                              // 8E27 LDX $C7 / 8E29 LDA $2500,X
    }
    x = (x + 1) & 0xFF;                                  // 8809 INX / 880A BNE -- X is EIGHT bits
  } while (x != 0);
  return 0x00;                                           // 880C SEC -> 8E23 LDA #00 / 8E25 BCS
}

// C2/F5ED -- map a PC id to its equipped weapon's three-colour palette block in bank $D1. The
// hardware multiplier does pc*$50; $7E:2629 + that is the weapon slot of the record.
static int ctVmC2F5ED(uint8_t* r, const uint8_t* rom, int romLen, int pc) {
  int w = r[(0x2629 + pc * 0x50) & 0xFFFF];              // F601 $4216 / F606 LDA $2629,X / AND #00FF
  r[0x041B] = (uint8_t)w; r[0x041C] = 0x00;              // F60C STA $041B -- SIXTEEN bits
  int v = ((size_t)(0xD6CEC + w) < (size_t)romLen) ? rom[0xD6CEC + w] : 0;   // F610 LDA $CD6CEC,X
  return 0x114B00 + v * 6;                               // F617-F61F: v*2 + v*4 + $4B00, bank $D1
}

// $FF:F958 -- the five TRIPLE-TECH availability flags. Recorded as the one real risk in the chain
// and measured harmless: no hardware, no wait, three ROM tables, and it owns $7E:04A0-$04A8.
// ⚠ $04A1 and $04A2 are written too, not just $04A0/$04A3/$04A4-$04A8 -- whenever a combination's
// required-member mask is satisfied. A capture that showed only 19 writes was a property of that
// savestate's party, not of the routine.
static void ctVmC2F958(uint8_t* r, const uint8_t* rom) {
  int have = 0xFE;                                       // F960 LDA #FE / F962 STA $A3
  for (int y = 8; y >= 0; y--) {                         // F964 LDY #08 / F971 DEY / F972 BPL
    int pc = r[0x2980 + y];                              // F966 LDX $2980,Y
    if (pc & 0x80) continue;                             // F969 BMI
    have &= ~rom[0x3FF9BB + pc];                         // F96B LDA $FFF9BB,X / F96F TRB $A3
  }
  r[0x04A3] = (uint8_t)have;
  r[0x04A0] = 0x00;                                      // F974 STZ $A0
  do {
    int i = r[0x04A0];                                   // F976 LDX $A0
    r[0x04A4 + i] = 0x00;                                // F978 STZ $A4,X -- before any test
    if (!(rom[0xC2983 + i] & have)) {                    // F97A LDA $CC2983,X / AND $A3 / F980 BNE
      int x = i * 3, ok = 0;                             // F982 ASL A / ADC $A0 -> i*3
      r[0x04A1] = 0x03;                                  // F988 LDA #03 / F98A STA $A1
      for (;;) {
        int t = (rom[0xC2953 + x] - 1) & 0xFF;           // F98C LDA $CC2953,X / F990 DEC A
        r[0x04A2] = (uint8_t)t;                          // F991 STA $A2
        if (!(rom[0x3FF9BB + (t & 7)] & r[0x2837 + (t >> 3)])) break;   // F99D / F9A2 / F9A5 BEQ
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
// Returns its terminal Y, which the gate does NOT restore -- see ctVmC2Gate.
static int ctVmC2_82E1(uint8_t* r) {
  r[0x2857] = 0x00; r[0x2858] = 0x00;                    // 82E4 STZ $2857 -- SIXTEEN bits
  for (int k = 0; k < 3; k++) r[0x2859 + k] = r[0x2857 + k];   // 82F0 MVN $7E,$7E with C = 2
  int y = 0x285C;                                        // where the MVN leaves Y
  for (int rec = 0x2600; rec < 0x2830; rec += 0x50) {
    int a = r[rec + 0x2A];                               // 82FB LDA $002A,X
    if (a < 0xAE || a >= 0xB3) continue;                 // 82FE CMP #AE / BCC ; 8302 CMP #B3 / BCS
    y = a;                                               // 8306 TAY -- and it OUTLIVES the routine
    if (r[0x03F6 + a] == 0) continue;                    // 8307 LDA $03F6,Y / 830A BEQ
    r[0x27A9 + a] = 0x80;                                // 830C LDA #80 / 830E STA $27A9,Y
  }
  return y;
}

// C2/834D -- THE EPILOGUE, and it is what makes this family expensive: it runs after EVERY leaf,
// including the 489 pure membership queries of op 0xD2. A test that writes nothing of its own still
// repaints three OBJ palette rows from bank $D1, rebuilds five triple-tech flags and rescans seven
// PC records. ⚠ An EMPTY party slot is skipped WITHOUT ADVANCING the destination, so a one-member
// party paints row $21BA and leaves $21DA/$21FA alone.
static int ctVmC2Epilogue(uint8_t* r, const uint8_t* rom, int romLen) {
  int y = 0x21BA;                                        // 8358 LDY #21BA
  for (int x = 0; x < 3; x++) {                          // 835B LDX #0000 / 8376 CPX #0003
    if (r[0x2980 + x] & 0x80) continue;                  // 8363 BMI ->8375
    int src = ctVmC2F5ED(r, rom, romLen, r[0x2980 + x]); // 8366 JSR C2/F5ED -> X, and A = 5
    for (int k = 0; k < 6; k++)                          // 8369 MVN $7E,$D1 with C = 5 -> SIX bytes
      r[(y + k) & 0xFFFF] = ((size_t)(src + k) < (size_t)romLen) ? rom[src + k] : 0;
    y = (y + 6 + 0x1A) & 0xFFFF;                         // MVN leaves Y past the block; 8371 ADC #1A
  }
  if ((size_t)romLen > 0x3FF9C3) ctVmC2F958(r, rom);  // 837B JSL $FF:F958 (its $FF:F9BB / $CC:29xx reads; a no-ROM run has none)
  return ctVmC2_82E1(r);                                 // 837F JSR C2/82E1
}
// The LIVE VM's handle on the epilogue. Its own op 0xD2/0xD3/0xD4 arms edit the party in place and never
// reach C2/8C36, so without this the three weapon rows ($7E:21BA/$21DA/$21FA, colours 13-15 of OBJ slots
// 5/6/7) were never painted -- which is exactly why a PC's sword drew BLACK in the field.
void CtVmC2Epilogue(uint8_t* r, const uint8_t* rom, int romLen) { (void)ctVmC2Epilogue(r, rom, romLen); }
static int ctVmC2Gate(uint8_t* r, const uint8_t* rom, int romLen, int leaf, int y, int x, int* exitY);
// THE LOCATION LOAD'S TAIL, C0/56CB-56D3: `TDC / XBA / LDA #0B / JSL C2/8004` right after the slot-1 runner C0/56C8 --
// C2/8004 `BRA 800A / JSR C2/8C36`, leaf 11 (C2/8C5A, an RTS), i.e. the gate for its EPILOGUE alone: every load repaints
// the three weapon rows. MEASURED (`--monitor 7E21BA:6 --writer` over the attract): C2:836C rewrites $21BA at f6581 (the
// 1B1 load after the race, whose C3:0636 setup had overwritten the row at f6171 with 152EEE0C2A20), f6718, f6759 (06B).
// `x` is C0/595C's exit X (the slot-1 pointer word, $7F:2003, when the runner dispatched nothing) -> $04C9; `y` the
// init runner's last `TXY` (C0/5906, the offset of the last op it dispatched) -> $04C0. Neither is read before the
// next gate call overwrites it (the leaves that read $04C0 are entered with their own Y).
void CtVmC2LoadTail(uint8_t* r, const uint8_t* rom, int romLen, int x, int y) {
  (void)ctVmC2Gate(r, rom, romLen, 11, y, x, NULL);     // 56CD LDA #0B -> 8004 -> 8C36
}

// C2/8C36 itself. `x` is the X register at the JSL, which each handler leaves somewhere different --
// op 0xD7 puts the ITEM ID there, op 0xF8 leaves the dispatch's opcode*2, op 0xD4 leaves whichever
// object index its last `TAX` produced. All of it lands in $04C9, and leaf 12 READS it.
// ⚠ AND IT PRESERVES NEITHER X NOR Y: `8C36 PHB / PHD / PHP` saves the bank, the direct page and
// the flags, and nothing else. `exitY` reports the Y it leaves, which C2/82E1's `8306 TAY` makes
// DATA-DEPENDENT -- $285C when no PC wears a triple-tech relic, otherwise that relic's id. Only op
// 0xF8 can see it, because only op 0xF8 calls the gate twice.
static int ctVmC2Gate(uint8_t* r, const uint8_t* rom, int romLen, int leaf, int y, int x,
                      int* exitY) {
  r[0x04C0] = (uint8_t)y;                                // 8C44 STY $C0 -- SEP #30, EIGHT bits
  r[0x04C9] = (uint8_t)x;                                // 8C46 STX $C9 -- ditto
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
    default: a = ctVmC2Leaf12(r); break;                 // 8E1E (leaf 12; leaves 8-10 = 8D63/8D6A/8D8F unported)
  }
  r[0x04C1] = (uint8_t)a;                                // 8C4D STA $C1
  // ⚠ LEAVES 6 AND 7 LEAK THE ACCUMULATOR WIDTH. Both open with `REP #30` and end `LDA #0000 / RTS`
  // without ever restoring it, so the gate's own `85 C1` executes SIXTEEN bits wide and zeroes
  // $04C2 as well -- the same $04C2 that leaves 0 and 3 use as their scan limit.
  if (wide) r[0x04C2] = (uint8_t)(a >> 8);
  int ey = ctVmC2Epilogue(r, rom, romLen);               // 8C4F JSR C2/834D
  if (exitY) *exitY = ey;
  return a & 0xFF;                                       // 8C52 LDA #00 / XBA / 8C55 LDA $C1
}

// C0/39E1 and C0/39F4 -- the two OBJ palette-row shifts op 0xD4 runs when it compacts the party.
// $20 bytes each: row $21C0 -> $21A0, then row $21E0 -> $21C0.
static void ctVm39E1(uint8_t* r) { for (int k = 0; k < 0x20; k++) r[0x21A0 + k] = r[0x21C0 + k]; }
static void ctVm39F4(uint8_t* r) { for (int k = 0; k < 0x20; k++) r[0x21C0 + k] = r[0x21E0 + k]; }

// EXPORTED (declared in move.h) because the live VM's own move family (ct_vm.c moveOp) reaches this
// same arm and used to carry a PARAPHRASE of it. There is one C0/30B3 and this is it.
int CtVmMoveSnap30B3(uint8_t* r, int idx) {
  r[0x1900 + idx] = 0x00;                                // 30B5 STZ -- EIGHT-bit, REP #20 is at 30BB
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
  r[0x01D9] = (uint8_t)sy; r[0x01DA] = (uint8_t)(sy >> 8);        // 30D9 / 310D -- OVERWRITES sx
  int ty = (r[0x1880 + tgt] | (r[0x1881 + tgt] << 8)) & 0xFFF0;   // 30DD / 3111
  if (ty == sy) {
    if (xEqual) return 0;                                // 30E7 SEP #20 / 30E9 CLC -- fully aligned
    r[0x1A01 + idx] = 0x01;                              // 311F / 3121 -- X moved, Y already right
    r[0x10B00 + idx] = 0x01;                             // 3124
    return 1;                                            // 3128 SEC
  }
  // 312A, reached from BOTH 30E5 and 3119. `BCC ->3140`: carry clear means ty < sy.
  r[0x1980 + idx] = (uint8_t)(ty < sy ? 0xF0 : 0x10);    // 3142 / 312E
  r[0x1A01 + idx] = 0x01;                                // 314B / 3137
  r[0x10B00 + idx] = 0x01;                               // 314E / 313A
  return 1;                                              // 3152 / 313E SEC
}

static void moveArriveAnim(uint8_t* r, int idx) {
  if (r[0x1780 + idx] != 0) return;                      // 5690
  if (r[0x1680 + idx] == 0) return;                      // 5698
  r[0x1680 + idx] = 0;                                   // 569C
  r[0x1681 + idx] = 0;                                   // 569F
  r[0x1601 + idx] = 0;                                   // 56A2
}

// C0/5228's body now lives in ctVmMoveActorOp above, shared with C0/52D3 and C0/536B.
// The old ctMoveToActor paraphrase that used to sit here was NOT that routine -- see the
// commit that replaced it for the list of what it dropped.

// C0/C98A. Two routines welded together: the ENQUEUE (C98A-CA06) and the RELOAD (CA09-CA75).
//
// THE QUEUE. $76 and $77 are the head and tail of a singly-linked FIFO of objects waiting for the
// sprite assembler; the next-pointer is $1081,X and the null is any value >= $80 (the drain writes
// $80, C0/CABB/CACA). C0/CA76 pops from the head under a raster budget (CAA2-CAD3: pop, or when
// head == tail empty the list by setting BOTH ends null), so an object can still be queued when its
// timer next fires -- and the whole point of this routine is that the phase INC only happens on an
// accepted enqueue. The animation therefore advances at the rate the assembler drains, not at the
// rate the timer fires.
//
// $1100,X picks the end: >= 3 appends at the tail (C9A6), == 0 pushes at the head (C9D0), == 1/2
// appends as well (C9E2) unless the one-shot $78 == 2 sends it to the front instead (and clears it).
// bit7 is the only state that never enqueues.
//
// This used to be a flat gate -- `st == 0 || st == 1 || st == 2 -> return, no advance` -- which is
// two errors at once: those states DO advance (via C9D0/C9E2), and state >= 3 does NOT advance when
// the object is already queued. --sweepc98a measured it: 33744 of 116640 input combinations had the
// wrong anim state. Note that in the regime where the drain keeps up (queue empty at every timer
// fire) the old flat gate happened to be right for state >= 3, which is why no scenario check moved.
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
  int mode = r[0x1780 + idx];                               // RELOAD (CA09): pick the modifier table
  int mod = (mode <= 1) ? r[0x1680 + idx] : r[0x1781 + idx];
  // CA22 `CLC` / CA23 `ADC $1580,X` / CA26 `STA $C1` / CA28-CA2B phase / CA2E `ADC $C1` -- and that
  // second ADC has NO CLC in front of it, so the 16-bit carry OUT of the first one lands in the
  // index. Reachable: mod*4 + $1580 wraps past $FFFF (--sweepc98a: base $FFF0, mod 4 -> $10000).
  int sum = ((mod & 0xFF) << 2) + (r[0x1580 + idx] | (r[0x1581 + idx] << 8));
  int c1 = sum & 0xFFFF;
  // ...and CA26 IS A STORE: DP = $0100, so `STA $C1` (16-bit) leaves the base in $01C1/$01C2. Nothing in the port
  // read it, so it went unported -- until op 0x88's blob arm (C0/4A23 `STA $C1`, move.c CtVmColorFx) wrote the same
  // scratch: measured on the Manoria nuns, $01C1 is C0:4A25's 0009 on the frames obj16's loop runs and C0:CA28's
  // on the others (--monitor 7E01C1:2 --writer, saves/field_re/m81_nuns_lake.state f1..f5).
  r[0x01C1] = (uint8_t)c1; r[0x01C2] = (uint8_t)(c1 >> 8);
  int ix = ((r[0x1681 + idx] & 0xFF) + c1 + (sum > 0xFFFF)) & 0xFFFF;  // phase-indexed offset into $E4
  int a = (0x240000 + ix < romLen) ? rom[0x240000 + ix] : 0;
  if (a != 0) { r[0x1601 + idx] = (uint8_t)a; return; }     // CA72: store, phase persists
  a = (0x240000 + c1 < romLen) ? rom[0x240000 + c1] : 0;    // CA45: re-look-up at the phaseless base
  r[0x1601 + idx] = (uint8_t)a;
  if (mode != 2) { r[0x1681 + idx] = 0; return; }           // CA48-CA4D / CA6A: phase := 0
  // CA4F-CA69, the mode-2 PING-PONG (was deferred). $7F0B01,X is the direction/repeat state, and
  // the ROM reads it with two chained DECs, so the three arms are by ORIGINAL value:
  //   v == 1  -> DEC $1681,X only     (CA54 -> CA66)   the phase walks BACKWARD, v untouched
  //   v == 2  -> $7F0B01 := 1, then DEC $1681,X        (CA57 -> CA61/CA62 -> CA66)
  //   else    -> $7F0B01 := v - 2, $1681 := 0          (CA59/CA5D)
  // That backward step is the whole point: the sequence plays forward to its $FF sentinel and then
  // REVERSES, instead of restarting. Zeroing the phase for every mode (what this used to do) turned
  // a ping-pong into a saw.
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
  // C0/CB3D-CB43. The row select is `BEQ` (facing 0) / `CMP #02 / BEQ` (facing 2) / `BPL`, and that
  // BPL tests the sign of (A - 2), not of A -- CMP leaves N from the subtraction. So facing 1 gives
  // 1 - 2 = 0xFF, N SET, BPL NOT taken, and it falls through to CB45 = the x1 row. Only facing 3
  // (and anything above it) reaches CB5B = x3.
  // This read `facing & 0x80`, i.e. bit7 of A, which sent facing 1 -- the most common facing there
  // is -- down the x3 row. Measured on the oracle, base_loc6 obj8: facing 1, $1480 = 40,
  // $1500 = 0x72C0, mod 3, phase 1 -> the x1 row reads ROM byte 0x14 and the x3 row 0x1C, and the
  // oracle's live $0F01 is 0x14. The old comment "(facing 1 or 3)" named the bug out loud.
  if (facing == 0)                  d6 = b1500;             // CB52  -> base only
  else if (facing == 2)             d6 = b1480 * 2 + b1500; // CB6E  -> x2
  else if ((facing - 2) & 0x80)     d6 = b1480 + b1500;     // CB45  -> x1  (facing 1)
  else                              d6 = b1480 * 3 + b1500; // CB5B  -> x3  (facing 3 and above)
  int ph = r[0x1681 + idx] & 0xFF;                          // phase
  int mod = (cfg == 2) ? r[0x1781 + idx] : r[0x1680 + idx]; // CB8D ($1781) vs CBB1 ($1680)
  d6 = (d6 + ((mod & 0xFF) << 2) + ph) & 0xFFFF;
  int fb = (0x240000 + d6 < romLen) ? rom[0x240000 + d6] : 0xFF;  // CB9E/CBC2: [$D6] in bank $E4
  if (fb == 0xFF && cfg == 2) {
    // C0/CBA4-CBB0, the mode-2 arm of the sentinel -- and it is NOTHING like the other one. The ROM
    // decrements the repeat state and returns CLC: "do not commit this frame". The wrap below
    // belongs to the cfg != 2 path alone (CB8B `BNE ->CBB1` splits them), which the first cut of
    // that fix missed by applying the rewind to every cfg.
    //   v == 1 -> leave it   (CBA9 BEQ skips the store)
    //   else   -> v := v - 1
    int v = r[0x10B01 + idx];
    if (((v - 1) & 0xFF) != 0) r[0x10B01 + idx] = (uint8_t)((v - 1) & 0xFF);
    return -1;                                              // CBAF: CLC = do NOT commit
  }
  if (fb == 0xFF) {
    // THE WRAP, C0/CBC8-CBDA. The sentinel does not mean "no frame": the ROM REWINDS $D6 by $D9,
    // marks the phase, and RE-FETCHES -- returning that byte with carry SET, exactly like the
    // normal path (CBDA is the shared `SEC / RTS`). $D9 is the PHASE: C0/CB7A-CB80
    // `LDA $1681,X / AND #00FF / STA $D9`, and the offset added above is `mod*4 + $D9`
    // (CB93-CB98), which is what this function already computes. So the rewind is exactly `- ph`.
    // We used to return 0xFF here, and main.c's commit gate (`fb != 0xFF`) then SKIPPED the
    // commit -- so on every sequence wrap the port held the old frame where the ROM advanced to
    // the row's first. --sweepcb3a caught it: 148 of 2187 input combinations.
    d6 = (d6 - ph) & 0xFFFF;                                // CBCA-CBCF
    r[0x1681 + idx] = 0xFF;                                 // CBD3-CBD5
    fb = (0x240000 + d6 < romLen) ? rom[0x240000 + d6] : 0xFF;   // CBD8: the RE-FETCH
  }
  return fb;
}

// CtHRet now lives in move.h -- --sweepops needs it to grade a single handler's carry.

// ---------------------------------------------------------------------------
// C0/9E84 -- the PC's FACING selector, called from the HEAD of C0/9E29 (9E29 JSR C0/9E84) before it
// copies the velocity. It indexes a 64-entry WORD table at $C0:9FF7 from the two velocity deltas and
// dispatches to 16 handlers plus an idle default. Full derivation: disasm/notes/player_anim_9e84.md.
//
// GRADED BY --sweepanim against micro-execution of the real routine. An earlier cut of this file was
// wired into --portrun on the theory that it drove $0F01 (the animation FRAME); it did not, and was
// reverted (3648128). What it actually owns is $1600, the FACING -- which is the only thing left
// diverging on the player once the joypad and 00A7 legs landed.
// ---------------------------------------------------------------------------

// The shared 15-byte tail every arm ends in (A171/A1A4/A1BB/A1D2/A211/A23D/A254 are the same bytes
// with a different k). THE CMP GUARD IS LOAD-BEARING: an unchanged mode resets NEITHER phase nor
// timer, which is why the walk phase does not stutter while a direction is held.
static void ctAnimMode(uint8_t* r, int y, int k) {
  if (r[0x1680 + y] == (uint8_t)k) return;               // A175 LDA $1680,Y / CMP #k / BEQ rts
  r[0x1680 + y] = (uint8_t)k;
  r[0x1681 + y] = 0;                                     // phase
  r[0x1601 + y] = 0;                                     // timer
}

// C0/A18C, the RUN tail. A live velocity, a non-lead object, or $E5 set all mean RUN; all of them
// clear means you asked to run and did not move -- the run-into-wall HEAD-SHAKE.
static void ctAnimRunTail(uint8_t* r, int y) {
  r[0x0158] = 0; r[0x0159] = 0;                          // A18C STZ $58 / A18E STZ $59
  if (r[0x1D32] || r[0x1D33] || y != r[0x0197] || r[0x01E5])
    ctAnimMode(r, y, 0x06);                              // A1A4, k = $06 RUN
  else
    ctAnimMode(r, y, 0x17);                              // A1BB, k = $17 head-shake
}

// C0/A171, the WALK tail. It opens with the SAME `STZ $58 / STZ $59` as the run tail -- the previous
// cut of this file dropped those two stores because only A18C's copy was transcribed, and every
// seeded idle counter then survived a walk. That single omission was most of --sweepanim's 480.
static void ctAnimWalkTail(uint8_t* r, int y) {
  r[0x0158] = 0; r[0x0159] = 0;                          // A171 STZ $58 / A173 STZ $59
  ctAnimMode(r, y, 0x01);                                // A175, k = $01 WALK
}

// C0/A1E9, the IDLE chain, reached through the table's $9FF2 default (`LDX $6D / BRL ->A1E9`).
// Standing still puts both deltas at 0, so the index is 0 and every zero slot lands here.
static void ctAnimIdleChain(uint8_t* r, const uint8_t* rom, int romLen, int y) {
  // A1E9 LDA $62 / BEQ ->A1F7 / CMP #02 / BNE ->A1F7 / CPY $97 / BNE ->A1F7 / A1F5 BRA ->A1D2
  if (r[0x0162] == 0x02 && y == r[0x0197]) { ctAnimMode(r, y, 0x19); return; }
  if (r[0x011F] == 0) { ctAnimMode(r, y, 0x00); return; }         // A1F7/A1F9 -> A211
  // A1FB REP #20 -- $58 is read as a WORD from here to A20E, and the compare is 16-bit.
  int c = r[0x0158] | (r[0x0159] << 8);                           // A1FD LDA $58
  if (c != 0x02FF) {                                              // A1FF CMP #02FF / A202 BEQ ->A22A
    if (c < 0x02FF) { ctAnimMode(r, y, 0x00); return; }           // A204 BCC ->A211
    if ((c & 0x007F) != 0x007F) return;                           // A206-A20C; A20E SEP #20 / A210 RTS
  }                                                               //   -- that arm writes NOTHING AT ALL
  // A22A LDA #8000 / A22D TSB $58. Sixteen-bit: `A9 00 80` is a WORD immediate here, which a linear
  // disassembler mis-decodes as `LDA #00 / BRA` because it has already seen A20E's SEP.
  // NOTE it ORs into the ORIGINAL counter -- A206's `AND #007F` changed A, never memory.
  int v = (c | 0x8000) & 0xFFFF;
  r[0x0158] = (uint8_t)v; r[0x0159] = (uint8_t)(v >> 8);
  int t = (r[0x01F8] + 1) & 0xFF;                                 // A22F SEP #20 / A231 INC $F8
  r[0x01F8] = (uint8_t)t;
  int a = CtRomByte(rom, romLen, 0xFE00 + t);                     // A233 LDA $F8 / TAX / A236 LDA $FE00,X
                                                                  //   DB = $00 and bank $00:$FE00 mirrors
                                                                  //   $C0:$FE00 = file offset $FE00.
  if (a >= 0x08) { ctAnimMode(r, y, 0x00); return; }              // A239 CMP #08 / A23B BCS ->A211
  ctAnimMode(r, y, 0x21);                                         // A23D, k = $21 idle BLINK
}

void CtPlayerAnimSelect(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  r[0x01E5] = 0;                                         // 9E84 STZ $E5
  int y = idx;                                           // 9E86 LDY $6D
  if (r[0x1D34] != 0) {                                  // 9E88 LDA $1D34 / 9E8B BEQ ->9EAF
    if (r[0x1D34] & 0x04) {                              // 9E8D BIT #04 / 9E8F BNE ->9E95
      // 9E95 LDA $1D2D / 9E98 BEQ ->9EA4
      if (r[0x1D2D] == 0) { r[0x1681 + y] = 0; r[0x1601 + y] = 0; return; }      // 9EA4-9EAE
      // 9E9A BPL / EOR #FF / INC A / 9E9F CMP #18 / 9EA1 BRL ->A254. `82` is BRL, UNCONDITIONAL --
      // so the CMP has no consumer and the fold above it is dead. Reading it as a branch is the
      // obvious mistake, and this file made it once.
      ctAnimMode(r, y, 0x1B);                            // A254, k = $1B
      return;
    }
    r[0x01E5] = 1;                                       // 9E91 INC $E5 (from the STZ above) / BRA ->9EAF
  }
  // THE INDEX. The negative fold is `EOR #FF / CLC / ADC #21` = (0x20 - a) & 0xFF -- NOT a two's
  // complement negate -- and ONLY the X axis is shifted, so the two axes occupy different bit ranges
  // of one index. `AND #7E` keeps bits 1..6: an EVEN index into a WORD table of 64 entries.
  int bx = r[0x1D2C];                                    // 9EAF
  if (bx & 0x80) bx = ((bx ^ 0xFF) + 0x21) & 0xFF;       // 9EB2 BPL ->9EB9 / 9EB4-9EB8
  int sh = (bx >> 3) & 0xFF;                             // 9EB9 LSR A x3
  r[0x01DB] = (uint8_t)sh;                               // 9EBC STA $DB
  int by = r[0x1D2D];                                    // 9EBE
  if (by & 0x80) by = ((by ^ 0xFF) + 0x21) & 0xFF;       // 9EC1 BPL ->9EC8 / 9EC3-9EC7
  // ⚠ 9ECA's `AND #7E` IS WRITTEN TWICE -- here and at the top of CtAnimDispatch9FF7 -- and the
  // two are MUTUALLY redundant: mutating either one alone changes nothing, because the other
  // still clears bit 0. Both belong: this transcribes the instruction at this address, and the
  // dispatch's copy covers the three follower entries (C0/9F1C, 9F6B, 9FCF) that reach the same
  // table with an index they compute themselves. --sweepanim grades the fact with a TWO-SITE
  // mutation; a one-site one survives, and did.
  int ix = (by | sh) & 0x7E;                             // 9EC8 ORA $DB / 9ECA AND #7E / TAX
  CtAnimDispatch9FF7(r, rom, romLen, y, ix);             // 9ECD JSR ($9FF7,X)
}

// `JSR ($9FF7,X)` -- the facing/anim handler table, shared by the LEAD's selector above (C0/9ECD) and by
// the three FOLLOWER entries C0/9F1C, C0/9F6B (after a ring advance) and C0/9FCF (the hold arm), which
// compute their index differently but dispatch through the very same table into the very same handlers
// (every handler begins `LDX $6D`, i.e. the object being run, whichever entry reached it).
void CtAnimDispatch9FF7(uint8_t* r, const uint8_t* rom, int romLen, int y, int ix) {
  ix &= 0x7E;
  int t = rom[0x9FF7 + ix] | (rom[0x9FF8 + ix] << 8);    // 9ECD JSR ($9FF7,X)
  // Each handler's operands are read STRAIGHT FROM THE ROM rather than transcribed into a table here:
  // a hand-copied facing list is exactly the kind of thing that goes stale silently, and this routine
  // has already cost two wrong hand-decodes. The handlers begin `LDX $6D`, so they re-load X = idx
  // over the `TAX` above and store through the object index, not the table index.
  if (t == 0x9FF2) { ctAnimIdleChain(r, rom, romLen, y); return; }               // the default arm
  int tail, d;
  if (t >= 0xA079 && t < 0xA0C9) {                       // CARDINAL, stride $0A: unconditional facing
    r[0x1600 + y] = rom[t + 3];                          // +2 LDA #f  -> the operand at +3
    d = rom[t + 8] | (rom[t + 9] << 8);                  // +7 BRL, operand at +8, next instr at +10
    tail = (t + 10 + ((d >= 0x8000) ? d - 0x10000 : d)) & 0xFFFF;
  } else if (t >= 0xA0C9 && t < 0xA171) {                // DIAGONAL, stride $15: keep-else-force
    int cur = r[0x1600 + y];                             // +2 LDA $1600,X
    if (cur != rom[t + 6] && cur != rom[t + 10])         // +5 CMP #a / +9 CMP #b
      r[0x1600 + y] = rom[t + 14];                       // +13 LDA #a / +15 STA $1600,X
    d = rom[t + 19] | (rom[t + 20] << 8);                // +18 BRL, operand at +19, next instr at +21
    tail = (t + 21 + ((d >= 0x8000) ? d - 0x10000 : d)) & 0xFFFF;
  } else return;                                         // not a shape this port knows
  if (tail == 0xA171)      ctAnimWalkTail(r, y);
  else if (tail == 0xA18C) ctAnimRunTail(r, y);
}
// C0/A254 and its siblings, reached by name from the follower entries (9EEE / 9F3D `BRL ->A254` = mode $1B,
// the stair walk). The CMP guard is the same load-bearing one every tail shares.
void CtAnimModeSet(uint8_t* r, int y, int k) { ctAnimMode(r, y, k); }

// C0/9E29 -- the PC controller's APPLY, reached from C0/3040. Two halves:
//
//  1. the velocity. `$1D32`/`$1D33` (what C0/881E computed) go straight to `$1900,Y`/`$1980,Y`; when
//     BOTH are zero the ROM branches to 9E36 and STZs them instead, which lands the same value.
//  2. the party PATH-HISTORY RING -- seven parallel arrays at `$7F0C00`, `$0C80`, `$0D00`, `$0D80`,
//     `$0E00`, `$0E80` and `$0F00`, all indexed by the 7-bit cursor `$AB` (`INC A / AND #7F`, so it
//     wraps at 128 entries). This is the trail a conga FOLLOWER walks; nothing in the port filled it
//     before. Note it is written ONLY on the non-zero arm: a stationary frame records nothing.
//
// 9E29 calls C0/9E84 first. That routine writes ANIM state (`$1681`/`$1601`) and dispatches
// `JSR ($9FF7,X)`; it does NOT touch the velocity, so it is deliberately not modelled here.
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

// ---------------------------------------------------------------------------
// VM op 0x02 -- C0/5FB6, the PRIORITY CALL. The ONLY opcode in the ported set that writes ANOTHER
// object's state, which is what made --portrun's idx-12 f71 line unreadable for so long: the writer
// probe named C0/6027 as the author of $1192 with `$6D = 10`, i.e. object $10 reaching into object
// $12's script pointer. No amount of staring at object $12's own script could explain an IP that
// object $12 never moved.
//
// Three bytes. Operand 1 is the TARGET object; operand 2 packs the new priority in bits 7..3
// (`LSR A` x3) and the script-function slot in bits 3..0 (`AND #000F`). EVERY arm exits IP+3 with
// carry SET -- the three rejects reach 6039, which is just the normal exit with one extra INY to make
// up for the 5FD7 INY they skipped.
//
// $7F0580 is a per-PRIORITY-LEVEL array of 128-byte rows indexed by object; the ROM addresses it with
// the hardware multiplier (`STA $4202 / LDA #80 / STA $4203 / ... / LDA $4216` = x128).
//
// GRADED BY --sweepop02, NOT by --portrun: in that scenario the port drops the calling object at f66
// on an unported op 0x91, five frames before the call, so --portrun dispatches this opcode ZERO times
// and scores identically with the handler present or absent.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// C0/A88D -- the OBJECT ACTIVATION GATE, and it is a RECOMPUTED PREDICATE, not a stored flag. Both
// arms of C0/A810's per-object dispatch reach it (A82D for state >= 3 and A873 for state < 3) and
// both gate the SAME four calls on its carry: `BCC` skips C98A (anim), A98A (unlink), AB45 (screen
// position) and A9CD (relink). Only state 7 (-> C0/A947) and state 8 (skipped outright) bypass it.
//
// --portrun used to stand in for this with `$0F00,X & 0x80` read from the previous frame's snapshot.
// That is the right answer whenever the object's activity does not change, and wrong exactly on a
// transition -- which is what the op-0x91 port exposed: at f66 the ROM does `$1A91 01->00 @ C0/4CD9`
// and then, same frame, `$0F10 80->00 @ C0/A915`, after which C98A is never reached and the anim
// timer stops. The port, holding the stale latch, ticked it one time too many.
//
// THE LATCH IS A PURE FUNCTION OF THE PREDICATE -- no hysteresis. A8E5 `LDA $0F00,X / BMI ->A90E`
// only skips the one-shot SETUP for an object that was already active, and A913 `BPL ->A934` only
// skips the STZ for one that was already inactive; either way the byte ends at $80 or $00.
//
// WHAT THIS DELIBERATELY DOES NOT DO, because the ask was the predicate and because each of these is
// a separately-gradable mechanism:
//   * the RISING-EDGE one-shot (A8EA-A904): $0E80/$0E81 = $80, $1681/$1601/$1B00 = 0, $0F01 = $FF
//   * the four sub-calls -- C0/7170 and C0/734C (the OBJ palette allocator, ported in ct_vm.c on the
//     Mem* abstraction, see disasm/notes/objpal_allocator.md) and C0/6F9A / C0/A9CD / C0/EA42 /
//     C0/7056 / C0/A98A (all ported in this file, but they are side effects of the edges, not of the
//     predicate)
//   * the falling-edge tails A924-A932 ($0129 = $0D when $7F0A00,X and $0130 allow) and A934-A93C
//     ($7F0B01,X = 2 when non-zero)
// --sweepa88d grades exactly what IS here -- the carry, the four window bytes and the latch -- and
// seeds $0154 bit 7 so that C0/7170 and C0/734C are inert BY THE ROM'S OWN GATE rather than by the
// sweep averting its eyes.
//
// NOTE the duplication: ct_vm.c carries an equivalent predicate for the live VM (graded by
// vm-activation-pass-0F00). The two cannot share code today because that one reads position from
// sc->fx[] FLOATS rather than $1801/$1881 -- the same split pinned by vmdiff-withpos-bedslide.
// ---------------------------------------------------------------------------
int CtObjActiveGate(uint8_t* r, int idx) {
  int act = 1;
  if (r[0x1100 + idx] & 0x80) act = 0;                   // A88F LDA $1100,X / A892 BMI ->A910
  else {
    int vis = r[0x1A81 + idx];                           // A894 -- the byte op 0x90/0x91 writes
    if (vis == 0 || (vis & 0x80)) act = 0;               // A897 BEQ ->A910 / A899 BMI ->A910
    else {
      // A89B-A8C9, the camera window. Each edge SATURATES: the ROM tests the borrow/carry and
      // re-loads a literal 0 or $FF rather than letting the byte wrap.
      int c3 = r[0x1D0A] - 4; if (c3 < 0)    c3 = 0x00;  // A89F SBC #04 / A8A1 BCS / A8A3 LDA #00
      int c4 = r[0x1D0C] + 4; if (c4 > 0xFF) c4 = 0xFF;  // A8AB ADC #04 / A8AD BCC / A8AF LDA #FF
      int c5 = r[0x1D0E] - 2; if (c5 < 0)    c5 = 0x00;  // A8B7 SBC #02 / A8B9 BCS / A8BB LDA #00
      int c6 = r[0x1D10] + 5; if (c6 > 0xFF) c6 = 0xFF;  // A8C3 ADC #05 / A8C5 BCC / A8C7 LDA #FF
      r[0x01C3] = (uint8_t)c3; r[0x01C4] = (uint8_t)c4;  // A8A5 / A8B1
      r[0x01C5] = (uint8_t)c5; r[0x01C6] = (uint8_t)c6;  // A8BD / A8C9
      // The compares are against the tile coordinate DOUBLED, and the ASL is EIGHT-bit -- bit 7 of
      // $1801/$1881 falls off the top rather than widening the value.
      int tx = (r[0x1801 + idx] << 1) & 0xFF;            // A8CD LDA $1801,X / A8D0 ASL A
      int ty = (r[0x1881 + idx] << 1) & 0xFF;            // A8D9 LDA $1881,X / A8DC ASL A
      if (tx < c3 || tx >= c4) act = 0;                  // A8D1 CMP $C3 BCC / A8D5 CMP $C4 BCS
      else if (ty < c5 || ty >= c6) act = 0;             // A8DD CMP $C5 BCC / A8E1 CMP $C6 BCS
    }
  }
  r[0x0F00 + idx] = (uint8_t)(act ? 0x80 : 0x00);        // A8F2 STA / A915 STZ -- see the note above
  return act;                                            // A90E SEC (active) / A940 CLC (inactive)
}

// ---------------------------------------------------------------------------
// C0/3154 -- THE PLAYER'S TOUCH REDIRECT, and the reason a walking NPC's script never has to "end":
// the encounter does not come from the NPC's own closed loop, it is PREEMPTED into it from outside.
// op 0xB0's state-0 arm ends
//     304A LDX $6D / 304C LDA $1600,X / 304F JSR C0/5B8D / 3052 BCC ->3057 / 3054 JSR C0/3154
// and C0/5B8D is only `LDA $EB / BPL ->5B93 / CLC / RTS  ...  5B93 SEC / RTS` -- carry SET iff $01EB
// bit 7 is CLEAR, i.e. the touch latch holds a real object index rather than the $80 "nothing"
// sentinel that C0/881E re-arms every frame. NOTE the `304C LDA $1600,X` is DEAD: 5B8D's first
// instruction overwrites A. (This ROM does that a lot -- cf. the dead `LDX $6D` before TAX in op
// 0x00 and the dead `CMP #$18` before the BRL in C0/9E84.)
//
// C0/3154 IS OP 0x02'S CALL ARM WITH TWO CONSTANTS BURNED IN: the target is $01EB (whoever the
// proximity scan hit) instead of a script operand, and both the new priority and the function slot
// are fixed at 2. Its guard `LDA $1C00,X / CMP #03 / BCC` is op 0x02's `newp < cur` specialised to
// newp = 2, so it is the same law, not a different one.
//
// NOT PORTED: the tail C0/31B1 that 31AD calls on BOTH non-reject paths. It gates on $0154 bit 1 and
// then does a tile-based shove of the touched object through $7E7040 with four facing arms -- a
// separate mechanism, and --sweeptouch seeds that bit so the ROM's own `BIT #02 / BNE ->31BB` makes
// it inert rather than the sweep averting its eyes.
// ---------------------------------------------------------------------------
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
  r[0x1180 + t] = (uint8_t)fn;                         // 319A STA $1180,X -- THE REDIRECT
  r[0x1181 + t] = (uint8_t)(fn >> 8);
  r[0x1C00 + t] = 0x02;                                // 319F LDA #02 / 31A1 STA $1C00,X
  r[0x1A80 + t] = 0;                                   // 31A4
  r[0x1A01 + t] = 0;                                   // 31A7
  r[0x1001 + t] = 0;                                   // 31AA STZ $1001,X -- the SCHEDULER DELAY, so
                                                       //   the preempted object runs on the very next
                                                       //   pass instead of waiting out its cadence
}

// C0/5FBE -- op 0x02's body, entered with the target index ALREADY in A. op 0x02 reaches it having
// read that index from the script (5FBA); op 0x05 reaches it via `BRL ->5FBE` having resolved it
// from the party-slot table instead. Y is ip+1 at the join for BOTH -- 5FB6 INY and 61E9 INY -- so
// everything downstream, operand 2 at ip+2 included, is literally the same code.
static int ctVmCall02(uint8_t* r, int ip, int tgt, int* carry) {
  *carry = 1;                                          // 603C SEC on every path
  // ⚠ THE DIRECT-PAGE STAGING WAS MISSING FROM THIS ROUTINE UNTIL op 0x05 ARRIVED. --sweepop02
  // grades a hand-listed write set, so it never looked at $01DB; --sweepops compares the whole
  // image, and the first case of op 0x05 -- which reaches this same body -- reported `$001DB
  // C=5A RO=02` immediately. Seven stores, all of them real: the routine's own scratch is shared
  // direct page, not private locals. Same class of gap as C0/5B95's missing `STZ $EA`.
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 5FBE STA $DB / 5FC1 STZ $DC
  // 5FC3 / 5FC8 / 5FCD -- three reject gates, each `-> 6039`, all BEFORE any further staging.
  if (r[0x1C01 + tgt] != 0 || (r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80))
    return (ip + 3) & 0xFFFF;
  int cur  = r[0x1C00 + tgt];                          // 5FD2 the target's CURRENT priority
  r[0x01D9] = (uint8_t)cur;                            // 5FD5 STA $D9
  int op2  = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 5FD9 operand 2
  r[0x01DD] = (uint8_t)op2;                            // 5FDD STA $DD
  int newp = (op2 >> 4) & 0x0F;                        // 5FDF-5FE2 LSR A x FOUR -- not three. Operand
                                                       //   2 is cleanly `newPrio:slot`, high nibble
                                                       //   and low nibble. Counting three (the two
                                                       //   dis.py windows overlapped at 5FDF) put the
                                                       //   priority off by a factor of two; caught by
                                                       //   --sweepop02 at 699/840, not by reading.
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
    r[sav]     = r[0x1180 + tgt];                      // 6003/6008: SAVE the target's live IP at its
    r[sav + 1] = r[0x1181 + tgt];                      //   CURRENT level, for op 0x00 to resume
    r[0x1180 + tgt] = (uint8_t)fn;                     // 6027 -- the measured writer
    r[0x1181 + tgt] = (uint8_t)(fn >> 8);
    r[0x1C00 + tgt] = (uint8_t)newp;                   // 602E
    r[0x1A80 + tgt] = 0; r[0x1A01 + tgt] = 0;          // 6031/6034: drop any in-flight move
  } else if (newp > cur) {                             // 603E -- the DEFER arm
    // A call to a LESS urgent level interrupts nothing: it PARKS the function ENTRY (not a saved IP)
    // at that level for op 0x00 to pick up on its way back down -- and only when the slot is empty
    // (6053 `LDA $7F0580,X` / 6057 `BNE ->6078` drops the call on the floor). It touches neither
    // $1180 nor $1C00 nor the mover.
    int e1 = (newp * 128 + tgt) & 0xFFFF;
    r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);        // 6050 STA $E1 (16-bit)
    int park = 0x10580 + e1;
    if ((r[park] | (r[park + 1] << 8)) == 0) {
      // $E3 is staged ONLY here, past `6057 BNE ->6078` -- a defer onto an occupied slot leaves it
      // untouched, unlike the call arm which always stages it.
      int e3 = (tgt * 16) & 0xFFFF;
      r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);      // 6062 STA $E3 (16-bit)
      r[park] = (uint8_t)fn; r[park + 1] = (uint8_t)(fn >> 8);      // 6074
    }
  }
  // newp == cur (5FE7 BEQ ->603A) lands here having done NOTHING: same level, no call.
  return (ip + 3) & 0xFFFF;
}

int CtVmOp02(uint8_t* r, int ip, int* carry) {         // 5FB6 INY / 5FBA LDA $7F2001,X
  return ctVmCall02(r, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], carry);
}

// C0/6109 -- op 0x04, the BLOCKING priority call. It looks like op 0x02 with extra steps and it is
// not: 0x02 issues a call and moves on, exiting SEC at ip+3 on every single path. 0x04 issues the
// call and then PARKS ON ITS OWN OPCODE BYTE until the target finishes, which makes it the first
// opcode here whose two visits run completely different code.
//
// THE STATE THAT SPLITS THEM IS $7F0980 INDEXED BY THE CALLER, NOT THE TARGET. 6109 `LDX $6D /
// LDA $7F0980,X / BEQ ->611A / BRL ->61A7`: zero means "no request outstanding" -> the FRESH arm;
// non-zero means "I already asked" -> the POLL arm. 61A0 sets it to 1 on a successful issue and
// 61DC clears it to 0 on completion, so the latch is the whole of the opcode's memory.
//
// IT NEVER EXECUTES INY. Every IP delta comes from the TYX/INX sequences at the exits, and there
// are exactly four exits, in two pairs:
//   6114  TYX/INX/INX/INX/SEC/RTS -> ip+3, CONTINUE   (fresh arm: the target is not runnable)
//   61A4  TYX/CLC/RTS             -> ip+0, PARK       (fresh arm: issued, or cannot issue yet)
//   61E0  TYX/INX/INX/INX/SEC/RTS -> ip+3, CONTINUE   (poll arm: the call has completed)
//   61E6  TYX/CLC/RTS             -> ip+0, PARK       (poll arm: still running)
// So it is a THREE-byte opcode -- opcode, target index, (priority<<4 | fn slot) -- even though
// nothing inside it ever advances Y.
//
// TWO PLACES WHERE COPYING op 0x02 WOULD HAVE BEEN WRONG, which is why this is derived from 6109's
// own bytes rather than diffed against 5FB6:
//   (a) 6127 `LDA $1C01,X / BNE ->61A4` PARKS. Op 0x02's corresponding test (5FC3) SKIPS to ip+3.
//       Same test, same byte, opposite decision -- a busy target makes 0x02 give up and 0x04 wait.
//   (b) 0x04 has NO defer arm. Op 0x02's `newp > cur` branch (603E) parks the function entry in the
//       $7F0580 ladder for op 0x00 to find on its way back down; 6109 has nothing of the kind --
//       614C/614E send BOTH the equal and the greater-or-equal case straight to the park exit.
// And the POLL arm does not re-test $1C01,X at all: 61B4/61B9 test only $1100,X and $1000,X.
// C0/6122 (fresh) and C0/61AF (poll) -- op 0x04's two joins, both entered with the target index
// already resolved. op 0x04 reads it from the script at 611E/61AB; op 0x07 `BRL`s to the same two
// labels having taken it from the party-slot table. Y is ip at both joins either way.
static int ctVmCall04(uint8_t* r, int idx, int ip, int tgt, int* carry) {
  // 6109 LDX $6D -- X is SIXTEEN-bit here (the runner's REP #10 at 5A9E), so this reads $016D AND
  // $016E. The runner's own `5A9C STX $6D` is an 8-BIT store (its SEP still applies), so it never
  // writes $016E; measured on saves/truce_market.state, $016D/$016E = $16/$00, i.e. the high byte
  // is 0 and X is the plain object index.
  if (r[0x10980 + idx] == 0) {
    // ================= FRESH REQUEST, C0/611A =================
    // 611B INX (X = ip+1) with no INY: the operand is read WITHOUT moving the IP.
    // 611C TDC / 611D XBA zeroes the accumulator's high half so the 8-bit LDA + 16-bit TAX at 6126
    // yields a clean index; $01DC is then STZ'd so the $DB pair reads as a 16-bit value at 6162.
    r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;        // 6122 STA $DB (8-bit) / 6124 STZ $DC
    if (r[0x1C01 + tgt] != 0) {                        // 6127 -> 61A4: PARK, not skip. See (a).
      *carry = 0; return ip; }
    if ((r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80)) {   // 612C / 6131 -> 6114
      *carry = 1; return (ip + 3) & 0xFFFF; }
    int cur = r[0x1C00 + tgt];                         // 6136 the target's CURRENT priority
    r[0x01D9] = (uint8_t)cur;                          // 6139
    int op2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 613E (X = ip+2, again without INY)
    r[0x01DD] = (uint8_t)op2;                          // 6142
    int newp = (op2 >> 4) & 0x0F;                      // 6144-6147 LSR A x FOUR
    r[0x01DF] = (uint8_t)newp;                         // 6148
    // 614A `CMP $D9` with A = the REQUESTED priority and $01D9 = the target's CURRENT one. 614C BEQ
    // and 614E BCS both park, so the call is issued ONLY when requested < current -- lower number
    // being more urgent, that is "do not interrupt something at least as important as this".
    if (newp >= cur) { *carry = 0; return ip; }        // -> 61A4
    int e1 = (cur * 0x80 + tgt) & 0xFFFF;              // 6152-6162: $4202*$4203 then + $DB
    r[0x01E1] = (uint8_t)e1; r[0x01E2] = (uint8_t)(e1 >> 8);   // 6164 STA $E1 -- SIXTEEN-bit
    int sav = 0x10580 + e1;                            // 616D STA $7F0580,X -- also 16-bit
    r[sav]     = r[0x1180 + tgt];                      // 6168: SAVE the target's live IP at its
    r[sav + 1] = r[0x1181 + tgt];                      //   current level, for op 0x00 to resume
    int e3 = (tgt * 16) & 0xFFFF;                      // 6171-6179 AND #00FF / ASL A x4
    r[0x01E3] = (uint8_t)e3; r[0x01E4] = (uint8_t)(e3 >> 8);   // 617A STA $E3 -- SIXTEEN-bit
    int ent = 0x12001 + ((e3 + (op2 & 0x0F) * 2) & 0xFFFF);    // 617C-6185: + slot*2
    int fn  = r[ent] | (r[ent + 1] << 8);              // 6186 -- a 16-bit read
    r[0x1180 + tgt] = (uint8_t)fn;                     // 618C STA $1180,X -- 16-bit
    r[0x1181 + tgt] = (uint8_t)(fn >> 8);
    r[0x1C00 + tgt] = (uint8_t)newp;                   // 6193
    r[0x1A80 + tgt] = 0x00;                            // 6196 STZ $1A80,X -- drop any in-flight move
    r[0x1A01 + tgt] = 0x00;                            // 6199 STZ $1A01,X
    r[0x10980 + idx] = 0x01;                           // 61A0 -- ARM THE CALLER'S WAIT LATCH
    *carry = 0; return ip;                             // 61A4 TYX / CLC / RTS -- park on our own byte
  }
  // ================= POLL, C0/61A7 =================
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 61AF / 61B1
  // 61B4 / 61B9 -- the target went away while we waited. NOTE THE ORDER: this arm jumps to 61D8 and
  // therefore clears the latch and returns WITHOUT ever writing $01D9/$01DD/$01DF, which the
  // completion arm below does write. Two paths, same exit, different write sets.
  if ((r[0x1100 + tgt] & 0x80) || (r[0x1000 + tgt] & 0x80)) {
    r[0x10980 + idx] = 0x00;                           // 61D8-61DC
    *carry = 1; return (ip + 3) & 0xFFFF;              // 61E0 -> ip+3, CONTINUE
  }
  int cur = r[0x1C00 + tgt];                           // 61BE
  r[0x01D9] = (uint8_t)cur;                            // 61C1
  int op2 = r[0x12001 + ((ip + 2) & 0xFFFF)];          // 61C6
  r[0x01DD] = (uint8_t)op2;                            // 61CA
  int newp = (op2 >> 4) & 0x0F;                        // 61CC-61CF
  r[0x01DF] = (uint8_t)newp;                           // 61D0
  // 61D2 `CMP $D9` again, and the SAME test now means the opposite thing. While the target is still
  // at or above the requested urgency it is still running our call, so 61D4/61D6 keep parking. Once
  // op 0x00 has walked its priority back DOWN in urgency (a HIGHER number), requested < current
  // falls through and the call is complete.
  if (newp >= cur) { *carry = 0; return ip; }          // -> 61E6 TYX / CLC / RTS
  r[0x10980 + idx] = 0x00;                             // 61D8: release the latch
  *carry = 1; return (ip + 3) & 0xFFFF;                // 61E0
}

int CtVmOp04(uint8_t* r, int idx, int ip, int* carry) {   // 611E / 61AB read the target themselves
  return ctVmCall04(r, idx, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], carry);
}

// C0/6086 -- op 0x03's body, the THIRD member of the call family and the one that sits between the
// other two. Op 0x02 never parks: it issues or defers and always exits SEC at ip+3. Op 0x04 issues
// and then BLOCKS on its own opcode byte until the callee finishes. Op 0x03 does neither -- it
// PARKS while the target is unavailable (busy, or running something at least as urgent) and then,
// once it has issued, CONTINUES. Three opcodes, three different answers to "what if I cannot call
// right now", and none of them derivable from the others.
//
// It also has NO defer arm: where op 0x02's `newp > cur` parks the function entry in the $7F0580
// ladder for op 0x00 to find, 60B0/60B2 send both the equal and the greater case to 6106, which
// parks the SCRIPT instead. And it never touches $7F0980, which is op 0x04's alone.
//
// Y is ip at the join (607E does TYX/INX without an INY), so operand 2 sits at ip+2 and the exits
// are 6100 `TYX/INX/INX/INX/SEC` -> ip+3 and 6106 `TYX/CLC` -> ip+0.
static void ctVmCall03(uint8_t* r, int ip, int tgt, CtHRet* hp) {
  hp->ok = 1;
  r[0x01DB] = (uint8_t)tgt; r[0x01DC] = 0x00;          // 6086 STA $DB / 6089 STZ $DC
  if (r[0x1C01 + tgt] != 0) {                          // 608B -> 6106 PARK
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

// C0/61F2 / 6207 / 6225 / 623B -- `LDA $97,X` with X = the script's operand. $0197/$0199/$019B are
// the three party slots' object indices (op 0x80's arms spill them there), so these opcodes name
// their target by PARTY SLOT rather than by object. Bit 7 set means the slot is EMPTY, and each
// sibling then skips its whole call with a three-byte advance.
int ctVmPartySlotTarget(const uint8_t* r, int slot) { return r[(0x0197 + slot) & 0x1FFFF]; }   // declared in move.h

// C0/5228, C0/52D3 and C0/536B -- ONE move-toward-an-actor body in THREE flavours. All three are
// the same 152-byte shape (5228 and 52D3 diff in 78 bytes, all past offset $4A, plus two branch
// displacements); what varies is which calls happen and where the ticket comes from.
//
//   `anim`   writes the facing $1600 and calls C0/5614 / C0/568B. TRUE for 0x94/0x95 (5228) and
//            0x98/0x99 (536B); FALSE for 0x9E/0x9F (52D3), which run the collision test against
//            the STALE facing exactly as op 0xA1 does.
//   `legged` carries the $1A80 leg counter and takes its ticket from a SCRIPT byte at ip+2 instead
//            of mirroring $1000. TRUE for 0x98/0x99 only, which is also why they are three bytes.
//
// ⚠ C0/568B RELOADS X = $6D ITSELF, so every arm that calls it acts on SELF regardless of which
// instruction the branch landed on. That is the trap op 0x8F sprang: the branch arithmetic says
// the target is in X, and it is, and it changes nothing.
static void ctVmMoveActorOp(uint8_t* r, const uint8_t* rom, int idx, int ip,
                            int viaSlot, int anim, int legged, int adv, CtHRet* h) {
  if (legged) {                                       // 534C -- op 0x92's $1A80 head
    if (r[0x1A80 + idx] != 0) {
      if (r[0x1A01 + idx] != 0) { h->ip = ip; h->carry = 0; h->ok = 1; return; }   // 5354 / 5356
      r[0x1A80 + idx] = 0x00;                         // 5359 STZ $1A80,X
      h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 535C -- NO 568B on THIS arm
    }
  } else if (r[0x1A01 + idx] != 0) {                  // 5217 / 52C2
    h->ip = ip; h->carry = 0; h->ok = 1; return;      // 521C / 52C7 TYX / CLC
  }
  int tgt = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 5223 / 52CE / 5366
  if (viaSlot) tgt = ctVmPartySlotTarget(r, tgt);     // 5423 / 5508 / 552F LDA $97,X
  // ⚠ THE TARGET-INACTIVE BRANCH LANDS ON A DIFFERENT INSTRUCTION IN EACH FLAVOUR. 0x9E/0x9F's
  // 52D6 -> 531E is a bare `BB TYX` so nothing at all happens; 0x94/0x95's 522B -> 5273 lands ON
  // `JSR C0/568B`; 0x98/0x99's 536E -> 53B6 lands on `A6 6D LDX $6D` first, so it ALSO clears
  // SELF's $1A80. Same test, same flag, three different outcomes.
  if (r[0x1100 + tgt] & 0x80) {                       // 5228/522B, 52D3/52D6, 536B/536E
    if (legged) r[0x1A80 + idx] = 0x00;               // 53B8
    if (anim) moveArriveAnim(r, idx);                 // 5273 / 53BB
    h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 5276 / 531E / 53BE
  }
  r[0x01F2] = r[0x1801 + tgt];                        // 522D / 52D8 / 5370
  r[0x01F3] = r[0x1881 + tgt];                        // 5232 / 52DD / 5375
  r[0x01E9] = (uint8_t)tgt; r[0x01EA] = (uint8_t)(tgt >> 8);   // 5237 STX $E9 -- SIXTEEN bit
  r[0x01F0] = r[0x1801 + idx];                        // 523B / 52E6 / 537E
  r[0x01F1] = r[0x1881 + idx];                        // 5240 / 52EB / 5383
  // Adjacency is WRAP-EXACT on each axis -- `CMP / INC A / CMP / DEC A DEC A / CMP`, all 8-bit --
  // NOT a signed |d| <= 1. At the map wrap the two disagree.
  int sy = r[0x1881 + idx], sx = r[0x1801 + idx];
  int yAdj = (sy == r[0x01F3]) || (((sy + 1) & 0xFF) == r[0x01F3]) ||
             (((sy - 1) & 0xFF) == r[0x01F3]);        // 5245-5252 / 52F0-52FD / 5388-5395
  int xAdj = (sx == r[0x01F2]) || (((sx + 1) & 0xFF) == r[0x01F2]) ||
             (((sx - 1) & 0xFF) == r[0x01F2]);        // 5254-5263 / 5301-530E / 5399-53A6
  if (yAdj && xAdj) {
    // `89 02 BIT #02` -- bit ONE of $1C81 selects C0/30B3, where ops 0x97/0xA1 test bit ZERO and
    // select C0/305D instead.
    if ((r[0x1C81 + idx] & 2) && CtVmMoveSnap30B3(r, idx)) {   // 526A/526E, 5316/5319, 53AE/53B1
      if (anim) ctMoveStepAnim5614(r, idx);           // 527B / 53C4 -- and $1A80 is NOT cleared
      h->ip = ip; h->carry = 0; h->ok = 1; return;    // 527E / 5323 / 53C7
    }
    if (legged) r[0x1A80 + idx] = 0x00;               // 53B8
    if (anim) moveArriveAnim(r, idx);                 // 5273 / 53BB
    h->ip = (ip + adv) & 0xFFFF; h->carry = 1; h->ok = 1; return;   // 5276 / 531E / 53BE
  }
  int ee = CtMoveDirAngle(r, rom);                    // 5281 / 5326 / 53CA JSR C0/ABA2
  r[0x01EE] = (uint8_t)ee;
  if (anim) r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];          // 528B/5290 / 53D4/53D9
  if (CtCollisionActorMovable(r, idx)) {              // 5293 / 5329 / 53DC JSR C0/5B95
    ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;             // 5298-529D / 532E / 53E1
    if (anim) r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];        // 52A0/52A5 -- the SECOND write,
  }                                                               //   easy to drop
  // ⚠ THE TICKET SOURCE DIFFERS: the legged pair takes the SCRIPT byte at ip+2, the other four
  // mirror $1000 -- and $7F0B00 takes the same byte as $1A01 in every case.
  int tk = legged ? r[0x12001 + ((ip + 2) & 0xFFFF)] : r[0x1000 + idx];   // 52AA / 5337 / 53F4
  r[0x1A01 + idx] = (uint8_t)tk;                      // 52AD / 533A / 53FA
  r[0x10B00 + idx] = (uint8_t)tk;                     // 52B0 / 533D / 53FD
  if (legged) r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);   // 5401 INC $1A80,X
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);        // 52B4 / 5341 / 5404 STY $C7
  CtBuildVelocityPriority(r, rom, idx);               // 52B6 / 5343 / 5406 JSR C0/ACFD
  if (anim) ctMoveStepAnim5614(r, idx);               // 52B9 / 5409 JSR C0/5614
  h->ip = ip; h->carry = 0; h->ok = 1;                // 52BC / 5346 / 540C LDX $C7 / CLC
}

// C0/32C8 and its four copies -- RELEASE ONE PARTY SLOT'S OBJECT. Ops 0xD9 and 0xDA hold five
// near-identical instances of this block; they differ only in which of the three DP slot pairs they
// index by and in the literal parked in $1180.
//   `xr`  the index the READS use. NOT always the same as the write index: op 0xD9's FIRST copy
//         loads it with a 16-bit `A6 LDX $97` (32CA), while the other four use `AA TAX` (3350,
//         33D7, 34A3, 34F6) on an 8-bit A whose high half was zeroed by the TDC/XBA idiom. The
//         opcode bytes were read to settle this -- the two forms differ only when $0198/$019A/
//         $019C is non-zero, which is exactly what the sweep seeds.
//   `lit` $171F / $173F / $175F for 0xD9's three slots, $16FF for BOTH of 0xDA's.
static void ctVmPartyRelease(uint8_t* r, int dp, int xr, int lit) {
  int prio = r[(0x1C00 + xr) & 0xFFFF];                          // 32CC LDA $1C00,X
  int prod = (prio * 0x80) & 0xFFFF;                             // 32CF/32D2 $4202 x $4203 -> $4216
  int dpv  = r[dp] | (r[dp + 1] << 8);                           // 32DF ADC $97 -- a SIXTEEN-bit read
  int off  = (prod + dpv) & 0xFFFF;                              // 32DB/32DC/32DF CLC / LDA / ADC
  r[0x01D9] = (uint8_t)off; r[0x01DA] = (uint8_t)(off >> 8);     // 32E1 STA $D9 (16-bit)
  int slot = r[(0x1180 + xr) & 0xFFFF] | (r[(0x1181 + xr) & 0xFFFF] << 8);   // 32E3 (16-bit)
  // `9F STA $7F0580,X` is a LONG store, so the 24-bit sum can leave WRAM entirely. Guarded rather
  // than masked: a wrap into $80 is not something this port can honestly claim to reproduce, and
  // no reachable offset gets near it.
  { int ea = (0x7F0580 + off) & 0xFFFFFF;                        // 32E8
    if (ea >= 0x7E0000 && ea <= 0x7FFFFE) {
      r[ea - 0x7E0000]     = (uint8_t)slot;
      r[ea - 0x7E0000 + 1] = (uint8_t)(slot >> 8); } }
  r[(0x1180 + dpv) & 0xFFFF] = (uint8_t)lit;                     // 32EC LDX $97 -- 16-bit, ALWAYS
  r[(0x1181 + dpv) & 0xFFFF] = (uint8_t)(lit >> 8);              // 32F1 STA $1180,X (16-bit)
  // ⚠ `SEP #30` at 32F4 FORCES XH TO ZERO, so the four stores below index by the LOW BYTE of the
  // value the 16-bit LDX just loaded -- they do not repeat the wide index.
  int x8 = dpv & 0xFF;
  r[0x1C00 + x8] = 0x00;                                         // 32F8
  r[0x1A80 + x8] = 0x00; r[0x1A01 + x8] = 0x00; r[0x1001 + x8] = 0x00;   // 32FB / 32FE / 3301
}

// C0/3284 and C0/3484 -- the five-store clear both opcodes run before releasing anything.
// ⚠ THE ROM STORES TO $7F0C00,X AND $7F0D00,X TWICE EACH, with X unmodified in between. The
// opcode bytes were dumped to be sure the disassembler was not eliding an offset: they really are
// `9F 00 0C 7F` `9F 00 0C 7F` `9F 00 0D 7F` `9F 00 0D 7F` `9F 00 0F 7F`. The intent was plainly to
// clear both halves of each pair; the ROM clears the first half twice. Taken verbatim -- the second
// write of each pair is what the poison audit reports as ungraded for these two opcodes.
static void ctVmPartyClear(uint8_t* r) {
  r[0x01AC] = r[0x01AB]; r[0x01AD] = r[0x01AB];                  // 327D/327F/3281 LDA $AB / STA $AC/$AD
  int x = r[0x01AB];                                             // 3283 TAX (zero-extended)
  r[0x10C00 + x] = 0x00; r[0x10C00 + x] = 0x00;                  // 3286 / 328A -- the SAME address
  r[0x10D00 + x] = 0x00; r[0x10D00 + x] = 0x00;                  // 328E / 3292 -- likewise
  r[0x10F00 + x] = 0x00;                                         // 3296
}

// C0/3306, 338B and 3412 -- stash one slot's two script-supplied coordinates and the QUADRANT their
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

// ===========================================================================================
// THE PLAYER'S SCRIPT OPCODES. An opcode trace of the ORACLE (PORTRUN_OPTRACE, hooking the dispatch
// at C0/5AB1 where X = opcode*2 and $6D is already latched) showed object 02 executes FOURTEEN
// distinct opcodes across --portrun's 360 frames, of which ELEVEN were undispatchable here -- which
// is why the harness never owned the player before f254 and why nothing ever TRAPPED: an actor the
// claim list never takes cannot trap.
//
// These are ten of the eleven. Each was derived from raw ROM bytes AND confirmed with
// `--callrom --writes` (exit carry, returned IP, and the exact write set). Op 0x96 is the eleventh
// and is NOT here: it needs C0/5614, C0/568B and C0/5B95, which this file does not have.
//
// THE RUNNER'S CONTRACT, which every one of these obeys (C0/5A93): the handler is entered with
// Y = the IP of its OWN opcode byte and $6D = the object index (already *2); it returns the NEW IP
// in X and the continue/yield decision in the CARRY (5AB4 `BCC` yields the visit; SEC spends one
// unit of the $0167 budget and dispatches the next opcode immediately).
// ===========================================================================================

// The 24-bit store target of op 0x4A, mapped into the port's flat 128 KB image. $7E is offset 0,
// $7F is offset 0x10000, and banks $00-$3F / $80-$BF below $2000 are the low-RAM mirror of $7E --
// which is the case the live measurement actually hit (`W f253 $00111 01->00 @ C0/67DE`).
// Anything else (real ROM, MMIO) is NOT modelled and returns -1 rather than corrupting the image.
static int ctFlatAddr24(int bank, int addr) {
  if (bank == 0x7E) return addr & 0xFFFF;
  if (bank == 0x7F) return 0x10000 + (addr & 0xFFFF);
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && (addr & 0xFFFF) < 0x2000) return addr & 0xFFFF;
  return -1;
}

// The SNES hardware divider: 16-bit dividend in $4204/$4205, 8-bit divisor in $4206, 16-bit
// quotient readable from $4214 after ~16 cycles (the ROM covers the latency with seven NOPs).
// ⚠ THE DIVIDE-BY-ZERO RULE IS NOT DERIVABLE FROM THE ROM -- op 0xFE's handler contains no zero
// guard and no zero path, so the bytes say nothing about it. $FFFF is the documented hardware
// result, and it is stated here as a hypothesis that --sweepops DECIDES: the grid seeds a zero
// divisor deliberately, so the oracle either confirms this or fails the case.
static int ctSnesDiv16(int dividend, int divisor) {
  if ((divisor & 0xFF) == 0) return 0xFFFF;
  return ((dividend & 0xFFFF) / (divisor & 0xFF)) & 0xFFFF;
}

// C0/67E3 -- the POKE FAMILY'S PROLOGUE. Three script bytes become the 24-bit pointer at
// $01D0/$01D1/$01D2 and X is left at ip+4. Ops 0x4A, 0x4B, 0x4C and 0x4D all `JSR` it and differ
// only in what they then move through it, and how wide.
static int ctVmPtr67E3(uint8_t* r, const uint8_t* script, int ip) {
  int lo  = script[(ip + 1) & 0xFFFF];                    // 67E5
  int mid = script[(ip + 2) & 0xFFFF];                    // 67EC
  int bk  = script[(ip + 3) & 0xFFFF];                    // 67F3
  r[0x01D0] = (uint8_t)lo; r[0x01D1] = (uint8_t)mid; r[0x01D2] = (uint8_t)bk;   // 67E9/67F0/67F7
  return (bk << 16) | (mid << 8) | lo;
}

// `87 STA [$D0]` -- a store through that pointer, 8 or 16 bits wide. Nothing is masked or clamped:
// the destination is entirely script-supplied, and ctFlatAddr24 returns -1 for anything outside the
// WRAM this port owns. A 16-bit store CARRIES INTO THE BANK BYTE, so the second byte is resolved
// separately rather than assumed adjacent in the flat image.
static void ctVmPokeLong(uint8_t* r, int full24, int val, int wide) {
  int fa = ctFlatAddr24((full24 >> 16) & 0xFF, full24 & 0xFFFF);
  if (fa >= 0) r[fa] = (uint8_t)val;
  if (!wide) return;
  int nx = (full24 + 1) & 0xFFFFFF;
  int fb = ctFlatAddr24((nx >> 16) & 0xFF, nx & 0xFFFF);
  if (fb >= 0) r[fb] = (uint8_t)(val >> 8);
}

// THE VARIABLE-ASSIGNMENT GRID -- ops 0x50/0x51/0x52/0x53/0x54/0x55/0x56/0x58/0x59, ROM C0/68C6..6A11: locals
// ($7F0200 + 2n) and globals ($7F0000 + word) copied into one another or loaded from the script. One body, the
// callers of the families below plus the live VM (src/ct_vm.c step() and its init pass runFunc), which route
// every member here: a local word is little-endian in WRAM like every other word the 65816 stores, so op 0x50's
// store and op 0x13/0x15's read agree with the ROM's own `LDA #0008 / STA $0D00,X` (C0/42C9) on the same array.
// Returns 1 if `script[ip]` is in the grid.
int CtVmVarFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op < 0x50 || op > 0x59 || op == 0x57) return 0;
  // C0/68C6..6A11 -- THE VARIABLE-ASSIGNMENT GRID. Nine handlers, all `INY`-driven, all SEC,
  // all straight-line, and they vary along three independent axes that no two of them share
  // completely: WHERE THE VALUE COMES FROM, WHERE IT GOES, and THE WIDTH OF THE STORE.
  //
  //   op  len  source                       destination            store
  //   50   4   immediate word  @ip+1        local[byte @ip+3]      16-bit
  //   51   3   local[byte @ip+1]            local[byte @ip+2]       8-bit
  //   52   3   local[byte @ip+1]            local[byte @ip+2]      16-bit
  //   53   4   global[word @ip+1]           local[byte @ip+3]       8-bit
  //   54   4   global[word @ip+1]           local[byte @ip+3]      16-bit
  //   55   2   global[0], read 8-BIT        local[byte @ip+1]       8-bit
  //   56   4   immediate byte  @ip+1        global[word @ip+2]      8-bit
  //   58   4   local[byte @ip+1]            global[word @ip+2]      8-bit
  //   59   4   local[byte @ip+1]            global[word @ip+2]     16-bit
  //
  // ⚠ THE TWO ARRAYS ARE INDEXED DIFFERENTLY AND THAT IS THE EASIEST THING HERE TO GET WRONG.
  // A LOCAL index is a BYTE and is DOUBLED (`ASL A`) -- stride two, the same array ops 0x12
  // and 0x71..0x77 use. A GLOBAL index is a WORD and is used RAW, no shift at all. 0x53 and
  // 0x58 sit either side of that line in the same four-byte shape.
  //
  // ⚠ AND THE 8-BIT/16-BIT PAIRS ARE OTHERWISE BYTE-IDENTICAL: 0x51 vs 0x52 and 0x53 vs 0x54
  // and 0x58 vs 0x59 differ ONLY in where the `SEP #20` sits relative to the store. Reading
  // either member of a pair and generalising gets the other exactly half right.
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
    case 0x55: { val = r[0x10000];                                            // 699A, 8-BIT
                 dst = 0x10200 + ((script[(ip + 1) & 0xFFFF] * 2) & 0xFFFF);
                 K.len = 2; K.sw = 8; } break;
    case 0x56: { val = script[(ip + 1) & 0xFFFF];
                 r[0x01D9] = (uint8_t)val;         // 69AC STA $D9 -- 8-BIT, the REP is at 69AF
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

// THE POKE FAMILY -- ops 0x4A/0x4B/0x4C/0x4D, ROM C0/67D7 / 67FB / 680C / 6829: a byte or a word, literal or out
// of a LOCAL, through the 24-bit pointer C0/67E3 stages at $01D0-$01D2. One body, TWO callers like the families
// below: CtFieldRunnerOne (--sweepops) and the live event VM's step(), which had arms for 0x4A/0x4B only and none
// for 0x4C/0x4D -- so the `4C/4D 00 02 7E nn` of seven scenes (2, 5, 71, 223, 225, 277, 439; the fair's Silver
// Points among them) -- a local into the text's number slot $0200, read by the {num} codes C2/5953/5985/59C8
// through C2/5815's `LDX #0200 / STX $3D` -- never ran. `script` + `ip`
// for the reason CtVmItemFamily gives. Carry is SET on every exit. Returns 1 if `script[ip]` is in the family.
int CtVmPokeFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  switch (op) {
    case 0x4A: {
      // C0/67D7 -- WRITE BYTE THROUGH A SCRIPT-SUPPLIED 24-BIT POINTER. Branchless, IP+5, SEC.
      // Operands 1..3 are the pointer (lo/mid/bank) staged at $01D0/$01D1/$01D2; operand 4 is the
      // value, stored with `67DE STA [$D0]`. Nothing is masked or clamped -- the destination is
      // entirely script-supplied. Measured in situ: `W f253 $00111 01->00 @ C0/67DE  $6D=02`.
      int p = ctVmPtr67E3(r, script, ip);                       // 67D7 JSR C0/67E3
      ctVmPokeLong(r, p, script[(ip + 4) & 0xFFFF], 0);       // 67DA / 67DE STA [$D0]
      *outIp = (ip + 5) & 0xFFFF; return 1;        // 67E0 INX / SEC
    }
    case 0x4B: {
      // C0/67FB -- op 0x4A's SIXTEEN-BIT twin. `C2 20 REP #20` before the load makes both the
      // script read and the store two bytes wide, so it takes SIX bytes and pokes a word.
      int p = ctVmPtr67E3(r, script, ip);
      int v = script[(ip + 4) & 0xFFFF] | (script[(ip + 5) & 0xFFFF] << 8);  // 6800
      ctVmPokeLong(r, p, v, 1);                                      // 6804 STA [$D0] (16-bit)
      *outIp = (ip + 6) & 0xFFFF; return 1;        // 6808 INX / INX / SEC
    }
    case 0x4C:
    case 0x4D: {
      // C0/680C and C0/6829 -- poke a LOCAL VARIABLE rather than a literal: the fourth operand
      // indexes the $7F0200 word array and its contents go through the pointer.
      // ⚠ THESE TWO ARE BYTE-IDENTICAL EXCEPT FOR WHERE `E2 20 SEP #20` SITS. In 0x4C it comes
      // BEFORE the load, so both the fetch and the store are 8-bit; in 0x4D it comes AFTER the
      // store, so both are 16-bit. Same length either way -- reading one and generalising gets
      // exactly half of this right, which is the recurring trap for these SEP-position pairs.
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

// C0/5F74 -- op 0x00, the RETURN that partners op 0x02's priority CALL. Two paths, and only one of
// them is an "IP delta" at all.
//   prio == 7 -> 5FB3 TYX / CLC / RTS: writes NOTHING, IP UNCHANGED. The object parks on the 0x00
//                byte and re-runs this handler on every visit. Priority 7 means "nothing to return
//                to", so this is the idle terminator, not an advance.
//   prio <  7 -> clear our own $7F0580 slot (a 16-BIT store, both bytes), then walk DOWN in urgency
//                -- slot += 0x80 and `INC $1C00,X` PER STEP -- until a non-zero slot appears, and
//                return THAT WORD as the new IP. It is an absolute script address and can be lower
//                than the entry IP (measured: y=0749 -> x=073F, a BACKWARD jump), so this opcode
//                can never be modelled as `ip += n`.
// NOTE the intermediate $1C00 writes are real: the ROM emits prio+1, prio+2, ... one at a time, so a
// port that assigns the final value matches on state but not on a write TRACE.
static int ctVmOp00(uint8_t* r, int idx, int ip, int* carry) {
  int cur = r[0x1C00 + idx];                             // 5F76
  if (cur == 7) { *carry = 0; return ip; }               // 5F79 CMP #07 / 5F7B BEQ ->5FB3
  int sl = 0x10580 + (((cur * 0x80) + idx) & 0xFFFF);    // 5F7D-5F8F: the x128 hardware multiply
  r[sl] = 0; r[sl + 1] = 0;                              // 5F90 LDA #0000 / 5F93 STA $7F0580,X (16-bit)
  int p = cur;
  for (int guard = 0; guard < 8; guard++) {              // PORT GUARD: the ROM has no bound here and
                                                         // trusts a non-zero slot to exist.
    sl += 0x80;                                          // 5F97 TXA / CLC / ADC #0080
    p++;
    r[0x1C00 + idx] = (uint8_t)p;                        // 5FA0 INC $1C00,X -- once PER STEP
    int v = r[sl] | (r[sl + 1] << 8);                    // 5FA6 LDA $7F0580,X
    if (v) { *carry = 1; return v; }                     // 5FAA BNE -> 5FAC/5FAE TAX = the new IP
  }
  *carry = 1; return ip;                                 // unreachable in a well-formed scene
}

// ONE OPCODE, ONE DISPATCH -- the unit C0/5A93 actually executes per `JSR ($5D6E,X)`. Extracted from
// the loop below so a sweep can micro-execute a single handler and compare the CARRY and the RETURNED
// IP, which are the handler's real contract with the runner (5AB4 `BCC` yields the visit, 5ABF commits
// X as the new IP) and are invisible from outside the loop. Returns the trapped opcode, or -1.
// C0/6467 -- the COMPARISON PREDICATE TABLE the conditional-branch opcodes dispatch through
// (`6307 / 6433 JSR ($6467,X)` with X = mode*2). Each handler exits CLC when the predicate HOLDS
// (the shared exits C0/64B3 and C0/651F) and SEC when it does not -- so the branch opcodes take
// their jump on the SEC/FALSE answer, which is why they read as "jump if NOT".
//
// ⚠ THERE ARE SIXTEEN MODES, NOT EIGHT. 0-7 compare the 8-bit $01D9 against $01DB; 8-15 are the SAME
// eight predicates on the SIXTEEN-BIT pairs $01D9/$01DA vs $01DB/$01DC, and every one of those
// handlers opens with `C2 20 REP #20` (C0/64CF, 64DB, 64E7, 64F5, 6503, 6511, 6523, 652F).
// src/ct_vm.c's cmp_ masks the mode with `& 7`, which silently turns every 16-bit compare into an
// 8-bit one. FLAGGED, NOT FIXED HERE -- that is the live engine's copy and its own job.
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

// C0/6349 -- the TAKEN-BRANCH tail, SHARED by both conditional opcodes: 0x12 reaches it with
// `630A BCS ->6349` and 0x16 with `6436 BCC ->643B / 6438 BRL ->6349`.
//   6349 C2 10 REP #10 / 634B A6 C3 LDX $C3      X = ip+4, reloaded from the spill
//   634D BF 01 20 7F LDA $7F2001,X               the displacement, read AT ip+4
//   6351 C2 20 / 6353 29 FF 00 / 6356 85 D9      $01D9 <- disp, a SIXTEEN-BIT store
//   6358 8A TXA / 6359 18 CLC / 635A 65 D9 ADC $D9 / 635C AA TAX / 635F SEC
// ⚠ THE BASE IS ip+4 -- the displacement's OWN address -- not ip+5, and `6356 STA $D9` CLOBBERS the
// value that was just compared. Both are real and both are graded.
// ⚠ THE BASE IS NOT ALWAYS ip+4. It is whatever the CASE spilled to $01C3, i.e. the displacement's
// own address, and op 0x13 spills ip+5 because its comparand is a two-byte immediate. Ops 0x12,
// 0x14, 0x15 and 0x16 all spill ip+4. That is why the base is a parameter and not recomputed here.
static void ctVmBranchTakenAt(uint8_t* r, int base, CtHRet* h) {
  base &= 0xFFFF;
  int disp = r[0x12001 + base];
  r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;
  h->ip = (base + disp) & 0xFFFF; h->carry = 1; h->ok = 1;
}

// C0/3570 (op 0xBB) and its two siblings C0/35B5 (0xC1) / C0/35FC (0xC2) are ONE routine with
// a single hardwired difference: the value stored to $0130. `pos` is that value.
static void ctVmTextRequest(uint8_t* r, int idx, int ip, CtHRet* hp, int pos) {
  CtHRet h = *hp;

        // C0/3570 -- ARM A DIALOGUE/TEXT REQUEST AND WAIT ON IT. The most-used opcode the byte-exact
        // runner lacked: 3207 static sites, 2.8% of every opcode in the 504 parseable scenes.
        // FOUR arms, and only ONE of them consumes the opcode's two bytes.
        //
        // ⚠ ARM 1 IS THE ROM'S PANIC HANDLER, AND IT NEVER RETURNS -- AND ITS SENSE IS THE OPPOSITE
        // OF WHAT IT LOOKS LIKE. `3570 LDA $2D / 3572 D0 06 BNE ->357A` CONTINUES when $012D is
        // NON-zero; FALLING THROUGH -- i.e. $012D == 0 -- is what runs `3574 A2 E0 7F LDX #$7FE0`
        // (a SIXTEEN-BIT immediate, exactly where a linear disassembler desyncs) and then
        // `3577 82 A4 F8 BRL`, target 357A + (signed)$F8A4 = 357A - 1884 = C0/2E1E:
        //   SEI / A9 00 PHA PLB / LDA #80 STA $2100 (forced blank) / STA $4200 (NMI+IRQ off) /
        //   ... paints CGRAM from X, the #$7FE0 loaded above ... / 58 CLI / 80 FE BRA -2
        // a fatal-error hang on a coloured screen, with no return path. So $012D is a REQUIRED
        // precondition -- reaching op 0xBB without it is a ROM-level fatal error -- and there is no
        // honest C for a routine that never returns: the port TRAPS.
        // I WROTE THIS BACKWARDS FIRST and the byte-level reading did not catch it; single-stepping
        // the oracle did (`3570 3572 3574 3577 2E1E ...` with $012D == 0). A branch polarity is not
        // something a listing makes obvious -- BNE's target is the CONTINUE path here, not the arm.
        if (r[0x012D] == 0) { h.ip = ip; h.carry = 0; h.ok = 0; *hp = h; return; }
        // 357C: a request is already in flight -- park on the opcode and yield, changing nothing.
        if (r[0x0129] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; *hp = h; return; }   // 35B2 TYX / 35B3 CLC
        // 3584: OUR request has completed (the per-object flag is set) -- clear it and move on. This
        // is the ONLY arm that advances the IP.
        if (r[0x10A00 + idx] != 0) {
          r[0x10A00 + idx] = 0x00;                          // 35A7 LDA #00 / 35A9 STA $7F0A00,X
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; *hp = h; return;   // 35AD TYX/INX/INX + 35B0 SEC
        }
        // 3586: ISSUE the request, then park and yield until it completes.
        r[0x10A00 + idx] = 0x01;                            // 3586/3588
        r[0x012A] = r[0x12001 + ((ip + 1) & 0xFFFF)];       // 358C-358E operand / 3592 STA $2A
        r[0x012E] = r[0x016D];                              // 3594/3596 -- 8-bit, so the LOW byte only
        r[0x0132] = 0x00;                                   // 3598 STZ $32
        r[0x0129] = 0x01;                                   // 359A/359C STA $29 -- the in-flight latch
        r[0x0130] = (uint8_t)pos;   // 359E STZ $30 (0xBB) | 35E3/362A LDA #imm / STA $30
        // 35A0 LDA #20 / 35A2 `14 54` TRB $54: TEST-AND-RESET, so it CLEARS bit 5 of $0154 and leaves
        // every other bit alone. Writing $20 there instead would be a very quiet difference.
        r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x20);
        h.ip = ip; h.carry = 0; h.ok = 1; *hp = h; return;            // 35A4 TYX / 35A5 CLC -- park on the op
        *hp = h;
}

// C0/4B2C -- op 0x88's COLOUR-EFFECT SLOT ALLOCATOR. Walks $0520 in steps of 12 as far as $60, so
// EIGHT twelve-byte slots, and a slot is free when its first byte is zero. Returns the byte OFFSET
// (the ROM leaves it in Y, carry CLEAR) or -1 when the table is full (carry SET).
static int ctVmOp88Alloc(const uint8_t* r) {
  for (int y = 0; y < 0x60; y += 12)                     // 4B38 ADC #000C / 4B3E CPY #0060
    if (r[0x0520 + y] == 0) return y;                    // 4B2F LDA $0520,Y / 4B32 BEQ ->4B45
  return -1;                                             // 4B47 SEC
}

// Op 0xE2, ROM C0/3B9E: STAGE A LOCATION CHANGE OUT OF FOUR LOCALS -- op 0xE0's fade variant (C0/3AB7: the same $0117
// latch, the same $0119 counter C0/0C8A drains, the same park), with every operand the INDEX of a local instead of a
// literal. Runs with D = $0100:
//   3B9E LDA $17 / BMI ->3C02                    a change already pending: 3C02 TYX / CLC -- park
//   3BA2 STY $C7                                 the ENTRY IP (16-bit)
//   3BA4 INY / TYX / LDA $7F2001,X / REP #20 / AND #00FF / ASL / TAX / LDA $7F0200,X / STA $12
//                                                the location = the local's whole WORD (no AND #01FF, unlike 3AC5)
//   3BB7 .. SEP #20 / LDA $7F0200,X / STA $14    tile X  = the second local's low byte
//   3BCA .. STA $15                              tile Y  = the third's
//   3BDF .. STA $16                              the facing byte = the fourth's, as it is (no 3AD4 LSR / TSB build)
//   3BF4 LDA $17 / ORA #80 / STA $17 / LDA #0F / STA $19
//   3BFE LDX $C7 / CLC / RTS                     X = the entry IP: the object parks ON the op
// The inns (loc 00C's keeper: `56 0C 66 01` .. then `E0 0F 03 08 08` to loc 10F) leave the way back in $7F0166..016C,
// and loc 10F's obj0 copies it into locals 0A-0D (`53 68 01 0A` ..) and leaves with `E2 0C 0A 0B 0D`. Two callers, like
// CtVmColorSlotOp: CtFieldRunnerOne (--sweepops) and the live VM's step(). `script` = the $7F:2001 base.
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

// C0/4892 -- op 0x88, the per-object colour/palette effect. MOVED HERE VERBATIM out of CtFieldRunnerOne's switch --
// only the operand source (`r[0x12001 + ip]` -> `script[ip]`) and the result protocol (`h.x = ...; break` ->
// `out->x = ...; return`) changed -- so the LIVE event VM in ct_vm.c runs THIS body, which --sweepops micro-executes
// against the ROM, instead of the C mirror it kept (a flag, a rotation and a blob copy, and NOTHING for the 682
// `88 4x` / `88 5x` sites the scene scripts carry). Same reason and same signature shape as CtVmItemFamily: the
// runner here passes `r + 0x12001` ($7F:2001), the live VM its host copy of the body. The blob's MVN source is
// $7F:2001 + ip + 4 as well, so it reads the script pointer too. `idx` = obj*2. Always handles the op (out->ok = 1).
// Op 0x2E, ROM C0/4A4E: claim a colour-effect record ($40 / $50 arms: a $0520 slot through C0/4B2C, the FD/E82C / FD/E5A4
// fades the VM pass ticks), copy an inline palette blob twice ($80 arm), or a two-byte no-op. One body, TWO callers
// like CtVmColorFx: CtFieldRunnerOne (--sweepops) and the live VM's step() + init pass. `script` is the ROM's
// $7F:2001 base (the IP counts from it); the carry comes back in `out` (CLC = the slot table was full).
void CtVmColorSlotOp(uint8_t* r, const uint8_t* script, int ip, CtHRet* out) {
  // C0/4A4E -- ONE opcode, THREE unrelated jobs, selected by the HIGH NIBBLE of its first
  // operand: $40 and $50 both claim a colour-effect slot, $80 copies an inline data blob, and
  // everything else is a two-byte no-op. The three arms have three different lengths.
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 4A4E STY $C7 (16-bit)
  int o1 = script[(ip + 1) & 0xFFFF];                    // 4A52
  r[0x01D9] = (uint8_t)o1;                                      // 4A56 STA $D9
  int hi = o1 & 0xF0;                                           // 4A58
  if (hi == 0x40 || hi == 0x50) {                               // 4A5A / 4A61 -- BOTH to 4A72
    int y = ctVmOp88Alloc(r);                                   // 4A72 JSR C0/4B2C
    // A FULL TABLE STILL EATS THE SIX BYTES. It differs from success in the CARRY alone, so
    // the operand is consumed and the visit yields rather than retrying next frame.
    if (y < 0) { out->ip = (ip + 6) & 0xFFFF; out->carry = 0; out->ok = 1; return; }   // 4A77 x6 / CLC
    r[0x0520 + y] = (uint8_t)(o1 & 0xF0);                       // 4A85
    r[0x0526 + y] = (uint8_t)(o1 & 0x0F);                       // 4A8C
    r[0x0521 + y] = script[(ip + 2) & 0xFFFF];           // 4A97
    r[0x0522 + y] = script[(ip + 3) & 0xFFFF];           // 4A9F
    int dd = script[(ip + 4) & 0xFFFF];                  // 4AA3
    r[0x01DD] = (uint8_t)dd;                                    // 4AA7 STA $DD -- EIGHT bit here
    r[0x0525 + y] = script[(ip + 5) & 0xFFFF];           // 4AAE
    r[0x0524 + y] = 0x00;                                       // 4AB3
    r[0x0523 + y] = 0x08;                                       // 4AB8
    // The last two fields each SPLAT ONE NIBBLE ACROSS BOTH: the high nibble is OR'd with
    // itself shifted down, the low nibble with itself shifted up.
    r[0x01D9] = (uint8_t)((dd & 0xF0) >> 4);                    // 4AC0 x4 LSR / 4AC4 STA $D9
    r[0x0527 + y] = (uint8_t)((dd & 0xF0) | ((dd & 0xF0) >> 4));    // 4ACC
    r[0x01D9] = (uint8_t)((dd & 0x0F) << 4);                    // 4AD3 x4 ASL / 4AD7 STA $D9
    r[0x0528 + y] = (uint8_t)((dd & 0x0F) | ((dd & 0x0F) << 4));    // 4ADF
    out->ip = (ip + 6) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 4AE2 SEC
  }
  if (hi != 0x80) {                                             // 4A68
    out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 4A6F INX / SEC
  }
  // 4AE4 -- COPY AN INLINE BLOB TWICE. The operands are a table index and a SIXTEEN-bit
  // length, and the payload follows them in the script, so the IP advance is script-driven.
  int o2 = script[(ip + 2) & 0xFFFF];                    // 4AE8
  int ddw = (o2 * 2) & 0xFFFF;                                  // 4AF1 ASL A (16-bit)
  r[0x01DD] = (uint8_t)ddw; r[0x01DE] = (uint8_t)(ddw >> 8);    // 4AF2 STA $DD -- SIXTEEN bit,
                                                                // unlike the same store above
  // ⚠ A SIXTEEN-BIT SCRIPT READ: `REP #20` at 4AEC is still in force, so 4AFA takes TWO bytes.
  int word = script[(ip + 3) & 0xFFFF] | (script[(ip + 4) & 0xFFFF] << 8);
  int cnt = (word - 3) & 0xFFFF;                                // 4AFE x3 DEC A
  r[0x01C1] = (uint8_t)cnt; r[0x01C2] = (uint8_t)(cnt >> 8);    // 4B01 STA $C1 (16-bit)
  int src = (ip + 5 + 0x2001) & 0xFFFF;                         // 4B05/4B06 TXA / ADC #2001
  r[0x01DB] = (uint8_t)src; r[0x01DC] = (uint8_t)(src >> 8);    // 4B0A STA $DB (16-bit)
  // `54 7E 7F` -- MVN takes the DESTINATION BANK FIRST, so this reads bank $7F (the script)
  // and writes bank $7E. It moves A+1 bytes, and A is the length ALREADY REDUCED BY THREE.
  // The second move re-reads the SAME source from $DB and lands $200 lower.
  for (int pass = 0; pass < 2; pass++) {
    int dstv = ((pass ? 0x2000 : 0x2200) + ddw) & 0xFFFF;       // 4AF5 / 4B16
    for (int k = 0; k <= cnt; k++)
      r[(dstv + k) & 0xFFFF] = script[(ip + 5 + k) & 0xFFFF];   // = $7F:(src + k), src = ip + 5 + $2001
  }
  // 4B22 TXA / SBC #2001 -- X ends past the payload, so this lands on ip + word + 3.
  out->ip = (ip + word + 3) & 0xFFFF; out->carry = 1; out->ok = 1; return;
}

void CtVmColorFx(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  // C0/4892 -- THE PER-OBJECT COLOUR/PALETTE EFFECT. Five arms selected by the operand, each
  // with its OWN LENGTH, and one of them variable. This is the opcode [[ct-field-op88-color]]
  // records as "a COLOUR effect, NOT a draw flag".
  //
  //   operand == $00              -> 48C0  RESTORE: free the slot and re-upload the palette
  //   operand == $20 or $30       -> 4919  a 4-byte effect  (EXACT values, tested before the mask)
  //   (operand & $F0) == $40/$50  -> 4970  a 5-byte effect
  //   (operand & $F0) == $80      -> 49F8  a VARIABLE-length direct palette upload
  //   anything else               -> 48BD  ip+2, SEC, no writes at all
  //
  // 4892 `STY $C7` spills the OPCODE'S OWN IP (the handler never executes INY), and every arm
  // advances from that spill, which is why their lengths can differ so freely.
  //
  // THE SLOT MACHINE: C0/4B2C walks $0520 in steps of 12 up to $60 -- EIGHT twelve-byte slots
  // -- and a slot is free when its first byte is 0. It returns the byte OFFSET in Y with the
  // carry CLEAR, or SEC when full. On a full table the two allocating arms still consume
  // their operands and exit CLC, i.e. they yield the visit and retry, rather than skipping.
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 4892 STY $C7 (16-bit)
  int sel = script[(ip + 1) & 0xFFFF];                   // 4896
  if (sel != 0) r[0x01D9] = (uint8_t)sel;                       // 489C -- NOT on the zero arm
  int pal = r[0x0F81 + idx];
  if (sel == 0) {
    // ---- 48C0 RESTORE ----------------------------------------------------------------
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
      r[(dst + k) & 0xFFFF] = (s < romLen) ? rom[s] : 0;        // 48F8 MVN $7E,$E4
    }
    for (int k = 0; k < 0x18; k++) {                            // 4902/4905 -- the SAME source
      int s = 0x240000 + ((src + k) & 0xFFFF);                  //   again, dest + $200
      r[(dst + 0x200 + k) & 0xFFFF] = (s < romLen) ? rom[s] : 0;
    }
    out->ip = (ip + 2) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 490B
  }
  if (sel == 0x20 || sel == 0x30) {
    // ---- 4919: a four-byte effect. The two selectors are compared WHOLE, at 489E and 48A2,
    // BEFORE the `48A6 AND #F0` -- so $21 is not $20 here, unlike the $4x/$5x/$8x arms.
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
    // ---- 4970: a five-byte effect, selected on the MASKED nibble.
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
    // 49D0-49F3: each nibble of operand 3 is REPLICATED into both halves of its own byte.
    r[0x01D9] = (uint8_t)((o3 & 0xF0) >> 4);                    // 49D8
    r[0x0527 + y] = (uint8_t)((o3 & 0xF0) | ((o3 & 0xF0) >> 4));// 49E0
    r[0x01D9] = (uint8_t)((o3 & 0x0F) << 4);                    // 49EB
    r[0x0528 + y] = (uint8_t)((o3 & 0x0F) | ((o3 & 0x0F) << 4));// 49F3
    out->ip = (ip + 5) & 0xFFFF; out->carry = 1; out->ok = 1; return;     // 49F6
  }
  if ((sel & 0xF0) == 0x80) {
    // ---- 49F8: a DIRECT PALETTE UPLOAD, and the only VARIABLE-LENGTH opcode in this file.
    // `4A1C LDA $7F2001,X` is a SIXTEEN-BIT read (the REP #20 at 4A0B is still in force), so
    // the COUNT is a word at ip+2, and `4A20 DEC A` three times makes MVN move count-2 bytes.
    // The exit reconstructs the IP from X after the moves: X ends at source + (count-2) and
    // `4A46 SBC #2001` removes the $7F2001 base, giving ip + count + 2.
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

// op 0x80's two workers live next to CtObjLoad, further down, because they share its descriptor
// walk; the runner needs them here.
static int  ctPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn);
static void ctPcPlaceDefault(uint8_t* r, int idx);

// C0/4B9F-4BBD and C0/4C4F-4C6E -- ONE tile-attribute block with TWO entries. Op 0x8B reaches it
// with a tile built from its own two operands; op 0x8D reaches it with the object's CURRENT tile,
// i.e. the one the pixel coordinates it just stored happen to land on. Byte for byte the two copies
// are the same instructions in the same order, so they are one function here rather than two.
//
// `4B9F`/`4BAB` (and `4C4F`/`4C5B`) are `BF` = LDA long,X, which DOES cross the bank boundary, so
// $7E7000 + t can reach into $7F -- and r[] is one flat image, so a plain add is already correct.
// The stride is why the planes at +$00 and +$40 do not overlap: 64 tiles per row on a 256-byte row
// pitch, the same layout as the player's plane at $7E7080.
static void ctVmTileAttr(uint8_t* r, int idx, int t) {
  int a1 = ((r[0x7000 + t] & 0x40) >> 2) | 0x20;     // AND #40 / LSR / LSR / ORA #20
  r[0x01D9] = (uint8_t)a1;                           // 4BA9 / 4C59 -- $D9 is REUSED, not the tile now
  int a2 = ((r[0x7040 + t] & 0x40) >> 2) | 0x20;     // the SECOND plane
  r[0x0C00 + idx] = (uint8_t)a2;                     // 4BB7 / 4C67 -- from $7E7040
  r[0x0C01 + idx] = (uint8_t)a1;                     // 4BBA / 4C6A -- from $7E7000, via $D9
}

// C0/426A -- THE JOIN POINT op 0x80 AND ITS SEVEN IMMEDIATE SIBLINGS SHARE. Everything from the
// party test down is one routine; what differs upstream is only how the PC id arrived and how far
// Y moved getting it. op 0x80 fetched an operand with `421E INY` (so Y = ip+1 here); ops 0x57/0x5C/
// 0x62/0x68/0x6A/0x6C/0x6D hardwire the id and never touch Y at all (so Y = ip). C0/448D's own INY
// then applies equally, which is exactly why the same code gives 0x80 a TWO-byte encoding and the
// seven siblings a ONE-byte one. `yIn` is Y at 426A.
static void ctVmPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn,
                       CtHRet* hp) {
  // 426C / 4276 / 4281 -- the three ACTIVE PARTY slots. $7E2980-2982; see [[ct-vm-party-ops]].
  int slot = (id == r[0x2980]) ? 0 : (id == r[0x2981]) ? 1 : (id == r[0x2982]) ? 2 : -1;
  if (slot < 0) {
    // 428C -- not in the party. The ONLY arm that never calls C0/448D, hence the only one
    // whose Y is still ip+1, which is exactly why it supplies the INX at 4299 that the other
    // three do not need. All four arms return ip+2.
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
  r[0x1B01 + idx] = 0x01;                              // 42AB -- C0/448D does NOT write this
  r[0x1101 + idx] = (uint8_t)id;                       // 42B0 LDA $D9 / STA $1101,X
  // 42B3 `STX $97` is a SIXTEEN-BIT store: X is wide from the runner's 5A9E REP #10 and
  // nothing narrows it on this path, so it writes $0197 AND $0198. The same is true of
  // `STX $0B89` below, which therefore also touches $0B8A -- slot 1's own entry. That is the
  // ROM's behaviour, not a transcription slip: slot 1 overwrites it when its turn comes.
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
    // The leader always takes the default placement. In the ROM its five stores straddle the
    // block above (42BA/42BF before it, 42E1/42E8/42FA after); every address is distinct, so
    // doing them together here cannot change the resulting image. It DOES matter that they
    // come after C0/448D, which sets $1600,X = 1 at 454D and is overwritten at 42E5.
    ctPcPlaceDefault(r, idx);
  } else {
    r[0x01AB] = 0x00; r[0x01AC] = 0x00; r[0x01AD] = 0x00;   // 4356 / 4409 STZ $AB/$AC/$AD --
                                                // the party path-history cursor
    int lead = r[0x0197];                              // 435C / 440F LDA $97 (8-bit)
    if (lead & 0x80) ctPcPlaceDefault(r, idx);         // 435E BMI -- bit 7 = NO LEADER
    else {
      // 4360-4386: follow the leader. THE ROM USES TWO DIFFERENT INDEX WIDTHS HERE and they
      // are only equal because of a value it guarantees elsewhere. 4362 `AND #00FF` / 4365
      // `TAX` zero-extends the byte for the $1800 read, but 436E and 437D are plain
      // `LDX $97`, SIXTEEN-BIT, so those two read $0197 AND $0198. $0198 is 0 in every state
      // the engine can produce -- the same 16-bit `STX $97` at 42B3 writes it -- so the two
      // coincide in practice. Written as the ROM computes them rather than collapsed,
      // because collapsing them is an assumption about $0198 rather than a reading of the
      // instruction. --sweepops cannot tell the two apart: probing it needs $0198 non-zero,
      // which sends these reads outside the flat model's address space (see its seeding).
      int leadW = (r[0x0197] | (r[0x0198] << 8)) & 0x1FFFF;
      r[0x1800 + idx] = r[0x1800 + lead];              // 4366 / 436B, via the ZERO-EXTENDED X
      r[0x1801 + idx] = r[0x1801 + lead];
      r[0x1880 + idx] = r[0x1880 + leadW];             // 4370 / 4375, via the WIDE X
      r[0x1881 + idx] = r[0x1881 + leadW];
      r[0x1900 + idx] = 0x00; r[0x1901 + idx] = 0x00;  // 4378 STZ $1900,X -- SIXTEEN-bit, so
      r[0x1600 + idx] = r[0x1600 + leadW];             // 437F / 4384, also via the WIDE X    it clears BOTH
    }
  }
  hp->ip = (yIn + 1) & 0xFFFF; hp->carry = 1; hp->ok = 1;
}


// ---- THE $C2 PARTY / ROSTER / INVENTORY-COUNT FAMILY -- C0/388D (0xCF), 38BE (0xD0), 38D3 (0xD1),
// 38E8 (0xD2), 37F9 (0xD7), 3919 (0xD3), 394E (0xD4), 3E5C/3E6E/3E75 (0xF8/0xF9/0xFA). Ten opcodes
// over 896 static sites, all of them reaching `JSL $C2:8004` with a leaf index.
//
// MOVED HERE VERBATIM out of CtFieldRunnerOne's switch -- only the result protocol changed
// (`h.x = ...; break` -> `out->x = ...; return 1`) -- so the LIVE event VM in ct_vm.c can run THIS
// body instead of a second transcription. Same reason as CtVmItemFamily: --sweepops micro-executes
// these ten opcodes against the ROM through CtFieldRunnerOne, so the live VM inherits that grade
// rather than keeping its own copy of the rule (disasm/notes/vm_two_implementations.md).
//
// ⚠ UNLIKE THE $C1 ITEM FAMILY THE CARRY IS NOT CONSTANT: 0xD0/0xD1/0xD3 END the visit (carry 0 --
// their handlers contain no SEC/CLC anywhere, and the entry carry is 0 because the runner's dispatch
// does a SIXTEEN-bit ASL), 0xD4's carry is the last `CMP $D9` that ran, and the rest set it. So the
// carry comes back through `out` instead of being assumed by the caller.
//
// ⚠ AND THE BRANCH POLARITY IS THE OPPOSITE OF THE $C1 SIDE'S: 0xCF/0xD2 take the jump when the leaf
// returns NONZERO (the PC is ABSENT), where 0xC9/0xCC take theirs on zero.
//
// `script` + `ip` for the same reason CtVmItemFamily takes them: CtFieldRunnerOne runs on the ROM's
// own addresses and passes `r + 0x12001` ($7F:2001), while ct_vm.c keeps the scene body in host
// memory. Every other address here is WRAM and identical for both callers.
// Returns 1 if `script[ip]` is in the family, 0 if not.
int CtVmPartyGateFamily(uint8_t* r, const uint8_t* rom, int romLen,
                        const uint8_t* script, int ip, CtHRet* out) {
  int op = script[ip];
  switch (op) {
      case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD7: {
        // THE $C2 PARTY GATE'S SIMPLE HALF -- C0/388D (0xCF), 38BE (0xD0), 38D3 (0xD1), 38E8 (0xD2),
        // 37F9 (0xD7). All five share one prologue, byte for byte:
        //   TYX / INX / TDC / XBA / LDA $7F2001,X / INX / STX $C7 / <stage> / LDA #leaf / JSL $C2:8004
        // so the operand is at ip+1 and $01C7 (SIXTEEN bits) gets ip+2 before the call.
        //
        // ⚠ THE STAGING INSTRUCTION IS WHERE THEY DIVERGE, and it decides what the gate's `STY $C0`
        // and `STX $C9` record. 0xCF/0xD0/0xD1/0xD2 do `TAY`, so Y is the operand and X is still
        // ip+2. 0xD7 does `TAX` instead -- so X is the ITEM ID (leaf 12 reads it back out of $04C9)
        // and Y is never touched, leaving the RUNNER'S OWN Y, the raw IP, to land in $04C0.
        int arg = script[(ip + 1) & 0xFFFF];                   // 38EC / 37FD
        int spill = (ip + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 38F1 STX $C7, 16-bit
        int leaf = (op == 0xD2) ? 0 : (op == 0xCF) ? 3 : (op == 0xD0) ? 4 : (op == 0xD1) ? 5 : 12;
        int a = ctVmC2Gate(r, rom, romLen, leaf, op == 0xD7 ? ip : arg, op == 0xD7 ? arg : spill, 0);
        if (op == 0xD0 || op == 0xD1) {
          // 38D0 `LDX $C7 / RTS`. No SEC, no CLC, and none anywhere else in either handler -- the
          // gate's PHP/PLP hands back the entry carry, which is 0. THE VISIT ENDS. 70 sites.
          out->ip = spill; out->carry = 0; out->ok = 1; return 1;
        }
        if (op == 0xD7) {
          // 380B STA $D9 (8-bit) then the count is mirrored into the scene array at operand2*2.
          r[0x01D9] = (uint8_t)a;                                     // 380B
          int slot = script[spill];                              // 380F LDA $7F2001,X
          r[0x10200 + ((slot * 2) & 0xFFFF)] = (uint8_t)a;            // 381E STA $7F0200,X
          out->ip = (spill + 1) & 0xFFFF; out->carry = 1; out->ok = 1; return 1;  // 3822 LDX $C7 / INX / SEC
        }
        // 0xCF and 0xD2: `CMP #00 / BNE ->branch`. THE JUMP IS TAKEN WHEN THE LEAF RETURNED NONZERO,
        // i.e. when the PC is ABSENT -- the opposite polarity to the $C1 side's 0xC9/0xCC, which
        // branch on zero. The displacement is read at the spill and added to it.
        if (a == 0) { out->ip = (spill + 1) & 0xFFFF; out->carry = 1; out->ok = 1; return 1; }   // 38FE/3900/SEC
        int disp = script[spill];                                // 3905 / 38AA
        r[0x01D9] = (uint8_t)disp; r[0x01DA] = 0x00;                  // 390E STA $D9, 16-bit after
        out->ip = (spill + disp) & 0xFFFF; out->carry = 1; out->ok = 1; return 1; //   AND #00FF; 3912 ADC $D9
      }
      case 0xD3: {
        // C0/3919 -- JOIN. Leaf 1 puts the char id in the active party, then the handler wires the
        // OBJECT: $018D + charid holds that PC's object index, and it goes into the first of
        // $0199 / $019B that is empty. Length 2, and the visit ENDS (entry carry, see above).
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
        // C0/394E -- LEAVE. The only member of the family that does real work BEFORE the gate: it
        // scans the three party-OBJECT slots $0197/$0199/$019B, and for each non-empty one compares
        // $1101,X (that object's char id) against the operand.
        //
        // ⚠ THE SCAN STOPS AT THE FIRST EMPTY SLOT, it does not skip past it, and `3998 BMI ->39B1`
        // jumps INTO THE COMPACTION rather than falling through -- so an empty slot 1 still shifts
        // $019B up and still runs the palette shift, without slot 2 ever being compared.
        //
        // ⚠ AND THE CARRY IS THE LAST `CMP $D9` THAT RAN (or the entry 0 if none did), because the
        // gate's PHP/PLP carries it across the JSL and `39DE LDX $C7 / RTS` is all that follows. The
        // FOUND path therefore always continues -- CMP sets carry on equality.
        int arg = script[(ip + 1) & 0xFFFF];
        int spill = (ip + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 3955 STX $C7, 16-bit
        r[0x01D9] = (uint8_t)arg;                                     // 3957 STA $D9 -- EIGHT bits,
        int carry = 0, gx = spill, done = 0;                          //   $01DA is NOT touched here
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
        // C0/3E5C / 3E6E / 3E75 -- HEAL THE PARTY. ONE byte each: `INY / STY $C7` and nothing is
        // read from the script at all. 0xF9 runs leaf 6 (recompute max HP), 0xFA runs leaf 7
        // (refill MP), 0xF8 runs BOTH -- so its epilogue runs twice.
        //
        // ⚠ X AT THE JSL IS THE DISPATCH'S opcode*2, because these three never load X. That is what
        // makes op 0xF8's two calls write DIFFERENT $04C9 values: $F0 the first time, then $30, the
        // X the gate itself left behind (C2/82E1 always ends its record loop at $2830, narrowed to
        // eight bits by C2/834D's PLP). Getting this from a final-state compare alone is impossible
        // -- only the second write survives.
        int spill = (ip + 1) & 0xFFFF, ey = spill;
        r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);   // 3E5D STY $C7, 16-bit
        if (op != 0xFA) ctVmC2Gate(r, rom, romLen, 6, spill, op * 2, &ey);   // 3E5F LDA #06 / 3E61
        // ⚠ THE SECOND CALL SEES NEITHER OF THE FIRST CALL'S ARGUMENTS. X is $30 (C2/82E1 always
        // ends its record loop at $2830, narrowed to eight bits by C2/834D's PLP) and Y is whatever
        // that same loop's `8306 TAY` last stored. So $04C9 goes $F0 -> $30 and $04C0 goes ip+1 ->
        // a relic id. A final-state compare only ever sees the SECOND of each pair.
        if (op != 0xF9) ctVmC2Gate(r, rom, romLen, 7, op == 0xF8 ? ey : spill,
                                   op == 0xF8 ? 0x30 : op * 2, 0);
        out->ip = spill; out->carry = 0; out->ok = 1; return 1;                   // 3E6B LDX $C7 / RTS -- yields
      }
    default: return 0;
  }
}

// THE POINTER READ -- ops 0x48/0x49, ROM C0/6792 / C0/67C7: a byte or a word through a script-supplied 24-bit pointer
// into a local. One body, the two callers the poke family has: CtFieldRunnerOne (--sweepops) and the live event
// VM (src/ct_vm.c step() and its init pass), which had a byte-only 0x48 paraphrase and NO 0x49 -- so the 77 `49 ..`
// sites never ran, among them every `49 05 01 7E nn` a location's obj0 init uses to ask where the party came from
// ($0105): scene 65 then never set $7F01A3 bit 3 for an arrival from $01F0. Carry SET. Returns 1 if in the family.
int CtVmReadPtrFamily(uint8_t* r, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  if (op != 0x48 && op != 0x49) return 0;
  // C0/6792 and C0/67C7 -- READ A BYTE (0x48) or WORD (0x49) THROUGH A SCRIPT-SUPPLIED 24-BIT
  // POINTER into a local variable. They are op 0x4A's counterparts -- that one WRITES through
  // a pointer staged at $01D0-$01D2, these READ through one staged at $01CD-$01CF by the
  // shared prologue C0/67A2 -- and the two differ ONLY in where the `SEP #20` sits: 0x48 has
  // it at 6795 BEFORE the load, 0x49 at 67D0 AFTER the store, so 0x48 moves one byte and 0x49
  // moves two. Same five-byte encoding, same SEC, one instruction apart.
  //
  // 67BD `STX $D9` is SIXTEEN-BIT (the REP #20 is at 67BF, after), so it spills ip+4 across
  // $01D9 AND $01DA, and 679D `LDX $D9` is what the exit advances from -- ip+4 then INX.
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
      int fb = ctFlatAddr24(bk, (((mid << 8) | lo) + 1) & 0xFFFF);   // from the SAME bank,
      r[a + 1] = (fb >= 0) ? r[fb] : 0;              //   the offset wrapping within it
    }
  }
  *outIp = ip + 5;
  return 1;
}

// THE LOCAL INC / DEC / SET -- ops 0x71/0x72/0x73/0x75/0x76/0x77, ROM C0/6C69 / 6C85 / 6CA1 / 6CBD / 6CD6 / 6CF0. Same two
// callers as the arithmetic below. The live VM had 8-bit paraphrases of 0x71/0x73/0x75/0x77 and NO arm for the two
// SIXTEEN-bit ones, 0x72 and 0x76, which it stepped over -- the tent of horrors' hostage counter `72 12` (scene 434, the
// PCs' fn7 at 00DC: the sparkle at B2, the flames at 0120) never moved, so the hostage sank forever.
// C0/6C69 / 6C85 / 6CA1 / 6CBD / 6CD6 / 6CF0 -- SIX writers of the SCENE-LOCAL array at
// $7F0200 + var*2. The stride is TWO, the same array and stride op 0x12 compares against.
// All six share one skeleton -- `INY / TYX / LDA $7F2001,X / REP #20 / AND #00FF / ASL A /
// TAX`, the operation, then `INY / TYX / SEC / RTS` -- so two bytes, SEC, no yield.
//
// ⚠ THEY ARE NOT ALL 8-BIT, AND THE SHAPE DOES NOT SHOW IT. The `SEP #20` sits BEFORE the
// access in 0x71 (6C76), 0x73 (6CAE), 0x75 (6CCA) and 0x77 (6CFD), but AFTER it in 0x72
// (6C9B) and 0x76 (6CEA) -- so those two run SIXTEEN-BIT and touch the byte above the
// variable as well. Reading the family as one shape writes half of it wrong.
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
    case 0x72: { int w = ((r[a] | (r[a + 1] << 8)) + 1) & 0xFFFF;     // 6C92-6C97, SIXTEEN-bit
                 r[a] = (uint8_t)w; r[a + 1] = (uint8_t)(w >> 8); } break;
    case 0x76: r[a] = 0x01; r[a + 1] = 0x00; break;                   // 6CE3-6CE6, SIXTEEN-bit
  }
  *outIp = ip + 2;
  return 1;
}

// THE LOCAL ARITHMETIC -- ops 0x5B/0x5E/0x5F/0x60/0x61/0x63/0x64/0x67/0x69/0x6B/0x6F, ROM C0/6A1F..6C68. Same two
// callers; the live VM had byte paraphrases of six of them (0x63/0x64 through `1 << (b & 7)`, not the ROM's $FF20 /
// $FF28 tables) and NO arm for 0x5E/0x60/0x61/0x6B, which it stepped over -- the fair's obj0 fn0 `5E 15 22` /
// `61 15 20` among them. `rom` is for the two mask tables. Carry SET. Returns 1 if `script[ip]` is in the family.
int CtVmLocalArithFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int* outIp) {
  int op = script[ip];
  switch (op) {
    case 0x5B: case 0x5E: case 0x5F: case 0x60: case 0x61:
    case 0x63: case 0x64: case 0x67: case 0x69: case 0x6B: case 0x6F: break;
    default: return 0;
  }
  // C0/6A1F..6C68 -- THE LOCAL-VARIABLE ARITHMETIC FAMILY. Eleven read-modify-write handlers
  // on $7F0200 + index*2, all SEC, all three bytes except 0x60, which is four:
  //
  //   op  source                          op    width
  //   5B  immediate byte  @ip+1           +=     8
  //   5E  local[byte @ip+1]               +=    16
  //   5F  immediate byte  @ip+1           -=     8
  //   60  immediate WORD  @ip+1           -=    16   (four bytes; dst index at ip+3)
  //   61  local[byte @ip+1]               -=     8
  //   63  C0:$FF20[byte @ip+1]            |=     8
  //   64  C0:$FF28[byte @ip+1]            &=     8
  //   67  immediate byte  @ip+1           &=     8
  //   69  immediate byte  @ip+1           |=     8
  //   6B  immediate byte  @ip+1           ^=     8
  //   6F  immediate byte  @ip+1           >>=    8   (a LOOP: `LSR A / DEC $D9 / BNE`)
  //
  // ⚠ 0x63 AND 0x64 ARE NOT PLAIN OR/AND. They read the SAME bit-mask tables as ops 0x65/0x66
  // -- but WITHOUT the `AND #0F` those two apply, so the index here is the FULL operand byte
  // and can run right off the sixteen-entry table into whatever follows. 0x63/0x69 and
  // 0x64/0x67 look like duplicate pairs and are not: one member of each takes its operand
  // through a table and the other uses it raw.
  //
  // ⚠ AND 0x6F's SHIFT COUNT IS A DO-WHILE, NOT A WHILE: `6C5B LSR A / 6C5C DEC $D9 / 6C5E
  // BNE ->6C5B` shifts FIRST and tests after, so a count of 0 shifts 256 times, not none.
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
    int mt = (op == 0x63) ? 0xFF20 : 0xFF28;                        // 6B29 / 6B54 -- NO mask
    src = (mt + o1 < romLen) ? rom[mt + o1] : 0;
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
    // 6C5B `LSR A / 6C5C DEC $D9 / 6C5E BNE ->6C5B`. TWO things here: it is a DO-WHILE, so a
    // count of 0 shifts 256 times rather than none; and `DEC $D9` decrements MEMORY, not a
    // register -- the counter IS $01D9 and the loop leaves it at 0 on every path. Keeping the
    // count in a local and never writing it back is what the first version did, and the sweep
    // said so at once (`$001D9 C=01 RO=00`).
    default: { res = cur;
               do { res = (res & 0xFF) >> 1;
                    r[0x01D9] = (uint8_t)(r[0x01D9] - 1); } while (r[0x01D9] != 0); } break;
  }
  r[a] = (uint8_t)res;
  if (wide) r[a + 1] = (uint8_t)(res >> 8);
  *outIp = ip + len;
  return 1;
}

// THE FACING FAMILY -- ten opcodes, ONE ROM tail, C0/554B-5556: `LDX $6D / STA $1600,X / STZ $1601,X / TYX / INX / CLC /
// RTS`. They differ only in where the value comes from and which object X names when the tail runs:
//   0x0F / 0x17 / 0x1B / 0x1D   C0/5535 / 5539 / 553D / 5541   `LDA #imm / BRA ->554B`   facing 0/1/2/3, self, 1 byte
//   0xA6                        C0/5545   `INY / TYX / LDA $7F2001,X` -> 554B              the operand, self, 2 bytes
//   0xA7                        C0/5557   the operand indexes the local array $7F0200 (stride 2) -> 554B, self, 2 bytes
//   0x1E / 0x1F / 0x25 / 0x26   C0/556C / 5579 / 5586 / 5593 `INY / TYX / TDC / XBA / LDA $7F2001,X / TAX / LDA #imm /
//                               BRA ->554D`: facing 0/1/2/3 on the OBJECT THE OPERAND NAMES, 2 bytes
// ⚠ THE TAIL ZEROES THE WALK-ANIM TIMER ($1601,X, the countdown C0/C98A decrements): the object whose facing changed
// re-enqueues on its NEXT anim leg instead of when its old countdown runs out. The live VM had its own copy of five of
// these (0x0F/0x17/0x1B/0x1D/0xA6) that stored the facing only, and none of the other five: a PC turned by a script
// kept its old cadence -- MEASURED, the Manoria nuns (saves/field_re/m81_nuns_lake.state, `--monitor 7E1602:2 --writer`):
// obj1's facing 02 -> 03 by C0:5550 at f36 with $1603 reloaded to 05 by C0:CA48 the same frame, where the port counted
// 05 -> 04 on. Invisible while the anim drain emptied its whole queue every frame; with the drain's deadline (the
// virtual beam, src/ct_vbeam.h) the order objects are queued in decides which are served, and that drift became the
// first divergence of every lockstep with walking PCs. Two callers: CtFieldRunnerOne (--sweepops) and the live VM's
// applyEffect / init pass. `*outTgt` = the X index written. Carry always CLEAR. Returns 1 if `script[ip]` is in it.
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
    case 0x1E: case 0x1F: case 0x25: case 0x26:                    // 5570 LDA $7F2001,X / TAX -- then 554D, NOT 554B:
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
// THE SCREEN-EFFECT OPS -- 0xF1 (C0/3D07, the colour-math transition) and 0xFE (C0/3F26, the eight interpolation
// channels C0/21E1's entry 6 runs). One body, TWO callers, as CtVmPokeFamily: CtFieldRunnerOne (--sweepops micro-
// executes it) and the live event VM's step(), which walked over both by their length -- the combat tutorial's yellow
// line (loc 0FE: `FE 30 48 48 ..` + `F1 7F 00`) never had its channels, its COLDATA nor its CGADSUB. `script` + `ip` as
// CtVmPokeFamily. Returns 1 if `script[ip]` is one of them (the IP and the carry in *out).
int CtVmScreenFxOp(uint8_t* r, const uint8_t* script, int ip, CtHRet* out) {
  CtHRet h = { ip, 1, 0 };
  switch (script[ip]) {
    case 0xF1: {
      // C0/3D07 -- STAGE A COLOUR-MATH TRANSITION. $0BDF is the shadow of $2131 (CGADSUB): the
      // only reader is `C0/A515 LDA $0BDF / STA $2131`, next to $0BD7->$212C and $0BD8->$212D.
      // That fixes the operand's meaning -- CGADSUB's top three bits are subtract/half/backdrop
      // and its low five are the layer mask, which is exactly the #E0 / #1F split below. The two
      // literals differ in bit 7 alone: #17 = add, #97 = subtract, same layers.
      //
      // It STAGES only. `$0118 |= 0x18` requests the transition and the mainloop runs it -- and
      // bit 3 of that is what the already-ported op 0xF3 blocks on, so F1/F3 are producer and
      // consumer of the same bit.
      //
      // FIVE arms, and the LENGTH IS NOT CONSTANT: the `INX` at 3D0F that steps onto the second
      // operand is skipped when the first is zero, so that arm consumes TWO bytes and the other
      // four consume three.
      int o1 = script[((ip + 1) & 0xFFFF)];                    // 3D09
      if (o1 == 0) {                                                // 3D0D BEQ ->3D6B
        r[0x0122] = 0xE0;                                           // 3D6D STA $22
        r[0x0121] = 0xE0;                                           // 3D6F STA $21
        r[0x0BE0] = r[0x0BDF];                                      // 3D71/3D74 restore the shadow
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 3D77 INX / SEC -- TWO bytes
      }
      r[0x0122] = (uint8_t)o1;                                      // 3D10 STA $22
      r[0x01D9] = (uint8_t)(o1 & 0xE0);                             // 3D14 STA $D9
      r[0x0121] = (uint8_t)((r[0x0121] & 0x1F) | (o1 & 0xE0));      // 3D1C -- mode from the operand
      int o2 = script[((ip + 2) & 0xFFFF)];                    // 3D1E
      if (o2 == 0) {                                                // 3D22 BEQ ->3D44
        r[0x0121] = r[0x0122];                                      // 3D46 -- FULL byte, not masked
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
      // 3D2F, reached by fallthrough and by the BRA at 3D5D. It recomputes $01D9/$0121 from
      // $0122, which the code above has already set to o1 -- so both stores land on the value
      // they already hold. Kept because the ROM issues them.
      r[0x01D9] = (uint8_t)(r[0x0122] & 0xE0);                      // 3D33 STA $D9
      r[0x0121] = (uint8_t)((r[0x0121] & 0x1F) | (r[0x0122] & 0xE0));   // 3D3B STA $21
      r[0x0118] = (uint8_t)(r[0x0118] | 0x18);                      // 3D3F TSB $18 -- request it
      h.ip = (ip + 3) & 0xFFFF; h.carry = 0; h.ok = 1; break;       // 3D41 INX / CLC -- yields
    }
    case 0xFE: {
      // C0/3F26 -- BUILD EIGHT INTERPOLATION CHANNELS. EIGHTEEN bytes: one duration, then eight
      // (start, target) pairs. For each pair it computes a per-frame delta in 8.8 fixed point
      // through the SNES DIVIDER and seeds the running value, then arms $0139 = 6 so the screen
      // effect dispatcher C0/21E1 picks it up. Single exit, ip+18, CLC.
      int D = script[((ip + 1) & 0xFFFF)];           // 3F28
      r[0x01F4] = (uint8_t)D;                             // 3F2C STA $F4 -- 8-bit, $01F5 untouched
      r[0x11CE8] = (uint8_t)D;                            // 3F2E STA $7F1CE8, the live countdown
      int c7 = (ip + 2) & 0xFFFF;
      r[0x01C7] = (uint8_t)c7; r[0x01C8] = (uint8_t)(c7 >> 8);     // 3F33 STX $C7 (16-bit)
      r[0x01F0] = 0x00;                                   // 3F35 -- ONCE, outside the loop, so the
                                                          // 16-bit word at $01F0 always reads a<<8
      r[0x01F6] = 0x00; r[0x01F7] = 0x00;                 // 3F37 / 3F39
      r[0x01EE] = 0x08;                                   // 3F3B -- the channel count is HARDCODED
      for (int i = 0; i < 8; i++) {
        int a = script[(c7 & 0xFFFF)];               // 3F41
        r[0x01F1] = (uint8_t)a;                           // 3F45 STA $F1
        int b = script[((c7 + 1) & 0xFFFF)];         // 3F48
        c7 = (c7 + 2) & 0xFFFF;
        r[0x01C7] = (uint8_t)c7; r[0x01C8] = (uint8_t)(c7 >> 8);   // 3F4D STX $C7
        int f0 = r[0x01F0] | (r[0x01F1] << 8);            // 3F55 SBC $F0 reads the WORD = a<<8
        int diff = (((b << 8) & 0xFFFF) - f0) & 0xFFFF;   // 3F4F XBA / 3F50 LDA #00 / 3F54 SEC/SBC
        int q;
        if (diff == 0) {                                  // 3F57 BEQ ->3F91
          q = 0;                                          // the divider is NOT touched on this arm
        } else if (b > a) {                               // 3F59 BCS ->3F7B (carry = no borrow)
          q = ctSnesDiv16(diff, D);                       // 3F7B $4204 / 3F82 $4206 / 3F8E $4214
        } else {
          int mag = ((diff ^ 0xFFFF) + 1) & 0xFFFF;       // 3F5B EOR #FFFF / 3F5E INC A
          q = ((ctSnesDiv16(mag, D) ^ 0xFFFF) + 1) & 0xFFFF;       // 3F75 EOR #FFFF / 3F78 INC A
        }
        int xi = r[0x01F6] | (r[0x01F7] << 8);            // 3F91 LDX $F6 (16-bit)
        r[0x11CF9 + xi]     = (uint8_t)q;                 // 3F93 STA $7F1CF9,X -- 16-bit on ALL
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

// OP 0x7A, C0/4D06 -- the jump toward a tile (the arc: $1B80/$1B81, integrated by C0/AAC7). One body, TWO callers, as
// CtVmPokeFamily: CtFieldRunnerOne (--sweepops micro-executes it) and the live event VM's step(), which had no arm for it
// -- the combat tutorial's five imps (loc 0FE, `7A 04 03 02` ..) never jumped into the damage area and obj0 waited on
// them for good. `script` + `ip` as CtVmPokeFamily; `idx` = the object's X. Returns 1 (the IP and the carry in *out).
int CtVmOp7A(uint8_t* r, const uint8_t* rom, const uint8_t* script, int ip, int idx, CtHRet* out) {
  CtHRet h = { ip, 1, 0 };
  switch (script[ip]) {
    case 0x7A: {
      // C0/4D06 -- MOVE TOWARD A TILE WITH A COMPUTED TICKET. Four bytes: target tile X, target
      // tile Y, and a third operand that is both a fallback ticket and a scale factor. Three arms
      // on $1A80,X, the same progress flag ops 0x92/0x9C use:
      //   armed, ticket running -> ip+0, CLC
      //   armed, ticket drained -> clear $1A80 AND $1B80, ip+4, SEC
      //   not armed             -> ARM (below) and park at ip+0, CLC
      //
      // THE ARM IS THE ONLY OPCODE IN THIS FILE THAT USES THE HARDWARE DIVIDER. It aims with
      // C0/ABA2, builds a velocity with C0/AC69, and then divides the DISTANCE by the VELOCITY to
      // get how many ticks the leg needs -- `|delta| << 8` into $4204 and `|velocity|` into
      // $4206, reading the quotient's LOW BYTE back from $4214.
      //
      // ⚠ IT TRIES THE X AXIS FIRST AND FALLS THROUGH TO Y, THEN TO A LITERAL. `4D53 BEQ` (dx==0)
      // and `4D67 BEQ` (velX==0) BOTH jump to the Y attempt at 4DA1, whose own two zero-tests
      // jump on to 4DF4, where the ticket is simply operand 3. So a mover with no X component
      // silently uses Y, and one with neither uses the operand -- there is no divide-by-zero path
      // because each axis is tested before it is used as a divisor.
      //
      // ⚠ AND BOTH AXES ADJUST $1980 (the Y velocity), NOT ONE EACH. 4D97 and 4DEA are the same
      // `ADC $1980,X`, and so is the fallback's at 4E11. That reads like a ROM slip and goes in
      // verbatim: what the rest of the engine consumes is what these three arms actually write.
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
      // The multiplier and the divider SHARE $4216: after `STA $4202 / STA $4203` it holds the
      // 16-bit product, not the division remainder. `LSR A / EOR #FFFF / INC A` negates the
      // halved product, and the 16-bit ADC folds it into $1980/$1981.
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

// THE BODIES THE LIVE VM DID NOT HAVE (2026-10-08). Each family below was MOVED VERBATIM out of CtFieldRunnerOne's
// switch -- only the operand reads go through `script`, as in CtVmLocalIncFamily -- because the live event VM (ct_vm.c
// step() and its init pass runFunc) had no arm for them, or a paraphrase, and stepped over the missing ones by their
// length with no effect (the class CT_VM_UNHANDLED cannot see; disasm/notes/live_vm_op_gaps.md has the census). Both
// runners now call the one body --sweepops micro-executes against the ROM. All share one signature: `script` = the
// $7F:2001 image the IP indexes, `idx` = the object's X; returns 1 if `script[ip]` is in the family (IP + carry in *out).
//
// 0x0A/0x0B/0x0C (C0/6254 / 6269 / 6282) -- an object's kill and its cadence pause.
int CtVmObjEnableFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x0A: {
        // C0/6254 -- DESPAWN. Like op 0x02 it writes ANOTHER object's state, and that is how it
        // surfaced: probing why idx 16's anim timer stopped at f254 gave `$1116 04->80 @ C0/625F`
        // with `$6D = 02` -- the player's script disabling object $16 -- immediately followed by
        // `$0F16 80->00 @ C0/A915`, C0/A88D deactivating it on the state bit7 reject, after which
        // C0/C98A is never reached and the timer holds.
        //   6254 INY / TYX / TDC / XBA        (the XBA idiom zeroes C's high byte for the 16-bit TAX)
        //   6258 LDA $7F2001,X                operand 1 = the TARGET object index
        //   625D LDA #80 / 625F STA $1100,X   state := $80, i.e. bit7 = inactive
        //   6262 LDA #00 / 6264 STA $1A81,X   visibility := 0, the same byte op 0x91 clears
        //   6267 BRA ->6247 = TYX / INX / SEC / RTS
        // Two bytes, IP+2, carry SET. Both stores are plain assignments, not OR-in bit sets.
        if (getenv("PORTRUN_CLAIMS")) printf("  OP0A  self=%02X tgt=%02X\n", idx, script[ip + 1]);
        int tgt = script[ip + 1];       // 6258
        r[0x1100 + tgt] = 0x80;                           // 625F
        r[0x1A81 + tgt] = 0x00;                           // 6264
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x0B: {
        // C0/6269 -- DISABLE AN OBJECT: set bit 7 of $1000,X for the target named by the operand.
        //   6269 INY / TYX / TDC / XBA / LDA $7F2001,X / TAX
        //   6272 LDA $1000,X / ORA #80 / STA $1000,X
        //   627A CPX $6D / 627C BNE ->6247    -> TYX / INX / SEC / RTS   (ip+2, continue)
        //   627E TYX / INX / CLC / RTS                                   (ip+2, YIELD)
        // ⚠ THE CARRY DEPENDS ON WHETHER THE TARGET IS SELF. Disabling any other object continues;
        // disabling YOURSELF yields the visit, which is the only sane thing to do having just
        // switched off the object whose script is running. Its sibling 0x0C has no such test.
        int tgt = script[ip + 1];        // 626D
        r[0x1000 + tgt] = (uint8_t)(r[0x1000 + tgt] | 0x80);            // 6272-6277
        h.ip = (ip + 2) & 0xFFFF; h.carry = (tgt != idx);  // 627A CPX $6D
        h.ok = 1; break;
      }
      case 0x0C: {
        // C0/6282 -- the INVERSE of 0x0B, clearing bit 7, and NOT symmetric with it: it has no
        // `CPX $6D` at all and branches straight to the shared `62B1 TYX / INX / SEC / RTS`, so it
        // is SEC unconditionally. Re-enabling yourself is not a state you can be in to observe.
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
      case 0x89:                                          // C0/4B49 -- SET MOVE SPEED. One write.
        // `4B51 STA $1A00,X`. Note it does NOT touch $1A01, the mover ticket sharing the slot.
        r[0x1A00 + idx] = script[ip + 1];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x8A: {
        // C0/4B58 -- op 0x89's INDIRECT twin. Same `STA $1A00,X` with X = $6D, but the speed comes
        // from the scene-local array at $7F0200 (stride TWO) rather than straight from the script.
        int v = script[ip + 1];           // 4B5A
        r[0x1A00 + idx] = r[0x10200 + ((v * 2) & 0xFFFF)];  // 4B63 ASL A / 4B67 / 4B6D
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 4B70 TYX / INX / SEC
      }
      default: return 0;
  }
  *out = h;
  return 1;
}

// 0x19/0x1C/0x1A (C0/653B / 654E / 6568) -- the op-result word $7F0A80,X and the CASE on it.
int CtVmResultFamily(uint8_t* r, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 1;
  (void)rom; (void)romLen; (void)idx;
  switch (op) {
      case 0x19: case 0x1C: {
        // C0/653B and C0/654E -- load a value into this object's RESULT word $7F0A80,X, joining at
        // the shared tail C0/655C `LDX $6D / STA $7F0A80,X / SEP #20 / TYX / INX / SEC`.
        //   0x19: operand = a LOCAL index; 6548 `LDA $7F0200,X` after `ASL A` -- stride two, and
        //         the load is SIXTEEN-BIT (REP #20 at 6541 is still in force), so it takes the
        //         whole word, unlike op 0x4F's 8-bit store into the same array.
        //   0x1C: operand is a SIXTEEN-BIT index read at 6552 and used RAW (no ASL) against
        //         $7F0000, the global page -- a different array, a different stride, and one more
        //         INY (6556), which is the whole of why it is three bytes to 0x19's two.
        // The store at 655E is 16-bit either way, so both write $7F0A80+idx AND +idx+1.
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
        // C0/6568 -- BRANCH ON THE RESULT WORD. Three bytes, SEC on BOTH exits, and the polarity is
        // the interesting part: `6570 CMP $7F2001,X / 6574 BNE ->657A` takes the DISPLACEMENT when
        // the result does NOT match, and falls through to ip+3 when it DOES. So this is
        // "continue if equal, else jump" -- the opposite of the `if (x == n) goto` it reads like.
        // The compare is 8-BIT ($7F0A80,X loaded at 656A with M still 8-bit).
        // The branch base is ip+2 -- the displacement's OWN address -- exactly as ops 0x10/0x11.
        int got = r[0x10A80 + idx];                        // 656A
        if (got == script[ip + 1]) {     // 6570 -- equal falls through
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 6576 INX / INX / SEC
        }
        int d = script[ip + 2];          // 657B
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;          // 6584 STA $D9 -- SIXTEEN-bit, so $01DA
        h.ip = (ip + 2 + d) & 0xFFFF;                      // 6586-658A TXA / CLC / ADC $D9 / TAX
        h.carry = 1; h.ok = 1; break;                      //   is ZEROED, not left alone
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
        // C0/65F8 and C0/661F -- read an object's FACING into a scene variable. Same split as
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
        // C0/662C -- SKIP IF THE OBJECT IS INACTIVE. The active arm writes nothing at all.
        int o = script[ip + 1];             // 6630
        if (r[0x0F00 + o] != 0) {                             // 6635 / 6638 BEQ
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 663A TYX / INX / INX
        }
        // ⚠ THE BRANCH BASE IS ip+2 -- the displacement byte's OWN offset -- because 663F/6640 is
        // `TYX / INX` with Y = ip+1. Op 0x2D's equivalent has no INX and so bases at ip+1.
        int d = script[ip + 2];             // 6641
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;             // 664A STA $D9 -- SIXTEEN bit
        h.ip = (ip + 2 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x28: {
        // C0/6655 -- SKIP UNLESS THE OBJECT IS INSIDE A BOX AROUND THE CAMERA. The two camera
        // halves are read EIGHT-bit and shifted right once, so only the low byte of each 16-bit
        // camera word matters, and the box is expressed in half-tiles.
        int o = script[ip + 1];             // 6659
        r[0x01DB] = (uint8_t)(r[0x1D0A] >> 1);                // 665E / 6661 LSR (8-bit) / 6662
        r[0x01DD] = (uint8_t)(r[0x1D0E] >> 1);                // 6664 / 6667 / 6668
        int dx = (r[0x1801 + o] - r[0x01DB]) & 0xFF;          // 666A / 666D SEC / 666E SBC
        int dy = (r[0x1881 + o] - r[0x01DD]) & 0xFF;          // 667A / 667D SEC / 667E SBC
        // Six separate branches, all landing on 668F: dx==0, dx==1, dx>=$0E, dy==0, dy==1, dy>=$0D.
        // So the box is dx in [2,$0D] and dy in [2,$0C] -- ASYMMETRIC by one on the Y axis.
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
        // C0/66B2..6790 -- THE BUTTON-BRANCH FAMILY. Seventeen dispatch slots, two shared tails and
        // a single question each: is one bit of one byte set?
        //   bit CLEAR -> 66D0: read the displacement at ip+1 and jump to ip+1+disp (SEC)
        //   bit SET   -> 6701 / 6784 (identical `INY / TYX / SEC / RTS`): fall through to ip+2
        //
        // ⚠ THE TWO HALVES OF THE FAMILY DO NOT ADDRESS THEIR SOURCE THE SAME WAY, and reading one
        // and generalising gets the other wrong by $100. Ops 0x30-0x39 use `AD F8 00 LDA $00F8`, an
        // ABSOLUTE load through DB = $00, so they read $7E:00F2 / $7E:00F8 -- the live pad. Ops
        // 0x3B-0x44 use `A5 51 LDA $51`, DIRECT PAGE with DP = $0100, so they read $7E:0150 / $0151
        // -- the latched EDGE. Same shape, different memory.
        //
        // ⚠ AND ONLY THE LATCHED HALF CONSUMES ITS BIT: 0x3B-0x44 do `TRB` on the taken path, which
        // is why they load with `AND` (leaving the mask in A for the TRB) where the live half uses
        // `BIT` (which does not disturb A, and needs no mask afterwards).
        //
        // 0x47 is in the range but is not a branch at all: C0/6788 stores the operand to $016B and
        // falls into the 6784 tail, so it is a two-byte assignment that happens to live here.
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

// ---- THE CAPTION PLANE'S CONSUMERS (disasm/notes/field_caption_plane.md) ----------------------------------------------
// The VRAM a host keeps, if it keeps one: a DMA the ROM fires into VRAM (word address, source bytes, byte count). NULL in
// the hosts that have none (the sweeps, --vm): the WRAM side of every routine below is the same either way.
CtVramSinkFn g_ctVramSink = NULL;
static void vmVramDma(int word, const uint8_t* src, int bytes) { if (g_ctVramSink) g_ctVramSink(word, src, bytes); }

// C0/75A0 -- the caption buffer's fill: seven MVNs in a doubling ladder, 0x20 bytes from $D2:0000 into $7E:C800, then
// each block copied onto its own tail (0x20, 0x40, 0x80, 0x100, 0x200, 0x400) until $C800-$CFFF are covered. $D2:0000-001F
// is thirty-two zero bytes, so it is a 2048-byte zero fill -- written as the source it really is, not as a memset.
static void vmFill75A0(uint8_t* r, const uint8_t* rom, int romLen) {
  for (int q = 0; q < 0x800; q++)
    r[0xC800 + q] = (rom && 0x120000 + (q & 0x1F) < romLen) ? rom[0x120000 + (q & 0x1F)] : 0;   // a dat-only live run has no ROM
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

// C0/274D, the main loop's C0/00B4 leg: the caption requests ops 0x2A/0x2B/0x32 left in $0154 (bits 2/3/4).
//   274D LDA $54 / BIT #1C / BNE 277E          (none of them: bit 5 -> C0/2758, below)
//   277E BIT #10 / BNE 278F                    a line first; the clears wait for a frame without one
//   2782 AND #0C / ASL / TSB $46               bit 2 -> $46 bit 3 (VRAM $7000), bit 3 -> bit 4 (VRAM $7400)
//   2787 JSR C0/75A0 / LDA #0C / TRB $54
//   278F the line: `MVN $7E,$D2` twice from $D2:0000 (0x20 bytes each, Y running on) -> $7E:D800-D83F zeroed; WMADD = $7E:D800
//        (27AA-27B2, the WRAM port's address registers -- hardware, not WRAM); then from $FD:A000 + $0BE1, one byte a cell:
//        bit 7 -> 27E0 (the record's end), '/' -> 27EB (the line's end), else ' ' -> 00, '.' -> 1B, any other -> byte - $40,
//        `STA $2180` the tile and `LDA #2C / STA $2180` its attribute (palette 3, priority 1).
//   27E0 LDA #10 / TRB $54 / JSR 27EB / INC $0BE4      the record's end: the request is done, and one row more down
// '/' leaves bit 4 set, so the next line is written on the next frame -- one line a frame. MEASURED, the rule's cartridge
// (scene 05B, build/_caption/s5b_tp.state, --monitor 0154,0146,0BE1 --writer): $0154 = 10 at f123 (C0:3EDA), $0BE1 09 / 0A
// / 18 on f124-126 for record 0 `PRODUCER//KAZUHIKO AOKI`, $46 = 20 (C0:2823), $0154 = 00 (C0:27E4) on f126.
// With none of 1C, bit 5 (a chest's box: C0/1F1C `LDA #20 / TSB $54`, the probe's only setter) takes C0/2758, M8:
//   2758 LDA $1D0A / LSR / CMP $5D / BCS 2779      the camera's left column >= the chest's    -> close
//   2760 LDA $1D0C / LSR / CMP $5D / BCC 2779      its right column < the chest's             -> close
//   2768 LDA $1D0E / LSR / CMP $5E / BCS 2779      top row >= the chest's                     -> close
//   2770 LDA $1D10 / LSR / CMP $5E / BCC 2779 / RTS   bottom row < the chest's                -> close
//   2779 LDA #0D / STA $29 / RTS                   the box machine's CLOSE state (C0/2032 from the next frame)
// $5D/$5E = the chest's tile (C0/1E52 `LDX $DB / STX $5D`, the probe's only writer). MEASURED, the rule's cartridge
// (saves/field_re/s49_chest_walk.state: loc 031, chest (12,37), A at f10, Down f34, Right f134, Down f534): $015D = 0C25
// (C0:1E54) at f11, $0129 = 07 from f51 while the lead walks; at f280 the camera's top `$1D0E` = 4A (row 25 = the
// chest's) and `$0129` = 0D (C0:277D) the same frame, rewritten every frame of the ramp ($0127 24..00 over f281-290);
// $0129 = 00 (C0:204F) and $0154 = 00 (C0:2055) at f291.
// Returns 1 when it ran an arm (0: $0154 had none of 1C and no bit 5).
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
    r[0xD800 + k] = (rom && 0x120000 + (k & 0x1F) < romLen) ? rom[0x120000 + (k & 0x1F)] : 0;   // second at Y = $D820
  int x = r[0x0BE1] | (r[0x0BE2] << 8), w = 0xD800;                  // 27AA-27B5 WMADD = $7E:D800, LDX $0BE1
  for (;;) {
    long at = 0x3DA000L + x;                                         // 27B8 LDA $FDA000,X (long,X: no bank wrap)
    uint8_t c = (rom && at < romLen) ? rom[at] : 0x80;               // (no ROM: no table -- the record ends here)
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

// The field NMI's arms for the caption plane, C0/EAD1-EB77: after the four `CMP #01..#04` (a style tilemap, ct_vm.c's),
//   EB46 BIT #08 / BEQ / JSR C0/7F5D (VRAM $7000 <- $7E:C800, $0800 bytes: C0/7F62) / LDA #08 / TRB $46 / ... BRL EC15
//   EB5A BIT #10 / BEQ / JSR C0/7F58 (VRAM $7400 <- the same)                       / LDA #10 / TRB $46 / ... BRL EC15
//   EB6E BIT #20 / BEQ / JSR C0/EC77 (VRAM $0BE5 <- $7E:D800, $0BE7 bytes, VMAIN $80) / LDA #20 / TRB $46, into EB79
// ONE arm an NMI: $46 = 18 takes two (MEASURED, scene 05C: $46 = 18 at f2344, 10 at f2345 (C0:EB51), 00 at f2346 (C0:EB65)).
// The 08/10 arms skip C0/EB79-EC13 (no CGRAM, OAM, text line, VRAM queue: ct_vm.c CtFieldNmiTilemapArm); 20 falls into it.
// Returns the arm (8, 0x10, 0x20) or 0.
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
        // C0/3E84 -- ARM THE BG3 CAPTION/CREDITS TEXT PLANE, then point $0BE1 at record #N of the
        // ASCII string table at $FD:A000. 68 static sites. Length 2, carry SET, ONE exit.
        //
        // It is the last sizeable opcode whose only obstacle was in-bank code nobody had derived:
        // `3E93 JSR C0/6E27` and `3E96 JSR C0/75A0`. Neither is blocked --
        //   C0/6E27 decompresses the caption font and queues its DMA. Its ONE out-of-bank transfer
        //     is `6E3D JSL $C3:0002`, and $C3:0002 is `4C 57 05` = JMP C3/0557, the shared "Evil
        //     Peer" LZ THIS PORT ALREADY HAS as ct_lz_decompress. Nothing else leaves bank $C0.
        //   C0/2DC8 (called from 6E58) programs VMADDL/VMAIN and DMA channel 7 and fires it. It
        //     writes NO WRAM AT ALL, so there is nothing here for a WRAM sweep to grade.
        //   C0/75A0 zero-fills the staging buffer through an MVN doubling ladder.
        //
        // ⚠ THE $FD:A000 ACCESS IS A DATA READ, NOT A CODE TRANSFER: `3EC1 BF 00 A0 FD` is
        // `LDA long,X`. Long indexed addressing does NOT wrap at the bank boundary, and the flat
        // file offset 0x3DA000 + X carries into bank $FE by itself, so no masking is wanted.
        //
        // DB is $00 throughout, so $1D35/$1D36/$0BE1/$0BE9 are the bank-$00 mirror of $7E.
        r[0x1D36] = (uint8_t)(r[0x1D36] | 0x04);         // 3E87 TSB -- test-and-SET, NOT an assign
        r[0x1D35] = (uint8_t)(r[0x1D35] | 0x04);         // 3E8A TSB
        r[0x1D39] = 0x00;                                // 3E8D STZ, 8-bit: it does NOT touch $1D3A
        r[0x1D3A] = 0x00;                                // 3E90 STZ -- a separate instruction
        // ---- 3E93 JSR C0/6E27: the caption font, $FD:0000 -> $7F:5080, then DMA to VRAM $5000 ----
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
        // ⚠ 6E27 IGNORES the size report. Unlike the object loader at C0/466B it never advances
        // $71, and the DMA length below is the hardcoded $0400 rather than $0306.
        r[0x0148] = 0x00; r[0x0149] = 0x50;              // 6E44 STX $48 = $5000 (VRAM WORD address)
        r[0x014B] = 0x80; r[0x014C] = 0x50;              // 6E49 STX $4B = $5080 (A-bus address)
        r[0x014D] = 0x7F;                                // 6E4D STA $4D = A-bus bank
        r[0x014A] = 0x01;                                // 6E51 STA $4A = DMAP7
        r[0x014E] = 0x00; r[0x014F] = 0x04;              // 6E56 STX $4E = $0400 bytes
        // 6E58 JSR C0/2DC8 -- PPU and DMA registers only, no WRAM write: the $0400 bytes go to VRAM $5000 at once (the
        // BG3 characters, tiles 0-63 -- where C0/6DCF put the location's BG3 tileset), to the host's VRAM if it keeps one.
        vmVramDma(0x5000, r + 0x15080, 0x400);
        // ---- 3E96 JSR C0/75A0: clear the BG3 staging buffer $7E:C800-$7E:CFFF ----
        vmFill75A0(r, rom, romLen);
        // ---- 3E9C: the operand's bit 7 picks the MODE, its low seven bits pick the RECORD ----
        int b29 = script[ip + 1];      // 3E9C LDA $7F2001,X
        r[0x0BE9] = (uint8_t)((b29 & 0x80) ? 0x02 : 0x01);   // 3EA6 / 3EB1 -- the ONLY difference
        r[0x01D9] = (uint8_t)(b29 & 0x7F);               // 3EA2 / 3EAD: the clear arm stores the raw
        r[0x01C7] = (uint8_t)(ip + 1);                   //   byte, which already has bit 7 clear, so
        r[0x01C8] = (uint8_t)((ip + 1) >> 8);            //   BOTH arms leave b & $7F. 3EB4 (16-bit)
        // ---- 3EB6: walk records until the counter reaches $D9. A record ends at the first byte
        // with bit 7 set, and the INX at 3ECA steps PAST that terminator, so X lands on the START
        // of the next record. Operand 0 exits before the first compare and leaves X = 0.
        int x29 = 0;                                     // 3EB6 LDX #0000
        r[0x01DD] = 0x00;                                // 3EB9 STZ $DD
        while ((r[0x01D9] & 0xFF) != (r[0x01DD] & 0xFF) && 0x3DA000 + x29 < romLen) {   // 3EBB/3EBD/3EBF CMP / BEQ (no ROM: no table)
          int by29 = (0x3DA000 + x29 < romLen) ? rom[0x3DA000 + x29] : 0;   // 3EC1
          x29 = (x29 + 1) & 0xFFFF;                                  // 3EC7 or 3ECA -- both INX
          if (by29 & 0x80) r[0x01DD] = (uint8_t)(r[0x01DD] + 1);     // 3ECB INC $DD, 8-bit
        }
        r[0x0BE1] = (uint8_t)x29; r[0x0BE2] = (uint8_t)(x29 >> 8);   // 3ECF STX $0BE1 (16-bit)
        // The loop's only exit is a taken BEQ, and CMP leaves carry SET on equality; nothing after
        // it touches carry, and the routine contains no CLC/SEC. So the carry is SET on every path,
        // including operand 0 where the body never runs.
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 3ED2 LDX $C7 / 3ED4 INX
      }
      case 0x2A: case 0x2B: case 0x32:
        // C0/3ED6 / 3EDE / 3EE2 -- SET A BIT IN $0154. Three entries into one four-instruction tail:
        //   3ED6 A9 04        (0x2A, falls straight into 3ED8)
        //   3EDE A9 08 80 F6  (0x2B)      3EE2 A9 10 80 F2  (0x32)
        //   3ED8 TSB $54 / TYX / INX / CLC / RTS
        // TSB is test-and-SET, so this ORs rather than assigns -- the three bits are independent and
        // a later one must not wipe an earlier one. Y never moves, so `TYX / INX` makes them ONE
        // byte, and the tail exits CLC, yielding the visit.
        // $0154 is the same latch op 0xDA tests bit 0 of and TRBs on its completion arm.
        r[0x0154] = (uint8_t)(r[0x0154] | (op == 0x2A ? 0x04 : op == 0x2B ? 0x08 : 0x10));   // 3ED8
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;        // 3EDA TYX / INX / CLC
      case 0x2C: {
        // C0/3EE6 -- clear three scroll/window bytes, then park two operands SHIFTED LEFT BY THREE.
        // The three STZs run with A still 8-bit, so they clear ONE byte each; the two `STA $1D8F` /
        // `STA $1D91` that follow are 16-bit (REP #20 at 3EF7).
        // ⚠ $1D92 IS WRITTEN TWICE -- once by the STZ at 3EEC and again as the HIGH half of the
        // 16-bit store at 3F0A. The second write wins, so a port that keeps only the STZ is wrong
        // whenever operand 2 exceeds $1F.
        r[0x1D3F] = 0x00;                                   // 3EE6 STZ (8-bit)
        r[0x1D40] = 0x00;                                   // 3EE9
        r[0x1D92] = 0x00;                                   // 3EEC
        int v1 = (script[ip + 1] * 8) & 0xFFFF;    // 3EF3 / 3EF9 ASL x3
        r[0x1D8F] = (uint8_t)v1; r[0x1D90] = (uint8_t)(v1 >> 8);     // 3EFC STA $1D8F (16-bit)
        // 3F00's LDA is SIXTEEN-bit and so reads two script bytes, but 3F04's `AND #00FF` throws
        // the upper one away -- the operand is one byte.
        int v2 = (script[ip + 2] * 8) & 0xFFFF;    // 3F00 / 3F07 ASL x3
        r[0x1D91] = (uint8_t)v2; r[0x1D92] = (uint8_t)(v2 >> 8);     // 3F0A STA $1D91 (16-bit)
        h.ip = (ip + 3) & 0xFFFF; h.carry = 0; h.ok = 1; break;      // 3F0F INX / CLC
      }
      case 0x2F:
        // C0/3F12 -- two operand bytes into $0BE3/$0BE4. Branchless, ip+3, SEC. The neighbours of
        // $0BDF/$0BE0, which op 0xF1 showed to be the $2131 CGADSUB shadow pair.
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
        // C0/3711 -- ASSIGN THIS OBJECT'S PALETTE SLOT FROM THE OPERAND, THEN UPLOAD IT.
        // Straight-line, no calls, no branches: 3711..3756, one exit, SEC.
        //
        // It opens with the SAME `LDA #80 / TSB $54` as ops 0x2A/0x2B/0x32 -- a fourth independent
        // bit in the one latch -- and then does real work, which is why it is not part of their
        // shared tail.
        //
        // The upload is byte-for-byte the tail of op 0x88's RESTORE arm (48DE-490B): the same
        // $2102 + pal*16 destination, the same 16-bit ROM offset out of $1400,X, the same source
        // bank $E4, the same 24 bytes written TWICE -- once at dest and once at dest + $200.
        // The only difference is where `pal` comes from: 0x88 READS $0F81,X (whatever was already
        // assigned), 0x33 COMPUTES it from the operand and WRITES it there.
        //
        // ⚠ THE OPERAND IS NOT THE SLOT. `371E ASL A / 371F AND #0E` doubles it and then keeps
        // three bits, so pal = (operand & 7) * 2 -- only EVEN slots 0..14 are reachable and bit 3
        // of the doubled value is discarded. Grading this needs an operand >= 8 (to catch a missing
        // AND) and an odd one (to catch a missing ASL); both masked forms agree on 0..7 even.
        //
        // ⚠ `371C STX $C7` spills ip+2 with X SIXTEEN BITS -- $01C7 AND $01C8 -- and 3753 reloads
        // from it, so the length is fixed at 2 on the one exit.
        //
        // 3721 `LDX $6D` happens BEFORE `3726 REP #30`, but X is already 16-bit on entry from the
        // runner, so idx is a full 16-bit index for both $0F81,X and the later $1400,X.
        r[0x0154] = (uint8_t)(r[0x0154] | 0x80);                       // 3711 LDA #80 / TSB $54
        r[0x01C7] = (uint8_t)(ip + 2); r[0x01C8] = (uint8_t)((ip + 2) >> 8);   // 371C (16-bit)
        int pal33 = (script[ip + 1] << 1) & 0x0E;    // 371E ASL / 371F AND #0E
        r[0x0F81 + idx] = (uint8_t)pal33;                              // 3723 STA $0F81,X
        int dst33 = (0x2102 + pal33 * 16) & 0xFFFF;                    // 372B-3735 (*16, +2, +$2100)
        int src33 = r[0x1400 + idx] | (r[0x1401 + idx] << 8);          // 3738 LDA $1400,X (16-bit)
        for (int k = 0; k < 0x18; k++) {                               // 373D LDA #0017 -> 24 bytes
          int s = 0x240000 + ((src33 + k) & 0xFFFF);
          r[(dst33 + k) & 0xFFFF] = (s < romLen) ? rom[s] : 0;         // 3740 MVN $7E,$E4
        }
        for (int k = 0; k < 0x18; k++) {                               // 3743 PLX/PLA -- the SAME
          int s = 0x240000 + ((src33 + k) & 0xFFFF);                   //   source again, dest + $200
          r[(dst33 + 0x200 + k) & 0xFFFF] = (s < romLen) ? rom[s] : 0; // 374D MVN $7E,$E4
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
        // C0/6D09 -- STEP THE $01F8 CURSOR AND READ THE $C0:FE00 TABLE INTO A SCENE VARIABLE.
        // The increment happens BEFORE the lookup and is stored back, so this opcode is stateful:
        // calling it twice reads two different table entries. Same $F8 cursor and same 256-byte
        // table C0/ACFD's bounce uses.
        int t = (r[0x01F8] + 1) & 0xFF;                     // 6D0B LDA $F8 / 6D0D INC A (8-bit)
        r[0x01F8] = (uint8_t)t;                             // 6D0E STA $F8
        // DB = $00 and bank $00:$FE00 mirrors $C0:$FE00 = file offset $FE00, the same convention
        // every other rom[] read in this file uses.
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
        // C0/3DF4 -- write ONE 16-bit value into up to THREE scroll registers, selected by a mask.
        // FIVE bytes: a 16-bit value at ip+1, the mask at ip+3, and a byte at ip+4 that always
        // lands in $1D4D. Mask bits 0/1/2 gate $1D24, $1D26 and $1D28, each written with a 16-bit
        // `LDY $DB / STY` -- so each writes TWO bytes, not one, and all three take the SAME value.
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
        // C0/3D84 -- set $01BC, and ZERO IS SPECIAL. `STA $BC / BNE ->3D94` skips the next two
        // stores when the operand is non-zero, so $1D4E/$1D4F are cleared only by an operand of 0
        // -- and they are cleared with A, which is 0 on exactly that path. Two bytes, ip+2, SEC.
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

// See move.h. Kept in the order --sweepops grew it: its per-opcode report line follows this order.
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
  // 5A9C `STX $6D` with DP = $0100 writes $016D, NOT $006D. This latch had been going to $006D
  // since the runner was written; nothing in the port READ it (every handler takes idx as a
  // parameter), so it was dead AND wrong, and no scenario check could see it. --sweepops caught it
  // on its first run -- 600/600 mismatches, all on $0006D -- because that sweep diffs the WHOLE
  // 128 KB image instead of a hand-listed write set. X is still 8-BIT here (5A9E's REP #10 comes
  // after), so this is a one-byte store and $016E is left alone.
  r[0x016D] = (uint8_t)idx;
    int op = r[0x12001 + ip];                             // fetch opcode from $7F2001 + IP
    CtHRet h;
    switch (op) {
      case 0x00:                                          // C0/5F74 -- RETURN (op 0x02's partner)
        h.ip = ctVmOp00(r, idx, ip, &h.carry); h.ok = 1; break;
      case 0x10: {
        // C0/62B5 -- JUMP FORWARD. Two bytes on paper, but the IP does not ADVANCE, it JUMPS:
        // 62C2 `TXA` takes X = ip+1 (the operand's own address) and 62C4 `ADC $D9` adds the operand,
        // so the target is ip + 1 + operand -- NOT ip + 2 + operand. Getting that base wrong is an
        // off-by-one on every branch in every script.
        //   62B5 C8 INY / 62B6 BB TYX / 62B7 BF 01 20 7F LDA $7F2001,X
        //   62BB C2 20 REP #20 / 62BD 29 FF 00 AND #00FF / 62C0 85 D9 STA $D9
        //   62C2 8A TXA / 62C3 18 CLC / 62C4 65 D9 ADC $D9
        //   62C6 AA TAX / 62C7 E2 20 SEP #20 / 62C9 38 SEC / 62CA 60 RTS
        // This is the single most-used opcode the byte-exact runner did not have: 3720 static sites
        // across the 504 parseable scenes, 3.3% of all of them.
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];
        // 62C0 `STA $D9` is a SIXTEEN-BIT store (62BB REP #20), so it lands on $01D9 AND $01DA, and
        // 62BD `AND #00FF` has already cleared the high byte.
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;
        h.ip = (ip + 1 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x11: {
        // C0/62CB -- JUMP BACKWARD. The same routine with SBC where 0x10 has ADC, and it does not
        // even duplicate the tail: `62DC BRA ->62C6` lands in 0x10's own TAX/SEP/SEC/RTS.
        //   62D8 8A TXA / 62D9 38 SEC / 62DA E5 D9 SBC $D9   -> target = ip + 1 - operand
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;    // 62D6, 16-bit as above
        h.ip = (ip + 1 - d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x82: {
        // C0/4590 -- LOAD AN NPC. The handler is two instructions:
        //   4590 A9 04    LDA #04
        //   4592 20 57 45 JSR C0/4557
        // and everything after it is the object loader already ported as CtObjLoad -- the $E4:F023
        // descriptor walk, the C0/5C90 per-scene graphics dedup, the JSL C3/0002 decompression and
        // the $71 heap cursor -- graded 720/720 by --sweeploader. #04 is the $1100,X actor state
        // C0/4557 stamps at 4559, which is CtObjLoad's `state` argument.
        //
        // TWO BYTES, and the returned IP is NOT Y. 459A `STY $C7` spills it after C0/4557's two
        // INYs (455C and 458D), and BOTH exits restore that spill -- 4667 `LDX $C7` and 46DB
        // `LDX $C7` -- so X on return is the SPILLED value, ip+2. It happens to equal what
        // CtObjLoad returns, but that is a measured fact, not an assumption: --sweeploader has
        // been comparing them directly (`myIp != r[$01C7] | r[$01C8] << 8`) for all 720 cases.
        //
        // CARRY IS ALWAYS SET -- 4669 and 46DD are both `SEC / RTS` -- so this opcode never yields
        // and never costs a budget slot beyond its own DEC. The only paths that do not reach an
        // RTS are the two diagnostic halts (`LDX #imm / BRL C0/2E1E`), which do not return at all.
        //
        // ⚠ ITS SIBLING 0x83 IS NOT THIS ROUTINE WITH state = 5. Slot $C0:5E74 points at C0/46DF,
        // which is a SEPARATE 285-byte handler: stride $0A instead of $05 (46E4 `LDA #0A` vs 4595
        // `LDA #05`), descriptor base $E4:F600 instead of $E4:F023, a THREE-byte encoding (4799
        // re-reads the script at the spilled IP and 47FB `INX` returns ip+3), an inline OBJ-slot
        // allocation into $0B88/$0D00/$1700, a `CMP #F8` early-out at 477A, and a `JSR C0/E12A`
        // VRAM upload. Routing it here on the symmetry of the two dispatch slots would be a guess.
        h.ip = CtObjLoad(r, rom, romLen, idx, ip, 4) & 0xFFFF;
        h.carry = 1; h.ok = 1; break;
      }
      case 0x4A: case 0x4B: case 0x4C: case 0x4D: {
        // THE POKE FAMILY, C0/67D7 / 67FB / 680C / 6829. The body lives in CtVmPokeFamily (beside its
        // prologue C0/67E3) because the LIVE event VM runs it too -- it had no arm at all for 0x4C/0x4D,
        // so the fair's `4C 00 02 7E 1B` never put the Silver Points into the text's number slot $0200.
        // THIS call is what --sweepops micro-executes; the live VM inherits the grade. SEC on every exit.
        int ni = ip;
        CtVmPokeFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x5A:                                          // C0/6A12 -- SET STORYLINE. One long store.
        // `6A18 STA $7F0000` -- absolute LONG, so the address is baked in and DB is irrelevant. The
        // operand lands verbatim: no mask, no clamp, and a 0x00 operand stores like any other.
        r[0x10000] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x87: {
        // C0/4876 -- SET SCHEDULER PERIOD. Branchless, IP+2, SEC. Three writes.
        // `487E INC A` is EIGHT-BIT, so operand $FF wraps to $00; $01D9 gets the value BEFORE the
        // bit-7 merge; and BOTH $1000 (the period) and $1001 (the live countdown) take
        // (old & 0x80) | v -- bit 7 is the object-halted flag and is deliberately carried over, so
        // the countdown is RESET to the new period at the instant the opcode runs.
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
        // C0/4B74 -- PLACE AT AN ABSOLUTE TILE. Three bytes, branchless IP, ALWAYS SEC. The two
        // sub-position literals ARE the engine's placement convention, straight from the ROM:
        // `4B81 LDA #80 / 4B83 STA $1800,X` (X = tile CENTRE) and `4B93 LDA #FF / 4B95 STA $1880,X`
        // (Y = tile FOOT).
        int tx = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4B74 INY / 4B76 LDA $7F2001,X
        r[0x1801 + idx] = (uint8_t)tx;                    // 4B7C
        r[0x01D9] = (uint8_t)tx;                          // 4B7F STA $D9  -- DP = $0100
        r[0x1800 + idx] = 0x80;                           // 4B83
        int ty = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 4B86 INY / 4B88 LDA $7F2001,X
        r[0x1881 + idx] = (uint8_t)ty;                    // 4B8E
        r[0x01DA] = (uint8_t)ty;                          // 4B91 STA $DA
        r[0x1880 + idx] = 0xFF;                           // 4B95
        // 4B98 `LDA $0F80,X` / 4B9B `BPL ->4BBF` -- bit 7 of the per-object flags byte gates the
        // whole priority block. Clear = place and leave $0C00/$0C01 alone.
        if (r[0x0F80 + idx] & 0x80) {
          // ⚠ 4B9D `A6 D9 LDX $D9` is SIXTEEN-BIT: the runner enters every handler with the X flag
          // CLEAR (C0/5A9E REP #10, never undone before the 5AB1 dispatch), so this loads the WORD
          // $01D9|$01DA<<8 = tileY*256 + tileX. Reading it as a byte would silently drop the row.
          // That stride is why the planes at +$00 and +$40 do not overlap: 64 tiles per row on a
          // 256-byte row pitch, the same layout as the player's plane at $7E7080.
          ctVmTileAttr(r, idx, (tx | (ty << 8)) & 0xFFFF);   // 4B9F-4BBD, shared with op 0x8D
        }
        // 4BBF `TYX` with Y = ip+2 (both INYs ran) / 4BC0 INX / 4BC1 SEC.
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x8C: {
        // C0/4BC3 -- op 0x8B's INDIRECT twin. Same three bytes, same single exit, same always-SEC,
        // and a BYTE-IDENTICAL private copy of the tile-attribute block (4C03-4C22 equals op 0x8B's
        // 4B9F-4BBE and op 0x8D's 4C4F-4C6E), so ctVmTileAttr is reused verbatim.
        //
        // Both operands become INDICES into the scene-local array at $7F0200, stride 2, exactly as
        // op 0x8A indexes it for op 0x89. ⚠ THE `ASL A` AT 4BCE IS SIXTEEN-BIT (REP #20 at 4BC9),
        // so an index of $80 reaches offset $100 rather than wrapping to $00 -- the array is read
        // across its full $200 bytes and only the LOW byte of each stride-2 slot is used.
        //
        // ⚠⚠ IT DOES NOT WRITE $1800,X. Op 0x8B has `4B81 LDA #80 / 4B83 STA $1800,X` between its
        // `STA $D9` and its second INY; op 0x8C has nothing there -- the byte at 4BDD is C8 (INY),
        // confirmed against the raw dump. The Y-side literal IS present in both, so the omission is
        // asymmetric even within this handler: $1880,X gets $FF and $1800,X is left untouched.
        // Do NOT restore the symmetry; a port that adds it is wrong on every case.
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
        // C0/4C74 -- SET THE PRIORITY / OAM-GROUP BYTE $0F80,X. Two bytes, two arms, and BOTH arms
        // end `TYX / INX / SEC` (4CB3 and 4CD1), so the IP delta and the carry are the same either
        // way: ip+2, SEC. There is no yield and no park-on-op.
        //
        // THIS IS THE OPCODE THAT WRITES THE BYTE THE REST OF THE ENGINE BRANCHES ON. Op 0x8B above
        // and C0/ACFD both gate their tile-property block on `$0F80,X` bit 7; C0/4C7C is where that
        // bit comes from. The operand lands VERBATIM and UNCONDITIONALLY, before the test.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4C74 INY / 4C75 TYX / 4C76 LDA $7F2001,X
        r[0x0F80 + idx] = (uint8_t)v;                    // 4C7C STA $0F80,X
        if (v & 0x80) {                                  // 4C7F `89 80` BIT #80 (immediate) / 4C81 BEQ ->4CB7
          // 4C83-4C8A builds the cell in the ACCUMULATOR, not with an ASL: `LDA $1881,X` (tile Y)
          // then `EB XBA` moves it into the HIGH byte, and the following `LDA $1801,X` is EIGHT-bit
          // so it fills the LOW byte without disturbing it. `AA TAX` then moves all sixteen bits
          // (the runner left the X flag clear). Result: tileY*256 + tileX -- the same formula op
          // 0x8B reaches by a different route, and with NO mask applied here either.
          int cell = (r[0x1801 + idx] | (r[0x1881 + idx] << 8)) & 0xFFFF;
          // Both reads are `BF` long,X, which crosses the bank boundary; r[] is one flat image.
          int a1 = ((r[0x7000 + cell] & 0x40) >> 2) | 0x20;   // 4C8B/4C8F/4C91/4C92/4C93
          r[0x01D9] = (uint8_t)a1;                            // 4C95 STA $D9 -- DP = $0100
          int a2 = ((r[0x7040 + cell] & 0x40) >> 2) | 0x20;   // 4C97..4C9F
          // ⚠ CROSSED, exactly as in op 0x8B: the $7E7040 plane feeds $0C00 and the $7E7000 plane
          // feeds $0C01. C0/ACFD's own two copies assign these the OTHER way round; the two laws are
          // genuinely different and unifying them would be silently wrong.
          r[0x0C00 + idx] = (uint8_t)a2;                      // 4CA1 LDX $6D / 4CA3 STA $0C00,X
          r[0x0C01 + idx] = (uint8_t)a1;                      // 4CA6 LDA $D9 / 4CA8 STA $0C01,X
        } else {
          // 4CB7: FIXED priority packed into the operand's own nibbles. This is byte-for-byte the
          // law C0/ADD7 uses, already ported as CtBuildVelocityPriority's bit7-clear arm above.
          r[0x01D9] = (uint8_t)v;                        // 4CB7 STA $D9 (A still holds the operand)
          r[0x0C01 + idx] = (uint8_t)(v & 0x30);         // 4CB9 AND #30 / 4CBB STA $0C01,X
          // 4CC0-4CC3 is FOUR `0A` ASL A in 8-bit mode, so bits 0-1 of the operand land in bits 4-5
          // and everything above bit 3 is shifted out entirely.
          r[0x0C00 + idx] = (uint8_t)((v << 4) & 0x30);  // 4CC4 AND #30 / 4CC6 STA $0C00,X
        }
        // Both arms then invalidate the cached anim frame and clear its timer -- 4CAB/4CAD + 4CB0 on
        // the bit7 arm, 4CC9/4CCB + 4CCE on the other. $0F01 = $FF is the same "no frame committed"
        // marker C0/A8F5 writes on the activation rising edge.
        r[0x0F01 + idx] = 0xFF;
        r[0x1601 + idx] = 0x00;
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xAA:
      case 0xAE:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x8D: {
        // C0/4C27 -- PLACE AT AN ABSOLUTE PIXEL POSITION. FIVE bytes, and the two coordinates are
        // SIXTEEN-BIT reads, not byte pairs the port may assemble however it likes:
        //   4C27 INY / TYX / 4C29 REP #20 / 4C2B LDA $7F2001,X   -- 16-bit, from ip+1
        //   4C31 STA $1800,X                                      -- 16-bit: sub-pixel AND tile
        //   4C34 INY / 4C35 INY / 4C37 LDA $7F2001,X              -- 16-bit, from ip+3
        //   4C3D STA $1880,X
        // So the low byte of each store is the sub-tile offset and the HIGH byte is the tile index,
        // which is why op 0x8B -- the tile-granular sibling -- writes $1801/$1881 directly and fills
        // $1800/$1880 with its two placement literals ($80 = tile centre, $FF = tile foot).
        int px = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);
        r[0x1800 + idx] = (uint8_t)px; r[0x1801 + idx] = (uint8_t)(px >> 8);      // 4C31
        int py = r[0x12001 + ((ip + 3) & 0xFFFF)] | (r[0x12001 + ((ip + 4) & 0xFFFF)] << 8);
        r[0x1880 + idx] = (uint8_t)py; r[0x1881 + idx] = (uint8_t)(py >> 8);      // 4C3D
        // 4C42 `LDA $0F80,X` / 4C45 `BPL ->4C6F` -- the same bit-7 gate op 0x8B has. The tile it
        // then looks up is the object's CURRENT one, assembled at 4C47-4C4E by
        // `LDA $1881,X / XBA / LDA $1801,X / TAX` -- i.e. the tile the coordinates just written
        // land on, read back out of the object rather than taken from operands.
        if (r[0x0F80 + idx] & 0x80)
          ctVmTileAttr(r, idx, (r[0x1801 + idx] | (r[0x1881 + idx] << 8)) & 0xFFFF);
        // 4C6F `TYX` with Y = ip+3 (three INYs ran) / INX / INX / SEC.
        h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xB3:
      case 0xB4:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x7A:
        // C0/4D06. The body lives in CtVmOp7A because the LIVE event VM runs it too; THIS call is what --sweepops
        // micro-executes, and the live VM inherits the grade.
        CtVmOp7A(r, rom, r + 0x12001, ip, idx, &h); break;
      case 0xB9: case 0xBA: case 0xBC: case 0xBD: {
        // C0/2FC6, C0/2FCA, C0/2FD2 and C0/2FCE -- WAIT A FIXED NUMBER OF VISITS. FOUR one-byte
        // entries into the shared tail at C0/2FD4, differing only in the immediate they carry:
        //   2FC6 LDA #04 / BRA ->2FD4   (0xB9)     2FCA LDA #08 / BRA ->2FD4   (0xBA)
        //   2FCE LDA #20 / BRA ->2FD4   (0xBD)     2FD2 LDA #10   (0xBC, falls straight in)
        // The tail compares that limit against the SAME counter $7F0A01,X that op 0xAD uses, and
        // its three arms do NOT advance uniformly:
        //   limit == counter -> 2FE7: clear the counter, ip+1, SEC   (done -- the only arm that
        //                             consumes the byte)
        //   limit >  counter -> 2FF1: INC the counter, ip+0, CLC     (keep waiting)
        //   limit <  counter -> 2FDE: clear the counter, ip+0, SEC   (restart, WITHOUT advancing)
        // That last arm is the odd one: it exits SEC with the IP unchanged, so the runner spends a
        // budget slot and immediately re-enters this same opcode with the counter now zeroed.
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
        // C0/2F96 -- WAIT N VISITS, counting in $7F0A01,X. Three arms, and the counter cell is owned
        // solely by this handler and the 0xB9/0xBA/0xBC/0xBD family (ROM-wide scan).
        int n = r[0x12001 + ((ip + 1) & 0xFFFF)];
        int c = r[0x10A01 + idx];                         // 2F9E
        if (n > c) { r[0x10A01 + idx] = (uint8_t)(c + 1); // 2FBF -- cannot wrap: c+1 <= n <= 0xFF
                     h.ip = ip; h.carry = 0; }            // 2FC3 TYX / 2FC4 CLC: park and yield
        else if (n == c) { r[0x10A01 + idx] = 0x00;       // 2FB1
                     h.ip = (ip + 2) & 0xFFFF; h.carry = 1; }   // 2FB5 TYX/INX/INX + 2FB8 SEC
        else { r[0x10A01 + idx] = 0x00;                   // 2FA8 -- a stale counter above N resets
                     h.ip = ip; h.carry = 1; }            // 2FAC TYX / 2FAD SEC: SAME opcode re-runs
                                                          //   this visit, now against counter 0
        h.ok = 1; break;
      }
      case 0xE3:                                          // C0/3C05 -- the CONTROL-LOCK flag $011F.
        // `3C0B STA $1F` with A EIGHT-BIT, so it covers ONE address; had A been 16-bit the next
        // script byte would spill into $0120, the byte C0/8840 tests to gate the player collision.
        // The operand lands VERBATIM -- values 0x02/0x03/0x04 were measured, so a port storing
        // `operand ? 1 : 0` would already be byte-wrong.
        r[0x011F] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x02:                                          // C0/5FB6 -- the PRIORITY CALL
        h.ip = CtVmOp02(r, ip, &h.carry); h.ok = 1; break;
      case 0x04:                                          // C0/6109 -- the BLOCKING priority call
        h.ip = CtVmOp04(r, idx, ip, &h.carry); h.ok = 1; break;
      case 0x80: {
        // C0/421E -- LOAD A PC, PARTY-AWARE. Two bytes, SEC on every path, never yields.
        // Full derivation in disasm/notes/pc_loader_op80.md. NOT to be merged with op 0x81
        // (C0/4476), the always-NPC loader: confusing the two once broke Marle walking 439 -> 7.
        int id = r[0x12001 + ((ip + 1) & 0xFFFF)];           // 4220 LDA $7F2001,X
        r[0x01D9] = (uint8_t)id;                             // 4224 STA $D9 (8-bit)
        // 4226-4268: a SEVEN-WAY dispatch storing THIS object's index into $018D + id -- "which
        // object slot holds PC id". The chain is `CMP #n / BNE next` with NO default arm, so an
        // id > 6 stores nowhere at all and simply falls through to the party test.
        if (id <= 6) r[0x018D + id] = (uint8_t)idx;          // 422A LDA $6D / STA $8D (8-bit)
        ctVmPcLoad(r, rom, romLen, idx, id, (ip + 1) & 0xFFFF, &h);   // 421E INY, then join 426A
        break;
      }
      case 0x57: case 0x5C: case 0x62: case 0x68: case 0x6A: case 0x6C: case 0x6D: {
        // C0/41E4 / 41EC / 41F4 / 41FC / 4204 / 420C / 4214 -- SEVEN one-byte siblings of op 0x80,
        // each with the PC id HARDWIRED instead of taken from an operand. Every one is four
        // instructions:  `LDA $6D / STA $8D+id / LDA #id / BRA ->421A`, and 421A is `STA $D9/
        // BRA ->426A`, i.e. straight into op 0x80's party test. They never execute INY and never
        // read the script, so Y is still the opcode's own IP when C0/448D adds its single INY --
        // which makes all four exits land on ip+1. ONE byte each.
        //
        // ⚠ THE OPCODE -> ID MAPPING IS NOT MONOTONIC IN THE HANDLER ADDRESS, so it has to be a
        // table and cannot be arithmetic on the opcode: C0/41FC (op 0x68) carries id 4 and writes
        // $91, while the LATER C0/4204 (op 0x6A) carries id 3 and writes $90. The two are swapped
        // relative to their addresses, and every other member is in order -- exactly the shape that
        // invites `id = (op - 0x57) / something` and gets two of the seven wrong.
        static const struct { int op, id; } PCIMM[7] = {
          { 0x57, 0 }, { 0x5C, 1 }, { 0x62, 2 }, { 0x6A, 3 },     // 41E4 / 41EC / 41F4 / 4204
          { 0x68, 4 }, { 0x6C, 5 }, { 0x6D, 6 },                  // 41FC / 420C / 4214
        };
        int id = 0;
        for (int k = 0; k < 7; k++) if (PCIMM[k].op == op) { id = PCIMM[k].id; break; }
        r[0x018D + id] = (uint8_t)idx;                     // 41E4/41E6 LDA $6D / STA $8D+id
        r[0x01D9] = (uint8_t)id;                           // 421A STA $D9
        ctVmPcLoad(r, rom, romLen, idx, id, ip, &h);       // 421C BRA ->426A, with Y UNMOVED
        break;
      }
      case 0x0A: case 0x0B: case 0x0C:
        CtVmObjEnableFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0xC1: case 0xC2:
        // C0/35B5 and C0/35FC -- THE SAME ROUTINE AS 0xBB, differing in exactly one instruction, and
        // the ENCODING proves it rather than symmetry asserting it: where 0xBB has `359E 64 30`
        // (STZ $30, two bytes) these have `35E3 A9 01 85 30` and `362A A9 02 85 30` (four bytes), and
        // EVERY branch displacement around them shifts by exactly 2 to match -- `D0 34` -> `D0 36`,
        // `D0 21` -> `D0 23`. All three BRLs still resolve to the same panic handler: 0xBB via
        // 357A+$F8A4, 0xC1 via 35BF+$F85F, 0xC2 via 3606+$F818, all = C0/2E1E.
        // So $0130 is a THREE-VALUED variant selector and these three opcodes are one routine with
        // it hardwired to 0, 1 and 2. Ported as one body with the value passed in -- but the value
        // is mutation-tested, because "they look the same" is what has been wrong twice this session.
        ctVmTextRequest(r, idx, ip, &h, (op == 0xC1) ? 1 : 2);
        break;
      case 0xBB:
        ctVmTextRequest(r, idx, ip, &h, 0);
        break;
      case 0x12: {
        // C0/62DE -- BRANCH ON A SCENE-LOCAL VARIABLE. FIVE bytes: var, comparand, mode, disp.
        //   62E0 operand1 = the local var index; 62E9 `ASL A` doubles it and 62ED
        //   `LDA $7F0200,X` reads the LOCAL array -- so the stride is TWO bytes per variable.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = r[0x10200 + ((v * 2) & 0xFFFF)];      // 62F1 STA $D9 (8-bit)
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 62F5 / 62F9 STA $DB
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 62FC
        // 6301 `STX $C3` with X = ip+4, and X is SIXTEEN-BIT here (the SEP #10 is at 6304, after),
        // so the spill lands on $01C3 AND $01C4.
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);
        // 6303 `ASL A` is 8-bit and 6307 `JSR ($6467,X)` indexes a SIXTEEN-entry table, so a mode of
        // $10 or more dispatches PAST the table into whatever bytes follow. That is real ROM
        // behaviour but it is NOT DERIVED, so trap rather than invent a target.
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }  // 630C-6312
        ctVmBranchTakenAt(r, ip + 4, &h);                       // 630A BCS ->6349
        break;
      }
      case 0x20: case 0x23: case 0x24:
        CtVmObjQueryFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x21: case 0x22: {
        // C0/65AA and C0/65EB -- read an object's TILE PAIR into two scene variables. 0x22 names
        // the object by PARTY SLOT and then BRAs into 0x21's body at 65B2, so everything from the
        // $1801 read onwards is shared and both are FOUR bytes.
        // ⚠ `B5 97 LDA $97,X` INDEXES BY THE RAW SLOT, not slot*2 -- unlike every $7F0200 access in
        // this same family, which is stride 2.
        int o = (op == 0x21) ? r[0x12001 + ((ip + 1) & 0xFFFF)]
                             : ctVmPartySlotTarget(r, r[0x12001 + ((ip + 1) & 0xFFFF)]);   // 65F4
        r[0x01D9] = r[0x1801 + o];                            // 65B3 / 65B6 (8-bit; $01DA untouched)
        r[0x01DB] = r[0x1881 + o];                            // 65B8 / 65BB (8-bit; $01DC untouched)
        int vx = r[0x12001 + ((ip + 2) & 0xFFFF)];            // 65BF
        int vy = r[0x12001 + ((ip + 3) & 0xFFFF)];            // 65D4
        // BOTH bytes are loaded before EITHER is stored, X-variable first -- so if vx == vy the Y
        // byte is the survivor, not the X one.
        r[0x10200 + ((vx * 2) & 0xFFFF)] = r[0x01D9];         // 65C8 ASL (16-bit) / 65CE
        r[0x10200 + ((vy * 2) & 0xFFFF)] = r[0x01DB];         // 65DD ASL (16-bit) / 65E3
        h.ip = (ip + 4) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x27: case 0x28:
        CtVmObjBranchFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x2D: {
        // C0/66A5 -- SKIP IF NOTHING IS HELD. $00F8 is the button latch and $00F9 the direction
        // latch; either one non-zero falls through, and only both-zero branches.
        if (r[0x00F8] != 0 || r[0x00F9] != 0) {               // 66A6/66A9 BNE, 66AB/66AE BEQ
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 6701 INY / TYX
        }
        // ⚠ THE BASE IS ip+1, NOT ip+2. C0/66D0 is a bare `BB TYX` with NO `E8 INX`, where ops 0x27
        // and 0x28 both do `TYX / INX` -- that single missing byte is the whole difference, and it
        // also makes this opcode TWO bytes where they are three.
        int d = r[0x12001 + ((ip + 1) & 0xFFFF)];             // 66D1
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;             // 66DA (16-bit)
        h.ip = (ip + 1 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xC0: case 0xC3: case 0xC4: {
        // C0/3643 / 36AA / 36DD -- THE DECISION-BOX REQUEST+WAIT. Four arms over ONE shared issue
        // tail at C0/3674, which all three enter at its FIRST instruction (no enter-one-later trick
        // here). They differ in exactly one byte of effect: the value put in $0130 -- 0, 1 or 2 --
        // which is why 0xC0 needs only `64 30 STZ $30` where the others need `A9 0n 85 30`, and why
        // every surrounding branch displacement in the three handlers is shifted by that difference.
        //
        // NOT the ported 0xBB/0xC1/0xC2 family (ctVmTextRequest): this one is THREE bytes not two,
        // reads a second operand, writes $0130 in the PROLOGUE rather than the tail, and writes two
        // extra bytes on completion. Reusing that helper would be wrong on all four counts.
        if (r[0x012D] == 0) {                                  // 3643 / 3645 BNE -- NON-zero CONTINUES
          // 3647 LDX #$7FE0 / 364A BRL C0/2E1E, the fatal hang, which ends in `BRA -2` and NEVER
          // RETURNS. There is no honest IP to report, so trap instead of inventing one -- the same
          // convention the ported sibling already uses for this panic.
          h.ip = ip; h.carry = 0; h.ok = 0; break;
        }
        if (r[0x0129] != 0) {                                  // 364D / 364F -- a request in flight
          h.ip = ip; h.carry = 0; h.ok = 1; break;             // 3671 TYX / CLC -- NO writes at all
        }
        // 3651 `A6 6D LDX $6D` is SIXTEEN-bit, so this index carries $016E as its high byte --
        // unlike 3699's `A5 6D LDA $6D` further down, which takes the low byte only.
        int x6 = r[0x016D] | (r[0x016E] << 8);
        int a0 = 0x10000 + ((0x0A00 + x6) & 0xFFFF);
        if (r[a0] != 0) {                                      // 3653 / 3657 BNE -- OURS completed
          r[a0] = 0x00;                                        // 365D / 365F
          r[0x10000 + ((0x0A80 + x6) & 0xFFFF)] = r[0x0166];   // 3663 LDA $66 / 3665 -- 8-bit both
          r[0x0162] = 0x00;                                    // 3669 STZ $62
          // 366B TYX / three INX / SEC. THREE bytes -- the 0xBB family is two, do not copy it.
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        // ---- arm D: arm the request, then PARK. It reads both operands WITHOUT moving the IP --
        // 36A7's TYX puts X back to ip -- so only the completion arm above ever consumes the bytes.
        r[0x0130] = (uint8_t)(op == 0xC0 ? 0x00 : op == 0xC3 ? 0x01 : 0x02);   // 3659/36C0/36F3
        r[a0] = 0x01;                                          // 3674 / 3676
        r[0x012A] = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 367C / 3680
        int o2 = r[0x12001 + ((ip + 2) & 0xFFFF)];             // 3683
        r[0x01D9] = (uint8_t)o2;                               // 3687 STA $D9 -- the WHOLE byte
        r[0x0165] = (uint8_t)(o2 & 0x03);                      // 3689 AND #03 / 368B
        r[0x0164] = (uint8_t)((o2 >> 2) & 0x03);               // 368D LDA $D9 / LSR / LSR / 3691 / 3693
        r[0x0162] = 0x01;                                      // 3695 / 3697 -- the pair of 3669's 0
        r[0x012E] = r[0x016D];                                 // 3699 -- EIGHT-bit, LOW byte only
        r[0x0132] = 0x00;                                      // 369D STZ $32
        r[0x0129] = 0x01;                                      // 369F / 36A1 -- arms arm B next frame
        // 36A5 `14 54 TRB $54` is test-and-RESET: it CLEARS bit 5. Ops 0x2A/0x2B/0x32 use TSB on
        // the same byte, which SETS. The two are not interchangeable.
        r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x20);
        h.ip = ip; h.carry = 0; h.ok = 1; break;               // 36A7 TYX / CLC -- IP UNCHANGED
      }
      case 0x13: {
        // C0/6313 -- op 0x12 with the operands WIDENED. SIX bytes: [var][immLo][immHi][mode][disp].
        // ⚠ IT IS THE SAME SHAPE AS 0x12 WITH ONE `E2 20 SEP #20` MISSING. Op 0x12 carries it at
        // 62EB, before its `LDA $7F0200,X`; op 0x13's only SEP #20 is deferred to 6330. That single
        // absence makes FOUR accesses sixteen-bit instead of eight -- the local read, its $D9 spill,
        // the script fetch and its $DB spill -- which is what turns operand 2 into a two-byte
        // immediate and pushes the whole opcode from five bytes to six.
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 6315 (8-bit) / 631E ASL / 631F TAX
        { int a = 0x10200 + ((v * 2) & 0xFFFF);
          r[0x01D9] = r[a]; r[0x01DA] = r[a + 1]; }       // 6320 / 6324 STA $D9 (16-bit)
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 6328 / 632C STA $DB (16-bit): TWO
        r[0x01DC] = r[0x12001 + ((ip + 3) & 0xFFFF)];     //   script bytes, not one
        int mode = r[0x12001 + ((ip + 4) & 0xFFFF)];      // 6332, after 6330 SEP #20
        // 6337 STX $C3 with X = ip+5 and X still SIXTEEN-bit (the SEP #10 is at 633A) -- and ip+5
        // is where the displacement lives, one further out than every sibling.
        r[0x01C3] = (uint8_t)(ip + 5); r[0x01C4] = (uint8_t)((ip + 5) >> 8);
        // ⚠ THE OFF-TABLE BOUND IS 8, NOT $10. `633D JSR ($6477,X)` starts at ENTRY 8 of the C0/6467
        // table, so only eight entries remain: $6487 is already handler code (A5 D9), and a mode of
        // 8 would call C0/D9A5. Copying op 0x12's `>= 0x10` would dispatch into code.
        if (mode >= 0x08) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, 8 | mode)) { h.ip = (ip + 6) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 5, &h);                 // 6340 BCS ->6349
        break;
      }
      case 0x14: {
        // C0/6361 -- op 0x12 with the RIGHT operand made a second LOCAL VARIABLE. Five bytes:
        // [var1][var2][mode][disp]. Both `E2 20 SEP #20` are present (636E and 6383), so both reads
        // and both spills are EIGHT-bit: $01DA and $01DC are left alone entirely.
        int v1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 6363 / 636C ASL / 636D TAX
        r[0x01D9] = r[0x10200 + ((v1 * 2) & 0xFFFF)];     // 6370 / 6374 STA $D9 (8-bit)
        int v2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 6378 / 6381 ASL / 6382 TAX
        r[0x01DB] = r[0x10200 + ((v2 * 2) & 0xFFFF)];     // 6385 / 6389 STA $DB (8-bit)
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 638D
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 6392 STX $C3
        // This one dispatches through the table BASE C0/6467, so all sixteen entries are valid and
        // the bound is $10 -- the same as 0x12/0x16, and NOT 0x13/0x15's 8.
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 4, &h);                 // 639B BCS ->6349
        break;
      }
      case 0x15: {
        // C0/63A4 -- both operands are LOCAL WORDS. Five bytes: [var1][var2][mode][disp]. Where op
        // 0x14 has a SEP #20 before each `LDA $7F0200,X`, this has NEITHER: its single one sits at
        // 63CA, after both reads. So all four accesses are sixteen-bit.
        int v1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 63A6 -- 8-bit, the REP #20 is at 63AA
        { int a = 0x10200 + ((v1 * 2) & 0xFFFF);
          r[0x01D9] = r[a]; r[0x01DA] = r[a + 1]; }       // 63B1 / 63B5 STA $D9 (16-bit)
        // 63B9's LDA is sixteen-bit and so physically reads script[ip+2] AND script[ip+3], but
        // 63BD's `AND #00FF` throws the upper one away -- the index is one byte.
        int v2 = r[0x12001 + ((ip + 2) & 0xFFFF)];
        { int a = 0x10200 + ((v2 * 2) & 0xFFFF);
          r[0x01DB] = r[a]; r[0x01DC] = r[a + 1]; }       // 63C2 / 63C6 STA $DB (16-bit)
        int mode = r[0x12001 + ((ip + 3) & 0xFFFF)];      // 63CC, after 63CA SEP #20
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 63D1 STX $C3
        if (mode >= 0x08) { h.ip = ip; h.carry = 0; h.ok = 0; break; }         // the WIDE half again
        if (ctVmCompare(r, 8 | mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }
        ctVmBranchTakenAt(r, ip + 4, &h);                 // 63DA BCC ->63DF / 63DC BRL ->6349
        break;
      }
      case 0x16: {
        // C0/63E6 -- BRANCH ON A GLOBAL (storyline) BYTE. Five bytes: index, comparand, MODEBYTE,
        // disp. The mode byte's BIT 7 is not part of the mode: it selects the high page AND is
        // masked off before dispatch, in the 63FC arm only.
        int li = r[0x12001 + ((ip + 1) & 0xFFFF)];
        r[0x01D9] = (uint8_t)li;                          // 63EC STA $D9 -- overwritten at 6410/6427
        r[0x01DB] = r[0x12001 + ((ip + 2) & 0xFFFF)];     // 63EF / 63F3 STA $DB
        int mb = r[0x12001 + ((ip + 3) & 0xFFFF)];        // 63F6 / 63FA BPL ->6418
        r[0x01C3] = (uint8_t)(ip + 4); r[0x01C4] = (uint8_t)((ip + 4) >> 8);   // 63FD / 6419 STX $C3
        int mode = (mb & 0x80) ? (mb & 0x7F) : mb;        // 63FF AND #7F, on the bit-7 arm ONLY
        r[0x01DD] = (uint8_t)mode;                        // 6401 / 641B STA $DD
        int g = (mb & 0x80) ? ((li | 0x0100) & 0xFFFF) : li;   // 640A ORA #0100
        r[0x01D9] = r[0x10000 + g];                       // 6410 / 6427 LDA $7F0000,X / 642B STA $D9
        if (mode >= 0x10) { h.ip = ip; h.carry = 0; h.ok = 0; break; }   // same off-table case
        if (ctVmCompare(r, mode)) { h.ip = (ip + 5) & 0xFFFF; h.carry = 1; h.ok = 1; break; }  // 643B-6441
        ctVmBranchTakenAt(r, ip + 4, &h);                       // 6436 BCC ->643B / 6438 BRL ->6349
        break;
      }
      case 0x03:
        // C0/607E -- the middle member of the call family. `TYX / INX` and NO INY, so Y stays at
        // the opcode's own IP and the exits supply the whole delta. See ctVmCall03 for why its
        // park/continue behaviour is neither 0x02's nor 0x04's.
        ctVmCall03(r, ip, r[0x12001 + ((ip + 1) & 0xFFFF)], &h);        // 6082 / join 6086
        break;
      case 0x05: case 0x06: case 0x07: {
        // C0/61E9 / 61FE / 6214 -- THE PARTY-SLOT CALLS. Each is its parent opcode with the target
        // taken from `$0197 + slot` instead of read as an object index, plus a skip arm for an
        // empty slot:
        //   0x05 -> `BRL ->5FBE`, op 0x02's body      0x06 -> `BRL ->6086`, op 0x03's body
        //   0x07 -> `BRL ->6122` (fresh) and `->61AF` (poll), op 0x04's two arms
        // ⚠ Y DIFFERS BETWEEN THEM AND IT IS NOT COSMETIC. 0x05 opens `61E9 INY` (matching op
        // 0x02's own 5FB6 INY) so Y is ip+1 at its join; 0x06 and 0x07 open `TYX / INX` with Y
        // UNTOUCHED (matching ops 0x03 and 0x04) so Y is ip. Each sibling mirrors its parent
        // exactly, which is why the shared bodies need no ip fixup at all -- and why pairing the
        // wrong sibling with the wrong parent would be off by one everywhere downstream.
        int slot = r[0x12001 + ((ip + 1) & 0xFFFF)];                    // 61ED / 6202 / 6220
        int tgt  = ctVmPartySlotTarget(r, slot);                        // 61F2 / 6207 / 6225
        if (tgt & 0x80) {                    // 61F4 / 6209 / 6227 BMI -- the slot is EMPTY
          // 61F9 INY INY / 620E INY INY INY / 622C INY INY INY, then TYX / SEC. Different counts
          // because Y started differently; all three land on ip+3.
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        if (op == 0x05)      h.ip = ctVmCall02(r, ip, tgt, &h.carry), h.ok = 1;
        else if (op == 0x06) ctVmCall03(r, ip, tgt, &h);
        else                 h.ip = ctVmCall04(r, idx, ip, tgt, &h.carry), h.ok = 1;
        break;
      }
      case 0xD8:
        // C0/3A83 -- A REQUEST/ACK RENDEZVOUS on $0117/$0118 bit 0, three bytes, ALWAYS CLC.
        // Three arms, and only ONE of them moves the IP:
        //   $0117 bit 0 SET -> 3AAA: park on this byte, writing nothing. The request is in flight.
        //   $0118 bit 0 SET -> 3AAD: `LDA #01 / TRB $18` clears the ACK and advances ip+3.
        //   otherwise       -> 3A8F: ISSUE -- stage both operands, `ORA #21` into $0117 (bits 0 AND
        //                      5, not just the request bit), STZ $011F, then FALL THROUGH into
        //                      3AAA and park. Issuing costs the visit; only the ack advances.
        // The completion arm is `CLC / TYX / INX / INX / INX` -- THREE INX. A truncated listing
        // makes it look like two, and the operands only account for two, so it is worth reading to
        // the RTS: the byte after the second operand is consumed by the ack, not by the issue.
        //
        // Unlike op 0xC8's rendezvous, this one is NOT walled off behind another bank: `LDA #01 /
        // TSB $18` appears at C0/18BF and C0/18D4, so the acknowledgement is raised from within
        // bank $C0. The same shape as op 0xBB's $0129 latch, which nothing inside its own handler
        // can clear either.
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
        // C0/3557 -- STAGE THE THREE TEXT-MACHINE BYTES $012B/$012C/$012D. Four bytes, branchless,
        // SEC, and no INY anywhere -- `TYX / INX` walks the operands and the final INX at 356D makes
        // it ip+4. $012D is the byte op 0xBB REQUIRES to be non-zero: falling into C0/3572 with it
        // clear runs the ROM's panic handler, which never returns. So this opcode is what makes a
        // text request legal, and the two belong to one mechanism.
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
        // C0/68C6..6A11 -- THE VARIABLE-ASSIGNMENT GRID. The body lives in CtVmVarFamily because the live event
        // VM's step() and its init pass run it too (the init pass had no 0x53/0x58 at all: the fair's obj0 fn0
        // `53 52 00 1B` never loaded the Silver Points). THIS call is what --sweepops grades. SEC on every exit.
        int ni = ip;
        CtVmVarFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xA8: case 0xA9: {
        // C0/55A0 and C0/5605 -- TURN TO FACE ANOTHER OBJECT. Two bytes each. 0xA8 takes the target
        // as an object index; 0xA9 takes a PARTY SLOT and resolves it through `560E LDA $97,X`
        // before `BRL ->55A9`, joining 0xA8's body with X already holding the object index and Y
        // still the opcode's own IP -- so both are ip+2.
        //
        // ⚠ 0xA9 DOES NOT TEST THE SLOT FOR EMPTY. The party-slot CALLS (0x05/0x06/0x07) all check
        // bit 7 and skip; this one does not, so an empty slot ($80) is used as object index $80 and
        // the handler proceeds. That is the ROM's behaviour and it is not an oversight to correct.
        //
        // ⚠ AND THE TWO ARMS DIFFER IN THE CARRY, which is the whole reason this opcode is not
        // "compute a facing": an INACTIVE target exits SEC at 5603 (skip it, keep running), while
        // the normal path exits CLC at 55FE (the turn costs the visit).
        int tgt;
        if (op == 0xA8) tgt = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 55A4 / 55A8 TAX
        else            tgt = r[(0x0197 + r[0x12001 + ((ip + 1) & 0xFFFF)]) & 0x1FFFF];  // 560E
        if (r[0x1100 + tgt] & 0x80) {                                  // 55A9 / 55AC BMI ->5600
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 5600 TYX / INX / INX / SEC
        }
        r[0x01F2] = r[0x1801 + tgt];                                   // 55AE the target's TILE X
        r[0x01F3] = r[0x1881 + tgt];                                   // 55B3 ...and TILE Y
        r[0x01D9] = (uint8_t)tgt; r[0x01DA] = 0x00;                    // 55B8 STX $D9 -- X is 16-bit
        r[0x01F0] = r[0x1801 + idx];                                   // 55BC this object's TILE X
        r[0x01F1] = r[0x1881 + idx];                                   // 55C1 ...and TILE Y
        // 55C6-55CE: if BOTH tiles match, the tile coordinates cannot produce a direction, so the
        // routine re-stages the SUB-TILE positions instead and aims with those. Note the operand
        // order flips with them -- self into $F0/$F1 from $1800/$1880, target into $F2/$F3.
        if (r[0x01F1] == r[0x01F3] && r[0x01F0] == r[0x01F2]) {
          r[0x01F0] = r[0x1800 + idx]; r[0x01F1] = r[0x1880 + idx];    // 55D0 / 55D5
          r[0x01F2] = r[0x1800 + tgt]; r[0x01F3] = r[0x1880 + tgt];    // 55DC / 55E1
        }
        int ee = CtMoveDirAngle(r, rom);                               // 55E6 JSR C0/ABA2
        r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];                   // 55F0 / 55F5
        r[0x1601 + idx] = 0x00;                                        // 55F8 STZ $1601,X
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;        // 55FB TYX / INX / INX / CLC
      }
      case 0x9C: {
        // C0/4EC7 -- MOVE ALONG A HEADING WITHOUT TURNING. Three bytes, and structurally op 0x92's
        // twin -- same $1A80 progress flag, same $1A01 ticket, same park-on-the-opcode, same three
        // arms -- but it is NOT op 0x92, and the differences are all OMISSIONS, which is the kind
        // that vanishes if you port it by copying the neighbour:
        //   * NO `LDA $F700,X / STA $1600,X`. Op 0x92 turns the actor to face its heading; this one
        //     leaves the facing alone, so the sprite keeps its current direction while it slides.
        //   * NO `JSR C0/5614` after the velocity build. Op 0x92 re-selects the walk anim at every
        //     step issue; this one does not, so whatever animation is playing keeps playing.
        //   * NO `JSR C0/568B` on the arrival visit -- op 0x92 plays the arrival anim, 4ED6 just
        //     clears the flag and advances.
        // Together: a pure translation with the animation left entirely to the script.
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
        r[0x10B00 + idx] = 0x80;                           // 4EFA STA $7F0B00,X -- a LITERAL, as 0x92
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);  // 4EFE INC $1A80,X -- increment, not set
        // 4F01 `STY $C7` with Y STILL THE OPCODE'S OWN IP -- this handler never executes INY, it
        // walks the operands with TYX/INX -- so 4F06 `LDX $C7` parks exactly here.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);              // 4F03 JSR C0/ACFD (reads $01EE itself)
        h.ip = ip; h.carry = 0; h.ok = 1; break;           // 4F08 CLC -- the arm visit parks too
      }
      case 0x18: {
        // C0/6442 -- BRANCH ON THE STORYLINE BYTE ($7F0000, global index 0). Three bytes, SEC on
        // both exits, and structurally op 0x1A's twin: `6448 CMP $7F2001,X / 644C BCS ->6452` takes
        // the DISPLACEMENT when storyline >= operand and falls through to ip+3 when it is LESS.
        // So the opcode means "continue while the story has not reached N, else jump past" -- and
        // as with 0x1A the branch base is ip+2, the displacement's own address, not ip+3.
        if (r[0x10000] < r[0x12001 + ((ip + 1) & 0xFFFF)]) {           // 644C BCS not taken
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 644E INX / INX / SEC
        }
        int d = r[0x12001 + ((ip + 2) & 0xFFFF)];                      // 6453
        r[0x01D9] = (uint8_t)d; r[0x01DA] = 0x00;                      // 645C STA $D9 -- SIXTEEN-bit
        h.ip = (ip + 2 + d) & 0xFFFF; h.carry = 1; h.ok = 1; break;    // 645E-6462
      }
      case 0xED:
        // C0/3CBC -- WAIT ON APU MAILBOX PORT 1. `TYX / LDA $2141 / BNE ->3CC3 / INX / CLC / RTS`.
        // ONE byte, always CLC, and as with 0xF2/0xF3 the branch decides only whether the IP MOVES:
        // non-zero parks on this byte, zero advances to ip+1. Unlike its sibling 0xEE (which reads
        // $2143 and masks `AND #0F`) this one tests the WHOLE byte.
        h.ip = (g_ctApuPort1 == 0) ? ((ip + 1) & 0xFFFF) : ip;
        h.carry = 0; h.ok = 1; break;
      case 0xF2:
        // C0/3CFD -- WAIT ON $0118 BIT 1, the bit op 0xF0's non-zero arm SETS with `TSB $18`. One
        // byte, always CLC, park while set. Same shape as 0xF3, one bit over.
        h.ip = (r[0x0118] & 0x02) ? ip : ((ip + 1) & 0xFFFF);
        h.carry = 0; h.ok = 1; break;
      case 0xF0: {
        // C0/3CD0 -- SCREEN FADE / BRIGHTNESS, two bytes and CLC on both arms, which the ROM
        // selects on the operand's LOW NIBBLE:
        //   low != 0 -> $011C = $011D = low, $011B = high, and `LDA #02 / TSB $18` ARMS the wait
        //               that op 0xF2 spins on. A timed fade.
        //   low == 0 -> $0119 = high and NOTHING else -- no $011B/$011C/$011D, no $0118 bit. An
        //               immediate set of the brightness byte, with no wait to arm.
        // Both exits are ip+2 because `3CD6 INX` runs BEFORE the split, so the two arms differ in
        // their writes alone. $0119 is SCREEN brightness (not, as once recorded, music volume).
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
        // C0/68A5 -- LOCAL VARIABLE := IMMEDIATE. Three bytes: the VALUE comes first (ip+1) and the
        // variable INDEX second (ip+2), which is the reverse of the order the mnemonic suggests.
        // The index is doubled (68B7 ASL A) into the same $7F0200 array, stride TWO, that ops 0x12
        // and 0x71..0x77 use -- but the store is 8-BIT (SEP #20 at 68B9 comes before it).
        int v = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 68A7
        r[0x01D9] = (uint8_t)v;                            // 68AB STA $D9
        int vi = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 68AE
        r[0x10200 + ((vi * 2) & 0xFFFF)] = (uint8_t)v;     // 68BD STA $7F0200,X
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x19: case 0x1C: case 0x1A:
        CtVmResultFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0x65: case 0x66: {
        // C0/6B76 and C0/6BA4 -- SET and CLEAR a bit in the GLOBAL flag page. Three bytes:
        // operand1 selects the bit, operand2 is the byte index.
        //   mask = C0:$FF20[operand1 & $0F]   (0x65, an OR mask)
        //   mask = C0:$FF28[operand1 & $0F]   (0x66, an AND mask)
        //   page = BIT 7 OF OPERAND1, and that is worth deriving rather than reading: 6B88-6B8C is
        //     `LDA $DB / ROL A / ROL A / AND #01`, and two ROLs leave bit 7 of the ORIGINAL byte in
        //     bit 0 regardless of the carry coming in -- the first ROL's carry-out IS bit 7, and the
        //     second shifts it into bit 0. So it does not depend on the runner's carry state.
        //   6B8E `XBA` parks that bit in B, so the 8-bit `LDA` of operand2 followed by the 16-bit
        //     `TAX` at 6B95 gives X = page*256 + operand2. The address is $7F0000 + X.
        // ⚠ THE INDEX IS THE WHOLE LOW NIBBLE, NOT THREE BITS, AND THE TWO TABLES OVERLAP AND THEN
        // DIVERGE. $FF20 is `01 02 04 08 10 20 40 80 FE FD FB F7 EF DF BF 7F` and $FF28 starts at
        // that table's ninth byte -- so $FF28[0..7] is $FF20[8..15], but $FF28[8..15] runs on into
        // `00 00 01 00 02 00 03 00`, which is not a mask table at all. An operand1 with a low
        // nibble >= 8 therefore makes 0x65 OR with an AND-mask (setting seven bits at once) and
        // makes 0x66 AND with 0, 1, 2 or 3. That is what the ROM does; "correcting" either to
        // `1 << (operand1 & 7)` would invent a table the cartridge does not contain. Both are read
        // straight out of `rom` rather than transcribed, so there is nothing to get wrong.
        int o1 = r[0x12001 + ((ip + 1) & 0xFFFF)];         // 6B7A
        r[0x01DB] = (uint8_t)o1;                           // 6B7E STA $DB
        int mt = (op == 0x65) ? 0xFF20 : 0xFF28;           // 6B83 / 6BB1 LDA $FFxx,X
        int m = (mt + (o1 & 0x0F) < romLen) ? rom[mt + (o1 & 0x0F)] : 0;
        r[0x01D9] = (uint8_t)m;                            // 6B86 STA $D9
        int a = 0x10000 + ((((o1 >> 7) & 1) << 8) | r[0x12001 + ((ip + 2) & 0xFFFF)]);
        r[a] = (uint8_t)(op == 0x65 ? (r[a] | m) : (r[a] & m));        // 6B9A ORA / 6BC8 AND
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0x84:
        // C0/4867 -- SET THE SOLIDITY BYTE $1B01,X from the operand. Two INYs (4867 and 4872) and
        // the exit is a bare `TYX / SEC`, so the deltas cancel out to ip+2 with no INX at all:
        //   4867 INY / TYX / LDA $7F2001,X / 486D LDX $6D / 486F STA $1B01,X
        //   4872 INY / 4873 TYX / SEC / RTS
        // $1B01 is the byte C0/5B95's actor-collision scan tests at 5BDB to decide whether a
        // candidate blocks the mover at all, and the object loaders set it to 1 as they spawn.
        r[0x1B01 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];   // 4869 / 486F
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xE0: {
        // C0/3AB7 -- STAGE A LOCATION CHANGE, FADE VARIANT. It writes the request and NEVER ADVANCES
        // THE IP: both exits are `TYX / CLC`, and Y is untouched throughout (only X is stepped to
        // walk the operands), so the object parks ON the opcode byte. The commit happens elsewhere
        // and replaces the script; re-entry meanwhile hits the bit-7 guard below and does nothing.
        if (r[0x0117] & 0x80) {                                       // 3AB9 BMI ->3AEE
          h.ip = ip; h.carry = 0; h.ok = 1; break;                    // 3AEE TYX / CLC
        }
        int w1 = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 3ABF
        r[0x01D9] = (uint8_t)w1; r[0x01DA] = (uint8_t)(w1 >> 8);      // 3AC3 STA $D9 (16-bit)
        int loc = w1 & 0x01FF;                                        // 3AC5 AND #01FF -- NINE bits
        r[0x0112] = (uint8_t)loc; r[0x0113] = (uint8_t)(loc >> 8);    // 3AC8 STA $12 (16-bit)
        int w2 = r[0x12001 + ((ip + 3) & 0xFFFF)] | (r[0x12001 + ((ip + 4) & 0xFFFF)] << 8);  // 3ACC
        r[0x0114] = (uint8_t)w2; r[0x0115] = (uint8_t)(w2 >> 8);      // 3AD0 STA $14 (16-bit)
        // $0116 is built from the FIRST operand's HIGH BYTE alone, in two passes over it: the
        // shifted middle nibble is STORED, then bit 7 is OR'd in with TSB.
        int hb = r[0x01DA];                                           // 3AD4 LDA $DA
        r[0x0116] = (uint8_t)((hb >> 1) & 0x0F);                      // 3AD6/3AD7/3AD9
        r[0x0116] = (uint8_t)(r[0x0116] | (hb & 0x80));               // 3ADF TSB $16
        r[0x0117] = (uint8_t)(r[0x0117] | 0x80);                      // 3AE5 -- the one-shot latch
        r[0x0119] = 0x0F;                                             // 3AE9 STA $19
        h.ip = ip; h.carry = 0; h.ok = 1; break;                      // 3AEB TYX / CLC -- NO advance
      }
      case 0xE2:
        CtVmChangeLocVars(r, r + 0x12001, ip, &h); break;             // C0/3B9E, shared with ct_vm.c step()
      case 0x2E:
        CtVmColorSlotOp(r, r + 0x12001, ip, &h); break;   // one body, shared with ct_vm.c step() + the init pass
      case 0xD9: {
        // C0/326C -- SCATTER THE PARTY. A SEVEN-byte opcode (three slots x two coordinate bytes)
        // driven by the phase counter $7F03FC, which it both reads and writes, so it re-enters on
        // successive frames and only advances the IP once the whole sequence has drained.
        //   phase 0      -> the first-visit block: release each occupied slot, stash its destination
        //   phase 1..3   -> park at ip, nothing to do yet
        //   phase >= 4   -> the completion check, the ONLY path that consumes the seven bytes
        // Nothing here sets it to 4 or more; the drain that finishes the walk-off does.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 326C STY $C7 (16-bit)
        int ph = r[0x103FC];                                          // 326E LDA $7F03FC
        if (ph != 0) {                                                // 3272 BEQ ->32C8
          if (ph < 0x04) { h.ip = ip; h.carry = 0; h.ok = 1; break; } // 3274/3276 -- 3278 TYX / CLC
          ctVmPartyClear(r);                                          // 327B..3296
          // 329A/32A4/32AE: each slot whose index is NOT bit-7-flagged must have reached priority
          // zero. Any that has not sends the whole opcode back to 3278 to park -- so this is a
          // rendezvous across all three, not three independent tests.
          for (int s = 0; s < 3; s++) {
            int v = r[0x0197 + s * 2];
            if (!(v & 0x80) && r[0x1C00 + v] == 0) { h.ip = ip; h.carry = 0; h.ok = 1; goto d9done; }
          }
          r[0x103FC] = 0x00;                                          // 32B8/32BA reset the phase
          h.ip = (ip + 7) & 0xFFFF; h.carry = 0; h.ok = 1;            // 32BE x7 INY / TYX / CLC
          d9done: break;
        }
        r[0x011F] = 0x00;                                             // 32C8 STZ $1F
        // Slot 0 is released UNCONDITIONALLY -- it alone gets no bit-7 test, and it alone takes its
        // read index from a 16-bit `LDX $97` rather than a zero-extended TAX.
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
        // C0/345A -- the ONE-byte sibling of 0xD9, releasing slots 1 and 2 only, on its own phase
        // counter $7F03FE and behind the $0154 bit-0 latch. Both its copies park $16FF in $1180,
        // where 0xD9 uses three different literals.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);      // 345A STY $C7
        if (r[0x0199] & 0x80) {                                       // 345F/3461 BPL ->3466
          h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;     // 3463 INX / CLC
        }
        if (r[0x0154] & 0x01) {                                       // 3466/3468 BIT #01
          // The latch is already up: this is a LATER visit, waiting for the phase to reach 3.
          if (r[0x103FE] < 0x03) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 3470/3472 -- 3474
          r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x01);          // 3477/3479 TRB $54
          ctVmPartyClear(r);                                          // 347B..3496
          h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;     // 349A LDX $C7 / INX / SEC
        }
        // 349F STX $C3 -- a SECOND 16-bit IP spill, to a different pair than $C7. 0xD9 has no
        // counterpart; it reloads from $C7 throughout.
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
        // THE SCREEN-EFFECT OPS, C0/3D07 / C0/3F26. The bodies live in CtVmScreenFxOp because the LIVE event VM runs them
        // too (it stepped over both). THIS call is what --sweepops micro-executes; the live VM inherits the grade.
        CtVmScreenFxOp(r, r + 0x12001, ip, &h); break;
      case 0xF3:
        // C0/3D7A -- WAIT ON $0118 BIT 3. `TYX / LDA $18 / BIT #08 / BNE ->3D82 / INX / CLC / RTS`.
        // ONE byte, and it exits CLC either way -- the branch decides only whether the IP MOVES.
        // Bit SET keeps X at ip (park on this byte and re-test next visit); bit CLEAR advances to
        // ip+1. So it blocks WHILE the bit is set, which is the opposite of how the BNE reads.
        h.ip = (r[0x0118] & 0x08) ? ip : ((ip + 1) & 0xFFFF);
        h.carry = 0; h.ok = 1; break;
      case 0xFF: {
        // C0/3FAF -- THE SCREEN-MODE OPCODE, and it is PARTIALLY blocked. `3FB7 CMP #90 / 3FB9 BCS
        // ->4000` splits it in two, and the split is what makes the port possible:
        //   subcmd <  $90  the mode-SWITCH path. It runs a fade loop containing `JSL $FD:FFF7`,
        //                  then `3FD6 LDX #06FF / TXS` -- it HARD-RESETS THE STACK POINTER and
        //                  never returns to the runner, ending in `JML $C3:0000` or one of four
        //                  `JML $FD:xxxx` stubs. Unportable, and there is no honest IP: TRAP.
        //   subcmd >= $90  `4000..400D`, which never touches any of that: it indexes the 16-entry
        //                  table at C0/400E and ends `400C CLC / 400D RTS`, so every returning
        //                  sub-handler yields and the IP is whatever X the sub-handler left.
        //
        // $01DF = the subcmd is written FIRST, on EVERY invocation, before any branch -- so even
        // the blocked arms commit that one byte. The port writes it and then traps, mirroring how
        // ops 0x12..0x16 commit their staging before an off-table dispatch.
        int sc = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 3FB1
        r[0x01DF] = (uint8_t)sc;                          // 3FB5 STA $DF -- before the CMP
        // ⚠ TWO MORE SUB-COMMANDS ARE BLOCKED INSIDE THE PORTABLE HALF, and $A0-$A3 are the ROM's
        // own panic: table entries 16-19 all hold $5F6E, the invalid-opcode stub that 32 other
        // opcodes share. Past $A3 the index runs off the table into code.
        if (sc < 0x90 || sc == 0x96 || sc == 0x9F || sc > 0x9F) {
          h.ip = ip; h.carry = 0; h.ok = 0; break;        // 0x96 -> JML $FD:C000, 0x9F -> $C2:8004
        }
        switch (sc) {
          case 0x90: case 0x97: {
            // C0/4036 and C0/4191 -- identical but for the mode they arm. FIVE bytes: three
            // operands from ip+2..ip+4, and $013C is ZEROED rather than loaded.
            r[0x013A] = r[0x12001 + ((ip + 2) & 0xFFFF)];         // 4039 / 4194
            r[0x013B] = r[0x12001 + ((ip + 3) & 0xFFFF)];         // 4040 / 419B
            r[0x013D] = r[0x12001 + ((ip + 4) & 0xFFFF)];         // 4047 / 41A2
            r[0x013C] = 0x00;                                     // 404D / 41A8 STZ $3C
            r[0x0139] = (uint8_t)(sc == 0x90 ? 0x01 : 0x04);      // 404F / 41AA -- the ONLY difference
            h.ip = (ip + 5) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          }
          case 0x91:                                              // C0/4055
            r[0x0139] = 0x02;                                     // 4058 / 405A
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C:
            // C0/41B0/41B4/41B8/41C0/41C4 -- five two-byte entries into ONE tail at C0/41BA
            // (`STA $39 / TYX / INX / INX`). $41B8 is the fall-through case and the other four BRA in.
            r[0x0139] = (uint8_t)(sc == 0x98 ? 0x05 : sc == 0x99 ? 0x07 :
                                  sc == 0x9A ? 0x09 : sc == 0x9B ? 0x0B : 0x0E);
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x9D:                                              // C0/41DC
            // `LDA #02 / TRB $54` -- test-and-RESET, so it CLEARS bit 1 of $0154. Ops 0x2A/0x2B/0x32
            // use TSB on the same byte to SET.
            r[0x0154] = (uint8_t)(r[0x0154] & (uint8_t)~0x02);    // 41DE
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          case 0x9E:                                              // C0/41C8
            if (r[0x0139] == 0x00) {                              // 41CA BEQ ->41D6
              r[0x0139] = 0x0C;                                   // 41D6 / 41D8
              h.ip = ip; h.carry = 0; h.ok = 1; break;            // 41DA TYX -- parks
            }
            if (r[0x0139] == 0x0D) {                              // 41CC CMP #0D / 41CE BEQ ->41D2
              h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;   // and $0139 is LEFT at $0D
            }
            h.ip = ip; h.carry = 0; h.ok = 1; break;              // 41D0 -- still running, park
          default: {
            // C0/405D / 40EF / 40B3 / 4145 -- ops 0x92/0x93/0x94/0x95, ONE shape with two finish
            // bodies. Each is a three-state handshake against $0139 and $0350.
            int p = (sc == 0x92 || sc == 0x94) ? 0x01 : 0x02;     // 4098 / 412A / 40D4 / 4166
            int c = (sc == 0x92) ? 0x00 : (sc == 0x93) ? 0x01 : (sc == 0x95) ? 0x02 : 0x03;
            int shortFinish = (sc == 0x94 || sc == 0x95);         // 40C2 / 4154 vs 406C / 40FE
            if (r[0x0139] == 0x00) {                              // 405F BEQ ->408E
              if (r[0x0350] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 4091 -- busy, park
              r[0x0351] = 0x00;                                   // 4095 STZ $0351
              r[0x0350] = (uint8_t)p;                             // 409A
              r[0x0352] = 0xC8; r[0x0353] = 0x17;                 // 40A0 STX $0352 -- SIXTEEN bit
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
              r[0x0BDB] = 0x00; r[0x0BDD] = 0x00;                 // 4080 / 4083 -- ⚠ $0BDC IS SKIPPED
              r[0x0128] = (uint8_t)(r[0x0128] & (uint8_t)~0x80);  // 4088 TRB $28 -- reset, not store
            }
            h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
          }
        }
        break;
      }
      case 0xE4:
        // C0/3D97 -- IMMEDIATE SCRIPTED TILE COPY (CtVmTileCopyE4, shared with the live VM). The script is the
        // image at $7F:2001, which is where this runner keeps it.
        CtVmTileCopyE4(r, r + 0x12001, ip);
        h.ip = (ip + 8) & 0xFFFF; h.carry = 0; h.ok = 1; break;       // 3DC0 LDX $C7 / INX / CLC
      case 0xE5:
        // C0/3DC5 -- STAGE A TILE COPY (CtVmTileStageE5, shared with the live VM): $013E-$0145 and $0117 bit 5, no
        // copy -- the main loop's C0/1770 makes it (ct_vm.c, the $0117 bit-5 service). It exits CLC.
        CtVmTileStageE5(r, r + 0x12001, ip);
        h.ip = (ip + 8) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0xE6: case 0xF4:
        CtVmScrollFamily(r, rom, romLen, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c (step + init pass)
      case 0xE7:
        // C0/3E30 -- a THREE-STATE opcode driven by the counter $1D00, and the only one here whose
        // three arms have three different IPs:
        //   $1D00 == 0 -> 3E41: arm it. Both operands are DOUBLED (`ASL A`) into $1D02/$1D03, the
        //                 counter is INCremented and $011F is cleared; then `3E59 TYX / CLC` parks
        //                 on this very byte with the IP UNCHANGED.
        //   $1D00 == 1 -> 3E36 BEQ ->3E59: park again, writing NOTHING. This is the wait.
        //   otherwise  -> 3E38: clear the counter and advance THREE bytes, SEC.
        // So the opcode arms, spins, and releases -- and only the release moves the IP.
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
        // C0/6240 -- ARM THIS OBJECT'S BUSY FLAG. `LDX $6D / LDA #01 / STA $1C01,X / TYX / INX /
        // SEC / RTS`. ONE byte -- no INY anywhere, so the exit's single INX is the whole delta.
        // $1C01,X is the byte op 0x02 rejects on (5FC3) and op 0x04 PARKS on (6127), so this pair
        // is how a script makes itself uncallable and callable again.
        r[0x1C01 + idx] = 0x01;                            // 6244
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x09:
        // C0/624B -- its partner: `LDA #00 / STA $1C01,X / BRA ->6247`, joining 0x08's own tail.
        r[0x1C01 + idx] = 0x00;                            // 624F
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x0D:
        // C0/6295 -- SET THE PER-OBJECT COLLISION GATE $1C80,X. Two bytes, IP+2, carry SET.
        //   6295 C8 INY / 6296 BB TYX / 6297 7B TDC / 6298 EB XBA   <- the "zero A's high byte"
        //     idiom (TDC moves the 16-bit DP register into C, XBA parks it), NOT a memory access
        //   6299 BF 01 20 7F LDA $7F2001,X / 629D A6 6D LDX $6D / 629F 9D 80 1C STA $1C80,X
        //   62A2 80 0D BRA ->62B1 = TYX / INX / SEC / RTS
        // ONE write. $1C80 bit 0 is the byte C0/ACFD tests to decide whether the BOUNCE arm exists
        // at all (ADA1/ADA4/ADA6), so this opcode is what arms map-solidity for an object.
        r[0x1C80 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0x0E:
        // C0/62A4 -- the SIBLING of 0x0D, byte-for-byte identical except `9D 81 1C STA $1C81,X`,
        // and it does not even share the tail by branching: it carries its own `BB E8 38 60`.
        // $1C81 bit 0 is op 0x96's snap-to-tile-centre opt-in (C0/4F90 BIT #01).
        r[0x1C81 + idx] = r[0x12001 + ((ip + 1) & 0xFFFF)];
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xAC:
        CtVmAnimFamily(r, r + 0x12001, ip, idx, &h); break;   // one body, shared with ct_vm.c step() -- see the definition
      case 0x90:
        // C0/4CD5 -- SHOW. The head of the three-way family whose shared tail is 4CD9
        // `STA $1A81,X / TYX / INX / CLC / RTS`: 0x90 enters with #$01, 0x91 (C0/4CE0) with #$00 and
        // 0x7E (C0/4CE6) with #$80. One byte, IP+1, carry CLEAR. $1A81 is the byte C0/A88D rejects
        // an object on when it is 0 or has bit 7 set, i.e. this is the visibility latch.
        r[0x1A81 + idx] = 0x01;                               // 4CD7 LDA #01 / 4CD9 STA $1A81,X
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x7C: case 0x7D:
        // C0/4CEC and C0/4CF9 -- the OTHER-OBJECT entries into the same 4CD9 tail. Where 0x90,
        // 0x91 and 0x7E do `LDX $6D` and act on the RUNNING object, these two read an operand and
        // `TAX` it, so the store lands on $1A81 + THAT object instead:
        //   4CEC INY / TYX / TDC / XBA / LDA $7F2001,X / 4CF4 TAX / LDA #01 / BRA ->4CD9
        //   4CF9 the same with #00
        // TWO bytes each -- the INY at 4CEC/4CF9 is the only difference in the IP arithmetic, the
        // shared `4CDC TYX / INX` being common to all five. And they inherit the tail's CLC, so
        // setting ANOTHER object's visibility yields the visit exactly as setting your own does.
        r[0x1A81 + r[0x12001 + ((ip + 1) & 0xFFFF)]] = (uint8_t)(op == 0x7C ? 0x01 : 0x00);
        h.ip = (ip + 2) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x7E:
        // C0/4CE6 -- the third entry into that same tail, with #$80. A88D reads $1A81 bit 7 as a
        // reject too (A899 BMI ->A910), so $80 is a DIFFERENT kind of hidden from 0x91's $00.
        r[0x1A81 + idx] = 0x80;                               // 4CE8 LDA #80 / 4CEA BRA ->4CD9
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x91:
        // C0/4CE0 -- `LDX $6D / LDA #00 / BRA ->4CD9`, i.e. it falls into op 0x90's tail with a
        // different immediate. That shared tail is the whole routine:
        //   4CD9 STA $1A81,X / 4CDC TYX / 4CDD INX / 4CDE CLC / 4CDF RTS
        // One byte, IP+1, carry CLEAR (yield). Project memory names 0x90/0x91 SHOW/HIDE, and the
        // immediates (1 vs 0) are consistent with that -- but what READS $1A81 is not derived here,
        // so this comment does not claim it.
        // The two siblings sharing the tail are DELIBERATELY NOT PORTED: 4CD5 (op 0x90) stores 1 and
        // 4CE6 stores $80. Nothing in any current check reaches either -- the trap tally lists only
        // 0x0A and 0x91 -- and porting them for symmetry is how ungraded code accumulates.
        r[0x1A81 + idx] = 0;                              // 4CD9 with A = #00
        h.ip = (ip + 1) & 0xFFFF; h.carry = 0; h.ok = 1; break;
      case 0x92: {
        // C0/4E73 -- MOVE ALONG A FIXED HEADING FOR A DURATION. Three bytes (heading, duration).
        // Where op 0x96 below re-aims at a TARGET TILE on every visit, this one aims ONCE and then
        // just runs the clock down, so its progress flag is a second byte, $1A80,X, and the whole
        // handler is split on it. Like 0x96 it PARKS ON THE OPCODE and only the arrival visit
        // consumes the three bytes.
        if (r[0x1A80 + idx] != 0) {                       // 4E75 LDA $1A80,X / 4E78 BEQ ->4E8E
          if (r[0x1A01 + idx] != 0) {                     // 4E7A LDA $1A01,X / 4E7D BEQ ->4E82
            h.ip = ip; h.carry = 0; h.ok = 1; break;      // 4E7F TYX / 4E80 CLC -- ticket still running
          }
          r[0x1A80 + idx] = 0x00;                         // 4E82 STZ $1A80,X -- the leg is over
          moveArriveAnim(r, idx);                         // 4E85 JSR C0/568B (already ported)
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4E88 TYX/INX x3 / 4E8C SEC
        }
        // 4E8E: START. Note the heading is taken VERBATIM from the operand -- there is no C0/ABA2
        // here, which is exactly what separates this opcode from 0x96.
        int ee = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4E8E TYX/INX / 4E90 LDA $7F2001,X
        // 4E94 `REP #20` makes 4E99 `STA $EE` a SIXTEEN-BIT store, so it lands on $01EE AND $01EF,
        // and 4E96 `AND #00FF` has already cleared the high byte -- $01EF is zeroed, not left alone.
        r[0x01EE] = (uint8_t)ee; r[0x01EF] = 0x00;        // 4E99 STA $EE -- DP = $0100, so $01EE
        r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];      // 4E9E LDA $F700,X / 4EA3 STA $1600,X
        r[0x10B00 + idx] = 0x80;                          // 4EA6 LDA #80 / 4EA8 STA $7F0B00,X --
                                                          //   a LITERAL here; op 0x96 mirrors $1000
        r[0x1A01 + idx] = r[0x12001 + ((ip + 2) & 0xFFFF)];   // 4EAF operand 2 = DURATION -> ticket
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1);     // 4EB8 INC $1A80,X -- an INCREMENT,
                                                          //   8-bit (M still set from 4E9C SEP #20)
        // 4EBB `STY $C7` spills the IP because C0/ACFD clobbers Y, and 4EC3 `LDX $C7` reloads it.
        // The store is SIXTEEN-bit (X flag clear), so it lands on $01C7 AND $01C8. The port keeps
        // the IP in a C local and does not need the spill -- but the ROM WRITES THOSE TWO BYTES, and
        // --sweepops compares the whole 128 KB image, so leaving them out is a real mismatch.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);               // 4EBD JSR C0/ACFD (reads $01EE itself)
        ctMoveStepAnim5614(r, idx);                       // 4EC0 JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4EC3 LDX $C7 / 4EC5 CLC -- park and yield
      }
      case 0xEE:
        // C0/3CC5 -- WAIT ON THE SOUND DRIVER, and the ONLY field opcode that reads outside WRAM.
        //   3CC5 BB          TYX          X = the IP of this very opcode
        //   3CC6 AD 43 21    LDA $2143    the SPC700's OUTPUT mailbox port 3
        //   3CC9 29 0F       AND #0F
        //   3CCB D0 01       BNE ->3CCE   low nibble non-zero -> leave X parked on the opcode
        //   3CCD E8          INX          zero -> consume the single byte
        //   3CCE 18 60       CLC / RTS    BOTH arms yield; only the IP differs
        // $2143 is NOT WRAM and NOT readable from r[]. On the CPU side of the SNES, writes to
        // $2140-$2143 go to the SPC's INPUT ports while reads return its OUTPUT ports, so this
        // cannot be a read-back of something the CPU stored: a ROM-wide scan finds $2143 written 30
        // times by the bank-$C7 Akao driver and read in the whole field engine ONLY here. It is a
        // genuine CPU<->APU handshake, i.e. an INPUT at the port's boundary -- the same category as
        // the joypad word CtJoypadPoll takes from the host -- so the host supplies it. See
        // g_ctApuPort3 in move.h for who fills it and from where.
        // CORROBORATION that the mask means "busy", from a second, independent site: C2/CBE0 is
        // `AF 43 21 00` (LDA $00:2143 LONG -- which an absolute-only opcode scan cannot see) followed
        // by `29 0F` AND #$0F and `F0 F8` BEQ back onto itself, i.e. the same latch and the same
        // nibble used as a spin-wait. Two unrelated call sites, one meaning.
        // ⚠ DUPLICATED, like the A88D predicate above: src/ct_vm.c:1531 carries this opcode for the
        // live VM as `if ((vmSoundPort(3) & 0x0F) != 0) return 1;` -- the same test, the same
        // park-on-nonzero, and the same 0 default when no APU is attached. The two cannot share code
        // today because that engine reaches the value through a CtSoundSink callback while this one
        // works on the flat r[] image, which by construction has no cell for a $21xx register. If
        // one of them ever changes, change both.
        h.ip = ((g_ctApuPort3 & 0x0F) == 0) ? ((ip + 1) & 0xFFFF) : ip;
        h.carry = 0; h.ok = 1; break;
      case 0xA1: {
        // C0/5191 -- op 0x97 with the ENTIRE ANIM/FACING LAYER REMOVED. Byte-verified: 51F1's
        // `JSR C0/ABA2` is followed IMMEDIATELY by 51F4's `JSR C0/5B95`, with no F700 lookup and no
        // `STA $1600,X` between them -- so the collision test runs against the STALE facing, the
        // +$30 nudge writes only $01EE, the arrival exit has no `JSR C0/568B`, and no path calls
        // C0/5614. Same three-byte, park-on-op shape otherwise.
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
        if (CtCollisionActorMovable(r, idx)) {            // 51F4 -- against the OLD facing
          ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;   // 51F9-51FE -- and NO F700 write
        }
        r[0x1A01 + idx] = r[0x1000 + idx];                // 5202 / 5205
        r[0x10B00 + idx] = r[0x1000 + idx];               // 5208
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);   // 520C STY $C7 (16-bit)
        CtBuildVelocityPriority(r, rom, idx);             // 520E JSR C0/ACFD
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 5211 LDX $C7 -- no 5614
      }
      case 0x9D: {
        // C0/4F0A -- op 0x92 with BOTH operands read through the scene-var array and the anim layer
        // removed: no `JSR C0/568B` on the drain arm (4F19 is `9E 80 1A BB ...` where 0x92 has
        // `9E 80 1A 20 8B 56`), no $1600 write, and no `JSR C0/5614`.
        if (r[0x1A80 + idx] != 0) {                       // 4F0C / 4F0F BEQ ->4F22
          if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 4F14 / 4F16
          r[0x1A80 + idx] = 0x00;                         // 4F19 STZ $1A80,X -- and NO 568B
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 4F1C TYX / INX x3
        }
        // ⚠ THE HEADING LOAD IS SIXTEEN-BIT (4F2F) BUT THE DURATION LOAD IS EIGHT (4F46): the
        // `SEP #20` at 4F44 sits between them. The heading's `AND #00FF` then discards the high
        // half, so `STA $EE` still zeroes $01EF.
        int ee = r[0x10200 + ((r[0x12001 + ((ip + 1) & 0xFFFF)] * 2) & 0xFFFF)];   // 4F2D / 4F2F
        r[0x01EE] = (uint8_t)ee; r[0x01EF] = 0x00;        // 4F37 STA $EE (16-bit)
        r[0x1A01 + idx] = r[0x10200 + ((r[0x12001 + ((ip + 2) & 0xFFFF)] * 2) & 0xFFFF)];  // 4F46/4F4C
        r[0x10B00 + idx] = 0x80;                          // 4F4F/4F51 -- a LITERAL, like 0x92's
        r[0x1A80 + idx] = (uint8_t)(r[0x1A80 + idx] + 1); // 4F55 INC $1A80,X (8-bit)
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);   // 4F58 STY $C7
        CtBuildVelocityPriority(r, rom, idx);             // 4F5A JSR C0/ACFD -- reads $01EE itself
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4F5D LDX $C7 -- no 5614, no $1600
      }
      case 0x94: case 0x95: case 0x9E: case 0x9F: case 0x98: case 0x99: {
        // C0/5215 / 5410 (0x94/0x95), C0/52C0 / 54F5 (0x9E/0x9F) and C0/534A / 550E (0x98/0x99).
        // The ODD member of each pair resolves its operand through the party-slot table -- three
        // inserted bytes, `B5 97 AA` (LDA $97,X / TAX) -- and everything downstream is shared.
        int legged = (op == 0x98 || op == 0x99);
        int anim   = (op == 0x94 || op == 0x95 || op == 0x98 || op == 0x99);
        int viaSlot = (op == 0x95 || op == 0x9F || op == 0x99);
        ctVmMoveActorOp(r, rom, idx, ip, viaSlot, anim, legged, legged ? 3 : 2, &h);
        break;
      }
      case 0x8F: {
        // C0/5429 -- MOVE TOWARD A PARTY MEMBER UNTIL ADJACENT **OR** UNTIL COMFORTABLY INSIDE THE
        // CAMERA WINDOW. Two bytes. It is the only member with NO $1C81 test and NO snap call at
        // all -- neither `89 01`/C0/305D nor `89 02`/C0/30B3 occurs in 5429-54F4. In their place it
        // runs the same box test op 0x28 uses, on the same halved camera bytes.
        if (r[0x1A01 + idx] != 0) { h.ip = ip; h.carry = 0; h.ok = 1; break; }   // 542B / 5430
        int tgt = ctVmPartySlotTarget(r, r[0x12001 + ((ip + 1) & 0xFFFF)]);      // 543C LDA $97,X
        if (r[0x1100 + tgt] & 0x80) {                     // 543F / 5442 BMI ->54A8
          // ⚠ THE BRANCH ARITHMETIC ALONE GETS THIS WRONG. 5442 is `30 64`, so 5444 + $64 = 54A8 --
          // ONE INSTRUCTION PAST `54A6 LDX $6D`, which looks like it leaves X holding the target
          // from 543E's TAX. It does. IT MAKES NO DIFFERENCE: C0/568B's own first instruction is
          // `568B LDX $6D`, so it reloads SELF regardless of how it was entered, and 54A6 is dead.
          // Measured: passing the target scores 56/60, passing self 60/60.
          // What DOES differ across the siblings on this same test is which calls happen at all --
          // 0x98/0x99's 536E -> 53B6 also clears SELF's $1A80 first, and 0x9E/0x9F's 52D6 -> 531E
          // is a bare TYX that calls nothing and writes nothing.
          moveArriveAnim(r, idx);                         // 54A8 JSR C0/568B -- SELF, via its own LDX
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 54AB TYX / INX INX
        }
        r[0x01F2] = r[0x1801 + tgt];                      // 5444 / 5447
        r[0x01F3] = r[0x1881 + tgt];                      // 5449 / 544C
        // 544E `STX $E9` happens even though this opcode never calls C0/30B3 -- the write is real.
        r[0x01E9] = (uint8_t)tgt; r[0x01EA] = (uint8_t)(tgt >> 8);
        r[0x01F0] = r[0x1801 + idx];                      // 5452 / 5455
        r[0x01F1] = r[0x1881 + idx];                      // 5457 / 545A
        int sy = r[0x1881 + idx], sx2 = r[0x1801 + idx];
        int yAdj = (sy == r[0x01F3]) || (((sy + 1) & 0xFF) == r[0x01F3]) ||
                   (((sy - 1) & 0xFF) == r[0x01F3]);      // 545C-546F
        int xAdj = (sx2 == r[0x01F2]) || (((sx2 + 1) & 0xFF) == r[0x01F2]) ||
                   (((sx2 - 1) & 0xFF) == r[0x01F2]);     // 5471-547A
        if (yAdj && xAdj) {
          moveArriveAnim(r, idx);                         // 54A6 LDX $6D / 54A8 -- SELF this time
          h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
        }
        // NOT adjacent: fall into the camera-window test. These two writes happen on BOTH remaining
        // arms, and on the step arm C0/ACFD later overwrites them.
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
        r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];      // 54C0 / 54C5
        if (CtCollisionActorMovable(r, idx)) {            // 54C8 JSR C0/5B95
          ee = (ee + 0x30) & 0xFF; r[0x01EE] = (uint8_t)ee;          // 54CD-54D2
          r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];               // 54D5 / 54DA
        }
        r[0x1A01 + idx] = r[0x1000 + idx];                // 54DF / 54E2
        r[0x10B00 + idx] = r[0x1000 + idx];               // 54E5
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);     // 54E9 STY $C7
        CtBuildVelocityPriority(r, rom, idx);             // 54EB JSR C0/ACFD
        ctMoveStepAnim5614(r, idx);                       // 54EE JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 54F1 LDX $C7
      }
      case 0x96: case 0x97: {
        // C0/4F61 -- MOVE TO ABSOLUTE TILE, and the last opcode the player's script needed. It is a
        // PARK-ON-OP: it consumes its three bytes ONLY on the visit that declares arrival, and every
        // other visit re-parks the IP on the 0x96 byte so the same opcode re-dispatches. FIVE paths,
        // and only one of them is SEC.
        //
        // C0/5084 (op 0x97) is the SAME ROUTINE with the two operand fetches replaced: where 0x96
        // has `BB E8 BF 01 20 7F 85 F2` (script byte straight to $F2), 0x97 interposes
        // `C2 20 29 FF 00 0A AA E2 20 BF 00 02 7F` -- REP #20 / AND #00FF / ASL A / TAX / SEP #20 /
        // LDA $7F0200,X -- so each operand INDEXES the scene-var array at stride 2. The SEP #20
        // sits BEFORE that load, so it is EIGHT-bit. From 50B9 on the bytes are identical to 0x96's
        // at 4F7A, which is why one body serves both.
        if (r[0x1A01 + idx] != 0) {                       // 4F63 LDA $1A01,X / 4F66 BEQ ->4F6B
          h.ip = ip; h.carry = 0; h.ok = 1; break;        // 4F68 TYX / CLC: mid-step, yield
        }
        int t1 = r[0x12001 + ((ip + 1) & 0xFFFF)];        // 4F6D operand 1 = target TILE X
        int t2 = r[0x12001 + ((ip + 2) & 0xFFFF)];        // 4F74 operand 2 = target TILE Y
        if (op == 0x97) {                                 // 5099/509D and 50AF/50B3
          t1 = r[0x10200 + ((t1 * 2) & 0xFFFF)];
          t2 = r[0x10200 + ((t2 * 2) & 0xFFFF)];
        }
        // 4F71/4F78/4F7F/4F84 stage all four into DP, and they are NOT dead: C0/ABA2 below reads
        // exactly $01F0-$01F3 to build the heading.
        r[0x01F2] = (uint8_t)t1; r[0x01F3] = (uint8_t)t2;
        r[0x01F0] = r[0x1801 + idx]; r[0x01F1] = r[0x1881 + idx];
        if (r[0x1881 + idx] == t2 && r[0x1801 + idx] == t1) {          // 4F86/4F88 then 4F8A/4F8C
          // ARRIVED. $1C81,X bit 0 is the per-object "snap to the tile centre" opt-in (op 0x0E sets
          // it; it defaults to 0), and C0/305D returns SEC only when it actually had to move.
          if ((r[0x1C81 + idx] & 1) && ctArriveSnap305D(r, idx)) {     // 4F90/4F93 BIT #01 / 4F97 JSR
            ctMoveStepAnim5614(r, idx);                   // 4FA5 -- the snap is a step, so re-select
            h.ip = ip; h.carry = 0; h.ok = 1; break;      // 4FA8 TYX / CLC: still parked
          }
          moveArriveAnim(r, idx);                         // 4F9C JSR C0/568B
          h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;      // 4F9F TYX/INX x3 / 4FA3 SEC
        }
        // 4FAB: NOT arrived -> issue a fresh step.
        // The four staged bytes above ARE C0/ABA2's inputs; it subtracts them itself, unsigned.
        int ee = CtMoveDirAngle(r, rom);                  // 4FAB JSR C0/ABA2 -- writes $01EE
        r[0x01EE] = (uint8_t)ee;                          // ABB6/ABC7/... -- $EE is DP, so $01EE
        r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];      // 4FB5 LDA $F700,X / 4FBA STA $1600,X
        if (CtCollisionActorMovable(r, idx)) {            // 4FBD JSR C0/5B95 / 4FC0 BCC ->4FD2
          ee = (ee + 0x30) & 0xFF;                        // 4FC2-4FC5: the +$30 sidestep
          r[0x01EE] = (uint8_t)ee;                        // 4FC7
          r[0x1600 + idx] = rom[0xF700 + (ee & 0xFF)];    // 4FCA/4FCF -- the ROM re-writes the FACING
        }                                                 //   too, which is easy to drop
        r[0x1A01 + idx] = r[0x1000 + idx];                // 4FD4/4FD7 arm the ticket
        r[0x10B00 + idx] = r[0x1000 + idx];               // 4FDA mirror to $7F0B00
        // 4FDE `STY $C7` -- the IP spill that 4FE6 `LDX $C7` reloads, because C0/ACFD clobbers Y.
        // SIXTEEN-bit (the runner leaves the X flag clear), so it lands on $01C7 AND $01C8. The port
        // keeps the IP in a C local and does not NEED the spill, which is why it was omitted -- but
        // the ROM writes those two bytes, and nothing compared them until --sweepops grew to cover
        // this opcode. Measured the moment it did: `$001C7: C=5A RO=00`.
        r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
        CtBuildVelocityPriority(r, rom, idx);               // 4FE0 JSR C0/ACFD (reads $01EE itself)
        ctMoveStepAnim5614(r, idx);                       // 4FE3 JSR C0/5614
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 4FE6 LDX $C7 / 4FE8 CLC -- Y is CLOBBERED
                                                          //   by ACFD, which is why the ROM spills the
                                                          //   IP to $C7 at 4FDE and reloads it here.
      }
      case 0xA0:                                          // MOVE_TO_TILE (C0/5129) -- CtActorMoveStep
        h.ip = CtActorMoveStep(r, rom, idx) ? ((ip + 3) & 0xFFFF) : ip;   // arrived -> IP+3 + continue
        h.carry = (h.ip != ip); h.ok = 1; break;          // arrived -> carry SET; yield/issue -> CLEAR
      case 0xA6:                                           // SET_FACING (C0/5545) -- CtVmFacingFamily, with its nine
      case 0x0F: case 0x17: case 0x1B: case 0x1D:          //   siblings: one tail, C0/554B-5556, which also zeroes
      case 0x1E: case 0x1F: case 0x25: case 0x26: case 0xA7: {   // the anim timer $1601,X
        int ni, tg; CtVmFacingFamily(r, r + 0x12001, ip, idx, &ni, &tg);
        h.ip = ni; h.carry = 0; h.ok = 1; break; }
      case 0x81: {
        // C0/4476 -- THE ALWAYS-NPC OBJECT LOADER. One straight line, no branch and no call
        // anywhere in C0/4476..4556, so a single exit at ip+2 with carry SET.
        //
        // It does not CALL the descriptor walk -- it FALLS THROUGH into it at C0/448D, which is the
        // same body op 0x80's three in-party arms reach by `JSR C0/448D`. That body is already
        // ported as ctPcLoad (record base $E4:F000, unbiased -- op 0x82's copy at C0/45A3 reads
        // $E4:F024, i.e. entry id+7). So all this opcode adds is its four-store prologue.
        //
        // ⚠ $1100,X = 3 IS THE WHOLE POINT OF THE OPCODE. op 0x82 writes 4 and op 0x83 writes 5 at
        // the equivalent spot; 3 is the always-NPC marker. And unlike op 0x80, nothing here later
        // overwrites $1600,X -- ctPcLoad leaves it at 1, where op 0x80's arms replace it with
        // ($0104 & 3). Do not copy op 0x80's tail behaviour in.
        r[0x1100 + idx] = 0x03;                             // 4476 LDA #03 / 4478 LDX $6D / 447A
        r[0x1B01 + idx] = 0x01;                             // 447D LDA #01 / 447F -- BEFORE the id
        int id = r[0x12001 + ((ip + 1) & 0xFFFF)];          // 4482 INY / 4483 TYX / 4484
        r[0x1101 + idx] = (uint8_t)id;                      // 4488 LDX $6D / 448A -- the RAW operand
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
        // THE $C1 ITEM + GOLD FAMILY. The body lives in CtVmItemFamily (below ctVmC1Gate) because
        // the LIVE event VM (ct_vm.c) executes it too, and a second transcription of it there would
        // be exactly the split this tree already has too much of (vm_two_implementations.md). This
        // call is the one --sweepops micro-executes against the ROM, so the shared body is graded
        // here and INHERITED by the live VM rather than re-derived for it.
        // Carry is SET on every exit of all seven, so no arm yields.
        int ni = ip;
        CtVmItemFamily(r, r + 0x12001, ip, &ni);
        h.ip = ni & 0xFFFF; h.carry = 1; h.ok = 1; break;
      }
      case 0xCF: case 0xD0: case 0xD1: case 0xD2: case 0xD7:
      case 0xD3: case 0xD4:
      case 0xF8: case 0xF9: case 0xFA:
        // The body lives in CtVmPartyGateFamily (just above this function) because ct_vm.c runs it too.
        // THIS call is what --sweepops micro-executes, so the shared body is graded here and inherited
        // by the live VM. It sets ip, carry and ok itself -- the carry is not uniform across the ten.
        CtVmPartyGateFamily(r, rom, romLen, r + 0x12001, ip, &h);
        break;
      case 0x4E: {
        // C0/6846 -- COPY AN INLINE SCRIPT BLOB TO WRAM. Recorded as blocked on `JSR C0/E2AA` and
        // `JSR C0/6038`; neither appears anywhere in the handler. It is two arms that differ in ONE
        // BYTE -- `6872 MVN $7F,$7F` against `6897 MVN $7E,$7F` -- selected by a destination-bank
        // operand tested `CMP #7E`, so ONLY the literal $7E takes the second arm.
        //
        // Layout: `4E <dst16> <dstbank> <len16> <data...>`. The data is INLINE, so the source is the
        // script itself at ip+6 and the opcode's LENGTH IS ITS PAYLOAD -- the returned IP is
        // ip+6+count, recovered by the ROM as `TXA / SEC / SBC #2001` after the MVN has walked X.
        //
        // ⚠ THE COUNT IS `len - 3` AND MVN MOVES C+1, so it moves len-2 bytes. Three separate
        // `DEC A` at 685F-6864, SIXTEEN bits wide. A len below 3 therefore WRAPS the count and moves
        // most of a 64 KB bank; the port reproduces that, and the seed grid deliberately does NOT
        // exercise it -- it would flood the compared image and drown every other fact in the row.
        int dst = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 684A
        int bank = r[0x12001 + ((ip + 3) & 0xFFFF)];                  // 6853 / 6857 CMP #7E
        int len4E = r[0x12001 + ((ip + 4) & 0xFFFF)] | (r[0x12001 + ((ip + 5) & 0xFFFF)] << 8); // 685E
        int cnt = (len4E - 3) & 0xFFFF;                               // 6862-6864 DEC A x3, 16-bit
        r[0x01C1] = (uint8_t)cnt; r[0x01C2] = (uint8_t)(cnt >> 8);    // 6865 STA $C1 (16-bit)
        int src = (0x2001 + ip + 6) & 0xFFFF;                         // 6869 TXA / CLC / ADC #2001
        int dbase = (bank == 0x7E) ? 0x00000 : 0x10000;               // the ONLY difference: MVN's
        for (int k = 0; k <= cnt; k++)                                //   destination bank byte
          r[dbase + ((dst + k) & 0xFFFF)] = r[0x10000 + ((src + k) & 0xFFFF)];
        h.ip = (ip + 6 + cnt + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 6876-687B / 687E SEC
      }
      case 0xC8: {
        // C0/3757 -- REQUEST / ACK / PARK, the same three-arm shape as op 0x04's call-and-wait. It
        // was recorded as "portable but never satisfied", and that is a statement about the LIVE
        // engine's scheduling, not about this handler: all three arms are pure bank $C0, and each
        // has an honest IP and carry.
        //   $0117 bit 6 set  -- a request of ours is already outstanding: PARK on the opcode.
        //   $0118 bit 6 set  -- the service answered: clear it and step over the operand.
        //   neither          -- ISSUE: publish the operand and latch $0117, then PARK.
        // ⚠ THE OUTSTANDING TEST COMES FIRST, so once $0117 bit 6 is latched the ack bit is never
        // even looked at -- the service must CLEAR $0117 before setting $0118, or the 3774 arm is
        // unreachable. That ordering is the whole reason the opcode looks like it hangs.
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
        // C0/3B1B (0xDD) and C0/3B45 (0xDE) -- STAGE A LOCATION CHANGE. 0xDE is literally 0xDD with
        // `LDA #01 / STA $1E` in front of it and a `BRA` into the same body.
        //
        // They are the RETURNING members of the 0xDD-0xE1 family. Their siblings 0xDF and 0xE1 stage
        // the identical fields into $0112/$0114/$0116 and then spin on `LDA $4210 / BPL` before
        // `3B91 LDX #06FF / TXS` and `BRL ->0000` -- a hard stack reset that never comes back. Those
        // two are NOT ported and no case is opened for them, because a case that can only trap would
        // claim their 87 sites in the coverage count while grading nothing.
        //
        // FIVE bytes: a 16-bit destination, a 16-bit coordinate pair, and a third field the ROM
        // builds out of the FIRST operand's high byte alone, in two passes over the same byte.
        if (op == 0xDE) r[0x011E] = 0x01;                             // 3B45 LDA #01 / 3B47 STA $1E
        int w1 = r[0x12001 + ((ip + 1) & 0xFFFF)] | (r[0x12001 + ((ip + 2) & 0xFFFF)] << 8);  // 3B1F
        r[0x01D9] = (uint8_t)w1; r[0x01DA] = (uint8_t)(w1 >> 8);      // 3B23 STA $D9 (16-bit)
        int loc = w1 & 0x01FF;                                        // 3B25 AND #01FF -- NINE bits
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
        // C0/46DF -- LOAD A FIELD ENEMY / BIG SPRITE. 1471 static sites, 1.28%, the single largest
        // remaining opcode. Body C0/46DF..4866, 392 bytes, exclusively 0x83's. Length 3, ONE RTS
        // (47FD), carry ALWAYS SET.
        //
        // ⚠ ITS FIRST HALF *IS* THE OBJECT LOADER, which is why nothing here is re-derived:
        // C0/4557 is literally shared (JSR at 46E1 with A = 5), and C0/46E4-476A is C0/4595-461B
        // with only the STRIDE and the five table offsets changed -- 9 differing bytes in 147.
        // The recorded note "0x83 is NOT CtObjLoad with state=5" is right about the TAIL and only
        // the tail: op 0x83 never calls ctLoaderPartySlot and never does `INC $73`.
        // The other recorded note, "slot-alloc inline", is REFUTED -- see 47A9 below.
        //
        // The one out-of-bank transfer on any reachable path is `4825 JSL $C3:0002`, the same LZ
        // op 0x82 uses and the port already has.
        r[0x1100 + idx] = 0x05;                                  // 4559 -- state 5 = enemy
        int ip831 = (ip + 1) & 0xFFFF, ip832 = (ip + 2) & 0xFFFF;
        int id83 = r[0x12001 + ip831];                           // 455E
        r[0x1101 + idx] = (uint8_t)id83;                         // 4564
        r[0x1280 + idx] = r[0x0171]; r[0x1281 + idx] = r[0x0172]; // 456C (16-bit) -- staging address
        r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 4571
        r[0x1200 + idx] = 0x7F;                                  // 4579 -- the graphics BANK
        r[0x1600 + idx] = 0x01;                                  // 457E
        r[0x1680 + idx] = 0x00; r[0x1681 + idx] = 0x00;          // 4583 / 4588
        r[0x01C7] = (uint8_t)ip832; r[0x01C8] = (uint8_t)(ip832 >> 8);  // 46E9 STY $C7, 16-bit --
        r[0x1B01 + idx] = 0x01;                                  // 46EF    the ONLY surviving IP
        int rec83 = ctLoaderWalk(r, rom, romLen, idx, id83, 0x24F600, 10);   // 46E4-476A
        int e383 = (romLen > rec83) ? rom[rec83] : 0;            // 476F $E4F600,X
        r[0x01E3] = (uint8_t)e383;                               // 4773 STA $E3
        // ---- the three graphics arms. 4777 `LDA $1101,X / CMP #F8 / BCC` -- note the walk above
        // has ALREADY run for every id, so an early-out on the id would be wrong.
        if (id83 >= 0xF8) {
          // ---- 4845 ROM-DIRECT: the sheet is not staged in $7F at all, it is read straight out
          // of a raw ROM bank, so 4861 OVERWRITES the prologue's $1200,X = $7F. 8 of 1471 sites.
          r[0x01D9] = (uint8_t)e383; r[0x01DA] = 0x00;           // 484C (16-bit, high byte zeroed)
          int a83 = ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3, 1);          // 4858
          r[0x1280 + idx] = (uint8_t)a83; r[0x1281 + idx] = (uint8_t)(a83 >> 8);
          r[0x1200 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xAF, e383 * 3 + 2, 0);  // 4861
        } else {
          int k83; for (k83 = 0; k83 < 16; k83++) if (r[0x0920 + k83] == (uint8_t)e383) break;
          if (k83 < 16) {
            // ---- 4781 DEDUP HIT: share the owner's staging address. C0/5C90 writes NOTHING on a
            // hit. ⚠ AND UNLIKE op 0x82's hit arm (4639-4663) there is no party-slot store here.
            int own83 = r[0x0930 + k83];                         // 5CC2 LDA $0930,X
            r[0x1280 + idx] = r[0x1280 + own83];                 // 4794 (16-bit)
            r[0x1281 + idx] = r[0x1281 + own83];
          } else {
            // ---- 47FE DEDUP MISS: claim a slot, decompress, advance the cursor.
            for (k83 = 0; k83 < 16; k83++) if (r[0x0920 + k83] == 0xFF) break;   // 5CA5 CMP #FF
            if (k83 >= 16) { h.ip = ip; h.carry = 0; h.ok = 0; break; }  // 5CB0 LDX #4010 / BRL
            r[0x0920 + k83] = (uint8_t)e383;                     // 5CB6-5CBD    C0/2E1E: SEI, force
            r[0x0930 + k83] = (uint8_t)idx;                      //   blank, then `2E65 BRA ->2E65`
            r[0x01D9] = (uint8_t)e383; r[0x01DA] = 0x00;         // 4805 -- the SECOND $D9 spill
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
            // 4833 `CMP #8000 / BPL`: 16-bit, so the halt fires when the cursor is BELOW $8000,
            // i.e. the staging heap wrapped past $FFFF. Same diagnostic infinite loop.
            if (cur83 < 0x8000) { h.ip = ip; h.carry = 0; h.ok = 0; break; }  // 4838 / BRL C0/2E1E
          }
        }
        // ---- COMMON TAIL, C0/4799-47FD. All three arms converge here.
        int op283 = r[0x12001 + ip832];                          // 479B, read back via the $C7 spill
        r[0x01E1] = (uint8_t)op283;                              // 479F
        int n83 = ((op283 & 0x7F) + 1) & 0xFF;                   // 47A1/47A3 -- INC A is EIGHT-BIT,
        // ⚠ NO ALLOCATION HAPPENS HERE. 47A9 claims $0B88+n unconditionally: no free-scan, no bit-7
        // test, no loop -- the bytes between the INC A and the store are `E2 10 AA A5 6D 9D 88 0B`,
        // with no CMP and no branch. "slot-alloc inline" was wrong.
        r[0x0B88 + n83] = (uint8_t)idx;                          // 47A9 STA $0B88,X
        int v83 = ((n83 & 0xFE) << 4) & 0xFFFF;                  // 47AF-47B5 AND #00FE / ASL x4
        r[0x01D9] = (uint8_t)v83; r[0x01DA] = (uint8_t)(v83 >> 8);            // 47B6 STA $D9, 16-bit
        int dv83 = (v83 + ((n83 & 1) << 3)) & 0xFFFF;            // 47B9-47C0 AND #0001 / ASL x3 / ADC
        // ⚠ 47C2 `STX $D9` RUNS UNDER SEP #10, so X is EIGHT BITS and only $01D9 is overwritten --
        // $01DA keeps the high half of the 16-bit store above. Modelling this as a 16-bit spill
        // would silently zero a byte the engine reads.
        r[0x01D9] = (uint8_t)n83;                                             // 47C2 STX $D9, 8-bit
        r[0x0D00 + idx] = (uint8_t)dv83; r[0x0D01 + idx] = (uint8_t)(dv83 >> 8);   // 47C6, 16-bit
        int tv83 = (n83 << 5) & 0xFFFF;                          // 47CF AND #00FF / 47D2 ASL x5 --
        r[0x1700 + idx] = (uint8_t)tv83;                         //   the AND is what makes the
        r[0x1701 + idx] = (uint8_t)(tv83 >> 8);                  //   accumulator's hidden B half
        if ((r[0x1201 + idx] & 0x03) == 0x03) {                  // 47DC-47E3    irrelevant here
          // ---- 47E5: the BIG-SPRITE VRAM upload arm. `STX $AE` (8-bit X, so $01AF is NOT touched)
          // then `JSR C0/E12A`. E12A is ~1030 bytes -- it decompresses 96 tiles to $7F:3800-43FF,
          // DMAs $0C00 to VRAM word $0400, and lays a 24x8 OAM template at $7F:4BC2 -- and it is
          // NOT ported yet, so this TRAPS rather than returning a state that is missing all of it.
          // Measured reach: 10 of the 1471 sites, all with record byte+4 = $23 (ids $6F/$93/$D7).
          h.ip = ip; h.carry = 0; h.ok = 0; break;
        }
        if (op283 & 0x80) r[0x1100 + idx] = 0x06;                // 47EC-47F4 -- 73 of 1471 sites
        h.ip = (ip + 3) & 0xFFFF; h.carry = 1; h.ok = 1; break;  // 47F9 LDX $C7 / INX / 47FC SEC
      }
      case 0xE8: case 0xEA: case 0xEB: case 0xEC: {
        // THE SOUND-MAILBOX FAMILY -- C0/3C11, C0/3C2F, C0/3C4C, C0/3C98. 4063 static sites, which
        // was the largest single unported block in this runner and the #1 opcode by site share
        // (0xE8 alone is 2.63%). The HANDLERS are ported and graded; what is not modelled is the
        // DRIVER, deliberately and permanently -- the SPC700 is RUN, not modelled (see below).
        //
        // All four stage bytes into the $1E00 mailbox and `JSL $C7:0004`. They are ONE mechanism
        // with four payloads, so they share one case. Every one of them exits CLC (yield).
        //
        // ⚠ WHAT THIS PORT DOES AND DOES NOT CLAIM. The handlers themselves -- the staging, the
        // $01C7 spill, the length and the carry -- are byte-exact and graded. The DRIVER is not
        // modelled here at all: see ctVmSoundPost. With $1E05 bit 7 clear the ROM's own service
        // short-circuits to its epilogue and the whole observable WRAM effect is these staging
        // writes plus `$1E00 = 0`, which is exactly what is implemented.
        //
        // ⚠ AND THE COMMAND BYTE IS UNGRADABLE BY A WRAM SWEEP, no matter how it is seeded. The
        // epilogue's `STZ $00` destroys $1E00 before the handler returns, so $18, $10, $81/$82/$83
        // and 0xEC's entire script-driven command byte are all written and then wiped. Measured:
        // writing $99 instead of $18 still scores a clean 60/60. Those values are correct and are
        // exercised for real by src/ct_vm.c's live path (--vmdiff, --audiotest, --songload); they
        // are simply invisible HERE, and the check's note says so rather than implying coverage.
        //
        // ⚠ $1E03 IS STICKY. Only 0xEB writes it; 0xE8, 0xEA and 0xEC leave whatever the previous
        // command left there, and $C7:061C really does send that stale byte to $2143. Poisoning
        // $1E03 in the grid is the only way to tell "leave alone" from "write 0".
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
          } else if (op == 0xEA) {                      // C0/3C2F -- play SONG
            spill = ip + 1; len = 2;                    // 3C41 STX $C7
            r[0x29AE] = (uint8_t)a1;                    // 3C35 STA $7E29AE -- the shadow copy, and
            r[0x1E01] = (uint8_t)a1;                    //   it is written even when the driver goes
            r[0x1E00] = 0x10;                           //   on to REJECT the id ($C7:0AE9 = $53)
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          } else if (op == 0xEB) {                      // C0/3C4C -- THREE round-trips back to back
            spill = ip + 2; len = 3;                    // 3C78 STX $C7, spilled ONCE before the 1st
            r[0x1E01] = (uint8_t)a1; r[0x1E02] = (uint8_t)a2;
            r[0x1E03] = 0xFF; r[0x1E00] = 0x81;         // the ONLY writer of $1E03 in the family
            r[0x01C7] = (uint8_t)spill; r[0x01C8] = (uint8_t)(spill >> 8);
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
            r[0x1E01] = 0x00; r[0x1E02] = 0xFF; r[0x1E00] = 0x82;
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
            r[0x1E01] = 0x00; r[0x1E02] = 0xFF; r[0x1E00] = 0x83;
            if (!ctVmSoundPost(r)) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
          } else {                                      // 0xEC, C0/3C98 -- the RAW escape
            // ⚠ FOUR BYTES, AND NO TRAILING INX. The other three do `LDX $C7 / INX`; this one
            // does three INX BEFORE `3CB2 STX $C7`, so the spill already holds ip+4 and 3CB6
            // reloads it unchanged. Getting that wrong costs a byte on 178 sites.
            spill = ip + 4; len = 4;
            r[0x1E00] = (uint8_t)a1;                    // the COMMAND itself is script data
            r[0x1E01] = (uint8_t)a2; r[0x1E02] = (uint8_t)a3;   // $1E03 deliberately untouched
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
        // C0/3546 and C0/354F -- PERPETUAL wrappers around the whole of op 0x94 / op 0x95:
        //   3546 STY $E3 / JSR C0/5215 / LDX $E3 / CLC / RTS
        //   354F STY $E3 / JSR C0/5410 / LDX $E3 /       RTS
        // `STY $E3` is SIXTEEN-BIT (the runner leaves the X flag clear), so it spills the ip to
        // $01E3 AND $01E4 -- a write the old paraphrase made nowhere. `LDX $E3` then DISCARDS
        // whatever ip the inner handler returned, which is what makes these perpetual; the inner
        // handler's WRITES still stand.
        // ⚠ THEY DIFFER IN ONE BYTE OF CONTROL FLOW: 0xB5 has a `CLC` before its RTS and 0xB6 does
        // not, so 0xB5 always yields while 0xB6 PASSES THE INNER CARRY THROUGH.
        r[0x01E3] = (uint8_t)ip; r[0x01E4] = (uint8_t)(ip >> 8);      // 3546 / 354F STY $E3
        ctVmMoveActorOp(r, rom, idx, ip, op == 0xB6, 1, 0, 2, &h);    // 3548 / 3551 JSR
        h.ip = ip;                                                    // 354B / 3554 LDX $E3
        if (op == 0xB5) h.carry = 0;                                  // 354D CLC -- 0xB6 has none
        h.ok = 1; break;
      }
      case 0xAF:                                           // C0/2FFD = `JSR C0/3002 / INX / RTS`
      case 0xB0: {                                         // C0/3002 directly -- ONE shared body.
        // The IP pairing mirrors B1/B2 exactly: 3002 returns with the IP untouched (0xB0 is the
        // PERPETUAL player-control op a PC's script sits on), and 2FFD's `INX` advances it by one.
        //   3004  $0138 non-zero -> CLC with NO apply at all
        //   3012  $1100,X == 0 -> 3040: arm $1A01 = 1, then JSR C0/9E29
        //   3015  == 1 -> C0/A26B ; 3018  == 2 -> C0/A2CE  -- BOTH UNPORTED, so they trap rather
        //         than half-run: arming the ticket without the handler would move the object with a
        //         stale velocity, which is worse than reporting the gap.
        //   301A  >= 3 -> nothing at all
        int st = r[0x1100 + idx];
        if (r[0x0138] == 0) {
          if (st == 0) {
            r[0x1A01 + idx] = 1;                           // 3040
            // C0/9E29's FIRST instruction is `JSR C0/9E84` -- the FACING selector runs before the
            // velocity copy, and it is the player's only writer of $1600. The romLen argument is
            // 0x10000 = "bank $C0 is present", which is all this call needs ($9FF7's dispatch table
            // and $FE00's blink table both live there) and exactly what every other rom[] read in
            // this file already assumes.
            CtPlayerAnimSelect(r, rom, 0x10000, idx);      // 9E29
            ct9E29Apply(r, idx);                           // 9E2C onward
            // 304A-3054: C0/5B8D's carry is just `$01EB bit7 clear`, then C0/3154 preempts
            // the touched object's script. This is what redirects an actor's IP from OUTSIDE
            // its own script -- the mechanism --portrun's idx-16 f254 line needs.
            if (!(r[0x01EB] & 0x80)) CtVmTouchRedirect(r);
          }
          else if (st == 1 || st == 2) { h.ip = ip; h.carry = 0; h.ok = 0; break; }
        }
        h.ip = (op == 0xAF) ? ((ip + 1) & 0xFFFF) : ip;    // 2FFD's INX vs 3002's bare RTS
        h.carry = 0; h.ok = 1; break;                      // both exits are CLC = yield
      }
      default:                                             // not yet ported -> trap
        h.ip = ip; h.carry = 0; h.ok = 0; break;
    }
  *out = h;
  return h.ok ? -1 : op;
}

int g_ctVmTraceIdx = -1, g_ctVmTraceFrame = -1;

// THE SPC700's OUTPUT MAILBOX PORT 3, supplied by the host. See op 0xEE (C0/3CC5) above for the
// derivation and for why this is an input rather than something r[] could hold. Default 0 means
// "low nibble clear", which is the arm that lets the script advance -- so a host that never sets it
// gets a no-op opcode rather than a script wedged forever on a byte it can never pass.
uint8_t g_ctApuPort3 = 0;
uint8_t g_ctApuOut[4] = { 0, 0, 0, 0 };                     // ops 0x48 / 0x49 on $2140-$2143; see move.h
uint8_t g_ctApuPort1 = 0;                                   // op 0xED (C0/3CBD); see move.h

int CtFieldRunner(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // C0/5A93 `LDA $68 / STA $67`, and BOTH are DP-relative with DP = $0100 -- so the reload lives at
  // $0168 and the live counter at $0167. This read `r[0x0068]`, i.e. $7E0068, a byte no field code
  // owns; it was almost always 0, so the `<= 0 -> 5` fallback below supplied the answer and the real
  // budget was never consulted. That fallback is itself an invention: when the ROM's $68 is 0 the
  // first `DEC $67` wraps to $FF and `BNE` loops, giving 256 opcodes, not 5. Same class as the
  // $006D/$016D latch bug -- a DP-relative address ported as if DP were 0.
  int budget = r[0x0168];                                 // 5A93 LDA $68
  r[0x0167] = (uint8_t)budget;                            // 5A95 STA $67 -- the counter is REAL memory
  int ip = r[0x1180 + idx] | (r[0x1181 + idx] << 8);      // 16-bit script IP
  // the $6D latch now lives in CtFieldRunnerOne, at the ROM's own address
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

// =================================================================================================
// C0/E952 - C0/EA62 -- the FOUR-SLOT sprite-assembly ARENA, and C0/CAD9 -- the drain's per-object
// visit. See disasm/notes/field_tick.md.
//
// THE ARENA. $0BC0..$0BC3 is a BYTE-strided owner table: bit7 SET = free (every release writes
// $80), bit7 clear = the owning object's $6D (= obj*2, so at most $7E and never ambiguous with the
// free marker). A grant writes the base address into $0D80,X as a WORD: $3800 + slot*$200, an
// offset into the $7F staging bank, i.e. 512 bytes = 16 SNES 4bpp tiles per slot.
//
// THE POOL IS NOT FRAME-SCOPED. It is tempting to clear it each frame -- live monitoring of
// party3_field shows $0BC0 = 80 80 80 80 at every single frame boundary -- but that is the common
// case, not an invariant, and clearing it would be invention. Two ROM mechanisms leave a partly
// staged object holding its slots into the next frame: C0/CA76 refuses to run at all while the
// VRAM upload queue is non-empty (CA76 `LDA $09A0 / BEQ ->CA7C / RTS`), and its raster budget
// (CA99/CA9B) can cut between the passes of a class-1/2 object -- in which case C0/B701's release
// gate sees $1B00,X below the class arity and returns SEC without freeing.
// =================================================================================================

int CtArenaAlloc(uint8_t* r, int idx, int n) {
  // C0/E952 (n=1) / C0/E97A (n=2) / C0/E9AA (n=3). The scan bound is 4/3/2 respectively -- i.e.
  // 4-(n-1) -- and a multi-slot claim needs the slots CONSECUTIVE (E984 / E9B4 / E9B9 each BPL back
  // to the INX on the first occupied follower).
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
  // count $1B00,X reaches the class arity. Note the ROM frees slot k..k+n-1 UNCONDITIONALLY once it
  // finds k -- it never re-checks that the FOLLOWERS are owned by $6D. Ported verbatim.
  int bound = 4 - (n - 1);                                // E9F0 #04 / EA0D #03 / EA2D #02
  for (int k = 0; k < bound; k++) {
    if (r[0x0BC0 + k] != (uint8_t)idx) continue;          // E9EB / EA08 / EA28 CMP $6D
    for (int j = 0; j < n; j++) r[0x0BC0 + k + j] = 0x80; // E9F9 / EA16-EA19 / EA36-EA3C
    return;
  }
  // not found -> silent no-op (E9F4 / EA11 / EA31)
}

void CtArenaFreeRun(uint8_t* r, int idx) {
  // C0/EA42 -- the CLASS-AGNOSTIC release: free every CONSECUTIVE slot owned by $6D, 1 to 4 of them.
  // This is what the camera-window FALLING edge (C0/A918) and the scene-entry sweep (C0/B117) use,
  // and it is NOT CtArenaFree(r, idx, 1): releasing a class-2 object through the 1-slot path would
  // leak its other two slots. EA42 also brackets no SEP/REP -- it is entered with X already 8-bit.
  int k = 0;
  for (; k < 4; k++) if (r[0x0BC0 + k] == (uint8_t)idx) break;   // EA44-EA4E
  if (k == 4) return;                                            // EA50 RTS
  for (;;) {
    r[0x0BC0 + k] = 0x80;                                        // EA51-EA53
    if (++k == 4) return;                                        // EA56-EA59
    if (r[0x0BC0 + k] != (uint8_t)idx) return;                   // EA5B-EA60
  }
}

// C0/CBDC -- class 0 (4 OBJ pieces). This ports the FRAME half: resolve, guard, allocate, commit.
//
// CLASS 0 NEVER RETURNS SEC. Its only three RTS are CC03 (reached when CB3A returned carry clear),
// CC09 and CDC7, and all three are carry-clear; a raw byte scan of CBDC..CDC7 finds no `38 60`. So
// the drain always pops a class-0 object after exactly ONE visit. The first derivation of this
// asserted the opposite -- that the drain "keeps re-entering the object exactly while CB3A/E952
// keep producing work" -- and three independent verifiers refuted it. The resumable SEC protocol is
// real, but it belongs to classes 1 and 2, whose passes ride the $1B00,X counter.
//
// PORTED TOO, and this comment used to say otherwise -- CC17-CDBE, the TILE half: the 16-tile blit
// through the $2180 WRAM port (ctAnimBlit -- C0/E687 verbatim, or C0/E534 with every byte reversed
// through the $C0:FD00 bit-reversal table, which is the tile word's bit14 H-FLIP baked into VRAM),
// the 7-table VRAM upload queue at $0940-$09A0 (ctAnimAppend) and the 4-entry OBJ template at
// $7F4802 + $1700,X (ctAnimTemplate; $1700,X = slot << 5, five ASLs at C0/47D2-47D6 -- not << 3).
// --sweepcad9 grades all of it byte-exact over $7F:3800-3FFF, $7F:4802+ and $0940-$09B3: 165888/
// 165888, `0 class1/2-tilehalf`, and mis-scaling the blit's tile offset by one bit costs 8704 cases.
// A 24-bit read the way the handlers' `LDA [$D3],Y` sees it. HiROM: banks $C0-$FF are ROM at file
// offset (bank-$C0)<<16; $7E/$7F are WRAM; banks $00-$3F (and their $80-$BF mirrors) expose WRAM
// $7E:0000-1FFF below $2000. Anything else this port does not model and returns 0 -- measured, the
// live sprite tables hold banks $D2/$D3/$D4, and only a fresh-boot oracle leaves them 0.
static int romRead24(const uint8_t* r, const uint8_t* rom, int romLen, int bank, int addr) {
  bank &= 0xFF; addr &= 0xFFFF;
  if (bank >= 0xC0) { int off = ((bank - 0xC0) << 16) | addr; return off < romLen ? rom[off] : 0; }
  if (bank == 0x7E || bank == 0x7F) return r[((bank - 0x7E) << 16) | addr];
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && addr < 0x2000) return r[addr];
  return 0;
}

// The same read as a flat 24-bit address, for the two copiers' `[$CD],Y` / `$D20000,X` forms, both
// of which can CARRY out of their base bank.
static int romRead24Flat(const uint8_t* r, const uint8_t* rom, int romLen, int a24) {
  return romRead24(r, rom, romLen, (a24 >> 16) & 0xFF, a24 & 0xFFFF);
}

// C0/CC3F-CC76 -- the PIXEL COPY, plus both copiers it dispatches to. `tiles` per class.
//
// Per tile the handler reads a WORD out of the frame record. Bits 0-10 are the tile index and the
// source is $CF:($CD + index*32); bit14 selects the copier. Bits 11-13 and 15 are masked off by
// both (`AND #07FF`) and are provably ignored here.
//
// THE TWO COPIERS DO NOT WRITE THE SAME WAY, and that is the whole reason $D0 exists alongside the
// WRAM port. C0/E534 (bit14 SET) pushes 32 bytes through the $2180 port, which auto-increments;
// C0/E687 (bit14 clear) sets DB = $7F and stores DIRECTLY to $0000,Y with Y = $D0, then RESYNCS the
// port with `TYA / CLC / ADC #0020 / STA $2181` (E72E-E735) so a following E534 continues from the
// right place. The caller keeps $D0 in step by adding $20 per tile (CC55), so both land tile t at
// $7F:($0D80,X + t*$20) -- which is what makes this portable as one loop.
//
// C0/E534 IS THE HORIZONTAL FLIP: every byte goes through $C0:FD00, an exact bit-reversal table
// (ROM[$C0FD00+i] == bitreverse8(i) for all 256 i, verified exhaustively). A 4bpp SNES tile stores
// one pixel row per byte per bitplane and the byte ORDER is preserved, so reversing each byte
// mirrors each row. H-flip is baked into VRAM, which is why the OBJ attribute carries no flip bits.
//
// E687'S BANK DISPATCH IS NOT A PASS-THROUGH. Only $7F (E8C2), $D2 (E7BC), $D3 (E83F) and $D4
// (E739) read their own bank; ANY other value of $CF falls through to C0/E6B6, which reads bank
// **$D5 hardcoded**. E534 has no such dispatch -- its `[$CD],Y` honours $CF for every bank -- so the
// two copiers genuinely read different data when $CF is outside that set.
// One staging run: `n` tiles taken from tile-word byte-offset `wOff` of the frame record, written at
// the arena base + `dOff`. A pass is one or two of these -- the class-2 `$0D00,X == $68` variants
// interleave two runs where the others do one.
// `wIdx` = this run reads the arena base $0D80,X with the WORD $6D/$6E rather than the byte.
// Every two-segment leg does a `REP #30` between its runs (D58C, and the equivalents in D68B /
// D738 / DA69), so its SECOND run is word-indexed while the first is not. Traced per leg:
//   byte: CC1B CC94 CF3B CFB4 D2A7 D30F D38C D563 D625 D68F D73C D7E7 D864 DA6B DB23
//   WORD: D5C4 D6F4 D7A1 DACE
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
      // `LDA [$CD],Y` is a 24-BIT add, so it can carry out of $CF into the next bank.
      int base = ((gfxBank & 0xFF) << 16) | gfxAddr;
      for (int i = 0; i < 32; i++)
        r[0x10000 + ((d + i) & 0xFFFF)] =
            rom[0xFD00 + romRead24Flat(r, rom, romLen, base + off + i)];   // E546 LDA $FD00,X
    } else {                                                // C0/E687
      int sb = (gfxBank == 0x7F || gfxBank == 0xD2 || gfxBank == 0xD3 || gfxBank == 0xD4)
             ? gfxBank : 0xD5;                              // E699-E6B3, else E6B6's hardcoded $D5
      int x = (gfxAddr + off) & 0xFFFF;                     // E691 CLC / ADC $CD -- a 16-BIT add
      for (int i = 0; i < 32; i++)
        r[0x10000 + ((d + i) & 0xFFFF)] =
            (uint8_t)romRead24Flat(r, rom, romLen, ((sb & 0xFF) << 16) + x + i);
    }
  }
}

// C0/CC17-CDBE -- the TILE HALF. Shared by all three classes, which differ
// only in the piece count, the frame-record stride and the transfer length. Four separable jobs:
//
//   CC17-CC3B  resolve the frame RECORD pointer. $D3/$D4 = $1380,X + frame*stride computed on the
//              SNES HARDWARE MULTIPLIER ($4202 = frame, $4203 = stride, product from $4216), and
//              $D5 = the record bank $1300,X. This is where the per-class stride $28/$50/$78 is
//              actually applied.
//   CC3F-CC76  the 16-tile PIXEL COPY -- ctAnimBlit above.
//   CC78-CCB4  the VRAM upload QUEUE append: SEVEN parallel 16-bit tables ($0940 srcA, $0950 dstA,
//              $0960 srcB, $0970 dstB, $0980 lenA, $0990 lenB, $09A0 used), cursor $79 advancing by
//              2, and a 16-bit STZ terminating the NEXT slot. `INC $09A0,X` and that STZ are both
//              16-BIT (M is wide here) -- but C0/CA76's prologue reads $09A0 8-BIT, which is why
//              $09A1 accumulates stale values.
//   CCB6-CDBE  the OBJ TEMPLATE at $7F4802 + $1700,X, `pieces` entries of 8 bytes. Only five of the
//              eight are written here: +2/+3 = the record's X displacement SIGN-EXTENDED, +4 = its
//              Y displacement, +6 = ($0D00,Y & $FF) + e*2, +7 = the attribute. Bytes +0/+1 (the
//              final on-screen X) and +5 are left alone.
// The frame RECORD pointer: $D3/$D4 = $1380,X + frame*stride on the SNES hardware multiplier, bank
// $1300,X. `stride` is $28 / $50 / $78 by class.
static int ctAnimRec(const uint8_t* r, int idx, int stride) {
  return ((r[0x1380 + idx] | (r[0x1381 + idx] << 8)) + r[0x0F01 + idx] * stride) & 0xFFFF;
}

// CC78-CCB4 -- the upload-queue append, done ONCE PER SPRITE by the pass that returns CLC. Verified
// by scanning every leg for `STA $0950,X`: it appears only at CCA8 (class 0), CFA8 and D380 (class
// 1's mode-3 and pass-2 legs), D858 and DB17 (class 2's two phase-2 legs). Passes that return SEC
// stage tiles and nothing else.
//
// dstB and srcB DO NOT SHARE AN OFFSET. Class 0 is +$100/+$100, class 1 is +$100/+$200, class 2's
// non-$68 leg is +$300/+$500 and its $68 leg +$100/+$100 -- and the two lengths differ too ($500 and
// $100, swapped between the variants). Extrapolating class 0's symmetry would be wrong four ways.
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
  int used = ((r[0x09A0 + q] | (r[0x09A1 + q] << 8)) + 1) & 0xFFFF;        // CCAC INC, 16-BIT
  r[0x09A0 + q] = (uint8_t)used; r[0x09A1 + q] = (uint8_t)(used >> 8);
  q = (q + 2) & 0xFF;                                                     // CCAF/CCB0 INX INX
  r[0x09A0 + q] = 0; r[0x09A1 + q] = 0;                                   // CCB1 STZ, also 16-BIT
  r[0x0179] = (uint8_t)q;                                                 // CCB4 STX $79
  #undef CT_QW
}

// CCB6-CDBE -- the OBJ TEMPLATE at $7F4802 + $1700,X, also once per sprite. `pieces` entries of 8
// bytes, of which five are written: +2/+3 the record's X displacement SIGN-EXTENDED, +4 its Y
// displacement, +6 ($0D00,Y & $FF) + e*2, +7 the attribute. Bytes +0/+1 (the final on-screen X) and
// +5 are left alone. `recOff` is where the piece data starts, i.e. past the tile-word array:
// $20 for class 0's 16 words, $40 for class 1's 32, $60 for class 2's 48.
//
// THE PRIORITY RULE IS NOT SHARED, and extrapolating class 0's would be wrong on every piece.
// Class 0 tests each piece's OWN +4 byte against #E8 at runtime (CD4F/CD6B/...), four CMPs. Classes
// 1 and 2 contain **zero** `CMP #E8`: they split by piece INDEX, pieces 0-3 taking $0C00,Y and every
// piece from 4 up taking $0C01,Y (D4C6/D4DB for class 1's eight; DA23/DA38/DA4D for class 2's
// twelve). Counted from the ROM: class 0 has 24 `STA $7F48xx,X` for 4 pieces because the E8 test
// duplicates the +7 store, while classes 1/2 have exactly 5 per piece.
static void ctAnimTemplate(uint8_t* r, const uint8_t* rom, int romLen, int idx,
                           int rec, int recBank, int pieces, int recOff, int e8Test, int jumpAt) {
  int base = r[0x1700 + idx] | (r[0x1701 + idx] << 8);   // CCB8 8-bit LDX / CCBA 16-bit LDA / CCBF TAX
  int y16 = r[0x016D] | (r[0x016E] << 8);                // CD2C LDY $6D, Y 16-bit (REP #10 at CCBD)
  int tile = r[0x0D00 + y16] & 0xFF;                     // CD2E, an 8-BIT load
  int attr = (r[0x0F81 + y16] | r[0x0D01 + y16]) & 0xFF; // CD47-CD4D -> $D9
  for (int e = 0; e < pieces; e++) {
    int p = (0x4802 + base + e * 8) & 0xFFFF;
    int xd = romRead24(r, rom, romLen, recBank, rec + recOff + e * 2);     // CCC5 LDA [$D3],Y
    int yd = romRead24(r, rom, romLen, recBank, rec + recOff + 1 + e * 2); // CCD8
    r[0x10000 + p]     = (uint8_t)xd;                                     // +2
    r[0x10000 + p + 1] = (uint8_t)((xd & 0x80) ? 0xFF : 0x00);            // +3, CCCB BPL sign-extend
    r[0x10000 + p + 2] = (uint8_t)yd;                                     // +4
    // +6. The tile number advances by 2 per piece -- EXCEPT for ONE extra $10, inserted at a
    // point that DIFFERS BETWEEN THE TWO CLASS-2 GEOMETRIES. There is exactly one `CLC / ADC #10`
    // in each class-2 template: DA02 (non-$68) sits before the piece-8 store at $7F4846, DCA9
    // ($68) before the piece-4 store at $7F4826. Classes 0 and 1 have NONE -- their INC A counts
    // are exactly (pieces-1)*2 + 1 with no ADC at all. `jumpAt` < 0 means no jump.
    int tn = tile + e * 2 + ((jumpAt >= 0 && e >= jumpAt) ? 0x10 : 0);
    r[0x10000 + p + 4] = (uint8_t)(tn & 0xFF);                            // CD35 / DA00-DA03
    int pr = e8Test ? ((r[0x10000 + p + 2] >= 0xE8) ? r[0x0C01 + y16] : r[0x0C00 + y16])
                    : ((e < 4) ? r[0x0C00 + y16] : r[0x0C01 + y16]);
    r[0x10000 + p + 5] = (uint8_t)(attr | pr);                            // +7
  }
}

static int ctAnimClass0(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // CBF2-CBFE. CtFieldFrameLookup already folds in the caller's `$1780,X == 3 -> $1301,X` static-pose
  // short-circuit (see above, and --sweepcb3a deliberately excludes cfg 3 for that reason), so this
  // must NOT repeat it -- doing so would double-apply the branch.
  int fb = CtFieldFrameLookup(r, rom, romLen, idx);
  if (fb < 0) return 0;                                   // CC01 BCS not taken -> CC03 RTS, C clear
  if (fb == r[0x0F01 + idx]) return 0;                    // CC04 CMP / CC07 BNE -> CC09 CLC/RTS
  if (!CtArenaAlloc(r, idx, 1)) return 0;                 // CC0D JSR E952 / CC10 BCC -> CC09:
                                                          //   pool full -> the frame is DROPPED and
                                                          //   $0F01 keeps its old value
  r[0x0F01 + idx] = (uint8_t)fb;                          // CC12-CC14: THE commit
  { static const CtBlitSeg SEG = { 0x000, 0x00, 16, 0 };  // CC17-CDBE, the whole tile half
    int rec = ctAnimRec(r, idx, 0x28), bk = r[0x1300 + idx];
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &SEG);
    ctAnimAppend(r, idx, 0x100, 0x100, 0x0100, 0x0100);
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 4, 0x20, 1, -1); }
  // CDBF `LDX $6D` runs with X 16-BIT -- REP #10 at CCBD, and the only SEP #10 in CCB0..CDC7 is at
  // CDC4, i.e. AFTER this. So the INC is indexed by the WORD $6D/$6E, not by the byte. $7E1B00 +
  // 0xFFFF = $7F1AFF, still inside the caller's 0x20000 WRAM image, so no masking is needed.
  int x16 = r[0x016D] | (r[0x016E] << 8);                 // DP = $0100
  r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);       // CDC1 INC $1B00,X (staged-pass count)
  return 0;                                               // CDC6 CLC / CDC7 RTS
}

// C0/CEF5 -- class 1 (8 OBJ pieces, an $50-byte frame record, TWO consecutive arena slots). This
// ports the FRAME half, exactly as class 0's does; the tile half is left out for the same reason.
//
// THE SHAPE, and it is not class 0 with bigger numbers. CEF5's dispatch (CF0B-CF1E) is three-way:
//
//   $1780,X == 3            -> CF21, a SINGLE visit that stages the WHOLE two-slot run (32 tiles,
//                             CF6B `LDA #0020`), bumps $1B00,X by TWO and returns CLC. It never
//                             reads $1B00,X at all.
//   ($1B00,X & $7F) == 0    -> D28A, PASS 1: resolve, guard, allocate TWO slots (C0/E97A), commit
//                             $0F01,X, stage 16 tiles, INC $1B00,X, and return **SEC** -- the ROM
//                             asking C0/CA76 to keep this object at the FIFO head and call again.
//   otherwise               -> D30D, PASS 2: no lookup, no guard, no allocation and NO commit. It
//                             only stages the other 16 tiles at $0D80,X + $200, INCs, and CLCs.
//
// So the whole frame decision lives in pass 1 and the mode-3 path; pass 2 is pure staging. That is
// why porting the frame half still makes class 1 behave correctly at the arena/queue level.
//
// THE THREE `INC $1B00,X` DO NOT AGREE ON INDEX WIDTH, which is a ROM fact and not a tidy one:
// D308 (pass 1) runs after `D304 SEP #30`, so it is indexed by the BYTE $6D; D4F0 (pass 2) and
// D11A/D11D (mode 3) run while X is still 16-bit (`D3B4 REP #10` / `CFDE REP #10`, with their
// `SEP #10` at D4F3 / D120 -- i.e. AFTER), so those are indexed by the WORD $6D/$6E.
//
// PORTED, and this comment used to say otherwise: the tile halves (32 tiles at CF5F-CF98, 16 at
// D2C9-D302, 16 at D33C-D370) are the CtBlitSeg descriptors below, their upload-queue appends are
// ctAnimAppend and the 8-entry OBJ template at $7F4802 + $1700,X is ctAnimTemplate. --sweepcad9
// reaches every one of them through the $1B00,X pass selector and reports `0 class1/2-tilehalf`.
static int ctAnimClass1(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  int mode3 = (r[0x1780 + idx] == 3);                     // CF0B-CF12 CMP #03
  int x16 = r[0x016D] | (r[0x016E] << 8);                 // DP = $0100
  int rec = ctAnimRec(r, idx, 0x50), bk = r[0x1300 + idx];   // class-1 record stride $50
  if (!mode3 && (r[0x1B00 + idx] & 0x7F) != 0) {          // CF14-CF1B -> D30D, PASS 2
    static const CtBlitSeg S2 = { 0x200, 0x20, 16, 0 };   // D30F: base + $200, tile words 16..31
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S2);
    ctAnimAppend(r, idx, 0x100, 0x200, 0x0200, 0x0200);   // D380: dstB +$100 but srcB +$200
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 8, 0x40, 0, -1);   // D3B9 LDY #0040, 8 pieces
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D4F0, WORD-indexed
    return 0;                                             // D4F5 CLC / D4F6 RTS
  }
  // CF21 (mode 3) and D28A (pass 1) share the resolve/guard/allocate/commit. CtFieldFrameLookup
  // already folds the `$1780 == 3 -> $1301,X` case, which is exactly what CF21 reads directly.
  int fb = CtFieldFrameLookup(r, rom, romLen, idx);       // CF21 / D28A JSR C0/CB3A
  if (fb < 0) return 0;                                   // D28D BCS not taken -> D28F RTS
  if (fb == r[0x0F01 + idx]) return 0;                    // CF24-CF2A / D290-D296
  if (!CtArenaAlloc(r, idx, 2)) return 0;                 // CF2D / D299 JSR C0/E97A -- TWO slots;
                                                          //   CF30 / D29C BCC -> drop the frame
  r[0x0F01 + idx] = (uint8_t)fb;                          // CF32-CF34 / D29E-D2A0
  rec = ctAnimRec(r, idx, 0x50);   // CC22/D2A3: the multiplier reads $0F01 AFTER the commit, so
                                   // the record must be re-resolved here. Pass 2 has no commit
                                   // and correctly keeps the value resolved at entry.
  if (mode3) {
    static const CtBlitSeg S = { 0x000, 0x00, 32, 0 };    // CF6B #0020: BOTH slots in one visit
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S);
    ctAnimAppend(r, idx, 0x100, 0x200, 0x0200, 0x0200);   // CFA8
    ctAnimTemplate(r, rom, romLen, idx, rec, bk, 8, 0x40, 0, -1);   // CFE3 LDY #0040
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D11A  -- TWO increments in ONE visit,
    r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);     // D11D     so B701 sees the arity at once
    return 0;                                             // D122 CLC / D123 RTS
  }
  { static const CtBlitSeg S1 = { 0x000, 0x00, 16, 0 };   // D2A7: base, tile words 0..15.
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &S1); }      //   Pass 1 does NOT append or build.
  r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);       // D308, BYTE-indexed (D304 SEP #30)
  return 1;                                               // D30B SEC -- "call me again"
}

// C0/D4F7 -- class 2 (12 OBJ pieces, a $78-byte frame record, THREE consecutive arena slots). Frame
// half only, like classes 0 and 1.
//
// D50D-D514 IS A TRAP AND IT IS CANONICAL. `LDA $1780,X / CMP #03 / BNE ->D515 / RTS` -- and `CMP`
// with A == 3 leaves the carry SET, so a class-2 object in anim mode 3 returns **SEC having done
// nothing at all**. C0/CA76 reads SEC as "keep this object at the FIFO head and call me again", so
// it spins on that object until the raster deadline expires. Verified from raw bytes
// (`BD 80 17 C9 03 D0 01 60`). Ported verbatim; see the note in CtAnimDrainPass about what that
// costs a port with no clock.
//
// SIX LEGS, TWO OF WHICH MATTER. `$1B00,X & $7F` picks the pass (D515-D51E) and `$0D00,X == $68`
// picks one of two staging geometries (D520/D52D/D539), giving 2 x 3 bodies:
//     phase 0 -> D546 (non-$68) / D608 ($68)      SEC, D603 / D686
//     phase 1 -> D68B            / D738            SEC, D733 / D7E0
//     phase 2 -> D7E5            / DA69            CLC, DA62 / DD21
// The two phase-0 bodies are **byte-identical** through the whole frame half
// (`[25 ROM bytes at 0xD546: python tools/dis.py 0xD546 25] ...`), so the $68
// split does not touch the frame decision -- it only changes the staging offset ($0100/$0200 for
// phase 1, $0400/$0300 for phase 2). And `JSR CB3A` appears only at D546/D608 while `JSR E9AA`
// appears only at D555/D617, so the lookup, the guard, the allocation and the commit ALL live in
// phase 0. Phases 1 and 2 are pure staging with no early exit (verified: their bodies contain no
// RTS at all, and the stray `60` bytes are operands of `STA $0960,X` and `LDY #$0060`).
//
// THE SAME INDEX-WIDTH ASYMMETRY AS CLASS 1, and now it looks like a rule rather than an accident:
// every SEC (intermediate) tail runs after a `SEP #30` and is BYTE-indexed (D603/D686/D733/D7E0,
// after D5FF/D682/D72F/D7DC); every CLC (final) tail runs with X still 16-bit and is WORD-indexed
// (DA62/DD21, whose `SEP #10` comes after the INC).
static int ctAnimClass2(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  if (r[0x1780 + idx] == 3) return 1;                     // D510-D514: SEC, and NO work done
  int n = r[0x1B00 + idx] & 0x7F;                         // D515-D518
  int x16 = r[0x016D] | (r[0x016E] << 8);
  // D520/D52D/D539: the LOW byte of $0D00,X picks one of two staging geometries. They differ only
  // in where each run lands and which tile words it takes -- an interleave, not a different job.
  int v68 = ((r[0x0D00 + idx] & 0xFF) == 0x68);
  int rec = ctAnimRec(r, idx, 0x78), bk = r[0x1300 + idx];   // class-2 record stride $78
  //                                     dOff  wOff  n
  static const CtBlitSeg P0A[2] = { { 0x000, 0x00, 8, 0 }, { 0x200, 0x10, 8, 1 } };  // D546 / D5C4 (WORD)
  static const CtBlitSeg P0B[1] = { { 0x000, 0x00, 16, 0 } };                        // D608
  static const CtBlitSeg P1A[2] = { { 0x100, 0x20, 8, 0 }, { 0x300, 0x30, 8, 1 } };  // D68F / D6F4 (WORD)
  static const CtBlitSeg P1B[2] = { { 0x200, 0x20, 8, 0 }, { 0x400, 0x30, 8, 1 } };  // D73C / D7A1 (WORD)
  static const CtBlitSeg P2A[1] = { { 0x400, 0x40, 16, 0 } };                        // D7E7
  static const CtBlitSeg P2B[2] = { { 0x300, 0x40, 8, 0 }, { 0x500, 0x50, 8, 1 } };  // DA6B / DACE (WORD)
  if (n == 0) {                                           // D51A BEQ -> D52D -> D546 / D608
    int fb = CtFieldFrameLookup(r, rom, romLen, idx);     // D546 / D608 JSR C0/CB3A
    if (fb < 0) return 0;                                 // D549 BCS not taken -> D54B RTS
    if (fb == r[0x0F01 + idx]) return 0;                  // D54C-D552 the equality guard
    if (!CtArenaAlloc(r, idx, 3)) return 0;               // D555 / D617 JSR C0/E9AA -- THREE slots
    r[0x0F01 + idx] = (uint8_t)fb;                        // D55A-D55C / D61C-D61E
    rec = ctAnimRec(r, idx, 0x78);                        // the commit moved $0F01, so re-resolve
    if (v68) ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0B[0]);
    else { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0A[0]);
           ctAnimBlit(r, rom, romLen, idx, rec, bk, &P0A[1]); }
    r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);     // D603 / D686, BYTE-indexed
    return 1;                                             // SEC
  }
  if (n == 1) {                                           // D51C CMP #01 / D51E BEQ -> D539
    const CtBlitSeg* p1 = v68 ? P1B : P1A;
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &p1[0]);
    ctAnimBlit(r, rom, romLen, idx, rec, bk, &p1[1]);
    r[0x1B00 + idx] = (uint8_t)(r[0x1B00 + idx] + 1);     // D733 / D7E0, BYTE-indexed
    return 1;                                             // SEC
  }
  if (v68) { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2B[0]);
             ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2B[1]);
             ctAnimAppend(r, idx, 0x100, 0x100, 0x0100, 0x0500); }   // DB07
  else     { ctAnimBlit(r, rom, romLen, idx, rec, bk, &P2A[0]);
             ctAnimAppend(r, idx, 0x300, 0x500, 0x0500, 0x0100); }   // D848 -- note the SWAP
  ctAnimTemplate(r, rom, romLen, idx, rec, bk, 12, 0x60, 0, v68 ? 4 : 8);  // D893/DB52 LDY #0060;
                                                          // DCA9 jumps at piece 4, DA02 at 8
  r[0x1B00 + x16] = (uint8_t)(r[0x1B00 + x16] + 1);       // DA62 / DD21, WORD-indexed
  return 0;                                               // CLC -- the object is finally popped
}

int CtAnimDrainVisit(uint8_t* r, const uint8_t* rom, int romLen, int idx) {
  // C0/CAD9. Four gate tests, EVERY one of them returning CLC -- which is mandatory, because CLC is
  // what makes C0/CA76 POP the object. A reject that returned SEC would jam it at the FIFO head.
  if (r[0x1100 + idx] & 0x80) return 0;                   // CADE BPL: bit7 SET = destroyed/inactive
  int a = r[0x1A81 + idx];
  if (a == 0 || (a & 0x80)) return 0;                     // CAE5 BEQ / CAE7 BMI (any $01-$7F passes)
  if (r[0x0F00 + idx] == 0) return 0;                     // CAEC: outside the camera window
  switch (r[0x1201 + idx] & 3) {                          // CAEE-CAFD: the sprite's PIECE COUNT
    case 0:  return ctAnimClass0(r, rom, romLen, idx);    // CAF3 BEQ / CB01 BRL C0/CBDC -- 4 pieces
    case 1:  return ctAnimClass1(r, rom, romLen, idx);    // CAF9 BRA / CB04 BRL C0/CEF5 -- 8 pieces
    case 2:  return ctAnimClass2(r, rom, romLen, idx);    // CAFD BEQ / CB07 BRL C0/D4F7 -- 12 pieces
    default: return 0;                                    // CAFF CLC/RTS -- class 3 is never drawn
                                                          // by this pass (C0/E12A handles it).
  }
}

// =================================================================================================
// C0/B701 -- the RELEASE, and C0/CA76 -- the FIFO DRAIN. Together with the enqueue (C0/C98A) and the
// visit (C0/CAD9) these close the arena's lifecycle, which is a two-point loop inside ONE frame:
//
//   C0/00D3 JSR C0/A810   the movement loop -> C0/C98A per object = the ENQUEUE
//   C0/00D6 JSR C0/CA76   the DRAIN         -> C0/CAD9 per pop    = the E952 GRANT (class 0's only)
//   C0/00DB BRL C0/B271   the DRAW pass     -> B2AF/B2BB JSR C0/B309 -> B309 JSR C0/B701 = the FREE
//
// C0/B701's three `JSR E9E2 / E9FF / EA1F` are the ONLY callers of the 1/2/3-slot frees in the ROM.
// =================================================================================================

// C0/B788 (reposition) and C0/B8CA (instantiate) -- class 0's two OAM EMITTERS. They are the same
// routine except that the instantiate one copies the STAGED record ($7F:4802 + $1700,X) into the
// LIVE one ($7F:4BC0 + $1700,X) as it goes; the reposition one re-derives from what is already live.
// That copy-out is exactly why C0/B701 may free the arena slot at that instant (see above).
//
// THE RECORD IS NOW FULLY ACCOUNTED FOR. The template (C0/CCB6-CDBE) writes +2/+3 (X displacement,
// sign-extended), +4 (Y displacement), +6 (tile) and +7 (attribute); these emitters compute +0 (the
// final on-screen X, low 8 bits) and +1 (the final on-screen Y). +5 is written by neither.
//
// THE X HALF walks the pieces BACKWARDS from the last, and that is not stylistic: each piece folds
// bit 8 of its X into an accumulator as `A = ($E5 << 2) | bit8`, so going backwards puts piece 0 in
// bits 0-1 and the last piece in the high bits -- the SNES OAM HIGH-TABLE layout. Only the X bit is
// accumulated; every SIZE bit comes from the single `ORA #$AA` at B7D0, i.e. all four pieces are
// 16x16. On the final iteration `CPX $D9 / BEQ` fires BEFORE the store, so the finished byte only
// ever reaches $4F00,X, never $E5.
//
// EVERY per-object read here uses the WORD $6D/$6E: C0/00D9's `REP #10` leaves X 16-bit for the
// whole draw pass, and B788 never narrows it.
//
// $C6 IS THE SCREEN Y'S BIT 8, AND THE EMITTER WRITES IT ITSELF. `B796 STA $C5` runs with M
// SIXTEEN-BIT (REP #20 at B78D), so it stores the WORD (screen Y & $01FF) across $C5 AND $C6 --
// $C5 gets the low byte, $C6 gets bit 8. Elsewhere $C6 really is the camera window's bottom edge
// (C0/A8BF `LDA $1D10 / CLC / ADC #05 / STA $C6`, siblings at A962/B151; the fourth writer E94C is
// a bulk `STA $C0..$C7` scratch clear), and that is what an earlier reading of this file claimed
// was being tested here -- WRONG: the emitter clobbers it two instructions in. So the two tests at
// B7D9 and B836 are one Y-RANGE CLASSIFIER on the object's own screen Y:
//     Y >= $100        -> path A (B7DD / B922)
//     $80 <= Y <= $FF  -> path B (B83A / B9A5)
//     Y <= $7F         -> path C (B882 / B9F7)
// B8CA has the SAME three (its middle path simply BRAs into the tail of the third at BA47 instead
// of carrying its own epilogue, which is why a raw `AB 60` scan finds only two exits there).
// THE X AND Y HALVES DO NOT COVER THE SAME NUMBER OF RECORDS, and the Y count VARIES BY PATH.
// The X half is a LOOP over `pieces` -- there is exactly one `STA $4BC0,X` site in the whole
// routine. The Y half is UNROLLED, and counting its `+1` stores per path gives, identically in
// both emitters: path A ($4BC1/$4BC9/$4BD1/$4BD9/$4BE1) = FIVE records, paths B and C = FOUR.
// So the `Y >= $100` case writes one record more than the template ever filled; that spare
// record's +4 is whatever was left there, and it gets $C5 added and clamped like any other.
// THE Y CLAMP IS NOT ONE RULE WITH A MODE -- it is SEVEN, hand-written per class and per path,
// read from the bytes at each path entry. Class 0's three do NOT generalise, and class 1's path C
// differs between its own two emitters. `d` is the record's Y displacement, `sy` the screen Y.
static int ctOamClamp(int cls, int mode, int isRep, int d, int sy) {
  int t = d + (sy & 0xFF), c = t > 0xFF, y = t & 0xFF;      // CLC / ADC $C5
  if (cls == 0) {
    if (mode == 0) return (c && y >= 0xE0) ? y : 0xE0;      // B7DD/B922: BCC->E0, >=E0 keep
    if (mode == 1) return (!c || y < 0xE0) ? y : 0xE0;      // B83A/B9A5: BCC keep, <E0 keep
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;       // B882/B9F7: BPL keep, >=E0 keep
  }
  if (cls == 1) {
    if (mode == 0) return (c && y >= 0xE0) ? y : 0xE0;      // BC50/BF1C -- same as class 0's A
    if (mode == 1) return (y < 0xE0) ? y : 0xE0;            // BB00/BD80 -- NO carry test at all
    // mode 2, and the two emitters DIFFER: the reposition one alone pre-tests the sign of the
    // DISPLACEMENT (BB7F `BMI ->BB88`), so a non-negative d that does not carry is kept outright
    // (BB84 `BCC ->BB93`) without ever reaching the BPL/#E0 pair. BE46 has no such pre-test.
    if (isRep && !(d & 0x80) && !c) return y;
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;       // BB8B / BE49
  }
  if (cls == 3) {   // C0/C73A's three paths -- the tenth, eleventh and twelfth hand-written rules,
                    // read off the bytes at each path entry and NOT assumed from class 2's. They
                    // happen to come out identical to class 2's three; that is a measurement, and
                    // each arm cites the address it was read from.
    if (mode == 0) return (!c && (y & 0x80) == 0) ? y : 0xE0; // C826 BCS ->#E0 / C828 BPL keep
    if (mode == 1) return y;                                  // C7E2-C7E8: NO CLAMP WHATSOEVER
    return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;         // C805 BPL keep / C807 CMP #E0 BCS keep
  }
  if (mode == 0) return (!c && (y & 0x80) == 0) ? y : 0xE0; // C209/C5C1: BCS->E0, BPL keep
  if (mode == 1) return y;                                  // C0CD/C3A5: NO CLAMP WHATSOEVER
  return ((y & 0x80) == 0 || y >= 0xE0) ? y : 0xE0;         // C13B/C483
}

static void ctOamEmit(uint8_t* r, int cls, int pieces, int instantiate) {
  int w = r[0x016D] | (r[0x016E] << 8);                       // $6D/$6E, X 16-bit throughout
  int sy = (r[0x0A80 + w] | (r[0x0A81 + w] << 8)) & 0x01FF;   // B78F $C5 = screen Y, 9 bits
  int sx = r[0x0A00 + w] | (r[0x0A01 + w] << 8);              // B798 $C3 = screen X
  int base = r[0x1700 + w] | (r[0x1701 + w] << 8);            // B7A0 $D9 = the record base
  // ...and those three are REAL STORES into the engine's direct page (D = $0100), not just locals --
  // all three SIXTEEN-BIT, REP #20 being in force from B78D. They were missing, and $01D9 is the one
  // that shows: it is the byte a live frame ends on, because A810 runs before the draw pass, so the
  // last emit's record base is what sits there at the frame boundary (measured 0x0040, writer C0:B7A6
  // = the store at B7A4). $01C5's high byte is also the `mode` selector the Y half reads back as
  // `$C6 != 0`, so writing it is what makes that read a memory read rather than a private local.
  r[0x01C5] = (uint8_t)sy;   r[0x01C6] = (uint8_t)(sy >> 8);  // B796 STA $C5
  r[0x01C3] = (uint8_t)sx;   r[0x01C4] = (uint8_t)(sx >> 8);  // B79C STA $C3
  r[0x01E5] = 0;             r[0x01E6] = 0;                   // B79E STZ $E5 (16-bit: clears the pair)
  r[0x01D9] = (uint8_t)base; r[0x01DA] = (uint8_t)(base >> 8);// B7A4 STA $D9
  #define CT_LIVE(o)  r[0x10000 + ((0x4BC0 + base + (o)) & 0xFFFF)]
  #define CT_STAGE(o) r[0x10000 + ((0x4802 + base + (o)) & 0xFFFF)]
  // The X half runs in GROUPS OF FOUR, each walking its own four pieces backwards and ending in
  // its own `ORA #$AA` and one high-table store. The destinations are NOT contiguous: class 2's
  // third byte goes to $4B40 (C0BA / C310-family), not $4F02.
  static const int HIDST[3][3] = { { 0x4F00, 0, 0 }, { 0x4F00, 0x4F01, 0 },
                                   { 0x4F00, 0x4F01, 0x4B40 } };
  for (int g = 0; g * 4 < pieces; g++) {
  // The accumulator lives in the direct page: B7B9 `STA $E6` (this piece's X bit 8), B7BB-B7BF `A = ($E5 << 2) |
  // $E6`, and B7C5 `STA $E5` for every piece BUT the group's last (`CPX $D9 / BEQ` comes first). So a group leaves
  // $E5 = the bits of its first three pieces and $E6 = the last piece's bit -- the routine-level shadow measured the
  // port leaving $01E5 at 00 where the cartridge had 11 (C0/B701, the attract, docs/SHADOW_PROPOSAL.md).
  int hi = 0, hiBeforeLast = 0, bit = 0;
  // From the second group on the loop's end is a direct-page word too: BAB6 `STZ $E5`, then BABA-BAC0 `LDA $D9 / CLC /
  // ADC #0020 / STA $E7` (16-bit; the third group's C087 adds #0040), the `CPX $E7` the group's last piece meets.
  if (g) { int eb = (base + 0x20 * g) & 0xFFFF; r[0x01E7] = (uint8_t)eb; r[0x01E8] = (uint8_t)(eb >> 8); }
  for (int e = g * 4 + 3; e >= g * 4; e--) {                  // B7A6 starts at the LAST piece
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
  // The spare record is CLASS 0's alone, and only on path A: counting `+1` stores per path gives
  // 5/4/4 for class 0 but 8/8/8 for class 1 and 12/12/12 for class 2.
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

// =================================================================================================
// C0/C73A -- CLASS 3's emitter, and the whole of class 3's release. B701 reaches it through
// `B712 BNE ->B785 / B785 BRL C73A`, a TAIL CALL, so C73A's own `RTS` at C7FA/C81F/C989 carries
// straight back to B309's `BCC` draw gate.
//
// IT IS NOT A MEMBER OF THE REPOSITION/INSTANTIATE FAMILY. There is exactly ONE class-3 emitter:
// C0/C6E7 reads `$4BC2,X` and never `$4802,X`, so class 3 always re-derives from the LIVE record
// the way a reposition emitter does; C73A tests `$1B00,X` nowhere, calls no arena free and never
// stamps `$1B00,X = 0x80`. That is why B701's `T = (class & 3) + 1` law stops at class 2 -- class 3
// simply is not staged through the arena's release at all.
//
// ⚠ CLASS 3 IS SIX GROUPS OF FOUR PIECES -- 24, not `(cls+1)*4 = 16`. The X half calls C0/C6E7 six
// times, stepping X by $20 (four 8-byte records) between calls (C767/C77B/C78F/C7A3/C7B7), and its
// six high-table bytes land at $4F00 / $4F01 / $4B40 / $4B41 / $4F80 / $4F81 -- the same six, in the
// same order, that the draw arm C0/B5CB pushes, and B5AF advances its main cursor by $60 on all
// three priority arms (B619 / B6A1 / B6F8). The Y half's counter is `LDA #18` = 24 on every path.
//
// C0/C6E7 IS THE GROUP, and it walks its four pieces FORWARD, not backwards like B788's loop: it
// stashes pieces 0/1/2's bit 8 of X in $E5/$E6/$E7 and keeps piece 3's in A, then folds
// `((b3<<2 | b2)<<2 | b1)<<2 | b0` and `ORA #$AA` (C728-C737). The BYTE is the same OAM high-table
// layout the backwards accumulator builds -- piece 0 in bits 0-1 -- but the direct-page bytes it
// leaves behind are different, so this is written the way the ROM writes it rather than folded
// into ctOamEmit.
//
// THE CARRY, which this port used to refuse to name. It is DATA-DEPENDENT, not underivable:
//   * path A (C820) is 24 UNROLLED records with no X advance, so the flag at C989 `RTS` is the carry
//     out of the LAST record's `ADC $C5` (C97D) -- `LDA #E0`, `STA` and `PLB` all leave it alone;
//   * paths B (C7DE) and C (C7FB) fall out of their loop epilogue, so the flag is the carry out of
//     the final 16-bit `ADC #0008` on X (C7EF / C814) -- set only when the record base is within
//     eight bytes of wrapping bank $7F.
// The old `return 0` was a measurement that held only while `$1700` happened to be zero; the `-1`
// that replaced it refused to say either. Both are now retired.
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
  // C7C5-C7DD: X is reloaded from $1700,X and the Y-RANGE CLASSIFIER runs on the object's own
  // screen Y, exactly as the other three emitters' does -- `$C6 != 0` (>= $100) picks path A,
  // then `$C5` bit 7 picks B over C.
  int mode = ((sy >> 8) & 1) ? 0 : ((sy & 0x80) ? 1 : 2);     // C7D0 LDA $C6 / C7D7 LDA $C5 BMI
  for (int e = 0; e < 24; e++) {                              // 24 on EVERY path (C820 unrolled, #18)
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
  // C0/B701. Called only from C0/B309 (`JSR B701 / B30C BCC ->B30F / B30E RTS`), so THE CARRY IS THE
  // DRAW GATE: SEC = this object emits no OAM this frame, CLC = fall through and draw it.
  //
  // The three class arms at B716 / B737 / B75E are the SAME instruction sequence -- verified against
  // raw bytes, they differ only in the threshold, the two emitter addresses and which free they
  // call. Writing them as one law rather than three copies:
  //
  //     T = (class & 3) + 1                 == the arena ARITY in slots (1 / 2 / 3)
  //     n = $1B00,X                         == the staged-pass counter
  //     if ((n & 0x7F) < T) { bit7 set -> reposition-emit, CLC ; else -> SEC }
  //     else                { instantiate-emit, free T slots, $1B00,X = 0x80, CLC }
  //
  // bit7 means "already released": the reposition emitters (B788/BA65/BFF2) re-derive the on-screen
  // OAM X/Y from templates ALREADY in the live buffer, while the instantiate emitters
  // (B8CA/BCDC/C2BF) first COPY the staged template out of $7F:4802 + $1700,X into the live buffer
  // $7F:4BC0 + $1700,X. That copy-out is exactly why the slot may be freed at that instant.
  //
  // ALL SEVEN EMITTERS ARE NOW PORTED -- class 0's pair, class 1's and class 2's (ctOamEmit above)
  // and class 3's C0/C73A (ctOamEmitClass3 above). All seven touch ZERO of $0BC0..$0BC3, $0D80,
  // $1B00, $0F01 or $0F00 in any addressing mode and call neither the arena nor the frame lookup,
  // which is why their absence never perturbed the lifecycle while they were missing.
  int T = (r[0x1201 + idx] & 3) + 1;                       // B703-B714 (class 3 falls out below)
  // B712 BNE ->B785 / B785 BRL C0/C73A -- a TAIL CALL, so C73A's own RTS carries back to B309.
  // It frees nothing, tests $1B00,X nowhere and never stamps 0x80: class 3 is outside the arena's
  // release law entirely, which is exactly why the `T` arity above has no class-3 arm. Its carry
  // is data-dependent -- the last `ADC $C5` on path A, the last `ADC #0008` on B and C -- and
  // ctOamEmitClass3 computes it from the same inputs the ROM does.
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

// =================================================================================================
// C0/B271 -- THE DRAW PASS, ported for the ARENA LIFECYCLE rather than for pixels.
//
// Its per-object body C0/B309 opens `JSR C0/B701 / B30C BCC ->B30F / B30E RTS`, and C0/B701 is the
// ONLY thing in the engine that releases the four-slot sprite-assembly arena and stamps
// $1B00,X = 0x80. So a host that runs the movement legs and the drain but NOT this pass exhausts the
// pool: measured, --portrun collapsed to 156/1592 before B701 was ported, and the reason it survives
// there now is that it re-seeds from the oracle every frame. A live tick has no such reset.
//
// PORTED HERE: the per-group cursor init (B271-B2A3), the bucket walk high-to-low (B2A5-B2C2), the
// B701 gate, stage 2's CURSOR BOOKKEEPING (B341 / B397 / B3BB -- one high-table slot and 0x10 main
// bytes per drawn object, the group chosen by `$0F80,X & 0x0C`: 0 -> $0181/$01DB, bit2 set ->
// $0185/$01DD, else $0189/$01DF), and the three hide-the-stale-tail loops (B2C4-B306).
//
// NOT PORTED: the OAM BYTES. Stage 2 copies them through the $2180 WRAM port out of the $7F:4Bxx and
// $7F:4F00 records; src/oam.c's CtBuildFieldOAM is this port's model of that copy and is graded on its
// own (--checkoam), so emitting them again here would be a second, ungraded emitter of the same bytes.
// The buffer stays UNMODELLED rather than half-written -- an unwritten byte claims nothing, whereas a
// half-written one is a promise broken every frame.
//
// `unresolved` (may be NULL) counts objects whose gate C0/B701 could not decide. With C0/C73A ported
// (ctOamEmitClass3) every class now returns a derived carry, so this counter should stay at ZERO; it
// is kept rather than deleted because it is the thing that would report a NEW undecidable class
// instead of letting one be guessed in either direction.
int gCtDrawSkipped;   // objects the B701 gate REFUSED (SEC) on the last pass -- see --sweepdraw
// How many times the draw walk hit its non-convergence guard, CUMULATIVE over the run. It is reported
// by Belthasar's worldtest so the guard cannot absorb a defect in silence -- which it did once already:
// the guard turned the $0E00 draw-list corruption from a hang into a passing run, and the !inWorld gate
// that actually fixed that corruption was consequently UNGRADED. A counter is what makes the difference
// between "the guard is never needed" and "nobody looked".
int gCtDrawRunaway;
int gCtDrawClsMask;   // bitmask of the sprite CLASSES that actually reached the draw list, i.e. the only
                      // classes whose OAM bytes any lockstep check can compare. It is reported rather
                      // than assumed because a routine can be widened to 8/12 pieces and still have
                      // those pieces observed by nothing (measured: --verifyframe sees classes 0x7 in
                      // the priority pass but only class 0 in the list, so writing garbage into pieces
                      // 4..11 changes not one compared byte).
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
      // A PORT-SIDE NON-CONVERGENCE GUARD, not a ROM behaviour -- the ROM's B2B2 loop is unbounded
      // because its $0E81 chain is always well-formed. Ours is not yet: with CT_FRAMELEGS=1 on a LIVE
      // host (Belthasar --scene 1 --worldtest) the chain CYCLES -- measured obj 00 <-> obj 04, with six
      // different buckets all claiming obj 0 as their head while obj 4's prev-link says IT is the head.
      // The scene-entry fill is not the culprit (CtObjSceneInit does write $0E00..$0EFF = $80); the
      // list is corrupted DURING the run by unlink/relink leaving stale heads behind. The lockstep
      // harness cannot see it because it re-seeds the whole list from the oracle every frame.
      // Bailing out here makes that a visible wrong frame instead of a hang; 128 is above any real
      // chain (the list holds at most 64 objects) so a well-formed list never reaches it.
      if (++guard > 128) { gCtDrawRunaway++; if (unresolved) *unresolved = unres; return -1; }
      r[0x016D] = (uint8_t)obj;                            // B2AD / B2B9 STA $6D (8-bit)
      int c = CtAnimRelease(r, obj);                        // B309 JSR C0/B701
      if (c < 0) unres++;                                  // class 3: no derivable carry -- see above
      else if (c == 0) {                                   // B30C BCC ->B30F: CLC = draw it
        int prio = r[0x0F80 + obj] & 0x0C;                 // B334 LDA $0F80,X / B337 AND #0C
        int hi, mn;                                        // the group's (high, main) cursor pair
        if (prio == 0)          { hi = 0x0181; mn = 0x01DB; }   // B339 BEQ ->B341
        else if (prio & 0x04)   { hi = 0x0185; mn = 0x01DD; }   // B33B BIT #04 / BNE ->B397
        else                    { hi = 0x0189; mn = 0x01DF; }   // B33F BRA ->B3BB
        // ⚠ THE STRIDE IS PER-CLASS, NOT A CONSTANT 0x10, and assuming otherwise was a real defect
        // this routine shipped with -- found by --sweepdraw, invisible to every scenario. B309's FIRST
        // dispatch is on the class (B316 BEQ ->B329 / B31C BRL ->B3DF / B323 BRL ->B4B8), and only then
        // does each class body pick the priority group; so each class has its OWN arms with its own
        // advance. Read off the group-0 arm of each: class 0 at B341 writes ONE high byte ($7F4F00) and
        // `B35D ADC #0010`; class 1 at B3FB writes TWO ($4F00 + $4F01, INX twice) and `B421 ADC #0020`;
        // class 2 at B4D4 writes THREE ($4F00 + $4F01 + $4B40) and 0x30. That is one high byte per
        // group of four pieces, which is exactly ctOamEmit's shape.
        // ⚠ AND THE COUNT IS NOT `(class & 3) + 1` EITHER -- class 3 breaks that arithmetic. Its arm
        // is B5CB, and it pushes SIX high bytes ($4F00/$4F01/$4B40/$4B41/$4F80/$4F81, the same six
        // C0/C73A writes, in the same order) and advances its main cursor by $60 -- on all three
        // priority arms (B619 / B6A1 / B6F8). So the entry count is READ OFF the four arms, 1/2/3/6,
        // never computed from the class number.
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
  if (r[0x09A0] != 0) return 0;                            // CA76 LDA $09A0 / BEQ / RTS -- the WHOLE
                                                           //   pass is skipped while the VRAM upload
                                                           //   queue still holds work. An 8-bit read:
                                                           //   $09A1 accumulates stale values (0x46
                                                           //   measured) because B201's clear is
                                                           //   `STZ $A0`, also 8-bit.
  r[0x0179] = 0;                                           // CA80 STZ $79 (the upload cursor; DP=$0100)
  int visits = 0;
  for (;;) {
    // CA85-CA9B, THE RASTER DEADLINE. CA88-CA91 assembles the 9-bit V counter out of two $213D
    // reads; CA94 `CMP #00F0 / BPL` is SIGNED (>= 240 = vblank -> no limit at all) and CA99
    // `CMP $6B / BCS` is UNSIGNED against a WORD ($6B/$6C, measured 0x0078 = scanline 120 on every
    // state tried; an event-VM op at C0/678E can set it from a script operand).
    //
    // THE CLOCK IS THE ZELDA3 MODEL'S VIRTUAL BEAM (scanline < 0, the live VM's call; the user's decision 2026-09-27):
    // in the ROM the V counter advances while the drain works, so the deadline cuts the queue mid-list and the rest
    // waits for the next frame (CAD6, no pop). The real line is the frame's whole CPU history, which a port does not
    // have; the virtual one is the pass's own work, V = CT_DRAIN_VBEAM_ENTRY + visits * CT_DRAIN_VBEAM_STEP
    // (src/ct_vbeam.h), and the oracle's drain reads the same V under CT_ORACLE_VIRTUAL_BEAM (snes.c snes_readBBus).
    // At the ROM's own $6B = 0x78 a pass makes two visits. A fixed `scanline` (the harnesses' 0xF0 = vblank = no
    // deadline) drains the queue fully: that is the regime the port ran in before, where the objects past the arena's
    // fourth slot were popped with CLC and lost their frame on every step.
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
    // THIS BRANCH IS UNGRADED, and the reason CHANGED once C0/CEF5 landed -- it is worth stating
    // rather than leaving as a silent hole in --sweepdrain. There now IS a ported path that returns
    // SEC: class 1's pass 1 (C0/D30B). But queueing a class-1 object segfaults that sweep's oracle,
    // because a fresh-boot state leaves the sprite pointer tables zero and C0/E687's bank dispatch
    // falls through to the C0/E6B6 copier, whose `STA $000n,Y` writes BANK 0 at an arbitrary Y --
    // into the DMA registers. Seeding a real ROM bank did not help either. --sweepcad9 micro-executes
    // the same handler safely (80640/80640) because it runs ONE visit on ONE object, where the drain
    // runs three back-to-back over accumulated state. So the SEC RETURN is verified; the loop's
    // response to it -- this `continue` -- is derived, not verified.
    if (c) {                                               // CAB1 / CAC5 BCS -> CA85: SEC, do NOT pop
      if (visits > 64) return -2;                          // NOT a ROM behaviour, and no longer just
      continue;                                            //   a malformed-list guard: C0/D50D-D514
    }                                                      //   returns SEC having done NOTHING for a
                                                           //   class-2 object in anim mode 3, so the
                                                           //   ROM itself spins on it -- terminated
                                                           //   only by the raster deadline, which a
                                                           //   clockless port does not have. -2 is
                                                           //   this port saying so, not inventing an
                                                           //   exit the ROM does not take.
    if (last) {                                            // CAC7-CAD3: empty the list
      r[0x1081 + h] = 0x80; r[0x0176] = 0x80; r[0x0177] = 0x80;
    } else {                                               // CAB3-CAC0: pop the head
      r[0x0176] = r[0x1081 + h];
      r[0x1081 + h] = 0x80;
    }
    if (visits > 64) return -2;                            // same guard on the pop path
  }
}

// =================================================================================================
// C0/4557 + C0/4590 -- the object LOADER's WRAM-field half. See disasm/notes/object_loader.md.
//
// This is what fills the tables the whole C0/CAD9 tree reads, and its absence is why that tree is
// byte-exact and unreachable. It walks the sprite id's 5-byte descriptor through four bank-$E4
// pointer tables whose bases C0/0BB0-0C0E loads from $E4:FFE0 into $AF / $B2 / $B5 / $B8 (all bank
// $E4, values $2000 / $2300 / $2600 / $2800). Descriptors live at $E4:F000; records 0..6 are the
// seven PCs, which is exactly why the NPC path indexes from $E4:F023 = $F000 + 7*5.
// =================================================================================================

// `[$dp],Y` on one of the four base pointers: a 24-bit add that can carry out of the base bank.
static int ctLoaderTbl(const uint8_t* r, const uint8_t* rom, int romLen, int dp, int y, int wide) {
  int base = ((r[0x0100 + dp + 2] & 0xFF) << 16) | r[0x0100 + dp] | (r[0x0100 + dp + 1] << 8);
  int lo = romRead24Flat(r, rom, romLen, base + y);
  return wide ? (lo | (romRead24Flat(r, rom, romLen, base + y + 1) << 8)) : lo;
}

// C0/4639-4663 and C0/4686-46AE -- the SAME three-way store, once per arm. `LDA $73 / CMP #01 /
// BMI / BEQ` is a SIGNED compare, so the "$29C8" arm is `($73 - 1) & $80`, which $73 = $81 takes
// as well as $73 = 0. Writing it as `p == 0` would be right for every value the engine produces
// and wrong for the branch the ROM actually has.
static void ctLoaderPartySlot(uint8_t* r, int cur) {
  int p = r[0x0173];
  int dst = (((p - 1) & 0x80) != 0) ? 0x29C8 : (p == 1 ? 0x29D4 : 0x29E0);
  r[dst] = (uint8_t)cur; r[dst + 1] = (uint8_t)(cur >> 8);
}

// THE DESCRIPTOR WALK, shared by the NPC loader (C0/45A3-4620) and the PC loader (C0/449B-4518).
// The two are the SAME INSTRUCTIONS, and that is MEASURED rather than noticed: aligned at their
// common `LDX $4216` (`AE 16 42`, NPC C0/45A3 and PC C0/449B), 140 bytes differ in exactly 18, of
// which FIVE are the table offsets (24/25/26/27/23 against 01/02/03/04/00), TWO are a `DEY DEY SEC`
// / `SEC DEY DEY` reordering that cannot differ in effect (SEC does not touch Y, DEY does not touch
// carry), and the remaining eleven lie past the walk, where the two tails genuinely part. So the
// RECORD BASE is the only real parameter: $E4:F023 for NPCs, $E4:F000 for PCs -- and $23 = 35 = 7*5
// is exactly the seven PC records the NPC table starts after, which is the same fact CtObjLoad's
// own comment states from the other side.
// Returns the record's flat ROM offset, which the NPC tail still needs for its dedup key.
// ⚠ THE STRIDE IS A PARAMETER, not a constant. C0/4590 (op 0x82) walks $E4:F023 with stride 5;
// C0/46E4 (op 0x83) is the SAME 147 bytes with only the stride and the five table offsets changed
// -- byte-diffing rom[0x4595:0x4628] against rom[0x46E4:0x4777] leaves exactly 9 differing bytes,
// all of them those two things. Same field semantics at rec+0..rec+4, same [$B2]/[$B5]/[$B8]
// walks, same `CLC / ADC #0000` no-op. So this is shared verbatim rather than duplicated.
static int ctLoaderWalk(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int recBase,
                        int stride) {
  int bf = (id * stride) & 0xFFFF;                         // 4595 LDA #05 (op 0x82) / 46E4 LDA #0A
  int rec = recBase + bf;                                  // $E4:F023 (NPC) or $E4:F000 (PC)
  r[0x01BF] = (uint8_t)bf; r[0x01C0] = (uint8_t)(bf >> 8); // 45A3 LDX $4216 / 45A6 STX $BF (16-bit)

  int f1 = (romLen > rec + 1) ? rom[rec + 1] : 0;          // 45A8 $E4F024,X
  // 45B1 `STA $D9`, 16-bit, and 45AE's `AND #00FF` has already cleared the high byte -- so $01DA
  // is ZEROED here, not left alone. The three DP spills above are the whole of what C0/4590 writes
  // to the direct page besides $E3; measured with `--callrom C0/4590 --regdp 0100 --writes`, which
  // named every one of them and their instruction addresses. They are not scratch the port may
  // skip: the field engine's DP is shared, and $01D9/$01DA is the same pair C0/ACFD stages through.
  r[0x01D9] = (uint8_t)f1; r[0x01DA] = 0x00;
  int y1 = f1 * 3;                                         // 45B3-45B6: *3 (a 3-byte pointer table)
  int a1 = ctLoaderTbl(r, rom, romLen, 0xB2, y1, 1);       // 45B9 LDA [$B2],Y -- the 16-bit address
  r[0x1380 + idx] = (uint8_t)a1; r[0x1381 + idx] = (uint8_t)(a1 >> 8);
  r[0x1300 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xB2, y1 + 2, 0);   // 45C4: the BANK

  int f2 = (romLen > rec + 2) ? rom[rec + 2] : 0;          // 45CB $E4F025,X
  int v2 = (f2 * 0x18) & 0xFFFF;                           // 45D2 LDA #18 -> $4216
  r[0x1400 + idx] = (uint8_t)v2; r[0x1401 + idx] = (uint8_t)(v2 >> 8);       // 45E5 (16-bit)

  int f3 = (romLen > rec + 3) ? rom[rec + 3] : 0;          // 45EC $E4F026,X
  int y3 = f3 * 2;                                         // 45F5 ASL A -- a WORD table
  int w0 = ctLoaderTbl(r, rom, romLen, 0xB5, y3, 1);       // 45F9/45FE: [$B5],Y and [$B5],Y+2
  int w1 = ctLoaderTbl(r, rom, romLen, 0xB5, y3 + 2, 1);
  int d = ((w1 - w0) & 0xFFFF) >> 2;                       // 45FD-4601: (next - this) >> 2. DERIVED
  r[0x1480 + idx] = (uint8_t)d; r[0x1481 + idx] = (uint8_t)(d >> 8);         //   from a DIFFERENCE,
  r[0x1500 + idx] = (uint8_t)w0; r[0x1501 + idx] = (uint8_t)(w0 >> 8);       //   not read from a table
  int w2 = ctLoaderTbl(r, rom, romLen, 0xB8, y3, 1);       // 460C LDA [$B8],Y
  r[0x1580 + idx] = (uint8_t)w2; r[0x1581 + idx] = (uint8_t)(w2 >> 8);

  r[0x1201 + idx] = (uint8_t)((romLen > rec + 4) ? rom[rec + 4] : 0);        // 4615: THE CLASS BYTE
  return rec;
}

int CtObjLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int ip, int state) {
  // ---- C0/4557, the shared prologue ------------------------------------------------------------
  r[0x1100 + idx] = (uint8_t)state;                        // 4559: 4 = NPC (4590), 5 = enemy (46E1)
  ip = (ip + 1) & 0xFFFF;                                  // 455C INY
  int id = r[0x12001 + ip];                                // 455E LDA $7F2001,X -- the SCRIPT byte
  r[0x1101 + idx] = (uint8_t)id;                           // 4564
  r[0x1280 + idx] = r[0x0171];                             // 456C LDA $71 / STA $1280,X (16-bit) --
  r[0x1281 + idx] = r[0x0172];                             //   the graphics staging address
  r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 4571 LDA #2020 / STA $0C00,X
  r[0x1200 + idx] = 0x7F;                                  // 4579: the graphics BANK
  r[0x1600 + idx] = 0x01;                                  // 457E: facing
  r[0x1680 + idx] = 0x00; r[0x1681 + idx] = 0x00;          // 4583 / 4588
  ip = (ip + 1) & 0xFFFF;                                  // 458D INY
  // ---- C0/4590, the descriptor walk ------------------------------------------------------------
  // 459A `STY $C7`, SIXTEEN-bit under DP = $0100. This is the ONLY copy of the advanced IP the
  // routine keeps -- Y is clobbered from 45B8 onward and both exits come back through `LDX $C7`
  // (4667 / 46DB) -- so a caller that read Y would get the pointer-table index, not the script IP.
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8);
  r[0x1B01 + idx] = 0x01;                                  // 459E
  int rec = ctLoaderWalk(r, rom, romLen, idx, id, 0x24F023, 5);   // C0/45A3-4620

  // ---- C0/5C90, the per-scene graphics dedup ---------------------------------------------------
  // $0920[0..15] holds the sprite ids already loaded this scene and $0930[0..15] the owning object's
  // $6D. A HIT returns the owner's index with carry SET and the caller copies that object's $1280
  // (4631) instead of loading again; a MISS claims the first $FF slot, records id and owner, and
  // returns carry CLEAR -- whereupon the ROM queues a graphics DECOMPRESSION and ADVANCES the
  // cursor. See object_loader.md.
  int e3 = (romLen > rec) ? rom[rec] : 0;                  // 4620 $E4F023,X
  r[0x01E3] = (uint8_t)e3;                                 // 4624 STA $E3 -- DP is $0100, so $01E3
  int k;
  for (k = 0; k < 16; k++) if (r[0x0920 + k] == (uint8_t)e3) break;          // 5C95 CMP $0920,X
  if (k < 16) {                                            // HIT (5CC2 LDA $0930,X / SEC)
    int owner = r[0x0930 + k];
    r[0x1280 + idx] = r[0x1280 + owner];                   // 4631-4636
    r[0x1281 + idx] = r[0x1281 + owner];
    // 4639-4663: the hit arm records the CURRENT cursor, not the owner's address. That looks like
    // a ROM slip -- the sprite's graphics are at the owner's $1280, not at $71 -- and it goes in
    // verbatim, because what the party slots hold is what the rest of the engine reads.
    ctLoaderPartySlot(r, r[0x0171] | (r[0x0172] << 8));
  } else {
    // ---- the MISS arm, C0/466B-46DE: queue the decompression and ADVANCE the cursor ------------
    for (k = 0; k < 16; k++) if (r[0x0920 + k] == 0xFF) break;               // 5CA5 CMP #FF
    if (k >= 16) return ip;   // ROM: LDX #4010 / BRL C0/2E1E -- a diagnostic HALT (SEI, force
                              // blank, NMI and DMA off), not a return. The port stops instead.
    r[0x0920 + k] = (uint8_t)e3;                           // 5CB6-5CBD: claim the slot
    r[0x0930 + k] = (uint8_t)idx;
    // The 6-byte request record: a 24-bit SOURCE from the 3-byte packet table [$AF], and a 24-bit
    // DESTINATION that is simply the cursor in bank $7F.
    r[0x01D9] = (uint8_t)e3; r[0x01DA] = 0x00;             // 4672 STA $D9 -- the SECOND spill, and
    int y = e3 * 3;                                        // 466D-4679: *3   it overwrites 45B1's
    int sLo = ctLoaderTbl(r, rom, romLen, 0xAF, y, 1);     // 467A LDA [$AF],Y / STA $0300
    int sBk = ctLoaderTbl(r, rom, romLen, 0xAF, y + 2, 0); // 46B2 (after INY INY) / STA $0302
    int cur = r[0x0171] | (r[0x0172] << 8);
    r[0x0300] = (uint8_t)sLo; r[0x0301] = (uint8_t)(sLo >> 8);
    r[0x0302] = (uint8_t)sBk;
    r[0x0303] = (uint8_t)cur; r[0x0304] = (uint8_t)(cur >> 8);               // 467F LDA $71
    r[0x0305] = 0x7F;                                      // 46B7 LDA #7F / STA $0305
    ctLoaderPartySlot(r, cur);                             // 4686-46AE: the PRE-advance cursor
    // JSL $C3:0002 -- the shared "Evil Peer" LZ, already ported as ct_lz_decompress. It writes the
    // sprite sheet to $7F:cur and REPORTS the byte count in $0306, which is the only thing that
    // moves $71. Guarded on a HiROM bank: a descriptor whose packet pointer is not $C0.. is not a
    // case the engine reaches, and decompressing from a bogus offset would grade nothing.
    if (sBk >= 0xC0) {
      static uint8_t scratch[0x10000];
      int src = ((sBk - 0xC0) << 16) | (sLo & 0xFFFF);
      if (src + 2 <= romLen) {
        int sz = (int)ct_lz_decompress_bounded(rom, (size_t)romLen, scratch,
                                               sizeof scratch, (size_t)src);
        // ...and what the ROM SPENDS on it: the load's black window holds for this decompression too
        // ($0305 = $7F, C3/0557's odd-bank arm). The host zeroes the accumulator before a load.
        g_ctLzSpriteCycles += ct_lz_rom_cycles(rom, (size_t)romLen, (size_t)src, 1);
        r[0x0306] = (uint8_t)sz; r[0x0307] = (uint8_t)(sz >> 8);             // C3/0002's report
        for (int q = 0; q < sz && cur + q < 0x10000; q++)                    // ... to $7F:cur
          r[0x10000 + cur + q] = scratch[q];
        cur = (cur + sz) & 0xFFFF;                         // 46C2-46C8: $71 += $0306
        r[0x0171] = (uint8_t)cur; r[0x0172] = (uint8_t)(cur >> 8);
      }
    }
    // 46CA `CMP #8000 / BPL`: the staging heap is $7F:8000-$FFFF, so a cursor BELOW $8000 means it
    // wrapped past $FFFF -- and that arm is `LDX #7C1F / BRL C0/2E1E`, the same diagnostic halt.
    // Measured live: $71 = $B860 (DEMO8_gtu), $9000 (bedslide), $FE20 (S5_gtu) -- all in range.
    if (cur < 0x8000) return ip;
  }
  r[0x0173] = (uint8_t)((r[0x0173] + 1) & 0xFF);           // 4665 / 46D9 INC $73
  return ip;
}

// C0/448D -- THE PC DESCRIPTOR LOADER, op 0x80's worker. It is the walk above with the record base
// $E4:F000 (see ctLoaderWalk for the byte evidence) and a COMPLETELY DIFFERENT TAIL: where the NPC
// loader calls the C0/5C90 dedup and decompresses through JSL C3/0002, this one reads its graphics
// pointer straight out of the 3-byte packet table [$AF] and stops. A PC's sheet is already resident.
//
// It also does NOT write $1B01,X -- C0/4590 has `LDX $6D / LDA #01 / STA $1B01,X` exactly where
// 448D has three NOPs (4498-449A), and op 0x80's arms set that byte themselves.
//
// `yIn` is Y AT THE JSR, not the opcode's IP, and that distinction is the whole encoding. 4490 does
// ONE INY of its own and 4496 `STY $C7` spills the result; 4552 `LDY $C7` restores it before the
// RTS, which is why op 0x80's arms can end `TYX / SEC` with no INX. op 0x80 arrives here having
// already done `421E INY` (yIn = ip+1, so two bytes); its seven immediate siblings arrive with Y
// untouched (yIn = ip, so ONE byte). Adding both INYs here would hard-code the two-byte form.
static int ctPcLoad(uint8_t* r, const uint8_t* rom, int romLen, int idx, int id, int yIn) {
  int ip = (yIn + 1) & 0xFFFF;                             // 4490 INY -- this routine's own, only
  r[0x01C7] = (uint8_t)ip; r[0x01C8] = (uint8_t)(ip >> 8); // 4496 STY $C7 (16-bit)
  int rec = ctLoaderWalk(r, rom, romLen, idx, id, 0x24F000, 5);       // C0/449B-4518
  // ---- the PC tail, C0/4518-4551 ---------------------------------------------------------------
  int e0 = (romLen > rec) ? rom[rec] : 0;                  // 4518 LDA $E4F000,X
  r[0x01D9] = (uint8_t)e0; r[0x01DA] = 0x00;               // 4521 STA $D9 -- 16-bit, so $01DA is
  int y = e0 * 3;                                          // 4523-4528: *3   ZEROED, not left alone
  int a = ctLoaderTbl(r, rom, romLen, 0xAF, y, 1);         // 4529 LDA [$AF],Y
  r[0x1280 + idx] = (uint8_t)a; r[0x1281 + idx] = (uint8_t)(a >> 8);  // 452D (16-bit)
  r[0x1200 + idx] = (uint8_t)ctLoaderTbl(r, rom, romLen, 0xAF, y + 2, 0);   // 4534/4536: the BANK
  r[0x0C00 + idx] = 0x20; r[0x0C01 + idx] = 0x20;          // 453B LDA #2020 / STA $0C00,X
  r[0x1680 + idx] = 0x00;                                  // 4543
  r[0x1681 + idx] = 0x00;                                  // 4548
  r[0x1600 + idx] = 0x01;                                  // 454D
  return ip;                                               // 4552 LDY $C7 / TYX / SEC / RTS
}

// C0/42BA-430F, C0/438A-43C2 and C0/443D-4472 -- ONE default placement, reached from all three of
// op 0x80's party arms and byte-for-byte identical in each.
//
// ITS THREE INPUTS ARE NOT WRITTEN HERE, AND NOT BY C0/448D EITHER. $0102/$0103/$0104 are the
// ARRIVAL STAGING laid down by the ChangeLocation handler C0/3B1B: `3B2C LDA $7F2001,X / 3B30
// STA $02` is a SIXTEEN-bit store of the arrival tile into $0102/$0103, and `3B34 LDA $DA / LSR /
// AND #0F / 3B39 STA $04` plus `3B3D LDA $DA / AND #80 / 3B3F TSB $04` builds $0104 out of the
// destination word's high byte -- facing in bits 0-1, and bit 7 carried over separately. So these
// are INPUTS to op 0x80, which is what makes the opcode placement-free on its own.
//
// The $FF / $80 literals are the same tile-foot / tile-centre convention op 0x8B writes directly.
static void ctPcPlaceDefault(uint8_t* r, int idx) {
  r[0x1801 + idx] = r[0x0102];                                          // 42BA / 438A / 443D
  r[0x1881 + idx] = r[0x0103];                                          // 42BF / 438F / 4442
  r[0x1600 + idx] = (uint8_t)(r[0x0104] & 3);                           // 42E1 / 4394 / 4447
  r[0x1880 + idx] = (uint8_t)((r[0x0104] & 0x08) ? 0x7F : 0xFF);        // 42E8 BIT #08
  r[0x1800 + idx] = (uint8_t)((r[0x0104] & 0x04) ? 0x00 : 0x80);        // 42FA BIT #04
}

// ---------------------------------------------------------------------------
// The OBJ SLOT ALLOCATOR -- C0/6F9A (entry + state dispatch), C0/6FBA (one slot),
// C0/7008 (a consecutive PAIR) and C0/7056 (the release). $0B88 is the slot table:
// bit7 set = FREE, else the owning object's $6D. This is what fills $1700 (the OBJ
// template base) and $0D00 (the OAM tile base) -- two of the three tables the CAD9
// animation tree reads that nothing in the port fills yet.
// ---------------------------------------------------------------------------

// C0/6F9A. `LDA $1100,X / BMI -> RTS / ASL A / TAX / JSR ($6FA7,X)`. The 9-entry table at
// $6FA7 reads B9 6F B9 6F B9 6F BA 6F BA 6F B9 6F B9 6F B9 6F B9 6F -- and $6FB9 is the
// byte `60` = RTS. So only states 3 and 4 allocate; every other state returns with the
// carry UNTOUCHED, which is why this returns -1 (not 0) for them: "no verdict", not CLC.
// Returns 1 = SEC (granted, $0D00/$1700 written), 0 = CLC (table full, nothing written).
int CtObjSlotAlloc(uint8_t* r, int idx) {
  static const uint8_t kArm[9] = { 0, 0, 0, 1, 1, 0, 0, 0, 0 };             // $6FA7 table
  int st = r[0x1100 + idx];
  if (st & 0x80) return -1;                                                 // 6F9E BMI ->6FA6
  // 6FA1 `ASL A / TAX` is EIGHT-BIT: bit7 falls out into the carry, so without the BMI above a
  // state of $83 would index entry 3 and allocate. Computing the index the ROM's way instead of
  // range-checking the state is what makes that BMI gradable rather than redundant.
  // ⚠ THE `& 0xFF` HERE IS DEFENSIVE, NOT GRADABLE, and that is worth saying rather than
  // leaving for the next person to discover with a surviving mutation: the BMI above has
  // already rejected every state with bit7 set, so `st << 1` can never exceed $FE by the time
  // it runs. It documents the ROM's eight-bit ASL; the BMI is what enforces it.
  int e = (st << 1) & 0xFF;                                                 // 6FA1 ASL A / TAX
  if (e >= 18) return -1;                       // PORT GUARD, not a ROM fact: the table holds nine
                                                // entries, so past it the ROM reads $6FB9/$6FBA as
                                                // a pointer and jumps into the weeds.
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
  // ONE formula, byte-identical on both paths (6FD1-6FE6 and 701C-7031): the even part of
  // the slot scaled by 16 plus 8 for an odd slot -- i.e. pairs share a 16-tile row.
  int tile = (((slot & 0xFE) << 4) + ((slot & 1) << 3)) & 0xFFFF;
  r[0x0D00 + idx] = (uint8_t)tile; r[0x0D01 + idx] = (uint8_t)(tile >> 8);  // 6FEB / 7036
  int tmpl = (slot & 0xFF) << 5;                                            // 6FF7 / 7042 ASL x5
  r[0x1700 + idx] = (uint8_t)tmpl; r[0x1701 + idx] = (uint8_t)(tmpl >> 8);  // 6FFC / 7047
  return 1;                                                                 // 7001 / 704C SEC
}

// ---------------------------------------------------------------------------
// C0/B1B2 -- the VRAM UPLOAD. This is the CONSUMER of the queue C0/CAD9 fills (already ported, see
// ctAnimAppend) and the only thing in the engine that clears `$09A0` -- which is C0/CA76's whole-pass
// gate, so until this runs the drain refuses to do anything at all. Called from C0/B111 (inside the
// scene-entry sweep C0/B0E6) and from C0/EB8C, the NMI.
//
// The routine sets DP = $0900 and configures DMA channel 7 ONCE, then walks the queue:
//   $2115 = $80    VMAIN: increment after the $2119 (high) write, step 1 word, no address translation
//   $4370 = $01    DMAP7 transfer mode 1 -- two registers, written alternately: $2118 then $2119
//   $4371 = $18    BBAD7 = $2118 (VMDATAL)
//   $4374 = $7F    A1B7, the source bank -- the staging bank, always
// then per entry X (stepping by two): fire once for pair A ($0950 dst / $0940 src / $0980 len) and
// once for pair B ($0970 / $0960 / $0990). `$09A0,X` is read EIGHT-BIT and a zero ends the walk --
// note ctAnimAppend writes that flag with a SIXTEEN-bit INC and terminates the next slot with a
// sixteen-bit STZ, so producer and consumer disagree on width exactly as C0/CA76's prologue does.
//
// `vram` is a 64 KB VRAM image, or NULL to walk the queue and clear the gate without copying -- which
// is what a host with no VRAM of its own passes. The gate clear is the half that the field frame
// needs; the copy is the half a renderer needs, and they are separable because the ROM keeps them in
// one routine only to share the channel setup.
// ---------------------------------------------------------------------------

// One `STA $420B` with channel 7 armed as above. Mode 1 alternates the two VRAM data ports and VMAIN
// $80 advances the word address after the SECOND of each pair, which is why the increment sits in the
// odd branch rather than at the top of the loop. A DMA size of 0 means 0x10000 bytes on real hardware,
// not zero -- the sweep drives that case rather than the port assuming either reading.
static void ctVramDma(const uint8_t* r, uint8_t* vram, int vmaddr, int src, int len) {
  if (!vram) return;
  // VMADD is a WORD address into 32K words. There is deliberately NO `& 0x7FFF` here: the byte
  // index below already masks to 0xFFFF, which folds word $8000 onto word $0000 exactly as the
  // hardware does. Masking twice would be an ungraded claim -- the acceptance driver proved it
  // unobservable, which is the signal to delete the expression rather than keep it.
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
  // The ROM has NO upper bound here (B1FD INX INX / BRA ->B1D1); it trusts the terminator that
  // ctAnimAppend's STZ writes. 0x80 bytes = 64 entries is a PORT-SIDE backstop, far above anything
  // the producer's cursor reaches, so a malformed queue loops finitely instead of forever.
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
  r[0x09A0] = 0;                    // B201 STZ $A0 -- EIGHT-bit, and it clears ENTRY 0 ONLY, not the
                                    // whole flag array. The next append starts the queue over from
                                    // cursor $79, so entry 0's terminator is all C0/CA76 needs.
  return n;
}

// ---------------------------------------------------------------------------
// The SCENE-ENTRY INIT of everything the animation tree allocates from. C0/56B0 runs, in order,
// `6F79 / 7155 / E935 / 5929 / B204 / A7E9 / 5709 / 595C`; four of those are the fills below, and
// they are the PRECONDITION for the rest -- C0/A9CD refuses to link unless `$0E01,X` is already
// negative, and that negative comes from C0/B204 and nowhere else.
//
// Three of the four write through the PPU's WRAM PORT rather than storing directly: they set
// DP = $2100 and push bytes at `$2180` after pointing `$2181/$2183` at the destination. That is a
// detail of HOW, not WHAT -- the destinations and the fill values are what a port owes, and the
// sweep micro-executes the real routines (the oracle's WRAM port and MVN both run) to prove it.
// ---------------------------------------------------------------------------
void CtObjSceneInit(uint8_t* r, int nobj) {
  int k;
  for (k = 0; k < 0x1E; k++) r[0x0B88 + k] = 0x80;   // C0/6F79: 30 OBJ slots, all FREE
  for (k = 0; k < 8;    k++) r[0x0BC0 + k] = 0x80;   // C0/E935: the arena owner table -- EIGHT bytes
                                                     //   are cleared, though only the first four are
                                                     //   ever handed out by C0/E952.
  // C0/B204: $0E00..$0E1F written directly (16 x two bytes), then THREE chained MVNs -- 0x20, 0x40
  // and 0x80 bytes, each from $0E00, with Y carrying on where the last left off -- which doubles
  // the fill out to $0EFF. Every head, flag, prev and next starts at $80 = "none"/"unlinked".
  for (k = 0; k < 0x100; k++) r[0x0E00 + k] = 0x80;
  // C0/A7E9: one (00, FF) pair per object from $0F00 -- the activation latch clear and the "no
  // frame yet" sentinel. Driven by the object count, not by a fixed length.
  for (k = 0; k < nobj; k++) { r[0x0F00 + k * 2] = 0x00; r[0x0F01 + k * 2] = 0xFF; }
}

// ---------------------------------------------------------------------------
// C0/A98A / C0/A9CD -- the Y-SORTED SPRITE LIST's unlink and link. These are the two calls that sit
// AROUND C0/AB45 inside the movement pass (`A832 C98A / A835 A98A / A838 AB45 / A83B A9CD`, and the
// same four at A878-A881), and the order is not incidental: the object must leave the bucket keyed
// on its OLD screen Y before AB45 recomputes it, and re-enter keyed on the NEW one.
//
// THE LAYOUT. Two 256-byte pages, each interleaving two arrays on parity:
//   $0E00,Y  (Y even, 0..$7E)  = the bucket HEADS -- bucket = ($0A80,X >> 1) & $FE, the LOW byte of
//                                the screen Y only, so bit8 (the off-screen marker) never buckets
//   $0E01,X  (X even)          = the per-object "OUT OF THE LIST" flag; bit7 set = unlinked
//   $0E80,X / $0E81,X          = the per-object PREV / NEXT links
// `$80` is the sentinel for "none" on every link. The draw pass walks $0E00 from bucket $7E DOWN,
// following $0E81 (C0/B2A0-B2C0) -- which is exactly what CtBuildFieldOAM in oam.c already does.
//
// WHY THESE HAD TO BE PORTED AND NOT REBUILT. `--checksort` is a recorded experiment that rebuilt
// the list from scratch each frame by inserting every active sprite at its bucket head: 1392 head
// and 3780 next mismatches across 180/180 frames. The list is HISTORY-DEPENDENT -- chain order
// records the order objects were inserted over time, not their index order -- so incremental
// maintenance is the only thing that reproduces it.
// ---------------------------------------------------------------------------

// C0/A98A. Removes the object from its bucket. Returns 1 if it did work, 0 if it was already out
// (`$0E01,X` negative -> A991 BMI, an immediate RTS).
int CtObjListUnlink(uint8_t* r, int idx) {
  if (r[0x0E01 + idx] & 0x80) return 0;                    // A98E/A991: already unlinked
  int prev = r[0x0E80 + idx];                              // A993
  if (prev & 0x80) {                                       // A996 BPL: negative = we are the HEAD
    int bucket = (r[0x0A80 + idx] >> 1) & 0xFE;            // A998-A99E (8-bit: the LOW byte only)
    int next = r[0x0E81 + idx];                            // A99F
    r[0x0E00 + bucket] = (uint8_t)next;                    // A9A2: the head becomes our NEXT
    int a = next;
    if (!(next & 0x80)) { a = 0x80; r[0x0E80 + next] = 0x80; }   // A9A5-A9AC: next is the head now
    r[0x0E81 + idx] = (uint8_t)a;                          // A9AD -- NOTE: `a` is the NEXT value when
    r[0x0E01 + idx] = (uint8_t)a;                          // A9B0    next was already negative, and
    return 1;                                              //         $80 otherwise. Not always $80.
  }
  int next = r[0x0E81 + idx];                              // A9B6
  r[0x0E81 + prev] = (uint8_t)next;                        // A9B9: prev.next = next
  // A9BC-A9C0 does `TAY / STA $0E80,Y` with NO sign test, so when we are the TAIL (next = $80) the
  // ROM writes prev into $0E80+$80 = $0F00 -- object 0's activation latch. That is what the bytes
  // say; it goes in verbatim, and the sweep measures it rather than the port deciding it is a bug.
  r[0x0E80 + next] = (uint8_t)prev;                        // A9BE: next.prev = prev
  r[0x0E80 + idx] = 0x80;                                  // A9C3
  r[0x0E81 + idx] = 0x80;                                  // A9C6
  r[0x0E01 + idx] = 0x80;                                  // A9C9
  return 1;
}

// C0/A9CD. Inserts the object at the HEAD of the bucket its CURRENT screen Y selects. Returns 1 if
// it linked, 0 if it was already in a list (`$0E01,X` non-negative -> A9D4 BPL, an immediate RTS).
// Note the non-empty arm never writes `$0E80,X`: the invariant that our prev is $80 comes from the
// unlink that must have preceded us, not from here.
int CtObjListLink(uint8_t* r, int idx) {
  if (!(r[0x0E01 + idx] & 0x80)) return 0;                 // A9D1/A9D4: already linked
  int bucket = (r[0x0A80 + idx] >> 1) & 0xFE;              // A9D6-A9DC
  int head = r[0x0E00 + bucket];                           // A9DD
  if (head & 0x80) {                                       // A9E0 BPL: negative = the bucket is EMPTY
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

// ---------------------------------------------------------------------------
// C0/AB45 -- the per-object SCREEN POSITION. Item 4 of the loader chain: the last of the tables the
// C0/CAD9 tree reads that nothing in the port filled. Called per object from the movement pass
// (C0/A838 and C0/A87E, both inside C0/A810) and from C0/B101.
//
// The camera is the low byte of $1D0A/$1D0E scaled by 8 PLUS the sub-tile phase $1D87/$1D89 & 7 --
// the same "camera = base*8 + phase" shape the field camera dossier records, though off different
// registers than the $1D93/$1D96 pair the commit uses. The object's world position is $1800/$1880
// shifted right FOUR (the low nibble is sub-pixel), and the difference is kept to NINE bits with
// bit8 FORCED on a borrow. That bit8 is not decoration: it is exactly the ">= $100" arm the six OAM
// emitters' Y clamps branch on, so an object left of or above the camera clamps as "off the top".
// ---------------------------------------------------------------------------
void CtObjScreenPos(uint8_t* r, int idx) {
  int phx = (r[0x1D87] | (r[0x1D88] << 8)) & 7;                          // AB49 LDA $1D87/AND #0007
  int phy = (r[0x1D89] | (r[0x1D8A] << 8)) & 7;                          // AB51
  int camX = ((((r[0x1D0A] | (r[0x1D0B] << 8)) & 0xFF) << 3) + phx) & 0xFFFF;   // AB59 ASL x3 + $D9
  int camY = ((((r[0x1D0E] | (r[0x1D0F] << 8)) & 0xFF) << 3) + phy) & 0xFFFF;   // AB67
  r[0x01D9] = (uint8_t)phx;  r[0x01DA] = (uint8_t)(phx >> 8);            // the DP scratch the ROM
  r[0x01DB] = (uint8_t)phy;  r[0x01DC] = (uint8_t)(phy >> 8);            // leaves behind, verbatim
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

// C0/7056. Recovers the slot from $1700,X >> 5 -- the allocator's own scale factor read
// backwards -- and releases it. Note it never consults the class: it frees the PAIR iff
// the neighbour is also ours, exactly like the arena's C0/EA42 run-release. Returns
// 1 = SEC (released), 0 = CLC (that slot is not this object's, nothing written).
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

// ---- THE ANIMATION FAMILY -- C0/2E67 (0xAA), 2E8F (0xAE), 2E87 (0xB3), 2E8B (0xB4), 2E9A (0xAB),
// 2F01 (0xB7), 2F71 (0xAC). One body, TWO callers, exactly like CtVmItemFamily / CtVmPartyGateFamily:
// CtFieldRunnerOne (what --sweepops micro-executes against the ROM) and the live event VM's step() in
// ct_vm.c, which until now kept this state in private C fields (animMode/animIndex/animBusy...) and
// keyed the BLOCKING ops' completion on a renderer flag instead of $7F0B01,X -- the byte C0/2E9C
// actually branches on, and the byte the ported frame legs (CtAnimTimerTick's CA4F ping-pong and
// CtFieldFrameLookup's CBA4 arm) already drive 2 -> 1 in the live image. Measured cost of the private
// copy: --vmdiff on a cutscene seeded mid-B7 (scene 8, the gate) made ZERO ip-steps in 1500 frames
// because nothing in a headless run ever cleared the flag, and in Belthasar the same cutscene ran ~800
// frames ahead of the ROM (DD at f1638 vs f2447) because the renderer's own playback decided when a
// B7 was over. The bodies below are the switch cases they used to be, moved verbatim; the ONLY edit is
// the operand reads, which go through `script` for the same reason CtVmItemFamily's do.
// Returns 1 if `script[ip]` is in the family (and fills *out), 0 otherwise.
int CtVmAnimFamily(uint8_t* r, const uint8_t* script, int ip, int idx, CtHRet* out) {
  int op = script[ip];
  if (op != 0xAA && op != 0xAB && op != 0xAC && op != 0xAE && op != 0xB3 && op != 0xB4 && op != 0xB7)
    return 0;
  CtHRet h; h.ip = ip; h.carry = 1; h.ok = 0;
  switch (op) {
      case 0xAA:                                          // C0/2E67 -- looping ANIMESET (operand id)
        // Five unconditional writes; op 0xB3 below is the SAME TAIL entered with A = 0 and without
        // the leading INY, which is why its IP delta is one less.
        r[0x1680 + idx] = script[(ip + 1) & 0xFFFF];   // 2E6F
        r[0x1780 + idx] = 0x01;                               // 2E74
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 2) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xAE:
        // C0/2E8F -- THE FOURTH ENTRY INTO THE SAME TAIL, and the one that proves reading this
        // family by symmetry would have been wrong. 0xB3 and 0xB4 are `LDA #imm / BRA ->2E6D`.
        // 0xAE is not: it stores $1680,X ITSELF and branches to 2E74, which is ONE INSTRUCTION
        // LATER -- past the `2E72 LDA #01`. So $1780,X receives the 0 still in the accumulator
        // instead of 1, and that single skipped instruction is the entire opcode:
        //     0xB3 -> $1680=0, $1780=1     0xB4 -> $1680=1, $1780=1     0xAE -> $1680=0, $1780=0
        //   2E8F A6 6D       LDX $6D
        //   2E91 A9 00       LDA #00
        //   2E93 9D 80 16    STA $1680,X
        //   2E96 A9 00       LDA #00
        //   2E98 80 DA       BRA -38 -> 2E9A - 38 = 2E74   (verified against the raw bytes)
        // $1780 is the anim MODE the drain tree branches on -- the same byte op 0xAC gates its
        // conditional $1680 write on -- so 0 versus 1 is not cosmetic.
        r[0x1680 + idx] = 0x00;                               // 2E93
        r[0x1780 + idx] = 0x00;                               // 2E74, with A = 0 and not 1
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // ONE byte: no INY anywhere
      case 0xB3:                                          // C0/2E87 -- ANIMESET 0. `LDA #00 / BRA ->2E6D`
        r[0x1680 + idx] = 0x00;                               // the shared 2E6D tail, A = 0
        r[0x1780 + idx] = 0x01;
        r[0x1601 + idx] = 0x00;
        r[0x10B01 + idx] = 0x00;
        r[0x1681 + idx] = 0x00;
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // ONE byte: no operand
      case 0xB4:                                          // C0/2E8B -- ANIMESET 1. `LDA #01 / BRA ->2E6D`
        // THE THIRD ENTRY INTO THE SAME TAIL, and the two already here pin its every byte:
        //   2E6D A6 6D       LDX $6D
        //   2E6F 9D 80 16    STA $1680,X   <- the ONLY thing the three entries differ in (A)
        //   2E72 A9 01       LDA #01
        //   2E74 9D 80 17    STA $1780,X
        //   2E77 9E 01 16    STZ $1601,X
        //   2E7A A9 00       LDA #00
        //   2E7C 9F 01 0B 7F STA $7F0B01,X
        //   2E80 9E 81 16    STZ $1681,X
        //   2E83 BB E8 38 60 TYX / INX / SEC / RTS
        // The tail's exit is `TYX / INX` -- ONE INX, always -- so the IP delta is decided entirely by
        // whether the ENTRY moved Y. 0xAA (2E67) opens with `C8 INY` to fetch its operand and so
        // lands on ip+2; 0xB3 (2E87) and 0xB4 (2E8B) are `LDA #imm / BRA` with Y untouched, so both
        // are ip+1 and carry SET. `80 DE` at 2E8D targets 2E8F - 34 = 2E6D (verified against the raw
        // bytes, not the listing). This handler takes NO operand.
        r[0x1680 + idx] = 0x01;                               // 2E6F with A = 1
        r[0x1780 + idx] = 0x01;                               // 2E74
        r[0x1601 + idx] = 0x00;                               // 2E77
        r[0x10B01 + idx] = 0x00;                              // 2E7C
        r[0x1681 + idx] = 0x00;                               // 2E80
        h.ip = (ip + 1) & 0xFFFF; h.carry = 1; h.ok = 1; break;
      case 0xB7:
      case 0xAB: {
        // C0/2E9A -- ANIME ONCE. FOUR arms keyed on $7F0B01,X, and the handler NEVER executes INY:
        // every IP delta comes from the TYX/INX at its two exits, so only the COMPLETION arm
        // consumes the opcode's two bytes. The other three re-park the IP on the 0xAB byte, which is
        // how a one-shot animation blocks the script until the drain reports it finished.
        int st = r[0x10B01 + idx];                        // 2E9C -- the ONLY arm selector
        int oper = script[(ip + 1) & 0xFFFF];
        if (st == 1) {                                    // 2EA2 DEC A / 2EA3 BEQ ->2EB4: COMPLETE
          r[0x10B01 + idx] = 0x00;                        // 2EB6
          r[0x1601 + idx] = 0x00;                         // 2EBA
          r[0x1681 + idx] = 0x00;                         // 2EBD
          int was = r[0x1680 + idx];
          if (was == 0xFF) r[0x1680 + idx] = 0x00;        // 2ECB, only when the $FF marker is there
          r[0x1780 + idx] = (uint8_t)((was == 0xFF) ? 0 : 1);   // 2ED0
          // ⚠ op 0xB7 (C0/2F01) IS THIS SAME BODY WITH ONE EXTRA INX. Byte-diffing 2E9A against
          // 2F01 over $60 bytes gives 39 differences, and they account for themselves completely:
          // three branch displacements, and then a ONE-BYTE SHIFT from +60 onward because 0xB7
          // carries an extra `E8 INX` at 2F3D before its SEC. Everything after re-aligns exactly.
          // So 0xB7 reads the SAME operands and advances THREE bytes, the third being consumed and
          // ignored -- which is what [[ct-field-marle-fair-meeting]] recorded as "op0xB7
          // ANIMEREPEAT len 1->3 (walk stalled)" when getting it wrong stalled Marle's walk.
          h.ip = (ip + (op == 0xB7 ? 3 : 2)) & 0xFFFF; h.carry = 1; h.ok = 1; break;   // 2ED3 / 2F3D
        }
        if (st >= 2 && oper == r[0x1781 + idx]) {         // PARK: the same anim is already running
          h.ip = ip; h.carry = 0; h.ok = 1; break;        // writes NOTHING (measured 0 writes)
        }
        // ARM (st == 0) and RE-ARM (st >= 2 with a different id) are the SAME CODE at 2EE0-2F00.
        r[0x1781 + idx] = (uint8_t)oper;                  // 2EE0
        r[0x1681 + idx] = 0x00;                           // 2EE3
        r[0x1601 + idx] = 0x00;                           // 2EE6
        if (r[0x1780 + idx] == 0) r[0x1680 + idx] = 0xFF; // 2EF0, conditional
        r[0x1780 + idx] = 0x02;                           // 2EF5 / 2F5D -- the same in both
        // ⚠ AND HERE IS WHERE 0xB7 STOPS BEING 0xAB. 0xAB stores the LITERAL 2 (2EF8 LDA #02 /
        // 2EFA STA $7F0B01,X); 0xB7 instead does `2F60 TYX / INX / INX / 2F63 LDA $7F2001,X /
        // 2F67 INC A / 2F6A STA $7F0B01,X` -- it reads a THIRD OPERAND and stores operand+1, which
        // is the repeat count the name implies. My first pass byte-diffed the two over $60 bytes,
        // saw a clean one-byte shift, and concluded "same body, one extra INX". THE WINDOW WAS TOO
        // SHORT: the divergence begins at 2F63, past its end. --sweepops said so at once
        // (`$10B03 C=02 RO=7F`), which is the difference between a diff and a proof.
        r[0x10B01 + idx] = (op == 0xB7)
                           ? (uint8_t)(script[(ip + 2) & 0xFFFF] + 1)   // 2F63-2F6A
                           : 0x02;                                             // 2EF8-2EFA
        h.ip = ip; h.carry = 0; h.ok = 1; break;          // 2EFE / 2F6E TYX / CLC
      }
      case 0xAC: {
        // C0/2F71 -- ANIMESET from a SPRITE-ASSEMBLY id, the $1780 = 3 member of the anim family
        // (0xAA/0xB3/0xB4 use mode 1, 0xAB uses 2). Two bytes, IP+2, and carry CLEAR -- it YIELDS,
        // unlike the mode-1 siblings, which is the one thing that cannot be guessed from symmetry.
        // ⚠ IT NEVER EXECUTES INY: `2F71 BB TYX / 2F72 E8 INX` reads the operand at ip+1 without
        // moving Y, so the exit `2F91 TYX / INX / INX` is what produces IP+2.
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


// ---------------------------------------------------------------------------
// C0/571A-58AA -- THE PER-OBJECT DEFAULT FILL of a location load, and the leg this port did not have.
//
// It sits between two of the four DMA clears (`5717 JSR C0/2DF1` = $7F:0200, then `58B9`/`58CA` =
// $7E:1900 and $7F:0580-1B3F), which is why the write census -- whose subject was the page-sized
// clears -- reported "four DMA transfers and nothing else" and this stayed invisible: every store
// here goes through the PPU's WRAM PORT (`PHD / LDA #2100 / TCD`, so `$81/$82/$83` are
// WMADDL/WMADDM/WMADDH and `$80` is WMDATA), so it is neither a DMA nor a `STA $1000,X` a grep can
// find. Bank $C0 holds exactly SIX `STA $1000,X` and every one of them preserves bit 7.
//
// WHAT IT COST TO LEAVE OUT (user report 2026-09-07, "when talking to GATO, nothing happens and the battle
// does not start", reproduced from their own F5 capture): `$1000,X` bit 7 is the object PAUSE, and it is
// the guard C0/5AD1 refuses an ACTIVATE on. Twenty-five scripts set it with op 0x0B and most never
// clear it (loc 130 pauses object 8 four times and unpauses it never), so without this fill the bit
// rides from scene to scene forever -- and GATO is object 8 of loc 6. Their capture: obj8
// `$1000 = 82`, the ORACLE's own loc-6 state: `02`. Measured directly with
// `--loadcensus saves/ow_w0.state --censustele 6 9 13 0 --censusat 7E1010` (an overworld state whose
// obj8 carries bit 7): `seq=381208 = 04 by C0/5781 (via the $2180 WRAM port)` -- one writer, this one.
// The same fill is why the oracle's stale objects read `$1100 = 80` where ours read the previous
// scene's live mode, and it is where `$1C00,X = 07` comes from (invoke_prologue_guards' "$1C00 is
// dead in our engine, a fresh --scene 2 has 00" -- it is not dead, it was never initialised).
//
// THE RENDER MOVES TOO, and `$1080` is why: it is the ANIM FIFO's link pair, whose bit-7 free/
// unlinked marker C0/5A0F reads (`LDA $1080,X / BPL` = already queued) exactly as C0/A9CD reads
// $0E01 for the draw list. Zero meant 'already queued and linked to object 0' on every fresh
// scene. Bisected: of the thirteen arrays, that one alone moves a frame hash (scene 5 at
// (376,488): 69CDD121 with the fill skipped, 6110E21A with it), and the oracle's own loc-5 image
// reads 80 for all 29 objects -- as do we now, along with $0F80/$1A81/$1780/$1A00/$1C00/$1100.
//
// The count is `$7F2000` = the object count for every array but one: `$1100` is pre-filled 0x3F = 63
// pairs of (80,00) -- the WHOLE array marked unused -- and only then given (07,00) per real object.
// The pairs below are the ROM's `LDX #a / LDY #b / STX $80 / STY $80`, i.e. even byte then odd byte.
//
// NOT PORTED, and the ROM is why: 5876 fills `$7F:0B00,X` with (00,00) and 5892 fills `$7F:0B80,X`
// with (FF,FF) -- both through WMADDH = 1 -- and then 58BC-58CA DMA-clears `$7F:0580-1B3F`, which
// covers both, to zero. Their net effect is nothing, and this port applies the clear from
// memLocationLoad, which runs EARLIER; writing them here would leave $7F:0B80 = FF where the ROM
// leaves 0. Transcribed as this note instead of as dead stores that would be wrong.
// ---------------------------------------------------------------------------
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
  for (o = 0; o < nobj; o++) { r[0x1000 + o * 2] = 0x04; r[0x1001 + o * 2] = 0x00; }   // 5772-5786 ⭐
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
