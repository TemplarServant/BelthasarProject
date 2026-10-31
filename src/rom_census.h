// rom_census.h -- which pages of the ROM image a run reads (the ROM-free runtime's meter; docs/ROM.md).
//
// CT_ROM_CENSUS=<file> (`%d` = the process id): every ROM image the engine keeps (rom_verify.c's, battle.c's) is moved
// into pages the OS refuses to read; the first read of each 4 KB page traps, is recorded with the code address that
// made it, and the page is opened. At exit the file gets one line per page read, `PAGE IMAGE CODEADDR` (the page's ROM
// file offset in hex, which image, the reading instruction relative to the executable's base), and
// `python tools/rom_census.py` sums runs per bank and per subsystem. Unset, nothing changes: the image is returned as is.
#pragma once
#include <stdint.h>

// The image to use from now on: `img` itself, or (CT_ROM_CENSUS) a guarded copy of its `len` bytes. `name` labels it.
const uint8_t* CtRomCensusWrap(const uint8_t* img, long len, const char* name);
uint8_t* CtRomCensusWrapMutable(uint8_t* img, long len, const char* name);
