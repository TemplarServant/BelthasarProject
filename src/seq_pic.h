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

// A STAGE's picture: the compositor's state plus what the stage's code sets around it -- the HDMA channels it programmed, HDMAEN,
// and the data ports' addresses ($2116 / $2115 VRAM, $2121 CGRAM) its DMAs go through.
typedef struct CtSeqStagePic {
  CtSeqPic p;
  CtSeqPicHdma ch[8];
  uint8_t hdmaen;           // $420C (the stage NMI writes it from $038C, C3/08FD)
  uint16_t vaddr;           // $2116
  uint8_t vmain;            // $2115
  uint8_t cgadd;            // $2121
  uint16_t joy;             // $4218 / $4219, the pad's auto-read (the race's 16-bit `SBC $4217` reads $4218 with it)
  uint8_t thread;           // where the stage's main thread waits (the stage's own numbering; 0 = its idle loop)
} CtSeqStagePic;
// A DMA from WRAM into the PPU's data ports, as a stage's code issues it ($43x0 / $43x1 = dmap / bbad, from WRAM byte `src`,
// `n` bytes; DMAP bit 3 = the fixed source): $2118 / $2119 (VRAM through the port's address and VMAIN), $2122 (CGRAM), $2104
// (OAM, from the address the stage set -- 0 in every stage measured). Reads `w` as the 128 KB WRAM ($7E at 0).
void CtSeqStagePicDma(CtSeqStagePic* sp, const uint8_t* w, uint8_t dmap, uint8_t bbad, uint32_t src, unsigned n);
// ...and `$2139 / $213A` into WRAM: VRAM read back through the port (its read buffer's one-word lag, VMAIN's step).
void CtSeqStagePicVramRead(CtSeqStagePic* sp, uint8_t* w, uint32_t dst, unsigned n);
// The same through $2139 / $213A in turn (a DMA in mode 1 from $2139: low, high, low, ...): the buffer -- loaded from the word at
// the address when $2116 was written -- refills and the address steps on the read VMAIN bit 7 names ($213A when set, $2139 when
// clear), so with VMAIN $80 the first word comes twice.
void CtSeqStagePicVramReadWords(CtSeqStagePic* sp, uint8_t* w, uint32_t dst, unsigned n);
// The stage's frame drawn from `w`'s HDMA tables.
void CtSeqStagePicFrame(CtSeqStagePic* sp, const uint8_t* w, uint8_t* rgb);


#endif
