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
// One of the picture's tasks (in the list's slot `slot`, a byte offset); returns 0 for a task this file does not draw (the caller's own, or not ported yet).
int CtRacePicTask(CtSeqStagePic* sp, uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io);

// C3/09E9's code at $7E:B574 (the setup B's first step), and the main thread's pass after each NMI ($7E:51D1).
void CtRacePicGenB574(uint8_t* w);
void CtRacePicThread(CtSeqStagePic* sp, uint8_t* w);

// Shared by the stages' pictures: the multiplier's product (M7A x M7B's high byte, $2134-$2136), $7E:36CC's HDMA channels
// from a 5-byte record table, and the common vblank hooks $380B (OAM), $3843 (the OAM shadow cleared), $388B (CGRAM) --
// returns 0 for any other hook.
long CtSeqPicMpy(const CtSeqStagePic* sp);
void CtSeqPicHdma36CC(CtSeqStagePic* sp, const uint8_t* w, unsigned table);
int CtSeqPicCommonHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// ...and the overlay's packet $7E:3691 (FE:0000,X into `$0303-$0305` through C3/0557), the DMA $7E:38E7 (`$F0` / `$F2` from
// `$0303-$0306`), C3/0D5E (a list of large sprites at `$15` / `$17` into the OAM shadow).
void CtSeqPicUnpack(uint8_t* w, const CtRaceIo* io, unsigned x);
void CtSeqPicDma38E7(CtSeqStagePic* sp, const uint8_t* w);
void CtSeqPicOam0D5E(uint8_t* w, unsigned list);
void CtSeqPicOam0DBC(uint8_t* w, unsigned rec);   // C3/0DBC: one small sprite from the record $7E:rec

#endif
