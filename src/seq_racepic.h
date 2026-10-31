// seq_racepic.h -- the motorcycle race's picture. See seq_racepic.c.
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
// One of the picture's tasks (in the list's slot `slot`, a byte offset); returns 0 for a task this file does not draw
// (the caller's own, or not ported).
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
// $7E:357B, the mode-7 per-line matrix builder (D = $0A00): `pre` is the entry's routine its `JSR` at $35FD names -- it leaves the
// first distance X and Y, and writes the loop's tail ($3634) and limit ($3637).
typedef void (*CtSeqPic357BPre)(uint8_t* w, unsigned* x, unsigned* y, unsigned* wm);   // wm: WMADD after it (17-bit)
void CtSeqPic357B(uint8_t* w, const CtRaceIo* io, CtSeqPic357BPre pre);
// C3/09E9's code (written through WMDATA from wm: X the tiles' page, Y the rows' base) and what it does when run (`JSR $B574` with
// the data bank $7F); C3/0307's scaler over the request in $1F..$2D (D = 0, the data bank $7F), the carry it enters with.
void CtSeqPicGen09E9(uint8_t* w, unsigned wm, unsigned x, unsigned y);
void CtSeqPicRun09E9(uint8_t* w, unsigned x, unsigned y);
void CtSeqPicScale0307(uint8_t* w, int carry);
void CtSeqPicRows37A6(uint8_t* w);                                    // $7E:37A6: $7F:0200's 64 rows of 64 doubled at $7F:2200
void CtSeqPicCopy36AA(uint8_t* w, const CtRaceIo* io, unsigned x, unsigned y);   // $7E:36AA
void CtSeqPicOam0DBC(uint8_t* w, unsigned rec);   // C3/0DBC: one small sprite from the record $7E:rec
void CtSeqPicOamHigh3870(uint8_t* w);             // $7E:3870: the high table's byte `($13)` shifted to the next sprite (`$11`)

#endif
