// seq_pic.h -- THE SEQUENCE STAGES' PICTURE, native (the user's decision, 2026-10-17: a native compositor, not the vendored
// PPU -- src/seq_ppu.c stays for the translated attract stages until they go native too).
//
// A $C3 sequence stage draws by writing the display registers ($2100-$213F) in its NMI and per scanline through HDMA, and by
// filling VRAM, CGRAM and OAM. Its native port fills a CtSeqPic the same way -- the arrays directly, the registers through
// CtSeqPicWrite with the hardware's latches (the scroll and mode-7 pairs, COLDATA's channel bits) -- and CtSeqPicFrame draws
// the frame a line at a time: the HDMA tables (walked as the hardware walks them) rewrite the registers between the lines,
// and each line is composed from what they hold then. What a line shows is the PPU's: snes/ppu.c is the oracle's PPU and
// the picture it draws is the truth, so the composition follows its rules (layer order by mode, the 2/4/8 bpp fetch, the
// offset-per-tile modes, mode 7's start and step, the windows, the colour math and its half, the sprites' 32 / 34 limits) --
// written here over the register bytes, with no bus, no ports and no clock.
// disasm/notes/seq_picture.md.
#ifndef CT_SEQ_PIC_H
#define CT_SEQ_PIC_H

#include <stdint.h>

typedef struct CtSeqPic {
  uint16_t vram[0x8000];
  uint16_t cgram[256];
  uint16_t oam[256];        // the 512-byte table as 256 words (x / y, then tile / attributes)
  uint8_t  highOam[32];
  uint8_t  r[0x40];         // the last byte written to each register $2100-$213F
  uint16_t hofs[4], vofs[4];
  uint8_t  scrollPrev, scrollPrev2;
  int16_t  m7[8];           // M7A M7B M7C M7D M7X M7Y M7HOFS M7VOFS
  uint8_t  m7prev;
  uint8_t  fix[3];          // COLDATA: red, green, blue
  uint8_t  mosaicStart;     // the line the last $2106 write took effect on (1 when written in the vblank)
} CtSeqPic;

// One HDMA channel as the stage programmed it ($43x0-$43x7).
typedef struct CtSeqPicHdma {
  uint8_t  dmap;            // $43x0: bit 6 indirect, bits 0-2 the transfer mode
  uint8_t  bAdr;            // $43x1
  uint16_t aAdr;            // $43x2/$43x3
  uint8_t  aBank;           // $43x4
  uint8_t  indBank;         // $43x7
} CtSeqPicHdma;

typedef uint8_t (*CtSeqPicRead)(void* ctx, uint32_t adr);   // a byte of WRAM, as HDMA reads its tables

void CtSeqPicReset(CtSeqPic* p);
// A register write ($21xx, adr = xx), in the vblank (line 0) or by HDMA at a line's end (line = the line just drawn).
void CtSeqPicWrite(CtSeqPic* p, uint8_t adr, uint8_t val, int line);
// The frame: the HDMA's init and first transfer, then lines 1..224 each drawn and followed by its HDMA transfer. rgb is
// 256 x 224 RGB888 (may be NULL).
void CtSeqPicFrame(CtSeqPic* p, const CtSeqPicHdma ch[8], uint8_t hdmaen, CtSeqPicRead rd, void* ctx, uint8_t* rgb);

#endif
