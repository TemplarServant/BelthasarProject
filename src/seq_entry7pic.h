// seq_entry7pic.h -- sequence entry 7's picture. See seq_entry7pic.c.
#ifndef CT_SEQ_ENTRY7PIC_H
#define CT_SEQ_ENTRY7PIC_H
#include <stdint.h>
#include "seq_pic.h"
#include "seq_race.h"

// The setup's picture: A = $7E:6248-62EB (before the script), B = $7E:6301-6333 (after it).
void CtEntry7PicSetupA(CtSeqStagePic* sp, uint8_t* w, const CtRaceIo* io);
void CtEntry7PicSetupB(CtSeqStagePic* sp);
// One vblank hook of $0940 (the common three); returns what its slot holds afterwards.
unsigned CtEntry7PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// The text task $64BF; 0 for any other task. And one live sparkle's draw ($7E:6629, C3/0DBC from $6640).
int CtEntry7PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned task);
void CtEntry7PicSparkle(uint8_t* w);
// Shared with the ending ($7E's one overlay): $7E:66B0 (packed 2bpp tiles from `$0303` in $7F widened to 4bpp at the VRAM
// address, `tiles` of them) and $7E:64CA (a string's sprites from X: the text's pen in the operands $64D6 / $64DA).
void CtSeqPicWiden66B0(CtSeqStagePic* sp, const uint8_t* w, unsigned tiles);
void CtSeqPicText64CA(uint8_t* w, unsigned x);

#endif
