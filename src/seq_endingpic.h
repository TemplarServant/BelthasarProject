// seq_endingpic.h -- the ending's picture. See seq_endingpic.c.
#ifndef CT_SEQ_ENDINGPIC_H
#define CT_SEQ_ENDINGPIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// 1 if the picture of the ending's entry `operand` is ported: all four (10, 11, 14, 15).
int CtEndingPicHas(uint8_t operand);
// $7E:A03D, a list of large sprites at `$15` / `$17` (its `BCS` at $A074 as the code stands).
void CtEndingPicListA03D(uint8_t* w, unsigned list);
// The setup's picture: A = $7E:8538-86A1 (before the script), B = $7E:870E-874E ($9BB8, the HDMA channels, the layers).
void CtEndingPicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtEndingPicSetupB(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
// A script's inline call (`F0 tgt`, D = `page`) that draws: 1 if this file ran it.
int CtEndingPicCall(CtSeqStagePic* sp, uint8_t* w, unsigned page, unsigned tgt, const CtRaceIo* io);
// One vblank hook of $0940 (in the list's slot `slot`, a byte offset); returns what its slot holds afterwards.
unsigned CtEndingPicHook(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned hook, const CtRaceIo* io);
// One of the picture's tasks; 0 for a task this file does not draw.
int CtEndingPicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io);
// The main thread's pass ($7E:3023 `JSR ($3024)`: $8930 once the world is loaded), after each NMI; and its state at the open.
void CtEndingPicThread(CtSeqStagePic* sp, uint8_t* w);
void CtEndingPicReset(void);

#endif
