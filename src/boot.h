// boot.h -- the reset/boot path.
#ifndef CT_BOOT_H
#define CT_BOOT_H

#include "snes.h"

// Run one frame of the boot. Not ported: it copies the oracle's WRAM into g_ram.
void CtBootRunFrame(Snes* oracle);

// Hardware register-init callback: receives a 16-bit register address + value.
typedef void (*CtRegWrite)(void* ctx, uint16_t reg, uint8_t val);

// The CPU/PPU register init at $FD:C000..$C0D3: emits the ROM's write sequence through w(); drives no
// hardware itself.
void CtBootHwInit(CtRegWrite w, void* ctx);

// The WRAM part of $C0:000E..0053: install the NMI/IRQ handler trampolines in low RAM and clear the work-RAM
// regions (the ROM zero-fills by DMA through $2DF1). Operates on a 128 KB WRAM image (7E:0000.. at ram[0],
// 7F:0000.. at ram[0x10000]); does not touch the stack.
void CtBootInitMain(uint8_t* ram);

#endif
