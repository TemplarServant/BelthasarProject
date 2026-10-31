// player.c -- the field player's input -> velocity half (player.h): the joypad poll C2/84F0 + C2/8545, the
// controller C0/881E, the d-pad dispatch C0/88EE/8902 and the edge clamp C0/9175. The $9FF7 table sets the
// facing, not the velocity; velocity is the accumulator $1D2E/$1D30 -> $1D32/$1D33.
#include "player.h"
#include "collision.h"

// C2/8555, one decode pass. `x` is the ROM's X (0, 2 or 4) for the raw word $F0,X and the decoded word
// $F6,X. Y is 0 for all three passes (C2/854C `LDY #00`), so each mask is the plain $7E2993-$299A byte.
static void ctJoyDecodePass(uint8_t* r, int x) {
  // The masks in the ROM's order: the first term rotates furthest and lands in bit 0, $2993 in bit 7.
  static const int MASK[8] = { 0x2998, 0x2996, 0x2997, 0x2994, 0x2999, 0x299A, 0x2995, 0x2993 };
  uint8_t res = 0;                                        // 28555 STZ $F6,X
  for (int b = 0; b < 8; b++) {
    uint8_t raw = r[0x00F0 + x];                          // 28557 LDA $F0,X (re-read every term)
    uint8_t m = r[MASK[b]];
    // `AND m / CMP m`: carry is set only if every bit of the mask is held (a button combination).
    int carry = ((raw & m) >= m);
    res = (uint8_t)((res >> 1) | (carry ? 0x80 : 0));     // 2855F ROR $F6,X
  }
  r[0x00F6 + x] = res;
  r[0x00F7 + x] = r[0x00F1 + x];                          // 285A7 LDA $F1,X / 285A9 STA $F7,X
}

void CtJoypadPoll(uint8_t* r, uint16_t hw) {
  // ---- C2/84F0: hardware -> $00F0-$00F5 -------------------------------------------------------
  int prev = r[0x00F2] | (r[0x00F3] << 8);                // 284FE LDX $F2 / 28500 STX $FE
  r[0x00FE] = (uint8_t)prev; r[0x00FF] = (uint8_t)(prev >> 8);
  uint8_t l = (uint8_t)hw, h = (uint8_t)(hw >> 8);        // $4218, $4219
  r[0x00F2] = (uint8_t)((l & 0xF0) | (h >> 4));           // 28505 AND #F0 / 28507 STA $F2
                                                          // 28510 LDA $4219 / LSR x4 / 28517 TSB $F2
  r[0x00F3] = (uint8_t)(h & 0x0F);                        // 2850C AND #0F / 2850E STA $F3
  int held = r[0x00F2] | (r[0x00F3] << 8);
  int edge = held & ~prev & 0xFFFF;                       // 28527 LDA $F2 / EOR $FE / AND $F2 -- 16-bit
  r[0x00F0] = (uint8_t)edge;      r[0x00F1] = (uint8_t)(edge >> 8);      // 2852D STA $F0
  r[0x00F4] = (uint8_t)edge;      r[0x00F5] = (uint8_t)(edge >> 8);      // 2852F STA $F4
  // 28531-28542 autorepeat. X is 8-bit from 28531 (SEP #10) but A stays 16-bit, so `TSB $F4` at 2853E
  // ORs the whole still-held word into $F4/$F5.
  int rep = held & prev & 0xFFFF;                         // 28535 LDA $F2 / 28537 AND $FE
  int xr;
  if (rep == 0) xr = r[0x00FC];                           // 28539 BEQ ->28540 LDX $FC
  else {
    xr = (r[0x00FD] - 1) & 0xFF;                          // 2853B DEX
    if (xr == 0) {                                        // 2853C BNE ->28542 (else fall to 2853E)
      int f4 = (r[0x00F4] | (r[0x00F5] << 8)) | rep;      // 2853E TSB $F4
      r[0x00F4] = (uint8_t)f4; r[0x00F5] = (uint8_t)(f4 >> 8);
      xr = r[0x00FC];                                     // 28540 LDX $FC
    }
  }
  r[0x00FD] = (uint8_t)xr;                                // 28542 STX $FD
  // ---- C2/8545: $00F0-$00F5 -> the decoded $00F6-$00FB ----------------------------------------
  // Two JSRs and a fall-through: three passes.
  for (int x = 0; x <= 4; x += 2) ctJoyDecodePass(r, x);
}

// The HELD word alone: what C2/84F0 + C2/8545 make of the auto-read word `hw` in $F8 | $F9 << 8 (the $F2 repack, then
// the X = 2 decode pass against the config masks $2993-$299A; $F9 = $F3, the d-pad nibble), without writing $00F0-$00FF.
uint16_t CtJoypadHeldWord(const uint8_t* r, uint16_t hw) {
  static const int MASK[8] = { 0x2998, 0x2996, 0x2997, 0x2994, 0x2999, 0x299A, 0x2995, 0x2993 };
  uint8_t l = (uint8_t)hw, h = (uint8_t)(hw >> 8);
  uint8_t f2 = (uint8_t)((l & 0xF0) | (h >> 4)), f3 = (uint8_t)(h & 0x0F);   // 28505-2850E
  uint8_t res = 0;
  for (int b = 0; b < 8; b++) {                                              // C2/8555 with X = 2
    uint8_t m = r[MASK[b]];
    res = (uint8_t)((res >> 1) | (((f2 & m) >= m) ? 0x80 : 0));
  }
  return (uint16_t)(res | (f3 << 8));                                        // 285A7: $F9 := $F3
}

// C2/85AE, the play clock: the third routine of C2/84D2 (`JSR 85AE` after 84EC and 8545, which are
// CtJoypadPoll), so every NMI that runs the latch runs it (field, world map and battle).
//   SEP #30; D=$0400; LDA $07 BNE rts;            <- $0407 != 0 pauses the clock
//   X=0: INC $00,X; CMP $C285D0,X; BCC rts;       <- limits C2/85D0 = 3C 3C 0A 06 0A 0A
//        STZ $00,X; INX; CPX #06; BCC loop;       <- ripple carry through the six digits
//   LDA #01; STA $06                              <- overflow flag (the menu shows the 99:59 cap)
void CtMenuClockTick(uint8_t* mem) {
  static const uint8_t lim[6] = { 60, 60, 10, 6, 10, 10 };
  if (!mem || mem[0x407]) return;
  for (int i = 0; i < 6; i++) {
    if (++mem[0x400 + i] < lim[i]) return;
    mem[0x400 + i] = 0;
  }
  mem[0x406] = 1;
}

void CtPlayerDpadIntent(uint8_t* ram, uint8_t dpadNib, int strong) {
  uint8_t pos = strong ? 0x20 : 0x10, neg = strong ? 0xE0 : 0xF0;   // +mag / -mag (two's complement)
  int dpos = strong ? 0x20 : 0x10;                                  // signed accumulator delta
  switch (dpadNib & 0x0F) {   // nibble bit order: Up=8, Down=4, Left=2, Right=1
    case 0x1: ram[0x1D2C] = pos; ram[0x1D2E] += dpos; break;                                       // Right (8925)
    case 0x2: ram[0x1D2C] = neg; ram[0x1D2E] -= dpos; break;                                       // Left  (8944)
    case 0x4: ram[0x1D2D] = pos; ram[0x1D30] += dpos; break;                                       // Down  (8982)
    case 0x8: ram[0x1D2D] = neg; ram[0x1D30] -= dpos; break;                                       // Up    (8963)
    case 0x5: ram[0x1D2C] = pos; ram[0x1D2D] = pos; ram[0x1D2E] += dpos; ram[0x1D30] += dpos; break; // Dn+Rt (89A1)
    case 0x6: ram[0x1D2C] = neg; ram[0x1D2E] -= dpos; ram[0x1D2D] = pos; ram[0x1D30] += dpos; break; // Dn+Lf (89D2)
    case 0x9: ram[0x1D2C] = pos; ram[0x1D2E] += dpos; ram[0x1D2D] = neg; ram[0x1D30] -= dpos; break; // Up+Rt (8A07)
    case 0xA: ram[0x1D2C] = neg; ram[0x1D2D] = neg; ram[0x1D2E] -= dpos; ram[0x1D30] -= dpos; break; // Up+Lf (8A3C)
    default: break;                                                                                // 0,3,7,B-F = noop (8924)
  }
}

// C0/9175 screen/map edge tests (C0/5B63-5B8C): carry set on RTS = the edge blocks that axis (returns
// nonzero). X = the player's actor index ($0197). 9175 dispatches down ($1D30 > 0) -> 5B63, up -> 5B71,
// right ($1D2E > 0) -> 5B79, left -> 5B86.
static int edgeDown (const uint8_t* r, int X) { return r[0x1881 + X] >= (uint8_t)(r[0x1D1D] + 1); }   // 5B63: CMP $1881,X vs $1D1D+1
static int edgeUp   (const uint8_t* r, int X) { return (uint8_t)(r[0x1D1C] + 1) >= r[0x1881 + X]; }   // 5B71: A=$1D1C+1 CMP $1881,X
static int edgeRight(const uint8_t* r, int X) { return r[0x1801 + X] >= r[0x1D1B]; }                  // 5B79: CMP $1801,X vs $1D1B
static int edgeLeft (const uint8_t* r, int X) { return r[0x1D1A] >= r[0x1801 + X]; }                  // 5B86: A=$1D1A CMP $1801,X

void CtPlayerControl(uint8_t* ram) {
  ram[0x01EB] = 0x80;
  ram[0x1D2E] = ram[0x1D2A];                          // C0/88E5: previous velocity -> accumulator
  ram[0x1D30] = ram[0x1D2B];
  ram[0x1D2C] = 0; ram[0x1D2D] = 0;                   // clear the direction intent
  if (ram[0x0162] == 0 && ram[0x011F] != 0) {         // C0/881E control gate
    if (ram[0x0138] == 0)                             // C0/88EE inner gate
      CtPlayerDpadIntent(ram, ram[0x00F9] & 0x0F, ram[0x00F8] & 0x02);
  }
  if (ram[0x0120] != 0) CtPlayerCollide(ram);          // C0/8847: tile-collision clamp (8A6D), zeroes
                                                        // $1D2E/$1D30 when the move walks into a wall
  // C0/9175: accumulator $1D2E/$1D30 -> velocity $1D32/$1D33, each gated to 0 at a screen edge.
  int X = ram[0x0197];
  ram[0x1D32] = 0; ram[0x1D33] = 0;
  if (ram[0x1D2E] != 0) {
    int blocked = ((int8_t)ram[0x1D2E] < 0) ? edgeLeft(ram, X) : edgeRight(ram, X);
    if (!blocked) ram[0x1D32] = ram[0x1D2E];
  }
  if (ram[0x1D30] != 0) {
    int blocked = ((int8_t)ram[0x1D30] < 0) ? edgeUp(ram, X) : edgeDown(ram, X);
    if (!blocked) ram[0x1D33] = ram[0x1D30];
  }
}
