// seq_entry5pic.h -- sequence entry 5's picture (the vortex after Magus; its logic is seq_entry5.c).
// disasm/notes/sequence_table.md, "17. ENTRY 5".
#ifndef CT_SEQ_ENTRY5PIC_H
#define CT_SEQ_ENTRY5PIC_H

#include <stdint.h>
#include "seq_race.h"      // CtRaceIo
#include "seq_racepic.h"   // CtSeqStagePic

// $7E:70DD-7156 less the logic's stores: the HDMA channels ($36CC, Y = 7354) before the records, the VRAM DMA and the PPU
// after them.
void CtEntry5PicSetupA(CtSeqStagePic* sp, uint8_t* w);
void CtEntry5PicSetupB(CtSeqStagePic* sp, uint8_t* w);
// $7E:7668-76A0 -- a record's radius `$5D,X` (after the logic's angle; `$F4` / `$F6` its operands).
void CtEntry5PicRadius(CtSeqStagePic* sp, uint8_t* w, unsigned x);
// $7E:76D9-771A -- a record's place `$54,X` / `$55,X` on its orbit (after the logic's facing).
void CtEntry5PicOrbit(CtSeqStagePic* sp, uint8_t* w, unsigned x, const CtRaceIo* io);
// $7E:773E-774D -- a record's new frame: its sixteen tiles into $7E:1C00 ($6E42).
void CtEntry5PicTiles(uint8_t* w, unsigned x, const CtRaceIo* io);
// $7E:7787 -- a record's four sprites ($701E).
void CtEntry5PicOam(uint8_t* w, unsigned x, const CtRaceIo* io);
// $7E:737E's two stores of the mosaic ($2106).
void CtEntry5PicMosaic(CtSeqStagePic* sp, uint8_t v);
// The picture's own tasks: 7325 (the windows' circles), 73C6 (the matrix), 73AA (the wave), 72FF (the radii). 1 = it was one.
int CtEntry5PicTask(CtSeqStagePic* sp, uint8_t* w, unsigned t, const CtRaceIo* io);
// The vblank hooks: 783C (the warp's upload), 7868 (a record's tiles), 78AB (the colour cycle) and the common three.
unsigned CtEntry5PicHook(CtSeqStagePic* sp, uint8_t* w, unsigned hook);
// The V-IRQ (C3/0529): the frame just drawn is blank from line 212 down; INIDISP = 80 until the next NMI.
void CtEntry5PicIrq(CtSeqStagePic* sp, uint8_t* rgb);
// $7E:7441 -- one pass of the main loop's renderer: the room's tilemap ($0C00) through the matrix into $1400, `INC $3B`.
void CtEntry5PicRender(uint8_t* w);

#endif
