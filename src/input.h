// input.h -- the CT per-frame joypad read (the field/menu input handler). Ported from the bank-C2
// service C2/8500 (build held/pressed) + C2/8545 (the button remap), fed by the SNES auto-joypad.
#ifndef CT_INPUT_H
#define CT_INPUT_H

#include <stdint.h>

// Port of CT's per-frame joypad read. `held` is the held-button word the harness writes to the SNES
// controller (s->input1->currentState; LOGICAL/serial bit order: B=0x01, Y=0x02, Select=0x04,
// Start=0x08, Up=0x10, Down=0x20, Left=0x40, Right=0x80, A=0x100, X=0x200, L=0x400, R=0x800). Models
// the chain the real hardware/oracle runs: the SNES auto-joypad (a 16-bit reversal of `held` into
// $4218/$4219), then the game routine C2/8500 that builds the held byte $00F2 + the d-pad nibble
// $00F3 + the prev-held $00FE/$00FF + the newly-pressed $00F0/$00F4, then the C2/8545 remap that
// builds the remapped newly-pressed $00F6 (via the live remap-mask config at $2993-$299A) and copies
// $00F1 to $00F7. Reads/writes those bytes in `ram` (WRAM image: $7E.. at offset 0). $00F0..$00FF
// are direct-page (low WRAM). $00FE must hold the PREVIOUS frame's $00F2 on entry (the caller keeps
// the per-frame state in `ram`, as the game does).
void CtReadJoypad(uint8_t* ram, uint16_t held);

#endif
