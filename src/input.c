// input.c -- CtReadJoypad: the CT per-frame joypad read (see input.h). Ported + validated byte-exact
// vs the oracle (--checkjoy). The end-to-end bit flow (verified against the vendored LakeSnes
// snes_doAutoJoypad + the ROM C2/8500..85AD):
//   currentState (logical bits B=0x01..R=0x800)
//     --auto-joypad 16-bit reversal (bit N -> portAutoRead bit 15-N)--> $4218/$4219 (the standard
//       SNES JOY1 layout: $4218 = {A7,X6,L5,R4,0,0,0,0}; $4219 = {B7,Y6,Sel5,St4,Up3,Dn2,Lf1,Rt0})
//     --C2/8500: $F2 = ($4218&$F0) | ($4219>>4)  (so $F2 holds A/X/L/R + B/Y/Sel/St; NOT the d-pad)
//                $F3 = $4219 & $0F               (the d-pad nibble Up/Down/Left/Right)
//                $FE/$FF = previous $F2/$F3 ; $F0=$F4 = ($F2 ^ $FE) & $F2 (newly pressed)
//     --C2/8545: $F6 = remapped-newly-pressed via the live WRAM masks $2993-$299A ; $F7 = $F1
#include "input.h"

void CtReadJoypad(uint8_t* ram, uint16_t held) {
  // (1) SNES auto-joypad (LakeSnes snes_doAutoJoypad): a full 16-bit reversal of the held word.
  uint16_t cs = held, port = 0;
  for (int i = 0; i < 16; i++) { port |= (uint16_t)((cs & 1u) << (15 - i)); cs >>= 1; }
  uint8_t r4218 = (uint8_t)(port & 0xFF), r4219 = (uint8_t)(port >> 8);

  // (2) C2/84FE-8500: $FE/$FF = the PREVIOUS frame's $F2/$F3 (LDX $F2 ; STX $FE, 16-bit X).
  uint8_t prevF2 = ram[0xF2], prevF3 = ram[0xF3];

  // (3) C2/8502-8517: build the held byte $F2 (+ folded B/Y/Sel/St) and the d-pad nibble $F3.
  uint8_t f2 = (uint8_t)(r4218 & 0xF0);   // A,X,L,R (bits 7-4)
  uint8_t f3 = (uint8_t)(r4219 & 0x0F);   // d-pad Up,Down,Left,Right (bits 3-0)
  f2 |= (uint8_t)(r4219 >> 4);            // TSB: fold B,Y,Select,Start into the low nibble

  // (4) C2/8527-852F (16-bit, after the REP #$30): newly-pressed = (held ^ prevHeld) & held, computed
  // 16-bit over $F2/$F3: $F0 = pressed buttons (low), $F1 = pressed d-pad (high). Copied to $F4/$F5
  // (the C2/8531 autofire loop may then OR extra bits into $F4 only).
  uint8_t fe = prevF2, ff = prevF3;
  uint8_t f0 = (uint8_t)((f2 ^ fe) & f2);
  uint8_t f1 = (uint8_t)((f3 ^ ff) & f3);
  ram[0xFE] = fe; ram[0xFF] = ff;
  ram[0xF2] = f2; ram[0xF3] = f3;
  ram[0xF0] = f0; ram[0xF1] = f1;
  ram[0xF4] = f0; ram[0xF5] = f1;

  // (6) C2/8545 (sub $8555): $F6 = remapped newly-pressed. STZ $F6; 8x { LDA $F0; AND mask; CMP mask;
  // ROR $F6 } over the live remap masks $2993-$299A (read with DBR=$7E), in this exact order; AND-then
  // -CMP-same-mask sets carry iff (src & mask) == mask. Then $F7 = $F1.
  static const uint16_t MASK[8] = { 0x2998, 0x2996, 0x2997, 0x2994, 0x2999, 0x299A, 0x2995, 0x2993 };
  uint8_t src = ram[0xF0], f6 = 0;
  for (int b = 0; b < 8; b++) {
    uint8_t m = ram[MASK[b]];
    uint8_t carry = ((src & m) == m) ? 1u : 0u;
    f6 = (uint8_t)((carry << 7) | (f6 >> 1));   // ROR: carry into bit 7
  }
  ram[0xF6] = f6; ram[0xF7] = ram[0xF1];
}
