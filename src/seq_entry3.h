// seq_entry3.h -- sequence entry 3 called from the story (`FF 23`, scenes 102-107 after the Blackbird), native. See
// seq_entry3.c.
#ifndef CT_SEQ_ENTRY3_H
#define CT_SEQ_ENTRY3_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine

// 1 if `operand` is entry 3 from the story (bit 6 clear: the attract's `FF 63` keeps its translated stage).
int CtEntry3StageHas(uint8_t operand);
// The setup's own cost in master cycles, NMI off (`--callrom 7E671F`, CT_CALLROM_NONMI=1; seq_entry3.c).
long CtEntry3StageSetupCycles(void);
// THE STAGE, its logic (stage 1), as CtEndingStage*: Open (C3:0000 + $7E:3000 + the setup $7E:671F), one NMI a frame,
// the teardown. Frame returns 1 once the script ENDed, 2 on the soft reset, else 0.
int  CtEntry3StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEntry3StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEntry3StageClose(uint8_t* w, const CtRaceIo* io);

#endif
