// seq_wram.h -- the $C3 stages' 16-bit WRAM access, one body for every stage file.
//
// The stages work on the 128 KB WRAM image `w` ($7E at 0, $7F at 0x10000), little-endian, each byte's address wrapped
// to the image (`& 0x1FFFF`: a word at $7F:FFFF takes its high byte from $7E:0000). Each file keeps its own short
// wrappers -- `rd` / `wr` in the logic files, `rw` / `ww` in the picture files -- whose bodies are these, so their call
// sites read as before. Macros, not functions: the wrappers stay the one call they always were (the build's /Ob1 inlines
// only what is marked inline, and an inline function the wrapper calls would be a second one). Each argument is read
// more than once, so pass plain names, as the wrappers do.
#ifndef CT_SEQ_WRAM_H
#define CT_SEQ_WRAM_H
#include <stdint.h>

#define CT_SEQ_W16(w, a) ((unsigned)((w)[(a) & 0x1FFFF] | ((w)[((a) + 1) & 0x1FFFF] << 8)))
#define CT_SEQ_W16_PUT(w, a, v) do { (w)[(a) & 0x1FFFF] = (uint8_t)(v); (w)[((a) + 1) & 0x1FFFF] = (uint8_t)((v) >> 8); } while (0)

#endif
