// oam.h -- the field sprite -> OAM build (C0/B276..BA61; disasm/notes/oam.md). Reads the sprite-object WRAM and
// produces the SNES OAM for the visible field sprites.
#ifndef CT_OAM_H
#define CT_OAM_H

#include <stdint.h>

// Build the field sprite OAM (stages 1+2) from WRAM `ram` ($7E.. at offset 0, $7F.. at 0x10000).
// Iterates the live sprite objects via the Y-sorted list $0E00 + the $0E81 chains; for each it
// assembles its OBJ tiles (stage 1: finalX/Y = object X/Y + the per-tile template offsets) and
// places the 4 OAM bytes (X,Y,tile,attr) + the high byte into the priority-grouped OAM (stage 2,
// group by $0F80 & 0x0C). Writes the visible objects' slots into oamMain[512]/oamHigh[32] and
// marks them written in wroteMain[512]/wroteHigh[32] (any of the four may be NULL to skip the
// mask). The caller owns the persistent OAM + the hide/clear of the unused slots.
void CtBuildFieldOAM(const uint8_t* ram, uint8_t* oamMain, uint8_t* oamHigh,
                     uint8_t* wroteMain, uint8_t* wroteHigh);

// Compute a sprite object's on-screen coords (the $0A00 / $0A80 that the OAM build consumes) from
// its actor world position ($1800 / $1880, sub-pixel) minus the camera, where the camera origin
// is (scroll $1D0A / $1D0E) * 8 + (fine scroll $1D87 / $1D89). C0/AB45..ABA1. Results are 9-bit ($0..$1FF).
void CtActorScreenCoords(const uint8_t* ram, int idx, int* outScreenX, int* outScreenY);

// One OBJ tile of a sprite's assembly: the position offsets + the OAM tile/attr.
typedef struct { int16_t xoff; int8_t yoff; uint8_t tile; uint8_t attr; } CtObjTile;

// Decode sprite `idx`'s OBJ assembly (4 tiles) for its current animation frame (the decoder
// C0/CC22..CDxx). The frame's per-tile X/Y offsets come from
// ROM at the long pointer $D3 = bank $1300[idx] : ($1380[idx] + frame($0F01[idx]) * 40), offset
// +0x20; the tile# is $0D00[idx] + t*2; the attr is $0F81[idx] | $0D01[idx] | $0C00/$0C01[idx]
// (the 0x10 position-based priority bit is added later by the C0/9B4D stage, not here). Fills
// out[4]. `rom`/`romLen` are the raw HiROM image.
void CtDecodeSpriteAssembly(const uint8_t* ram, const uint8_t* rom, int romLen, int idx, CtObjTile out[4]);

// C0/ABA2..AC68 -- the field move heading (0..255 brads, 0x40 = 90 deg). It takes no deltas: it reads the four bytes
// the caller stages ($01F0 curX, $01F1 curY, $01F2 tgtX, $01F3 tgtY), compares them with an unsigned borrow and writes
// $01EE (and $01EF, $01F4/$01F5, $01F6, $01F7); returns the heading. A quadrant base from the borrows, an axis swap of
// the table row/col in two quadrants, then a fine angle from the atan2 table at $C0:F300 with the index folded by a
// per-bit magnitude dispatch (bit 6 -> >>2, bit 5 -> >>1, else >>0). Callers stage the four bytes first, as C0/5129
// (5140/5147/514C) and op 0x96 (4F71/4F78/4F7F/4F84) do. `rom` is the raw HiROM image. `ct.exe --sweepaba2` checks it.
int CtMoveDirAngle(uint8_t* ram, const uint8_t* rom);

#endif
