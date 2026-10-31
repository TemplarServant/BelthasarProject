// input.h -- the per-frame joypad read (the field/menu input handler): C2/8500 (build held/pressed) and
// C2/8545 (the button remap), fed by the SNES auto-joypad.
#ifndef CT_INPUT_H
#define CT_INPUT_H

#include <stdint.h>

// `held` is the held-button word written to the SNES controller (s->input1->currentState; logical/serial
// bit order: B=0x01, Y=0x02, Select=0x04, Start=0x08, Up=0x10, Down=0x20, Left=0x40, Right=0x80, A=0x100,
// X=0x200, L=0x400, R=0x800). The chain: the auto-joypad (a 16-bit reversal of `held` into $4218/$4219),
// C2/8500 (held $00F2, d-pad nibble $00F3, previous $00FE/$00FF, newly pressed $00F0/$00F4), then C2/8545
// (remapped newly pressed $00F6 via the remap masks $2993-$299A; $00F7 = $00F1). `ram` is the WRAM image
// ($7E at offset 0). $00F2/$00F3 must hold the previous frame's values on entry, as in the game.
void CtReadJoypad(uint8_t* ram, uint16_t held);

#endif
