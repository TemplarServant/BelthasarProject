// seq_ending.h -- the ending's $C3 stage (sequence entries 10, 11, 14, 15: `FF 2A` / `FF 2B` / `FF 2E` / `FF 2F`), native.
// See seq_ending.c.
#ifndef CT_SEQ_ENDING_H
#define CT_SEQ_ENDING_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo and the shared $C3 machine (CtSeqStageBegin / CtSeqStageNmi / CtSeqScript)

// 1 if `operand` (the `FF` op's byte, $0384) is one of the ending's entries -- 10, 11, 14 or 15 ($7E:3043's table points all
// four at $7E:8538) -- called from the story (bit 6 clear).
int CtEndingStageHas(uint8_t operand);

// The setup's own cost in master cycles, NMI off (`CT_CALLROM_NONMI=1 ct.exe --callrom 7E8538 --pokewram 0384=<op>` from the
// cartridge inside `FF 2B`'s opening, saves/ending_re: the overlay in WRAM): it includes page A's first run and the inline
// picture loads it makes (8790 / 87CC). 0 for an operand that is not the ending's.
long CtEndingStageSetupCycles(uint8_t operand);

// THE STAGE, its logic (stage 1): C3:0000 + $7E:3000 + the setup $7E:8538 up to the main loop (the overlay, the common init,
// the entry's two script pages and its credits pointer, the brightness shadows, page A run once, the first NMI); one NMI a
// frame (`joy` = the $4218 word, as CtRaceStageFrame); the teardown. The song is the field's (bit 5): the scripts wait on its
// markers through io->port. Open returns -1 for what it does not port. Frame returns 1 once the stage ENDed (`FF 2B`'s fade,
// $7E:34E3), 2 on the soft reset (`FF 2E`/`FF 2F` end on "The End" with $0396 = 0: the player resets), else 0.
int  CtEndingStageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
int  CtEndingStageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtEndingStageClose(uint8_t* w, const CtRaceIo* io);

#endif
