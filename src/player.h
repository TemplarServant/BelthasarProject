// player.h -- the CT field PLAYER input-driven mover (the lead party actor, e.g. idx 02). Ported from
// the bank-C0 per-frame player controller C0/881E and its sub-routines (see the player-mover RE notes).
#ifndef CT_PLAYER_H
#define CT_PLAYER_H

#include <stdint.h>

// The d-pad dispatch (C0/8902 jump table + the handlers 8925/8944/8982/8963 + the 4 diagonals): given
// the d-pad nibble (bit order Up=8, Down=4, Left=2, Right=1) and the strength flag (strong!=0 -> +-0x20,
// weak -> +-0x10), writes the signed direction INTENT $1D2C (X) / $1D2D (Y) and adds the same magnitude
// into the running velocity ACCUMULATOR $1D2E (X) / $1D30 (Y). `ram` is the WRAM image ($7E at offset 0).
void CtPlayerDpadIntent(uint8_t* ram, uint8_t dpadNib, int strong);

// The per-frame JOYPAD POLL: C2/84F0 (hardware -> the raw button words $00F0-$00F5) followed by
// C2/8545 (-> the DECODED words $00F6-$00FB that the rest of the engine actually reads). `hw` is the
// auto-joypad result for controller 1 in HARDWARE layout -- the 16-bit value whose low byte is $4218
// (A/X/L/R in bits 7..4) and whose high byte is $4219 (B/Y/Select/Start/Up/Down/Left/Right, bit 7..0).
//
// C2/84F0 REPACKS the two pads into one word, and the repack is why $00F9's nibble is Up=8 Down=4
// Left=2 Right=1: `$F2 = ($4218 & $F0) | ($4219 >> 4)` and `$F3 = $4219 & $0F`. It then derives the
// NEWLY-PRESSED edge into $F0/$F1 and $F4/$F5 as `held & ~previous`, and folds an autorepeat pulse
// into $F4/$F5 off the counter $FC/$FD. $FE/$FF carry the previous frame's held word.
//
// C2/8545 runs C2/8555 THREE times (two JSRs at 2854F/28552 and then a FALL-THROUGH into the same
// body at 28555), with X = 0, 2, 4 -- so the three raw words $F0/$F2/$F4 become the three decoded
// words $F6/$F8/$FA, and `LDA $F1,X / STA $F7,X` copies each raw HIGH byte verbatim to $F7/$F9/$FB.
// $00F9 is therefore a straight copy of $00F3, the HELD d-pad nibble -- not a computed value.
// Each decoded byte is eight `AND mask / CMP mask / ROR` steps against the remappable button-config
// bytes at $7E2993-$7E299A, in the order 2998, 2996, 2997, 2994, 2999, 299A, 2995, 2993 (so the FIRST
// term ends up in bit 0). `CMP` after `AND` sets carry iff EVERY bit of the mask is held, which is
// what makes multi-button masks work.
//
// NOT MODELLED, and neither is reachable here: the `BIT $4212` auto-joypad-ready spin (284F7), and
// the `LDA $4218 / CMP #3030 / JML C0/FF00` soft-reset combo at 2851B (L+R+Select+Start).
void CtJoypadPoll(uint8_t* ram, uint16_t hw);
// The decoded HELD word ($F8 | $F9 << 8) that CtJoypadPoll would leave for `hw`, computed without writing $00F0-$00FF.
uint16_t CtJoypadHeldWord(const uint8_t* ram, uint16_t hw);
void CtMenuClockTick(uint8_t* mem);   // C2/85AE, the play clock ($0400-$0406), the latch's third routine

// The per-frame PLAYER CONTROLLER (C0/881E body, input -> velocity half): seeds the accumulators
// $1D2E/$1D30 from the previous velocity $1D2A/$1D2B (C0/88E5), clears the intent $1D2C/$1D2D, and -- if
// the control gate is open ($0162==0 && $011F!=0 && $0138==0) -- runs the d-pad dispatch (C0/88EE), then
// derives the velocity $1D32/$1D33 from the accumulators with screen-edge clamping (C0/9175): $1D32 =
// $1D2E unless the matching screen edge blocks (then 0), $1D33 = $1D30 likewise. Reads the d-pad
// $00F9 / strength $00F8, the gate flags, the clamp index $0197 + bounds $1D1A-$1D1D + the player tile
// $1801/$1881. Writes $1D2C/$1D2D/$1D2E/$1D30/$1D32/$1D33 in `ram`.
//
// C0/8A6D *IS* MODELLED, AT ITS REAL SEAM. This comment used to say it was not ("no-ops in open
// field"), and that stale sentence cost a whole derivation pass: it was read as evidence that the
// wall clamp could not be interposed and that CtPlayerControl would have to be split. It does not,
// and it need not. The ROM is `8840 LDA $0120 / BEQ ->884C / 8845 SEP #10 / 8847 JSR C0/8A6D /
// 884A REP #10 / 884C JSR C0/9175`, and player.c calls CtPlayerCollide under exactly that $0120 gate,
// between the d-pad dispatch and the publish. Docs that describe missing work outlive the work.
// ⚠ What IS still missing is one level down: C0/8A6D's four DIAGONAL handlers (8E21/8EF1/8FC1/909C)
// are unported, so a diagonal velocity passes through unclamped (see CtPlayerCollide in collision.c).
// Also unmodelled here: C0/99DE, C0/91AC and C0/93E1, the three legs after 9175 in C0/881E.
void CtPlayerControl(uint8_t* ram);

#endif
