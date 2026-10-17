// seq_racepic.h -- the motorcycle race's picture, native (stage 2). See seq_racepic.c.
#ifndef CT_SEQ_RACEPIC_H
#define CT_SEQ_RACEPIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// The setup's picture: A = $7E:3D8C and $7E:3B5D-3C4E (before the start values), the tables = $7E:4309, B = $7E:3CEA-3D44
// (after the script), then the CGRAM upload $7E:388B.
void CtRacePicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtRacePicTables(uint8_t* w);
void CtRacePicSetupB(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtRacePicCgram(CtSeqStagePic* sp, const uint8_t* w);
// One vblank hook of the list $0940 (C3/08F7 -> $7E:39BE); returns what its slot holds afterwards.
unsigned CtRacePicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook, const CtRaceIo* io);
// One of the picture's tasks; returns 0 for a task this file does not draw (the caller's own, or not ported yet).
int CtRacePicTask(CtSeqStagePic* sp, uint8_t* w, unsigned task, const CtRaceIo* io);

#endif
