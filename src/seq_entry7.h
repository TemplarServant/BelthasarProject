// seq_entry7.h -- sequence entry 7 (`FF 07`, scene 476 = loc 1DC: the Day of Lavos after the final battle is lost), native.
// See seq_entry7.c.
#ifndef CT_SEQ_ENTRY7_H
#define CT_SEQ_ENTRY7_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine

// 1 if `operand` is entry 7 (`FF 07`; bit 6 clear -- no attract call exists).
int CtEntry7StageHas(uint8_t operand);
// The setup's own cost in master cycles, NMI off (`--callrom 7E6248`, CT_CALLROM_NONMI=1; seq_entry7.c).
long CtEntry7StageSetupCycles(void);
// The stage's logic, as CtEntry4Stage*: Open (C3:0000 + $7E:3000 + the setup $7E:6248), one NMI a frame, the
// teardown. Frame returns 2 when the stage resets the console (task $63F5's `JML $00FF00`, a pad press 128 frames before),
// or on the soft reset (L+R+Select+Start); never 1 -- the script has no END.
int  CtEntry7StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEntry7StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEntry7StageClose(uint8_t* w, const CtRaceIo* io);
// The overlay's WRAM palette fade, task $7E:6439 (the ending runs it too).
void CtSeqPalFade6439(uint8_t* w);

#endif
