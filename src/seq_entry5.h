// seq_entry5.h -- sequence entry 5 (`$7E:7092`, A = 85 from C0/260E: the vortex after Magus), native. See seq_entry5.c.
#ifndef CT_SEQ_ENTRY5_H
#define CT_SEQ_ENTRY5_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine

// 1 if `operand` is entry 5 (C0/260E's `LDA #85`; no script site calls it with `FF`).
int CtEntry5StageHas(uint8_t operand);
// The setup's own cost in master cycles, NMI off (`--callrom 7E7092`, CT_CALLROM_NONMI=1; seq_entry5.c).
long CtEntry5StageSetupCycles(void);
// The stage's logic, as CtEntry4Stage*: Open (C3:0000 + $7E:3000 + the setup $7E:7092), one NMI a frame, the teardown.
// Frame returns 1 once the script ENDed, 2 on the soft reset, else 0.
int  CtEntry5StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEntry5StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEntry5StageClose(uint8_t* w, const CtRaceIo* io);

#endif
