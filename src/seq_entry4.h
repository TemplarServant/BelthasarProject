// seq_entry4.h -- sequence entry 4 (`FF 24`, scene 301: the Tyranno Lair, Lavos falls), native. See seq_entry4.c.
#ifndef CT_SEQ_ENTRY4_H
#define CT_SEQ_ENTRY4_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine

// 1 if `operand` is entry 4 (`FF 24`; bit 6 clear, as the other story entries).
int CtEntry4StageHas(uint8_t operand);
// The setup's own cost in master cycles, NMI off (`--callrom 7E53BB`, CT_CALLROM_NONMI=1; seq_entry4.c).
long CtEntry4StageSetupCycles(void);
// THE STAGE, its logic (stage 1), as CtEntry3Stage*: Open (C3:0000 + $7E:3000 + the setup $7E:53BB), one NMI a frame, the
// teardown. Frame returns 1 once the script ENDed, 2 on the soft reset, else 0.
int  CtEntry4StageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEntry4StageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEntry4StageClose(uint8_t* w, const CtRaceIo* io);

// C3/02DD with an 8-bit A (`$7E:3443`): the overlay's RNG, the carry threaded, X's low byte -- the rule's virtual beam.
uint8_t CtSeqRng02DD8(uint8_t* w, uint8_t a, int* c, unsigned x);

#endif
