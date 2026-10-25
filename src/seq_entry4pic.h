// seq_entry4pic.h -- sequence entry 4's picture, native (stage 2). See seq_entry4pic.c.
#ifndef CT_SEQ_ENTRY4PIC_H
#define CT_SEQ_ENTRY4PIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// The setup's picture: A = $7E:53BB-54B0 (before the script), B = $7E:54C1-54E9 (after it and the stars).
void CtEntry4PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtEntry4PicSetupB(CtSeqStagePic* sp);
// One vblank hook of $0940 (the common ones and $5654); returns what its slot holds afterwards.
unsigned CtEntry4PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// One of the picture's tasks ($56EB); 0 for a task this file does not draw.
int CtEntry4PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned task);

#endif
