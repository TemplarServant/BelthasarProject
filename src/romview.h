// romview.h -- the ROM's small tables, by name, from the asset container (CT_ASSET_TABLE).
//
// The ROM-free runtime (memory rom-free-runtime-goal) moves each system's reads into ct_assets.dat. A table the engine
// indexes by the ROM's own address arithmetic -- the trig and noise tables of bank C0, FD's tile-animation and
// colour-slot programs, FF's window tiles, text palettes and naming grid -- is packed whole under its start offset
// (assets/extract_assets.py pack_tables, its extent derived from the code that reads it or by the closure over every
// location); a read of the ROM at an offset one of them covers is then served from it. Offsets no table covers still
// come from the ROM (CT_ROM_CENSUS counts them).
#pragma once
#include <stdint.h>
#include "assets.h"

// From now on, serve reads from `as`'s CT_ASSET_TABLE entries (NULL: the ROM only).
void CtRomViewUse(const CtAssets* as);
// The byte at ROM file offset `off`: a table's, else rom[off] (0 outside both).
uint8_t CtRomByte(const uint8_t* rom, long romLen, long off);
// `len` bytes at `off`: the table's own when one covers them all, else rom + off (NULL outside the ROM).
const uint8_t* CtRomPtr(const uint8_t* rom, long romLen, long off, long len);
// `n` bytes from `off` into dst, byte by byte through CtRomByte.
void CtRomCopy(uint8_t* dst, const uint8_t* rom, long romLen, long off, long n);
// 1 when a table covers `off`.
int CtRomViewHas(long off);
// The whole check for ct.exe --tabassets: every table byte against the ROM's. Returns the bytes compared, -1 at a
// difference (printed).
long CtRomViewCompare(const uint8_t* rom, long romLen, const CtAssets* as);
