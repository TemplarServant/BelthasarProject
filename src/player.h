// player.h -- the field player's input-driven mover (the lead party actor), from the bank C0 per-frame
// player controller C0/881E and its subroutines.
#ifndef CT_PLAYER_H
#define CT_PLAYER_H

#include <stdint.h>

// The d-pad dispatch (C0/8902 jump table, handlers 8925/8944/8982/8963 and the four diagonals). `dpadNib`
// uses Up=8, Down=4, Left=2, Right=1; `strong` != 0 gives +-0x20, else +-0x10. Writes the direction intent
// $1D2C (X) / $1D2D (Y) and adds the same magnitude to the velocity accumulators $1D2E (X) / $1D30 (Y).
// `ram` is the WRAM image ($7E at offset 0).
void CtPlayerDpadIntent(uint8_t* ram, uint8_t dpadNib, int strong);

// The per-frame joypad poll: C2/84F0 (hardware -> raw words $00F0-$00F5), then C2/8545 (-> the decoded
// words $00F6-$00FB the engine reads). `hw` is controller 1's auto-joypad word: low byte $4218 (A/X/L/R in
// bits 7..4), high byte $4219 (B/Y/Select/Start/Up/Down/Left/Right in bits 7..0).
// C2/84F0 repacks: $F2 = ($4218 & $F0) | ($4219 >> 4), $F3 = $4219 & $0F (hence $00F9's Up=8 .. Right=1
// nibble). The new-press edge (held & ~previous) goes to $F0/$F1 and $F4/$F5; an autorepeat pulse from the
// counter $FC/$FD is ORed into $F4/$F5; $FE/$FF keep the previous held word.
// C2/8545 runs the decode C2/8555 three times (X = 0, 2, 4: two JSRs and a fall-through), turning $F0/$F2/$F4
// into $F6/$F8/$FA and copying each raw high byte to $F7/$F9/$FB, so $00F9 is a copy of $00F3. Each
// decoded bit is `AND mask / CMP mask / ROR` against the button-config bytes $7E2993-$7E299A; the carry is
// set only if every bit of the mask is held, which is what makes multi-button masks work.
// Not ported: the `BIT $4212` auto-joypad wait (284F7) and the L+R+Select+Start soft reset at 2851B.
void CtJoypadPoll(uint8_t* ram, uint16_t hw);
// The decoded held word ($F8 | $F9 << 8) that CtJoypadPoll would leave for `hw`, without writing $00F0-$00FF.
uint16_t CtJoypadHeldWord(const uint8_t* ram, uint16_t hw);
void CtMenuClockTick(uint8_t* mem);   // C2/85AE, the play clock ($0400-$0406), the latch's third routine

// The per-frame player controller (C0/881E, input -> velocity): seeds the accumulators $1D2E/$1D30 from
// the previous velocity $1D2A/$1D2B (C0/88E5), clears the intent $1D2C/$1D2D and, when the control gate
// is open ($0162 == 0, $011F != 0, $0138 == 0), runs the d-pad dispatch (C0/88EE). With $0120 != 0 it then
// runs the tile collision C0/8A6D (CtPlayerCollide), as the ROM does at 8840-8847. Last, C0/9175 publishes
// the velocity $1D32/$1D33 from the accumulators, each zeroed when its screen edge blocks (bounds
// $1D1A-$1D1D against the player tile $1801/$1881, X = $0197).
// The camera legs that follow in C0/881E (C0/99DE, 91AC, 93E1) run in CtSceneCameraTick (ct_vm.c).
void CtPlayerControl(uint8_t* ram);

#endif
