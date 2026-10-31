// seq_gwarp.h -- the gate warp: op 0xFF $80..$8F's program in bank $FD (C0/3FE4-3FFC `JML $FD:FFF1 / FFEB / FFE8 /
// FFE5` -> FD/DA00, DA5F, DABE, DB1D), its logic over the 128 KB WRAM and its picture through the native compositor
// (seq_pic.h). src/xlat/gwarp_fd.c, the translated program, remains as a reference.
// disasm/notes/gate_warp_fd_family.md sec. 11.
#ifndef CT_SEQ_GWARP_H
#define CT_SEQ_GWARP_H

#include <stdint.h>
#include "seq_pic.h"

typedef struct CtGwarp {
  const uint8_t* rom; long romLen;   // the user's ROM (headerless HiROM image): every table the program reads comes from it
  uint8_t  op;                       // the operand ($80..$8F)
  int      entry;                    // 0 = FD/DA00 (FF 80), 1 = DA5F (FF 81), 2 = DABE (FF 82), 3 = DB1D (FF 83 and up)
  int      frames;                   // CtGwarpFrame calls so far (1 = the hand-off's frame)
  int      done;                     // 1 once the program reached its `JML $C0:0000`
  // The 65816's registers where the program leaves them in memory: the accumulator's high byte (XBA / TAX / TAY carry it into
  // addresses and colours), X and Y (the stack pushes), S (the stack page), the WRAM port's address ($2181-$2183), the V flag
  // and C (the P byte the NMI pushes).
  uint16_t c, x, y, s;
  uint32_t wm;
  int      v, cy;                     // V and C as the next NMI finds them (the wait's flags)
  uint16_t mulA;                     // $4202 (WRMPYA); $4204/$4205 the dividend and the results, as LakeSnes keeps them
  uint16_t divA, mulRes, divRes;
} CtGwarp;

// 1 for an operand C0/3FDC sends to bank $FD ($80..$8F: ct_vm.c's op 0xFF split).
int CtGwarpHas(uint8_t op);
// The hand-off (C0/3FE8.. `JML`): `sp` is the stage's picture, its VRAM / CGRAM the field's as the console leaves them.
// Returns -1 without the ROM.
int CtGwarpOpen(CtGwarp* g, uint8_t* w, uint8_t op, CtSeqStagePic* sp, const uint8_t* rom, long romLen);
// One frame, vblank to vblank (as CtXlatStageFrame stops): the program's work under the rule, then its picture into `rgb`
// (256 x 224 RGB888, may be NULL). Returns 1 on the frame it reaches `JML $C0:0000` -- nothing is drawn then (the program
// ends inside the vblank: the screen keeps the last frame) -- else 0.
int CtGwarpFrame(CtGwarp* g, uint8_t* w, CtSeqStagePic* sp, uint8_t* rgb);

#endif
