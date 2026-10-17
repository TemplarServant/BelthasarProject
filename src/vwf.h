// vwf.h -- bank C2's proportional-text engine (the variable-width font), native: C2/57DF, C2/5823 and the string loop
// C2/58B2 with its states, control codes, numbers and the glyph C2/5DC4 (disasm/notes/vwf_engine.md).
#ifndef CT_VWF_H
#define CT_VWF_H

#include <stdint.h>

// Both run on the 128 KB WRAM image (`$7E` at offset 0) and keep ALL their state where the ROM keeps it: the direct page
// `$0200-$02A1` (D = $0200). The caller's block is the ROM's: `$020C` the string's id, `$020D-$020F` the long pointer to
// the strings' pointer table, `$0210-$0212` the tile strip's long address, `$0213` the glyphs a call may draw, `$0214`
// the mode (2 = a 4 bpp strip, pen from x 0; `& 0F` = 0 a 2 bpp strip; bit 7 = pen from x 0), `$02A1` the hex switch of
// the number codes. `rom` is the headerless HiROM image (bank C0 at offset 0).

// C2/57DF (`JSL C2:0003`): the string's pointer [$0D + 2 x $0C] into $31-$33, the state ($30 = 0), the pen ($34), the
// verdict ($15 = 4), the argument cursor ($3D-$3F = 00:0200) and the glyph count ($17 = 0).
void CtVwfBegin(uint8_t* wram, const uint8_t* rom, long romLen);

// C2/5823 (`JSL C2:0009`): one call -- up to `$0213` glyphs, or to a control code -- into the strip. `$15` says how it
// ended: 0 the string's end (the next call does nothing), 10 the call's quota spent, 3-0C a control code. Returns 0, or
// -1 when it met a case the ROM itself cannot take (a `12 nn` code with nn 2-7: C2/5A87's table has two entries) -- the
// call stops there.
int CtVwfRun(uint8_t* wram, const uint8_t* rom, long romLen);

#endif
