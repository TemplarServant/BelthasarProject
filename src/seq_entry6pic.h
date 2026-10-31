// seq_entry6pic.h -- sequence entry 6's picture. See seq_entry6pic.c.
#ifndef CT_SEQ_ENTRY6PIC_H
#define CT_SEQ_ENTRY6PIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// The setup's picture: A = $7E:78DD-79A1 (before the script), B = $7E:79D0-79F7 (after $8205).
void CtEntry6PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtEntry6PicSetupB(CtSeqStagePic* sp);
// One vblank hook of $0940 (for $7A35 / $79F8 their PPU half); returns what its slot holds afterwards.
unsigned CtEntry6PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// One of the picture's tasks; 0 for a task this file does not draw. $82D1's build ($357B with the entry's pre-routine).
int CtEntry6PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io);
void CtEntry6Pic357B(uint8_t* w, const CtRaceIo* io);
// $7E:3A97, a list of sprites at `$15` / `$17` (the ending's planet takes it too).
void CtSeqPicList3A97(uint8_t* w, unsigned list);
// The main thread's pass $7E:8278, after each NMI.
void CtEntry6PicThread(uint8_t* w);

#endif
