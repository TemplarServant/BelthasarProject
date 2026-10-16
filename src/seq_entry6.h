// seq_entry6.h -- sequence entry 6 (`FF 26`, scene 432 = loc 1B0: the Epoch flies into Lavos's shell), native. See
// seq_entry6.c.
#ifndef CT_SEQ_ENTRY6_H
#define CT_SEQ_ENTRY6_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine

// 1 if `operand` is entry 6 (`FF 26`; bit 6 clear -- no attract call exists).
int CtEntry6StageHas(uint8_t operand);
// The setup's own cost in master cycles, NMI off (`--callrom 7E78DD`, CT_CALLROM_NONMI=1; seq_entry6.c).
long CtEntry6StageSetupCycles(void);
// THE STAGE, its logic (stage 1), as CtEntry4Stage*: Open (C3:0000 + $7E:3000 + the setup $7E:78DD), one NMI a frame, the
// teardown. Frame returns 1 once the script ENDed ($34E3), 2 on the soft reset, else 0.
int  CtEntry6StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEntry6StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEntry6StageClose(uint8_t* w, const CtRaceIo* io);

#endif
