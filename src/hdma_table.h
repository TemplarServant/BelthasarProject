// hdma_table.h -- the field's HDMA table builder FD/C2C1, native (disasm/notes/field_hdma_tables.md).
#pragma once
#include <stdint.h>

// `JSL FD/C2C1` over a 128 KB WRAM image ($7E:0000 at wram128[0], $7F:0000 at wram128[0x10000]): builds one of the two
// table sets ($0153 bit 0 clear -> set B at $7F:1238.., set -> set A at $7F:0F80..) for the window `$0126` / `$0127`
// names, then flips the bit. `rom` is the user's headerless image: the window-wave offsets C0:FF30.. are read from it.
// Returns 0, or -1 when it built nothing (no WRAM, no ROM, or a `$0126` the ROM's mode tables have no handler for --
// the bit is left alone then; the caller decides).
int CtHdmaBuildNative(uint8_t* wram128, const uint8_t* rom, long romLen);
