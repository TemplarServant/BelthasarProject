// seq_entry3pic.h -- sequence entry 3's picture, native (stage 2). See seq_entry3pic.c.
#ifndef CT_SEQ_ENTRY3PIC_H
#define CT_SEQ_ENTRY3PIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// The setup's picture: A = $7E:671F-6772 (before the script), B = $7E:679F-6805 (after it; `operand` = $0384).
void CtEntry3PicSetupA(CtSeqStagePic* sp, uint8_t* w);
void CtEntry3PicSetupB(CtSeqStagePic* sp, uint8_t* w, uint8_t operand);
// One vblank hook of $0940; returns what its slot holds afterwards.
unsigned CtEntry3PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// One of the picture's tasks; 0 for a task this file does not draw.
int CtEntry3PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned task, const CtRaceIo* io);

#endif
