// seq_ppu.h - the sequence stages' picture drawn by the vendored LakeSnes PPU (snes/ppu.c). The native stages draw
// through seq_pic.c; this one is a reference (`ct.exe --seqppucmp`).
//
// A $C3 stage is register-driven: its NMI hooks DMA VRAM/CGRAM/OAM and write the $21xx registers, and its HDMA
// rewrites them per line (the race changes $2105 mid-frame and the mode-7 matrix on every line). The caller runs the
// stage's code and hands what it writes to this PPU. Only the hardware is here; none of the game's logic.
//
// The PPU reads four fields of its Snes (hPos, vPos, openBus, palTiming) and nothing else, so a zeroed
// shell is enough -- snes.c's bus is never involved.
#ifndef CT_SEQ_PPU_H
#define CT_SEQ_PPU_H

#include <stdint.h>

typedef struct Ppu Ppu;
typedef struct CtSeqPpu CtSeqPpu;

// One HDMA channel as the stage programmed it ($43x0-$43x7) -- the walk state is the frame's own.
typedef struct CtSeqPpuHdma {
  uint8_t  dmap;      // $43x0: bit 6 indirect, bits 0-2 the transfer mode
  uint8_t  bAdr;      // $43x1: $21xx
  uint16_t aAdr;      // $43x2/$43x3: the table
  uint8_t  aBank;     // $43x4
  uint8_t  indBank;   // $43x7
} CtSeqPpuHdma;

// A byte of the CPU's bus, as HDMA reads its tables ($7E/$7F WRAM in every stage measured).
typedef uint8_t (*CtSeqPpuRead)(void* ctx, uint32_t adr);

CtSeqPpu* CtSeqPpuCreate(void);
void      CtSeqPpuFree(CtSeqPpu* p);
Ppu*      CtSeqPpuCore(CtSeqPpu* p);        // for ppu_write and the VRAM/CGRAM/OAM a hook DMAs

// One frame, in snes.c's own order: ppu_handleFrameStart at (0,0), the HDMA init at (16,0), then for
// every line the draw at hPos 512 (lines 1..224) and the HDMA run at hPos 1104 (lines 0..224).
// `enable` is what the NMI wrote to $420C. The picture is written as 256x224 RGB888 (rgb may be NULL).
void CtSeqPpuFrame(CtSeqPpu* p, const CtSeqPpuHdma ch[8], uint8_t enable,
                   CtSeqPpuRead rd, void* ctx, uint8_t* rgb);

// The frame's line buffer, as ppu_runLine left it: row y of the 256x224 picture is (y+1)*2048 in the
// PPU's own buffer. For graders that compare against an oracle PPU's pixelBuffer byte for byte.
const uint8_t* CtSeqPpuLine(CtSeqPpu* p, int line);

#endif
